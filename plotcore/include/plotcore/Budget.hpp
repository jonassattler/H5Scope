// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// How much of the machine a plot may hold, as arithmetic.
//
// What a plot holds is bounded by the machine and by nothing else -- a run
// already read costs nothing to keep and everything to read again -- and the
// right fraction of it is the reader's to choose, because the same program runs
// on four gigabytes and on five hundred. The tiers and the detection of what
// the machine has live here; who shares the answer, and how a change of it is
// announced, is the host's (gui::PlotBudget in the viewer).

namespace plotcore {

/// How much of the machine the plots may spend on holding what they read.
enum class Appetite
{
    Low,
    Medium,
    Greedy,
};

/// Physical memory, in bytes, or 0 when it could not be found out.
///
/// sysconf on Linux and macOS and GlobalMemoryStatusEx on Windows -- both are
/// in the C library and the platform SDK, so this adds no dependency and
/// nothing for the RHEL 8 floor to be missing. Asked once.
[[nodiscard]] long long physicalMemory();

/// Bytes one tier allows on a machine of `physical` bytes.
///
/// A fraction, then a floor and a ceiling. The floor is what makes the setting
/// mean something on a small machine -- a thirty-second of four gigabytes is
/// 128 MB and holding less than that is barely a cache at all -- and the
/// ceiling is what stops "greedy" on a workstation from being a number nobody
/// asked for. Past a couple of gigabytes of runs the reader has stopped
/// benefiting: the whole line is in hand at a bucket finer than the pane, and
/// what is left to hold is octaves nobody is going to ask for. With nothing to
/// take a fraction of, the floor.
[[nodiscard]] long long allowance(Appetite appetite, long long physical);

} // namespace plotcore
