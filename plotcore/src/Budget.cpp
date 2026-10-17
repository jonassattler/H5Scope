// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "plotcore/Budget.hpp"

#include <algorithm>

#if defined(_WIN32)
// NOMINMAX before the header, or std::clamp below stops compiling: windows.h
// defines `max` and `min` as function-like macros, and MSVC reports the result
// as C2589, "illegal token on right side of '::'", at a line with nothing wrong
// with it. WIN32_LEAN_AND_MEAN because this wants one function out of the SDK
// and none of the sockets, RPC or OLE.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace plotcore {
namespace {

constexpr long long kMegabyte = 1024LL * 1024LL;

} // namespace

long long physicalMemory()
{
    // Asked once. It cannot change while the process runs, and on Linux it is
    // two syscalls that would otherwise happen on every resize.
    static const long long found = [] {
#if defined(_WIN32)
        MEMORYSTATUSEX status{};
        status.dwLength = sizeof(status);
        if (GlobalMemoryStatusEx(&status) == 0) {
            return 0LL;
        }
        return static_cast<long long>(status.ullTotalPhys);
#else
        const long pages = sysconf(_SC_PHYS_PAGES);
        const long page = sysconf(_SC_PAGE_SIZE);
        if (pages <= 0 || page <= 0) {
            return 0LL;
        }
        return static_cast<long long>(pages) * static_cast<long long>(page);
#endif
    }();
    return found;
}

long long allowance(Appetite appetite, long long physical)
{
    struct Tier
    {
        long long divisor;
        long long least;
        long long most;
    };
    Tier tier{};
    switch (appetite) {
    case Appetite::Low:
        tier = {32, 64 * kMegabyte, 256 * kMegabyte};
        break;
    case Appetite::Medium:
        tier = {8, 256 * kMegabyte, 2048 * kMegabyte};
        break;
    case Appetite::Greedy:
        tier = {2, 1024 * kMegabyte, 16384 * kMegabyte};
        break;
    }
    if (physical <= 0) {
        // Nothing to take a fraction of. The floor is the honest answer: it is
        // what this tier promises on the smallest machine it would run on, and
        // guessing higher on a machine that would not say how much it has is
        // guessing with the reader's memory.
        return tier.least;
    }
    return std::clamp(physical / tier.divisor, tier.least, tier.most);
}

} // namespace plotcore
