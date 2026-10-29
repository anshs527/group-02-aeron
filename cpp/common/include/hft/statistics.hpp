#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace hft {
class RttStatistics {
  public:
    void add(std::uint64_t ns) {
        ++count_;
        min_ = std::min(min_, ns);
        max_ = std::max(max_, ns);
        sum_ += static_cast<long double>(ns);
    }
    [[nodiscard]] std::uint64_t count() const { return count_; }
    [[nodiscard]] std::uint64_t min() const {
        require_samples();
        return min_;
    }
    [[nodiscard]] std::uint64_t max() const {
        require_samples();
        return max_;
    }
    [[nodiscard]] long double mean() const {
        require_samples();
        return sum_ / static_cast<long double>(count_);
    }

  private:
    void require_samples() const {
        if (count_ == 0) {
            throw std::logic_error("RTT statistics require measured samples");
        }
    }
    std::uint64_t count_ = 0;
    std::uint64_t min_ = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t max_ = 0;
    long double sum_ = 0;
};
} // namespace hft
