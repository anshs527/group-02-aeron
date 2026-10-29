#include "hft/options.hpp"
#include "hft/retry.hpp"
#include "hft/statistics.hpp"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <limits>
#include <string>
#include <vector>

namespace {
hft::Options parse(std::vector<std::string> args) {
    std::vector<char*> argv;
    for (auto& arg : args) {
        argv.push_back(arg.data());
    }
    return hft::parse_options(static_cast<int>(argv.size()), argv.data());
}
TEST(Options, RequiresExplicitAbsoluteDirectory) {
    EXPECT_THROW(parse({"ping"}), std::invalid_argument);
    EXPECT_THROW(parse({"ping", "--aeron-dir", "relative"}), std::invalid_argument);
    const auto opts = parse({"ping", "--aeron-dir", "/tmp/test", "--warmup", "0", "--messages", "3"});
    EXPECT_EQ(opts.total(), 3U);
    EXPECT_NE(opts.request_stream, opts.response_stream);
}
TEST(Options, RejectsInvalidArgumentsAndOverflow) {
    for (const auto* value : {"0", "-1", "+1", "2x", "18446744073709551616", "10000001"}) {
        EXPECT_THROW(parse({"ping", "--aeron-dir", "/tmp/test", "--messages", value}),
                     std::invalid_argument);
    }
    EXPECT_THROW(parse({"ping", "--aeron-dir"}), std::invalid_argument);
    EXPECT_THROW(parse({"ping", "--unknown", "x"}), std::invalid_argument);
    EXPECT_THROW(parse({"ping", "--aeron-dir", "/tmp/test", "--startup-timeout-ms", "0"}),
                 std::invalid_argument);
    EXPECT_THROW(parse({"ping", "--aeron-dir", "/tmp/test", "--response-timeout-ms", "60001"}),
                 std::invalid_argument);
    EXPECT_THROW(parse({"ping", "--aeron-dir", "/tmp/test", "--request-stream", "1002"}),
                 std::invalid_argument);
}
TEST(Publication, RetriesTemporaryStatusesUntilAccepted) {
    const std::array<std::int64_t, 4> statuses{-1, -2, -3, 64};
    std::size_t attempts = 0;
    hft::offer_until([&] { return statuses.at(attempts++); },
                     hft::Clock::now() + std::chrono::seconds(1));
    EXPECT_EQ(attempts, statuses.size());
}
TEST(Publication, FailsImmediatelyOnFatalOrUnknownStatus) {
    for (const std::int64_t status : {-4, -5, -6, -99}) {
        int attempts = 0;
        EXPECT_THROW(hft::offer_until([&] { ++attempts; return status; },
                                      hft::Clock::now() + std::chrono::seconds(1)),
                     std::runtime_error);
        EXPECT_EQ(attempts, 1);
    }
}
TEST(Publication, DeadlinePreventsOfferingAndPersistentBackpressureTimesOut) {
    bool called = false;
    EXPECT_THROW(hft::offer_until([&] { called = true; return 64; }, hft::Clock::now()),
                 std::runtime_error);
    EXPECT_FALSE(called);
    EXPECT_THROW(hft::offer_until([] { return -2; },
                                  hft::Clock::now() + std::chrono::milliseconds(1)),
                 std::runtime_error);
}
TEST(Statistics, ComputesBasicStatisticsWithoutIntegerSumOverflow) {
    hft::RttStatistics stats;
    EXPECT_THROW(stats.mean(), std::logic_error);
    EXPECT_THROW(stats.min(), std::logic_error);
    stats.add(10);
    stats.add(20);
    stats.add(60);
    EXPECT_EQ(stats.count(), 3U);
    EXPECT_EQ(stats.min(), 10U);
    EXPECT_EQ(stats.max(), 60U);
    EXPECT_EQ(stats.mean(), 30.0L);
    hft::RttStatistics large;
    large.add(std::numeric_limits<std::uint64_t>::max());
    large.add(std::numeric_limits<std::uint64_t>::max());
    EXPECT_EQ(large.mean(), static_cast<long double>(std::numeric_limits<std::uint64_t>::max()));
}
} // namespace
