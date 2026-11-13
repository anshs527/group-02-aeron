#include "hft/protocol.hpp"

#include <hft_sbe/CancelOrder.h>
#include <hft_sbe/Liquidity.h>
#include <hft_sbe/MessageHeader.h>
#include <hft_sbe/NewOrder.h>
#include <hft_sbe/OrderAccepted.h>
#include <hft_sbe/OrderCancelled.h>
#include <hft_sbe/OrderExecuted.h>
#include <hft_sbe/OrderRejected.h>
#include <hft_sbe/Ping.h>
#include <hft_sbe/Pong.h>
#include <hft_sbe/RejectReason.h>
#include <hft_sbe/RequestType.h>
#include <hft_sbe/Side.h>

#include <limits>
#include <stdexcept>

namespace hft {
namespace {
static_assert(sbe::MessageHeader::encodedLength() == header_size);
static_assert(sbe::Ping::sbeSchemaId() == schema_id);
static_assert(sbe::Ping::sbeSchemaVersion() == schema_version);
static_assert(header_size + sbe::Ping::sbeBlockLength() == echo_size);
static_assert(header_size + sbe::Pong::sbeBlockLength() == echo_size);
static_assert(header_size + sbe::NewOrder::sbeBlockLength() == order_size);
static_assert(header_size + sbe::CancelOrder::sbeBlockLength() == cancel_order_size);
static_assert(header_size + sbe::OrderAccepted::sbeBlockLength() == order_accepted_size);
static_assert(header_size + sbe::OrderRejected::sbeBlockLength() == order_rejected_size);
static_assert(header_size + sbe::OrderCancelled::sbeBlockLength() == order_cancelled_size);
static_assert(header_size + sbe::OrderExecuted::sbeBlockLength() == order_executed_size);
static_assert(sbe::CancelOrder::sbeTemplateId() ==
              static_cast<std::uint16_t>(TemplateId::cancel_order));
static_assert(sbe::OrderExecuted::sbeTemplateId() ==
              static_cast<std::uint16_t>(TemplateId::order_executed));
// Handwritten enums mirror generated codes exactly, so casts between them are exact.
static_assert(sbe::Side::Sell == static_cast<std::uint8_t>(Side::sell));
static_assert(sbe::RequestType::CancelOrder ==
              static_cast<std::uint8_t>(RequestType::cancel_order));
static_assert(sbe::RejectReason::InternalError ==
              static_cast<std::uint8_t>(RejectReason::internal_error));
static_assert(sbe::Liquidity::Taker == static_cast<std::uint8_t>(Liquidity::taker));

constexpr std::uint8_t side_max = 2;
constexpr std::uint8_t request_type_max = 2;
constexpr std::uint8_t reject_reason_max = 12;
constexpr std::uint8_t liquidity_max = 2;

void valid_id(std::uint64_t value) {
    if (value == 0 || value == std::numeric_limits<std::uint64_t>::max()) {
        throw std::invalid_argument("invalid zero or null identifier/quantity");
    }
}
void valid_id32(std::uint32_t value) {
    if (value == 0 || value == std::numeric_limits<std::uint32_t>::max()) {
        throw std::invalid_argument("invalid zero or null 32-bit identifier");
    }
}
void valid_price(std::int64_t ticks) {
    // Also rejects the int64 minimum, which is the SBE null sentinel.
    if (ticks <= 0) {
        throw std::invalid_argument("invalid price ticks");
    }
}
// Codes are contiguous from 1; zero, unknown codes and the 255 null are rejected.
template <typename Enum> Enum checked_enum(std::uint8_t raw, std::uint8_t max) {
    if (raw == 0 || raw > max) {
        throw std::invalid_argument("invalid enum value");
    }
    return static_cast<Enum>(raw);
}
template <typename Enum> std::uint8_t raw(Enum value) { return static_cast<std::uint8_t>(value); }
sbe::Side::Value to_sbe(Side side) { return static_cast<sbe::Side::Value>(raw(side)); }

void validate(const NewOrder& order) {
    valid_id32(order.client_id);
    valid_id(order.client_order_id);
    valid_id32(order.symbol_id);
    checked_enum<Side>(raw(order.side), side_max);
    valid_price(order.price_ticks);
    valid_id(order.quantity);
}
void validate(const CancelOrder& cancel) {
    valid_id32(cancel.client_id);
    valid_id(cancel.client_order_id);
    valid_id(cancel.orig_client_order_id);
    valid_id32(cancel.symbol_id);
}
void validate(const OrderAccepted& event) {
    valid_id32(event.client_id);
    valid_id(event.client_order_id);
    valid_id(event.order_id);
    valid_id32(event.symbol_id);
    checked_enum<Side>(raw(event.side), side_max);
    valid_price(event.price_ticks);
    valid_id(event.quantity);
}
void validate(const OrderRejected& event) {
    valid_id32(event.client_id);
    valid_id(event.client_order_id);
    if (event.symbol_id) {
        valid_id32(*event.symbol_id);
    }
    checked_enum<RequestType>(raw(event.request_type), request_type_max);
    checked_enum<RejectReason>(raw(event.reason), reject_reason_max);
}
void validate(const OrderCancelled& event) {
    valid_id32(event.client_id);
    valid_id(event.cancel_client_order_id);
    valid_id(event.orig_client_order_id);
    valid_id(event.order_id);
    valid_id32(event.symbol_id);
    valid_id(event.cancelled_quantity);
}
void validate(const OrderExecuted& event) {
    valid_id32(event.client_id);
    valid_id(event.client_order_id);
    valid_id(event.order_id);
    valid_id32(event.symbol_id);
    checked_enum<Side>(raw(event.side), side_max);
    valid_id(event.trade_id);
    valid_price(event.fill_price_ticks);
    valid_id(event.fill_quantity);
    if (event.leaves_quantity == std::numeric_limits<std::uint64_t>::max()) {
        throw std::invalid_argument("invalid null leaves quantity");
    }
    checked_enum<Liquidity>(raw(event.liquidity), liquidity_max);
}

// Ping/Pong: exact current version, template and block length only.
template <typename Message> void check_header(std::span<char> bytes) {
    // No generated decoder is invoked until the complete fixed frame is present.
    if (bytes.size() != header_size + Message::sbeBlockLength()) {
        throw std::invalid_argument("invalid message length");
    }
    sbe::MessageHeader header(bytes.data(), 0, bytes.size(), Message::sbeSchemaVersion());
    if (header.schemaId() != Message::sbeSchemaId() ||
        header.version() != Message::sbeSchemaVersion() ||
        header.templateId() != Message::sbeTemplateId() ||
        header.blockLength() != Message::sbeBlockLength()) {
        throw std::invalid_argument("invalid SBE header (schema/version/template/block length)");
    }
}
// Order messages: the version range policy of protocol.md section 10.
// Versions up to the current one must carry exactly the known block (no field
// has been appended since any accepted version). Newer versions may carry a
// longer block whose unknown trailing bytes are ignored. The frame length must
// always equal header + blockLength. Returns the wrapped, validated decoder.
template <typename Message> Message wrap_versioned(std::span<char> bytes, std::uint16_t min) {
    if (bytes.size() < header_size) {
        throw std::invalid_argument("invalid message length");
    }
    sbe::MessageHeader header(bytes.data(), 0, bytes.size(), Message::sbeSchemaVersion());
    if (header.schemaId() != Message::sbeSchemaId() ||
        header.templateId() != Message::sbeTemplateId()) {
        throw std::invalid_argument("invalid SBE header (schema/template)");
    }
    const std::uint16_t version = header.version();
    if (version < min) {
        throw std::invalid_argument("unsupported SBE version");
    }
    const std::uint16_t block = header.blockLength();
    const bool newer = version > Message::sbeSchemaVersion();
    if (newer ? block < Message::sbeBlockLength() : block != Message::sbeBlockLength()) {
        throw std::invalid_argument("invalid SBE block length for version");
    }
    if (bytes.size() != header_size + block) {
        throw std::invalid_argument("invalid message length");
    }
    Message message;
    message.wrapForDecode(bytes.data(), header_size, block, version, bytes.size());
    return message;
}
template <typename Message> std::uint64_t decode(std::span<char> bytes) {
    check_header<Message>(bytes);
    Message message;
    message.wrapForDecode(bytes.data(), header_size, Message::sbeBlockLength(),
                          Message::sbeSchemaVersion(), bytes.size());
    const auto correlation = message.correlationId();
    valid_id(correlation);
    return correlation;
}
template <typename Message> EchoFrame encode(std::uint64_t correlation) {
    EchoFrame bytes{};
    Message message;
    message.wrapAndApplyHeader(bytes.data(), 0, bytes.size()).correlationId(correlation);
    return bytes;
}
} // namespace

EchoFrame encode_echo(EchoKind kind, std::uint64_t correlation) {
    valid_id(correlation);
    switch (kind) {
    case EchoKind::ping: return encode<sbe::Ping>(correlation);
    case EchoKind::pong: return encode<sbe::Pong>(correlation);
    }
    throw std::invalid_argument("invalid echo kind");
}
std::uint64_t decode_echo(std::span<char> bytes, EchoKind expected) {
    switch (expected) {
    case EchoKind::ping: return decode<sbe::Ping>(bytes);
    case EchoKind::pong: return decode<sbe::Pong>(bytes);
    }
    throw std::invalid_argument("invalid echo kind");
}

OrderFrame encode_order(const NewOrder& order) {
    validate(order);
    OrderFrame bytes{};
    sbe::NewOrder message;
    message.wrapAndApplyHeader(bytes.data(), 0, bytes.size())
        .clientId(order.client_id)
        .clientOrderId(order.client_order_id)
        .symbolId(order.symbol_id)
        .side(to_sbe(order.side))
        .priceTicks(order.price_ticks)
        .quantity(order.quantity);
    return bytes;
}
NewOrder decode_order(std::span<char> bytes) {
    auto message = wrap_versioned<sbe::NewOrder>(bytes, new_order_min_version);
    NewOrder order{message.clientId(),   message.clientOrderId(),
                   message.symbolId(),   checked_enum<Side>(message.sideRaw(), side_max),
                   message.priceTicks(), message.quantity()};
    validate(order);
    return order;
}

CancelOrderFrame encode_cancel_order(const CancelOrder& cancel) {
    validate(cancel);
    CancelOrderFrame bytes{};
    sbe::CancelOrder message;
    message.wrapAndApplyHeader(bytes.data(), 0, bytes.size())
        .clientId(cancel.client_id)
        .clientOrderId(cancel.client_order_id)
        .origClientOrderId(cancel.orig_client_order_id)
        .symbolId(cancel.symbol_id);
    return bytes;
}
CancelOrder decode_cancel_order(std::span<char> bytes) {
    auto message = wrap_versioned<sbe::CancelOrder>(bytes, order_message_min_version);
    CancelOrder cancel{message.clientId(), message.clientOrderId(), message.origClientOrderId(),
                       message.symbolId()};
    validate(cancel);
    return cancel;
}

OrderAcceptedFrame encode_order_accepted(const OrderAccepted& event) {
    validate(event);
    OrderAcceptedFrame bytes{};
    sbe::OrderAccepted message;
    message.wrapAndApplyHeader(bytes.data(), 0, bytes.size())
        .clientId(event.client_id)
        .clientOrderId(event.client_order_id)
        .orderId(event.order_id)
        .symbolId(event.symbol_id)
        .side(to_sbe(event.side))
        .priceTicks(event.price_ticks)
        .quantity(event.quantity);
    return bytes;
}
OrderAccepted decode_order_accepted(std::span<char> bytes) {
    auto message = wrap_versioned<sbe::OrderAccepted>(bytes, order_message_min_version);
    OrderAccepted event{message.clientId(),
                        message.clientOrderId(),
                        message.orderId(),
                        message.symbolId(),
                        checked_enum<Side>(message.sideRaw(), side_max),
                        message.priceTicks(),
                        message.quantity()};
    validate(event);
    return event;
}

OrderRejectedFrame encode_order_rejected(const OrderRejected& event) {
    validate(event);
    OrderRejectedFrame bytes{};
    sbe::OrderRejected message;
    message.wrapAndApplyHeader(bytes.data(), 0, bytes.size())
        .clientId(event.client_id)
        .clientOrderId(event.client_order_id)
        .symbolId(event.symbol_id.value_or(sbe::OrderRejected::symbolIdNullValue()))
        .requestType(static_cast<sbe::RequestType::Value>(raw(event.request_type)))
        .rejectReason(static_cast<sbe::RejectReason::Value>(raw(event.reason)));
    return bytes;
}
OrderRejected decode_order_rejected(std::span<char> bytes) {
    auto message = wrap_versioned<sbe::OrderRejected>(bytes, order_message_min_version);
    const auto symbol = message.symbolId();
    OrderRejected event{message.clientId(), message.clientOrderId(),
                        symbol == sbe::OrderRejected::symbolIdNullValue()
                            ? std::nullopt
                            : std::optional<std::uint32_t>(symbol),
                        checked_enum<RequestType>(message.requestTypeRaw(), request_type_max),
                        checked_enum<RejectReason>(message.rejectReasonRaw(), reject_reason_max)};
    validate(event);
    return event;
}

OrderCancelledFrame encode_order_cancelled(const OrderCancelled& event) {
    validate(event);
    OrderCancelledFrame bytes{};
    sbe::OrderCancelled message;
    message.wrapAndApplyHeader(bytes.data(), 0, bytes.size())
        .clientId(event.client_id)
        .cancelClientOrderId(event.cancel_client_order_id)
        .origClientOrderId(event.orig_client_order_id)
        .orderId(event.order_id)
        .symbolId(event.symbol_id)
        .cancelledQuantity(event.cancelled_quantity);
    return bytes;
}
OrderCancelled decode_order_cancelled(std::span<char> bytes) {
    auto message = wrap_versioned<sbe::OrderCancelled>(bytes, order_message_min_version);
    OrderCancelled event{message.clientId(),          message.cancelClientOrderId(),
                         message.origClientOrderId(), message.orderId(),
                         message.symbolId(),          message.cancelledQuantity()};
    validate(event);
    return event;
}

OrderExecutedFrame encode_order_executed(const OrderExecuted& event) {
    validate(event);
    OrderExecutedFrame bytes{};
    sbe::OrderExecuted message;
    message.wrapAndApplyHeader(bytes.data(), 0, bytes.size())
        .clientId(event.client_id)
        .clientOrderId(event.client_order_id)
        .orderId(event.order_id)
        .symbolId(event.symbol_id)
        .side(to_sbe(event.side))
        .tradeId(event.trade_id)
        .fillPriceTicks(event.fill_price_ticks)
        .fillQuantity(event.fill_quantity)
        .leavesQuantity(event.leaves_quantity)
        .liquidity(static_cast<sbe::Liquidity::Value>(raw(event.liquidity)));
    return bytes;
}
OrderExecuted decode_order_executed(std::span<char> bytes) {
    auto message = wrap_versioned<sbe::OrderExecuted>(bytes, order_message_min_version);
    OrderExecuted event{message.clientId(),
                        message.clientOrderId(),
                        message.orderId(),
                        message.symbolId(),
                        checked_enum<Side>(message.sideRaw(), side_max),
                        message.tradeId(),
                        message.fillPriceTicks(),
                        message.fillQuantity(),
                        message.leavesQuantity(),
                        checked_enum<Liquidity>(message.liquidityRaw(), liquidity_max)};
    validate(event);
    return event;
}

TemplateId peek_template(std::span<const char> bytes) {
    if (bytes.size() < header_size) {
        throw std::invalid_argument("invalid message length");
    }
    // Little-endian uint16 fields at header offsets 2 (templateId) and 4 (schemaId).
    const auto u16 = [&](std::size_t offset) {
        return static_cast<std::uint16_t>(
            static_cast<unsigned>(static_cast<unsigned char>(bytes[offset])) |
            (static_cast<unsigned>(static_cast<unsigned char>(bytes[offset + 1])) << 8U));
    };
    const auto template_id = u16(2);
    if (u16(4) != schema_id) {
        throw std::invalid_argument("invalid SBE header (schema)");
    }
    if (template_id < static_cast<std::uint16_t>(TemplateId::ping) ||
        template_id > static_cast<std::uint16_t>(TemplateId::order_executed)) {
        throw std::invalid_argument("unknown SBE template");
    }
    return static_cast<TemplateId>(template_id);
}
void require_correlation(std::uint64_t actual, std::uint64_t expected) {
    if (actual != expected) {
        throw std::invalid_argument(
            "correlation mismatch: stale, duplicate, or unexpected response");
    }
}
} // namespace hft
