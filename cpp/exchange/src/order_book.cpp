#include "hft/order_book.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace hft {
namespace {
constexpr auto u32_max = std::numeric_limits<std::uint32_t>::max();
constexpr auto u64_max = std::numeric_limits<std::uint64_t>::max();

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::invalid_argument(message);
    }
}

bool valid_side(Side side) { return side == Side::buy || side == Side::sell; }

bool crosses(Side taker_side, std::int64_t level_price, std::int64_t limit) {
    return taker_side == Side::buy ? level_price <= limit : level_price >= limit;
}

template <typename Ladder>
std::uint64_t unmatched(const Ladder& opposite, Side side, std::int64_t limit,
                        std::uint64_t quantity) {
    auto remaining = quantity;
    for (const auto& [price, level] : opposite) {
        if (remaining == 0 || !crosses(side, price, limit)) {
            break;
        }
        remaining -= std::min(remaining, level.quantity);
    }
    return remaining;
}

template <typename Ladder, typename Order> auto rest(Ladder& ladder, const Order& order) {
    auto& level = ladder[order.price_ticks];
    level.quantity += order.leaves_quantity;
    return level.orders.insert(level.orders.end(), order);
}

template <typename Ladder, typename Iterator>
void erase(Ladder& ladder, std::uint64_t& side_quantity, Iterator position) {
    const auto level = ladder.find(position->price_ticks);
    level->second.quantity -= position->leaves_quantity;
    side_quantity -= position->leaves_quantity;
    level->second.orders.erase(position);
    if (level->second.orders.empty()) {
        ladder.erase(level);
    }
}

template <typename Ladder>
std::uint64_t quantity_at(const Ladder& ladder, std::int64_t price_ticks) {
    const auto level = ladder.find(price_ticks);
    return level == ladder.end() ? 0 : level->second.quantity;
}

template <typename Ladder> void append(const Ladder& ladder, std::vector<RestingOrder>& out) {
    for (const auto& [price, level] : ladder) {
        out.insert(out.end(), level.orders.begin(), level.orders.end());
    }
}
} // namespace

std::size_t OrderBook::KeyHash::operator()(const Key& key) const noexcept {
    auto hash = std::hash<std::uint64_t>{}(key.client_order_id);
    hash ^= std::hash<std::uint32_t>{}(key.client_id) +
            static_cast<std::size_t>(0x9e3779b97f4a7c15ULL) + (hash << 6) + (hash >> 2);
    return hash;
}

OrderBook::OrderBook(std::uint32_t symbol_id) : symbol_id_(symbol_id) {
    require(symbol_id != 0 && symbol_id != u32_max, "order book: invalid symbol");
}

void OrderBook::submit(const NewOrder& order, std::vector<OrderEvent>& events) {
    require(order.symbol_id == symbol_id_, "order book: symbol mismatch");
    require(order.client_id != 0 && order.client_id != u32_max, "order book: invalid client");
    require(order.client_order_id != 0 && order.client_order_id != u64_max,
            "order book: invalid client order id");
    require(valid_side(order.side), "order book: invalid side");
    require(order.price_ticks > 0, "order book: invalid price");
    require(order.quantity != 0 && order.quantity != u64_max, "order book: invalid quantity");

    const Key key{order.client_id, order.client_order_id};
    if (!used_ids_.insert(key).second) {
        events.emplace_back(
            reject(key, RequestType::new_order, RejectReason::duplicate_client_order_id));
        return;
    }
    const bool buy = order.side == Side::buy;
    const auto remainder = buy ? unmatched(asks_, order.side, order.price_ticks, order.quantity)
                               : unmatched(bids_, order.side, order.price_ticks, order.quantity);
    const auto same_side_quantity = buy ? bid_quantity_ : ask_quantity_;
    if (remainder > u64_max - same_side_quantity) {
        events.emplace_back(reject(key, RequestType::new_order, RejectReason::invalid_quantity));
        return;
    }

    RestingOrder taker{order.client_id,   order.client_order_id, next_order_id_++, order.side,
                       order.price_ticks, order.quantity,        order.quantity};
    events.emplace_back(OrderAccepted{taker.client_id, taker.client_order_id, taker.order_id,
                                      symbol_id_, taker.side, taker.price_ticks, taker.quantity});
    if (buy) {
        match(asks_, ask_quantity_, taker, events);
    } else {
        match(bids_, bid_quantity_, taker, events);
    }
    if (taker.leaves_quantity == 0) {
        orders_.emplace(key, Record{false, {}});
        return;
    }
    const auto position = buy ? rest(bids_, taker) : rest(asks_, taker);
    (buy ? bid_quantity_ : ask_quantity_) += taker.leaves_quantity;
    orders_.emplace(key, Record{true, position});
}

void OrderBook::cancel(const CancelOrder& request, std::vector<OrderEvent>& events) {
    require(request.symbol_id == symbol_id_, "order book: symbol mismatch");
    require(request.client_id != 0 && request.client_id != u32_max, "order book: invalid client");
    require(request.client_order_id != 0 && request.client_order_id != u64_max,
            "order book: invalid client order id");
    require(request.orig_client_order_id != 0 && request.orig_client_order_id != u64_max,
            "order book: invalid original client order id");

    const Key key{request.client_id, request.client_order_id};
    if (!used_ids_.insert(key).second) {
        events.emplace_back(
            reject(key, RequestType::cancel_order, RejectReason::duplicate_client_order_id));
        return;
    }
    const auto found = orders_.find(Key{request.client_id, request.orig_client_order_id});
    if (found == orders_.end()) {
        events.emplace_back(reject(key, RequestType::cancel_order, RejectReason::unknown_order));
        return;
    }
    auto& record = found->second;
    if (!record.live) {
        events.emplace_back(
            reject(key, RequestType::cancel_order, RejectReason::order_already_done));
        return;
    }
    const auto target = *record.position;
    if (target.side == Side::buy) {
        erase(bids_, bid_quantity_, record.position);
    } else {
        erase(asks_, ask_quantity_, record.position);
    }
    record = Record{false, {}};
    events.emplace_back(OrderCancelled{request.client_id, request.client_order_id,
                                       request.orig_client_order_id, target.order_id, symbol_id_,
                                       target.leaves_quantity});
}

template <typename Ladder>
void OrderBook::match(Ladder& opposite, std::uint64_t& opposite_quantity, RestingOrder& taker,
                      std::vector<OrderEvent>& events) {
    while (taker.leaves_quantity > 0 && !opposite.empty()) {
        const auto level = opposite.begin();
        if (!crosses(taker.side, level->first, taker.price_ticks)) {
            break;
        }
        auto& maker = level->second.orders.front();
        const auto fill = std::min(taker.leaves_quantity, maker.leaves_quantity);
        const auto trade_id = next_trade_id_++;
        maker.leaves_quantity -= fill;
        taker.leaves_quantity -= fill;
        level->second.quantity -= fill;
        opposite_quantity -= fill;
        events.emplace_back(OrderExecuted{maker.client_id, maker.client_order_id, maker.order_id,
                                          symbol_id_, maker.side, trade_id, level->first, fill,
                                          maker.leaves_quantity, Liquidity::maker});
        events.emplace_back(OrderExecuted{taker.client_id, taker.client_order_id, taker.order_id,
                                          symbol_id_, taker.side, trade_id, level->first, fill,
                                          taker.leaves_quantity, Liquidity::taker});
        if (maker.leaves_quantity == 0) {
            orders_.at(Key{maker.client_id, maker.client_order_id}) = Record{false, {}};
            level->second.orders.pop_front();
            if (level->second.orders.empty()) {
                opposite.erase(level);
            }
        }
    }
}

OrderRejected OrderBook::reject(const Key& key, RequestType type, RejectReason reason) const {
    return OrderRejected{key.client_id, key.client_order_id, symbol_id_, type, reason};
}

std::optional<std::int64_t> OrderBook::best_bid() const {
    if (bids_.empty()) {
        return std::nullopt;
    }
    return bids_.begin()->first;
}

std::optional<std::int64_t> OrderBook::best_ask() const {
    if (asks_.empty()) {
        return std::nullopt;
    }
    return asks_.begin()->first;
}

std::uint64_t OrderBook::level_quantity(Side side, std::int64_t price_ticks) const {
    return side == Side::buy ? quantity_at(bids_, price_ticks) : quantity_at(asks_, price_ticks);
}

std::uint64_t OrderBook::side_quantity(Side side) const {
    return side == Side::buy ? bid_quantity_ : ask_quantity_;
}

std::vector<RestingOrder> OrderBook::resting(Side side) const {
    std::vector<RestingOrder> out;
    if (side == Side::buy) {
        append(bids_, out);
    } else {
        append(asks_, out);
    }
    return out;
}

std::optional<RestingOrder> OrderBook::live_order(std::uint32_t client_id,
                                                  std::uint64_t client_order_id) const {
    const auto found = orders_.find(Key{client_id, client_order_id});
    if (found == orders_.end() || !found->second.live) {
        return std::nullopt;
    }
    return *found->second.position;
}

std::size_t OrderBook::live_order_count() const {
    std::size_t count = 0;
    for (const auto& [price, level] : bids_) {
        count += level.orders.size();
    }
    for (const auto& [price, level] : asks_) {
        count += level.orders.size();
    }
    return count;
}
} // namespace hft
