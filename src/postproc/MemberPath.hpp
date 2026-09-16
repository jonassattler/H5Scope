// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "h5core/FieldDataset.hpp"
#include "h5core/Types.hpp"

#include <QString>
#include <QStringList>

namespace postproc {

/// One link of a member chain, as it was written.
struct MemberStep {
    QString name;
    /// The body between the brackets, brackets taken off. Empty when the member
    /// was named without one.
    QString subscript;
    /// Whether a subscript was written at all, which `subscript` alone cannot
    /// say: `.b[]` is not `.b`, and only one of them is a mistake.
    bool bracketed = false;
};

/// Take a chain apart without knowing what it applies to.
///
/// Separate from resolving it because the two have different reasons to fail
/// and different places to be called. This one answers about the text -- a
/// missing name, an unbalanced bracket -- on a surface that has no datatype in
/// hand, such as a custom plot entry naming a dataset nobody has selected.
///
/// A member name is everything up to the next '.' or '[', which is permissive
/// on purpose: HDF5 puts almost no constraint on one, and a viewer that refused
/// the names a file actually contains would be wrong more often than a grammar
/// that accepts a strange one. The two characters the notation needs are the
/// two it cannot hold.
[[nodiscard]] bool parseMemberChain(const QString& text, std::vector<MemberStep>& chain,
                                    QString& error);

/// A member chain resolved against the datatype it applies to.
struct MemberChain {
    /// What a read needs: the links, the axes they append, and the leaf type.
    h5core::MemberSelection selection;

    /// The subscripts the chain carried, one per axis it appends, in the order
    /// those axes come. Empty strings for the axes nobody wrote one for.
    ///
    /// **These belong on the slice line, not on the chain.** `.samples[2]` and
    /// `[..., 2]` over the derived shape are the same selection -- that is the
    /// whole of the identity the notation rests on -- so the short spelling is
    /// resolved by handing its subscripts here, where the ordinary slice
    /// grammar reads them against the ordinary derived shape. Nothing about a
    /// member subscript is a second kind of subscript.
    QStringList folded;

    /// What to call each appended axis: the member that contributed it. A table
    /// row for an axis called "samples" is worth more than one called "dim 1".
    QStringList dimNames;

    QString error;

    [[nodiscard]] bool valid() const { return error.isEmpty(); }
    [[nodiscard]] bool empty() const { return selection.links.empty(); }
};

/// Read a member chain against the type it applies to.
///
/// The type rather than the file: this is arithmetic over a `TypeInfo` that was
/// described when the dataset was selected, so it costs no read and answers on
/// every keystroke, which is the contract every other validator in this
/// application keeps.
///
/// **The vlen rule.** An array member contributes a real axis -- every record
/// has the same four samples -- so `.samples[2]` folds into the slice like any
/// other subscript. A vlen's length differs from record to record, so it
/// contributes no axis at all, and the three cases are:
///
///   * `.tags` keeps the dataset's shape and reads as the list each record
///     holds. Not numeric, so it prints in the grid and does not plot.
///   * `.tags[3]` also keeps the shape, and reads as what the list holds -- so
///     it does plot. A record whose list is shorter than four has no value
///     there, which is an empty cell and a gap in a line.
///   * `.tags[0:2]` is refused. A range of a ragged member has no single shape,
///     and every view here is rectangular.
///
/// Nothing in that needs to know how long any record's list is, so nothing has
/// to read the whole dataset before it can draw the first of it.
[[nodiscard]] MemberChain resolveMemberChain(const QString& text,
                                             const h5core::TypeInfo& type);

/// The same, from an already-parsed chain.
[[nodiscard]] MemberChain resolveMemberChain(const std::vector<MemberStep>& chain,
                                             const h5core::TypeInfo& type);

/// The one slice line a member expression means, over the derived shape.
///
/// This is the identity, as a string. `/events[0:100].samples[2]` selects the
/// same elements as `(/events.samples)[0:100, 2]`, so the leading subscript is
/// expanded to name every axis of the dataset and the chain's own subscripts
/// are written after it, in the order the chain appended them. What comes back
/// is an ordinary slice line against an ordinary shape, which is what every
/// reader below this point already knows how to do.
///
/// `originRank` is the dataset's own rank -- how many of the terms in the line
/// belong to the leading subscript. The expansion here is textual and stops at
/// the comma: the terms themselves are read by the ordinary grammar, against
/// the ordinary shape, afterwards.
[[nodiscard]] QString sliceLineFor(const QString& subscript, const QStringList& folded,
                                   std::size_t originRank);

/// A chain printed back: ".position.x". What the member box shows, and what
/// `path()` puts after the dataset's own name.
[[nodiscard]] QString writeMemberChain(const std::vector<MemberStep>& chain);

} // namespace postproc
