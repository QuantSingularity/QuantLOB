#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace lob {

/// Lock-free-safe accumulator of nanosecond latency samples.
/// Not thread-safe for concurrent record() calls; designed for single-thread
/// measurement with offline statistical analysis.
class LatencyRecorder {
public:
    explicit LatencyRecorder(std::size_t reserve = 1024) {
        samples_.reserve(reserve);
    }

    void record(std::chrono::nanoseconds ns) {
        samples_.push_back(static_cast<uint64_t>(ns.count()));
    }

    void clear() noexcept { samples_.clear(); }

    [[nodiscard]] std::size_t count() const noexcept { return samples_.size(); }

    /// Direct sample access (used by Exporter).
    [[nodiscard]] uint64_t sample(std::size_t idx) const {
        if (idx >= samples_.size())
            throw std::out_of_range("LatencyRecorder::sample: index out of range");
        return samples_[idx];
    }

    [[nodiscard]] double mean_ns() const noexcept {
        if (samples_.empty()) return 0.0;
        double sum = 0.0;
        for (auto v : samples_) sum += static_cast<double>(v);
        return sum / static_cast<double>(samples_.size());
    }

    [[nodiscard]] double stddev_ns() const noexcept {
        if (samples_.size() < 2) return 0.0;
        double m = mean_ns();
        double sq = 0.0;
        for (auto v : samples_) {
            double d = static_cast<double>(v) - m;
            sq += d * d;
        }
        return std::sqrt(sq / static_cast<double>(samples_.size() - 1));
    }

    [[nodiscard]] uint64_t min_ns() const noexcept {
        if (samples_.empty()) return 0;
        return *std::min_element(samples_.begin(), samples_.end());
    }

    [[nodiscard]] uint64_t max_ns() const noexcept {
        if (samples_.empty()) return 0;
        return *std::max_element(samples_.begin(), samples_.end());
    }

    /// Returns the p-th percentile in nanoseconds (p in [0.0, 100.0]).
    /// Uses the nearest-rank method.
    [[nodiscard]] uint64_t percentile_ns(double p) const {
        if (samples_.empty()) return 0;
        if (p < 0.0 || p > 100.0)
            throw std::out_of_range("percentile_ns: p must be in [0, 100]");
        std::vector<uint64_t> sorted = samples_;
        std::sort(sorted.begin(), sorted.end());
        double      frac = p / 100.0 * static_cast<double>(sorted.size() - 1);
        std::size_t idx  = static_cast<std::size_t>(frac);
        if (idx >= sorted.size()) idx = sorted.size() - 1;
        return sorted[idx];
    }

    [[nodiscard]] uint64_t p50_ns()  const { return percentile_ns(50.0);  }
    [[nodiscard]] uint64_t p90_ns()  const { return percentile_ns(90.0);  }
    [[nodiscard]] uint64_t p99_ns()  const { return percentile_ns(99.0);  }
    [[nodiscard]] uint64_t p999_ns() const { return percentile_ns(99.9);  }

    /// Merge another recorder's samples into this one.
    void merge(const LatencyRecorder& other) {
        samples_.insert(samples_.end(),
                        other.samples_.begin(),
                        other.samples_.end());
    }

private:
    std::vector<uint64_t> samples_;
};

/// RAII timer: records elapsed nanoseconds into a LatencyRecorder on
/// destruction.  Designed for use on the hot path — overhead is a single
/// clock read at construction and one at destruction.
class ScopedTimer {
public:
    explicit ScopedTimer(LatencyRecorder& rec)
        : rec_(rec), start_(std::chrono::high_resolution_clock::now()) {}

    ~ScopedTimer() noexcept {
        auto end = std::chrono::high_resolution_clock::now();
        rec_.record(
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start_));
    }

    ScopedTimer(const ScopedTimer&)            = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;

private:
    LatencyRecorder&                               rec_;
    std::chrono::high_resolution_clock::time_point start_;
};

} // namespace lob
