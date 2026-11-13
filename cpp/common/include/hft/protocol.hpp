#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace hft {
// Wire contract: docs/protocol.md. Schema 421, version 1.
inline constexpr std::size_t header_size = 8;
inline constexpr std::uint16_t schema_id = 421;
inline constexpr std::uint16_t schema_version = 1;
// Oldest header version each order decoder accepts (protocol.md section 10).
// NewOrder's layout is unchanged since version 0; templates 4-8 start at 1.
// Ping/Pong accept only the exact current version: both peers share one build.
inline constexpr std::uint16_t new_order_min_version = 0;
inline constexpr std::uint16_t order_message_min_version = 1;

inline constexpr std::size_t echo_size = 16;
inline constexpr std::size_t order_size = 41;
inline constexpr std::size_t cancel_order_size = 32;
inline constexpr std::size_t order_accepted_size = 49;
inline constexpr std::size_t order_rejected_size = 26;
inline constexpr std::size_t order_cancelled_size = 48;
inline constexpr std::size_t order_executed_size = 66;
using EchoFrame = std::array<char, echo_size>;
using OrderFrame = std::array<char, order_size>;
using CancelOrderFrame = std::array<char, cancel_order_size>;
using OrderAcceptedFrame = std::array<char, order_accepted_size>;
using OrderRejectedFrame = std::array<char, order_rejected_size>;
using OrderCancelledFrame = std::array<char, order_cancelled_size>;
using OrderExecutedFrame = std::array<char, order_executed_size>;

enum class TemplateId : std::uint16_t {
    ping = 1,
    pong = 2,
    new_order = 3,
    cancel_order = 4,
    order_accepted = 5,
    order_rejected = 6,
    order_cancelled = 7,
    order_executed = 8
};
enum class EchoKind : std::uint16_t { ping = 1, pong = 2 };
enum class Side : std::uint8_t { buy = 1, sell = 2 };
enum class RequestType : std::uint8_t { new_order = 1, cancel_order = 2 };
enum class RejectReason : std::uint8_t {
    malformed_message = 1,
    unsupported_version = 2,
    unknown_client = 3,
    unknown_symbol = 4,
    invalid_side = 5,
    invalid_price = 6,
    invalid_quantity = 7,
    duplicate_client_order_id = 8,
    unknown_order = 9,
    order_already_done = 10,
    throttled = 11,
    internal_error = 12
};
enum class Liquidity : std::uint8_t { maker = 1, taker = 2 };

struct NewOrder {
    std::uint32_t client_id;
    std::uint64_t client_order_id;
    std::uint32_t symbol_id;
    Side side;
    std::int64_t price_ticks;
    std::uint64_t quantity;
    bool operator==(const NewOrder&) const = default;
};
struct CancelOrder {
    std::uint32_t client_id;
    std::uint64_t client_order_id;
    std::uint64_t orig_client_order_id;
    std::uint32_t symbol_id;
    bool operator==(const CancelOrder&) const = default;
};
struct OrderAccepted {
    std::uint32_t client_id;
    std::uint64_t client_order_id;
    std::uint64_t order_id;
    std::uint32_t symbol_id;
    Side side;
    std::int64_t price_ticks;
    std::uint64_t quantity;
    bool operator==(const OrderAccepted&) const = default;
};
struct OrderRejected {
    std::uint32_t client_id;
    std::uint64_t client_order_id;
    std::optional<std::uint32_t> symbol_id; // absent when the received value was invalid
    RequestType request_type;
    RejectReason reason;
    bool operator==(const OrderRejected&) const = default;
};
struct OrderCancelled {
    std::uint32_t client_id;
    std::uint64_t cancel_client_order_id;
    std::uint64_t orig_client_order_id;
    std::uint64_t order_id;
    std::uint32_t symbol_id;
    std::uint64_t cancelled_quantity;
    bool operator==(const OrderCancelled&) const = default;
};
struct OrderExecuted {
    std::uint32_t client_id;
    std::uint64_t client_order_id;
    std::uint64_t order_id;
    std::uint32_t symbol_id;
    Side side;
    std::uint64_t trade_id;
    std::int64_t fill_price_ticks;
    std::uint64_t fill_quantity;
    std::uint64_t leaves_quantity; // zero once fully filled
    Liquidity liquidity;
    bool operator==(const OrderExecuted&) const = default;
};

// Encoders validate ranges/enums and throw std::invalid_argument before writing.
// Decoders validate length and header before invoking generated decoders, then
// validate every field; all failures throw std::invalid_argument.
EchoFrame encode_echo(EchoKind kind, std::uint64_t correlation);
std::uint64_t decode_echo(std::span<char> bytes, EchoKind expected);
OrderFrame encode_order(const NewOrder& order);
NewOrder decode_order(std::span<char> bytes);
CancelOrderFrame encode_cancel_order(const CancelOrder& cancel);
CancelOrder decode_cancel_order(std::span<char> bytes);
OrderAcceptedFrame encode_order_accepted(const OrderAccepted& event);
OrderAccepted decode_order_accepted(std::span<char> bytes);
OrderRejectedFrame encode_order_rejected(const OrderRejected& event);
OrderRejected decode_order_rejected(std::span<char> bytes);
OrderCancelledFrame encode_order_cancelled(const OrderCancelled& event);
OrderCancelled decode_order_cancelled(std::span<char> bytes);
OrderExecutedFrame encode_order_executed(const OrderExecuted& event);
OrderExecuted decode_order_executed(std::span<char> bytes);

// Dispatch helper: requires a complete header with schema 421 and a known
// template. Version, block length and frame length are checked by the decoder.
TemplateId peek_template(std::span<const char> bytes);
void require_correlation(std::uint64_t actual, std::uint64_t expected);
} // namespace hft
