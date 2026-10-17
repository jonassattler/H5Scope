// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-FileCopyrightText: 2026 Andreas Martin
// SPDX-License-Identifier: GPL-3.0-only

#include "plotcore/Plot.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>

namespace plotcore {
namespace {

/// Which way a time base runs, if it runs one way at all. A NaN or a turn
/// anywhere is "neither": a bisection of it would answer nothing.
void orderOf(const std::vector<double>& time, bool& sorted, bool& ascending)
{
    bool up = true;
    bool down = true;
    for (std::size_t i = 0; i < time.size(); ++i) {
        if (!std::isfinite(time[i])) {
            up = down = false;
            break;
        }
        if (i > 0) {
            up = up && time[i - 1] <= time[i];
            down = down && time[i - 1] >= time[i];
        }
        if (!up && !down) {
            break;
        }
    }
    sorted = up || down;
    ascending = up;
}

/// Elements of `time` (its first `count`) before `x` in the order it runs.
long long before(const std::vector<double>& time, long long count, bool ascending, double x)
{
    long long low = 0;
    long long high = count;
    while (low < high) {
        const long long mid = low + (high - low) / 2;
        const double t = time[static_cast<std::size_t>(mid)];
        if (ascending ? t < x : t >= x) {
            low = mid + 1;
        }
        else {
            high = mid;
        }
    }
    return low;
}

} // namespace

struct Plot::Line
{
    std::string name;
    long long length = 0;
    bool finished = false;
    std::unique_ptr<PyramidBuilder> builder;
    LinePyramid pyramid;
    LineReader reader;
    std::vector<double> time;
    bool timeSorted = false;
    bool timeAscending = true;

    // The folds, kept until what they were folded for changes. Mutable,
    // because drawing is const and folding is how it is answered.
    mutable int summaryColumns = -1;
    mutable std::vector<double> summary;
    mutable std::vector<double> summaryAt;
    mutable long long stride = 1;
    mutable double step = 1.0;

    mutable std::optional<PlotWindow> window;
    mutable std::vector<double> closer;
    mutable std::vector<double> closerAt;

    mutable LogFoldGrid grid;
    mutable bool foldArmed = false;
    mutable const std::vector<double>* foldTime = nullptr;
    mutable std::vector<double> foldValues;
    mutable std::vector<double> foldXs;
    mutable bool foldSummarised = false;

    void forget() const
    {
        summaryColumns = -1;
        window.reset();
        closer.clear();
        closerAt.clear();
        foldArmed = false;
        grid.clear();
    }
};

Plot::Plot() = default;
Plot::~Plot() = default;
Plot::Plot(Plot&&) noexcept = default;
Plot& Plot::operator=(Plot&&) noexcept = default;

Plot::Line& Plot::at(int line)
{
    if (line < 0 || line >= lineCount()) {
        throw std::out_of_range("plotcore::Plot: no such line");
    }
    return *lines_[static_cast<std::size_t>(line)];
}

const Plot::Line& Plot::at(int line) const
{
    if (line < 0 || line >= lineCount()) {
        throw std::out_of_range("plotcore::Plot: no such line");
    }
    return *lines_[static_cast<std::size_t>(line)];
}

int Plot::lineCount() const
{
    return static_cast<int>(lines_.size());
}

long long Plot::budgetDoubles() const
{
    const long long bytes =
        budgetBytes_ > 0 ? budgetBytes_ : allowance(appetite_, physicalMemory());
    return bytes / static_cast<long long>(sizeof(double));
}

long long Plot::share() const
{
    return budgetDoubles() / std::max(lineCount(), 1);
}

long long Plot::heldDoubles() const
{
    long long held = 0;
    for (const std::unique_ptr<Line>& line : lines_) {
        held += static_cast<long long>(line->pyramid.doubles() + line->time.size());
    }
    return held;
}

void Plot::fitAll()
{
    // The free direction only. A share that has shrunk is honoured at once and
    // exactly; one that has grown would need elements no pyramid still has,
    // which only a reader can give back -- and a reader reaches below the base
    // anyway, so the picture is the same either way.
    const long long each = share();
    for (const std::unique_ptr<Line>& line : lines_) {
        if (!line->finished || line->pyramid.empty()) {
            continue;
        }
        const long long was = line->pyramid.baseBucket();
        (void)fitToBudget(line->pyramid, each);
        if (line->pyramid.baseBucket() != was) {
            line->forget();
        }
    }
}

int Plot::addLine(const double* values, long long count, std::string name)
{
    auto line = std::make_unique<Line>();
    line->name = std::move(name);
    const long long each = budgetDoubles() / (lineCount() + 1);
    if (values != nullptr && count > 0) {
        line->pyramid = pyramidOf(values, count, baseBucketFor(count, each));
        line->length = line->pyramid.length;
    }
    line->finished = true;
    lines_.push_back(std::move(line));
    fitAll();
    return lineCount() - 1;
}

int Plot::beginLine(long long count, std::string name)
{
    auto line = std::make_unique<Line>();
    line->name = std::move(name);
    const long long length = std::max<long long>(count, 0);
    const long long each = budgetDoubles() / (lineCount() + 1);
    line->builder = std::make_unique<PyramidBuilder>(length, baseBucketFor(length, each));
    line->length = length;
    lines_.push_back(std::move(line));
    return lineCount() - 1;
}

void Plot::appendToLine(int line, const double* values, long long count)
{
    Line& held = at(line);
    if (held.builder != nullptr) {
        held.builder->add(values, count);
    }
}

void Plot::finishLine(int line)
{
    Line& held = at(line);
    if (held.builder == nullptr) {
        return;
    }
    held.pyramid = held.builder->finish();
    held.length = held.pyramid.length;
    held.builder.reset();
    held.finished = true;
    held.forget();
    fitAll();
}

void Plot::setLineReader(int line, LineReader reader)
{
    Line& held = at(line);
    held.reader = std::move(reader);
    held.forget();
}

void Plot::removeLine(int line)
{
    (void)at(line);
    lines_.erase(lines_.begin() + line);
}

void Plot::clear()
{
    lines_.clear();
}

const std::string& Plot::name(int line) const
{
    return at(line).name;
}

void Plot::setName(int line, std::string name)
{
    at(line).name = std::move(name);
}

long long Plot::length(int line) const
{
    return at(line).length;
}

bool Plot::ready(int line) const
{
    return at(line).finished;
}

void Plot::setIndexAxis(double start, double step)
{
    if (!std::isfinite(start) || !std::isfinite(step) || step == 0.0) {
        return;
    }
    indexStart_ = start;
    indexStep_ = step;
    for (const std::unique_ptr<Line>& line : lines_) {
        line->forget();
    }
}

void Plot::setTime(const double* values, long long count)
{
    time_.assign(values, values + std::max<long long>(values == nullptr ? 0 : count, 0));
    orderOf(time_, timeSorted_, timeAscending_);
    for (const std::unique_ptr<Line>& line : lines_) {
        line->forget();
    }
}

void Plot::setLineTime(int line, const double* values, long long count)
{
    Line& held = at(line);
    held.time.assign(values, values + std::max<long long>(values == nullptr ? 0 : count, 0));
    orderOf(held.time, held.timeSorted, held.timeAscending);
    held.forget();
}

bool Plot::hasTime(int line) const
{
    return timeOf(at(line)) != nullptr;
}

const std::vector<double>* Plot::timeOf(const Line& line) const
{
    if (!line.time.empty()) {
        return &line.time;
    }
    if (!time_.empty()) {
        return &time_;
    }
    return nullptr;
}

void Plot::setAppetite(Appetite appetite)
{
    appetite_ = appetite;
    fitAll();
}

void Plot::setBudgetBytes(long long bytes)
{
    budgetBytes_ = std::max<long long>(bytes, 0);
    fitAll();
}

long long Plot::baseBucket(int line) const
{
    return at(line).pyramid.baseBucket();
}

void Plot::setPaneColumns(int columns)
{
    columns_ = std::max(columns, 1);
}

void Plot::setView(double xLow, double xHigh, bool xLog)
{
    if (!std::isfinite(xLow) || !std::isfinite(xHigh) || !(xHigh > xLow)) {
        viewSet_ = false;
        return;
    }
    viewLow_ = xLow;
    viewHigh_ = xHigh;
    xLog_ = xLog;
    viewSet_ = true;
}

LineExtent Plot::extent(int line) const
{
    const Line& held = at(line);
    LineExtent found;
    if (!held.finished || held.pyramid.empty()) {
        return found;
    }
    const Extremes all = extremesOver(held.pyramid, 0, held.pyramid.length);
    found.finite = all.found();
    found.minimum = all.lowest;
    found.maximum = all.highest;
    found.positive = smallestPositive(held.pyramid, found.positiveMinimum);
    return found;
}

Span Plot::xExtent(int line) const
{
    const Line& held = at(line);
    const long long count = held.length;
    if (const std::vector<double>* time = timeOf(held)) {
        const auto drawn = static_cast<std::size_t>(std::min<long long>(count, time->size()));
        double low = std::numeric_limits<double>::infinity();
        double high = -std::numeric_limits<double>::infinity();
        for (std::size_t i = 0; i < drawn; ++i) {
            const double t = (*time)[i];
            if (std::isfinite(t)) {
                low = std::min(low, t);
                high = std::max(high, t);
            }
        }
        return low <= high ? Span{low, high} : Span{0.0, 0.0};
    }
    const double first = indexStart_;
    const double last =
        indexStart_ + static_cast<double>(std::max<long long>(count - 1, 0)) * indexStep_;
    return {std::min(first, last), std::max(first, last)};
}

double Plot::xPositiveMinimum(int line) const
{
    const Line& held = at(line);
    if (const std::vector<double>* time = timeOf(held)) {
        const auto drawn = static_cast<std::size_t>(std::min<long long>(held.length, time->size()));
        double best = 0.0;
        for (std::size_t i = 0; i < drawn; ++i) {
            const double t = (*time)[i];
            if (t > 0.0 && std::isfinite(t) && (best == 0.0 || t < best)) {
                best = t;
            }
        }
        return best;
    }
    const Span span = xExtent(line);
    return gridPositiveMin(span.low, span.high, indexStep_);
}

bool Plot::positionsOf(const Line& line, double xLow, double xHigh, double& low, double& high) const
{
    if (const std::vector<double>* time = timeOf(line)) {
        const bool sorted = time == &line.time ? line.timeSorted : timeSorted_;
        const bool ascending = time == &line.time ? line.timeAscending : timeAscending_;
        if (!sorted) {
            return false;
        }
        const long long count =
            std::min<long long>(line.length, static_cast<long long>(time->size()));
        const long long a = before(*time, count, ascending, ascending ? xLow : xHigh);
        const long long b = before(*time, count, ascending, ascending ? xHigh : xLow);
        // One element either side, so the line enters and leaves the pane at
        // its edges.
        low = static_cast<double>(std::max<long long>(a - 1, 0));
        high = static_cast<double>(std::min<long long>(b + 1, count));
        return true;
    }
    low = (xLow - indexStart_) / indexStep_;
    high = (xHigh - indexStart_) / indexStep_;
    if (low > high) {
        std::swap(low, high);
    }
    return std::isfinite(low) && std::isfinite(high);
}

void Plot::foldSummary(Line& line) const
{
    if (line.summaryColumns == columns_) {
        return;
    }
    line.summary.clear();
    line.summaryAt.clear();
    if (!fillWhole(line.pyramid, columns_, line.summary, line.stride, line.step, &line.summaryAt)) {
        line.stride = 1;
        line.step = 1.0;
    }
    line.summaryColumns = columns_;
}

void Plot::foldCloser(Line& line) const
{
    double low = 0.0;
    double high = 0.0;
    if (!viewSet_ || !positionsOf(line, viewLow_, viewHigh_, low, high)) {
        line.window.reset();
        return;
    }
    const auto length = static_cast<double>(line.length);
    low = std::clamp(low, 0.0, length);
    high = std::clamp(high, 0.0, length);
    // The drawn run's resolution, as the viewer's: a run twice the pane, at
    // the bucket that puts one of them on each column of it.
    const int buckets = 2 * columns_;
    std::optional<PlotWindow> want = windowFor(low, high, line.length, buckets);
    if (!want.has_value()) {
        line.window.reset();
        return;
    }
    const long long base = line.pyramid.baseBucket();
    if (want->bucket < base && !line.reader) {
        // Finer than anything held, and nothing to read it from: the finest
        // run the pyramid can fold, which is a run at its base. Asked for as
        // the span that puts one base bucket in each of `buckets` -- windowFor
        // rounds its bucket up to a power of two, and the base is one, so that
        // is the base exactly. Dividing the bucket count by how far short the
        // wanted bucket fell is not: the rounding can leave it short again,
        // and the closer look then fell all the way back to the summary --
        // two points across a window of a hundred elements.
        want =
            windowFor(low, low + static_cast<double>(base) * buckets / 2.0, line.length, buckets);
        if (!want.has_value() || want->bucket != base) {
            line.window.reset();
            return;
        }
    }
    if (line.window == want) {
        return;
    }
    line.closer.clear();
    line.closerAt.clear();
    if (want->bucket >= base) {
        if (!fillWindow(line.pyramid, *want, line.closer, &line.closerAt)) {
            line.window.reset();
            return;
        }
    }
    else {
        // Below the base: the reader, for this run and nothing else.
        const long long take = std::min(want->span, line.length - want->first);
        std::vector<double> raw(static_cast<std::size_t>(std::max<long long>(take, 0)));
        if (take <= 0 || !line.reader(want->first, take, raw.data())) {
            line.window.reset();
            return;
        }
        if (want->bucket == 1) {
            line.closer = std::move(raw);
        }
        else {
            reduceBucketsLocated(raw.data(), take, want->bucket, want->first, line.closer,
                                 line.closerAt);
        }
    }
    line.window = want;
}

bool Plot::foldLog(Line& line) const
{
    if (!xLog_ || !viewSet_) {
        return false;
    }
    const std::vector<double>* time = timeOf(line);
    const int mode = time != nullptr ? 1 : 0;
    if (line.foldArmed && line.foldTime == time &&
        line.grid.serves(indexStart_, indexStep_, mode, columns_, viewLow_, viewHigh_)) {
        return true;
    }
    line.foldArmed = false;
    line.grid.remake(indexStart_, indexStep_, mode, columns_, viewLow_, viewHigh_);
    if (!line.grid.columns.has_value()) {
        return false;
    }
    std::vector<double> edges;
    if (time == nullptr) {
        edgesAlong(*line.grid.columns, indexStart_, indexStep_, edges);
    }
    else {
        const bool sorted = time == &line.time ? line.timeSorted : timeSorted_;
        const bool ascending = time == &line.time ? line.timeAscending : timeAscending_;
        if (!sorted) {
            return false;
        }
        const long long count =
            std::min<long long>(line.length, static_cast<long long>(time->size()));
        for (long long k = line.grid.columns->first; k <= line.grid.columns->last; ++k) {
            edges.push_back(
                static_cast<double>(before(*time, count, ascending, line.grid.columns->edge(k))));
        }
        if (!ascending) {
            std::reverse(edges.begin(), edges.end());
        }
    }
    ColumnFold fold;
    foldColumns(line.pyramid, edges, fold);
    line.foldValues = std::move(fold.values);
    line.foldXs.resize(fold.positions.size());
    for (std::size_t i = 0; i < fold.positions.size(); ++i) {
        const double p = fold.positions[i];
        if (time == nullptr) {
            line.foldXs[i] = indexStart_ + p * indexStep_;
        }
        else {
            const auto index = static_cast<std::size_t>(std::max(p, 0.0));
            line.foldXs[i] =
                index < time->size() ? (*time)[index] : std::numeric_limits<double>::quiet_NaN();
        }
    }
    line.foldSummarised = fold.summarised;
    line.foldTime = time;
    line.foldArmed = true;
    return true;
}

PlotLine Plot::drawLine(int line) const
{
    const Line& held = at(line);
    PlotLine drawn;
    if (!held.finished || held.pyramid.empty()) {
        return drawn;
    }
    auto& line_ = const_cast<Line&>(held);
    if (foldLog(line_)) {
        drawn.values = held.foldValues.data();
        drawn.count = static_cast<long long>(held.foldValues.size());
        drawn.xs = held.foldXs.data();
        drawn.summarised = held.foldSummarised;
        return drawn;
    }
    foldSummary(line_);
    foldCloser(line_);
    if (held.window.has_value()) {
        double low = 0.0;
        double high = 0.0;
        if (positionsOf(held, viewLow_, viewHigh_, low, high) &&
            held.window->covers(std::clamp(low, 0.0, static_cast<double>(held.length)),
                                std::clamp(high, 0.0, static_cast<double>(held.length)))) {
            drawn.values = held.closer.data();
            drawn.count = static_cast<long long>(held.closer.size());
            drawn.positionStart = static_cast<double>(held.window->first);
            drawn.positionStep =
                held.window->bucket == 1 ? 1.0 : static_cast<double>(held.window->bucket) / 2.0;
            if (!held.closerAt.empty() && held.closerAt.size() == held.closer.size()) {
                drawn.positions = held.closerAt.data();
            }
            drawn.summarised = held.window->bucket > 1;
            return drawn;
        }
    }
    drawn.values = held.summary.data();
    drawn.count = static_cast<long long>(held.summary.size());
    drawn.positionStep = held.step;
    if (!held.summaryAt.empty() && held.summaryAt.size() == held.summary.size()) {
        drawn.positions = held.summaryAt.data();
    }
    drawn.summarised = held.stride > 1;
    return drawn;
}

PlotAxis Plot::drawAxis(int line) const
{
    const Line& held = at(line);
    PlotAxis axis;
    axis.start = indexStart_;
    axis.step = indexStep_;
    if (const std::vector<double>* time = timeOf(held)) {
        // Held whole, so a position is an element and its x is that element's
        // time, exactly.
        axis.values = time->data();
        axis.count = static_cast<long long>(time->size());
        axis.valueStep = 1.0;
    }
    return axis;
}

} // namespace plotcore
