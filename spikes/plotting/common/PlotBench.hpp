// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// The measuring and the table, shared by all three spikes.
//
// A parallel of tools/BenchReport.hpp rather than a reuse of it: that file
// includes gui/H5Thread.hpp for its `crossings` column and would drag the
// application into a tree that is meant to be able to build without it. What
// is copied is the doctrine, which this repository already states in
// tests/test_cost.cpp -- a duration measures the machine, a count measures the
// program -- and the rule that two tools read side by side must print one
// format.
//
// Seven numbers per cell, and each answers a different question:
//
//   clear ms   throwing the *previous* dataset away. Measured on its own
//              because it is not always small and it is not always the same
//              order as building: QCustomPlot took 496 seconds to discard ten
//              thousand graphs it had built in one, and charged to the build
//              column that number would have been read as the cost of the
//              sixty-four lines that replaced them.
//   build ms   handing the renderer N lines of M points. The application's
//              DatasetPlot::fill() is exactly this call, once per line, and it
//              happens on the UI thread.
//   first ms   that call to the first complete frame on screen. A library that
//              builds its geometry lazily moves its cost from `build` to here,
//              and a reader feels the sum.
//   cpu p50    milliseconds inside a frame, median over the scripted gesture.
//   cpu p95    the same at the ninety-fifth percentile. A renderer with a fine
//              median and a bad p95 stutters, and the median is precisely the
//              statistic that hides it.
//   rss MiB    peak resident. Interesting against the data's own size: what is
//              being paid for is the renderer's copy, not the doubles.
//   allocs     allocations during the gesture, from the global operator new.
//              The count that travels: it is the same number on every machine,
//              and it is where a QList<QPointF> rebuilt per frame shows up.
//   alloc MiB  what those allocations asked for.
//
// Cells that exceed the point budget are printed as skipped rather than
// omitted. A grid with holes in it invites the reader to assume the hole was a
// failure, and the difference between "too big to hold" and "too slow to draw"
// is the whole question.

#include "common/FrameTimer.hpp"
#include "common/SyntheticSource.hpp"

#include <QtCore/QString>
#include <QtCore/QStringList>

#include <cstdint>
#include <string>
#include <vector>

namespace spike {

struct Cell {
    int series = 1;
    int points = 1;
    /// Marked in the table, because these two are not hypotheses about scale:
    /// 64 x 2048 is what a selection opens on today, and 10000 x 2048 is what
    /// the legend's `all` does to a ten-thousand-row table.
    bool realWorkload = false;
    /// Past the budget: reported as a skipped row rather than left out, so a
    /// hole in the grid cannot be misread as a failure to draw.
    bool skipped = false;
};

/// The grid, in a fixed order, with the two real cells included whatever the
/// budget -- a budget that skipped the actual workload would be measuring
/// something nobody asked about.
[[nodiscard]] std::vector<Cell> grid(std::int64_t pointBudget);

struct Row {
    std::string renderer;
    std::string shape;
    int series = 0;
    int points = 0;
    bool realWorkload = false;
    bool skipped = false;
    std::string note;

    double clearMs = 0.0;
    double buildMs = 0.0;
    double firstFrameMs = 0.0;
    FrameStats frames;
    double rssMiB = -1.0;
    double dataMiB = 0.0;
    std::uint64_t allocCalls = 0;
    std::uint64_t allocMiB = 0;
};

class BenchReport
{
public:
    void add(Row row) { rows_.push_back(std::move(row)); }

    [[nodiscard]] const std::vector<Row>& rows() const { return rows_; }

    /// The table, on stdout, in the format the other two benchmarks print.
    void print(const std::string& title) const;

    /// The same numbers as a tab-separated file, so the report can build its
    /// tables from the run rather than from a transcription. Appends when the
    /// file exists, so three spikes writing the same directory produce one
    /// comparable file instead of three that have to be joined by hand.
    bool appendTsv(const QString& path) const;

    /// One row, appended as soon as it is finished.
    ///
    /// Written per cell rather than per run because a run does not always
    /// finish: Qt Graphs segmentation-faults on a single line of ten million
    /// points, inside QSGCurveStrokeNode::cookGeometry, and a report assembled
    /// at the end would have lost the twenty cells that had already succeeded
    /// along with the one that did not. A benchmark should survive the thing
    /// it is benchmarking.
    static bool appendRow(const QString& path, const Row& row);

private:
    std::vector<Row> rows_;
};

/// What every spike accepts. One parser so that a flag means the same thing in
/// all three, and so the report can quote one command line.
struct Options {
    bool bench = false;       ///< run the grid and exit
    bool verify = false;      ///< run the correctness checks and exit
    bool offscreen = false;   ///< no display: software renderer, labelled
    bool help = false;
    QString out;              ///< directory for results
    int frames = 120;         ///< frames per cell, after the warm-up
    int warmup = 10;          ///< frames discarded before measuring
    std::int64_t budget = 25'000'000; ///< points a single cell may hold
    int series = 64;          ///< interactive: lines to draw
    int points = 2048;        ///< interactive: points per line
    Shape shape = Shape::Sine;
    std::uint32_t seed = 7;
    /// `--cell SxP` runs that one cell instead of the grid. The grid is the
    /// answer; a single cell is how the answer gets debugged, and how the
    /// report re-runs the one number a reader disputes.
    int cellSeries = 0;
    int cellPoints = 0;
    /// `--variant NAME`. A spike that has more than one way to draw the same
    /// thing -- a node per series or one node for all of them -- runs both and
    /// reports them as separate renderers rather than picking a winner in
    /// private.
    QString variant;
    QString unknownFlag;      ///< non-empty when parsing gave up
};

[[nodiscard]] Options parseOptions(const QStringList& arguments);
void printUsage(const char* program);

/// Set the environment a rendering benchmark needs, before QGuiApplication is
/// constructed. Returns false when `--offscreen` was not asked for and there is
/// no display to draw on, which is a refusal rather than a fallback: measuring
/// three renderers through the software rasteriser would compare three things
/// none of them ship as.
[[nodiscard]] bool prepareRenderEnvironment(const Options& options, std::string& why);

} // namespace spike
