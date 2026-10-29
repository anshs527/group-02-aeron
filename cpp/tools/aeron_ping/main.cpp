#include "hft/options.hpp"
#include "hft/protocol.hpp"
#include "hft/statistics.hpp"
#include "hft/transport.hpp"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <string_view>

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        std::cout << "aeron_ping " << hft::usage << '\n';
        return 0;
    }
    try {
        const auto options = hft::parse_options(argc, argv);
        hft::Endpoint endpoint(options, true);
        hft::RttStatistics stats;
        for (std::uint64_t id = 1; id <= options.total(); ++id) {
            auto request = hft::encode_echo(hft::EchoKind::ping, id);
            const auto start = hft::Clock::now();
            const auto deadline = start + options.response_timeout;
            endpoint.send(request, deadline);
            auto response = endpoint.receive(deadline);
            hft::require_correlation(hft::decode_echo(response, hft::EchoKind::pong), id);
            const auto end = hft::Clock::now();
            if (id > options.warmup) {
                stats.add(static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count()));
            }
        }
        if (stats.count() != options.messages) {
            throw std::runtime_error("incomplete measured run");
        }
        std::cout << "FUNCTIONAL VALIDATION ONLY; closed-loop RTT, not a performance benchmark\n"
                  << "PING OK count=" << stats.count() << " warmup=" << options.warmup
                  << " min_ns=" << stats.min() << " mean_ns=" << std::fixed << std::setprecision(1)
                  << stats.mean() << " max_ns=" << stats.max() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "PING FAILED: " << error.what() << "\nUsage: aeron_ping " << hft::usage << '\n';
        return 1;
    }
}
