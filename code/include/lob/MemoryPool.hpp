#pragma once

#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>
#include <utility>

namespace lob {

/// Lock-free, fixed-capacity memory pool using a LIFO free-stack.
/// Thread-safe for concurrent allocate/deallocate calls (MPMC).
///
/// FIX 1: Replaced deprecated std::aligned_storage_t (removed in C++23)
///         with a plain alignas byte array.
/// FIX 2: deallocate() had a TOCTOU race: it wrote free_stack_[top] BEFORE
///         the compare_exchange succeeded, so two concurrent deallocators could
///         both write to index `top` and then race on the CAS — one would win
///         but the other's write would be silently dropped, leaking a slot.
///         Fixed by writing the slot index AFTER winning the CAS.
template <typename T, std::size_t Capacity>
class MemoryPool {
    static_assert(Capacity > 0, "Capacity must be positive");
    static_assert(std::is_default_constructible_v<T>);

public:
    MemoryPool() noexcept {
        for (std::size_t i = 0; i < Capacity; ++i)
            free_stack_[i].store(i, std::memory_order_relaxed);
        top_.store(Capacity, std::memory_order_release);
    }

    ~MemoryPool() noexcept = default;

    MemoryPool(const MemoryPool&)            = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;

    /// Allocate raw storage for one T. Returns nullptr if pool is exhausted.
    T* allocate() noexcept {
        std::size_t top = top_.load(std::memory_order_acquire);
        while (top > 0) {
            if (top_.compare_exchange_weak(top, top - 1,
                                           std::memory_order_acq_rel,
                                           std::memory_order_acquire)) {
                std::size_t idx = free_stack_[top - 1].load(std::memory_order_relaxed);
                return std::launder(reinterpret_cast<T*>(&storage_[idx]));
            }
        }
        return nullptr;
    }

    /// Destroy T and return slot to pool.
    /// FIX: slot index is written AFTER the CAS succeeds to avoid the race.
    void deallocate(T* ptr) noexcept {
        if (!ptr) return;

        auto* raw = reinterpret_cast<StorageSlot*>(ptr);
        std::size_t idx = static_cast<std::size_t>(raw - storage_.data());
        assert(idx < Capacity);

        ptr->~T();

        std::size_t top = top_.load(std::memory_order_acquire);
        while (true) {
            std::size_t new_top = top + 1;
            if (top_.compare_exchange_weak(top, new_top,
                                           std::memory_order_acq_rel,
                                           std::memory_order_acquire)) {
                // We won the CAS — now it is safe to write the slot index.
                free_stack_[top].store(idx, std::memory_order_release);
                break;
            }
        }
    }

    /// Convenience: allocate + construct
    template <typename... Args>
    T* construct(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>) {
        T* ptr = allocate();
        if (ptr) new (ptr) T(std::forward<Args>(args)...);
        return ptr;
    }

    /// Convenience: alias for deallocate with a more explicit name
    void destroy(T* ptr) noexcept { deallocate(ptr); }

    std::size_t available() const noexcept {
        return top_.load(std::memory_order_acquire);
    }

    static constexpr std::size_t capacity() noexcept { return Capacity; }

private:
    // FIX: std::aligned_storage_t is deprecated in C++23.
    struct alignas(T) StorageSlot {
        std::byte bytes[sizeof(T)];
    };

    alignas(64) std::array<StorageSlot, Capacity>               storage_;
    alignas(64) std::array<std::atomic<std::size_t>, Capacity>  free_stack_;
    alignas(64) std::atomic<std::size_t>                        top_;
};

} // namespace lob
