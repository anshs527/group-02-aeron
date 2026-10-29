#pragma once

#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <thread>

namespace hft {
using Clock = std::chrono::steady_clock;
static_assert(Clock::is_steady);
using Deadline = Clock::time_point;

enum class OfferResult { accepted, retry };
// Aeron's public status contract. transport.cpp asserts these against upstream.
inline OfferResult classify_offer(std::int64_t status) {
    if (status >= 0) {
        return OfferResult::accepted;
    }
    switch (status) {
    case -1: // NOT_CONNECTED: temporary until deadline
    case -2: // BACK_PRESSURED: retry same message
    case -3: // ADMIN_ACTION: e.g. term rotation
        return OfferResult::retry;
    case -4:
        throw std::runtime_error("publication closed");
    case -5:
        throw std::runtime_error("publication maximum position exceeded");
    default:
        throw std::runtime_error("unrecoverable publication status " + std::to_string(status));
    }
}

template <typename Offer> void offer_until(Offer&& offer, Deadline deadline) {
    while (Clock::now() < deadline) {
        if (classify_offer(offer()) == OfferResult::accepted) {
            return;
        }
        std::this_thread::yield();
    }
    throw std::runtime_error("publication timeout (backpressure or disconnected peer)");
}
} // namespace hft
