// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QRegularExpression>
#include <QSortFilterProxyModel>
#include <QString>

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

protected:
    [[nodiscard]] bool filterAcceptsRow(int row,
                                        const QModelIndex& parent) const override;

private:
    [[nodiscard]] bool matches(const QModelIndex& index) const;
    /// True when `index` itself matches, or any already-read descendant does.
    [[nodiscard]] bool subtreeMatches(const QModelIndex& index) const;

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
