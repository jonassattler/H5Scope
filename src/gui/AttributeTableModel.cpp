// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "AttributeTableModel.hpp"

#include "h5core/Attribute.hpp"
#include "h5core/Error.hpp"

#include <QStringList>

namespace gui {
namespace {

QString formatShape(const std::vector<hsize_t>& shape)
{
    if (shape.empty()) {
        return QStringLiteral("scalar");
    }
    QStringList parts;
    parts.reserve(static_cast<int>(shape.size()));
    for (const hsize_t dim : shape) {
        parts << QString::number(dim);
    }
    return parts.join(QStringLiteral(" x "));
}

} // namespace

AttributeTableModel::AttributeTableModel(QObject* parent) : QAbstractTableModel(parent) {}

int AttributeTableModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(attributes_.size());
}

int AttributeTableModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QHash<int, QByteArray> AttributeTableModel::roleNames() const
{
    return {
        {NameRole, "name"},
        {TypeRole, "type"},
        {ShapeRole, "shape"},
        {ValueRole, "value"},
        {Qt::DisplayRole, "display"},
    };
}

QVariant AttributeTableModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0
        || static_cast<std::size_t>(index.row()) >= attributes_.size()) {
        return {};
    }
    const auto& attr = attributes_[static_cast<std::size_t>(index.row())];

    switch (role) {
    case NameRole:
        return QString::fromStdString(attr.name);
    case TypeRole:
        return QString::fromStdString(attr.type.description);
    case ShapeRole:
        return formatShape(attr.shape);
    case ValueRole:
        return QString::fromStdString(attr.value);
    case Qt::DisplayRole:
        // Column-addressed access, used by TableView and by the C++ tests.
        switch (index.column()) {
        case NameColumn:  return QString::fromStdString(attr.name);
        case TypeColumn:  return QString::fromStdString(attr.type.description);
        case ShapeColumn: return formatShape(attr.shape);
        case ValueColumn: return QString::fromStdString(attr.value);
        default:          return {};
        }
    default:
        return {};
    }
}

QVariant AttributeTableModel::headerData(int section, Qt::Orientation orientation,
                                         int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }
    switch (section) {
    case NameColumn:  return QStringLiteral("Name");
    case TypeColumn:  return QStringLiteral("Type");
    case ShapeColumn: return QStringLiteral("Shape");
    case ValueColumn: return QStringLiteral("Value");
    default:          return {};
    }
}

void AttributeTableModel::clear()
{
    beginResetModel();
    attributes_.clear();
    endResetModel();
}

void AttributeTableModel::setAttributes(std::vector<h5core::AttributeInfo> attributes)
{
    beginResetModel();
    attributes_ = std::move(attributes);
    endResetModel();
}

} // namespace gui
