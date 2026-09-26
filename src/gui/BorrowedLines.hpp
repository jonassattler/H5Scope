// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// The half of the borrow contract that both plots keep, kept once.
//
// A plot hands its PlotItem pointers straight into its own vectors and copies
// nothing (see PlotLine::values), so whatever the item was handed must not be
// freed while the item can still draw it. There are two honest ways to obey
// that: empty the item first, which blanks the pane, or let the old values
// outlive the change until the item is handed their replacement. Both plots
// need both, and each carried its own copy of the record that makes them work
// -- which item is reading, and what it may still be reading -- together with
// the rule for when either may be let go.
//
// Two copies of the same safety rule is how it came to be fixed in one of them
// at a time. The record was a pointer to the last item filled, and fill() moved
// it to a second item without emptying the first; the plot's destructor left
// the last item holding pointers into a plot that no longer existed. Each was
// the same few lines in DatasetPlot and in CustomPlot. They are here now, and
// the plots decide only *when* to lend, retire or release.

#include "PlotProjection.hpp"

#include <QPointer>

#include <type_traits>
#include <vector>

namespace gui {

class PlotItem;

/// Which item is drawing a plot's values, and the values it may still be
/// drawing although the plot has replaced them.
class BorrowedLines
{
public:
    BorrowedLines() = default;
    /// Empties the item, so one that outlives the plot has nothing left to
    /// read.
    ~BorrowedLines();

    BorrowedLines(const BorrowedLines&) = delete;
    BorrowedLines& operator=(const BorrowedLines&) = delete;
    BorrowedLines(BorrowedLines&&) = delete;
    BorrowedLines& operator=(BorrowedLines&&) = delete;

    /// Hand `lines` to `target`, which from now on is the one item reading.
    ///
    /// Only one item is ever recorded, and everything freed later is freed on
    /// that record, so an item that was drawing and is not `target` is emptied
    /// first. And since `target` has just been handed something else, nothing
    /// is reading what was retired any more: this is the one place the retired
    /// store is let go.
    void lend(PlotItem* target, std::vector<PlotLine> lines, const PlotAxis& axis);

    /// Empty whatever item is reading, and let go of everything retired.
    ///
    /// The blunt half of the contract: the pane draws nothing until the next
    /// lend(). Right only where the values are about to stop being a reading
    /// of anything -- a new dataset, a row removed or retyped.
    void release();

    /// Keep `values` alive although the plot is about to replace them, and
    /// leave `values` empty.
    ///
    /// The half that keeps a picture on screen. The item goes on drawing what
    /// it was given until lend() hands it the replacement. It costs nothing: a
    /// std::vector move takes the buffer with it, so every pointer the item
    /// holds goes on naming the same doubles. With nothing reading, there is
    /// nothing to keep them for, and they are freed here -- which is also what
    /// bounds the store on a plot nobody is drawing.
    void retire(std::vector<double>& values);

    /// Whether an item is reading.
    [[nodiscard]] bool lent() const { return drawing_ != nullptr; }

    /// Doubles waiting in the retired store, for tests.
    [[nodiscard]] long long retiredDoubles() const;

private:
    /// A QPointer because the item belongs to a QML scene that is torn down
    /// and rebuilt without telling the plot.
    QPointer<PlotItem> drawing_;

    /// The bare vectors, and never whatever they came out of. This store held
    /// `std::map`s once, and growing a std::vector of those took the copy that
    /// std::move_if_noexcept falls back on where a map's move may throw -- so
    /// the store whose whole job is to keep the borrowed doubles alive was
    /// itself freeing them on Windows. A std::vector<double> move is noexcept
    /// on every implementation, so this one can only ever be moved.
    std::vector<std::vector<double>> retired_;

    // Stated against the member rather than against the type it happens to hold
    // today, so that changing it is what has to answer for this.
    static_assert(std::is_nothrow_move_constructible_v<decltype(retired_)::value_type>,
                  "the retired store must relocate by moving, or it frees what it holds alive");
};

} // namespace gui
