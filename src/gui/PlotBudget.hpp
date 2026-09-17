// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// How much the plots may keep, and who decides.
//
// There are two budgets in this application and they were one number, which is
// why that number had to be small.
//
// What the renderer *walks* is bounded per frame: every drawn line is projected
// on every frame of a drag, so a selection of ten thousand lines at two
// thousand points each is twenty million doubles per frame whatever the machine
// has. That bound is a property of the frame rate and not of the hardware --
// see DatasetPlot::kDrawBudget, which is the old kPointBudget under a name that
// says which of the two it is.
//
// What the models *hold* is bounded by the machine and by nothing else. A run
// already read costs nothing to keep and everything to read again: on a
// hundred-million-element dataset a single octave out is a twenty-million
// element read, and five runs -- which is what sixteen megabytes bought -- is
// not enough to keep the way back. Holding is where the memory should go, and
// this is the number that says how much of it there is.
//
// The reader chooses, because the right answer is not knowable here: the same
// binary runs on a laptop with four gigabytes and a workstation with five
// hundred, and a fraction that is generous on one is ruinous on the other.
// Settings > RAM budget is low, medium or greedy, and each is a fraction of
// what the machine actually has.

#include <QObject>

#include <cstddef>

namespace gui {

/// How much of the machine the plots may spend on holding what they read.
enum class Appetite
{
    Low,
    Medium,
    Greedy,
};

/// The one budget every plot object draws its share from.
///
/// Shared rather than per object, and that is a fix rather than a nicety: the
/// budget used to be a constant on each class, so a window with the Plot tab
/// and eight custom tabs open held nine times it with nothing capping the sum.
/// Multiplying a per-object constant by the number of objects is how a generous
/// number becomes an unbounded one.
class PlotBudget : public QObject
{
    Q_OBJECT

public:
    static PlotBudget& instance();

    [[nodiscard]] Appetite appetite() const { return appetite_; }
    void setAppetite(Appetite appetite);

    /// Physical memory, in bytes, or 0 when it could not be found out.
    ///
    /// sysconf on Linux and GlobalMemoryStatusEx on Windows -- both are in the
    /// C library and the platform SDK, so this adds no dependency and nothing
    /// for the RHEL 8 floor to be missing.
    [[nodiscard]] static long long physicalMemory();

    /// Doubles every plot may hold between them, at the current appetite.
    [[nodiscard]] long long total() const;

    /// Pin the budget to `bytes` whatever the machine has, or to 0 to go back
    /// to reading the machine.
    ///
    /// The same lever `H5SCOPE_PLOT_BUDGET_MB` pulls, which is where this
    /// started -- an override for tools/bench-zoom, read once into a static.
    /// It is a setter as well now because a *test* of what is held has to be
    /// able to move it between two assertions, and because a suite whose cache
    /// size depended on the machine it ran on would be measuring the machine,
    /// which is the thing tests/test_cost.cpp exists not to do.
    void setPinnedTotal(long long bytes);
    [[nodiscard]] long long pinnedTotal() const { return pinned_; }

    /// ...and the share one of them may hold on its own.
    ///
    /// The total divided by how many are alive. Blunt, and deliberately so: a
    /// share proportional to what each is actually drawing would be fairer and
    /// would also mean a tab's budget changing whenever another tab did
    /// something, which is a cache that empties itself for reasons the reader
    /// cannot see.
    [[nodiscard]] long long share() const;

    /// One more plot object to divide between, and one fewer.
    ///
    /// Called from the constructors and destructors of DatasetPlot and
    /// CustomPlot. A count rather than a registry because nothing here needs to
    /// know *which* they are.
    void join();
    void leave();

signals:
    /// The share has changed: a different appetite, or a tab opened or closed.
    /// Every plot re-reads what it may hold and gives up what it may not.
    void changed();

private:
    PlotBudget();

    Appetite appetite_ = Appetite::Medium;
    int plots_ = 0;
    /// Bytes, or 0 for "ask the machine". Seeded from the environment.
    long long pinned_ = 0;
};

} // namespace gui
