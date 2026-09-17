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
    return NameQuery(text).isWildcard();
}

void TreeFilterProxyModel::setNameIndex(NameIndex* index)
{
    if (index_ == index) {
        return;
    }
    if (index_ != nullptr) {
        disconnect(index_, nullptr, this, nullptr);
    }
    index_ = index;
    if (index_ == nullptr) {
        return;
    }
    // A branch that had nothing in it a moment ago may have something in it
    // now: the walk fills in behind the reader, and a filter typed while it is
    // running has to widen as the names land.
    connect(index_, &NameIndex::grew, this, [this] {
        if (query_.isEmpty()) {
            return;
        }
        index_->select(query_);
        beginFilterChange();
        endFilterChange(QSortFilterProxyModel::Direction::Rows);
        emit matchCountChanged();
    });
}

void TreeFilterProxyModel::setFilterText(const QString& text)
{
    if (query_.text() == text) {
        return;
    }
    query_ = NameQuery(text);
    if (index_ != nullptr) {
        // One pass over every name in the file, here rather than per row: the
        // rows ask for a mark afterwards and each answer is a hash lookup.
        index_->select(query_);
    }
    // `invalidate()` rather than `endFilterChange(Rows)`, and this is the
    // difference between a filter box and a stopwatch.
    //
    // Ending a filter change walks what survived and removes the rest as
    // *intervals*, one beginRemoveRows/endRemoveRows per run of adjacent
    // losers -- and each removal is a `QList::remove` out of the middle of the
    // parent's mapping, which is O(rows). A pattern that takes every tenth of
    // a group's sixty-five thousand children is therefore six and a half
    // thousand interval removals over a list of sixty-five thousand, plus six
    // and a half thousand row-removal signals for the view to act on. Measured
    // on `/flat` in the scale file: 30 s for one keystroke, and it was 30 s
    // before the index existed too -- the index made the matching fast and
    // left this untouched.
    //
    // `invalidate()` throws the mappings away instead and lets them be rebuilt
    // for whatever the view asks about next, under one layoutChanged. It keeps
    // persistent indexes (QSortFilterProxyModelPrivate::_q_clearMapping stores
    // and restores them), which is what the tree's expansion state is made of.
    beginFilterChange();
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
    emit matchCountChanged();
}

int TreeFilterProxyModel::matchCount() const
{
    return index_ == nullptr ? -1 : index_->hits();
}

QVariantMap TreeFilterProxyModel::matchIn(const QString& name) const
{
    const auto [start, length] = query_.markIn(name);
    return QVariantMap{{QStringLiteral("start"), start},
                       {QStringLiteral("length"), length}};
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
    if (query_.isEmpty()) {
        return found;
    }
    // Past the bound there is nothing to open: see kRevealLimit. Asked of the
    // index rather than counted here, so that the tree does not open to the
    // first two hundred of a quarter of a million and stop -- which would read
    // as the search having found exactly those.
    if (index_ != nullptr && index_->topHits(kRevealLimit).size() > kRevealLimit) {
        return found;
    }
    collectMatches({}, found);
    if (found.size() > kRevealLimit) {
        found.clear();
    }
    return found;
}

QStringList TreeFilterProxyModel::revealPaths() const
{
    if (query_.isEmpty() || index_ == nullptr) {
        return {};
    }
    QStringList found = index_->topHits(kRevealLimit);
    if (found.size() > kRevealLimit) {
        return {}; // more results than results: see kRevealLimit
    }
    return found;
}

void TreeFilterProxyModel::collectMatches(const QModelIndex& parent,
                                          QVariantList& into) const
{
    const auto* tree = qobject_cast<const H5TreeModel*>(sourceModel());
    // Never ask a group for children it has not read, because asking is what
    // reads them. What this can reach is what the tree is already showing.
    if (tree == nullptr || !tree->isPopulated(mapToSource(parent))) {
        return;
    }
    const int count = rowCount(parent);
    for (int row = 0; row < count; ++row) {
        if (into.size() > kRevealLimit) {
            return; // one past the bound is enough to know there are too many
        }
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
    return query_.accepts(name, path);
}

bool TreeFilterProxyModel::subtreeMatches(const QModelIndex& index) const
{
    const auto* tree = qobject_cast<const H5TreeModel*>(sourceModel());
    if (tree == nullptr) {
        return true;
    }
    if (index_ != nullptr) {
        // One conversion of one path, rather than the recursive walk this
        // used to be: the answer covers everything below the row as well.
        switch (index_->answer(tree->pathAt(index))) {
        case NameIndex::Answer::Yes:
            return true;
        case NameIndex::Answer::No:
            return false;
        case NameIndex::Answer::Unknown:
            break; // the index has not been here; look for ourselves
        }
    }
    return readSubtreeMatches(index);
}

bool TreeFilterProxyModel::readSubtreeMatches(const QModelIndex& index) const
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
        if (readSubtreeMatches(tree->index(row, 0, index))) {
            return true;
        }
    }
    return false;
}

bool TreeFilterProxyModel::filterAcceptsRow(int row, const QModelIndex& parent) const
{
    if (query_.isEmpty()) {
        return true;
    }
    QAbstractItemModel* source = sourceModel();
    if (source == nullptr) {
        return true;
    }
    return subtreeMatches(source->index(row, 0, parent));
}

} // namespace gui
