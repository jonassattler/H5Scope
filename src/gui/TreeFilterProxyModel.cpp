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

QVariantMap TreeFilterProxyModel::matchIn(const QString& name) const
{
    const auto answer = [](int start, int length) {
        return QVariantMap{{QStringLiteral("start"), start},
                           {QStringLiteral("length"), length}};
    };
    if (filterText_.isEmpty()) {
        return answer(-1, 0);
    }
    if (pattern_.has_value()) {
        // A pattern is anchored, so it takes the whole name or none of it --
        // there is no shorter run to look for, and no arithmetic to do.
        const QRegularExpressionMatch found = pattern_->match(name);
        if (!found.hasMatch()) {
            return answer(-1, 0);
        }
        return answer(static_cast<int>(found.capturedStart()),
                      static_cast<int>(found.capturedLength()));
    }
    const qsizetype at = name.indexOf(filterText_, 0, Qt::CaseInsensitive);
    if (at < 0) {
        return answer(-1, 0);
    }
    return answer(static_cast<int>(at), static_cast<int>(filterText_.size()));
}

QString TreeFilterProxyModel::pathAt(const QModelIndex& index) const
{
    const auto* tree = qobject_cast<const H5TreeModel*>(sourceModel());
    return tree == nullptr ? QString{} : tree->pathAt(mapToSource(index));
}

QModelIndex TreeFilterProxyModel::indexForPath(const QString& path) const
{
    const auto* tree = qobject_cast<const H5TreeModel*>(sourceModel());
    return tree == nullptr ? QModelIndex{} : mapFromSource(tree->indexForPath(path));
}

QVariantList TreeFilterProxyModel::matchIndexes() const
{
    QVariantList found;
    if (!filterText_.isEmpty()) {
        collectMatches({}, found);
    }
    return found;
}

void TreeFilterProxyModel::collectMatches(const QModelIndex& parent,
                                          QVariantList& into) const
{
    const auto* tree = qobject_cast<const H5TreeModel*>(sourceModel());
    // Same rule as the filter itself: never ask a group for children it has
    // not read, because asking is what reads them.
    if (tree == nullptr || !tree->isPopulated(mapToSource(parent))) {
        return;
    }
    const int count = rowCount(parent);
    for (int row = 0; row < count; ++row) {
        const QModelIndex child = index(row, 0, parent);
        if (matches(mapToSource(child))) {
            into.append(QVariant::fromValue(child));
            continue; // the topmost hit down this branch, and no further
        }
        collectMatches(child, into);
    }
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
