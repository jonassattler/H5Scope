// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "Thread.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>

namespace h5core::thread {
namespace {

/// The owner, or a default-constructed id when nobody has claimed.
///
/// `std::thread::id` is not guaranteed lock-free in an atomic, so the flag and
/// the id are kept apart: `active` is what the fast path reads, and the id is
/// only ever written by the claiming thread while `active` is false and read
/// after it is true. That ordering is what the acquire/release pair below is
/// for.
std::atomic<bool> active{false};
std::thread::id owner;

} // namespace

void claim()
{
    const std::thread::id self = std::this_thread::get_id();
    if (active.load(std::memory_order_acquire)) {
        if (owner == self) {
            return;
        }
        std::fprintf(stderr,
                     "h5core: a second thread tried to claim HDF5 while another "
                     "still holds it. HDF5 is not thread-safe in this build.\n");
        std::abort();
    }
    owner = self;
    active.store(true, std::memory_order_release);
}

void release()
{
    active.store(false, std::memory_order_release);
    owner = std::thread::id{};
}

bool claimed() noexcept
{
    return active.load(std::memory_order_acquire);
}

bool owned() noexcept
{
    return !active.load(std::memory_order_acquire)
           || owner == std::this_thread::get_id();
}

void check(const char* what)
{
    if (owned()) [[likely]] {
        return;
    }
    // Not an exception. An exception would unwind through HDF5's C frames and
    // could be caught by a caller that then carried on, which is the one thing
    // that must not happen: by the time this fires the process is one call away
    // from corrupting the library's global state.
    std::fprintf(stderr,
                 "h5core: %s was called from a thread that does not own HDF5. "
                 "Every HDF5 call in this process must be made on the one "
                 "thread that claimed it.\n",
                 (what != nullptr) ? what : "an h5core function");
    std::abort();
}

} // namespace h5core::thread
