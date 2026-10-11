// SPDX-FileCopyrightText: 2026 Andreas Martin
// SPDX-License-Identifier: GPL-3.0-only

#include "plotcore/LineStore.hpp"

#include "gui/PlotColumns.hpp"
#include "gui/PlotLevels.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <span>
#include <utility>

namespace gui {
namespace {

const QColor kCycle[] = {
    QColor(230, 159, 0),   QColor(86, 180, 233),  QColor(0, 158, 115), QColor(240, 228, 66),
    QColor(0, 114, 178),   QColor(213, 94, 0),    QColor(204, 121, 167),
};

QColor colourAt(int index, const QColor& given)
{
    if (given.isValid()) {
        return given;
    }
    return kCycle[static_cast<std::size_t>(index) % (sizeof(kCycle) / sizeof(kCycle[0]))];
}

/// Which drawn point of one reading of a time base `x` falls on.
///
/// The same question as CustomPlot's positionOfX. A time base that only ever
/// goes one way is a map that can be run backwards; one that doubles back is
/// not, and `false` says so. The order is asked of this array, not of the
/// time base as a whole, because a summary can ascend while the elements
/// under one of its buckets do not.
bool positionOfX(const std::vector<double>& values, double start, double step, double x,
                 double& position)
{
    if (values.empty() || !(step > 0.0) || !std::isfinite(x)) {
        return false;
    }
    std::size_t at = 0;
    if (std::is_sorted(values.begin(), values.end())) {
        at = static_cast<std::size_t>(std::lower_bound(values.begin(), values.end(), x) -
                                      values.begin());
    } else if (std::is_sorted(values.rbegin(), values.rend())) {
        at = static_cast<std::size_t>(
            std::lower_bound(values.begin(), values.end(), x, std::greater<>{}) - values.begin());
    } else {
        return false;
    }
    position = start + static_cast<double>(std::min(at, values.size() - 1)) * step;
    return std::isfinite(position);
}

} // namespace

void LineStore::emitChanged()
{
    if (onChanged) {
        onChanged();
    }
}

int LineStore::addLine(const double* values, qsizetype count, const QColor& colour)
{
    if (values == nullptr || count <= 0) {
        return -1;
    }

    Entry entry;
    entry.values = values;
    entry.count = count;
    entry.colour = colourAt(nextColour_++, colour);
    const long long length = static_cast<long long>(count);
    const long long base = baseBucketFor(length, kHoldDoubles);
    entry.pyramid = pyramidOf(values, length, base);
    lines_.push_back(std::move(entry));
    adopt(lines_.back());
    return static_cast<int>(lines_.size()) - 1;
}

int LineStore::beginLine(long long count, const QColor& colour)
{
    if (count <= 0) {
        return -1;
    }
    Entry entry;
    entry.count = static_cast<qsizetype>(count);
    entry.colour = colourAt(nextColour_++, colour);
    entry.building = std::make_unique<PyramidBuilder>(count, baseBucketFor(count, kHoldDoubles));
    lines_.push_back(std::move(entry));
    return static_cast<int>(lines_.size()) - 1;
}

void LineStore::addSamples(int index, const double* values, long long count)
{
    if (index < 0 || index >= lineCount() || values == nullptr || count <= 0) {
        return;
    }
    Entry& entry = lines_[static_cast<std::size_t>(index)];
    if (entry.building == nullptr) {
        return;
    }
    entry.building->add(values, count);
}

void LineStore::finishLine(int index)
{
    if (index < 0 || index >= lineCount()) {
        return;
    }
    Entry& entry = lines_[static_cast<std::size_t>(index)];
    if (entry.building == nullptr) {
        return;
    }
    entry.pyramid = entry.building->finish();
    entry.building.reset();
    entry.count = static_cast<qsizetype>(entry.pyramid.length);
    // The time was accepted against the count beginLine was given. A pyramid
    // that came back a different length is no longer one sample per time.
    if (entry.hasTime && entry.time != nullptr &&
        entry.time->pyramid.length != static_cast<long long>(entry.count)) {
        dropLineTime(entry);
    }
    adopt(entry);
}

void LineStore::setWindowReader(int index, WindowReader reader, void* user)
{
    if (index < 0 || index >= lineCount()) {
        return;
    }
    Entry& entry = lines_[static_cast<std::size_t>(index)];
    entry.reader = reader;
    entry.readerUser = user;
}

void LineStore::setOwnAxis(int index, bool on)
{
    if (index < 0 || index >= lineCount()) {
        return;
    }
    Entry& entry = lines_[static_cast<std::size_t>(index)];
    if (entry.ownAxis == on) {
        return;
    }
    entry.ownAxis = on;
    recount();
    emitChanged();
}

bool LineStore::ownAxis(int index) const
{
    if (index < 0 || index >= lineCount()) {
        return false;
    }
    return lines_[static_cast<std::size_t>(index)].ownAxis;
}

bool LineStore::lineExtent(int index, double& low, double& high) const
{
    if (index < 0 || index >= lineCount()) {
        return false;
    }
    const Entry& entry = lines_[static_cast<std::size_t>(index)];
    if (!entry.finite) {
        return false;
    }
    low = entry.low;
    high = entry.high;
    return true;
}

bool LineStore::linePositiveMinimum(int index, double& out) const
{
    if (index < 0 || index >= lineCount()) {
        return false;
    }
    return smallestPositive(lines_[static_cast<std::size_t>(index)].pyramid, out);
}

void LineStore::dropAxis()
{
    if (!axis_.whole.empty()) {
        retired_.push_back(std::move(axis_.whole));
    }
    if (!axis_.closer.empty()) {
        retired_.push_back(std::move(axis_.closer));
    }
    axis_ = Entry{};
    hasAxis_ = false;
    // The units just changed, or went away. The window in hand was in the
    // old ones, so the next range is applied rather than matched.
    viewMax_ = viewMin_ - 1.0;
    asked_ = false;
    dropFolds();
}

void LineStore::acceptAxis()
{
    if (axis_.pyramid.length < 2) {
        hasAxis_ = false;
        emitChanged();
        return;
    }
    hasAxis_ = true;
    rebuildWhole(axis_);
    const Extremes extremes = extremesOver(axis_.pyramid, 0, axis_.pyramid.length);
    axis_.finite = extremes.found();
    if (axis_.finite) {
        axis_.low = extremes.lowest;
        axis_.high = extremes.highest;
    }
    emitChanged();
}

void LineStore::setAxis(const double* values, qsizetype count)
{
    dropAxis();
    if (values == nullptr || count < 2) {
        emitChanged();
        return;
    }
    axis_.values = values;
    axis_.count = count;
    const long long length = static_cast<long long>(count);
    axis_.pyramid = pyramidOf(values, length, baseBucketFor(length, kHoldDoubles));
    acceptAxis();
}

void LineStore::beginAxis(long long count)
{
    dropAxis();
    if (count < 2) {
        emitChanged();
        return;
    }
    axis_.count = static_cast<qsizetype>(count);
    axis_.building = std::make_unique<PyramidBuilder>(count, baseBucketFor(count, kHoldDoubles));
    emitChanged();
}

void LineStore::addAxisSamples(const double* values, long long count)
{
    if (axis_.building == nullptr || values == nullptr || count <= 0) {
        return;
    }
    axis_.building->add(values, count);
}

void LineStore::finishAxis()
{
    if (axis_.building == nullptr) {
        return;
    }
    axis_.pyramid = axis_.building->finish();
    axis_.building.reset();
    axis_.count = static_cast<qsizetype>(axis_.pyramid.length);
    axis_.values = nullptr;
    acceptAxis();
}

void LineStore::setAxisReader(WindowReader reader, void* user)
{
    axis_.reader = reader;
    axis_.readerUser = user;
}

void LineStore::retireBuffers(Entry& entry)
{
    if (!entry.whole.empty()) {
        retired_.push_back(std::move(entry.whole));
    }
    if (!entry.wholePositions.empty()) {
        retired_.push_back(std::move(entry.wholePositions));
    }
    if (!entry.closer.empty()) {
        retired_.push_back(std::move(entry.closer));
    }
    if (!entry.closerPositions.empty()) {
        retired_.push_back(std::move(entry.closerPositions));
    }
}

void LineStore::dropLineTime(Entry& entry)
{
    entry.hasTime = false;
    if (!entry.placedXs.empty()) {
        retired_.push_back(std::move(entry.placedXs));
    }
    if (entry.time != nullptr) {
        retireBuffers(*entry.time);
        entry.time.reset();
    }
    // The units this line is drawn in just changed. The window in hand was
    // matched against the old ones.
    asked_ = false;
    dropFolds();
}

void LineStore::acceptLineTime(Entry& entry)
{
    if (entry.time == nullptr) {
        entry.hasTime = false;
        emitChanged();
        return;
    }
    Entry& time = *entry.time;
    const bool fits = time.pyramid.length >= 2 &&
                      time.pyramid.length == static_cast<long long>(entry.count);
    if (!fits) {
        // A time that is not one value per sample of this line would put
        // sample i at someone else's timestamp.
        dropLineTime(entry);
        emitChanged();
        return;
    }
    entry.hasTime = true;
    rebuildWhole(time);
    const Extremes extremes = extremesOver(time.pyramid, 0, time.pyramid.length);
    time.finite = extremes.found();
    if (time.finite) {
        time.low = extremes.lowest;
        time.high = extremes.highest;
    }
    asked_ = false;
    dropFolds();
    emitChanged();
}

bool LineStore::setLineAxis(int index, const double* values, qsizetype count)
{
    if (index < 0 || index >= lineCount()) {
        return false;
    }
    Entry& entry = lines_[static_cast<std::size_t>(index)];
    if (values == nullptr || count == 0) {
        const bool had = entry.hasTime || entry.time != nullptr;
        dropLineTime(entry);
        if (had) {
            emitChanged();
        }
        return true;
    }
    if (count < 2 || count != entry.count) {
        return false;
    }
    dropLineTime(entry);
    entry.time = std::make_unique<Entry>();
    entry.time->values = values;
    entry.time->count = count;
    const long long length = static_cast<long long>(count);
    entry.time->pyramid = pyramidOf(values, length, baseBucketFor(length, kHoldDoubles));
    acceptLineTime(entry);
    return entry.hasTime;
}

bool LineStore::beginLineAxis(int index, long long count)
{
    if (index < 0 || index >= lineCount()) {
        return false;
    }
    Entry& entry = lines_[static_cast<std::size_t>(index)];
    if (count < 2 || count != static_cast<long long>(entry.count)) {
        return false;
    }
    dropLineTime(entry);
    entry.time = std::make_unique<Entry>();
    entry.time->count = static_cast<qsizetype>(count);
    entry.time->building =
        std::make_unique<PyramidBuilder>(count, baseBucketFor(count, kHoldDoubles));
    // The old time is already gone. Until finish, this line is on the shared
    // clock, and the picture has to be asked for again to show that.
    emitChanged();
    return true;
}

void LineStore::addLineAxisSamples(int index, const double* values, long long count)
{
    if (index < 0 || index >= lineCount() || values == nullptr || count <= 0) {
        return;
    }
    Entry& entry = lines_[static_cast<std::size_t>(index)];
    if (entry.time == nullptr || entry.time->building == nullptr) {
        return;
    }
    entry.time->building->add(values, count);
}

bool LineStore::finishLineAxis(int index)
{
    if (index < 0 || index >= lineCount()) {
        return false;
    }
    Entry& entry = lines_[static_cast<std::size_t>(index)];
    if (entry.time == nullptr || entry.time->building == nullptr) {
        return false;
    }
    entry.time->pyramid = entry.time->building->finish();
    entry.time->building.reset();
    entry.time->count = static_cast<qsizetype>(entry.time->pyramid.length);
    entry.time->values = nullptr;
    acceptLineTime(entry);
    return entry.hasTime;
}

void LineStore::setLineAxisReader(int index, WindowReader reader, void* user)
{
    if (index < 0 || index >= lineCount()) {
        return;
    }
    Entry& entry = lines_[static_cast<std::size_t>(index)];
    if (entry.time == nullptr) {
        return;
    }
    entry.time->reader = reader;
    entry.time->readerUser = user;
}

void LineStore::setXLog(bool on)
{
    if (xLog_ == on) {
        return;
    }
    xLog_ = on;
    dropFolds();
    asked_ = false;
    emitChanged();
}

void LineStore::setYLog(bool on)
{
    if (yLog_ == on) {
        return;
    }
    yLog_ = on;
    dropFolds();
    emitChanged();
}

void LineStore::clearLines()
{
    lines_.clear();
    axis_ = Entry{};
    hasAxis_ = false;
    retired_.clear();
    length_ = 0;
    minimum_ = 0.0;
    maximum_ = 1.0;
    positiveMinimum_ = 0.0;
    hasPositive_ = false;
    shared_ = 0;
    emitChanged();
}

void LineStore::setPaneColumns(int columns)
{
    const int quantised =
        std::max(kColumnQuantum, (std::max(columns, 1) / kColumnQuantum) * kColumnQuantum);
    if (quantised == columns_) {
        return;
    }
    columns_ = quantised;
    const int cap = pointsFor();
    if (cap == cap_) {
        return;
    }
    cap_ = cap;
    for (Entry& entry : lines_) {
        retireBuffers(entry);
        entry.closerValid = false;
        rebuildWhole(entry);
        if (entry.time != nullptr) {
            retireBuffers(*entry.time);
            entry.time->closerValid = false;
            rebuildWhole(*entry.time);
        }
        if (!entry.placedXs.empty()) {
            retired_.push_back(std::move(entry.placedXs));
        }
    }
    if (hasAxis_) {
        retireBuffers(axis_);
        axis_.closerValid = false;
        rebuildWhole(axis_);
    }
    // The range has not moved, but the fold it was answered with has. The
    // next call has to run again rather than recognise the same window.
    asked_ = false;
    dropFolds();
    emitChanged();
}

void LineStore::setYPerPixel(double value)
{
    if (!(value > 0.0) || !std::isfinite(value)) {
        value = 0.0;
    }
    const double scale = std::max(std::abs(yPerPixel_), std::abs(value));
    if (value == yPerPixel_ || (scale > 0.0 && std::abs(value - yPerPixel_) <= scale * 1e-3)) {
        return;
    }
    // Column strokes place entry/exit by geometry, not by a chord tolerance.
    // Keep the value for hosts that still ask; nothing to rebuild.
    yPerPixel_ = value;
}

void LineStore::setVisibleRange(double xMin, double xMax)
{
    // The same window, already turned into positions. Asking again would
    // re-read a time base that is not in memory, on a frame that did not move.
    if (asked_ && xMin == askedMin_ && xMax == askedMax_) {
        return;
    }
    const double askedMin = xMin;
    const double askedMax = xMax;
    double low = xMin;
    double high = xMax;
    if (hasAxis_) {
        double first = 0.0;
        double second = 0.0;
        double firstResolution = 1.0;
        double secondResolution = 1.0;
        if (positionOf(axis_, xMin, first, firstResolution) &&
            positionOf(axis_, xMax, second, secondResolution)) {
            if (first > second) {
                std::swap(first, second);
                std::swap(firstResolution, secondResolution);
            }
            // Opened by the resolution each end was settled at, so the run
            // does not stop short of a point the pane is still drawing.
            low = first - firstResolution;
            high = second + secondResolution;
        } else {
            // Not a map. The summary is drawn against the times it has, and
            // a zoom stretches it rather than being refused.
            low = 0.0;
            high = static_cast<double>(std::max(axis_.pyramid.length, length_));
        }
    }
    // A line with its own time does not share this index window. The time
    // window can move while the shared indices stay, and that line still
    // has a new run to fold.
    bool ownTime = false;
    for (const Entry& entry : lines_) {
        ownTime = ownTime || entry.hasTime;
    }
    if (viewMin_ == low && viewMax_ == high && !ownTime) {
        asked_ = true;
        askedMin_ = askedMin;
        askedMax_ = askedMax;
        // The pane changing throws the closer look away and leaves this
        // window where it was. Returning here kept the coarse summary,
        // and a zoom that had already reached the samples drew none of
        // them: the summary's points sit a bucket apart, and the window
        // is narrower than that.
        bool missing = hasAxis_ && !axis_.closerValid;
        for (const Entry& entry : lines_) {
            missing = missing || !entry.closerValid;
            missing = missing || (entry.hasTime && entry.time != nullptr && !entry.time->closerValid);
        }
        if (missing) {
            refreshCloser();
        }
        refreshColumnStroke();
        return;
    }
    xMin = low;
    xMax = high;
    viewMin_ = xMin;
    viewMax_ = xMax;
    asked_ = true;
    askedMin_ = askedMin;
    askedMax_ = askedMax;
    // A logarithmic x keeps its column fold while the held grid still serves
    // the window. Linear draws the closer look refreshCloser just built.
    refreshCloser();
    refreshColumnStroke();
    emitChanged();
}

void LineStore::fillInto(std::vector<PlotLine>& lines, PlotAxis& axis)
{
    refreshColumnStroke();
    // A logarithmic fold already stated its own x. A linear line with its own
    // time still has to: the shared axis is a different index.
    for (Entry& entry : lines_) {
        if (entry.foldValid) {
            continue;
        }
        // Own time wins, and it is the only line that carries x of its own.
        // A line on the shared clock is placed by that axis, sample for
        // sample; stating x here makes it a second clock.
        if (entry.hasTime) {
            placeOwnTimes(entry);
        }
    }
    lines.clear();
    lines.reserve(lines_.size());
    for (const Entry& entry : lines_) {
        lines.push_back(lineOf(entry));
    }
    axis = PlotAxis{};
    axis.start = 0.0;
    axis.step = 1.0;
    if (!hasAxis_ || axis_.whole.empty()) {
        return;
    }
    axis.values = axis_.whole.data();
    axis.count = static_cast<qsizetype>(axis_.whole.size());
    axis.valueStep = axis_.wholeStep;
    if (axis_.closerValid && !axis_.closer.empty()) {
        axis.closerValues = axis_.closer.data();
        axis.closerCount = static_cast<qsizetype>(axis_.closer.size());
        axis.closerStart = static_cast<double>(axis_.closerWindow.first);
        axis.closerStep = axis_.closerStep;
    }
}

void LineStore::releaseRetired()
{
    retired_.clear();
}

void LineStore::adopt(Entry& entry)
{
    length_ = 0;
    for (const Entry& line : lines_) {
        if (line.building != nullptr) {
            continue;
        }
        length_ = std::max(length_, static_cast<long long>(line.count));
    }
    cap_ = pointsFor();
    if (!entry.foldValues.empty()) {
        retired_.push_back(std::move(entry.foldValues));
    }
    if (!entry.foldXs.empty()) {
        retired_.push_back(std::move(entry.foldXs));
    }
    entry.foldValid = false;
    rebuildWhole(entry);
    recount();
    emitChanged();
}

int LineStore::pointsFor() const
{
    const int pane = 2 * columns_;
    const int lines = std::max(lineCount(), 1);
    return std::clamp(std::min(kDrawBudget / lines, pane), kMinPoints, kMaxPoints);
}

void LineStore::rebuildWhole(Entry& entry)
{
    long long stride = 1;
    double step = 1.0;
    std::vector<double> positions;
    if (!fillWhole(entry.pyramid, cap_ / 2, entry.whole, stride, step, &positions)) {
        entry.whole.clear();
        entry.wholePositions.clear();
        entry.wholeStride = 1;
        entry.wholeStep = 1.0;
        entry.wholeSummarised = false;
        return;
    }
    // Positions stay for time bisection and for placing a line's own time.
    // A logarithmic x folds them again; a linear one draws this summary.
    if (positions.size() == entry.whole.size()) {
        entry.wholePositions = std::move(positions);
    }
    else {
        entry.wholePositions.clear();
    }
    entry.wholeStride = stride;
    entry.wholeStep = step;
    entry.wholeSummarised = stride > 1;
}

void LineStore::refreshCloser()
{
    if (lines_.empty()) {
        return;
    }
    // Lines on the shared clock share one index run. A line with its own
    // time is the same camera window inverted through that time, so a short
    // trace and a long one zoom together without sharing a length.
    std::optional<PlotWindow> shared;
    if (length_ > 0) {
        const long long domain =
            hasAxis_ && axis_.pyramid.length > 0 ? axis_.pyramid.length : length_;
        shared = windowFor(viewMin_, viewMax_, domain, cap_ / 2);
    }
    for (Entry& entry : lines_) {
        if (entry.hasTime && entry.time != nullptr) {
            double low = 0.0;
            double high = 0.0;
            std::optional<PlotWindow> own;
            if (indexSpan(*entry.time, askedMin_, askedMax_, low, high)) {
                const long long length = entry.time->pyramid.length > 0
                                             ? entry.time->pyramid.length
                                             : static_cast<long long>(entry.count);
                own = windowFor(low, high, length, cap_ / 2);
            }
            refreshEntry(entry, own);
            refreshEntry(*entry.time, own);
        } else {
            refreshEntry(entry, shared);
        }
    }
    if (hasAxis_) {
        refreshEntry(axis_, shared);
    }
}

bool LineStore::indexSpan(Entry& time, double t0, double t1, double& low, double& high)
{
    double first = 0.0;
    double second = 0.0;
    double firstResolution = 1.0;
    double secondResolution = 1.0;
    if (!positionOf(time, t0, first, firstResolution) ||
        !positionOf(time, t1, second, secondResolution)) {
        return false;
    }
    if (first > second) {
        std::swap(first, second);
        std::swap(firstResolution, secondResolution);
    }
    low = first - firstResolution;
    high = second + secondResolution;
    return true;
}

void LineStore::refreshEntry(Entry& entry, const std::optional<PlotWindow>& wanted)
{
    if (!wanted.has_value()) {
        if (entry.closerValid) {
            retired_.push_back(std::move(entry.closer));
            if (!entry.closerPositions.empty()) {
                retired_.push_back(std::move(entry.closerPositions));
            }
            entry.closerValid = false;
        }
        return;
    }
    if (entry.closerValid && entry.closerWindow == *wanted) {
        return;
    }
    std::vector<double> folded;
    std::vector<double> positions;
    // The pyramid answers every window at or above its base. Below that
    // the samples are not in memory, and only the reader — the file, or
    // the borrowed buffer — still has them. Without one, the whole-line
    // summary stays up.
    if (!fillWindow(entry.pyramid, *wanted, folded, &positions) &&
        !readWindow(entry, *wanted, folded, &positions)) {
        if (entry.closerValid) {
            retired_.push_back(std::move(entry.closer));
            if (!entry.closerPositions.empty()) {
                retired_.push_back(std::move(entry.closerPositions));
            }
            entry.closerValid = false;
        }
        return;
    }
    if (entry.closerValid) {
        retired_.push_back(std::move(entry.closer));
        if (!entry.closerPositions.empty()) {
            retired_.push_back(std::move(entry.closerPositions));
        }
    }
    if (positions.size() != folded.size()) {
        positions.clear();
    }
    entry.closerPositions = std::move(positions);
    entry.closer = std::move(folded);
    entry.closerWindow = *wanted;
    entry.closerStep = wanted->bucket == 1 ? 1.0 : static_cast<double>(wanted->bucket) / 2.0;
    entry.closerValid = true;
}

bool LineStore::readWindow(Entry& entry, const PlotWindow& window, std::vector<double>& folded,
                           std::vector<double>* positions)
{
    if (positions != nullptr) {
        positions->clear();
    }
    if (window.span <= 0 || window.first < 0) {
        return false;
    }
    const long long end = window.first + window.span;
    std::vector<double> raw;
    if (entry.values != nullptr && end <= static_cast<long long>(entry.count)) {
        // The borrowed buffer is the line. It is what a closer look reads
        // once the pyramid's base is coarser than the window.
        const double* from = entry.values + window.first;
        raw.assign(from, from + window.span);
    } else if (entry.reader != nullptr) {
        raw.resize(static_cast<std::size_t>(window.span));
        if (entry.reader(entry.readerUser, window.first, window.span, raw.data()) == 0) {
            return false;
        }
    } else {
        return false;
    }
    if (window.bucket <= 1) {
        folded = std::move(raw);
        return true;
    }
    folded.clear();
    if (positions == nullptr) {
        reduceBuckets(raw.data(), window.span, window.bucket, folded);
    }
    else {
        reduceBucketsLocated(raw.data(), window.span, window.bucket, window.first, folded,
                             *positions);
    }
    return !folded.empty();
}

bool LineStore::positionOf(Entry& entry, double x, double& position, double& resolution)
{
    if (!positionOfX(entry.whole, 0.0, entry.wholeStep, x, position)) {
        return false;
    }
    resolution = entry.wholeStep;
    const long long length = entry.pyramid.length;
    if (!(resolution > 1.0) || length <= 1 || (entry.values == nullptr && entry.reader == nullptr)) {
        return true;
    }
    // The summary's bracket is as wide as one of its drawn points. On a long
    // line that is thousands of elements, and the run that came back would
    // stay that coarse however far the reader zoomed. The samples around the
    // answer are the sharper reading.
    const auto half = static_cast<long long>(std::ceil(resolution));
    PlotWindow window;
    window.bucket = 1;
    const double origin = std::clamp(position - static_cast<double>(half), 0.0,
                                     static_cast<double>(length - 1));
    window.first = static_cast<long long>(origin);
    window.span = std::min(half * 2, length - window.first);
    std::vector<double> raw;
    if (window.span <= 0 || !readWindow(entry, window, raw)) {
        return true;
    }
    double closer = 0.0;
    if (positionOfX(raw, static_cast<double>(window.first), 1.0, x, closer)) {
        position = closer;
        resolution = 1.0;
    }
    return true;
}

void LineStore::recount()
{
    minimum_ = 0.0;
    maximum_ = 1.0;
    positiveMinimum_ = 0.0;
    hasPositive_ = false;
    shared_ = 0;
    bool any = false;
    for (Entry& entry : lines_) {
        const Extremes extremes = extremesOver(entry.pyramid, 0, entry.pyramid.length);
        entry.finite = extremes.found();
        if (entry.finite) {
            entry.low = extremes.lowest;
            entry.high = extremes.highest;
        }
        // A line on its own axis still has an extent. It is not part of the
        // common one, which would otherwise be stretched to fit a line the
        // common axis is no longer drawing.
        if (entry.ownAxis || !entry.finite) {
            continue;
        }
        ++shared_;
        if (!any) {
            minimum_ = extremes.lowest;
            maximum_ = extremes.highest;
            any = true;
        } else {
            minimum_ = std::min(minimum_, extremes.lowest);
            maximum_ = std::max(maximum_, extremes.highest);
        }
        double positive = 0.0;
        if (smallestPositive(entry.pyramid, positive)) {
            positiveMinimum_ = hasPositive_ ? std::min(positiveMinimum_, positive) : positive;
            hasPositive_ = true;
        }
    }
    if (!any) {
        minimum_ = 0.0;
        maximum_ = 1.0;
    }
}

void LineStore::dropFolds()
{
    for (Entry& entry : lines_) {
        if (!entry.foldValues.empty()) {
            retired_.push_back(std::move(entry.foldValues));
        }
        if (!entry.foldXs.empty()) {
            retired_.push_back(std::move(entry.foldXs));
        }
        entry.foldValid = false;
        entry.foldSummarised = false;
    }
    foldGrid_.clear();
    foldYLog_ = false;
}

double LineStore::timeAt(const Entry& time, long long at) const
{
    const LinePyramid& pyramid = time.pyramid;
    if (pyramid.empty() || at < 0 || at >= pyramid.length) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const PyramidLevel& bottom = pyramid.levels.front();
    if (bottom.bucket == 1) {
        return bottom.values[static_cast<std::size_t>(at)];
    }
    // The pair's first value is the bucket's first element. On a time base
    // that only goes one way, that is the edge a bisection needs.
    return bottom.values[static_cast<std::size_t>(at / bottom.bucket) * 2];
}

void LineStore::refreshColumnStroke()
{
    // Linear is the closer look. A column stroke there states its own x for
    // every line, including one the shared clock is meant to place sample for
    // sample, and a zoom stops being a run of the line.
    if (!xLog_) {
        return;
    }
    double x0 = 0.0;
    double x1 = 0.0;
    if (asked_) {
        x0 = askedMin_;
        x1 = askedMax_;
    } else {
        x0 = xMin();
        x1 = xMax();
    }
    if (!(x1 > x0) || !std::isfinite(x0) || !std::isfinite(x1) || columns_ < 1) {
        return;
    }

    // Index axis for LineStore strokes: start 0, step 1 (or times). Mode 0.
    constexpr int kMode = 0;
    if (!foldGrid_.serves(0.0, 1.0, kMode, columns_, x0, x1) || foldYLog_ != yLog_) {
        dropFolds();
        foldGrid_.remake(0.0, 1.0, kMode, columns_, x0, x1, xLog_);
        foldYLog_ = yLog_;
    }
    double foldMin = 0.0;
    double foldMax = 0.0;
    int foldColumns = 0;
    if (!foldGrid_.extent(foldMin, foldMax, foldColumns)) {
        return;
    }

    for (Entry& entry : lines_) {
        if (entry.foldValid || entry.pyramid.empty() || entry.building != nullptr) {
            continue;
        }
        std::span<const double> times;
        if (entry.hasTime && entry.time != nullptr && entry.time->values != nullptr &&
            entry.time->count == entry.count) {
            times = std::span<const double>(entry.time->values,
                                            static_cast<std::size_t>(entry.time->count));
        } else if (hasAxis_ && axis_.values != nullptr && axis_.count == entry.count) {
            times = std::span<const double>(axis_.values, static_cast<std::size_t>(axis_.count));
        }

        const double* raw =
            rawSamples(entry.pyramid, entry.values, static_cast<long long>(entry.count));
        ColumnStroke stroke;
        if (!rasterColumns(entry.pyramid, raw, static_cast<long long>(entry.count), times, 0.0, 1.0,
                           foldMin, foldMax, foldColumns, xLog_, yLog_, stroke)) {
            continue;
        }
        entry.foldValues = std::move(stroke.values);
        entry.foldXs = std::move(stroke.xs);
        entry.foldSummarised = stroke.summarised;
        entry.foldValid = true;
    }
}

PlotLine LineStore::lineOf(const Entry& entry) const
{
    PlotLine line;
    line.colour = entry.colour;
    if (entry.foldValid && !entry.foldValues.empty() &&
        entry.foldValues.size() == entry.foldXs.size()) {
        line.values = entry.foldValues.data();
        line.xs = entry.foldXs.data();
        line.count = static_cast<qsizetype>(entry.foldValues.size());
        line.summarised = entry.foldSummarised;
        return line;
    }
    if (entry.closerValid && !entry.closer.empty()) {
        line.values = entry.closer.data();
        line.count = static_cast<qsizetype>(entry.closer.size());
        line.positionStart = static_cast<double>(entry.closerWindow.first);
        line.positionStep = entry.closerStep;
        line.summarised = entry.closerWindow.bucket > 1;
        if (entry.closerPositions.size() == entry.closer.size()) {
            line.positions = entry.closerPositions.data();
        }
    } else if (!entry.whole.empty()) {
        line.values = entry.whole.data();
        line.count = static_cast<qsizetype>(entry.whole.size());
        line.positionStep = entry.wholeStep;
        line.summarised = entry.wholeSummarised;
        if (entry.wholePositions.size() == entry.whole.size()) {
            line.positions = entry.wholePositions.data();
        }
    }
    // Stated x wins over the shared axis. xOf reads xs and never the position.
    // Only a line with its own time carries that: the shared clock places the
    // others, and an xs here would hide it.
    if (entry.hasTime && line.values != nullptr &&
        static_cast<qsizetype>(entry.placedXs.size()) == line.count) {
        line.xs = entry.placedXs.data();
    }
    return line;
}

void LineStore::placeOwnTimes(Entry& entry)
{
    if (!entry.hasTime || entry.time == nullptr) {
        if (!entry.placedXs.empty()) {
            retired_.push_back(std::move(entry.placedXs));
        }
        return;
    }
    placeTimes(entry, *entry.time);
}

void LineStore::placeTimes(Entry& entry, const Entry& time)
{
    const std::vector<double>* values = nullptr;
    const std::vector<double>* at = nullptr;
    double start = 0.0;
    double step = 1.0;
    if (entry.closerValid && !entry.closer.empty()) {
        values = &entry.closer;
        start = static_cast<double>(entry.closerWindow.first);
        step = entry.closerStep;
        if (entry.closerPositions.size() == entry.closer.size()) {
            at = &entry.closerPositions;
        }
    } else if (!entry.whole.empty()) {
        values = &entry.whole;
        step = entry.wholeStep;
        if (entry.wholePositions.size() == entry.whole.size()) {
            at = &entry.wholePositions;
        }
    }
    if (values == nullptr) {
        if (!entry.placedXs.empty()) {
            retired_.push_back(std::move(entry.placedXs));
        }
        return;
    }
    // The time of the sample the extreme occurred at, not the extreme of the
    // time in the same bucket. Those are different samples, and drawing the
    // y extreme against the time extreme puts the point at neither of them.
    std::vector<double> xs(values->size());
    for (std::size_t i = 0; i < xs.size(); ++i) {
        const double index = at != nullptr ? (*at)[i] : start + static_cast<double>(i) * step;
        xs[i] = timeAt(time, std::llround(index));
    }
    if (xs == entry.placedXs) {
        return;
    }
    if (!entry.placedXs.empty()) {
        retired_.push_back(std::move(entry.placedXs));
    }
    entry.placedXs = std::move(xs);
}

bool LineStore::timeExtent(double& low, double& high) const
{
    bool any = false;
    const auto take = [&](const Entry& time) {
        if (!time.finite) {
            return;
        }
        if (!any) {
            low = time.low;
            high = time.high;
            any = true;
        } else {
            low = std::min(low, time.low);
            high = std::max(high, time.high);
        }
    };
    if (hasAxis_) {
        take(axis_);
    }
    for (const Entry& entry : lines_) {
        if (entry.hasTime && entry.time != nullptr) {
            take(*entry.time);
        }
    }
    return any;
}

double LineStore::xMin() const
{
    double low = 0.0;
    double high = 0.0;
    if (timeExtent(low, high)) {
        return low;
    }
    return 0.0;
}

double LineStore::xMax() const
{
    double low = 0.0;
    double high = 0.0;
    if (timeExtent(low, high)) {
        return high;
    }
    return length_ > 1 ? static_cast<double>(length_ - 1) : 1.0;
}

double LineStore::xPositiveMinimum() const
{
    double positive = 0.0;
    bool any = false;
    bool timed = hasAxis_;
    const auto take = [&](const Entry& time) {
        double value = 0.0;
        if (!smallestPositive(time.pyramid, value)) {
            return;
        }
        positive = any ? std::min(positive, value) : value;
        any = true;
    };
    if (hasAxis_) {
        take(axis_);
    }
    for (const Entry& entry : lines_) {
        if (entry.hasTime && entry.time != nullptr) {
            timed = true;
            take(*entry.time);
        }
    }
    if (any) {
        return positive;
    }
    if (timed) {
        return 0.0;
    }
    return length_ > 1 ? 1.0 : 0.0;
}

std::span<const double> LineStore::drawnValues(int index) const
{
    if (index < 0 || index >= lineCount()) {
        return {};
    }
    const Entry& entry = lines_[static_cast<std::size_t>(index)];
    if (entry.foldValid && !entry.foldValues.empty()) {
        return entry.foldValues;
    }
    if (entry.closerValid && !entry.closer.empty()) {
        // The run is twice the pane, so a pan still has data in hand. What is
        // drawn is the part the window asked for: the prefetch either side is
        // not on screen, and a count of the whole run is a count of the next
        // pan rather than of this picture.
        if (asked_ && entry.closerStep > 0.0) {
            const double low = std::min(viewMin_, viewMax_);
            const double high = std::max(viewMin_, viewMax_);
            std::size_t begin = entry.closer.size();
            std::size_t end = 0;
            for (std::size_t i = 0; i < entry.closer.size(); ++i) {
                const double at = static_cast<double>(entry.closerWindow.first) +
                                  static_cast<double>(i) * entry.closerStep;
                if (at < low || at > high) {
                    continue;
                }
                begin = std::min(begin, i);
                end = i + 1;
            }
            if (begin < end) {
                return std::span<const double>(entry.closer.data() + begin, end - begin);
            }
        }
        return entry.closer;
    }
    return entry.whole;
}

} // namespace gui
