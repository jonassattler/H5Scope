// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "DatasetPlot.hpp"

#include <QList>
#include <QPointF>
#include <QtGraphs/QXYSeries>

#include <algorithm>
#include <cmath>
#include <iterator>

namespace gui {

DatasetPlot::DatasetPlot(DatasetTableModel* table, QObject* parent)
    : QObject(parent), table_(table)
{
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

void DatasetPlot::invalidate()
{
    lines_.clear();
    // The geometry of the table went with them. Unlike the extent below, these
    // two are not recomputed on every ensure() -- they belong to the table
    // rather than to the drawn set, so hiding a line must not disturb them --
    // which makes this the one place they are cleared.
    points_ = 0;
    stride_ = 1;
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

void DatasetPlot::selectAll() { selectFirst(sourceSeriesCount()); }

void DatasetPlot::selectFirst(int count)
{
    const int total = std::min(std::max(count, 0), sourceSeriesCount());
    drawn_.clear();
    drawn_.reserve(static_cast<std::size_t>(total));
    for (int series = 0; series < total; ++series) {
        drawn_.push_back(series);
    }
    sampled_ = false;
    emit changed();
}

void DatasetPlot::selectNone()
{
    drawn_.clear();
    sampled_ = false;
    emit changed();
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
    } else {
        drawn_.erase(at);
    }
    sampled_ = false;
    emit changed();
}

DatasetTableModel::SampleRequest DatasetPlot::requestFor(int series) const
{
    // One line, thinned along its length. Along the rows that is one table row
    // in full; along the columns it is the transpose, read the same way round,
    // and fill() is what turns it back.
    // The trailing {} is the request's optional axes: the plot always reads the
    // table on screen, so it names none and the batch uses the model's own.
    return seriesFromRows_
               ? DatasetTableModel::SampleRequest{series, 1, 1, 0, -1, kMaxPoints, {}}
               : DatasetTableModel::SampleRequest{0, -1, kMaxPoints, series, 1, 1, {}};
}

void DatasetPlot::readMissing() const
{
    // Every line not already held, in one crossing of the thread rather than
    // one per line. Each crossing is a blocking round trip with a handshake at
    // both ends, and the handshake -- not the read -- is what made the
    // legend's `all` on a ten-thousand-row table stop the window: ten thousand
    // reads of a single row each, taken one at a time.
    std::vector<int> wanted;
    for (const int series : drawn_) {
        if (lines_.find(series) == lines_.end()) {
            wanted.push_back(series);
        }
    }
    if (wanted.empty()) {
        return;
    }

    // In batches rather than all at once. One crossing for ten thousand lines
    // would hold ten thousand answers in hand *and* the ten thousand copies of
    // them going into `lines_`, which is the same 160 MB twice; a batch at a
    // time keeps the second copy to kReadBatch lines while still turning the
    // round trips into a number a reader does not wait for.
    for (std::size_t first = 0; first < wanted.size(); first += kReadBatch) {
        const std::size_t last = std::min(first + kReadBatch, wanted.size());

        std::vector<DatasetTableModel::SampleRequest> requests;
        requests.reserve(last - first);
        for (std::size_t i = first; i < last; ++i) {
            requests.push_back(requestFor(wanted[i]));
        }

        std::vector<DatasetTableModel::NumericGrid> grids =
            table_->sampleValues(requests);
        const std::size_t count = std::min(last - first, grids.size());
        for (std::size_t i = 0; i < count; ++i) {
            DatasetTableModel::NumericGrid& grid = grids[i];
            if (!grid.error.isEmpty() && error_.isEmpty()) {
                error_ = grid.error;
            }
            // Every line covers the same extent of the other axis, so these are
            // the same for all of them and the last word is as good as the
            // first. The extent is what the x axis is drawn against, so it has
            // to be one number rather than one per line.
            points_ = seriesFromRows_ ? grid.columns : grid.rows;
            stride_ = seriesFromRows_ ? grid.columnStride : grid.rowStride;
            lines_.emplace(wanted[first + i], std::move(grid.values));
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
    hasFinite_ = false;
    error_.clear();

    // What is no longer drawn is no longer held: the cache exists to spare a
    // re-read of a line still on screen, not to accumulate every line the
    // reader has ever ticked.
    for (auto it = lines_.begin(); it != lines_.end();) {
        it = seriesVisible(it->first) ? std::next(it) : lines_.erase(it);
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
            } else {
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

int DatasetPlot::seriesCount() const { return static_cast<int>(drawn_.size()); }

int DatasetPlot::pointCount() const
{
    ensure();
    return points_;
}

int DatasetPlot::sourceSeriesCount() const
{
    return seriesFromRows_ ? table_->rowCount() : table_->columnCount();
}

bool DatasetPlot::thinned() const
{
    ensure();
    return stride_ > 1;
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
}

void DatasetPlot::setXStep(double value)
{
    if (qFuzzyCompare(xStep_, value)) {
        return;
    }
    xStep_ = value;
    emit xAxisChanged();
}

bool DatasetPlot::numeric() const { return table_->numeric(); }

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
    const QString label = seriesFromRows_ ? table_->rowLabel(series)
                                          : table_->columnLabel(series);
    // An axis carrying no dimension has no tuple to print -- a vector plotted
    // as one line is the case -- and a line still has to be called something.
    return label.isEmpty() ? QString::number(series) : label;
}

void DatasetPlot::fill(QAbstractSeries* target, int series)
{
    auto* points = qobject_cast<QXYSeries*>(target);
    if (points == nullptr) {
        return;
    }
    ensure();

    const auto held = lines_.find(series);
    if (held == lines_.end() || points_ <= 0) {
        points->clear();
        return;
    }

    const std::vector<double>& values = held->second;
    QList<QPointF> line;
    line.reserve(static_cast<qsizetype>(values.size()));
    for (std::size_t i = 0; i < values.size(); ++i) {
        // A cell that would not read is a gap in the line, not a zero: an
        // invented value at the axis is a reading of the data, and a wrong one.
        if (std::isfinite(values[i])) {
            // Where the element sits, not where the drawn point sits: a
            // thinned line skips stride_ elements between one drawn point and
            // the next, so its x has to skip the same distance. With the
            // default axis this is the element's own index, which is what the
            // grid's column headers count.
            const double position =
                static_cast<double>(i) * static_cast<double>(stride_);
            line.append(QPointF(xStart_ + position * xStep_, values[i]));
        }
    }
    // One bulk replace, not `count` appends: each append signals, and a series
    // loaded point by point from QML redraws the graph on every one of them.
    points->replace(line);
}

} // namespace gui
