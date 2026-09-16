// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "DatasetTableModel.hpp"

#include "gui/PlotLevels.hpp"

#include "h5core/Error.hpp"

#include <QStringList>
#include <QVariantList>

#include <algorithm>
#include <cmath>
#include <limits>

namespace gui {

DatasetTableModel::DatasetTableModel(QObject* parent) : QAbstractTableModel(parent) {}

void DatasetTableModel::setSource(bool present, h5core::DatasetInfo info, QString path,
                                  QString member, int originRank)
{
    beginResetModel();
    // Anything still on its way describes the last dataset. Disowning it here
    // is what stops a block of the previous selection arriving a frame later
    // and being painted into this one's grid.
    requests_.reset();
    asked_.clear();

    present_ = present;
    info_ = std::move(info);
    sourcePath_ = std::move(path);
    sourceMember_ = std::move(member);
    sourceOrigin_ = sourcePath_;
    if (!sourceMember_.isEmpty() && sourceOrigin_.endsWith(sourceMember_)) {
        sourceOrigin_.chop(sourceMember_.size());
    }
    sourceOriginRank_ = originRank;
    blocks_.clear();
    errorText_.clear();

    rebuild(present_ ? defaultLayout(info_.shape, info_.image) : TableLayout{});

    if (present_ && !info_.readable()) {
        errorText_ = tr("This dataset cannot be read: %1")
                         .arg(QString::fromStdString(info_.unreadableReason()));
    }
    endResetModel();
    emit datasetChanged();
}

void DatasetTableModel::setLayout(TableLayout layout)
{
    if (!present_ || layout.rank() != info_.rank() || layout.onX.size() != layout.indices.size()) {
        return;
    }
    beginResetModel();
    rebuild(std::move(layout));
    endResetModel();
}

void DatasetTableModel::rebuild(TableLayout layout)
{
    // A null dataspace holds no elements at all, and the empty product over the
    // axes would otherwise make a one-cell table out of nothing.
    axes_ = std::make_shared<const TableAxes>(std::move(layout), !present_ || info_.isNull());
    blocks_.clear();
    asked_.clear();
    // A different table has a different extent, and a colour ramp stretched
    // between the old one would be reading the new numbers on the old scale.
    extent_.reset();
}

int DatasetTableModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid() || !present_ || !errorText_.isEmpty()) {
        return 0;
    }
    return static_cast<int>(axes_->rows());
}

int DatasetTableModel::columnCount(const QModelIndex& parent) const
{
    if (parent.isValid() || !present_ || !errorText_.isEmpty()) {
        return 0;
    }
    return static_cast<int>(axes_->columns());
}

DatasetTableModel::Block DatasetTableModel::readBlock(const h5core::DataSource& source,
                                                      const TableAxes& axes, Block block,
                                                      QString& error)
{
    // On the HDF5 thread. One job per block rather than one per read: a block
    // is a hundred rows of runs, and paying a round trip for each of them would
    // be slower than the synchronous version it replaced.
    block.cells.assign(static_cast<std::size_t>(block.rows) * block.columns, QString{});

    const bool hasX = !axes.xDims().empty();
    const std::size_t lastX = hasX ? axes.xDims().back() : 0;

    try {
        for (int r = 0; r < block.rows; ++r) {
            int c = 0;
            while (c < block.columns) {
                const int column0 = block.columnOrigin + c;
                const int run = axes.runLength(column0, block.columns - c);

                std::vector<hsize_t> offset = axes.coordinates(block.rowOrigin + r, column0);
                std::vector<hsize_t> count(axes.rank(), 1);
                if (hasX) {
                    count[lastX] = static_cast<hsize_t>(run);
                }

                const h5core::DataWindow window = source.readWindow(offset, count);
                for (int i = 0; i < run && i < static_cast<int>(window.cells.size()); ++i) {
                    block.cells[static_cast<std::size_t>(r) * block.columns + c + i] =
                        QString::fromStdString(window.cells[static_cast<std::size_t>(i)]);
                }
                c += run;
            }
        }
    }
    catch (const h5core::H5Error& failure) {
        error = QString::fromStdString(failure.summary());
        return {};
    }

    block.valid = true;
    return block;
}

void DatasetTableModel::setReadError(QString text) const
{
    if (errorText_ == text) {
        return;
    }

    // rowCount() and columnCount() are both zero while a message stands, so a
    // read that fails -- or one that succeeds after another had failed -- does
    // not change what the cells hold, it changes how many of them there are.
    // Qt has one way to say that, and dataChanged is not it: a view told only
    // that the contents moved goes on addressing rows the model has just
    // stopped having, which is where a scroll into a dataset whose external
    // file had gone missing ended up indexing past the end of the table.
    //
    // A message replacing another message resizes nothing, and resetting for
    // one would throw away the reader's scroll position for a change of
    // wording.
    if (errorText_.isEmpty() == text.isEmpty()) {
        errorText_ = std::move(text);
        return;
    }

    auto* self = const_cast<DatasetTableModel*>(this);
    self->beginResetModel();
    errorText_ = std::move(text);
    blocks_.clear();
    asked_.clear();
    extent_.reset();
    self->endResetModel();
}

const DatasetTableModel::Block* DatasetTableModel::blockAt(int row, int column) const
{
    for (const Block& block : blocks_) {
        if (block.valid && row >= block.rowOrigin && row < block.rowOrigin + block.rows &&
            column >= block.columnOrigin && column < block.columnOrigin + block.columns) {
            return &block;
        }
    }
    return nullptr;
}

void DatasetTableModel::ensureBlock(int row, int column) const
{
    if (blockAt(row, column) != nullptr) {
        return;
    }

    Block block;
    block.rowOrigin = (row / kBlockRows) * kBlockRows;
    block.columnOrigin = (column / kBlockColumns) * kBlockColumns;
    block.rows = static_cast<int>(std::min<qint64>(kBlockRows, axes_->rows() - block.rowOrigin));
    block.columns =
        static_cast<int>(std::min<qint64>(kBlockColumns, axes_->columns() - block.columnOrigin));
    if (block.rows <= 0 || block.columns <= 0) {
        return;
    }

    // One request per block, however many cells of it the view asks about
    // before the answer lands. Without this the first paint of a screenful
    // would queue a hundred identical reads.
    const Origin origin{block.rowOrigin, block.columnOrigin};
    if (std::find(asked_.begin(), asked_.end(), origin) != asked_.end()) {
        return;
    }
    asked_.push_back(origin);

    auto* self = const_cast<DatasetTableModel*>(this);
    struct Read
    {
        Block block;
        QString error;
    };

    H5Thread::instance().submit(
        requests_,
        [axes = axes_, block](H5Session& session) {
            Read read;
            const h5core::DataSource* source = session.source();
            if (source == nullptr) {
                return read;
            }
            read.block = readBlock(*source, *axes, block, read.error);
            return read;
        },
        [self, origin](Read read) {
            std::erase(self->asked_, origin);
            if (!read.error.isEmpty()) {
                self->setReadError(read.error);
                return;
            }
            if (!read.block.valid) {
                return;
            }
            // Before the block is installed: clearing the message is what puts
            // the rows back, and setReadError() empties the cache when it
            // announces that.
            self->setReadError(QString{});

            const int rowOrigin = read.block.rowOrigin;
            const int columnOrigin = read.block.columnOrigin;
            const int rows = read.block.rows;
            const int columns = read.block.columns;

            // Newest first, and the oldest goes when there are too many. A
            // viewport that straddles a boundary wants two at once and gets
            // them; a reader scrolling through a dataset larger than RAM still
            // holds a bounded number of cells.
            std::erase_if(self->blocks_, [&](const Block& held) {
                return held.rowOrigin == rowOrigin && held.columnOrigin == columnOrigin;
            });
            self->blocks_.insert(self->blocks_.begin(), std::move(read.block));
            if (self->blocks_.size() > kCachedBlocks) {
                self->blocks_.resize(kCachedBlocks);
            }

            // Only the block that arrived. The rest of the table is either
            // already painted or is waiting on a request of its own.
            const QModelIndex topLeft = self->index(rowOrigin, columnOrigin);
            const QModelIndex bottomRight =
                self->index(rowOrigin + rows - 1, columnOrigin + columns - 1);
            if (topLeft.isValid() && bottomRight.isValid()) {
                emit self->dataChanged(topLeft, bottomRight);
            }
        });
}

double DatasetTableModel::NumericGrid::at(int row, int column) const
{
    if (row < 0 || row >= rows || column < 0 || column >= columns) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return values[static_cast<std::size_t>(row) * columns + column];
}

bool DatasetTableModel::numeric() const
{
    return present_ && info_.isNumeric() && info_.readable();
}

DatasetTableModel::NumericGrid DatasetTableModel::sampleValues(int firstRow, int rowSpan,
                                                               int maxRows, int firstColumn,
                                                               int columnSpan, int maxColumns) const
{
    return sampleValues(*axes_, firstRow, rowSpan, maxRows, firstColumn, columnSpan, maxColumns);
}

DatasetTableModel::NumericGrid DatasetTableModel::sampleFrom(const h5core::DataSource& source,
                                                             const TableAxes& axes, int firstRow,
                                                             int rowSpan, int maxRows,
                                                             int firstColumn, int columnSpan,
                                                             int maxColumns, bool envelope)
{
    // On the HDF5 thread. Everything above this -- whether there is a source at
    // all, whether its type has numbers in it -- was settled on the other side
    // out of the description, which is why none of it appears here.
    NumericGrid grid;

    // A span below zero means "to the end", so a caller that only wants a cap
    // does not have to know how big the table is first.
    const auto extent = [](qint64 first, int span, qint64 total) {
        if (first < 0 || first >= total) {
            return qint64{0};
        }
        const qint64 available = total - first;
        return span < 0 ? available : std::min<qint64>(span, available);
    };
    const qint64 rowExtent = extent(firstRow, rowSpan, axes.rows());
    const qint64 columnExtent = extent(firstColumn, columnSpan, axes.columns());
    if (rowExtent <= 0 || columnExtent <= 0 || maxRows <= 0 || maxColumns <= 0) {
        return grid;
    }

    // Ceiling division, so the stride is always enough: 100 rows into 30 is
    // every 4th, giving 25 -- never 34, which would overrun the cap.
    grid.rowStride = static_cast<int>((rowExtent + maxRows - 1) / maxRows);
    grid.columnStride = static_cast<int>((columnExtent + maxColumns - 1) / maxColumns);
    grid.rows = static_cast<int>((rowExtent + grid.rowStride - 1) / grid.rowStride);
    const int buckets =
        static_cast<int>((columnExtent + grid.columnStride - 1) / grid.columnStride);

    const bool hasX = !axes.xDims().empty();
    const std::size_t lastX = hasX ? axes.xDims().back() : 0;

    const bool hasY = !axes.yDims().empty();
    const std::size_t lastY = hasY ? axes.yDims().back() : 0;

    // An envelope, when one was asked for and there is anything to summarise.
    //
    // Along the columns when the line runs that way: the last x-dimension is
    // the one that turns fastest along a row, so a bucket of columns is one
    // contiguous hyperslab and one read.
    //
    // And down the rows when it runs that way, which is not an afterthought but
    // the case that matters most. defaultOnX keeps a rank-1 dimension on the
    // row axis so a vector still reads as a column in the grid, so *every* 1-D
    // dataset -- every trace, every spectrum, every log -- is a line down the
    // rows, and it would otherwise be the one shape still thinning by stride.
    // A span of rows is one hyperslab and one read exactly as a span of columns
    // is; it is merely not a contiguous one, because a row of a 2-D dataset is
    // as long as the dataset is wide. Same number of reads, same number of
    // elements, further apart.
    const bool envelopeColumns = envelope && hasX && grid.columnStride > 1;
    const bool envelopeRows = envelope && !envelopeColumns && hasY && grid.rowStride > 1;
    const int rowBuckets = grid.rows;

    grid.columns = envelopeColumns ? 2 * buckets : buckets;
    grid.columnStep = envelopeColumns ? grid.columnStride / 2.0 : grid.columnStride;
    grid.rows = envelopeRows ? 2 * rowBuckets : rowBuckets;
    grid.rowStep = envelopeRows ? grid.rowStride / 2.0 : grid.rowStride;

    const auto nan = std::numeric_limits<double>::quiet_NaN();
    grid.values.assign(static_cast<std::size_t>(grid.rows) * grid.columns, nan);

    const auto note = [&grid](double value) {
        if (!std::isfinite(value)) {
            return;
        }
        if (!grid.hasFinite) {
            grid.minimum = value;
            grid.maximum = value;
            grid.hasFinite = true;
        }
        else {
            grid.minimum = std::min(grid.minimum, value);
            grid.maximum = std::max(grid.maximum, value);
        }
    };

    // The extremes of a bucket, and a read cut back to a whole number of them,
    // are gui::extremesOf() and gui::wholeBuckets() -- see PlotLevels.hpp. They
    // used to be a struct and two lambdas here, and CustomPlot::readLine had a
    // second copy of the same arithmetic that agreed with this one only because
    // a test compared the two element for element.

    if (envelopeColumns) {
        // Every element of the row, in reads of up to kReadRun -- and then as
        // many whole buckets as each of those covers, summarised out of the
        // buffer.
        //
        // The reads and the buckets used to be the same thing: one read per
        // bucket, which was defensible because it is what the strided path
        // below costs too -- it reads one element per drawn point. It stopped
        // being defensible the moment a bucket got small. `all` on a table of
        // ten thousand rows gives each line 128 buckets of eight elements, and
        // that arrangement asked HDF5 for eight values 1.28 million times to
        // move twenty megabytes; the reader waited two seconds for it, and
        // essentially all of that was per-call overhead rather than I/O.
        //
        // Nothing about what is drawn changes. The same elements are read and
        // the same extremes come out of them, because a bucket is never split
        // across two reads: one that stops inside a bucket leaves it for the
        // next, and the only bucket summarised from part of itself is one wider
        // than a whole read -- which is the degradation the old code took for
        // every bucket at kReadRun, kept here for the same reason.
        //
        // What it buys is that a spike one sample wide cannot be missed -- it
        // is selected for *being* extreme rather than for landing where a
        // stride happened to fall -- and the extent the y axis is drawn against
        // is the line's true extent rather than the extent of a sample of it.
        try {
            for (int r = 0; r < grid.rows; ++r) {
                const auto row = static_cast<int>(firstRow + qint64{r} * grid.rowStride);
                int b = 0;
                while (b < buckets) {
                    const qint64 wanted = qint64{b} * grid.columnStride;
                    const auto column = static_cast<int>(firstColumn + wanted);
                    // Scattered Custom indices break the run, and the buckets
                    // past the break are then left for the read after this one.
                    const auto limit = static_cast<int>(
                        wholeBuckets(std::min<qint64>(kReadRun, columnExtent - wanted),
                                     grid.columnStride));
                    const int run = axes.runLength(column, std::max(limit, 1));

                    std::vector<hsize_t> offset = axes.coordinates(row, column);
                    std::vector<hsize_t> count(axes.rank(), 1);
                    count[lastX] = static_cast<hsize_t>(std::max(run, 1));
                    const h5core::NumericWindow window = source.readNumericWindow(offset, count);
                    const auto seen = static_cast<qsizetype>(window.values.size());

                    const int started = b;
                    for (; b < buckets; ++b) {
                        const qint64 at = qint64{b} * grid.columnStride;
                        const auto from = static_cast<qsizetype>(at - wanted);
                        if (from >= seen) {
                            break;
                        }
                        const qint64 span =
                            std::min<qint64>(grid.columnStride, columnExtent - at);
                        const auto to = static_cast<qsizetype>(std::min<qint64>(from + span, seen));
                        if (to - from < span && b > started) {
                            break; // the read stopped inside it; the next one covers it whole
                        }
                        const Extremes found = extremesOf(window.values.data(), from, to);
                        if (!found.found()) {
                            continue; // the whole bucket stays NaN, which is what it is
                        }
                        note(found.lowest);
                        note(found.highest);
                        const auto out = static_cast<std::size_t>(r) * grid.columns + 2 * b;
                        grid.values[out] = found.first();
                        grid.values[out + 1] = found.second();
                    }
                    if (b == started) {
                        ++b; // a read that yielded nothing must not stall the walk
                    }
                }
            }
        }
        catch (const h5core::H5Error& error) {
            grid.error = QString::fromStdString(error.summary());
        }
        return grid;
    }

    if (envelopeRows) {
        // The mirror of the block above, down the other axis, and batched the
        // same way: a read of up to kReadRun rows, then every whole bucket of
        // rows that read covers. A span of rows is one hyperslab exactly as a
        // span of columns is; it is merely not a contiguous one, because a row
        // of a 2-D dataset is as long as the dataset is wide.
        //
        // This is the case that matters most. defaultOnX keeps a rank-1
        // dimension on the row axis so a vector still reads as a column in the
        // grid, which makes every trace, every spectrum and every log in every
        // file a line down the rows.
        try {
            for (int c = 0; c < grid.columns; ++c) {
                const auto column = static_cast<int>(firstColumn + qint64{c} * grid.columnStride);
                int b = 0;
                while (b < rowBuckets) {
                    const qint64 wanted = qint64{b} * grid.rowStride;
                    const auto row = static_cast<int>(firstRow + wanted);
                    const auto limit = static_cast<int>(wholeBuckets(
                        std::min<qint64>(kReadRun, rowExtent - wanted), grid.rowStride));
                    const int run = axes.rowRunLength(row, std::max(limit, 1));

                    std::vector<hsize_t> offset = axes.coordinates(row, column);
                    std::vector<hsize_t> count(axes.rank(), 1);
                    count[lastY] = static_cast<hsize_t>(std::max(run, 1));
                    const h5core::NumericWindow window = source.readNumericWindow(offset, count);
                    const auto seen = static_cast<qsizetype>(window.values.size());

                    const int started = b;
                    for (; b < rowBuckets; ++b) {
                        const qint64 at = qint64{b} * grid.rowStride;
                        const auto from = static_cast<qsizetype>(at - wanted);
                        if (from >= seen) {
                            break;
                        }
                        const qint64 span = std::min<qint64>(grid.rowStride, rowExtent - at);
                        const auto to = static_cast<qsizetype>(std::min<qint64>(from + span, seen));
                        if (to - from < span && b > started) {
                            break; // the read stopped inside it; the next one covers it whole
                        }
                        const Extremes found = extremesOf(window.values.data(), from, to);
                        if (!found.found()) {
                            continue; // the whole bucket stays NaN, which is what it is
                        }
                        note(found.lowest);
                        note(found.highest);
                        const auto first = static_cast<std::size_t>(2 * b) * grid.columns + c;
                        const auto second = static_cast<std::size_t>(2 * b + 1) * grid.columns + c;
                        grid.values[first] = found.first();
                        grid.values[second] = found.second();
                    }
                    if (b == started) {
                        ++b; // a read that yielded nothing must not stall the walk
                    }
                }
            }
        }
        catch (const h5core::H5Error& error) {
            grid.error = QString::fromStdString(error.summary());
        }
        return grid;
    }

    // Two ways to walk a row, and the stride decides which is cheaper. With no
    // thinning the columns wanted are consecutive in the file, so one read of a
    // long run serves thousands of them. Once they are strided apart that run
    // would move `stride` times the data it yields, so each sampled column is
    // read on its own instead. Either way a row costs at most `grid.columns`
    // reads, which is what keeps a table far larger than the screen bounded.
    const bool consecutive = grid.columnStride == 1;

    try {
        for (int r = 0; r < grid.rows; ++r) {
            const auto row = static_cast<int>(firstRow + qint64{r} * grid.rowStride);

            int taken = 0;
            while (taken < grid.columns) {
                const qint64 wanted = qint64{taken} * grid.columnStride;
                const auto column = static_cast<int>(firstColumn + wanted);
                const int limit =
                    consecutive
                        ? static_cast<int>(std::min<qint64>(kReadRun, columnExtent - wanted))
                        : 1;
                const int run = axes.runLength(column, limit);

                std::vector<hsize_t> offset = axes.coordinates(row, column);
                std::vector<hsize_t> count(axes.rank(), 1);
                if (hasX) {
                    count[lastX] = static_cast<hsize_t>(run);
                }
                const h5core::NumericWindow window = source.readNumericWindow(offset, count);

                // Every sampled column this run happens to cover. Reading
                // starts on a wanted column by construction, so a full run
                // yields at least one; `progress` covers the one case that
                // does not -- a read clamped to nothing -- because a loop that
                // consumed no column would not advance at all.
                const int progress = taken;
                for (; taken < grid.columns; ++taken) {
                    const qint64 within = qint64{taken} * grid.columnStride - wanted;
                    if (within >= run || within >= static_cast<qint64>(window.values.size())) {
                        break;
                    }
                    const double value = window.values[static_cast<std::size_t>(within)];
                    grid.values[static_cast<std::size_t>(r) * grid.columns + taken] = value;
                    note(value);
                }
                if (taken == progress) {
                    ++taken; // leaves this column at NaN, which is what it is
                }
            }
        }
    }
    catch (const h5core::H5Error& error) {
        grid.error = QString::fromStdString(error.summary());
    }

    return grid;
}

/// Whether there is anything to sample, and what to say when there is not.
/// Shared by the blocking and the asked-for forms, because the answer has
/// nothing to do with which of them is being used.
bool DatasetTableModel::sampleable(NumericGrid& grid) const
{
    if (!present_) {
        return false;
    }
    if (!numeric()) {
        grid.error = errorText_.isEmpty()
                         ? tr("%1 holds %2, which has no numeric value.")
                               .arg(sourcePath_, QString::fromStdString(info_.type.description))
                         : errorText_;
        return false;
    }
    return true;
}

DatasetTableModel::NumericGrid DatasetTableModel::sampleValues(const TableAxes& axes, int firstRow,
                                                               int rowSpan, int maxRows,
                                                               int firstColumn, int columnSpan,
                                                               int maxColumns) const
{
    NumericGrid grid;
    if (!sampleable(grid)) {
        return grid;
    }
    return H5Thread::instance().invoke([&](H5Session& session) {
        const h5core::DataSource* source = session.source();
        return (source == nullptr) ? NumericGrid{}
                                   : sampleFrom(*source, axes, firstRow, rowSpan, maxRows,
                                                firstColumn, columnSpan, maxColumns);
    });
}

std::vector<DatasetTableModel::NumericGrid>
DatasetTableModel::sampleValues(const std::vector<SampleRequest>& requests) const
{
    std::vector<NumericGrid> grids;
    if (requests.empty()) {
        return grids;
    }
    NumericGrid refusal;
    if (!sampleable(refusal)) {
        // The same answer every one of them would have got, without a crossing
        // to fetch it: whether there is anything numeric to read is a question
        // about the description this side already holds.
        grids.assign(requests.size(), refusal);
        return grids;
    }
    // By reference, not by value. invoke() blocks until the job has run, so
    // `axes_` cannot move underneath it -- and a copy of it copies the whole
    // selection, which is one index per element of every dimension. On a
    // ten-million-element vector that is eighty megabytes of memcpy per read,
    // paid on every block the plot samples.
    return H5Thread::instance().invoke([&](H5Session& session) {
        const h5core::DataSource* source = session.source();
        return source == nullptr ? std::vector<NumericGrid>(requests.size())
                                 : readSamples(*source, *axes_, requests);
    });
}

std::vector<LinePyramid>
DatasetTableModel::samplePyramids(const std::vector<PyramidRequest>& requests,
                                  QString& refused) const
{
    std::vector<LinePyramid> built;
    if (requests.empty()) {
        return built;
    }
    NumericGrid refusal;
    if (!sampleable(refusal)) {
        // Why, and not merely that it will not read. A dataset of text has
        // nothing to plot, and the plot says so in the pane rather than drawing
        // an empty frame -- which it can only do if the reason crosses back.
        refused = refusal.error;
        built.assign(requests.size(), LinePyramid{});
        return built;
    }
    // By reference for the reason sampleValues gives: a copy of the axes is one
    // index per element of every dimension, which on a ten-million-element
    // vector is eighty megabytes of memcpy before a byte has been read.
    return H5Thread::instance().invoke([&](H5Session& session) {
        const h5core::DataSource* source = session.source();
        return source == nullptr ? std::vector<LinePyramid>(requests.size())
                                 : readPyramids(*source, *axes_, requests);
    });
}

std::vector<LinePyramid>
DatasetTableModel::readPyramids(const h5core::DataSource& source, const TableAxes& axes,
                                const std::vector<PyramidRequest>& requests)
{
    std::vector<LinePyramid> built;
    built.reserve(requests.size());
    for (const PyramidRequest& request : requests) {
        built.push_back(pyramidFrom(source, axes, request));
    }
    return built;
}

LinePyramid DatasetTableModel::pyramidFrom(const h5core::DataSource& source, const TableAxes& axes,
                                           const PyramidRequest& request)
{
    // On the HDF5 thread. One line, read from end to end exactly once, and kept
    // rather than thinned away.
    //
    // This is the same walk sampleFrom's envelope takes -- hyperslabs of up to
    // kReadRun elements, so the round trips follow the *length* of the line and
    // not the resolution asked of it -- and it differs in one thing: what comes
    // out is the elements at the finest bucket the budget affords instead of
    // two thousand points. Everything the reader zooms to afterwards is folded
    // out of that, in memory, which is the whole of why a zoom stopped costing
    // a read.
    LinePyramid pyramid;
    const long long length = request.alongRow ? axes.columns() : axes.rows();
    if (length <= 0) {
        return pyramid;
    }
    // The dimension that turns fastest along the line, when there is one.
    //
    // There need not be. defaultOnX keeps a rank-1 dimension on the row axis,
    // so a vector read the other way round is a thousand lines of one element
    // and the axis those lines run along names no dimension at all. Each of
    // them is then a single cell, read as one -- which is what the consecutive
    // path of sampleFrom does with the same table.
    const std::vector<std::size_t>& along = request.alongRow ? axes.xDims() : axes.yDims();
    const bool spans = !along.empty();
    const std::size_t fastest = spans ? along.back() : 0;

    const long long base = baseBucketFor(length, request.budget);
    PyramidBuilder builder(length, base);

    try {
        long long done = 0;
        while (done < length) {
            const long long remaining = length - done;
            const auto limit = static_cast<int>(std::min<long long>(kReadRun, remaining));
            const int run = !spans             ? 1
                            : request.alongRow ? axes.runLength(done, std::max(limit, 1))
                                               : axes.rowRunLength(done, std::max(limit, 1));
            const int take = std::max(run, 1);

            std::vector<hsize_t> offset = request.alongRow ? axes.coordinates(request.series, done)
                                                           : axes.coordinates(done, request.series);
            std::vector<hsize_t> count(axes.rank(), 1);
            if (spans) {
                count[fastest] = static_cast<hsize_t>(take);
            }

            const h5core::NumericWindow window = source.readNumericWindow(offset, count);
            const auto seen = static_cast<long long>(window.values.size());
            if (seen <= 0) {
                // A read that yielded nothing must not stall the walk. The
                // elements it would have covered are a gap, which is what they
                // are -- and a gap is a pair of NaN rather than a bucket that
                // is not there, because dropping it would slide every later one
                // left and draw the line across the hole.
                const std::vector<double> nothing(static_cast<std::size_t>(take),
                                                  std::numeric_limits<double>::quiet_NaN());
                builder.add(nothing.data(), take);
                done += take;
                continue;
            }
            builder.add(window.values.data(), seen);
            done += seen;
        }
    }
    catch (const h5core::H5Error&) {
        // Whatever was read stands and the line stops there, which draws a
        // partial line rather than none at all. The reason is already on its way
        // to the reader through the paths that report it.
        pyramid = builder.finish();
        pyramid.length = builder.taken();
        return pyramid;
    }

    pyramid = builder.finish();
    return pyramid;
}

std::vector<DatasetTableModel::NumericGrid>
DatasetTableModel::readSamples(const h5core::DataSource& source, const TableAxes& axes,
                               const std::vector<SampleRequest>& requests)
{
    // On the HDF5 thread, and everything it needs is an argument -- which is
    // the whole reason it is static. The blocking form above waits for it, and
    // DatasetPlot runs the same batch inside a submit() so that a closer look
    // at a line never stops the window; neither of them could share the body if
    // it reached for a member.
    std::vector<NumericGrid> read;
    read.reserve(requests.size());
    for (const SampleRequest& request : requests) {
        read.push_back(sampleFrom(source, request.axes.has_value() ? *request.axes : axes,
                                  request.firstRow, request.rowSpan, request.maxRows,
                                  request.firstColumn, request.columnSpan, request.maxColumns,
                                  request.envelope));
    }
    return read;
}

QVariant DatasetTableModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || !present_) {
        return {};
    }
    if (role != Qt::DisplayRole && role != Qt::ToolTipRole && role != Number) {
        return {};
    }
    const int row = index.row();
    const int column = index.column();
    if (row < 0 || column < 0 || row >= axes_->rows() || column >= axes_->columns()) {
        return {};
    }
    // A number the delegate can always rely on. Every other exit below is a
    // cell that is not there or would not read, and both of those are the same
    // "no value here" a struct or a string is.
    const auto missing = [role] {
        return role == Number ? QVariant(std::numeric_limits<double>::quiet_NaN()) : QVariant();
    };

    ensureBlock(row, column);
    const Block* block = blockAt(row, column);
    if (block == nullptr) {
        return missing();
    }
    const auto flat = static_cast<std::size_t>(row - block->rowOrigin) * block->columns +
                      static_cast<std::size_t>(column - block->columnOrigin);
    if (flat >= block->cells.size()) {
        return missing();
    }
    const QString& cell = block->cells[flat];
    if (role == Number) {
        // Off the cell the file gave, not off the display string: that one has
        // been rounded to the reader's notation, and a fill computed from it
        // would band a column the file says is smooth. Only for a dataset that
        // holds numbers -- the text of a struct parses to nothing anyway, and
        // saying so here is cheaper than finding out per cell.
        if (!numeric()) {
            return missing();
        }
        bool ok = false;
        const double value = cell.toDouble(&ok);
        return ok ? QVariant(value) : missing();
    }
    // The two text roles are the same value written two ways. What the grid
    // draws may be rounded to line the column up and is elided to fit the
    // width; the tooltip is the value the file holds, in full, because a
    // presentation must never be the only copy of a datum a reader can reach.
    return role == Qt::ToolTipRole ? cell : formatted(cell);
}

QVariantMap DatasetTableModel::valueExtent() const
{
    if (!extent_) {
        const NumericGrid grid = sampleValues(0, -1, kExtentSamples, 0, -1, kExtentSamples);
        extent_ = Extent{grid.minimum, grid.maximum, grid.hasFinite};
    }
    return {{QStringLiteral("minimum"), extent_->minimum},
            {QStringLiteral("maximum"), extent_->maximum},
            {QStringLiteral("valid"), extent_->valid}};
}

bool DatasetTableModel::floats() const
{
    return present_ && info_.type.cls == h5core::TypeClass::Float && info_.readable();
}

void DatasetTableModel::setFloatFormat(FloatFormat format)
{
    if (floatFormat_ == format) {
        return;
    }
    floatFormat_ = format;
    emit floatFormatChanged();
    // Nothing about *which* cells there are has changed, so this is a repaint
    // and not a reset: a reset would drop the reader's scroll position for a
    // change of notation.
    if (rowCount() > 0 && columnCount() > 0) {
        emit dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1), {Qt::DisplayRole});
    }
}

void DatasetTableModel::setFloatDecimals(int decimals)
{
    const int clamped = std::clamp(decimals, 0, kMaxFloatDecimals);
    if (floatDecimals_ == clamped) {
        return;
    }
    floatDecimals_ = clamped;
    emit floatFormatChanged();
    if (rowCount() > 0 && columnCount() > 0) {
        emit dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1), {Qt::DisplayRole});
    }
}

QString DatasetTableModel::formatted(const QString& text) const
{
    if (floatFormat_ == Shortest || !floats()) {
        return text;
    }
    // Round-tripping through a double is lossless here and nowhere else in the
    // codebase: h5core writes a float with std::format("{}", …), which is the
    // shortest text that reads back as the same double. So the cell string is
    // the value, and re-reading it loses nothing.
    bool ok = false;
    const double value = text.toDouble(&ok);
    if (!ok) {
        // A NaN, an infinity, or something that is not a number at all. Each of
        // those is already as short as it goes and none of them has decimals.
        return text;
    }
    return QString::number(value, floatFormat_ == Fixed ? 'f' : 'e', floatDecimals_);
}

int DatasetTableModel::widestCell(int firstRow, int rows, int firstColumn, int columns) const
{
    if (!present_ || rows <= 0 || columns <= 0) {
        return 0;
    }
    // The blocks already held, and no others: the rectangle asked for is
    // clamped into each of them rather than sliding the cache -- a column
    // width must not be the thing that decides which part of a dataset gets
    // read. Every block is walked because a viewport that straddles a boundary
    // is exactly the case where the widest cell is in the other one.
    ensureBlock(std::max(firstRow, 0), std::max(firstColumn, 0));

    int widest = 0;
    for (const Block& block : blocks_) {
        if (!block.valid) {
            continue;
        }
        const int firstR = std::max(firstRow, block.rowOrigin);
        const int lastR = std::min(firstRow + rows, block.rowOrigin + block.rows);
        const int firstC = std::max(firstColumn, block.columnOrigin);
        const int lastC = std::min(firstColumn + columns, block.columnOrigin + block.columns);

        for (int r = firstR; r < lastR; ++r) {
            for (int c = firstC; c < lastC; ++c) {
                const auto flat = static_cast<std::size_t>(r - block.rowOrigin) * block.columns +
                                  static_cast<std::size_t>(c - block.columnOrigin);
                if (flat < block.cells.size()) {
                    widest = std::max<int>(widest,
                                           static_cast<int>(formatted(block.cells[flat]).size()));
                }
            }
        }
    }
    return widest;
}

QVariantMap DatasetTableModel::elementAt(int row, int column) const
{
    if (!present_ || row < 0 || column < 0 || row >= axes_->rows() || column >= axes_->columns()) {
        return {};
    }

    // The one read in this class that is still waited for. It is a single
    // element, asked for by a click on the cell already on screen, and QML
    // calls it as a function with a return value -- so making it asked-for
    // would mean a property, a signal and a pane that is briefly blank, to
    // save a wait of one object read. See DEVLOG: this is the remaining
    // synchronous point and it is deliberate.
    struct Read
    {
        h5core::ElementValue value;
        QString error;
    };
    const Read read = H5Thread::instance().invoke([&](H5Session& session) {
        Read result;
        const h5core::DataSource* source = session.source();
        if (source == nullptr) {
            return result;
        }
        try {
            result.value = source->readElement(axes_->coordinates(row, column));
        }
        catch (const h5core::H5Error& error) {
            result.error = QString::fromStdString(error.summary());
        }
        return result;
    });
    if (!read.error.isEmpty()) {
        return {{QStringLiteral("error"), read.error}};
    }
    const h5core::ElementValue& element = read.value;

    QVariantList fields;
    fields.reserve(static_cast<qsizetype>(element.fields.size()));
    for (const h5core::FieldValue& field : element.fields) {
        fields.append(QVariantMap{
            {QStringLiteral("name"), QString::fromStdString(field.name)},
            {QStringLiteral("type"), QString::fromStdString(field.type)},
            {QStringLiteral("value"), QString::fromStdString(field.value)},
        });
    }

    return {
        {QStringLiteral("label"), cellLabel(row, column)},
        {QStringLiteral("text"), QString::fromStdString(element.text)},
        {QStringLiteral("json"), QString::fromStdString(element.json)},
        {QStringLiteral("fields"), fields},
    };
}

QString DatasetTableModel::labelFor(int row, int column, bool showX, bool showY) const
{
    const std::size_t rank = axes_->rank();
    if (rank == 0 || !present_) {
        return {};
    }

    const std::vector<hsize_t> coords = axes_->coordinates(row, column);

    // Rank 1 has nothing to disambiguate, so it reads as a plain index -- and
    // the axis that carries no dimension has no index to print at all.
    if (rank == 1) {
        const bool shown = axes_->layout().onX[0] ? showX : showY;
        return shown ? QString::number(coords[0]) : QString{};
    }

    QStringList parts;
    parts.reserve(static_cast<qsizetype>(rank));
    for (std::size_t d = 0; d < rank; ++d) {
        const bool shown = axes_->layout().onX[d] ? showX : showY;
        parts << (shown ? QString::number(coords[d]) : QStringLiteral("_"));
    }
    // Brackets, not parentheses: the slice line above the grid already writes
    // `/cube[:, 2, 0:4]`, and a cell's index tuple is the same statement about
    // the same dataset written for one element.
    return QStringLiteral("[%1]").arg(parts.join(QLatin1Char(',')));
}

QString DatasetTableModel::rowLabel(int row) const
{
    if (row < 0 || row >= axes_->rows()) {
        return {};
    }
    return labelFor(row, 0, false, true);
}

QString DatasetTableModel::columnLabel(int column) const
{
    if (column < 0 || column >= axes_->columns()) {
        return {};
    }
    return labelFor(0, column, true, false);
}

QString DatasetTableModel::cellLabel(int row, int column) const
{
    if (row < 0 || row >= axes_->rows() || column < 0 || column >= axes_->columns()) {
        return {};
    }
    return labelFor(row, column, true, true);
}

namespace {

/// One dimension's selection, written as a subscript.
///
/// The whole of it in order is ":", a contiguous ascending run is "a:b" with
/// the exclusive upper bound this application writes everywhere, and anything
/// else is the indices themselves, bracketed as numpy brackets its fancy
/// indexing. Never approximated: what comes out selects exactly what went in,
/// because the reader is going to paste it into a box that reads it back.
[[nodiscard]] QString writeSelection(const std::vector<hsize_t>& indices, hsize_t extent)
{
    if (indices.empty()) {
        return QStringLiteral(":");
    }
    bool consecutive = true;
    for (std::size_t i = 1; i < indices.size(); ++i) {
        if (indices[i] != indices[i - 1] + 1) {
            consecutive = false;
            break;
        }
    }
    if (consecutive) {
        if (indices.front() == 0 && indices.size() == extent) {
            return QStringLiteral(":");
        }
        return QStringLiteral("%1:%2").arg(indices.front()).arg(indices.back() + 1);
    }
    QStringList written;
    written.reserve(static_cast<qsizetype>(indices.size()));
    for (const hsize_t index : indices) {
        written << QString::number(index);
    }
    return QStringLiteral("[%1]").arg(written.join(QStringLiteral(",")));
}

} // namespace

QString DatasetTableModel::lineExpression(int line, bool fromRows) const
{
    const std::size_t rank = axes_->rank();
    if (!present_ || rank == 0 || sourcePath_.isEmpty()) {
        return {};
    }

    const std::vector<std::size_t>& along = fromRows ? axes_->xDims() : axes_->yDims();

    // More than one dimension with something to run along, and the line is the
    // product of them rather than a slice of any one. See the header.
    int running = 0;
    for (const std::size_t d : along) {
        if (axes_->layout().indices[d].size() > 1) {
            ++running;
        }
    }
    if (running > 1) {
        return {};
    }

    const std::vector<hsize_t> coords =
        fromRows ? axes_->coordinates(line, 0) : axes_->coordinates(0, line);
    if (coords.size() != rank) {
        return {};
    }

    const std::vector<hsize_t>& shape = info_.shape;
    QStringList parts;
    parts.reserve(static_cast<qsizetype>(rank));
    for (std::size_t d = 0; d < rank; ++d) {
        const bool runs = std::find(along.begin(), along.end(), d) != along.end();
        if (runs) {
            parts << writeSelection(axes_->layout().indices[d], d < shape.size() ? shape[d] : 0);
        }
        else {
            parts << QString::number(coords[d]);
        }
    }
    if (sourceMember_.isEmpty() || sourceOriginRank_ < 0) {
        return sourcePath_ + QStringLiteral("[") + parts.join(QStringLiteral(", "))
               + QStringLiteral("]");
    }

    // With a chain, the subscript has two halves and the chain goes between
    // them: the dataset's own axes are addressed before the member is named,
    // and the axes the member appends after it. That is the notation a reader
    // writes -- `/events[3, :].samples[2]` -- and it is the one that pastes
    // back into a custom tab, which is the whole reason this line exists.
    const auto split = std::min<qsizetype>(sourceOriginRank_, parts.size());
    const QStringList leading = parts.mid(0, split);
    const QStringList trailing = parts.mid(split);
    QString out = sourceOrigin_ + QStringLiteral("[")
                  + leading.join(QStringLiteral(", ")) + QStringLiteral("]")
                  + sourceMember_;
    if (!trailing.isEmpty()) {
        out += QStringLiteral("[") + trailing.join(QStringLiteral(", "))
               + QStringLiteral("]");
    }
    return out;
}

QHash<int, QByteArray> DatasetTableModel::roleNames() const
{
    // QML's TableView addresses cells through the "display" role by default.
    // "toolTip" is the same cell unrounded and unelided, which is what the
    // pointer is for.
    // "number" is the same cell as a double, which is what a cell filled by
    // its content is coloured from.
    return {{Qt::DisplayRole, "display"}, {Qt::ToolTipRole, "toolTip"}, {Number, "number"}};
}

QVariant DatasetTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole) {
        return {};
    }
    return orientation == Qt::Horizontal ? columnLabel(section) : rowLabel(section);
}

} // namespace gui
