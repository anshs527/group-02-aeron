#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace hft {
struct Options {
    std::string aeron_dir;
    std::uint64_t messages = 1000;
    std::uint64_t warmup = 100;
    std::int32_t request_stream = 1001;
    std::int32_t response_stream = 1002;
    std::chrono::milliseconds startup_timeout{10000};
    std::chrono::milliseconds response_timeout{2000};
    [[nodiscard]] std::uint64_t total() const { return messages + warmup; }
};
Options parse_options(int argc, char** argv);
inline constexpr const char* usage =
    "--aeron-dir ABSOLUTE_PATH [--messages 1000] [--warmup 100] "
    "[--startup-timeout-ms 10000] [--response-timeout-ms 2000] "
    "[--request-stream 1001] [--response-stream 1002]";
} // namespace hft
