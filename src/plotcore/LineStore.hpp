// SPDX-FileCopyrightText: 2026 Andreas Martin
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// A line that is already in RAM, held the way DatasetPlot holds one it read.
//
// The Plot tab's model is a file, a thread and a table. What it *does* with a
// line after the elements are in hand is the same question this answers: build
// a LinePyramid once, fold a whole-line summary out of it, and fold a closer
// look when the reader zooms in. The file and the thread are not here, so a
// numpy array -- or any other contiguous buffer of doubles -- can sit where
// a hyperslab used to.
//
// No QObject: the MinGW DLL that PySide paints through cannot link Qt, and a
// signal would have pulled it in. `onChanged` is an ordinary callback.
//
// `values` is borrowed. The store never copies the raw line; the pyramid is
// the copy, at the finest bucket the hold-budget affords. The owner of those
// doubles must keep them alive until clearLines() or the store is destroyed.

#include "gui/PlotLevels.hpp"
#include "gui/PlotPyramid.hpp"
#include "gui/PlotProjection.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace gui {

class LineStore
{
public:
    static constexpr long long kHoldDoubles = 8 << 20;
    static constexpr int kDrawBudget = 1 << 21;

    /// Called after add/clear/resize/zoom actually changes a fold. Not during
    /// fillInto: that would refill from inside a refill.
    std::function<void()> onChanged;

    /// Borrow `count` doubles as one line. An invalid colour takes the next
    /// entry of a short cycle.
    int addLine(const double* values, qsizetype count, const QColor& colour = {});

    /// A line that arrives in pieces, folded as each piece lands.
    ///
    /// `addLine` wants the whole buffer, because that is what a numpy array
    /// already is. A dataset is not: PyramidBuilder takes each read and keeps
    /// the pyramid, which is the copy, and the raw samples are not held. The
    /// line is absent from the picture until `finishLine`. A closer look finer
    /// than the pyramid's base asks `setWindowReader`, and without one the
    /// whole-line summary stays up.
    int beginLine(long long count, const QColor& colour = {});
    void addSamples(int index, const double* values, long long count);
    void finishLine(int index);

    /// Where a closer look finer than the pyramid's base is read from.
    ///
    /// Writes `count` doubles into `out`, the elements of the line from
    /// `first`. Returns 0 when it cannot. Called on the thread that asked for
    /// the picture, and only for a window the pyramid does not hold. A line
    /// borrowed from memory needs none of this: that buffer is read directly.
    using WindowReader = int (*)(void* user, long long first, long long count, double* out);
    void setWindowReader(int index, WindowReader reader, void* user);

    /// The shared x, borrowed like a line.
    ///
    /// Absent, x is the sample index. Present, it is one value per sample and
    /// the numbers on the axis are those values. It has to go one way: a range
    /// of x is then a range of positions, and the closer look the index axis
    /// already takes follows. One that doubles back is not that map, and a
    /// zoom stretches the whole-line summary. A line can name a time of its
    /// own; this is the clock the lines that did not are drawn against.
    void setAxis(const double* values, qsizetype count);

    /// The same axis, arriving in pieces. `setAxis` wants the whole buffer.
    /// A dataset is folded as each piece lands and the raw samples are not
    /// held; the axis is absent until `finishAxis`. A closer look finer than
    /// its base asks `setAxisReader`.
    void beginAxis(long long count);
    void addAxisSamples(const double* values, long long count);
    void finishAxis();
    void setAxisReader(WindowReader reader, void* user);

    /// This line's own time, borrowed like the shared axis.
    ///
    /// The numbered x stays one window. The line is drawn where its own
    /// timestamps fall in that window, and a zoom is the same window turned
    /// into this line's indices. The count has to be this line's: index i of
    /// the time is sample i. A different length is not a time for this line,
    /// and it changes nothing — the line stays on the shared clock. A count
    /// of 0 clears the own time and puts the line back there. It has to go
    /// one way, for the shared axis's reason. A time that doubles back is
    /// drawn, and a zoom stretches the summary.
    bool setLineAxis(int index, const double* values, qsizetype count);

    /// The same time, arriving in pieces. `count` is refused unless it is
    /// this line's length and at least two. The line stays on the shared
    /// clock until `finishLineAxis`.
    bool beginLineAxis(int index, long long count);
    void addLineAxisSamples(int index, const double* values, long long count);
    bool finishLineAxis(int index);
    void setLineAxisReader(int index, WindowReader reader, void* user);

    /// Whether x is logarithmic. The fold changes with it: a window of an
    /// octave or more is folded per column, because a bucket of elements is
    /// not a column on that axis. Under an octave the linear fold stays.
    void setXLog(bool on);

    /// Whether y is logarithmic. Column strokes treat non-positive y as a gap.
    void setYLog(bool on);

    void clearLines();
    void setPaneColumns(int columns);
    void setVisibleRange(double xMin, double xMax);

    /// One pixel of the pane, in y units. Retained for the host API; drawing
    /// uses PlotColumns entry/exit geometry rather than a chord tolerance.
    void setYPerPixel(double value);

    /// A line on an axis of its own leaves the common extent. The request is
    /// kept on the line; the common minimum and maximum are only the lines
    /// still on the common axis.
    void setOwnAxis(int index, bool on);
    [[nodiscard]] bool ownAxis(int index) const;
    [[nodiscard]] int sharedCount() const { return shared_; }
    [[nodiscard]] bool lineExtent(int index, double& low, double& high) const;

    /// The smallest value above zero on this line. False when there is none.
    /// A logarithmic band starts here: a bound at or below zero is not a place.
    [[nodiscard]] bool linePositiveMinimum(int index, double& out) const;

    /// Write the current folds into `lines` / `axis`. Pointers stay valid
    /// until the next fillInto, clearLines, or a rebuild that retires them --
    /// call releaseRetired() only after the renderer has been handed the new
    /// pointers.
    void fillInto(std::vector<PlotLine>& lines, PlotAxis& axis);
    void releaseRetired();

    [[nodiscard]] int lineCount() const { return static_cast<int>(lines_.size()); }
    [[nodiscard]] int paneColumns() const { return columns_; }
    [[nodiscard]] long long length() const { return length_; }

    [[nodiscard]] double xMin() const;
    [[nodiscard]] double xMax() const;
    [[nodiscard]] double xPositiveMinimum() const;
    [[nodiscard]] double minimum() const { return minimum_; }
    [[nodiscard]] double maximum() const { return maximum_; }
    [[nodiscard]] double positiveMinimum() const { return hasPositive_ ? positiveMinimum_ : 0.0; }
    [[nodiscard]] std::span<const double> drawnValues(int index) const;

private:
    struct Entry
    {
        const double* values = nullptr;
        qsizetype count = 0;
        QColor colour;
        LinePyramid pyramid;
        std::vector<double> whole;
        /// Element index of each value in `whole`, when it is an envelope.
        std::vector<double> wholePositions;
        std::vector<double> closer;
        /// Element index of each value in `closer`. Empty when the run is the
        /// samples themselves.
        std::vector<double> closerPositions;
        long long wholeStride = 1;
        double wholeStep = 1.0;
        bool wholeSummarised = false;
        PlotWindow closerWindow;
        double closerStep = 1.0;
        bool closerValid = false;
        std::vector<double> foldValues;
        std::vector<double> foldXs;
        bool foldSummarised = false;
        bool foldValid = false;
        bool ownAxis = false;
        bool finite = false;
        std::unique_ptr<PyramidBuilder> building;
        WindowReader reader = nullptr;
        void* readerUser = nullptr;
        double low = 0.0;
        double high = 1.0;

        /// This line's time, when it has one. A value member would be the
        /// type containing itself. Absent, or a length other than this
        /// line's, and the line is drawn on the shared axis.
        std::unique_ptr<Entry> time;
        bool hasTime = false;

        /// x of each drawn y, when the line names its own time. Borrowed
        /// into PlotLine::xs. The shared axis is a different index, so a
        /// position on it would name the wrong sample.
        std::vector<double> placedXs;
    };

    [[nodiscard]] int pointsFor() const;
    void adopt(Entry& entry);
    void dropAxis();
    void acceptAxis();
    void dropLineTime(Entry& entry);
    void acceptLineTime(Entry& entry);
    void retireBuffers(Entry& entry);
    void rebuildWhole(Entry& entry);
    void refreshCloser();
    /// Fold every line onto the view's pixel columns (PlotColumns).
    void refreshColumnStroke();
    void dropFolds();
    [[nodiscard]] double timeAt(const Entry& time, long long at) const;
    /// The index run of `time` that `t0`..`t1` covers. False when the time
    /// is not a map; the caller then keeps the whole-line summary.
    [[nodiscard]] bool indexSpan(Entry& time, double t0, double t1, double& low, double& high);
    void refreshEntry(Entry& entry, const std::optional<PlotWindow>& wanted);
    [[nodiscard]] bool readWindow(Entry& entry, const PlotWindow& window,
                                  std::vector<double>& folded,
                                  std::vector<double>* positions = nullptr);
    [[nodiscard]] bool positionOf(Entry& entry, double x, double& position, double& resolution);
    void placeOwnTimes(Entry& entry);
    /// `placedXs[i]` is the time of the sample point `i` names.
    void placeTimes(Entry& entry, const Entry& time);
    void recount();
    /// The union of the shared axis and every line's own time. False when
    /// there is no time at all, and x is the sample index.
    [[nodiscard]] bool timeExtent(double& low, double& high) const;
    [[nodiscard]] PlotLine lineOf(const Entry& entry) const;
    void emitChanged();

    std::vector<Entry> lines_;
    Entry axis_;
    bool hasAxis_ = false;
    bool xLog_ = false;
    bool yLog_ = false;
    std::vector<std::vector<double>> retired_;
    int columns_ = kDefaultColumns;
    int cap_ = kMinPoints;
    double viewMin_ = 0.0;
    double viewMax_ = 1.0;
    /// Value units per device pixel of y. See setYPerPixel.
    double yPerPixel_ = 0.0;
    double askedMin_ = 0.0;
    double askedMax_ = 1.0;
    bool asked_ = false;
    /// Column-stroke grid covering askedMin_..askedMax_ with margin. A pan
    /// inside that margin keeps every entry's foldValid stroke.
    LogFoldGrid foldGrid_;
    bool foldYLog_ = false;
    long long length_ = 0;
    double minimum_ = 0.0;
    double maximum_ = 1.0;
    double positiveMinimum_ = 0.0;
    bool hasPositive_ = false;
    int shared_ = 0;
    int nextColour_ = 0;
};

} // namespace gui
