// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "PlotBudget.hpp"

#include <QByteArray>

#include <algorithm>

#if defined(Q_OS_WIN)
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace gui {
namespace {

constexpr long long kMegabyte = 1024LL * 1024LL;

/// What one tier is allowed, given `physical` bytes of memory.
///
/// A fraction, then a floor and a ceiling. The floor is what makes the setting
/// mean something on a small machine -- a thirty-second of four gigabytes is
/// 128 MB and holding less than that is barely a cache at all -- and the
/// ceiling is what stops "greedy" on a workstation from being a number nobody
/// asked for. Past a couple of gigabytes of runs the reader has stopped
/// benefiting: the whole line is in hand at a bucket finer than the pane, and
/// what is left to hold is octaves nobody is going to ask for.
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

} // namespace

PlotBudget& PlotBudget::instance()
{
    static PlotBudget budget;
    return budget;
}

long long PlotBudget::physicalMemory()
{
    // Asked once. It cannot change while the process runs, and on Linux it is
    // two syscalls that would otherwise happen on every resize.
    static const long long found = [] {
#if defined(Q_OS_WIN)
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

void PlotBudget::setAppetite(Appetite appetite)
{
    if (appetite_ == appetite) {
        return;
    }
    appetite_ = appetite;
    emit changed();
}

long long PlotBudget::total() const
{
    // An override, for the suites and for tools/bench-data. A benchmark whose
    // cache size depended on the machine it ran on would be measuring the
    // machine, which is the thing tests/test_cost.cpp exists not to do.
    static const long long pinned = [] {
        const QByteArray asked = qgetenv("H5SCOPE_PLOT_BUDGET_MB");
        if (asked.isEmpty()) {
            return 0LL;
        }
        bool ok = false;
        const long long megabytes = asked.toLongLong(&ok);
        return ok && megabytes > 0 ? megabytes * kMegabyte : 0LL;
    }();

    const long long bytes = pinned > 0 ? pinned : allowance(appetite_, physicalMemory());
    return bytes / static_cast<long long>(sizeof(double));
}

long long PlotBudget::share() const
{
    return total() / std::max(plots_, 1);
}

void PlotBudget::join()
{
    ++plots_;
    emit changed();
}

void PlotBudget::leave()
{
    plots_ = std::max(plots_ - 1, 0);
    emit changed();
}

} // namespace gui
