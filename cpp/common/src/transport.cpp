#include "hft/transport.hpp"

#include <cstring>
#include <stdexcept>
#include <thread>

namespace hft {
static_assert(aeron::NOT_CONNECTED == -1 && aeron::BACK_PRESSURED == -2 &&
              aeron::ADMIN_ACTION == -3 && aeron::PUBLICATION_CLOSED == -4 &&
              aeron::MAX_POSITION_EXCEEDED == -5);

Endpoint::Endpoint(const Options& options, bool ping) {
    const auto deadline = Clock::now() + options.startup_timeout;
    aeron::Context context;
    context.aeronDir(options.aeron_dir);
    context.mediaDriverTimeout(static_cast<long>(options.startup_timeout.count()));
    context.errorHandler([state = errors_](const std::exception& error) {
        std::lock_guard lock(state->mutex);
        if (state->message.empty()) {
            state->message = error.what();
        }
    });
    client_ = aeron::Aeron::connect(context);
    const auto pub_id = client_->addPublication(
        "aeron:ipc", ping ? options.request_stream : options.response_stream);
    const auto sub_id = client_->addSubscription(
        "aeron:ipc", ping ? options.response_stream : options.request_stream);
    while (Clock::now() < deadline) {
        check();
        if (!publication_) {
            publication_ = client_->findPublication(pub_id);
        }
        if (!subscription_) {
            subscription_ = client_->findSubscription(sub_id);
        }
        if (publication_ && subscription_ && publication_->isConnected() &&
            subscription_->isConnected()) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    throw std::runtime_error("startup timeout waiting for driver/peer and both streams");
}

void Endpoint::check() {
    std::lock_guard lock(errors_->mutex);
    if (!errors_->message.empty()) {
        throw std::runtime_error("Aeron client error: " + errors_->message);
    }
    if (client_ && client_->isClosed()) {
        throw std::runtime_error("Aeron client closed");
    }
}

void Endpoint::send(std::span<char> bytes, Deadline deadline) {
    if (bytes.empty() || bytes.size() > echo_size) {
        throw std::invalid_argument("tool publication length outside supported frame size");
    }
    aeron::concurrent::AtomicBuffer buffer(reinterpret_cast<std::uint8_t*>(bytes.data()),
                                           static_cast<aeron::util::index_t>(bytes.size()));
    offer_until(
        [&] {
            check();
            return publication_->offer(buffer);
        },
        deadline);
}

EchoFrame Endpoint::receive(Deadline deadline) {
    EchoFrame frame{};
    bool received = false;
    bool invalid_length = false;
    // Never throw through the C client's callback. Validate/copy here, decode after poll.
    auto handler = [&](aeron::concurrent::AtomicBuffer& buffer, aeron::util::index_t offset,
                       aeron::util::index_t length, aeron::Header&) {
        if (length != static_cast<aeron::util::index_t>(frame.size()) || offset < 0 ||
            offset > buffer.capacity() || length > buffer.capacity() - offset) {
            invalid_length = true;
            return;
        }
        std::memcpy(frame.data(), buffer.buffer() + offset, frame.size());
        received = true;
    };
    while (Clock::now() < deadline) {
        check();
        subscription_->poll(handler, 1);
        if (invalid_length) {
            throw std::invalid_argument("invalid message length in Aeron fragment");
        }
        if (received) {
            return frame;
        }
        std::this_thread::yield();
    }
    throw std::runtime_error("response timeout waiting for complete message");
}
} // namespace hft
