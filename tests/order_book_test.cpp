#include "hft/order_book.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <map>
#include <random>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

namespace {
using hft::Liquidity;
using hft::OrderEvent;
using hft::RejectReason;
using hft::RequestType;
using hft::Side;
using Events = std::vector<OrderEvent>;

constexpr std::uint32_t symbol = 7;
constexpr auto u64_max = std::numeric_limits<std::uint64_t>::max();

hft::NewOrder order(std::uint32_t client, std::uint64_t id, Side side, std::int64_t price,
                    std::uint64_t quantity) {
    return {client, id, symbol, side, price, quantity};
}
hft::NewOrder buy(std::uint32_t client, std::uint64_t id, std::int64_t price,
                  std::uint64_t quantity) {
    return order(client, id, Side::buy, price, quantity);
}
hft::NewOrder sell(std::uint32_t client, std::uint64_t id, std::int64_t price,
                   std::uint64_t quantity) {
    return order(client, id, Side::sell, price, quantity);
}
hft::CancelOrder cancel_of(std::uint32_t client, std::uint64_t id, std::uint64_t orig) {
    return {client, id, orig, symbol};
}

OrderEvent accepted(const hft::NewOrder& o, std::uint64_t order_id) {
    return hft::OrderAccepted{o.client_id, o.client_order_id, order_id,  symbol,
                              o.side,      o.price_ticks,     o.quantity};
}
OrderEvent executed(std::uint32_t client, std::uint64_t id, std::uint64_t order_id, Side side,
                    std::uint64_t trade_id, std::int64_t price, std::uint64_t fill,
                    std::uint64_t leaves, Liquidity liquidity) {
    return hft::OrderExecuted{client,   id,    order_id, symbol, side,
                              trade_id, price, fill,     leaves, liquidity};
}
OrderEvent rejected(std::uint32_t client, std::uint64_t id, RequestType type, RejectReason reason) {
    return hft::OrderRejected{client, id, symbol, type, reason};
}
OrderEvent cancelled(std::uint32_t client, std::uint64_t id, std::uint64_t orig,
                     std::uint64_t order_id, std::uint64_t quantity) {
    return hft::OrderCancelled{client, id, orig, order_id, symbol, quantity};
}

class OrderBookTest : public ::testing::Test {
  protected:
    Events submit(const hft::NewOrder& o) {
        Events events;
        book.submit(o, events);
        return events;
    }
    Events cancel(const hft::CancelOrder& c) {
        Events events;
        book.cancel(c, events);
        return events;
    }
    hft::OrderBook book{symbol};
};

TEST_F(OrderBookTest, EmptyBookHasNoPricesOrOrders) {
    EXPECT_EQ(book.symbol_id(), symbol);
    EXPECT_FALSE(book.best_bid());
    EXPECT_FALSE(book.best_ask());
    EXPECT_TRUE(book.resting(Side::buy).empty());
    EXPECT_TRUE(book.resting(Side::sell).empty());
    EXPECT_EQ(book.side_quantity(Side::buy), 0U);
    EXPECT_EQ(book.side_quantity(Side::sell), 0U);
    EXPECT_EQ(book.live_order_count(), 0U);
    EXPECT_THROW(static_cast<void>(hft::OrderBook{0}), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(hft::OrderBook{std::numeric_limits<std::uint32_t>::max()}),
                 std::invalid_argument);
}

TEST_F(OrderBookTest, NonCrossingOrdersAreAcceptedAndRest) {
    const auto bid = buy(1, 1, 100, 10);
    const auto ask = sell(2, 1, 101, 5);
    EXPECT_EQ(submit(bid), Events{accepted(bid, 1)});
    EXPECT_EQ(submit(ask), Events{accepted(ask, 2)});
    EXPECT_EQ(book.best_bid(), 100);
    EXPECT_EQ(book.best_ask(), 101);
    EXPECT_EQ(book.level_quantity(Side::buy, 100), 10U);
    EXPECT_EQ(book.level_quantity(Side::sell, 101), 5U);
    EXPECT_EQ(book.level_quantity(Side::sell, 100), 0U);
    EXPECT_EQ(book.live_order(1, 1), (hft::RestingOrder{1, 1, 1, Side::buy, 100, 10, 10}));
    EXPECT_EQ(book.live_order_count(), 2U);
}

TEST_F(OrderBookTest, BestPriceTradesFirstRegardlessOfArrival) {
    submit(sell(1, 1, 103, 1));
    submit(sell(2, 1, 101, 1));
    submit(sell(3, 1, 102, 1));
    const auto first = buy(4, 1, 103, 1);
    EXPECT_EQ(
        submit(first),
        (Events{accepted(first, 4), executed(2, 1, 2, Side::sell, 1, 101, 1, 0, Liquidity::maker),
                executed(4, 1, 4, Side::buy, 1, 101, 1, 0, Liquidity::taker)}));
    const auto second = buy(4, 2, 103, 1);
    EXPECT_EQ(
        submit(second),
        (Events{accepted(second, 5), executed(3, 1, 3, Side::sell, 2, 102, 1, 0, Liquidity::maker),
                executed(4, 2, 5, Side::buy, 2, 102, 1, 0, Liquidity::taker)}));
    EXPECT_EQ(book.best_ask(), 103);
}

TEST_F(OrderBookTest, EqualPricesFillInArrivalOrder) {
    submit(sell(1, 1, 100, 5));
    submit(sell(2, 1, 100, 5));
    submit(sell(3, 1, 100, 5));
    const auto taker = buy(9, 1, 100, 12);
    EXPECT_EQ(
        submit(taker),
        (Events{accepted(taker, 4), executed(1, 1, 1, Side::sell, 1, 100, 5, 0, Liquidity::maker),
                executed(9, 1, 4, Side::buy, 1, 100, 5, 7, Liquidity::taker),
                executed(2, 1, 2, Side::sell, 2, 100, 5, 0, Liquidity::maker),
                executed(9, 1, 4, Side::buy, 2, 100, 5, 2, Liquidity::taker),
                executed(3, 1, 3, Side::sell, 3, 100, 2, 3, Liquidity::maker),
                executed(9, 1, 4, Side::buy, 3, 100, 2, 0, Liquidity::taker)}));
    EXPECT_EQ(book.resting(Side::sell),
              (std::vector<hft::RestingOrder>{{3, 1, 3, Side::sell, 100, 5, 3}}));
    EXPECT_FALSE(book.best_bid());
}

TEST_F(OrderBookTest, PartialFillKeepsQueuePosition) {
    submit(sell(1, 1, 100, 10));
    submit(sell(2, 1, 100, 10));
    submit(buy(9, 1, 100, 4));
    submit(sell(3, 1, 100, 10));
    EXPECT_EQ(book.resting(Side::sell),
              (std::vector<hft::RestingOrder>{{1, 1, 1, Side::sell, 100, 10, 6},
                                              {2, 1, 2, Side::sell, 100, 10, 10},
                                              {3, 1, 4, Side::sell, 100, 10, 10}}));
    const auto taker = buy(9, 2, 100, 8);
    EXPECT_EQ(
        submit(taker),
        (Events{accepted(taker, 5), executed(1, 1, 1, Side::sell, 2, 100, 6, 0, Liquidity::maker),
                executed(9, 2, 5, Side::buy, 2, 100, 6, 2, Liquidity::taker),
                executed(2, 1, 2, Side::sell, 3, 100, 2, 8, Liquidity::maker),
                executed(9, 2, 5, Side::buy, 3, 100, 2, 0, Liquidity::taker)}));
    EXPECT_EQ(book.level_quantity(Side::sell, 100), 18U);
}

TEST_F(OrderBookTest, AggressorSweepsLevelsAtRestingPricesAndRestsRemainder) {
    submit(sell(1, 1, 101, 3));
    submit(sell(2, 1, 102, 4));
    submit(sell(3, 1, 103, 5));
    const auto taker = buy(9, 1, 102, 10);
    EXPECT_EQ(
        submit(taker),
        (Events{accepted(taker, 4), executed(1, 1, 1, Side::sell, 1, 101, 3, 0, Liquidity::maker),
                executed(9, 1, 4, Side::buy, 1, 101, 3, 7, Liquidity::taker),
                executed(2, 1, 2, Side::sell, 2, 102, 4, 0, Liquidity::maker),
                executed(9, 1, 4, Side::buy, 2, 102, 4, 3, Liquidity::taker)}));
    EXPECT_EQ(book.best_bid(), 102);
    EXPECT_EQ(book.best_ask(), 103);
    EXPECT_EQ(book.live_order(9, 1), (hft::RestingOrder{9, 1, 4, Side::buy, 102, 10, 3}));
    EXPECT_EQ(book.side_quantity(Side::sell), 5U);
}

TEST_F(OrderBookTest, SellAggressorTradesAtRestingBidPrice) {
    submit(buy(1, 1, 105, 2));
    const auto taker = sell(2, 1, 100, 2);
    EXPECT_EQ(
        submit(taker),
        (Events{accepted(taker, 2), executed(1, 1, 1, Side::buy, 1, 105, 2, 0, Liquidity::maker),
                executed(2, 1, 2, Side::sell, 1, 105, 2, 0, Liquidity::taker)}));
    EXPECT_EQ(book.live_order_count(), 0U);
}

TEST_F(OrderBookTest, FullFillOnEntryIsAcceptedFirstAndNeverRests) {
    submit(sell(1, 1, 100, 5));
    const auto taker = buy(2, 1, 100, 5);
    const auto events = submit(taker);
    ASSERT_EQ(events.size(), 3U);
    EXPECT_EQ(events.front(), accepted(taker, 2));
    EXPECT_FALSE(book.live_order(2, 1));
    EXPECT_FALSE(book.live_order(1, 1));
    EXPECT_FALSE(book.best_bid());
    EXPECT_FALSE(book.best_ask());
    EXPECT_EQ(cancel(cancel_of(2, 2, 1)),
              Events{rejected(2, 2, RequestType::cancel_order, RejectReason::order_already_done)});
}

TEST_F(OrderBookTest, CancelRemovesLiveOrderAndEmptyLevel) {
    submit(buy(1, 1, 100, 10));
    submit(buy(1, 2, 99, 4));
    EXPECT_EQ(cancel(cancel_of(1, 3, 1)), Events{cancelled(1, 3, 1, 1, 10)});
    EXPECT_FALSE(book.live_order(1, 1));
    EXPECT_EQ(book.level_quantity(Side::buy, 100), 0U);
    EXPECT_EQ(book.best_bid(), 99);
    EXPECT_EQ(book.side_quantity(Side::buy), 4U);
}

TEST_F(OrderBookTest, CancelAfterPartialFillRemovesOnlyOpenQuantity) {
    submit(sell(1, 1, 100, 10));
    submit(buy(2, 1, 100, 4));
    EXPECT_EQ(cancel(cancel_of(1, 2, 1)), Events{cancelled(1, 2, 1, 1, 6)});
    EXPECT_EQ(book.side_quantity(Side::sell), 0U);
}

TEST_F(OrderBookTest, CancelOutcomesForDoneUnknownAndForeignOrders) {
    submit(buy(1, 1, 100, 10));
    EXPECT_EQ(cancel(cancel_of(1, 2, 1)), Events{cancelled(1, 2, 1, 1, 10)});
    EXPECT_EQ(cancel(cancel_of(1, 3, 1)),
              Events{rejected(1, 3, RequestType::cancel_order, RejectReason::order_already_done)});
    EXPECT_EQ(cancel(cancel_of(1, 4, 99)),
              Events{rejected(1, 4, RequestType::cancel_order, RejectReason::unknown_order)});
    EXPECT_EQ(cancel(cancel_of(1, 5, 2)),
              Events{rejected(1, 5, RequestType::cancel_order, RejectReason::unknown_order)});

    submit(sell(2, 1, 105, 3));
    EXPECT_EQ(cancel(cancel_of(1, 6, 1)),
              Events{rejected(1, 6, RequestType::cancel_order, RejectReason::order_already_done)});
    EXPECT_EQ(cancel(cancel_of(3, 1, 1)),
              Events{rejected(3, 1, RequestType::cancel_order, RejectReason::unknown_order)});
    EXPECT_EQ(book.live_order(2, 1), (hft::RestingOrder{2, 1, 2, Side::sell, 105, 3, 3}));
}

TEST_F(OrderBookTest, DuplicateClientOrderIdsAreRejectedWithoutSideEffects) {
    const auto original = buy(1, 1, 100, 10);
    submit(original);
    const auto before = book.resting(Side::buy);
    EXPECT_EQ(submit(buy(1, 1, 101, 3)), Events{rejected(1, 1, RequestType::new_order,
                                                         RejectReason::duplicate_client_order_id)});
    EXPECT_EQ(book.resting(Side::buy), before);

    EXPECT_EQ(
        cancel(cancel_of(1, 1, 1)),
        Events{rejected(1, 1, RequestType::cancel_order, RejectReason::duplicate_client_order_id)});
    EXPECT_EQ(book.resting(Side::buy), before);

    EXPECT_EQ(cancel(cancel_of(1, 2, 1)), Events{cancelled(1, 2, 1, 1, 10)});
    EXPECT_EQ(
        submit(buy(1, 1, 100, 10)),
        Events{rejected(1, 1, RequestType::new_order, RejectReason::duplicate_client_order_id)});
    EXPECT_EQ(
        submit(buy(1, 2, 100, 10)),
        Events{rejected(1, 2, RequestType::new_order, RejectReason::duplicate_client_order_id)});
    EXPECT_EQ(
        cancel(cancel_of(1, 2, 1)),
        Events{rejected(1, 2, RequestType::cancel_order, RejectReason::duplicate_client_order_id)});

    const auto other_client = buy(2, 1, 100, 1);
    EXPECT_EQ(submit(other_client), Events{accepted(other_client, 2)});
    const auto next = buy(1, 3, 100, 1);
    EXPECT_EQ(submit(next), Events{accepted(next, 3)});
}

TEST_F(OrderBookTest, RejectedCancelStillConsumesItsId) {
    EXPECT_EQ(cancel(cancel_of(1, 1, 50)),
              Events{rejected(1, 1, RequestType::cancel_order, RejectReason::unknown_order)});
    EXPECT_EQ(submit(buy(1, 1, 100, 1)), Events{rejected(1, 1, RequestType::new_order,
                                                         RejectReason::duplicate_client_order_id)});
}

TEST_F(OrderBookTest, AggregateOverflowIsRejectedAndLeavesBookUnchanged) {
    const auto big = buy(1, 1, 100, u64_max - 1);
    EXPECT_EQ(submit(big), Events{accepted(big, 1)});
    EXPECT_EQ(submit(buy(2, 1, 99, 2)),
              Events{rejected(2, 1, RequestType::new_order, RejectReason::invalid_quantity)});
    EXPECT_EQ(book.side_quantity(Side::buy), u64_max - 1);
    EXPECT_EQ(book.live_order_count(), 1U);

    const auto one = buy(2, 2, 99, 1);
    EXPECT_EQ(submit(one), Events{accepted(one, 2)});
    EXPECT_EQ(book.side_quantity(Side::buy), u64_max);

    submit(sell(3, 1, 101, 5));
    const auto crossing = buy(2, 3, 101, 5);
    EXPECT_EQ(submit(crossing).front(), accepted(crossing, 4));
    EXPECT_EQ(book.side_quantity(Side::buy), u64_max);
    EXPECT_EQ(submit(buy(2, 4, 101, 6)),
              Events{rejected(2, 4, RequestType::new_order, RejectReason::invalid_quantity)});

    const auto big_sell = sell(4, 1, 100, u64_max - 1);
    EXPECT_EQ(submit(big_sell).front(), accepted(big_sell, 5));
    EXPECT_EQ(book.side_quantity(Side::buy), 1U);
    EXPECT_EQ(book.side_quantity(Side::sell), 0U);
}

TEST_F(OrderBookTest, InvalidInputThrowsWithoutChangingStateOrConsumingIds) {
    submit(buy(1, 1, 100, 10));
    const auto before = book.resting(Side::buy);
    auto wrong_symbol = buy(1, 2, 100, 1);
    wrong_symbol.symbol_id = symbol + 1;
    auto bad_side = buy(1, 2, 100, 1);
    bad_side.side = static_cast<Side>(3);
    for (const auto& bad :
         {wrong_symbol, bad_side, buy(0, 2, 100, 1), buy(1, 0, 100, 1), buy(1, u64_max, 100, 1),
          buy(1, 2, 0, 1), buy(1, 2, -5, 1), buy(1, 2, 100, 0), buy(1, 2, 100, u64_max)}) {
        Events events;
        EXPECT_THROW(book.submit(bad, events), std::invalid_argument);
        EXPECT_TRUE(events.empty());
    }
    auto cancel_wrong_symbol = cancel_of(1, 2, 1);
    cancel_wrong_symbol.symbol_id = symbol + 1;
    for (const auto& bad : {cancel_wrong_symbol, cancel_of(0, 2, 1), cancel_of(1, 0, 1),
                            cancel_of(1, 2, 0), cancel_of(1, 2, u64_max)}) {
        Events events;
        EXPECT_THROW(book.cancel(bad, events), std::invalid_argument);
        EXPECT_TRUE(events.empty());
    }
    EXPECT_EQ(book.resting(Side::buy), before);
    const auto valid = buy(1, 2, 100, 1);
    EXPECT_EQ(submit(valid), Events{accepted(valid, 2)});
}

TEST_F(OrderBookTest, SelfTradeIsAllowedAndBothReportsGoToTheOwner) {
    submit(sell(1, 1, 100, 3));
    const auto taker = buy(1, 2, 100, 3);
    EXPECT_EQ(
        submit(taker),
        (Events{accepted(taker, 2), executed(1, 1, 1, Side::sell, 1, 100, 3, 0, Liquidity::maker),
                executed(1, 2, 2, Side::buy, 1, 100, 3, 0, Liquidity::taker)}));
}

using Command = std::variant<hft::NewOrder, hft::CancelOrder>;

std::vector<Command> random_commands(std::size_t count) {
    std::mt19937_64 rng(421);
    const auto pick = [&](std::uint64_t n) { return rng() % n; };
    std::map<std::uint32_t, std::uint64_t> next_id;
    std::vector<Command> commands;
    for (std::size_t i = 0; i < count; ++i) {
        const auto client = static_cast<std::uint32_t>(1 + pick(3));
        auto& next = next_id[client];
        const auto roll = pick(100);
        if (roll < 30 && next > 0) {
            const auto orig = 1 + pick(next + 2);
            commands.emplace_back(cancel_of(client, ++next, orig));
        } else {
            const auto id = roll < 33 && next > 0 ? 1 + pick(next) : ++next;
            const auto side = pick(2) == 0 ? Side::buy : Side::sell;
            commands.emplace_back(
                order(client, id, side, static_cast<std::int64_t>(95 + pick(11)), 1 + pick(20)));
        }
    }
    return commands;
}

struct OrderTrack {
    hft::NewOrder order;
    std::uint64_t filled = 0;
    std::uint64_t cancelled = 0;
};

TEST(OrderBookReconciliation, RandomCommandsReconcileAndReplayDeterministically) {
    const auto commands = random_commands(2000);
    hft::OrderBook book(symbol);
    std::vector<Events> log;
    std::map<std::pair<std::uint32_t, std::uint64_t>, OrderTrack> orders;
    std::map<std::uint64_t, std::vector<hft::OrderExecuted>> trades;
    std::uint64_t last_order_id = 0;
    std::uint64_t last_trade_id = 0;
    std::uint64_t bought = 0;
    std::uint64_t sold = 0;
    std::size_t accepted_count = 0;
    std::size_t rejected_count = 0;
    std::size_t cancelled_count = 0;

    for (const auto& command : commands) {
        Events events;
        if (const auto* o = std::get_if<hft::NewOrder>(&command)) {
            book.submit(*o, events);
            ASSERT_FALSE(events.empty());
            if (const auto* reject = std::get_if<hft::OrderRejected>(&events.front())) {
                ASSERT_EQ(events.size(), 1U);
                EXPECT_EQ(reject->client_order_id, o->client_order_id);
                EXPECT_EQ(reject->reason, RejectReason::duplicate_client_order_id);
                ++rejected_count;
            } else {
                const auto& ack = std::get<hft::OrderAccepted>(events.front());
                EXPECT_EQ(ack.client_id, o->client_id);
                EXPECT_EQ(ack.client_order_id, o->client_order_id);
                EXPECT_EQ(ack.order_id, last_order_id + 1);
                last_order_id = ack.order_id;
                orders.emplace(std::pair{o->client_id, o->client_order_id}, OrderTrack{*o});
                ++accepted_count;
                for (std::size_t i = 1; i < events.size(); ++i) {
                    ASSERT_TRUE(std::holds_alternative<hft::OrderExecuted>(events[i]));
                }
            }
        } else {
            const auto& c = std::get<hft::CancelOrder>(command);
            book.cancel(c, events);
            ASSERT_EQ(events.size(), 1U);
            if (const auto* done = std::get_if<hft::OrderCancelled>(&events.front())) {
                auto& track = orders.at({done->client_id, done->orig_client_order_id});
                EXPECT_EQ(track.order.client_id, c.client_id);
                track.cancelled += done->cancelled_quantity;
                ++cancelled_count;
            } else {
                ASSERT_TRUE(std::holds_alternative<hft::OrderRejected>(events.front()));
                ++rejected_count;
            }
        }
        for (const auto& event : events) {
            const auto* fill = std::get_if<hft::OrderExecuted>(&event);
            if (fill == nullptr) {
                continue;
            }
            auto& track = orders.at({fill->client_id, fill->client_order_id});
            track.filled += fill->fill_quantity;
            ASSERT_LE(track.filled, track.order.quantity);
            EXPECT_EQ(fill->leaves_quantity, track.order.quantity - track.filled);
            EXPECT_EQ(fill->side, track.order.side);
            if (fill->side == Side::buy) {
                EXPECT_LE(fill->fill_price_ticks, track.order.price_ticks);
                bought += fill->fill_quantity;
            } else {
                EXPECT_GE(fill->fill_price_ticks, track.order.price_ticks);
                sold += fill->fill_quantity;
            }
            if (fill->liquidity == Liquidity::maker) {
                EXPECT_EQ(fill->fill_price_ticks, track.order.price_ticks);
            }
            if (trades.find(fill->trade_id) == trades.end()) {
                EXPECT_EQ(fill->trade_id, last_trade_id + 1);
                last_trade_id = fill->trade_id;
            }
            trades[fill->trade_id].push_back(*fill);
        }
        if (book.best_bid() && book.best_ask()) {
            ASSERT_LT(*book.best_bid(), *book.best_ask());
        }
        log.push_back(std::move(events));
        if (log.size() % 100 != 0 && log.size() != commands.size()) {
            continue;
        }
        for (const auto side : {Side::buy, Side::sell}) {
            std::uint64_t total = 0;
            for (const auto& resting : book.resting(side)) {
                total += resting.leaves_quantity;
            }
            ASSERT_EQ(book.side_quantity(side), total) << "after command " << log.size();
        }
    }

    EXPECT_EQ(bought, sold);
    EXPECT_GT(bought, 0U);
    EXPECT_GT(accepted_count, 0U);
    EXPECT_GT(rejected_count, 0U);
    EXPECT_GT(cancelled_count, 0U);
    for (const auto& [trade_id, reports] : trades) {
        ASSERT_EQ(reports.size(), 2U) << trade_id;
        EXPECT_EQ(reports[0].liquidity, Liquidity::maker);
        EXPECT_EQ(reports[1].liquidity, Liquidity::taker);
        EXPECT_NE(reports[0].side, reports[1].side);
        EXPECT_EQ(reports[0].fill_price_ticks, reports[1].fill_price_ticks);
        EXPECT_EQ(reports[0].fill_quantity, reports[1].fill_quantity);
    }
    std::size_t live = 0;
    for (const auto& [key, track] : orders) {
        const auto resting = book.live_order(key.first, key.second);
        const auto leaves = resting ? resting->leaves_quantity : 0;
        if (resting) {
            ++live;
        }
        EXPECT_EQ(track.filled + track.cancelled + leaves, track.order.quantity);
    }
    EXPECT_EQ(live, book.live_order_count());

    hft::OrderBook replay(symbol);
    for (std::size_t i = 0; i < commands.size(); ++i) {
        Events events;
        if (const auto* o = std::get_if<hft::NewOrder>(&commands[i])) {
            replay.submit(*o, events);
        } else {
            replay.cancel(std::get<hft::CancelOrder>(commands[i]), events);
        }
        ASSERT_EQ(events, log[i]) << "command " << i;
    }
    EXPECT_EQ(replay.resting(Side::buy), book.resting(Side::buy));
    EXPECT_EQ(replay.resting(Side::sell), book.resting(Side::sell));
}
} // namespace
