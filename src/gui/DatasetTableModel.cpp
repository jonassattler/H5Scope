// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "DatasetTableModel.hpp"

#include "h5core/Error.hpp"

#include <QStringList>
#include <QVariantList>

#include <algorithm>
#include <cmath>
#include <limits>

namespace gui {

DatasetTableModel::DatasetTableModel(QObject* parent) : QAbstractTableModel(parent) {}

void DatasetTableModel::setSource(bool present, h5core::DatasetInfo info, QString path)
{
    beginResetModel();
    // Anything still on its way describes the last dataset. Disowning it here
    // is what stops a block of the previous selection arriving a frame later
    // and being painted into this one's grid.
    requests_.reset();
    askedRowOrigin_ = -1;
    askedColumnOrigin_ = -1;

    present_ = present;
    info_ = std::move(info);
    sourcePath_ = std::move(path);
    block_ = {};
    errorText_.clear();

    rebuild(present_ ? defaultLayout(info_.shape, info_.image) : TableLayout{});

    if (present_ && !info_.readable()) {
        errorText_ = tr("This dataset cannot be read: %1")
                         .arg(QString::fromStdString(info_.unreadableReason()));
    }
    endResetModel();
    emit datasetChanged();
}

void DatasetTableModel::setLayout(const TableLayout& layout)
{
    if (!present_ || layout.rank() != info_.rank()
        || layout.onX.size() != layout.indices.size()) {
        return;
    }
    beginResetModel();
    rebuild(layout);
    endResetModel();
}

void DatasetTableModel::rebuild(TableLayout layout)
{
    // A null dataspace holds no elements at all, and the empty product over the
    // axes would otherwise make a one-cell table out of nothing.
    axes_ = TableAxes(std::move(layout), !present_ || info_.isNull());
    block_ = {};
    askedRowOrigin_ = -1;
    askedColumnOrigin_ = -1;
    // A different table has a different extent, and a colour ramp stretched
    // between the old one would be reading the new numbers on the old scale.
    extent_.reset();
}

int DatasetTableModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid() || !present_ || !errorText_.isEmpty()) {
        return 0;
    }
    return static_cast<int>(axes_.rows());
}

int DatasetTableModel::columnCount(const QModelIndex& parent) const
{
    if (parent.isValid() || !present_ || !errorText_.isEmpty()) {
        return 0;
    }
    return static_cast<int>(axes_.columns());
}

DatasetTableModel::Block DatasetTableModel::readBlock(const h5core::DataSource& source,
                                                      const TableAxes& axes,
                                                      Block block, QString& error)
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

                std::vector<hsize_t> offset =
                    axes.coordinates(block.rowOrigin + r, column0);
                std::vector<hsize_t> count(axes.rank(), 1);
                if (hasX) {
                    count[lastX] = static_cast<hsize_t>(run);
                }

                const h5core::DataWindow window = source.readWindow(offset, count);
                for (int i = 0; i < run && i < static_cast<int>(window.cells.size());
                     ++i) {
                    block.cells[static_cast<std::size_t>(r) * block.columns + c + i] =
                        QString::fromStdString(
                            window.cells[static_cast<std::size_t>(i)]);
                }
                c += run;
            }
        }
    } catch (const h5core::H5Error& failure) {
        error = QString::fromStdString(failure.summary());
        return {};
    }

    block.valid = true;
    return block;
}

void DatasetTableModel::ensureBlock(int row, int column) const
{
    if (block_.valid && row >= block_.rowOrigin
        && row < block_.rowOrigin + block_.rows && column >= block_.columnOrigin
        && column < block_.columnOrigin + block_.columns) {
        return;
    }

    Block block;
    block.rowOrigin = (row / kBlockRows) * kBlockRows;
    block.columnOrigin = (column / kBlockColumns) * kBlockColumns;
    block.rows = static_cast<int>(
        std::min<qint64>(kBlockRows, axes_.rows() - block.rowOrigin));
    block.columns = static_cast<int>(
        std::min<qint64>(kBlockColumns, axes_.columns() - block.columnOrigin));
    if (block.rows <= 0 || block.columns <= 0) {
        return;
    }

    // One request per block, however many cells of it the view asks about
    // before the answer lands. Without this the first paint of a screenful
    // would queue a hundred identical reads.
    if (askedRowOrigin_ == block.rowOrigin && askedColumnOrigin_ == block.columnOrigin) {
        return;
    }
    askedRowOrigin_ = block.rowOrigin;
    askedColumnOrigin_ = block.columnOrigin;

    auto* self = const_cast<DatasetTableModel*>(this);
    struct Read {
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
            read.block = readBlock(*source, axes, block, read.error);
            return read;
        },
        [self](Read read) {
            self->askedRowOrigin_ = -1;
            self->askedColumnOrigin_ = -1;
            if (!read.error.isEmpty()) {
                self->block_ = {};
                self->errorText_ = read.error;
                emit self->dataChanged(self->index(0, 0),
                                       self->index(std::max(0, self->rowCount() - 1),
                                                   std::max(0, self->columnCount() - 1)));
                return;
            }
            if (!read.block.valid) {
                return;
            }
            self->block_ = std::move(read.block);
            self->errorText_.clear();
            // Only the block that arrived. The rest of the table is either
            // already painted or is waiting on a request of its own.
            const QModelIndex topLeft =
                self->index(self->block_.rowOrigin, self->block_.columnOrigin);
            const QModelIndex bottomRight =
                self->index(self->block_.rowOrigin + self->block_.rows - 1,
                            self->block_.columnOrigin + self->block_.columns - 1);
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

DatasetTableModel::NumericGrid
DatasetTableModel::sampleValues(int firstRow, int rowSpan, int maxRows,
                                int firstColumn, int columnSpan,
                                int maxColumns) const
{
    return sampleValues(axes_, firstRow, rowSpan, maxRows, firstColumn, columnSpan,
                        maxColumns);
}

DatasetTableModel::NumericGrid
DatasetTableModel::sampleFrom(const h5core::DataSource& source, const TableAxes& axes,
                              int firstRow, int rowSpan, int maxRows, int firstColumn,
                              int columnSpan, int maxColumns)
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
    grid.columnStride =
        static_cast<int>((columnExtent + maxColumns - 1) / maxColumns);
    grid.rows = static_cast<int>((rowExtent + grid.rowStride - 1) / grid.rowStride);
    grid.columns =
        static_cast<int>((columnExtent + grid.columnStride - 1) / grid.columnStride);

    const auto nan = std::numeric_limits<double>::quiet_NaN();
    grid.values.assign(static_cast<std::size_t>(grid.rows) * grid.columns, nan);

    const bool hasX = !axes.xDims().empty();
    const std::size_t lastX = hasX ? axes.xDims().back() : 0;

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
                    consecutive ? static_cast<int>(std::min<qint64>(
                                      kReadRun, columnExtent - wanted))
                                : 1;
                const int run = axes.runLength(column, limit);

                std::vector<hsize_t> offset = axes.coordinates(row, column);
                std::vector<hsize_t> count(axes.rank(), 1);
                if (hasX) {
                    count[lastX] = static_cast<hsize_t>(run);
                }
                const h5core::NumericWindow window =
                    source.readNumericWindow(offset, count);

                // Every sampled column this run happens to cover. Reading
                // starts on a wanted column by construction, so a full run
                // yields at least one; `progress` covers the one case that
                // does not -- a read clamped to nothing -- because a loop that
                // consumed no column would not advance at all.
                const int progress = taken;
                for (; taken < grid.columns; ++taken) {
                    const qint64 within = qint64{taken} * grid.columnStride - wanted;
                    if (within >= run
                        || within >= static_cast<qint64>(window.values.size())) {
                        break;
                    }
                    const double value =
                        window.values[static_cast<std::size_t>(within)];
                    grid.values[static_cast<std::size_t>(r) * grid.columns + taken] =
                        value;
                    if (std::isfinite(value)) {
                        if (!grid.hasFinite) {
                            grid.minimum = value;
                            grid.maximum = value;
                            grid.hasFinite = true;
                        } else {
                            grid.minimum = std::min(grid.minimum, value);
                            grid.maximum = std::max(grid.maximum, value);
                        }
                    }
                }
                if (taken == progress) {
                    ++taken; // leaves this column at NaN, which is what it is
                }
            }
        }
    } catch (const h5core::H5Error& error) {
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
                               .arg(sourcePath_,
                                    QString::fromStdString(info_.type.description))
                         : errorText_;
        return false;
    }
    return true;
}

DatasetTableModel::NumericGrid
DatasetTableModel::sampleValues(const TableAxes& axes, int firstRow, int rowSpan,
                                int maxRows, int firstColumn, int columnSpan,
                                int maxColumns) const
{
    NumericGrid grid;
    if (!sampleable(grid)) {
        return grid;
    }
    return H5Thread::instance().invoke([&](H5Session& session) {
        const h5core::DataSource* source = session.source();
        return (source == nullptr)
                   ? NumericGrid{}
                   : sampleFrom(*source, axes, firstRow, rowSpan, maxRows, firstColumn,
                                columnSpan, maxColumns);
    });
}

void DatasetTableModel::requestSamples(const TableAxes& axes, int firstRow, int rowSpan,
                                       int maxRows, int firstColumn, int columnSpan,
                                       int maxColumns,
                                       std::function<void(NumericGrid)> then)
{
    NumericGrid grid;
    if (!sampleable(grid)) {
        // Answered here rather than across the thread: nothing is going to be
        // read, and a caller that got its answer a frame later for no reason
        // would flicker for no reason.
        then(std::move(grid));
        return;
    }
    H5Thread::instance().submit(
        requests_,
        [axes, firstRow, rowSpan, maxRows, firstColumn, columnSpan,
         maxColumns](H5Session& session) {
            const h5core::DataSource* source = session.source();
            return (source == nullptr)
                       ? NumericGrid{}
                       : sampleFrom(*source, axes, firstRow, rowSpan, maxRows,
                                    firstColumn, columnSpan, maxColumns);
        },
        std::move(then));
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
    if (row < 0 || column < 0 || row >= axes_.rows() || column >= axes_.columns()) {
        return {};
    }
    // A number the delegate can always rely on. Every other exit below is a
    // cell that is not there or would not read, and both of those are the same
    // "no value here" a struct or a string is.
    const auto missing = [role] {
        return role == Number ? QVariant(std::numeric_limits<double>::quiet_NaN())
                              : QVariant();
    };

    ensureBlock(row, column);
    if (!block_.valid) {
        return missing();
    }
    const auto flat = static_cast<std::size_t>(row - block_.rowOrigin) * block_.columns
                      + static_cast<std::size_t>(column - block_.columnOrigin);
    if (flat >= block_.cells.size()) {
        return missing();
    }
    const QString& cell = block_.cells[flat];
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
        const NumericGrid grid =
            sampleValues(0, -1, kExtentSamples, 0, -1, kExtentSamples);
        extent_ = Extent{grid.minimum, grid.maximum, grid.hasFinite};
    }
    return {{QStringLiteral("minimum"), extent_->minimum},
            {QStringLiteral("maximum"), extent_->maximum},
            {QStringLiteral("valid"), extent_->valid}};
}

bool DatasetTableModel::floats() const
{
    return present_
           && info_.type.cls == h5core::TypeClass::Float
           && info_.readable();
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
        emit dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1),
                         {Qt::DisplayRole});
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
        emit dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1),
                         {Qt::DisplayRole});
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

int DatasetTableModel::widestCell(int firstRow, int rows, int firstColumn,
                                  int columns) const
{
    if (!present_ || rows <= 0 || columns <= 0) {
        return 0;
    }
    // One block covers what a screen shows several times over, so the rectangle
    // asked for is clamped into the block the view is already over rather than
    // sliding it -- a column width must not be the thing that decides which
    // part of a dataset gets read.
    ensureBlock(std::max(firstRow, 0), std::max(firstColumn, 0));
    if (!block_.valid) {
        return 0;
    }
    const int firstR = std::max(firstRow, block_.rowOrigin);
    const int lastR = std::min(firstRow + rows, block_.rowOrigin + block_.rows);
    const int firstC = std::max(firstColumn, block_.columnOrigin);
    const int lastC = std::min(firstColumn + columns,
                               block_.columnOrigin + block_.columns);

    int widest = 0;
    for (int r = firstR; r < lastR; ++r) {
        for (int c = firstC; c < lastC; ++c) {
            const auto flat =
                static_cast<std::size_t>(r - block_.rowOrigin) * block_.columns
                + static_cast<std::size_t>(c - block_.columnOrigin);
            if (flat < block_.cells.size()) {
                widest = std::max<int>(
                    widest, static_cast<int>(formatted(block_.cells[flat]).size()));
            }
        }
    }
    return widest;
}

QVariantMap DatasetTableModel::elementAt(int row, int column) const
{
    if (!present_ || row < 0 || column < 0 || row >= axes_.rows()
        || column >= axes_.columns()) {
        return {};
    }

    // The one read in this class that is still waited for. It is a single
    // element, asked for by a click on the cell already on screen, and QML
    // calls it as a function with a return value -- so making it asked-for
    // would mean a property, a signal and a pane that is briefly blank, to
    // save a wait of one object read. See DEVLOG: this is the remaining
    // synchronous point and it is deliberate.
    struct Read {
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
            result.value = source->readElement(axes_.coordinates(row, column));
        } catch (const h5core::H5Error& error) {
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
    const std::size_t rank = axes_.rank();
    if (rank == 0 || !present_) {
        return {};
    }

    const std::vector<hsize_t> coords = axes_.coordinates(row, column);

    // Rank 1 has nothing to disambiguate, so it reads as a plain index -- and
    // the axis that carries no dimension has no index to print at all.
    if (rank == 1) {
        const bool shown = axes_.layout().onX[0] ? showX : showY;
        return shown ? QString::number(coords[0]) : QString{};
    }

    QStringList parts;
    parts.reserve(static_cast<qsizetype>(rank));
    for (std::size_t d = 0; d < rank; ++d) {
        const bool shown = axes_.layout().onX[d] ? showX : showY;
        parts << (shown ? QString::number(coords[d]) : QStringLiteral("_"));
    }
    // Brackets, not parentheses: the slice line above the grid already writes
    // `/cube[:, 2, 0:4]`, and a cell's index tuple is the same statement about
    // the same dataset written for one element.
    return QStringLiteral("[%1]").arg(parts.join(QLatin1Char(',')));
}

QString DatasetTableModel::rowLabel(int row) const
{
    if (row < 0 || row >= axes_.rows()) {
        return {};
    }
    return labelFor(row, 0, false, true);
}

QString DatasetTableModel::columnLabel(int column) const
{
    if (column < 0 || column >= axes_.columns()) {
        return {};
    }
    return labelFor(0, column, true, false);
}

QString DatasetTableModel::cellLabel(int row, int column) const
{
    if (row < 0 || row >= axes_.rows() || column < 0 || column >= axes_.columns()) {
        return {};
    }
    return labelFor(row, column, true, true);
}

QHash<int, QByteArray> DatasetTableModel::roleNames() const
{
    // QML's TableView addresses cells through the "display" role by default.
    // "toolTip" is the same cell unrounded and unelided, which is what the
    // pointer is for.
    // "number" is the same cell as a double, which is what a cell filled by
    // its content is coloured from.
    return {{Qt::DisplayRole, "display"},
            {Qt::ToolTipRole, "toolTip"},
            {Number, "number"}};
}

QVariant DatasetTableModel::headerData(int section, Qt::Orientation orientation,
                                       int role) const
{
    if (role != Qt::DisplayRole) {
        return {};
    }
    return orientation == Qt::Horizontal ? columnLabel(section) : rowLabel(section);
}

} // namespace gui
