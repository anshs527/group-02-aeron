#pragma once

#include "hft/protocol.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

namespace hft {
using OrderEvent = std::variant<OrderAccepted, OrderRejected, OrderCancelled, OrderExecuted>;

struct RestingOrder {
    std::uint32_t client_id;
    std::uint64_t client_order_id;
    std::uint64_t order_id;
    Side side;
    std::int64_t price_ticks;
    std::uint64_t quantity;
    std::uint64_t leaves_quantity;
    bool operator==(const RestingOrder&) const = default;
};

class OrderBook {
  public:
    explicit OrderBook(std::uint32_t symbol_id);

    void submit(const NewOrder& order, std::vector<OrderEvent>& events);
    void cancel(const CancelOrder& request, std::vector<OrderEvent>& events);

    std::uint32_t symbol_id() const { return symbol_id_; }
    std::optional<std::int64_t> best_bid() const;
    std::optional<std::int64_t> best_ask() const;
    std::uint64_t level_quantity(Side side, std::int64_t price_ticks) const;
    std::uint64_t side_quantity(Side side) const;
    std::vector<RestingOrder> resting(Side side) const;
    std::optional<RestingOrder> live_order(std::uint32_t client_id,
                                           std::uint64_t client_order_id) const;
    std::size_t live_order_count() const;

  private:
    struct Key {
        std::uint32_t client_id;
        std::uint64_t client_order_id;
        bool operator==(const Key&) const = default;
    };
    struct KeyHash {
        std::size_t operator()(const Key& key) const noexcept;
    };
    using Queue = std::list<RestingOrder>;
    struct Level {
        Queue orders;
        std::uint64_t quantity = 0;
    };
    using Bids = std::map<std::int64_t, Level, std::greater<>>;
    using Asks = std::map<std::int64_t, Level, std::less<>>;
    struct Record {
        bool live;
        Queue::iterator position;
    };

    template <typename Ladder>
    void match(Ladder& opposite, std::uint64_t& opposite_quantity, RestingOrder& taker,
               std::vector<OrderEvent>& events);
    OrderRejected reject(const Key& key, RequestType type, RejectReason reason) const;

    std::uint32_t symbol_id_;
    Bids bids_;
    Asks asks_;
    std::uint64_t bid_quantity_ = 0;
    std::uint64_t ask_quantity_ = 0;
    std::uint64_t next_order_id_ = 1;
    std::uint64_t next_trade_id_ = 1;
    std::unordered_set<Key, KeyHash> used_ids_;
    std::unordered_map<Key, Record, KeyHash> orders_;
};
} // namespace hft
