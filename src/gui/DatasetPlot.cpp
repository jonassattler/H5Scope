// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "DatasetPlot.hpp"

#include "gui/PlotBudget.hpp"
#include "gui/PlotLevels.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace gui {

DatasetPlot::DatasetPlot(DatasetTableModel* table, QObject* parent) : QObject(parent), table_(table)
{
    // The closer look waits for the gesture to stop. Restarted by every push of
    // the view, so a wheel spun through six octaves reads once and a drag reads
    // when it ends -- and what is already on screen keeps being drawn until it
    // does.
    // One budget, shared out between this and every custom tab -- so opening a
    // tab makes every other one's share smaller and closing it hands the memory
    // back, rather than each holding its own constant and the window holding
    // the sum of them.
    PlotBudget::instance().join();
    connect(&PlotBudget::instance(), &PlotBudget::changed, this, &DatasetPlot::applyBudget);

    settle_.setSingleShot(true);
    settle_.setInterval(kSettleMilliseconds);
    connect(&settle_, &QTimer::timeout, this, &DatasetPlot::askForDetail);

    // And the pane waits for the drag to stop. A window dragged from narrow to
    // wide crosses a dozen column quanta, and each crossing used to be a
    // re-read of every drawn line -- which the reader saw as a plot flickering
    // under their hand while they were still deciding how wide they wanted it.
    resize_.setSingleShot(true);
    resize_.setInterval(kResizeMilliseconds);
    connect(&resize_, &QTimer::timeout, this, &DatasetPlot::applyColumns);

    // Two different events, deliberately. setDataset() and setLayout() both
    // reset the model, but only one of them is a new selection.
    connect(table_, &DatasetTableModel::datasetChanged, this, [this] {
        // A vector is a one-column table, because defaultOnX keeps a rank-1
        // dimension on the row axis so it still reads as a column in the grid.
        // Drawing "each row as a line" there would produce a thousand lines of
        // one point each, which is not a plot of anything. Chosen once per
        // selection rather than bound, so the reader stays free to switch back
        // -- and so that merely narrowing a dimension does not switch it back
        // for them.
        seriesFromRows_ = !(table_->columnCount() == 1 && table_->rowCount() > 1);
        reseed();
        // datasetChanged arrives after the modelReset that setDataset also
        // emits, so this is the last word on what the plot is: say so again
        // rather than leave QML holding the state from a tenth of a second ago.
        invalidate();
    });

    connect(table_, &QAbstractItemModel::modelReset, this, [this] {
        // The selection goes with the table: line 400 of the old arrangement
        // names a different row of the new one, so keeping the ticks would be
        // keeping the wrong ones.
        reseed();
        invalidate();
    });
}

DatasetPlot::~DatasetPlot()
{
    // Disconnected first, and this is not tidiness. leave() tells every other
    // plot that its share just grew, and a signal emitted from a destructor
    // reaches this object's own slot as well -- ~QObject, which is what breaks
    // the connections, runs after this body. applyBudget() then asks the model
    // how long the line is, through a table that is already gone.
    disconnect(&PlotBudget::instance(), nullptr, this, nullptr);
    PlotBudget::instance().leave();
}

void DatasetPlot::applyBudget()
{
    // Nothing is re-read and nothing is released: what is drawn is sized by the
    // pane and by kDrawBudget, neither of which this touches. What changes is
    // how many runs there is room to keep beside it.
    trimLevels();
    refreshDetail();
}

void DatasetPlot::releaseDrawing() const
{
    if (drawing_ != nullptr) {
        drawing_->clear();
        drawing_ = nullptr;
    }
}

void DatasetPlot::retire(std::map<int, std::vector<double>>& cache) const
{
    if (cache.empty()) {
        return;
    }
    if (drawing_ == nullptr) {
        // Nothing is reading it, so there is nothing to keep it alive for. This
        // is also what bounds the store: without it a plot nobody is drawing --
        // headless, or on a tab that is not on screen -- would accumulate one
        // cache per change until something filled a renderer.
        cache.clear();
        return;
    }
    // The values out of the map rather than the map itself, and that is the
    // whole of what a retired line is: a buffer nothing must free yet. Moving a
    // std::vector takes the buffer with it, so every pointer the renderer was
    // given goes on naming the same doubles -- and a std::vector<double> move
    // is noexcept, so growing the store can only ever move them too. Keeping
    // the maps here meant growing a std::vector of std::map, which takes the
    // copy on a library whose map move is not noexcept and frees what this
    // exists to protect. See the note on Detail.
    //
    // `drawing_` is deliberately left alone -- the item is still reading these
    // values and is still the thing that has to be emptied if they ever do have
    // to go.
    retired_.reserve(retired_.size() + cache.size());
    for (auto& held : cache) {
        if (!held.second.empty()) {
            retired_.push_back(std::move(held.second));
        }
    }
    cache.clear();
}

void DatasetPlot::retire(std::vector<double>& values) const
{
    if (values.empty() || drawing_ == nullptr) {
        values.clear();
        return;
    }
    retired_.push_back(std::move(values));
    values.clear();
}

void DatasetPlot::invalidate()
{
    // The one place that empties the renderer rather than retiring what it is
    // reading. A new dataset is not a coarser reading of the old one: drawing
    // the old line for another frame would be drawing the wrong file.
    releaseDrawing();
    lines_.clear();
    // And the lines they were folded out of. A pyramid is a reading of one
    // dataset's elements; carrying it into another would be drawing the wrong
    // file, which is the same thing releaseDrawing() above is here to prevent.
    pyramids_.clear();
    retired_.clear();
    // And the closer look with them. A new table is a new window onto it: what
    // was being looked at closely was a run of the old one, and the range the
    // surface last pushed is in the old table's x. Both are forgotten here and
    // the surface pushes the current view again on its next refill, which is
    // why this does not try to keep them.
    clearDetail();
    viewMin_ = 0.0;
    viewMax_ = 0.0;
    // The geometry of the table went with them. Unlike the extent below, these
    // two are not recomputed on every ensure() -- they belong to the table
    // rather than to the drawn set, so hiding a line must not disturb them --
    // which makes this the one place they are cleared.
    points_ = 0;
    step_ = 1.0;
    sampled_ = false;
    emit changed();
}

void DatasetPlot::reseed()
{
    // The first sixty-four, not all of them. Strokes over one another stop
    // separating at a few dozen, so a table of ten thousand rows drawn whole
    // is a picture of nothing that takes a while to produce -- and the reader
    // waits for it before they have asked for anything at all.
    //
    // What makes a window honest is saying so, and both readouts do: the
    // legend's header prints "64 / 10000" and the footer "64 lines of 10000".
    // Nothing is out of reach behind it -- every line of the table is listed
    // in the legend and tickable, and the legend's `all` is one press.
    selectFirst(kMaxInitialSeries);
}

void DatasetPlot::setSeriesFromRows(bool fromRows)
{
    if (seriesFromRows_ == fromRows) {
        return;
    }
    seriesFromRows_ = fromRows;
    reseed(); // the axes swapped; the old selection names nothing now
    invalidate();
}

void DatasetPlot::selectAll()
{
    selectFirst(sourceSeriesCount());
}

int DatasetPlot::pointsFor(int lines) const
{
    // A bucket is a column, and a bucket answers with two values.
    const int pane = 2 * columns_;
    if (lines <= 0) {
        return std::clamp(pane, kMinPoints, kMaxPoints);
    }
    return std::clamp(std::min(kDrawBudget / lines, pane), kMinPoints, kMaxPoints);
}

void DatasetPlot::applyCap(int cap)
{
    if (cap == cap_) {
        return;
    }
    // A line already held was read at the old resolution, with an x arithmetic
    // that is one number for all of them, so it has to be read again. It is
    // retired rather than freed: until the new reading is in hand it is the
    // picture on screen, and a pane that empties itself while the reader
    // resizes the window is a worse reading of the data than one drawn at the
    // bucket they had a moment ago.
    retire(lines_);
    // The pyramids stay. A different pane width is a different *fold* of the
    // same elements, and those elements are in hand -- so what used to be a
    // re-read of every drawn line on every sixty-four pixels of a window drag
    // is now arithmetic over a buffer, which is why the resize debounce buys
    // less than it used to and costs nothing to keep.
    //
    // The closer look is cut to the same budget -- its buckets are worked out
    // from `cap_` -- so a line held at the old one is a line at the wrong
    // resolution with the wrong x arithmetic. It goes the same way.
    clearDetail();
    cap_ = cap;
    sampled_ = false;
}

void DatasetPlot::applyColumns()
{
    if (wantedColumns_ == columns_) {
        return;
    }
    columns_ = wantedColumns_;
    const int cap = pointsFor(static_cast<int>(drawn_.size()));
    if (cap == cap_) {
        // A wider pane that asks for the same number of points is not a
        // re-read: the budget, not the pane, was what bounded this line.
        return;
    }
    applyCap(cap);
    emit changed();
    refreshDetail();
}

void DatasetPlot::setPaneColumns(int columns)
{
    // Down to the quantum, and never to nothing. See kColumnQuantum for why
    // down rather than to the nearest.
    const int quantised = std::clamp((std::max(columns, 0) / kColumnQuantum) * kColumnQuantum,
                                     kMinPoints / 2, kMaxPoints / 2);
    if (quantised == wantedColumns_) {
        return;
    }
    wantedColumns_ = quantised;
    if (wantedColumns_ == columns_) {
        // Dragged out and back again inside one gesture. Nothing to do, and
        // nothing to wait for either.
        resize_.stop();
        return;
    }
    if (!measured_) {
        // The surface measuring itself for the first time. There is no gesture
        // to wait out and nothing for the wait to protect: whatever has been
        // read so far was read at an assumed width, so making this one wait
        // would open every plot at the wrong resolution and re-read every line
        // of it a fifth of a second later.
        measured_ = true;
        resize_.stop();
        applyColumns();
        return;
    }
    // Otherwise nothing happens here. See the note on the declaration: the read
    // is at the end of the drag, not once per sixty-four pixels of it.
    resize_.start();
}

void DatasetPlot::selectFirst(int count)
{
    const int total = std::min(std::max(count, 0), sourceSeriesCount());
    drawn_.clear();
    drawn_.reserve(static_cast<std::size_t>(total));
    for (int series = 0; series < total; ++series) {
        drawn_.push_back(series);
    }

    // How many points a line gets depends on how many lines there are, so a
    // bulk change of the drawn set can change it -- and a line already held was
    // read at the old resolution, with an x arithmetic that is one number for
    // all of them. Those lines go.
    //
    // Only here, and pointedly not in setSeriesVisible: ticking one name in a
    // legend of ten thousand must not re-read the other nine thousand nine
    // hundred and ninety-nine.
    applyCap(pointsFor(total));

    sampled_ = false;
    emit changed();
    refreshDetail();
}

void DatasetPlot::selectNone()
{
    drawn_.clear();
    sampled_ = false;
    emit changed();
    refreshDetail();
}

QString DatasetPlot::seriesExpression(int series) const
{
    return table_ == nullptr ? QString{} : table_->lineExpression(series, seriesFromRows_);
}

bool DatasetPlot::seriesVisible(int series) const
{
    return std::binary_search(drawn_.begin(), drawn_.end(), series);
}

void DatasetPlot::setSeriesVisible(int series, bool visible)
{
    if (series < 0 || series >= sourceSeriesCount()) {
        return;
    }
    // Kept ascending, so the legend, the drawn order and the colour a line
    // takes from a ramp all agree with the table's own order -- and so that
    // asking whether a line is drawn is a binary search rather than a scan.
    const auto at = std::lower_bound(drawn_.begin(), drawn_.end(), series);
    const bool present = at != drawn_.end() && *at == series;
    if (present == visible) {
        return;
    }
    if (visible) {
        drawn_.insert(at, series);
    }
    else {
        drawn_.erase(at);
    }
    sampled_ = false;
    emit changed();
    // A line ticked while the reader is looking closely has no closer look of
    // its own yet. It draws from the whole-line summary -- correct, and coarse
    // -- until the next settle reads the one run it is missing.
    refreshDetail();
}

long long DatasetPlot::pyramidBudget() const
{
    // The share this plot may hold, split between the lines it is drawing.
    // Twice, because what is held has to leave room for the runs in `levels_`
    // that a bucket below the base still reads -- and because a budget spent to
    // the last double is a budget with nothing left for the frame that finds it
    // was a little short.
    const int lines = std::max(static_cast<int>(drawn_.size()), 1);
    return PlotBudget::instance().share() / (2LL * lines);
}

void DatasetPlot::buildPyramids() const
{
    // Every line not already held, in one crossing rather than one per line --
    // the argument readMissing used to make here, and it has not changed: each
    // crossing is a blocking round trip with a handshake at both ends, and it
    // was the handshake rather than the read that made the legend's `all` on a
    // ten-thousand-row table stop the window.
    std::vector<int> wanted;
    for (const int series : drawn_) {
        if (pyramids_.find(series) == pyramids_.end()) {
            wanted.push_back(series);
        }
    }
    if (wanted.empty()) {
        return;
    }
    const long long budget = pyramidBudget();

    // In batches, for the reason the summaries were read in batches: one
    // crossing for ten thousand lines would hold ten thousand answers in hand
    // *and* the copies of them going into the cache.
    for (std::size_t first = 0; first < wanted.size(); first += kReadBatch) {
        const std::size_t last = std::min(first + kReadBatch, wanted.size());

        std::vector<DatasetTableModel::PyramidRequest> requests;
        requests.reserve(last - first);
        for (std::size_t i = first; i < last; ++i) {
            requests.push_back(
                DatasetTableModel::PyramidRequest{wanted[i], seriesFromRows_, budget});
        }

        QString refused;
        std::vector<LinePyramid> built = table_->samplePyramids(requests, refused);
        if (!refused.isEmpty() && error_.isEmpty()) {
            error_ = refused;
        }
        const std::size_t count = std::min(last - first, built.size());
        for (std::size_t i = 0; i < count; ++i) {
            pyramids_.insert_or_assign(wanted[first + i], std::move(built[i]));
        }
    }
}

void DatasetPlot::readMissing() const
{
    // One pass over each line, and everything after it is arithmetic.
    //
    // This used to read a line thinned to `cap_` points and throw the other ten
    // million elements away -- and then read them again for the closer look,
    // and again for the one after that. It reads them once now and keeps them,
    // at the finest bucket the budget affords, so the summary below and every
    // run the reader zooms to afterwards are folds of a buffer already in hand.
    //
    // Nothing about the picture changes. fillWhole() answers with exactly what
    // a read at that stride answered with, element for element, because
    // coarsening an envelope is exact -- which is the property PlotLevels.hpp
    // states and PlotPyramid.hpp spends.
    buildPyramids();

    std::map<int, std::vector<double>> replaced;
    for (const int series : drawn_) {
        const auto pyramid = pyramids_.find(series);
        if (pyramid == pyramids_.end() || pyramid->second.empty()) {
            continue;
        }
        if (lines_.find(series) != lines_.end()) {
            continue; // already folded at this width
        }
        std::vector<double> summary;
        long long stride = 1;
        double step = 1.0;
        // Half as many buckets as points, because an envelope answers with two
        // values for each of them -- so a line still arrives as at most
        // kMaxPoints doubles and nothing about what this costs in memory
        // changes.
        if (!fillWhole(pyramid->second, cap_ / 2, summary, stride, step)) {
            continue;
        }
        // Every line covers the same extent of the other axis, so these are the
        // same for all of them and the last word is as good as the first. The
        // extent is what the x axis is drawn against, so it has to be one
        // number rather than one per line.
        points_ = static_cast<int>(summary.size());
        step_ = step;
        lines_.emplace(series, std::move(summary));
    }
}

void DatasetPlot::ensure() const
{
    if (sampled_) {
        return;
    }
    sampled_ = true;
    minimum_ = 0.0;
    maximum_ = 0.0;
    hasFinite_ = false;
    error_.clear();

    // What is no longer drawn is no longer held: the cache exists to spare a
    // re-read of a line still on screen, not to accumulate every line the
    // reader has ever ticked.
    //
    // A line the renderer is reading cannot simply be erased, so the ones that
    // go are moved into the retired store instead -- the same double buffering
    // applyCap() uses, for the same reason. Extracting a node keeps the vector
    // exactly where it is, which is what any pointer already handed out
    // requires.
    const auto prune = [this](std::map<int, std::vector<double>>& cache) {
        std::map<int, std::vector<double>> gone;
        for (auto it = cache.begin(); it != cache.end();) {
            if (seriesVisible(it->first)) {
                it = std::next(it);
                continue;
            }
            const auto at = it++;
            gone.insert(cache.extract(at));
        }
        retire(gone);
    };
    prune(lines_);
    for (Detail& level : levels_) {
        prune(level.lines);
    }
    // The pyramids go the blunt way, because nothing borrows them: what the
    // renderer holds a pointer into is always a vector in `lines_` or in a
    // Detail, both of which have just been retired rather than freed.
    for (auto it = pyramids_.begin(); it != pyramids_.end();) {
        it = seriesVisible(it->first) ? std::next(it) : pyramids_.erase(it);
    }

    readMissing();

    for (const int series : drawn_) {
        const auto held = lines_.find(series);
        if (held == lines_.end()) {
            continue; // it did not read; readMissing() kept the reason
        }
        for (const double value : held->second) {
            if (!std::isfinite(value)) {
                continue;
            }
            if (!hasFinite_) {
                minimum_ = value;
                maximum_ = value;
                hasFinite_ = true;
            }
            else {
                minimum_ = std::min(minimum_, value);
                maximum_ = std::max(maximum_, value);
            }
        }
    }

    if (drawn_.empty()) {
        // Nothing was read, so nothing has reported why it could not be. A
        // zero-sized sample answers that without touching the file:
        // sampleValues checks the datatype before it reads anything.
        error_ = table_->sampleValues(0, 0, 1, 0, 0, 1).error;
    }
}

QVariantList DatasetPlot::drawnSeries() const
{
    QVariantList series;
    series.reserve(static_cast<qsizetype>(drawn_.size()));
    for (const int index : drawn_) {
        series.append(index);
    }
    return series;
}

int DatasetPlot::seriesCount() const
{
    return static_cast<int>(drawn_.size());
}

int DatasetPlot::pointCount() const
{
    ensure();
    // What is drawn, which is the closer look while there is one. The footer
    // says how many points are on screen, and the answer changed the moment
    // part of the line started being read at a finer bucket than the rest of it
    // ever was.
    const int at = drawnLevel();
    return at >= 0 && levels_[static_cast<std::size_t>(at)].points > 0
               ? levels_[static_cast<std::size_t>(at)].points
               : points_;
}

int DatasetPlot::sourceSeriesCount() const
{
    return seriesFromRows_ ? table_->rowCount() : table_->columnCount();
}

bool DatasetPlot::thinned() const
{
    ensure();
    const int at = drawnLevel();
    return at >= 0 && levels_[static_cast<std::size_t>(at)].points > 0
               ? levels_[static_cast<std::size_t>(at)].step > 1.0
               : step_ > 1.0;
}

double DatasetPlot::minimum() const
{
    ensure();
    return hasFinite_ ? minimum_ : 0.0;
}

double DatasetPlot::maximum() const
{
    ensure();
    return hasFinite_ ? maximum_ : 0.0;
}

int DatasetPlot::sourcePointCount() const
{
    // The table's own length along x, not the thinned one: thinning is how
    // many of the points are drawn, and says nothing about how long the data
    // is. This is the number the default axis is 0 : 1 : len(data) of.
    return seriesFromRows_ ? table_->columnCount() : table_->rowCount();
}

void DatasetPlot::setXStart(double value)
{
    if (qFuzzyCompare(xStart_, value)) {
        return;
    }
    xStart_ = value;
    emit xAxisChanged();
    // The axis decides what a position is worth in x, so moving it moves which
    // elements the reader is looking at without the view having moved at all.
    refreshDetail();
}

void DatasetPlot::setXStep(double value)
{
    if (qFuzzyCompare(xStep_, value)) {
        return;
    }
    xStep_ = value;
    emit xAxisChanged();
    refreshDetail();
}

bool DatasetPlot::numeric() const
{
    return table_->numeric();
}

bool DatasetPlot::hasData() const
{
    ensure();
    return hasFinite_ && !drawn_.empty() && points_ > 0;
}

QString DatasetPlot::error() const
{
    ensure();
    return error_;
}

QString DatasetPlot::seriesLabel(int series) const
{
    const QString label = seriesFromRows_ ? table_->rowLabel(series) : table_->columnLabel(series);
    // An axis carrying no dimension has no tuple to print -- a vector plotted
    // as one line is the case -- and a line still has to be called something.
    return label.isEmpty() ? QString::number(series) : label;
}

PlotLine DatasetPlot::lineOf(int series) const
{
    ensure();
    PlotLine line;

    // The closer look, while it covers what is on screen and this line has one.
    //
    // It is a second summary of the same line rather than a replacement for the
    // first, and this is where that pays: the moment the reader zooms out past
    // the run in hand, or pans off the end of it, the whole-line summary below
    // is what is drawn. That one covers everything by construction, so a zoom
    // out is a correct picture in the same frame instead of half a line until a
    // read lands.
    const int at = drawnLevel();
    if (at >= 0) {
        const Detail& level = levels_[static_cast<std::size_t>(at)];
        const auto closer = level.lines.find(series);
        if (closer != level.lines.end() && !closer->second.empty()) {
            // Where the run starts, in the same table positions the whole-line
            // summary counts in -- which is the whole of what PlotLine needs to
            // draw a piece of a line in the right place.
            line.positionStart = static_cast<double>(level.window.first);

            line.values = closer->second.data();
            line.count = static_cast<qsizetype>(closer->second.size());
            line.positionStep = level.step;
            return line;
        }
    }

    const auto held = lines_.find(series);
    if (held == lines_.end() || points_ <= 0) {
        return line;
    }
    line.values = held->second.data();
    line.count = static_cast<qsizetype>(held->second.size());
    // Where the element sits, not where the drawn point sits: a thinned line
    // skips step_ elements between one drawn point and the next, so its x has
    // to skip the same distance. With the default axis and no thinning this is
    // the element's own index, which is what the grid's column headers count.
    //
    // Half a bucket when the line was read as an envelope: the two values of a
    // bucket are its extremes, they occurred somewhere inside it, and putting
    // them at its start and its middle is the nearest thing to where they were
    // that costs nothing to say. A bucket is about a pixel wide, so the error
    // is half of one.
    line.positionStep = step_;
    return line;
}

PlotAxis DatasetPlot::drawingAxis() const
{
    PlotAxis axis;
    axis.start = xStart_;
    axis.step = xStep_;
    return axis;
}

// ---------------------------------------------------------------------------
// The closer look
// ---------------------------------------------------------------------------

void DatasetPlot::setVisibleRange(double xMin, double xMax)
{
    if (viewMin_ == xMin && viewMax_ == xMax) {
        return;
    }
    // Whether the run in hand still covers the pane is a property of the view,
    // so it can change without anything being read -- and when it does, a
    // different set of values has to reach the renderer. That is what this is
    // watching for; the read, if there is one, is a tenth of a second away.
    const std::optional<PlotWindow> drawing = drawnWindow();
    viewMin_ = xMin;
    viewMax_ = xMax;
    refreshDetail();
    if (drawnWindow() != drawing) {
        emit changed();
    }
}

void DatasetPlot::setZoomFocus(double x, double factor)
{
    if (!std::isfinite(x) || !std::isfinite(factor) || !(factor > 0.0)) {
        clearZoomFocus();
        return;
    }
    focusX_ = x;
    focusInward_ = factor > 1.0;
    focusActive_ = true;
    // Not a read of its own: setVisibleRange arrives in the same turn of the
    // event loop with the range this zoom produced, and that is what decides
    // whether anything is worth reading. This only says which way it went.
}

void DatasetPlot::clearZoomFocus()
{
    focusActive_ = false;
}

PlotFocus DatasetPlot::focusFor() const
{
    PlotFocus focus;
    if (!focusActive_ || !std::isfinite(xStart_) || !std::isfinite(xStep_) ||
        !(std::abs(xStep_) > 0.0)) {
        return focus;
    }
    // x = start + position * step, so a position is the same arithmetic run
    // backwards -- the mapping visiblePositions() uses, over one value.
    const double position = (focusX_ - xStart_) / xStep_;
    if (!std::isfinite(position)) {
        return focus;
    }
    focus.position = position;
    focus.inward = focusInward_;
    focus.active = true;
    return focus;
}

bool DatasetPlot::visiblePositions(double& first, double& last) const
{
    if (!std::isfinite(viewMin_) || !std::isfinite(viewMax_) || !std::isfinite(xStart_) ||
        !std::isfinite(xStep_) || !(std::abs(xStep_) > 0.0)) {
        return false;
    }
    // x = start + position * step, so a position is the same arithmetic run
    // backwards. A negative step draws the line right to left and the two ends
    // change places with it.
    double low = (viewMin_ - xStart_) / xStep_;
    double high = (viewMax_ - xStart_) / xStep_;
    if (low > high) {
        std::swap(low, high);
    }
    if (!std::isfinite(low) || !std::isfinite(high) || !(high > low)) {
        return false;
    }
    first = low;
    last = high;
    return true;
}

std::optional<PlotWindow> DatasetPlot::detailFor(int buckets) const
{
    if (drawn_.empty() || static_cast<int>(drawn_.size()) > kWindowedSeries) {
        // Past a few hundred strokes over one another the picture is a
        // distribution rather than a line, and re-reading every one of them
        // each time the reader zooms would spend the whole cost of the
        // selection again to sharpen something nobody can follow.
        return {};
    }
    double low = 0.0;
    double high = 0.0;
    if (!visiblePositions(low, high)) {
        return {};
    }
    return windowFor(low, high, static_cast<long long>(sourcePointCount()), buckets);
}

int DatasetPlot::paneBuckets() const
{
    // Half as many buckets as points, because an envelope answers with two
    // values for each of them -- the same trade readMissing() makes.
    return cap_ / 2;
}

int DatasetPlot::detailBuckets() const
{
    // One octave finer than the pane needs, which is the cheapest prefetch
    // there is.
    //
    // A read costs its *span*: an envelope moves every element of the run
    // whatever bucket it folds them into, so asking for twice as many buckets
    // over the same run touches exactly the same file and the same number of
    // hyperslabs, and answers with twice as many points. What that buys is the
    // next octave in: the reader's next step down is already in hand and is a
    // draw rather than a read, where before it was a stretched picture for a
    // tenth of a second and then a jump. It also draws better standing still --
    // four points a column rather than two is real detail inside the column
    // rather than a vertical bar across it.
    //
    // Nothing like it exists for the octave *out*: that view is wider than this
    // run, and no amount of resolution inside a run makes it cover more of the
    // line. kPrefetchOctaves is how that direction is answered instead.
    //
    // Taken only while the drawn set can afford to hold it twice, because that
    // is what it costs: every drawn line holds a whole-line summary and a run,
    // and this doubles the second of them.
    const int lines = std::max(static_cast<int>(drawn_.size()), 1);
    const long long share = PlotBudget::instance().share();
    return share / lines >= 2LL * cap_ ? 2 * paneBuckets() : paneBuckets();
}


int DatasetPlot::heldLevels() const
{
    // A run is about `cap_` doubles per line -- paneBuckets() buckets, and a
    // bucket answers with two values -- so how many of them there is room for
    // is the budget divided by what one costs. One line gets all of them; a
    // selection wide enough to be spending the budget on the summaries
    // themselves keeps the run it is on and nothing else.
    const int lines = std::max(static_cast<int>(drawn_.size()), 1);
    const long long affordable =
        PlotBudget::instance().share() / std::max<long long>(lines * 2LL * cap_, 1);
    return static_cast<int>(std::clamp<long long>(affordable, 1, kHeldLevels));
}

bool DatasetPlot::drawnPositions(double& first, double& last) const
{
    if (!visiblePositions(first, last)) {
        return false;
    }
    // Against the data rather than against the view: a pane showing the end of
    // the line shows some empty axis past it, and a run that reaches the last
    // element covers everything there is to draw out there.
    first = std::max(first, 0.0);
    last = std::min(last, static_cast<double>(sourcePointCount()));
    return true;
}

LevelView DatasetPlot::levelView() const
{
    LevelView view;
    if (drawn_.empty() || static_cast<int>(drawn_.size()) > kWindowedSeries) {
        // Past a few hundred strokes over one another the picture is a
        // distribution rather than a line, and re-reading every one of them
        // each time the reader zooms would spend the whole cost of the
        // selection again to sharpen something nobody can follow. An unusable
        // view is how that is said to the policy.
        return view;
    }
    if (!visiblePositions(view.low, view.high)) {
        return view;
    }
    view.length = static_cast<long long>(sourcePointCount());
    view.paneBuckets = paneBuckets();
    view.detailBuckets = detailBuckets();
    view.prefetchOctaves = kPrefetchOctaves;
    return view;
}

std::span<const HeldLevel> DatasetPlot::ladder() const
{
    // Refilled rather than rebuilt: clear() keeps the capacity, so this
    // allocates once for the life of the object. The completeness test is the
    // one the policy cannot make for itself -- a run read before a line was
    // ticked back on is a run this object cannot draw from, however well its
    // window covers the pane.
    ladder_.clear();
    ladder_.reserve(levels_.size());
    for (const Detail& level : levels_) {
        const bool whole =
            !level.lines.empty() && std::all_of(drawn_.begin(), drawn_.end(), [&level](int series) {
                return level.lines.find(series) != level.lines.end();
            });
        ladder_.push_back(HeldLevel{level.window, whole});
    }
    return ladder_;
}

int DatasetPlot::drawnLevel() const
{
    return gui::drawnLevel(ladder(), levelView());
}

std::optional<PlotWindow> DatasetPlot::drawnWindow() const
{
    const int at = drawnLevel();
    return at < 0 ? std::optional<PlotWindow>{}
                  : levels_[static_cast<std::size_t>(at)].window;
}

int DatasetPlot::levelAt(const PlotWindow& window) const
{
    for (std::size_t i = 0; i < levels_.size(); ++i) {
        if (levels_[i].window == window) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

std::optional<PlotWindow> DatasetPlot::detailWanted() const
{
    return gui::wantedLevel(ladder(), levelView(), focusFor());
}

void DatasetPlot::refreshDetail()
{
    if (!detailFor(paneBuckets()).has_value()) {
        dropDetail();
        return;
    }

    // Everything the pyramids can answer, now, in this call.
    //
    // A run at or above a line's base bucket is a fold of a buffer already in
    // hand, so there is nothing to wait for and nothing to arm the next step
    // with -- which is why this loops where the read path returns. The whole
    // ladder the policy wants is usually memory: the run on screen, the four
    // octaves in towards the pointer and the two out, all of them folded before
    // the frame that asked is drawn. That is the difference between a zoom that
    // resolves as the reader turns the wheel and one that answers a tenth of a
    // second after they stop.
    //
    // Bounded by heldLevels() rather than trusted to terminate on its own. It
    // does terminate -- wantedLevel() never names a run that served() already
    // answers, and trimLevels() keeps the ladder at heldLevels() -- but a loop
    // whose exit is a policy in another file is a loop worth bounding here.
    bool filled = false;

    // The pane's own preferred run first, whether or not something coarser in
    // hand would already have covered it.
    //
    // wantedLevel() stops asking once *some* held run covers the view at a
    // bucket no coarser than the pane strictly needs, and that was the right
    // rule while every run cost a round trip: a run read on the way in is
    // usually a little finer than the next view out needs, and re-reading the
    // file to gain a fraction of an octave would have been a round trip spent
    // on almost nothing. The cost of settling for it is that the pane can be
    // drawn at up to an octave coarser than it asked -- about one drawn station
    // per column where it asked for two, which is what kSamplesPerColumn's
    // slack absorbs.
    //
    // Out of a held line that trade has no second side. The finer run is a fold
    // of a buffer already in hand, so there is nothing to weigh against it, and
    // the pane gets the resolution it asked for on every frame rather than on
    // the frames where the ladder happens to line up.
    if (const std::optional<PlotWindow> own = detailFor(detailBuckets());
        own.has_value() && fillDetail(*own)) {
        filled = true;
        trimLevels();
    }

    for (int step = 0; step <= heldLevels(); ++step) {
        wanted_ = detailWanted();
        if (!wanted_.has_value() || !fillDetail(*wanted_)) {
            break;
        }
        filled = true;
        trimLevels();
    }
    if (filled) {
        emit changed();
    }

    wanted_ = detailWanted();
    if (!wanted_.has_value()) {
        // Everything worth holding is held. Not a read, not a signal, not even
        // a timer -- which is what makes panning inside a run free.
        settle_.stop();
        return;
    }
    if (focusActive_) {
        // A zoom reads at once rather than waiting the gesture out.
        //
        // The settle was protecting a thread that can only run one job after
        // another from a wheel spun through six octaves, and it protected it by
        // making the reader wait a tenth of a second at the end of every
        // gesture for a picture that could have been arriving while they span.
        // What actually bounds the cost is asking for one run at a time and
        // letting the reply arm the next -- see inFlight_ -- and with that in
        // place the wait buys nothing. A pan still settles: a pan has no focus,
        // and a pan that leaves the run in hand would otherwise read on every
        // frame of the drag.
        settle_.stop();
        askForDetail();
        return;
    }
    settle_.start();
}

void DatasetPlot::trimLevels()
{
    const int allowed = heldLevels();
    while (static_cast<int>(levels_.size()) > allowed) {
        // Rebuilt each time round, because erasing one changes which of the
        // rest is coldest.
        const std::size_t worst = gui::coldestLevel(ladder(), levelView(), focusFor());
        retire(levels_[worst].lines);
        levels_.erase(levels_.begin() + static_cast<long>(worst));
    }
}

bool DatasetPlot::fillDetail(const PlotWindow& detail)
{
    if (drawn_.empty()) {
        return false;
    }
    // Every drawn line, or none of them. A run half folded out of memory and
    // half read from the file would be two pictures of one moment, and the half
    // that had not arrived yet would be the one the reader noticed.
    for (const int series : drawn_) {
        const auto pyramid = pyramids_.find(series);
        if (pyramid == pyramids_.end() || pyramid->second.empty() ||
            detail.bucket < pyramid->second.baseBucket()) {
            return false;
        }
    }

    int at = levelAt(detail);
    if (at < 0) {
        // The same hazard takeDetail() names: this push_back may reallocate
        // `levels_` while the renderer is reading a run already in it, so a
        // Detail is move-only and the relocation cannot be a copy.
        levels_.emplace_back(detail);
        at = static_cast<int>(levels_.size()) - 1;
    }
    Detail& level = levels_[static_cast<std::size_t>(at)];

    bool any = false;
    std::vector<double> folded;
    for (const int series : drawn_) {
        if (level.lines.find(series) != level.lines.end()) {
            continue; // already held at this run
        }
        if (!fillWindow(pyramids_[series], detail, folded)) {
            continue;
        }
        // Every line covers the same run, so these are the same for all of them
        // and the last word is as good as the first. They are what a read of
        // this run would have reported: a pair per bucket half a bucket apart,
        // or the elements themselves when the bucket is one.
        level.points = static_cast<int>(folded.size());
        level.step = detail.bucket == 1 ? 1.0 : static_cast<double>(detail.bucket) / 2.0;
        level.lines[series] = std::move(folded);
        folded.clear();
        any = true;
    }
    return any;
}

void DatasetPlot::askForDetail()
{
    if (!wanted_.has_value() || table_ == nullptr || !table_->present() || !table_->numeric()) {
        return;
    }
    if (inFlight_) {
        // One read out at a time. The reply arms the next one -- takeDetail
        // ends in refreshDetail() -- so nothing is lost by not asking now, and
        // what is gained is that a gesture cannot queue reads faster than the
        // one HDF5 thread can run them. Without it, dropping the settle would
        // put six runs of a spun wheel in a queue the reader has to wait out
        // before the one they stopped on is even started.
        return;
    }
    const PlotWindow want = *wanted_;

    // Every drawn line that has not been read at this run. Usually all of them
    // -- the run is one this object has never held -- and sometimes one, which
    // is a line ticked in the legend while the reader was already looking
    // closely.
    const int at = levelAt(want);
    std::vector<int> series;
    for (const int line : drawn_) {
        if (at < 0 || levels_[static_cast<std::size_t>(at)].lines.find(line) ==
                          levels_[static_cast<std::size_t>(at)].lines.end()) {
            series.push_back(line);
        }
    }
    if (series.empty()) {
        return;
    }

    std::vector<DatasetTableModel::SampleRequest> requests;
    requests.reserve(series.size());
    for (const int line : series) {
        requests.push_back(detailRequestFor(line, want));
    }

    // One crossing for the whole set, which is what kWindowedSeries is: the
    // guard is one kReadBatch, so a closer look is never more than a single job
    // however many lines are drawn.
    //
    // Submitted rather than waited for, and this is the whole point of the
    // arrangement. There is already a correct picture on screen; blocking the
    // window to replace it with a sharper one would be spending the reader's
    // attention to save them nothing. Anything asked for and no longer wanted
    // is disowned by its ticket rather than painted.
    asked_ = want;
    inFlight_ = true;
    H5Thread::instance().submit(
        requests_,
        [axes = table_->sharedAxes(), requests](H5Session& session) {
            const h5core::DataSource* source = session.source();
            return source == nullptr ? std::vector<DatasetTableModel::NumericGrid>(requests.size())
                                     : DatasetTableModel::readSamples(*source, *axes, requests);
        },
        [this, want, series](std::vector<DatasetTableModel::NumericGrid> grids) {
            inFlight_ = false;
            takeDetail(want, series, std::move(grids));
        });
}

void DatasetPlot::takeDetail(const PlotWindow& detail, const std::vector<int>& series,
                             std::vector<DatasetTableModel::NumericGrid> grids)
{
    asked_.reset();
    if (wanted_ != detail) {
        // The reader moved on while this was out. Dropping it is not a loss --
        // but arming the next read is not optional, because only one read is
        // out at a time now and this reply is what lets the next one go. A
        // return without it is a plot that stops reading until the reader
        // touches something.
        refreshDetail();
        return;
    }

    int at = levelAt(detail);
    if (at < 0) {
        // A resolution this object has not held before. Nothing is retired --
        // the runs it already has are the ones the reader zoomed through and
        // are exactly what makes going back free. Which is what this push_back
        // has to keep true: it may reallocate `levels_` while the renderer is
        // reading one of the runs already in it, so a Detail is move-only and
        // the relocation cannot be a copy. See the declaration.
        levels_.emplace_back(detail);
        at = static_cast<int>(levels_.size()) - 1;
    }
    Detail& level = levels_[static_cast<std::size_t>(at)];

    const std::size_t count = std::min(series.size(), grids.size());
    for (std::size_t i = 0; i < count; ++i) {
        DatasetTableModel::NumericGrid& grid = grids[i];
        if (!grid.values.empty()) {
            // Every line covers the same run, so these are the same for all of
            // them and the last word is as good as the first.
            level.points = seriesFromRows_ ? grid.columns : grid.rows;
            level.step = seriesFromRows_ ? grid.columnStep : grid.rowStep;
        }
        // Even when it read nothing. An entry that is there and empty is what
        // stops this line from being asked for over and over; lineOf() falls
        // back to a coarser run, or to the whole-line summary, which is what a
        // line that will not read closely should draw.
        auto held = level.lines.find(series[i]);
        if (held != level.lines.end()) {
            retire(held->second);
        }
        level.lines[series[i]] = std::move(grid.values);
    }

    trimLevels();
    emit changed();
    // And on to the next octave, if there is one worth reading. The chain is
    // what keeps the prefetch idle work: each run waits out its own settle, so
    // a reader who starts moving again cancels the rest of it.
    refreshDetail();
}

void DatasetPlot::clearDetail()
{
    settle_.stop();
    // A ticket reset means the reply in flight will never call its
    // continuation, so the flag it would have cleared has to be cleared here.
    // Otherwise nothing is ever read again: askForDetail() would go on
    // believing a read it will never hear about is still out.
    requests_.reset();
    inFlight_ = false;
    asked_.reset();
    wanted_.reset();
    for (Detail& level : levels_) {
        retire(level.lines);
    }
    levels_.clear();
}

void DatasetPlot::dropDetail()
{
    const bool anything = !levels_.empty();
    clearDetail();
    if (anything) {
        emit changed();
    }
}

DatasetTableModel::SampleRequest DatasetPlot::detailRequestFor(int series,
                                                               const PlotWindow& detail) const
{
    // One line, narrowed to the run. The axis the
    // line runs down is the one that carries the window, which is why the two
    // branches differ by more than their order.
    const auto first = static_cast<int>(detail.first);
    const auto span = static_cast<int>(detail.span);
    return seriesFromRows_ ? DatasetTableModel::SampleRequest{series,         1,  1,   first, span,
                                                              detail.columns, {}, true}
                           : DatasetTableModel::SampleRequest{
                                 first, span, detail.columns, series, 1, 1, {}, true};
}

void DatasetPlot::fill(PlotItem* target)
{
    if (target == nullptr) {
        return;
    }
    ensure();
    std::vector<PlotLine> lines;
    lines.reserve(drawn_.size());
    for (const int series : drawn_) {
        lines.push_back(lineOf(series));
    }
    target->setLines(std::move(lines), drawingAxis());
    drawing_ = target;
    // ...and now, and only now, is nothing reading what was retired. This is
    // the one place those vectors are freed, because it is the one place a
    // renderer that was borrowing them has just been given something else.
    retired_.clear();
}

} // namespace gui
