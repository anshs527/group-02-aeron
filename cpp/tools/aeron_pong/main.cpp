#include "hft/options.hpp"
#include "hft/protocol.hpp"
#include "hft/transport.hpp"

#include <iostream>
#include <string_view>

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        std::cout << "aeron_pong " << hft::usage << '\n';
        return 0;
    }
    try {
        const auto options = hft::parse_options(argc, argv);
        hft::Endpoint endpoint(options, false);
        for (std::uint64_t expected = 1; expected <= options.total(); ++expected) {
            const auto deadline = hft::Clock::now() + options.response_timeout;
            auto request = endpoint.receive(deadline);
            const auto correlation = hft::decode_echo(request, hft::EchoKind::ping);
            hft::require_correlation(correlation, expected);
            auto response = hft::encode_echo(hft::EchoKind::pong, correlation);
            endpoint.send(response, deadline);
        }
        // Publication success means accepted by the driver, not acknowledged by ping.
        // The harness also requires ping's completed count before declaring success.
        std::cout << "PONG OK count=" << options.total() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "PONG FAILED: " << error.what() << "\nUsage: aeron_pong " << hft::usage << '\n';
        return 1;
    }
}
