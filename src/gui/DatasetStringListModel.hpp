// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QAbstractListModel>

namespace gui {

class DatasetTableModel;

/// The elements of a string dataset as a flat list, one entry per element in
/// row-major order.
///
/// A string dataset is tabular in shape but not in nature: the interesting
/// thing about each element is its whole text, which a grid cell cannot show.
/// The Data Viewer stacks a full-height text pane per element, and a list is
/// what a stack of panes wants as a model.
///
/// Nothing is read here. Every lookup goes back through DatasetTableModel, so
/// this inherits that model's windowed reads unchanged -- a string dataset far
/// larger than RAM stays scrollable, and only the panes the view has actually
/// built ever touch the file.
class DatasetStringListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles {
        ValueRole = Qt::UserRole + 1, ///< the element's full text
        LabelRole,                   ///< its index, written "[2]" or "[1, 3]"
        LengthRole,                  ///< character count, for the pane header
    };
    Q_ENUM(Roles)

    explicit DatasetStringListModel(DatasetTableModel* source,
                                    QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    /// Flat position of the table cell at (row, column), or -1 if outside the
    /// dataset. Lets the grid and the stack of panes point at the same element.
    [[nodiscard]] Q_INVOKABLE int indexOfCell(int row, int column) const;

private:
    DatasetTableModel* source_;
};

} // namespace gui
