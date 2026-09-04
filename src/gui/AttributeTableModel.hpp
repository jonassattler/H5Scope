// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "h5core/File.hpp"
#include "h5core/Types.hpp"

#include <QAbstractTableModel>

#include <memory>
#include <vector>

namespace gui {

/// Attributes of the selected object, for the Metadata tab.
///
/// Replaces the Widgets version's QTableWidget, which was a view holding its
/// own data and had no QML counterpart.
class AttributeTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column { NameColumn = 0, TypeColumn, ShapeColumn, ValueColumn, ColumnCount };
    Q_ENUM(Column)

    enum Roles {
        NameRole = Qt::UserRole + 1,
        TypeRole,
        ShapeRole,
        ValueRole,
    };
    Q_ENUM(Roles)

    explicit AttributeTableModel(QObject* parent = nullptr);

    void showObject(const std::shared_ptr<h5core::File>& file, const QString& path);
    void clear();

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation,
                                      int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

private:
    std::vector<h5core::AttributeInfo> attributes_;
};

} // namespace gui
