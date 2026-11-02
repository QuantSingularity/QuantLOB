#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <optional>
#include <type_traits>

using namespace std;

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
template <typename T, size_t Capacity>
class RingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a power of two");
    static_assert(is_default_constructible_v<T>);

public:
    RingBuffer() noexcept : head_(0), tail_(0) {}

    ~RingBuffer() noexcept = default;

    RingBuffer(const RingBuffer&)            = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;

    /// Push an item. Returns false if the buffer is full.
    /// Must only be called by the PRODUCER thread.
    bool push(const T& item) noexcept {
        const size_t head = head_.load(memory_order_relaxed);
        const size_t next = (head + 1) & mask_;
        if (next == tail_.load(memory_order_acquire)) return false; // full
        buffer_[head] = item;
        head_.store(next, memory_order_release);
        return true;
    }

    bool push(T&& item) noexcept {
        const size_t head = head_.load(memory_order_relaxed);
        const size_t next = (head + 1) & mask_;
        if (next == tail_.load(memory_order_acquire)) return false;
        buffer_[head] = move(item);
        head_.store(next, memory_order_release);
        return true;
    }

    /// Pop an item. Returns nullopt if the buffer is empty.
    /// Must only be called by the CONSUMER thread.
    optional<T> pop() noexcept {
        const size_t tail = tail_.load(memory_order_relaxed);
        if (tail == head_.load(memory_order_acquire)) return nullopt;
        T item = move(buffer_[tail]);
        tail_.store((tail + 1) & mask_, memory_order_release);
        return item;
    }

    /// Approximate emptiness check. Safe to call from either thread.
    bool empty() const noexcept {
        return head_.load(memory_order_acquire) ==
               tail_.load(memory_order_acquire);
    }

    /// Approximate size. May be transiently off by 1 under concurrent access.
    size_t size() const noexcept {
        const size_t h = head_.load(memory_order_acquire);
        const size_t t = tail_.load(memory_order_acquire);
        return (h - t + Capacity) & mask_;
    }

    static constexpr size_t capacity() noexcept { return Capacity; }

private:
    static constexpr size_t mask_ = Capacity - 1;

    // Separate cache lines to avoid false sharing between producer/consumer.
    alignas(64) atomic<size_t> head_;
    alignas(64) atomic<size_t> tail_;
    alignas(64) array<T, Capacity>  buffer_;
};

} // namespace lob
