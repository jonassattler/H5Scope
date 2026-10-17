// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-FileCopyrightText: 2026 Andreas Martin
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Lines held in memory, folded for whatever the reader is looking at.
//
// What the viewer's DatasetPlot and CustomPlot do once the elements are in
// hand, without the file, the thread or the table: each line is held as a
// LinePyramid at the finest base the budget affords, and every picture of it
// is a fold of that pyramid -- the whole-line summary at the pane's width, a
// closer look at the window on screen, and on a logarithmic x axis a fold per
// pixel column. A fold of a pyramid is a few thousand doubles, so every frame
// asks for exactly what it draws and nothing is read ahead: the ladder of
// held runs exists in the viewer to hide reads of a file, and here there are
// none to hide.
//
// A line's x is the index (`start + i * step`), a time base the plot shares,
// or a time base of the line's own -- each one value per element, held whole.
// A time base that only ever goes one way is a map that can be run backwards,
// so a window of x is a window of elements and a closer look follows; one that
// doubles back is drawn from the whole-line summary, as in the viewer.
//
// Lines are copied in. A borrowed buffer is a lifetime the caller has to keep
// across a language boundary, and the copy is not extra: over a base of one
// the pyramid's base *is* the copy. A line too large for the budget is held at
// a coarser base, and a LineReader is what reaches below it.

#include "plotcore/Budget.hpp"
#include "plotcore/Camera.hpp"
#include "plotcore/Levels.hpp"
#include "plotcore/Projection.hpp"
#include "plotcore/Pyramid.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace plotcore {

/// Reads `count` elements of a line, from element `first`, into `out`; false
/// when it cannot. What a closer look below the pyramid's base asks.
using LineReader = std::function<bool(long long first, long long count, double* out)>;

/// The values of a line: its finite extent, and the smallest of it above zero,
/// which is where a logarithmic y axis starts.
struct LineExtent
{
    bool finite = false;
    double minimum = 0.0;
    double maximum = 0.0;
    bool positive = false;
    double positiveMinimum = 0.0;
};

class Plot
{
public:
    Plot();
    ~Plot();
    Plot(Plot&&) noexcept;
    Plot& operator=(Plot&&) noexcept;
    Plot(const Plot&) = delete;
    Plot& operator=(const Plot&) = delete;

    // --- lines -----------------------------------------------------------
    /// Copy `count` values in as a new line; its index.
    int addLine(const double* values, long long count, std::string name = {});
    /// A line that arrives in pieces: `count` elements, appended in order and
    /// folded as they land, then finished. Not drawn until it is.
    int beginLine(long long count, std::string name = {});
    void appendToLine(int line, const double* values, long long count);
    void finishLine(int line);
    /// Where a closer look finer than the line's base reads from.
    void setLineReader(int line, LineReader reader);
    void removeLine(int line);
    void clear();

    [[nodiscard]] int lineCount() const;
    [[nodiscard]] const std::string& name(int line) const;
    void setName(int line, std::string name);
    /// Elements of the line, as held.
    [[nodiscard]] long long length(int line) const;
    /// Whether it has finished arriving.
    [[nodiscard]] bool ready(int line) const;

    // --- x -----------------------------------------------------------------
    /// The x of element `i` of a line with no time: `start + i * step`.
    void setIndexAxis(double start, double step);
    [[nodiscard]] double indexStart() const { return indexStart_; }
    [[nodiscard]] double indexStep() const { return indexStep_; }
    /// A time base every line without its own is drawn against; empty for
    /// none.
    void setTime(const double* values, long long count);
    /// A line's own time base, which beats the shared one; empty for none.
    void setLineTime(int line, const double* values, long long count);
    [[nodiscard]] bool hasTime(int line) const;

    // --- holding -----------------------------------------------------------
    /// The share of the machine the lines may hold between them, as a tier...
    void setAppetite(Appetite appetite);
    /// ...or pinned to `bytes`, or 0 to go back to the tier.
    void setBudgetBytes(long long bytes);
    [[nodiscard]] Appetite appetite() const { return appetite_; }
    /// Doubles the lines may hold between them.
    [[nodiscard]] long long budgetDoubles() const;
    /// Doubles they hold.
    [[nodiscard]] long long heldDoubles() const;
    /// The finest bucket a line is held at; one is every element.
    [[nodiscard]] long long baseBucket(int line) const;

    // --- what is being looked at -------------------------------------------
    /// Device-pixel columns of the pane: one bucket each.
    void setPaneColumns(int columns);
    [[nodiscard]] int paneColumns() const { return columns_; }
    /// The window of x on screen, and whether the axis is logarithmic -- which
    /// decides what a column is.
    void setView(double xLow, double xHigh, bool xLog);

    // --- answers ---------------------------------------------------------------
    [[nodiscard]] LineExtent extent(int line) const;
    /// The finite x a line spans.
    [[nodiscard]] Span xExtent(int line) const;
    /// The smallest x of the line above zero, or 0.
    [[nodiscard]] double xPositiveMinimum(int line) const;

    /// The line as it is drawn now: a fold of its pyramid for the view and
    /// the pane, with the element of each extreme. Its arrays are the plot's
    /// and are valid until the next call that is not const.
    [[nodiscard]] PlotLine drawLine(int line) const;
    /// The x axis that line is drawn against.
    [[nodiscard]] PlotAxis drawAxis(int line) const;

private:
    struct Line;

    [[nodiscard]] Line& at(int line);
    [[nodiscard]] const Line& at(int line) const;
    [[nodiscard]] long long share() const;
    void fitAll();
    [[nodiscard]] const std::vector<double>* timeOf(const Line& line) const;
    [[nodiscard]] bool positionsOf(const Line& line, double xLow, double xHigh, double& low,
                                   double& high) const;
    void foldSummary(Line& line) const;
    void foldCloser(Line& line) const;
    [[nodiscard]] bool foldLog(Line& line) const;

    std::vector<std::unique_ptr<Line>> lines_;
    double indexStart_ = 0.0;
    double indexStep_ = 1.0;
    std::vector<double> time_;
    bool timeSorted_ = false;
    bool timeAscending_ = true;
    Appetite appetite_ = Appetite::Medium;
    long long budgetBytes_ = 0;
    int columns_ = 1000;
    double viewLow_ = 0.0;
    double viewHigh_ = 0.0;
    bool viewSet_ = false;
    bool xLog_ = false;
};

} // namespace plotcore
