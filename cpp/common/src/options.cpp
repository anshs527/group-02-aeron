#include "hft/options.hpp"

#include <charconv>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace hft {
namespace {
std::uint64_t number(std::string_view text, std::uint64_t min, std::uint64_t max) {
    std::uint64_t value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value < min || value > max) {
        throw std::invalid_argument("invalid or out-of-range numeric option: " + std::string(text));
    }
    return value;
}
} // namespace

Options parse_options(int argc, char** argv) {
    Options result;
    for (int i = 1; i < argc; i += 2) {
        const std::string_view key(argv[i]);
        if (i + 1 == argc) {
            throw std::invalid_argument("missing value for " + std::string(key));
        }
        const std::string_view value(argv[i + 1]);
        if (key == "--aeron-dir") {
            result.aeron_dir = value;
        } else if (key == "--messages") {
            result.messages = number(value, 1, 10000000);
        } else if (key == "--warmup") {
            result.warmup = number(value, 0, 10000000);
        } else if (key == "--request-stream") {
            result.request_stream = static_cast<std::int32_t>(
                number(value, 1, std::numeric_limits<std::int32_t>::max()));
        } else if (key == "--response-stream") {
            result.response_stream = static_cast<std::int32_t>(
                number(value, 1, std::numeric_limits<std::int32_t>::max()));
        } else if (key == "--startup-timeout-ms") {
            result.startup_timeout = std::chrono::milliseconds(number(value, 1, 60000));
        } else if (key == "--response-timeout-ms") {
            result.response_timeout = std::chrono::milliseconds(number(value, 1, 60000));
        } else {
            throw std::invalid_argument("unknown option: " + std::string(key));
        }
    }
    if (result.aeron_dir.empty() || !std::filesystem::path(result.aeron_dir).is_absolute()) {
        throw std::invalid_argument("--aeron-dir must be an explicit absolute path");
    }
    if (result.request_stream == result.response_stream) {
        throw std::invalid_argument("request and response stream IDs must differ");
    }
    return result;
}
} // namespace hft
