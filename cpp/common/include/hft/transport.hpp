#pragma once

#include "hft/options.hpp"
#include "hft/protocol.hpp"
#include "hft/retry.hpp"

#include <Aeron.h>

#include <memory>
#include <mutex>
#include <span>
#include <string>

namespace hft {
// Single-threaded tool endpoint. Aeron's conductor owns its own thread.
class Endpoint {
  public:
    Endpoint(const Options& options, bool ping);
    void send(std::span<char> bytes, Deadline deadline);
    EchoFrame receive(Deadline deadline);

  private:
    struct ErrorState {
        std::mutex mutex;
        std::string message;
    };
    void check();
    // Outlives the client and its asynchronous error callback.
    std::shared_ptr<ErrorState> errors_ = std::make_shared<ErrorState>();
    std::shared_ptr<aeron::Aeron> client_;
    std::shared_ptr<aeron::Publication> publication_;
    std::shared_ptr<aeron::Subscription> subscription_;
};
} // namespace hft
