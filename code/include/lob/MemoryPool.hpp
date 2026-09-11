#pragma once

#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>
#include <utility>

using namespace std;

namespace lob {

/// Lock-free, fixed-capacity memory pool using a LIFO free-stack.
/// Thread-safe for concurrent allocate/deallocate calls (MPMC).
///
/// Lifetime contract (conventional allocator semantics):
///   * allocate()  returns RAW storage; it does not construct a T.
///   * deallocate() returns a slot to the pool; it does NOT run T's destructor.
///   * construct() = allocate() + placement-new.
///   * destroy()   = T::~T() + deallocate().
/// Pair allocate()/deallocate() for raw storage, or construct()/destroy() for
/// full object lifetime management. Never call deallocate() on a pointer
/// obtained from construct() if T has a non-trivial destructor: use destroy().
///
/// Notes:
///   * Storage uses a plain alignas byte array (aligned_storage_t is deprecated
///     in C++23).
///   * deallocate() writes the freed slot index only AFTER winning the CAS,
///     avoiding a TOCTOU race where two concurrent deallocators could both
///     write the same free-stack index and leak a slot.
template <typename T, size_t Capacity>
class MemoryPool {
    static_assert(Capacity > 0, "Capacity must be positive");
    static_assert(is_default_constructible_v<T>);

public:
    MemoryPool() noexcept {
        for (size_t i = 0; i < Capacity; ++i)
            free_stack_[i].store(i, memory_order_relaxed);
        top_.store(Capacity, memory_order_release);
    }

    ~MemoryPool() noexcept = default;

    MemoryPool(const MemoryPool&)            = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;

    /// Allocate raw storage for one T. Returns nullptr if pool is exhausted.
    T* allocate() noexcept {
        size_t top = top_.load(memory_order_acquire);
        while (top > 0) {
            if (top_.compare_exchange_weak(top, top - 1,
                                           memory_order_acq_rel,
                                           memory_order_acquire)) {
                size_t idx = free_stack_[top - 1].load(memory_order_relaxed);
                return launder(reinterpret_cast<T*>(&storage_[idx]));
            }
        }
        return nullptr;
    }

    /// Return a raw slot to the pool. Does NOT run T's destructor; use
    /// destroy() if the object was created with construct().
    /// The slot index is written AFTER the CAS succeeds to avoid the race.
    void deallocate(T* ptr) noexcept {
        if (!ptr) return;

        auto* raw = reinterpret_cast<StorageSlot*>(ptr);
        size_t idx = static_cast<size_t>(raw - storage_.data());
        assert(idx < Capacity);

        size_t top = top_.load(memory_order_acquire);
        while (true) {
            size_t new_top = top + 1;
            if (top_.compare_exchange_weak(top, new_top,
                                           memory_order_acq_rel,
                                           memory_order_acquire)) {
                // We won the CAS — now it is safe to write the slot index.
                free_stack_[top].store(idx, memory_order_release);
                break;
            }
        }
    }

    /// Convenience: allocate + construct
    template <typename... Args>
    T* construct(Args&&... args) noexcept(is_nothrow_constructible_v<T, Args...>) {
        T* ptr = allocate();
        if (ptr) new (ptr) T(forward<Args>(args)...);
        return ptr;
    }

    /// Convenience: destroy (run T's destructor) then return the slot.
    /// Pair with construct(); this is the lifetime-managing counterpart to it.
    void destroy(T* ptr) noexcept {
        if (!ptr) return;
        ptr->~T();
        deallocate(ptr);
    }

    size_t available() const noexcept {
        return top_.load(memory_order_acquire);
    }

    static constexpr size_t capacity() noexcept { return Capacity; }

private:
    struct alignas(T) StorageSlot {
        byte bytes[sizeof(T)];
    };

    alignas(64) array<StorageSlot, Capacity>               storage_;
    alignas(64) array<atomic<size_t>, Capacity>  free_stack_;
    alignas(64) atomic<size_t>                        top_;
};

} // namespace lob
