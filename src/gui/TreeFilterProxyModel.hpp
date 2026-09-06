// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QModelIndex>
#include <QRegularExpression>
#include <QSortFilterProxyModel>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <optional>

namespace gui {

class H5TreeModel;

/// Name/path filter over H5TreeModel, for the filter box at the foot of the
/// tree pane.
///
/// Two grammars, and which one is in use is decided by what was typed rather
/// than by a mode the reader has to set:
///
///   plain text   matched anywhere in the name or the path, case-insensitively.
///                `temp` finds `/run/3/temperature`.
///   a wildcard   `*`, `?` or a `[...]` class anywhere in the text makes it a
///                pattern, matched against the whole of the name and the whole
///                of the path. `temp*` is every name that begins with temp;
///                `*.raw` every one that ends in it; `/run/?/temp*` reaches
///                into the path itself.
///
/// A pattern is anchored and plain text is not, which sounds inconsistent and
/// is the only reading that makes both useful: the whole point of writing
/// `temp*` rather than `temp` is to say *begins with*, and a `*` that only ever
/// added to an unanchored substring search would mean nothing at all. `*` also
/// matches `/` here, so a pattern can cross the hierarchy; a path is one string
/// as far as this box is concerned.
///
/// The subtlety underneath is laziness. QSortFilterProxyModel's own recursive
/// filtering asks every candidate parent for its children, and in this model
/// asking for a row count is what triggers the read from disk -- so switching
/// it on would walk the entire file the moment the user typed a character,
/// which is exactly what H5TreeModel is built to avoid.
///
/// This subclass therefore recurses only into groups whose children have
/// already been read. A filter matches what the user has actually opened, plus
/// anything at the top level; expanding further widens the search. That is a
/// deliberate trade: an instant, bounded filter over the loaded hierarchy
/// rather than a complete one that stalls on a multi-gigabyte file.
///
/// It also says *what* it matched, and not only that it did. A filtered tree
/// that merely hides the misses leaves the reader to find the letters again by
/// eye on every row that survived, and to open every branch by hand to reach
/// the rows the filter was for. So the proxy answers two further questions out
/// of the same comparison: which run of characters the filter took in a given
/// name (matchIn()), and which are the topmost hits the tree should be opened
/// to (matchIndexes()).
class TreeFilterProxyModel : public QSortFilterProxyModel
{
    Q_OBJECT

public:
    explicit TreeFilterProxyModel(QObject* parent = nullptr);

    /// The text to match, case-insensitively, against each node's name and
    /// path. Empty shows everything. See the class note for the two grammars.
    void setFilterText(const QString& text);
    [[nodiscard]] QString filterText() const { return filterText_; }

    /// Whether `text` would be read as a wildcard pattern rather than as a run
    /// of characters to look for. Public so the filter box can say which of the
    /// two the reader is writing.
    [[nodiscard]] static bool isWildcard(const QString& text);

    /// The topmost hit down every branch, as proxy indexes -- the rows the tree
    /// has to be opened far enough to show. Empty when nothing is typed.
    ///
    /// Topmost rather than every hit, because a hit's own children are hits
    /// too: the filter matches paths, so everything under `/run` matches `run`.
    /// Opening the results themselves would answer a search for a group with
    /// its entire contents; the row the reader was looking for is the group.
    ///
    /// It costs no read. Every row above a hit is on screen only because
    /// something already read below it matched, so it has been listed already
    /// -- which is why the tree can be opened to the results without walking
    /// into the file the lazy model exists to stay out of.
    Q_INVOKABLE [[nodiscard]] QVariantList matchIndexes() const;

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
    ///
    /// Plain text marks the run it found; a pattern marks the whole name,
    /// because a pattern is anchored and takes all of it or none.
    Q_INVOKABLE [[nodiscard]] QVariantMap matchIn(const QString& name) const;

    /// A proxy row's path in the file, and the row for a path. The pair of them
    /// let the tree write down which branches the reader had open before a
    /// filter hid them, and put them back afterwards.
    Q_INVOKABLE [[nodiscard]] QString pathAt(const QModelIndex& index) const;
    Q_INVOKABLE [[nodiscard]] QModelIndex indexForPath(const QString& path) const;

protected:
    [[nodiscard]] bool filterAcceptsRow(int row,
                                        const QModelIndex& parent) const override;

private:
    [[nodiscard]] bool matches(const QModelIndex& index) const;
    /// True when `index` itself matches, or any already-read descendant does.
    [[nodiscard]] bool subtreeMatches(const QModelIndex& index) const;
    /// Gather the topmost hits below `parent`, not descending past one.
    void collectMatches(const QModelIndex& parent, QVariantList& into) const;

    QString filterText_;
    /// The compiled pattern, when the text is one and it compiled. Held rather
    /// than rebuilt per row: this is asked once per node of the loaded tree on
    /// every keystroke. Absent for plain text, and absent for a pattern that
    /// does not compile -- an unclosed `[` is a pattern halfway to being typed,
    /// and falling back to a substring search keeps the box answering while it
    /// is finished rather than emptying the tree.
    std::optional<QRegularExpression> pattern_;
};

} // namespace gui
