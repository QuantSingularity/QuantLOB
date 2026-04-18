#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <optional>
#include <type_traits>

namespace lob {

/// Lock-free, single-producer/single-consumer (SPSC) ring buffer.
///
/// THREAD SAFETY:
///   - Exactly ONE producer thread may call push() concurrently.
///   - Exactly ONE consumer thread may call pop() concurrently.
///   - size() and empty() are approximate when called from a third thread.
///
/// DESIGN NOTE: This is a classic SPSC queue using a "wasted slot" sentinel
/// to distinguish full from empty without a separate size counter.
/// One slot is permanently reserved, so the usable capacity is (Capacity-1).
///
/// Capacity MUST be a power of two so that the index mask trick works.
template <typename T, std::size_t Capacity>
class RingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a power of two");
    static_assert(std::is_default_constructible_v<T>);

public:
    RingBuffer() noexcept : head_(0), tail_(0) {}

    ~RingBuffer() noexcept = default;

    RingBuffer(const RingBuffer&)            = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;

    /// Push an item. Returns false if the buffer is full.
    /// Must only be called by the PRODUCER thread.
    bool push(const T& item) noexcept {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t next = (head + 1) & mask_;
        if (next == tail_.load(std::memory_order_acquire)) return false; // full
        buffer_[head] = item;
        head_.store(next, std::memory_order_release);
        return true;
    }

    bool push(T&& item) noexcept {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t next = (head + 1) & mask_;
        if (next == tail_.load(std::memory_order_acquire)) return false;
        buffer_[head] = std::move(item);
        head_.store(next, std::memory_order_release);
        return true;
    }

    /// Pop an item. Returns std::nullopt if the buffer is empty.
    /// Must only be called by the CONSUMER thread.
    std::optional<T> pop() noexcept {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) return std::nullopt;
        T item = std::move(buffer_[tail]);
        tail_.store((tail + 1) & mask_, std::memory_order_release);
        return item;
    }

    /// Approximate emptiness check. Safe to call from either thread.
    bool empty() const noexcept {
        return head_.load(std::memory_order_acquire) ==
               tail_.load(std::memory_order_acquire);
    }

    /// Approximate size. May be transiently off by 1 under concurrent access.
    std::size_t size() const noexcept {
        const std::size_t h = head_.load(std::memory_order_acquire);
        const std::size_t t = tail_.load(std::memory_order_acquire);
        return (h - t + Capacity) & mask_;
    }

    static constexpr std::size_t capacity() noexcept { return Capacity; }

private:
    static constexpr std::size_t mask_ = Capacity - 1;

    // Separate cache lines to avoid false sharing between producer/consumer.
    alignas(64) std::atomic<std::size_t> head_;
    alignas(64) std::atomic<std::size_t> tail_;
    alignas(64) std::array<T, Capacity>  buffer_;
};

} // namespace lob
