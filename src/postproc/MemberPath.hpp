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

    /// How many of the appended axes each link contributed, one entry per link
    /// in `selection.links`, and how many came before any link -- from a
    /// dataset whose own type is an array of compounds, which no member name
    /// can carry. Together they say which link a subscript has to be written
    /// on to read back as the axis it is about; see writeSelection.
    std::vector<std::size_t> linkAxes;
    std::size_t leadingAxes = 0;

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

/// A selection written as one line that reads back as the same selection:
/// `[:].samples[2]`, not `[:, 2].samples`.
///
/// `terms` is the slice over the whole derived shape, one per axis, as the
/// slice line holds it. The first `originRank` of them are the dataset's own
/// and go in front of the chain; each of the rest goes on the link that
/// appended its axis, because a subscript written before a chain binds to the
/// dataset's axes and one written after `.b` binds to the axes `b` appended --
/// the rule sliceLineFor reads by. A link whose axes are all whole is written
/// bare, so a plain `[:].samples` stays as short as it was.
///
/// It is the other half of sliceLineFor, and the reason it exists is that the
/// slice bar used to print the derived slice in front of the chain -- a
/// spelling nothing in this program reads. `[:, 2].samples` over a rank-1
/// table came back from Return as "3 subscripts for 2 dimensions", so the one
/// box that prints a compound's selection printed a line it would then refuse.
///
/// The chain's own text, vlen index included, is `chain.selection.text`. A
/// chain with leading axes cannot be written this way -- no member carries
/// them -- and comes back with every term in front of it, as before.
[[nodiscard]] QString writeSelection(const QStringList& terms, std::size_t originRank,
                                     const MemberChain& chain);

/// How many chains a type is opened out into before the listing stops.
///
/// A compound of a thousand members is a shape real files have, and a list of a
/// thousand entries is not a way to choose anything. The cap is the same
/// argument the Information tab's member listing makes.
inline constexpr int kMaxMemberChains = 200;

/// Every chain a type offers, in file order, depth first.
///
/// `.position` and `.position.x` are both in it: selecting an intermediate
/// compound is a selection like any other, and a reader looking for `x` should
/// find it under the name the file gave it rather than having to know it is
/// there. What is *not* in it is a subscript -- `.samples` with a `2` on the
/// slice line is the same selection as `.samples[2]`, and the line is where
/// every subscript in this program lives.
///
/// A chain stops at a vlen, because a chain cannot go on through one; and a
/// chain never enters an array's elements, because an array's dimensions are
/// axes rather than names.
///
/// Arithmetic over a `TypeInfo`, so it costs no read -- which is what lets the
/// list be built for a dropdown on selection and, later, offered per keystroke.
[[nodiscard]] QStringList memberChains(const h5core::TypeInfo& type,
                                       int limit = kMaxMemberChains);

} // namespace postproc
