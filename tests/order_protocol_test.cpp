#include "hft/protocol.hpp"

#include <hft_sbe/MessageHeader.h>
#include <hft_sbe/OrderExecuted.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

// Order protocol contract tests (docs/protocol.md sections 4, 7 and 10).
namespace {
using Bytes = std::vector<char>;
constexpr auto u32_max = std::numeric_limits<std::uint32_t>::max();
constexpr auto u64_max = std::numeric_limits<std::uint64_t>::max();
constexpr auto i64_max = std::numeric_limits<std::int64_t>::max();
constexpr auto i64_min = std::numeric_limits<std::int64_t>::min();

void put_le(std::span<char> bytes, std::size_t offset, std::size_t width, std::uint64_t value) {
    for (std::size_t i = 0; i < width; ++i) {
        bytes[offset + i] =
            static_cast<char>(static_cast<unsigned char>((value >> (8 * i)) & 0xffU));
    }
}
std::uint16_t get_u16(std::span<const char> bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(
        static_cast<unsigned>(static_cast<unsigned char>(bytes[offset])) |
        (static_cast<unsigned>(static_cast<unsigned char>(bytes[offset + 1])) << 8U));
}
void set_block_length(std::span<char> bytes, std::size_t value) { put_le(bytes, 0, 2, value); }
void set_template(std::span<char> bytes, std::uint16_t value) { put_le(bytes, 2, 2, value); }
void set_schema(std::span<char> bytes, std::uint16_t value) { put_le(bytes, 4, 2, value); }
void set_version(std::span<char> bytes, std::uint16_t value) { put_le(bytes, 6, 2, value); }

const hft::NewOrder new_order{1, 2, 3, hft::Side::buy, 4, 5};
const hft::CancelOrder cancel_order{7, 8, 9, 10};
const hft::OrderAccepted order_accepted{1, 2, 3, 4, hft::Side::sell, 5, 6};
const hft::OrderRejected order_rejected{1, 2, 3U, hft::RequestType::new_order,
                                        hft::RejectReason::invalid_price};
const hft::OrderCancelled order_cancelled{1, 2, 3, 4, 5, 6};
const hft::OrderExecuted order_executed{1, 2, 3, 4, hft::Side::buy,
                                        5, 6, 7, 0, hft::Liquidity::maker};

// One row per order template: a valid encoded frame and a decode check.
struct Codec {
    std::string name;
    std::uint16_t template_id;
    std::uint16_t min_version;
    std::size_t block;
    Bytes frame;
    std::function<bool(std::span<char>)> decodes_to_original;
};
template <typename Frame, typename Value, typename Decode>
Codec make(std::string name, hft::TemplateId id, std::uint16_t min, const Frame& frame,
           const Value& original, Decode decode) {
    return {std::move(name),
            static_cast<std::uint16_t>(id),
            min,
            frame.size() - hft::header_size,
            Bytes(frame.begin(), frame.end()),
            [original, decode](std::span<char> bytes) { return decode(bytes) == original; }};
}
std::vector<Codec> codecs() {
    return {
        make("NewOrder", hft::TemplateId::new_order, hft::new_order_min_version,
             hft::encode_order(new_order), new_order, &hft::decode_order),
        make("CancelOrder", hft::TemplateId::cancel_order, hft::order_message_min_version,
             hft::encode_cancel_order(cancel_order), cancel_order, &hft::decode_cancel_order),
        make("OrderAccepted", hft::TemplateId::order_accepted, hft::order_message_min_version,
             hft::encode_order_accepted(order_accepted), order_accepted,
             &hft::decode_order_accepted),
        make("OrderRejected", hft::TemplateId::order_rejected, hft::order_message_min_version,
             hft::encode_order_rejected(order_rejected), order_rejected,
             &hft::decode_order_rejected),
        make("OrderCancelled", hft::TemplateId::order_cancelled, hft::order_message_min_version,
             hft::encode_order_cancelled(order_cancelled), order_cancelled,
             &hft::decode_order_cancelled),
        make("OrderExecuted", hft::TemplateId::order_executed, hft::order_message_min_version,
             hft::encode_order_executed(order_executed), order_executed,
             &hft::decode_order_executed),
    };
}

TEST(OrderProtocol, EncodedHeadersCarrySchemaVersionTemplateAndBlockLength) {
    const std::vector<std::pair<std::uint16_t, std::size_t>> expected{{3, 33}, {4, 24}, {5, 41},
                                                                      {6, 18}, {7, 40}, {8, 58}};
    const auto rows = codecs();
    ASSERT_EQ(rows.size(), expected.size());
    for (std::size_t i = 0; i < rows.size(); ++i) {
        SCOPED_TRACE(rows[i].name);
        const auto& frame = rows[i].frame;
        EXPECT_EQ(get_u16(frame, 0), expected[i].second);
        EXPECT_EQ(get_u16(frame, 2), expected[i].first);
        EXPECT_EQ(get_u16(frame, 4), 421);
        EXPECT_EQ(get_u16(frame, 6), 1);
        EXPECT_EQ(frame.size(), hft::header_size + expected[i].second);
    }
}

TEST(OrderProtocol, RoundTripsEveryMessageAtMinimumAndMaximumValues) {
    for (const bool maximum : {false, true}) {
        SCOPED_TRACE(maximum ? "maximum" : "minimum");
        const std::uint32_t id32 = maximum ? u32_max - 1 : 1;
        const std::uint64_t id64 = maximum ? u64_max - 1 : 1;
        const std::int64_t price = maximum ? i64_max : 1;
        const std::uint64_t leaves = maximum ? u64_max - 1 : 0;

        const hft::CancelOrder cancel{id32, id64, id64, id32};
        auto cancel_bytes = hft::encode_cancel_order(cancel);
        EXPECT_EQ(hft::decode_cancel_order(cancel_bytes), cancel);

        const hft::OrderCancelled cancelled{id32, id64, id64, id64, id32, id64};
        auto cancelled_bytes = hft::encode_order_cancelled(cancelled);
        EXPECT_EQ(hft::decode_order_cancelled(cancelled_bytes), cancelled);

        for (const auto side : {hft::Side::buy, hft::Side::sell}) {
            const hft::OrderAccepted accepted{id32, id64, id64, id32, side, price, id64};
            auto accepted_bytes = hft::encode_order_accepted(accepted);
            EXPECT_EQ(hft::decode_order_accepted(accepted_bytes), accepted);
            for (const auto liquidity : {hft::Liquidity::maker, hft::Liquidity::taker}) {
                const hft::OrderExecuted executed{id32, id64,  id64, id32,   side,
                                                  id64, price, id64, leaves, liquidity};
                auto executed_bytes = hft::encode_order_executed(executed);
                EXPECT_EQ(hft::decode_order_executed(executed_bytes), executed);
            }
        }
        for (const auto request : {hft::RequestType::new_order, hft::RequestType::cancel_order}) {
            for (std::uint8_t code = 1; code <= 12; ++code) {
                for (const auto symbol :
                     {std::optional<std::uint32_t>{id32}, std::optional<std::uint32_t>{}}) {
                    const hft::OrderRejected rejected{id32, id64, symbol, request,
                                                      static_cast<hft::RejectReason>(code)};
                    auto rejected_bytes = hft::encode_order_rejected(rejected);
                    EXPECT_EQ(hft::decode_order_rejected(rejected_bytes), rejected);
                }
            }
        }
    }
}

TEST(OrderProtocol, GoldenLittleEndianOrderExecutedFrame) {
    const hft::OrderExecuted event{
        0x01020304U,           0x1112131415161718ULL, 0x2122232425262728ULL, 0x31323334U,
        hft::Side::sell,       0x4142434445464748ULL, 0x5152535455565758LL,  0x6162636465666768ULL,
        0x7172737475767778ULL, hft::Liquidity::taker};
    const std::vector<unsigned char> golden{
        0x3a, 0x00, 0x08, 0x00, 0xa5, 0x01, 0x01, 0x00, // header: 58, 8, 421, 1
        0x04, 0x03, 0x02, 0x01,                         // clientId
        0x18, 0x17, 0x16, 0x15, 0x14, 0x13, 0x12, 0x11, // clientOrderId
        0x28, 0x27, 0x26, 0x25, 0x24, 0x23, 0x22, 0x21, // orderId
        0x34, 0x33, 0x32, 0x31,                         // symbolId
        0x02,                                           // side Sell
        0x48, 0x47, 0x46, 0x45, 0x44, 0x43, 0x42, 0x41, // tradeId
        0x58, 0x57, 0x56, 0x55, 0x54, 0x53, 0x52, 0x51, // fillPriceTicks
        0x68, 0x67, 0x66, 0x65, 0x64, 0x63, 0x62, 0x61, // fillQuantity
        0x78, 0x77, 0x76, 0x75, 0x74, 0x73, 0x72, 0x71, // leavesQuantity
        0x02};                                          // liquidity Taker
    auto bytes = hft::encode_order_executed(event);
    ASSERT_EQ(bytes.size(), golden.size());
    for (std::size_t i = 0; i < golden.size(); ++i) {
        EXPECT_EQ(static_cast<unsigned char>(bytes[i]), golden[i]) << "byte " << i;
    }
    Bytes from_golden(golden.begin(), golden.end());
    EXPECT_EQ(hft::decode_order_executed(from_golden), event);
    // Inspect the actual generated accessors as well as the validated adapter.
    hft::sbe::OrderExecuted generated;
    generated.wrapForDecode(bytes.data(), hft::header_size, 58, 1, bytes.size());
    EXPECT_EQ(generated.liquidity(), hft::sbe::Liquidity::Taker);
    EXPECT_EQ(generated.leavesQuantity(), event.leaves_quantity);
}

TEST(OrderProtocol, RejectsEveryTruncatedLengthAndOversizedFrame) {
    for (auto& codec : codecs()) {
        SCOPED_TRACE(codec.name);
        for (std::size_t length = 0; length < codec.frame.size(); ++length) {
            EXPECT_THROW(codec.decodes_to_original(std::span<char>(codec.frame).first(length)),
                         std::invalid_argument)
                << "length " << length;
        }
        for (const std::size_t extra : {1U, 8U}) {
            auto oversized = codec.frame;
            oversized.resize(oversized.size() + extra, 0);
            EXPECT_THROW(codec.decodes_to_original(oversized), std::invalid_argument);
        }
        Bytes zeros(codec.frame.size() + 1, 0);
        EXPECT_THROW(codec.decodes_to_original(zeros), std::invalid_argument);
        EXPECT_TRUE(codec.decodes_to_original(codec.frame));
    }
}

TEST(OrderProtocol, RejectsCorruptionOfEachHeaderField) {
    for (const auto& codec : codecs()) {
        SCOPED_TRACE(codec.name);
        // blockLength: disagrees with the frame length, or shorter than known.
        for (const std::size_t block : {codec.block - 1, codec.block + 1, std::size_t{0}}) {
            auto bytes = codec.frame;
            set_block_length(bytes, block);
            EXPECT_THROW(codec.decodes_to_original(bytes), std::invalid_argument);
        }
        // templateId: every other known template, zero, unknown and null.
        for (const int id : {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0xffff}) {
            if (id == codec.template_id) {
                continue;
            }
            auto bytes = codec.frame;
            set_template(bytes, static_cast<std::uint16_t>(id));
            EXPECT_THROW(codec.decodes_to_original(bytes), std::invalid_argument) << id;
        }
        // schemaId.
        for (const int schema : {0, 420, 422, 0xffff}) {
            auto bytes = codec.frame;
            set_schema(bytes, static_cast<std::uint16_t>(schema));
            EXPECT_THROW(codec.decodes_to_original(bytes), std::invalid_argument) << schema;
        }
        // version: below the template's minimum is rejected. NewOrder's minimum
        // is 0, so no version is below it; its version-0 case is accepted below.
        for (std::uint16_t version = 0; version < codec.min_version; ++version) {
            auto bytes = codec.frame;
            set_version(bytes, version);
            EXPECT_THROW(codec.decodes_to_original(bytes), std::invalid_argument) << version;
        }
    }
}

struct EnumField {
    std::size_t codec;
    std::size_t frame_offset;
    std::set<int> valid;
};
TEST(OrderProtocol, RejectsEveryInvalidEnumValueIncludingNull) {
    const auto rows = codecs();
    const std::vector<EnumField> fields{
        {0, 24, {1, 2}},                                  // NewOrder side
        {2, 32, {1, 2}},                                  // OrderAccepted side
        {3, 24, {1, 2}},                                  // OrderRejected requestType
        {3, 25, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12}}, // OrderRejected rejectReason
        {5, 32, {1, 2}},                                  // OrderExecuted side
        {5, 65, {1, 2}},                                  // OrderExecuted liquidity
    };
    for (const auto& field : fields) {
        const auto& codec = rows[field.codec];
        SCOPED_TRACE(codec.name + " offset " + std::to_string(field.frame_offset));
        for (int value = 0; value <= 255; ++value) {
            auto bytes = codec.frame;
            bytes[field.frame_offset] = static_cast<char>(static_cast<unsigned char>(value));
            if (field.valid.count(value) != 0) {
                EXPECT_NO_THROW(codec.decodes_to_original(bytes)) << value;
            } else {
                EXPECT_THROW(codec.decodes_to_original(bytes), std::invalid_argument) << value;
            }
        }
    }
}

struct IntegerField {
    std::size_t codec;
    std::size_t frame_offset;
    std::size_t width;
    bool zero_valid;
    bool null_valid;
    bool is_price;
};
TEST(OrderProtocol, RejectsZeroNullAndNonPositiveIntegerFields) {
    const auto rows = codecs();
    const std::vector<IntegerField> fields{
        {1, 8, 4, false, false, false},
        {1, 12, 8, false, false, false},
        {1, 20, 8, false, false, false},
        {1, 28, 4, false, false, false},
        {2, 8, 4, false, false, false},
        {2, 12, 8, false, false, false},
        {2, 20, 8, false, false, false},
        {2, 28, 4, false, false, false},
        {2, 33, 8, false, false, true},
        {2, 41, 8, false, false, false},
        {3, 8, 4, false, false, false},
        {3, 12, 8, false, false, false},
        {3, 20, 4, false, true, false}, // OrderRejected symbolId is optional
        {4, 8, 4, false, false, false},
        {4, 12, 8, false, false, false},
        {4, 20, 8, false, false, false},
        {4, 28, 8, false, false, false},
        {4, 36, 4, false, false, false},
        {4, 40, 8, false, false, false},
        {5, 8, 4, false, false, false},
        {5, 12, 8, false, false, false},
        {5, 20, 8, false, false, false},
        {5, 28, 4, false, false, false},
        {5, 33, 8, false, false, false},
        {5, 41, 8, false, false, true},
        {5, 49, 8, false, false, false},
        {5, 57, 8, true, false, false}, // OrderExecuted leavesQuantity may be zero
    };
    for (const auto& field : fields) {
        const auto& codec = rows[field.codec];
        SCOPED_TRACE(codec.name + " offset " + std::to_string(field.frame_offset));
        const std::uint64_t all_ones = field.width == 4 ? u32_max : u64_max;
        auto zero = codec.frame;
        put_le(zero, field.frame_offset, field.width, 0);
        auto null = codec.frame;
        put_le(null, field.frame_offset, field.width, all_ones);
        if (field.zero_valid) {
            EXPECT_NO_THROW(codec.decodes_to_original(zero));
        } else {
            EXPECT_THROW(codec.decodes_to_original(zero), std::invalid_argument);
        }
        if (field.null_valid) {
            EXPECT_NO_THROW(codec.decodes_to_original(null));
        } else {
            // For prices all-ones is -1; for unsigned fields it is the SBE null.
            EXPECT_THROW(codec.decodes_to_original(null), std::invalid_argument);
        }
        if (field.is_price) {
            auto minimum = codec.frame;
            put_le(minimum, field.frame_offset, 8, static_cast<std::uint64_t>(i64_min));
            EXPECT_THROW(codec.decodes_to_original(minimum), std::invalid_argument);
        }
    }
    // The optional symbol decodes as absent, not as a value.
    auto null_symbol = hft::encode_order_rejected(order_rejected);
    put_le(null_symbol, 20, 4, u32_max);
    EXPECT_EQ(hft::decode_order_rejected(null_symbol).symbol_id, std::nullopt);
}

TEST(OrderProtocol, EncodersRejectInvalidValuesBeforeWriting) {
    auto cancel = cancel_order;
    cancel.orig_client_order_id = 0;
    EXPECT_THROW(hft::encode_cancel_order(cancel), std::invalid_argument);
    cancel = cancel_order;
    cancel.symbol_id = u32_max;
    EXPECT_THROW(hft::encode_cancel_order(cancel), std::invalid_argument);

    auto accepted = order_accepted;
    accepted.order_id = u64_max;
    EXPECT_THROW(hft::encode_order_accepted(accepted), std::invalid_argument);
    accepted = order_accepted;
    accepted.side = static_cast<hft::Side>(3);
    EXPECT_THROW(hft::encode_order_accepted(accepted), std::invalid_argument);
    accepted = order_accepted;
    accepted.price_ticks = 0;
    EXPECT_THROW(hft::encode_order_accepted(accepted), std::invalid_argument);

    auto rejected = order_rejected;
    rejected.symbol_id = 0U;
    EXPECT_THROW(hft::encode_order_rejected(rejected), std::invalid_argument);
    for (const int code : {0, 13, 255}) {
        rejected = order_rejected;
        rejected.reason = static_cast<hft::RejectReason>(static_cast<std::uint8_t>(code));
        EXPECT_THROW(hft::encode_order_rejected(rejected), std::invalid_argument);
    }
    rejected = order_rejected;
    rejected.request_type = static_cast<hft::RequestType>(3);
    EXPECT_THROW(hft::encode_order_rejected(rejected), std::invalid_argument);

    auto cancelled = order_cancelled;
    cancelled.cancelled_quantity = 0;
    EXPECT_THROW(hft::encode_order_cancelled(cancelled), std::invalid_argument);

    auto executed = order_executed;
    executed.leaves_quantity = u64_max;
    EXPECT_THROW(hft::encode_order_executed(executed), std::invalid_argument);
    executed = order_executed;
    executed.fill_quantity = 0;
    EXPECT_THROW(hft::encode_order_executed(executed), std::invalid_argument);
    executed = order_executed;
    executed.trade_id = 0;
    EXPECT_THROW(hft::encode_order_executed(executed), std::invalid_argument);
    executed = order_executed;
    executed.liquidity = static_cast<hft::Liquidity>(0);
    EXPECT_THROW(hft::encode_order_executed(executed), std::invalid_argument);
    executed = order_executed;
    executed.fill_price_ticks = -1;
    EXPECT_THROW(hft::encode_order_executed(executed), std::invalid_argument);
}

// D-23 older sender: NewOrder's layout is unchanged since version 0.
TEST(OrderCompatibility, AcceptsOlderVersionWithUnchangedLayout) {
    auto bytes = hft::encode_order(new_order);
    set_version(bytes, 0);
    EXPECT_EQ(hft::decode_order(bytes), new_order);
}
// D-22: no field is absent at any accepted older version, so a short block is malformed.
TEST(OrderCompatibility, RejectsOlderVersionWithShorterBlock) {
    const auto frame = hft::encode_order(new_order);
    Bytes bytes(frame.begin(), frame.end());
    set_version(bytes, 0);
    set_block_length(bytes, 32);
    bytes.pop_back();
    EXPECT_THROW(hft::decode_order(bytes), std::invalid_argument);
}
// D-22 newer sender: a longer block is accepted and unknown trailing bytes are ignored.
TEST(OrderCompatibility, AcceptsNewerVersionWithLongerBlockAndTrailingBytes) {
    for (const auto& codec : codecs()) {
        SCOPED_TRACE(codec.name);
        for (const std::uint16_t version : {std::uint16_t{2}, std::uint16_t{0xffff}}) {
            for (const std::size_t extra : {0U, 1U, 8U}) {
                auto bytes = codec.frame;
                set_version(bytes, version);
                set_block_length(bytes, codec.block + extra);
                bytes.resize(bytes.size() + extra, static_cast<char>(0xab));
                EXPECT_TRUE(codec.decodes_to_original(bytes)) << version << " +" << extra;
            }
        }
    }
}
TEST(OrderCompatibility, RejectsInconsistentNewerVersionFrames) {
    for (const auto& codec : codecs()) {
        SCOPED_TRACE(codec.name);
        // Newer version with a block shorter than known fields.
        auto shorter = codec.frame;
        set_version(shorter, 2);
        set_block_length(shorter, codec.block - 1);
        shorter.pop_back();
        EXPECT_THROW(codec.decodes_to_original(shorter), std::invalid_argument);
        // Newer version whose frame length disagrees with blockLength.
        for (const std::size_t trailing : {4U, 12U}) {
            auto mismatch = codec.frame;
            set_version(mismatch, 2);
            set_block_length(mismatch, codec.block + 8);
            mismatch.resize(mismatch.size() + trailing, 0);
            EXPECT_THROW(codec.decodes_to_original(mismatch), std::invalid_argument);
        }
        // The current version must carry exactly the known block.
        auto current_longer = codec.frame;
        set_block_length(current_longer, codec.block + 8);
        current_longer.resize(current_longer.size() + 8, 0);
        EXPECT_THROW(codec.decodes_to_original(current_longer), std::invalid_argument);
    }
}
TEST(OrderCompatibility, RejectsWrongSchemaIdAndVersionBelowMinimum) {
    for (const auto& codec : codecs()) {
        SCOPED_TRACE(codec.name);
        auto wrong_schema = codec.frame;
        set_schema(wrong_schema, 420);
        EXPECT_THROW(codec.decodes_to_original(wrong_schema), std::invalid_argument);
        EXPECT_THROW(hft::peek_template(wrong_schema), std::invalid_argument);
        if (codec.min_version > 0) {
            auto old = codec.frame;
            set_version(old, 0);
            EXPECT_THROW(codec.decodes_to_original(old), std::invalid_argument);
        }
    }
}
// Ping/Pong keep the exact-version policy: both peers come from one build.
TEST(OrderCompatibility, PingPongStillRequireExactVersion) {
    for (const std::uint16_t version : {std::uint16_t{0}, std::uint16_t{2}}) {
        auto bytes = hft::encode_echo(hft::EchoKind::pong, 1);
        put_le(bytes, 6, 2, version);
        EXPECT_THROW(hft::decode_echo(bytes, hft::EchoKind::pong), std::invalid_argument);
    }
}

TEST(OrderProtocol, PeekTemplateDispatchesKnownTemplatesOnly) {
    for (const auto& codec : codecs()) {
        EXPECT_EQ(static_cast<std::uint16_t>(hft::peek_template(codec.frame)), codec.template_id);
    }
    EXPECT_EQ(hft::peek_template(hft::encode_echo(hft::EchoKind::ping, 1)), hft::TemplateId::ping);
    EXPECT_EQ(hft::peek_template(hft::encode_echo(hft::EchoKind::pong, 1)), hft::TemplateId::pong);
    auto frame = Bytes(codecs()[1].frame);
    for (std::size_t length = 0; length < hft::header_size; ++length) {
        EXPECT_THROW(hft::peek_template(std::span<const char>(frame).first(length)),
                     std::invalid_argument);
    }
    for (const std::uint16_t id : {std::uint16_t{0}, std::uint16_t{9}, std::uint16_t{0xffff}}) {
        auto unknown = frame;
        set_template(unknown, id);
        EXPECT_THROW(hft::peek_template(unknown), std::invalid_argument);
    }
}
} // namespace
