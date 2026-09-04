// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "DatasetStringListModel.hpp"

#include "DatasetTableModel.hpp"

#include <QStringList>

#include <limits>

namespace gui {

DatasetStringListModel::DatasetStringListModel(DatasetTableModel* source,
                                               QObject* parent)
    : QAbstractListModel(parent), source_(source)
{
    // The source resets wholesale on every selection and every slice change,
    // and there is no partial update it can emit that this view could use --
    // a different dataset means a different list from index zero.
    connect(source_, &QAbstractItemModel::modelAboutToBeReset, this,
            [this] { beginResetModel(); });
    connect(source_, &QAbstractItemModel::modelReset, this, [this] { endResetModel(); });
}

int DatasetStringListModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    const qint64 rows = source_->rowCount();
    const qint64 columns = source_->columnCount();
    const qint64 total = rows * columns;
    // A dataset can hold more elements than an int can index. Clamping keeps
    // the view coherent instead of wrapping negative; nothing this large is
    // browsable pane-by-pane anyway.
    return static_cast<int>(
        std::min<qint64>(total, std::numeric_limits<int>::max()));
}

int DatasetStringListModel::indexOfCell(int row, int column) const
{
    const int columns = source_->columnCount();
    if (row < 0 || column < 0 || row >= source_->rowCount() || column >= columns) {
        return -1;
    }
    const qint64 flat = static_cast<qint64>(row) * columns + column;
    return flat > std::numeric_limits<int>::max() ? -1 : static_cast<int>(flat);
}

QVariant DatasetStringListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= rowCount()) {
        return {};
    }

    const int columns = source_->columnCount();
    if (columns <= 0) {
        return {};
    }
    const int row = index.row() / columns;
    const int column = index.row() % columns;

    switch (role) {
    case LabelRole:
        // The table's own name for the cell, so a pane is labelled with the
        // element's index tuple rather than with its position in a layout the
        // reader has just rearranged.
        return source_->cellLabel(row, column);
    case Qt::DisplayRole:
    case ValueRole:
        return source_->index(row, column).data(Qt::DisplayRole);
    case LengthRole:
        return source_->index(row, column).data(Qt::DisplayRole).toString().size();
    default:
        return {};
    }
}

QHash<int, QByteArray> DatasetStringListModel::roleNames() const
{
    return {
        {ValueRole, "value"},
        {LabelRole, "label"},
        {LengthRole, "length"},
        {Qt::DisplayRole, "display"},
    };
}

} // namespace gui
