// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "NameIndex.hpp"

#include <QModelIndex>
#include <QSortFilterProxyModel>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace gui {

class H5TreeModel;

/// Name/path filter over H5TreeModel, for the filter box at the foot of the
/// tree pane.
///
/// The two grammars it reads, and why one is anchored and the other is not, are
/// written over `gui::NameQuery` -- which is where the comparison itself lives,
/// so that this and `NameIndex` cannot drift into showing a row that the
/// results list does not collect.
///
/// **What it matches is the whole file.** The answer comes out of `NameIndex`:
/// every name, in RAM, walked once in the background when the file was opened.
/// Before that index existed this recursed over the model instead, which made
/// the filter exactly as complete as the reader's own expansion had been -- a
/// search over a file nobody had walked found nothing, and the only way to make
/// it find something was to expand the tree by hand, which reads. It was also
/// slow in the way that matters, because the recursion happened per row and per
/// keystroke: three hundred thousand objects measured at 160-330 ms a
/// character, most of it PCRE2 and QString conversions of names that had just
/// been converted.
///
/// **What it reads is still only what the reader opened.** A hit in a group
/// nobody has expanded makes that group survive the filter -- the branch is
/// there, closed, with the hits inside it -- and opening it is the reader's
/// move and one listing. The index decides *what to show*; it does not decide
/// what to read. `revealMatches` in ObjectTree.qml is the one place that opens
/// branches on the filter's behalf, and it does so only when the results are
/// few enough to be results; see `kRevealLimit`.
///
/// The index cannot always answer. A group it has not reached yet, one it
/// declined to descend into because a hard link led back to somewhere it had
/// been, and everything past its cap all come back `Unknown` -- and for those
/// this falls back to the recursive walk over what the model has read, which is
/// the behaviour it had before. So the filter is never wrong; it is complete
/// where the index is and lazy where it is not.
///
/// It also says *what* it matched, and not only that it did. A filtered tree
/// that merely hides the misses leaves the reader to find the letters again by
/// eye on every row that survived, and to open every branch by hand to reach
/// the rows the filter was for. So the proxy answers two further questions out
/// of the same comparison: which run of characters the filter took in a given
/// name (matchIn()), and which are the topmost hits the tree should be opened
/// to (matchIndexes(), revealPaths()).
class TreeFilterProxyModel : public QSortFilterProxyModel
{
    Q_OBJECT
    /// How many names the filter took across the whole file, or -1 when there
    /// is no index to ask. Counts hits and not the branches above them, and it
    /// is a property rather than a question because it goes on changing while
    /// the walk fills in behind the reader.
    Q_PROPERTY(int matchCount READ matchCount NOTIFY matchCountChanged)

public:
    explicit TreeFilterProxyModel(QObject* parent = nullptr);

    /// How many results are still few enough to be opened for the reader.
    ///
    /// A search that matched more rows than a pane could ever show is not a
    /// result to be opened, it is a search to be narrowed -- and opening it
    /// would be a listing per branch on the way to each of a quarter of a
    /// million hits, which is the whole cost of the lazy tree paid back in one
    /// keystroke. Past this the tree is left as the reader had it: the
    /// branches holding hits are on screen, closed, and they can open the one
    /// they meant.
    static constexpr int kRevealLimit = 200;

    /// Where the names come from. Not owned; it may be null, and everything
    /// goes on working when it is.
    void setNameIndex(NameIndex* index);

    /// The text to match, case-insensitively, against each node's name and
    /// path. Empty shows everything. See `gui::NameQuery` for the two grammars.
    void setFilterText(const QString& text);
    [[nodiscard]] QString filterText() const { return query_.text(); }

    /// Whether `text` would be read as a wildcard pattern rather than as a run
    /// of characters to look for. Public so the filter box can say which of the
    /// two the reader is writing.
    [[nodiscard]] static bool isWildcard(const QString& text);

    [[nodiscard]] int matchCount() const;

    /// The topmost hit down every branch, as proxy indexes -- the rows the tree
    /// has to be opened far enough to show. Empty when nothing is typed.
    ///
    /// Topmost rather than every hit, because a hit's own children are hits
    /// too: the filter matches paths, so everything under `/run` matches `run`.
    /// Opening the results themselves would answer a search for a group with
    /// its entire contents; the row the reader was looking for is the group.
    ///
    /// It costs no read. Every row this can name is one the model has already
    /// listed, which is why the tree can be opened to them without walking into
    /// the file the lazy model exists to stay out of. `revealPaths()` is the
    /// other half: the hits the index knows about and the tree has not listed
    /// its way down to yet.
    Q_INVOKABLE [[nodiscard]] QVariantList matchIndexes() const;

    /// The topmost hits in the whole file, as absolute paths, or empty when
    /// there are more than `kRevealLimit` of them. What the tree hands to
    /// `H5TreeModel::revealPath` so a result in a branch nobody has opened can
    /// still be opened to.
    Q_INVOKABLE [[nodiscard]] QStringList revealPaths() const;

    /// Where the filter bit into `name`: `{ start, length }`, and start -1 when
    /// it did not.
    ///
    /// A question rather than a role, and deliberately. The answer changes for
    /// every row on every keystroke while the rows themselves stay put, and
    /// announcing that as a model change means a `dataChanged` over the whole
    /// visible tree in the same turn the filter has just taken rows out of it
    /// -- which QQuickTreeView answers by drawing names that are no longer
    /// there. Asked instead, the delegate binds it to the filter text and the
    /// answer arrives with the keystroke that changed it.
    Q_INVOKABLE [[nodiscard]] QVariantMap matchIn(const QString& name) const;

    /// A proxy row's path in the file, and the row for a path. The pair of them
    /// let the tree write down which branches the reader had open before a
    /// filter hid them, and put them back afterwards.
    Q_INVOKABLE [[nodiscard]] QString pathAt(const QModelIndex& index) const;
    Q_INVOKABLE [[nodiscard]] QModelIndex indexForPath(const QString& path) const;

signals:
    void matchCountChanged();

protected:
    [[nodiscard]] bool filterAcceptsRow(int row,
                                        const QModelIndex& parent) const override;

private:
    [[nodiscard]] bool matches(const QModelIndex& index) const;
    /// True when `index` itself matches, or anything below it does -- out of
    /// the index where it can say, and out of what the model has read where it
    /// cannot. `index` is a *source* index.
    [[nodiscard]] bool subtreeMatches(const QModelIndex& index) const;
    /// The fallback: recurse over already-read children only. Never asks a
    /// group for children it has not read, because asking is what reads them.
    [[nodiscard]] bool readSubtreeMatches(const QModelIndex& index) const;
    /// Gather the topmost hits below `parent`, not descending past one.
    void collectMatches(const QModelIndex& parent, QVariantList& into) const;

    NameQuery query_;
    NameIndex* index_ = nullptr;
};

} // namespace gui
