// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "TreeFilterProxyModel.hpp"

#include "H5TreeModel.hpp"

namespace gui {

TreeFilterProxyModel::TreeFilterProxyModel(QObject* parent)
    : QSortFilterProxyModel(parent)
{
}

bool TreeFilterProxyModel::isWildcard(const QString& text)
{
    return text.contains(u'*') || text.contains(u'?') || text.contains(u'[');
}

void TreeFilterProxyModel::setFilterText(const QString& text)
{
    if (filterText_ == text) {
        return;
    }
    beginFilterChange();
    filterText_ = text;

    pattern_.reset();
    if (isWildcard(text)) {
        // NonPathWildcardConversion: `*` crosses `/` as well as everything
        // else. A path is one string in this box, so `/run/*/temp` has to be
        // able to reach past a separator -- the path-aware conversion would
        // stop it at one.
        QRegularExpression compiled = QRegularExpression::fromWildcard(
            text, Qt::CaseInsensitive,
            QRegularExpression::NonPathWildcardConversion);
        if (compiled.isValid()) {
            pattern_ = std::move(compiled);
        }
    }
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
}

bool TreeFilterProxyModel::matches(const QModelIndex& index) const
{
    const QString name = index.data(H5TreeModel::NameRole).toString();
    const QString path = index.data(H5TreeModel::PathRole).toString();
    if (pattern_.has_value()) {
        return pattern_->match(name).hasMatch() || pattern_->match(path).hasMatch();
    }
    return name.contains(filterText_, Qt::CaseInsensitive)
           || path.contains(filterText_, Qt::CaseInsensitive);
}

bool TreeFilterProxyModel::subtreeMatches(const QModelIndex& index) const
{
    if (matches(index)) {
        return true;
    }

    const auto* tree = qobject_cast<const H5TreeModel*>(sourceModel());
    // Descending into a group that has not been read yet would read it. Stop
    // here instead: the node stays hidden until the user expands it.
    if (tree == nullptr || !tree->isPopulated(index)) {
        return false;
    }

    const int count = tree->rowCount(index);
    for (int row = 0; row < count; ++row) {
        if (subtreeMatches(tree->index(row, 0, index))) {
            return true;
        }
    }
    return false;
}

bool TreeFilterProxyModel::filterAcceptsRow(int row, const QModelIndex& parent) const
{
    if (filterText_.isEmpty()) {
        return true;
    }
    QAbstractItemModel* source = sourceModel();
    if (source == nullptr) {
        return true;
    }
    return subtreeMatches(source->index(row, 0, parent));
}

} // namespace gui
