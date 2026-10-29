#include "hft/transport.hpp"

#include <chrono>
#include <iostream>
#include <string_view>
#include <thread>

// Real Aeron peer used ONLY by negative integration tests.
int main(int argc, char** argv) {
    try {
        if (argc != 3) {
            throw std::invalid_argument("expected AERON_DIR and fault mode");
        }
        hft::Options options;
        options.aeron_dir = argv[1];
        hft::Endpoint endpoint(options, false);
        const auto deadline = hft::Clock::now() + std::chrono::seconds(5);
        auto request = endpoint.receive(deadline);
        const auto id = hft::decode_echo(request, hft::EchoKind::ping);
        const std::string_view mode(argv[2]);
        if (mode == "no-response") {
            std::this_thread::sleep_for(std::chrono::seconds(3));
            std::cout << "FAULT INJECTED no-response\n";
            return 0;
        }
        auto response = hft::encode_echo(hft::EchoKind::pong, id);
        if (mode == "wrong-correlation") {
            response = hft::encode_echo(hft::EchoKind::pong, id + 1);
        } else if (mode == "wrong-template") {
            response = hft::encode_echo(hft::EchoKind::ping, id);
        } else if (mode != "truncated") {
            throw std::invalid_argument("unknown fault mode");
        }
        endpoint.send(std::span<char>(response).first(mode == "truncated" ? 15 : 16), deadline);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        std::cout << "FAULT INJECTED " << mode << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAULT PEER FAILED: " << error.what() << '\n';
        return 1;
    }
}
