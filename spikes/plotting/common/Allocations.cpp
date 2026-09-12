// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "common/Allocations.hpp"

#include <atomic>
#include <cstdlib>
#include <new>

namespace {

std::atomic<std::uint64_t> g_calls{0};
std::atomic<std::uint64_t> g_bytes{0};

// Relaxed, not sequentially consistent. Nothing orders anything against these
// counters -- they are read between phases, on one thread, with the frames
// already over -- and a full barrier on every allocation would make the
// benchmark measure the counter.
inline void note(std::size_t size)
{
    g_calls.fetch_add(1, std::memory_order_relaxed);
    g_bytes.fetch_add(size, std::memory_order_relaxed);
}

// std::malloc may return null for a zero-byte request, and operator new must
// not. One byte is the cheapest way to keep the guarantee.
inline void* take(std::size_t size)
{
    void* memory = std::malloc(size == 0 ? 1 : size);
    if (memory == nullptr) {
        throw std::bad_alloc();
    }
    note(size);
    return memory;
}

inline void* takeAligned(std::size_t size, std::align_val_t alignment)
{
    const std::size_t boundary = static_cast<std::size_t>(alignment);
    // aligned_alloc requires the size to be a multiple of the alignment, which
    // the caller is under no obligation to have arranged.
    const std::size_t rounded = ((size == 0 ? 1 : size) + boundary - 1)
                                / boundary * boundary;
    void* memory = std::aligned_alloc(boundary, rounded);
    if (memory == nullptr) {
        throw std::bad_alloc();
    }
    note(size);
    return memory;
}

} // namespace

// The replacement. Every form the standard defines, because a partial
// replacement is worse than none: the allocating half would be ours and the
// freeing half the library's, and the two do not have to agree about which
// heap the pointer came from.

void* operator new(std::size_t size) { return take(size); }
void* operator new[](std::size_t size) { return take(size); }

void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    void* memory = std::malloc(size == 0 ? 1 : size);
    if (memory != nullptr) {
        note(size);
    }
    return memory;
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
    void* memory = std::malloc(size == 0 ? 1 : size);
    if (memory != nullptr) {
        note(size);
    }
    return memory;
}

void* operator new(std::size_t size, std::align_val_t alignment)
{
    return takeAligned(size, alignment);
}

void* operator new[](std::size_t size, std::align_val_t alignment)
{
    return takeAligned(size, alignment);
}

void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete(void* memory, const std::nothrow_t&) noexcept { std::free(memory); }
void operator delete[](void* memory, const std::nothrow_t&) noexcept { std::free(memory); }
void operator delete(void* memory, std::align_val_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::align_val_t) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t, std::align_val_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t, std::align_val_t) noexcept { std::free(memory); }

namespace spike {

AllocationCount allocations()
{
    return {g_calls.load(std::memory_order_relaxed),
            g_bytes.load(std::memory_order_relaxed)};
}

bool allocationCountingActive()
{
    // Allocate something the optimiser cannot fold away and see whether the
    // counter moved. If it did not, the operators above are not the ones this
    // binary calls, and the report must say so rather than print zeroes.
    //
    // The pointer goes through an atomic on the way out and back: C++14 lets a
    // compiler elide a new/delete pair outright, and an elided probe would
    // report "not measured" for a counter that works perfectly well.
    static std::atomic<char*> escape{nullptr};
    const std::uint64_t before = g_calls.load(std::memory_order_relaxed);
    escape.store(new char[64], std::memory_order_relaxed);
    delete[] escape.exchange(nullptr, std::memory_order_relaxed);
    return g_calls.load(std::memory_order_relaxed) > before;
}

} // namespace spike
