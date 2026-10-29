#include "hft/protocol.hpp"

#include <hft_sbe/MessageHeader.h>
#include <hft_sbe/NewOrder.h>
#include <hft_sbe/Ping.h>
#include <hft_sbe/Pong.h>
#include <hft_sbe/Side.h>

#include <limits>
#include <stdexcept>

namespace hft {
namespace {
constexpr auto header_size = sbe::MessageHeader::encodedLength();
static_assert(header_size + sbe::Ping::sbeBlockLength() == echo_size);
static_assert(header_size + sbe::Pong::sbeBlockLength() == echo_size);
static_assert(header_size + sbe::NewOrder::sbeBlockLength() == order_size);

void valid_id(std::uint64_t value) {
    if (value == 0 || value == std::numeric_limits<std::uint64_t>::max()) {
        throw std::invalid_argument("invalid zero or null identifier/quantity");
    }
}
void validate(const NewOrder& order) {
    valid_id(order.client_id);
    valid_id(order.symbol_id);
    valid_id(order.client_order_id);
    valid_id(order.quantity);
    if (order.client_id == std::numeric_limits<std::uint32_t>::max() ||
        order.symbol_id == std::numeric_limits<std::uint32_t>::max() ||
        order.price_ticks <= 0 || (order.side != Side::buy && order.side != Side::sell)) {
        throw std::invalid_argument("invalid NewOrder field");
    }
}
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
template <typename Message> EchoFrame encode(std::uint64_t correlation) {
    EchoFrame bytes{};
    Message message;
    message.wrapAndApplyHeader(bytes.data(), 0, bytes.size()).correlationId(correlation);
    return bytes;
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
        .side(order.side == Side::buy ? sbe::Side::Buy : sbe::Side::Sell)
        .priceTicks(order.price_ticks)
        .quantity(order.quantity);
    return bytes;
}
NewOrder decode_order(std::span<char> bytes) {
    check_header<sbe::NewOrder>(bytes);
    sbe::NewOrder message;
    message.wrapForDecode(bytes.data(), header_size, sbe::NewOrder::sbeBlockLength(),
                          sbe::NewOrder::sbeSchemaVersion(), bytes.size());
    const auto side = message.side();
    if (side != sbe::Side::Buy && side != sbe::Side::Sell) {
        throw std::invalid_argument("invalid NewOrder side");
    }
    NewOrder order{message.clientId(), message.clientOrderId(), message.symbolId(),
                   side == sbe::Side::Buy ? Side::buy : Side::sell,
                   message.priceTicks(), message.quantity()};
    validate(order);
    return order;
}
void require_correlation(std::uint64_t actual, std::uint64_t expected) {
    if (actual != expected) {
        throw std::invalid_argument("correlation mismatch: stale, duplicate, or unexpected response");
    }
}
} // namespace hft
