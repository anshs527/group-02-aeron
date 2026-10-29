#include "hft/protocol.hpp"

#include <hft_sbe/MessageHeader.h>
#include <hft_sbe/NewOrder.h>
#include <hft_sbe/Ping.h>
#include <hft_sbe/Side.h>

#include <gtest/gtest.h>

#include <limits>
#include <stdexcept>
#include <vector>

namespace {
hft::NewOrder order(hft::Side side = hft::Side::buy) { return {1, 2, 3, side, 4, 5}; }
TEST(Protocol, PingPongRoundTripsAndGoldenLittleEndianHeader) {
    for (const auto kind : {hft::EchoKind::ping, hft::EchoKind::pong}) {
        for (const std::uint64_t id : {std::uint64_t{1}, std::numeric_limits<std::uint64_t>::max() - 1}) {
            auto bytes = hft::encode_echo(kind, id);
            EXPECT_EQ(hft::decode_echo(bytes, kind), id);
        }
    }
    const auto bytes = hft::encode_echo(hft::EchoKind::ping, 0x0102030405060708ULL);
    const std::vector<unsigned char> golden{8, 0, 1, 0, 0xa5, 1, 0, 0,
                                          8, 7, 6, 5, 4, 3, 2, 1};
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        EXPECT_EQ(static_cast<unsigned char>(bytes[i]), golden[i]);
    }
}
TEST(Protocol, GeneratedNewOrderCodecEnumsAndBoundaries) {
    for (const auto side : {hft::Side::buy, hft::Side::sell}) {
        for (const bool maximum : {false, true}) {
            const hft::NewOrder original{
                maximum ? std::numeric_limits<std::uint32_t>::max() - 1 : 1,
                maximum ? std::numeric_limits<std::uint64_t>::max() - 1 : 1,
                maximum ? std::numeric_limits<std::uint32_t>::max() - 1 : 1, side,
                maximum ? std::numeric_limits<std::int64_t>::max() : 1,
                maximum ? std::numeric_limits<std::uint64_t>::max() - 1 : 1};
            auto bytes = hft::encode_order(original);
            EXPECT_EQ(hft::decode_order(bytes), original);
            // Inspect actual generated accessors as well as the validated adapter.
            hft::sbe::NewOrder decoded;
            decoded.wrapForDecode(bytes.data(), 8, 33, 0, bytes.size());
            EXPECT_EQ(decoded.side(), side == hft::Side::buy ? hft::sbe::Side::Buy
                                                            : hft::sbe::Side::Sell);
            EXPECT_EQ(decoded.priceTicks(), original.price_ticks);
            EXPECT_EQ(decoded.quantity(), original.quantity);
        }
    }
}
TEST(Protocol, RejectsEveryTruncatedLengthAndOversizeBeforeDecoding) {
    auto echo = hft::encode_echo(hft::EchoKind::pong, 1);
    for (std::size_t length = 0; length < echo.size(); ++length) {
        EXPECT_THROW(hft::decode_echo(std::span<char>(echo).first(length), hft::EchoKind::pong),
                     std::invalid_argument);
    }
    auto bytes = hft::encode_order(order());
    for (std::size_t length = 0; length < bytes.size(); ++length) {
        EXPECT_THROW(hft::decode_order(std::span<char>(bytes).first(length)), std::invalid_argument);
    }
    std::vector<char> oversized(42);
    EXPECT_THROW(hft::decode_order(oversized), std::invalid_argument);
}
TEST(Protocol, RejectsHeaderMismatchWrongTemplateAndCorrelation) {
    for (const std::size_t offset : {0U, 2U, 4U, 6U}) {
        auto bytes = hft::encode_echo(hft::EchoKind::pong, 1);
        bytes[offset] ^= 0x01;
        EXPECT_THROW(hft::decode_echo(bytes, hft::EchoKind::pong), std::invalid_argument);
    }
    auto ping = hft::encode_echo(hft::EchoKind::ping, 1);
    EXPECT_THROW(hft::decode_echo(ping, hft::EchoKind::pong), std::invalid_argument);
    EXPECT_THROW(hft::require_correlation(1, 2), std::invalid_argument);
    EXPECT_NO_THROW(hft::require_correlation(2, 2));
}
TEST(Protocol, RejectsReservedIdentifiersInvalidEnumAndInvalidPrice) {
    EXPECT_THROW(hft::encode_echo(hft::EchoKind::ping, 0), std::invalid_argument);
    EXPECT_THROW(hft::encode_echo(hft::EchoKind::ping, UINT64_MAX), std::invalid_argument);
    auto value = order();
    value.quantity = UINT64_MAX;
    EXPECT_THROW(hft::encode_order(value), std::invalid_argument);
    value = order();
    value.client_id = UINT32_MAX;
    EXPECT_THROW(hft::encode_order(value), std::invalid_argument);
    value = order();
    value.price_ticks = std::numeric_limits<std::int64_t>::min();
    EXPECT_THROW(hft::encode_order(value), std::invalid_argument);
    // Generated encoders do not enforce business ranges: validate on decode too.
    for (const auto bad_price : {std::int64_t{0}, std::int64_t{-1},
                                 std::numeric_limits<std::int64_t>::min()}) {
        auto bytes = hft::encode_order(order());
        hft::sbe::NewOrder codec;
        codec.wrapForEncode(bytes.data(), 8, bytes.size()).priceTicks(bad_price);
        EXPECT_THROW(hft::decode_order(bytes), std::invalid_argument);
    }
    for (const auto bad_side : {0, 3, 255}) {
        auto bytes = hft::encode_order(order());
        bytes[24] = static_cast<char>(bad_side);
        EXPECT_THROW(hft::decode_order(bytes), std::exception);
    }
}
} // namespace
