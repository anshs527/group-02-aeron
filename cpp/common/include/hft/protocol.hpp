#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace hft {
inline constexpr std::size_t echo_size = 16;
inline constexpr std::size_t order_size = 41;
using EchoFrame = std::array<char, echo_size>;
using OrderFrame = std::array<char, order_size>;
enum class EchoKind : std::uint16_t { ping = 1, pong = 2 };
enum class Side : std::uint8_t { buy = 1, sell = 2 };
struct NewOrder {
    std::uint32_t client_id;
    std::uint64_t client_order_id;
    std::uint32_t symbol_id;
    Side side;
    std::int64_t price_ticks;
    std::uint64_t quantity;
    bool operator==(const NewOrder&) const = default;
};
EchoFrame encode_echo(EchoKind kind, std::uint64_t correlation);
std::uint64_t decode_echo(std::span<char> bytes, EchoKind expected);
OrderFrame encode_order(const NewOrder& order);
NewOrder decode_order(std::span<char> bytes);
void require_correlation(std::uint64_t actual, std::uint64_t expected);
} // namespace hft
