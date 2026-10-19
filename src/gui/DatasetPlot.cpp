// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "DatasetPlot.hpp"

#include "gui/PlotBudget.hpp"
#include "gui/PlotCore.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace gui {
namespace {

/// The positions `held`, or nothing when they do not describe the `count`
/// values being drawn -- a line drawn sample for sample has none, and a stale
/// set must never be read against a run of another length.
const double* positionsOf(const std::vector<double>& held, qsizetype count)
{
    return held.empty() || static_cast<qsizetype>(held.size()) != count ? nullptr : held.data();
}

/// The same, for line `series` of a cache keyed by line.
const double* positionsOf(const std::map<int, std::vector<double>>& held, int series,
                          qsizetype count)
{
    const auto found = held.find(series);
    return found == held.end() ? nullptr : positionsOf(found->second, count);
}

} // namespace

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

    // What the ladder gives up goes where the renderer's borrowing is kept.
    runs_.setRetire([this](std::vector<double>& values) { retire(values); });

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

    // The item this last filled is borrowing values this object owns, and
    // nothing orders the two deaths: a plot is deleted later than the row that
    // held it, and the item is QML's to destroy whenever the delegate goes.
    // Emptied here, so an item that outlives this -- by one frame, or for good
    // in a window nobody closed -- has nothing left to read.
    releaseDrawing();
}

void DatasetPlot::applyBudget()
{
    // The pyramids are where the memory is, so this has to touch them. It used
    // to trim the run ladder and nothing else, under a comment saying nothing
    // was re-read -- which was true when the only held thing was a handful of
    // runs and became misleading the moment a whole line was held beside them.
    // A reader who noticed this program holding three gigabytes and turned the
    // budget down saw no change at all until they selected another dataset.
    //
    // Coarsening is exact and free, so a budget turned down is honoured in the
    // call that turns it down. Refining is not arithmetic at all -- a finer
    // base is elements this no longer has -- so a budget turned *up* drops what
    // it could improve on and reads it again. That read is the one
    // readMissing() already makes and it blocks this thread, which is
    // unpleasant on a very large line; it is also exactly what the reader asked
    // for, through a menu, and is what selecting the dataset would have cost
    // them anyway. Drawing a pyramid as it fills is what would make it free,
    // and it is the largest piece of work left on this path.
    const long long budget = pyramidBudget();
    bool refine = false;
    for (auto it = pyramids_.begin(); it != pyramids_.end();) {
        if (!fitToBudget(it->second, budget)) {
            it = pyramids_.erase(it);
            refine = true;
            continue;
        }
        ++it;
    }
    if (refine) {
        buildPyramids();
    }
    trimLevels();
    refreshDetail();
}

void DatasetPlot::releaseDrawing() const
{
    lent_.release();
}

void DatasetPlot::retire(std::map<int, std::vector<double>>& cache) const
{
    // The values out of the map rather than the map itself, and that is the
    // whole of what a retired line is: a buffer nothing must free yet. Keeping
    // the maps meant growing a std::vector of std::map, which takes the copy on
    // a library whose map move is not noexcept and frees what this exists to
    // protect. See RunLadder::Run, and BorrowedLines for the rest.
    for (auto& held : cache) {
        lent_.retire(held.second);
    }
    cache.clear();
}

void DatasetPlot::retire(std::vector<double>& values) const
{
    lent_.retire(values);
}

void DatasetPlot::invalidate()
{
    // The one place that empties the renderer rather than retiring what it is
    // reading. A new dataset is not a coarser reading of the old one: drawing
    // the old line for another frame would be drawing the wrong file.
    releaseDrawing();
    lines_.clear();
    positions_.clear();
    dropFold();
    // And the lines they were folded out of. A pyramid is a reading of one
    // dataset's elements; carrying it into another would be drawing the wrong
    // file, which is the same thing releaseDrawing() above is here to prevent.
    pyramids_.clear();
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

QVariantMap DatasetPlot::seriesMove(int series) const
{
    const auto found = moves_.find(series);
    if (found == moves_.end()) {
        return {{QStringLiteral("scale"), 1.0}, {QStringLiteral("shift"), 0.0}};
    }
    return {{QStringLiteral("scale"), found->second.first},
            {QStringLiteral("shift"), found->second.second}};
}

void DatasetPlot::setSeriesMove(int series, double scale, double shift)
{
    if (series < 0 || !std::isfinite(scale) || !(scale > 0.0) || !std::isfinite(shift)) {
        return;
    }
    const auto found = moves_.find(series);
    if (scale == 1.0 && shift == 0.0) {
        if (found == moves_.end()) {
            return;
        }
        moves_.erase(found);
    }
    else if (found != moves_.end() && found->second == std::pair{scale, shift}) {
        return;
    }
    else {
        moves_[series] = {scale, shift};
    }
    // Nothing is re-read and no extent changes: the line is drawn under
    // another window of its axis. `changed` is what the figure refills on.
    emit changed();
}

void DatasetPlot::clearSeriesMoves()
{
    if (moves_.empty()) {
        return;
    }
    moves_.clear();
    emit changed();
}

QVariantMap DatasetPlot::lineMoves() const
{
    QVariantMap out;
    for (const auto& [series, move] : moves_) {
        out.insert(QString::number(series), QVariantList{move.first, move.second});
    }
    return out;
}

void DatasetPlot::setLineMoves(const QVariantMap& moves)
{
    // Read the way every remembered setting is: what does not parse is passed
    // over rather than refused, so a stored value from some other version
    // costs the reader that line's move and nothing else.
    std::map<int, std::pair<double, double>> read;
    for (auto it = moves.cbegin(); it != moves.cend(); ++it) {
        bool isSeries = false;
        const int series = it.key().toInt(&isSeries);
        const QVariantList pair = it.value().toList();
        if (!isSeries || series < 0 || pair.size() != 2) {
            continue;
        }
        const double scale = pair[0].toDouble();
        const double shift = pair[1].toDouble();
        if (std::isfinite(scale) && scale > 0.0 && std::isfinite(shift) &&
            (scale != 1.0 || shift != 0.0)) {
            read[series] = {scale, shift};
        }
    }
    if (read == moves_) {
        return;
    }
    moves_ = std::move(read);
    emit changed();
}

void DatasetPlot::setSeriesFromRows(bool fromRows)
{
    if (seriesFromRows_ == fromRows) {
        return;
    }
    seriesFromRows_ = fromRows;
    // A move names a line by its place, and the places have just become the
    // other axis's. Not in reseed(), which a new dataset also runs: that runs
    // after DatasetMemory has put the new dataset's own moves back, and would
    // throw them away again.
    moves_.clear();
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
    const int pane = 2 * pane_.applied;
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
    retire(positions_);
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
    if (!pane_.apply()) {
        return;
    }
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
    // Rounded down to the quantum, taken at once the first time and at the end
    // of a drag after that. See PaneColumns.
    switch (pane_.request(columns)) {
    case PaneColumns::Step::Nothing:
        break;
    case PaneColumns::Step::Cancel:
        resize_.stop();
        break;
    case PaneColumns::Step::Apply:
        resize_.stop();
        applyColumns();
        break;
    case PaneColumns::Step::Wait:
        resize_.start();
        break;
    }
}

void DatasetPlot::selectFirst(int count)
{
    const int total = std::min(std::max(count, 0), sourceSeriesCount());
    drawn_.clear();
    drawn_.reserve(static_cast<std::size_t>(total));
    for (int series = 0; series < total; ++series) {
        drawn_.push_back(series);
    }
    runs_.setLines(drawn_);

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
    runs_.setLines(drawn_);
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
    // What a line unticked held at each run goes now, retired; a line ticked
    // makes every run in hand one that cannot draw it until it is read there.
    runs_.setLines(drawn_);
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
    // Twice, because what is held has to leave room for the runs in `runs_`
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
    // coarsening an envelope is exact -- which is the property plotcore/Levels.hpp
    // states and plotcore/Pyramid.hpp spends.
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
        std::vector<double> positions;
        long long stride = 1;
        double step = 1.0;
        // Half as many buckets as points, because an envelope answers with two
        // values for each of them -- so a line still arrives as at most
        // kMaxPoints doubles and nothing about what this costs in memory
        // changes.
        if (!fillWhole(pyramid->second, cap_ / 2, summary, stride, step, &positions)) {
            continue;
        }
        // Every line covers the same extent of the other axis, so these are the
        // same for all of them and the last word is as good as the first. The
        // extent is what the x axis is drawn against, so it has to be one
        // number rather than one per line.
        points_ = static_cast<int>(summary.size());
        step_ = step;
        // A stride of one is the elements themselves; anything wider is a pair
        // of extremes per bucket, whatever the step works out to.
        summarised_ = stride > 1;
        lines_.emplace(series, std::move(summary));
        if (!positions.empty()) {
            positions_.insert_or_assign(series, std::move(positions));
        }
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
    positiveMinimum_ = 0.0;
    hasFinite_ = false;
    hasPositive_ = false;
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
    prune(positions_);
    prune(fold_.values);
    prune(fold_.xs);
    // The runs were pruned when the drawn set changed (RunLadder::setLines),
    // the same way. The pyramids go the blunt way, because nothing borrows
    // them: what the renderer holds a pointer into is always a vector in
    // `lines_` or in a run, both of which have been retired rather than freed.
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
            // The other end a logarithmic axis needs, taken here because this
            // pass is already touching every value there is -- when the line
            // has nothing better to answer with. A summary cannot say it: see
            // below.
            if (value > 0.0 && pyramids_.find(series) == pyramids_.end()) {
                positiveMinimum_ = hasPositive_ ? std::min(positiveMinimum_, value) : value;
                hasPositive_ = true;
            }
        }
        // ...and out of the line itself where it is held. An envelope keeps a
        // bucket's smallest, so a bucket holding a zero and a thousandth
        // answers with the zero and the thousandth is not in the summary at
        // all -- which on a logarithmic axis is the bottom of the axis gone.
        // See gui::smallestPositive.
        const auto pyramid = pyramids_.find(series);
        double smallest = 0.0;
        if (pyramid != pyramids_.end() && smallestPositive(pyramid->second, smallest)) {
            positiveMinimum_ = hasPositive_ ? std::min(positiveMinimum_, smallest) : smallest;
            hasPositive_ = true;
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
    if (!drawn_.empty()) {
        if (PlotLine folded; foldedLine(drawn_.front(), folded)) {
            return static_cast<int>(folded.count);
        }
    }
    if (const RunLadder::Run* run = runs().drawn(); run != nullptr) {
        // Every line covers the same run, so any that read is as good as
        // another; a drawn run has at least one.
        for (const auto& [series, held] : run->lines) {
            if (!held.values.empty()) {
                return static_cast<int>(held.values.size());
            }
        }
    }
    return points_;
}

int DatasetPlot::sourceSeriesCount() const
{
    return seriesFromRows_ ? table_->rowCount() : table_->columnCount();
}

bool DatasetPlot::thinned() const
{
    ensure();
    if (!drawn_.empty()) {
        if (PlotLine folded; foldedLine(drawn_.front(), folded)) {
            return folded.summarised;
        }
    }
    const RunLadder::Run* run = runs().drawn();
    return run != nullptr ? run->step > 1.0 : step_ > 1.0;
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

double DatasetPlot::positiveMinimum() const
{
    ensure();
    return hasPositive_ ? positiveMinimum_ : 0.0;
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

void DatasetPlot::setXLog(bool logarithmic)
{
    if (xLog_ == logarithmic) {
        return;
    }
    xLog_ = logarithmic;
    dropFold();
    emit xAxisChanged();
    refreshDetail();
}

std::optional<LogColumns> DatasetPlot::foldWanted() const
{
    if (!xLog_ || drawn_.empty() || static_cast<int>(drawn_.size()) > kWindowedSeries ||
        !std::isfinite(xStart_) || !std::isfinite(xStep_) || !(std::abs(xStep_) > 0.0)) {
        return {};
    }
    return logColumnsFor(viewMin_, viewMax_, paneBuckets());
}

bool DatasetPlot::foldServes() const
{
    return fold_.grid.serves(xStart_, xStep_, 0, paneBuckets(), viewMin_, viewMax_);
}

void DatasetPlot::dropFold() const
{
    retire(fold_.values);
    retire(fold_.xs);
    fold_.summarised.clear();
    fold_.edges.clear();
    fold_.grid.clear();
}

bool DatasetPlot::foldedLine(int series, PlotLine& line) const
{
    if (!foldWanted().has_value()) {
        return false;
    }
    if (!foldServes()) {
        // Another grid: a zoom that crossed an octave of density, a pan off the
        // margin, or an axis that moved. Retired rather than freed, because the
        // renderer is drawing the old one until it is handed this.
        dropFold();
        fold_.grid.remake(xStart_, xStep_, 0, paneBuckets(), viewMin_, viewMax_);
        if (!fold_.grid.columns.has_value()) {
            return false;
        }
        edgesAlong(*fold_.grid.columns, xStart_, xStep_, fold_.edges);
    }

    auto values = fold_.values.find(series);
    if (values == fold_.values.end()) {
        const auto pyramid = pyramids_.find(series);
        if (pyramid == pyramids_.end() || pyramid->second.empty()) {
            return false;
        }
        ColumnFold folded;
        foldColumns(pyramid->second, fold_.edges, folded);
        std::vector<double> xs(folded.positions.size());
        for (std::size_t i = 0; i < xs.size(); ++i) {
            xs[i] = xStart_ + folded.positions[i] * xStep_;
        }
        fold_.xs[series] = std::move(xs);
        fold_.summarised[series] = folded.summarised;
        values = fold_.values.insert_or_assign(series, std::move(folded.values)).first;
    }
    const std::vector<double>& xs = fold_.xs[series];
    line.values = values->second.data();
    line.xs = xs.data();
    line.count = static_cast<qsizetype>(values->second.size());
    line.summarised = fold_.summarised[series];
    return true;
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

    // On a logarithmic axis spanning an octave or more, the line folded onto
    // the pane's own columns -- and nothing below is asked. See LogColumns: the
    // runs and the summary are bucketed by element, which on that axis is the
    // wrong question for all but one part of the pane.
    if (foldedLine(series, line)) {
        return line;
    }

    // The closer look, while it covers what is on screen and this line has one.
    //
    // It is a second summary of the same line rather than a replacement for the
    // first, and this is where that pays: the moment the reader zooms out past
    // the run in hand, or pans off the end of it, the whole-line summary below
    // is what is drawn. That one covers everything by construction, so a zoom
    // out is a correct picture in the same frame instead of half a line until a
    // read lands.
    if (const RunLadder::Run* run = runs().drawn(); run != nullptr) {
        // A line that came back with nothing at this run is drawn from the
        // whole-line summary below, which is what a line that will not read
        // closely should draw.
        if (const RunLadder::LineRun* closer = run->of(series); closer != nullptr) {
            // Where the run starts, in the same table positions the whole-line
            // summary counts in -- which is the whole of what PlotLine needs to
            // draw a piece of a line in the right place.
            line.positionStart = static_cast<double>(run->window.first);

            line.values = closer->values.data();
            line.count = static_cast<qsizetype>(closer->values.size());
            line.positionStep = run->step;
            line.positions = positionsOf(closer->positions, line.count);
            // A run at bucket one is the elements of that run, read again and
            // drawn one for one; anything coarser is an envelope of them.
            line.summarised = run->window.bucket > 1;
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
    // Half a bucket when the line was read as an envelope, which is what turns
    // a window back into a range of drawn points. Where each extreme is *drawn*
    // is the element it is, in `positions`: the start and the middle of its
    // bucket, which is where both used to stand, put a spike on a bucket's last
    // sample half a bucket early.
    line.positionStep = step_;
    line.positions = positionsOf(positions_, series, line.count);
    line.summarised = summarised_;
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
    // A logarithmic axis wide enough to be folded per column is its own path:
    // the fold is memory, made in the frame that draws it, so the only question
    // here is whether the one in hand still serves -- and a pan inside it does,
    // which is what keeps a drag from refolding on every frame of it.
    const bool wasFolded = foldWanted().has_value();
    const double oldMin = viewMin_;
    const double oldMax = viewMax_;
    viewMin_ = xMin;
    viewMax_ = xMax;
    if (foldWanted().has_value()) {
        if (!wasFolded || !foldServes()) {
            emit changed();
        }
        return;
    }
    viewMin_ = oldMin;
    viewMax_ = oldMax;
    if (wasFolded) {
        // Zoomed in under an octave: the runs take over from here, and the
        // renderer is holding a fold that is about to stop being what lineOf()
        // answers with.
        viewMin_ = xMin;
        viewMax_ = xMax;
        refreshDetail();
        emit changed();
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
    zoom_.set(x, factor);
    // Not a read of its own: setVisibleRange arrives in the same turn of the
    // event loop with the range this zoom produced, and that is what decides
    // whether anything is worth reading. This only says which way it went.
}

void DatasetPlot::clearZoomFocus()
{
    zoom_.clear();
}

PlotFocus DatasetPlot::focusFor() const
{
    PlotFocus focus;
    if (!zoom_.active || !std::isfinite(xStart_) || !std::isfinite(xStep_) ||
        !(std::abs(xStep_) > 0.0)) {
        return focus;
    }
    // x = start + position * step, so a position is the same arithmetic run
    // backwards -- the mapping visiblePositions() uses, over one value.
    const double position = (zoom_.x - xStart_) / xStep_;
    if (!std::isfinite(position)) {
        return focus;
    }
    focus.position = position;
    focus.inward = zoom_.inward;
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

long long DatasetPlot::retiredDoubles() const
{
    return lent_.retiredDoubles();
}

long long DatasetPlot::heldDoubles() const
{
    long long held = 0;
    for (const auto& [series, pyramid] : pyramids_) {
        held += static_cast<long long>(pyramid.doubles());
    }
    return held;
}

int DatasetPlot::heldLevels() const
{
    // A run is about `cap_` doubles per line -- paneBuckets() buckets, and a
    // bucket answers with two values -- so how many of them there is room for
    // is the budget divided by what one costs. One line gets all of them; a
    // selection wide enough to be spending the budget on the summaries
    // themselves keeps the run it is on and nothing else.
    //
    // Out of what is *left* of the share once the pyramids have been paid for,
    // rather than out of the whole of it. LinePyramid::doubles() was written
    // for exactly this question and then went a release unused, which is the
    // shape of the problem it was written about: the number existed and nothing
    // consulted it, so the halving in pyramidBudget() was the only thing
    // standing between the two claims on one share.
    const long long spare = std::max<long long>(PlotBudget::instance().share() - heldDoubles(), 0);
    //
    // Twice over, because an envelope run holds a position beside every value
    // (RunLadder::LineRun::positions): a run is `2 * cap_` values and as many
    // positions.
    const int lines = std::max(static_cast<int>(drawn_.size()), 1);
    const long long affordable = spare / std::max<long long>(lines * 4LL * cap_, 1);
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

RunLadder& DatasetPlot::runs() const
{
    runs_.setView(levelView(), focusFor());
    return runs_;
}

RunLadder::PyramidOf DatasetPlot::pyramidOf() const
{
    return [this](int series) -> const LinePyramid* {
        const auto held = pyramids_.find(series);
        return held != pyramids_.end() ? &held->second : nullptr;
    };
}

std::optional<PlotWindow> DatasetPlot::drawnWindow() const
{
    const RunLadder::Run* run = runs().drawn();
    return run == nullptr ? std::optional<PlotWindow>{} : run->window;
}

void DatasetPlot::refreshDetail()
{
    if (foldWanted().has_value()) {
        // What is drawn is the fold, and nothing here would be looked at. The
        // runs already held stay held for the moment the reader zooms back
        // under an octave.
        settle_.stop();
        return;
    }
    RunLadder& ladder = runs();
    if (ladder.zoomedOut()) {
        dropDetail();
        return;
    }

    // Everything the pyramids can answer, now, in this call -- the run on
    // screen at the bucket the pane prefers, then every rung the policy wants.
    // See RunLadder::refresh, which carries the argument for both.
    ladder.setHeld(heldLevels());
    if (ladder.refresh(pyramidOf())) {
        emit changed();
    }

    // What is left is what the file has to answer: a run below the pyramids'
    // base, wanted and not held. A wanted run the pyramids can answer is never
    // read -- see RunLadder::read.
    const std::optional<RunRead> read = ladder.read(pyramidOf());
    wanted_ = read.has_value() ? std::optional<PlotWindow>{read->window} : std::nullopt;
    if (!wanted_.has_value()) {
        // Everything worth holding is held. Not a read, not a signal, not even
        // a timer -- which is what makes panning inside a run free.
        settle_.stop();
        return;
    }
    if (zoom_.active) {
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

void DatasetPlot::trimLevels() const
{
    RunLadder& ladder = runs();
    ladder.setHeld(heldLevels());
    ladder.trim();
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
    // Every drawn line that has not been read at this run: the whole drawn set
    // for a run never held, one line for a line ticked in the legend while the
    // reader was already looking closely.
    const std::optional<RunRead> read = runs().read(pyramidOf());
    if (!read.has_value()) {
        return;
    }
    const PlotWindow want = read->window;
    wanted_ = want;
    const std::vector<int>& series = read->lines;

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
    if (wanted_ != detail) {
        // The reader moved on while this was out. Dropping it is not a loss --
        // but arming the next read is not optional, because only one read is
        // out at a time now and this reply is what lets the next one go. A
        // return without it is a plot that stops reading until the reader
        // touches something.
        refreshDetail();
        return;
    }

    // A resolution this object has not held before goes in beside the rest.
    // Nothing is retired for it -- the runs it already has are the ones the
    // reader zoomed through and are exactly what makes going back free.
    const std::size_t count = std::min(series.size(), grids.size());
    for (std::size_t i = 0; i < count; ++i) {
        DatasetTableModel::NumericGrid& grid = grids[i];
        // Even when it read nothing. An entry that is there and empty is what
        // stops this line from being asked for over and over; lineOf() falls
        // back to the whole-line summary, which is what a line that will not
        // read closely should draw. Every line covers the same run, so the
        // step is the same for all of them.
        runs_.land(detail, series[i], seriesFromRows_ ? grid.columnStep : grid.rowStep,
                   std::move(grid.values), std::move(grid.positions));
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
    wanted_.reset();
    runs_.clear();
}

void DatasetPlot::dropDetail()
{
    const bool anything = !runs_.runs().empty();
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

void DatasetPlot::fill(LineTarget* target)
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
    // The one place the retired store is let go, because it is the one place a
    // renderer that was borrowing it has just been given something else.
    lent_.lend(target, std::move(lines), {drawingAxis()});
}

} // namespace gui
