// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// The measuring and the table, shared by bench-tree and bench-data.
//
// Two tools, one format, deliberately: they are read side by side when
// something is slow, and a column that means one thing in one of them and
// something else in the other is worse than no column. Everything specific to
// what is being measured stays in the tool; everything about *how* it is
// measured and printed is here.
//
// Four numbers per phase, and each answers a different question:
//
//   ms         how long it took, wall clock, waiting for the file included.
//              The number a reader would feel, and the only one that depends
//              on the machine.
//   ui ms      how much of that was spent inside a call on the calling thread.
//              On a UI thread this is the part that could not have been
//              drawing a frame, which is the difference between slow and
//              frozen. Work moved to the HDF5 thread shows up in `ms` and not
//              here, which is exactly the improvement asynchrony buys.
//   reads      read syscalls, from /proc/self/io. The number that travels: it
//              is the same on every machine, where a duration is not, and a
//              large HDF5 file usually lives on a filesystem where each of
//              these is a network round trip rather than a page-cache hit.
//   crossings  jobs sent to the HDF5 thread. Each is a queued call and, for
//              the blocking form, a wait on both sides. Bounded by the code
//              rather than by the file, so a phase whose crossings scale with
//              the data is a bug and not a cost -- which is precisely how the
//              plot was found to be reading one line per round trip.

#include "gui/H5Thread.hpp"

#if defined(__unix__)
#include <fcntl.h>
#include <unistd.h>
#endif

#include <chrono>
#include <cstddef>
#include <cstdio>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace bench {

/// Evict the file from the page cache, so a run measures the disk rather than
/// the memory the last run left it in. posix_fadvise on clean pages needs no
/// privilege, which drop_caches does -- and it drops only this file, so the
/// rest of the machine is left alone.
///
/// It is the honest default. A large HDF5 file is opened once, cold, by a
/// reader who then waits; measuring the second open of it measures a state the
/// complaint was never about.
inline bool evict([[maybe_unused]] const std::string& path)
{
#if defined(__unix__)
    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        return false;
    }
    ::fsync(fd);
    const bool ok = ::posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED) == 0;
    ::close(fd);
    return ok;
#else
    // Windows has no unprivileged way to drop one file from the cache, so the
    // caller is told it did not happen and says so. `--warm` is the honest way
    // to run this there.
    return false;
#endif
}

/// Read syscalls this process has made, from /proc/self/io's `syscr`.
///
/// Negative means "not available here", which measure() propagates and report()
/// prints as "-". Returning 0 instead would put a plausible number in the
/// column the count exists to fill.
inline long long readSyscalls()
{
#if !defined(__linux__)
    return -1;
#else
    std::FILE* io = std::fopen("/proc/self/io", "re");
    if (io == nullptr) {
        return -1;
    }
    char line[128] = {};
    long long value = -1;
    while (std::fgets(line, sizeof(line), io) != nullptr) {
        if (std::sscanf(line, "syscr: %lld", &value) == 1) {
            break;
        }
    }
    std::fclose(io);
    return value;
#endif
}

struct Row {
    const char* name = "";
    double milliseconds = 0.0; ///< wall clock, waiting for the file included
    double blocking = 0.0;     ///< of which was spent in a call on this thread
    long long reads = 0;
    long long crossings = 0;
    long long units = 0;
    const char* unitName = "";
};

inline void report(const std::vector<Row>& rows)
{
    std::printf("\n%-10s %10s %10s %12s %10s %10s %9s   %s\n", "phase", "ms",
                "ui ms", "reads", "crossings", "count", "reads/ea", "unit");
    std::printf("%-10s %10s %10s %12s %10s %10s %9s   %s\n", "----------",
                "----------", "----------", "------------", "----------",
                "----------", "---------", "------------------------");

    // Every count goes through here, so a column with nothing to say says "-"
    // rather than 0. Two things have nothing to say: a phase whose unit does
    // not apply, and -- on any platform without /proc/self/io -- the read
    // counter itself.
    const auto cell = [](long long value, char* buffer, std::size_t size) {
        if (value < 0) {
            std::snprintf(buffer, size, "-");
        } else {
            std::snprintf(buffer, size, "%lld", value);
        }
        return buffer;
    };

    long long totalReads = 0;
    long long totalCrossings = 0;
    bool readsKnown = true;
    double totalMs = 0.0;
    double totalBlocking = 0.0;
    for (const Row& row : rows) {
        if (row.reads < 0) {
            readsKnown = false;
        } else {
            totalReads += row.reads;
        }
        totalCrossings += row.crossings;
        totalMs += row.milliseconds;
        totalBlocking += row.blocking;

        char reads[24];
        char crossings[24];
        char units[24];
        char per[24];
        cell(row.reads, reads, sizeof reads);
        cell(row.crossings, crossings, sizeof crossings);
        cell(row.units > 0 ? row.units : -1, units, sizeof units);
        if (row.reads >= 0 && row.units > 0) {
            std::snprintf(per, sizeof per, "%.2f",
                          static_cast<double>(row.reads)
                              / static_cast<double>(row.units));
        } else {
            std::snprintf(per, sizeof per, "-");
        }

        std::printf("%-10s %10.1f %10.1f %12s %10s %10s %9s   %s\n", row.name,
                    row.milliseconds, row.blocking, reads, crossings, units, per,
                    row.units > 0 ? row.unitName : "");
    }
    char total[24];
    char totalCross[24];
    std::printf("%-10s %10.1f %10.1f %12s %10s\n\n", "total", totalMs,
                totalBlocking, cell(readsKnown ? totalReads : -1, total, sizeof total),
                cell(totalCrossings, totalCross, sizeof totalCross));
}

/// How much of a phase was spent inside a call on this thread. Accumulated by
/// blocking() below, which every model call in a benchmark is wrapped in.
inline double blockingMilliseconds = 0.0;

/// Time one call on this thread and add it to the phase's blocking total. What
/// is being counted is the part a frame would have had to wait for.
template<typename F>
auto blocking(F&& body) -> decltype(body())
{
    const auto start = std::chrono::steady_clock::now();
    if constexpr (std::is_void_v<decltype(body())>) {
        body();
        blockingMilliseconds += std::chrono::duration<double, std::milli>(
                                    std::chrono::steady_clock::now() - start)
                                    .count();
    } else {
        auto result = body();
        blockingMilliseconds += std::chrono::duration<double, std::milli>(
                                    std::chrono::steady_clock::now() - start)
                                    .count();
        return result;
    }
}

/// One measured phase, appended to `rows`. Steady clock rather than
/// QElapsedTimer's default so the numbers mean the same thing on every platform
/// this is compared across.
template<typename F>
void phase(std::vector<Row>& rows, const char* name, F&& body, long long units,
           const char* unitName)
{
    blockingMilliseconds = 0.0;
    const long long readsBefore = readSyscalls();
    const long long crossingsBefore = gui::H5Thread::instance().crossings();
    const auto start = std::chrono::steady_clock::now();
    body();
    const auto end = std::chrono::steady_clock::now();
    const long long readsAfter = readSyscalls();
    const long long crossings =
        gui::H5Thread::instance().crossings() - crossingsBefore;

    // Unknown in, unknown out. Subtracting two -1s would otherwise report a
    // confident zero for a count nothing measured.
    const long long reads =
        (readsBefore < 0 || readsAfter < 0) ? -1 : readsAfter - readsBefore;

    rows.push_back({name,
                    std::chrono::duration<double, std::milli>(end - start).count(),
                    blockingMilliseconds, reads, crossings, units, unitName});
}

} // namespace bench
