// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "h5core/Types.hpp"
#include "postproc/MemberPath.hpp"
#include "postproc/Operations.hpp"
#include "postproc/Pipeline.hpp"

#include <hdf5.h>

#include <QString>
#include <QStringList>

#include <vector>

namespace postproc {

/// A pipeline written out as text, and read back.
///
///     /group/dataset
///     .select(samples)
///     .slice(1, :)
///     .max(1)
///     .transpose
///     .reshape(2, 10)
///
/// The rows of the postprocessing panel say the same thing one control at a
/// time, and that is right for building a pipeline and wrong for reading or
/// pasting one: a chain of seven steps is seven rows of boxes to look along,
/// where it is seven short lines to read down. So the panel offers both and
/// they are two views of one thing -- a `Script` is exactly what the rows hold,
/// the path, the member, the slice and the steps, and each step's argument is
/// the very text its row's box holds. Nothing is lost in either direction,
/// which is what lets the two be mirrored rather than kept in step. A custom
/// plot's line is written in the same form, and is read by the same function.
///
/// **Where the path ends** is the one question this grammar has to answer that
/// the others did not. A link name can hold a `.` and a `(` as freely as it can
/// hold a `[` -- `splitExpression` has the same problem and the same kind of
/// answer -- so the path runs to the end of the first line, or, when everything
/// is written on one line, to the first `.name(` whose `name` is an operation.
/// After the path there is nothing but operations, and there the parentheses
/// are optional. The price is that a link whose name holds `.max(` cannot be
/// written on one line with its pipeline; on a line of its own it can.
///
/// Two of the words are not operations, because the rows they stand for are
/// not either:
///
/// - `.select(chain)` names the member of a compound everything after it reads,
///   which is the panel's Select row. It comes first, straight after the path,
///   because after a transpose or a reduction there is no struct left to select
///   from.
/// - The first `.slice(...)` -- straight after the path or the select -- is the
///   slice every pipeline begins with: the slice bar's, the panel's second row.
///   A `.slice` written anywhere later is an ordinary step, as a Slice added
///   from the dropdown is. With none written, the whole of the dataset is read.
struct Script {
    QString path;
    /// The member chain as the slice bar writes it, `.samples` or
    /// `.position.x`, subscripts and all. Empty for the dataset itself.
    QString member;
    /// Whether a leading slice was written, and what it said.
    bool sliced = false;
    QString slice;
    /// Everything after the leading slice, in order.
    std::vector<Step> steps;
    /// Why the text does not read as a script, or empty.
    QString error;

    [[nodiscard]] bool ok() const { return error.isEmpty(); }
    [[nodiscard]] bool operator==(const Script&) const = default;
};

/// Read a script. Pure text: nothing is resolved against a file, so a script
/// naming a dataset that does not exist reads perfectly well, and says so only
/// when it is checked against what the path turns out to be.
[[nodiscard]] Script parseScript(const QString& text);

/// How a script is written out.
enum class ScriptLayout {
    /// One step to a line, the path on the first: the form in the boxes, and
    /// what Return formats whatever was typed into. A step with no argument is
    /// written bare, `.transpose`, because that is how it is read aloud.
    Lines,
    /// All of it on one line, for a legend -- a label that breaks across lines
    /// is not a label. Every step keeps its parentheses here, because on one
    /// line they are what says where the path stops.
    OneLine,
};

[[nodiscard]] QString writeScript(const Script& script,
                                  ScriptLayout layout = ScriptLayout::Lines);

/// The pipeline a script runs: its slice first, then its steps.
///
/// A slice is over the whole derived shape -- the dataset's axes and then the
/// ones the chain appends -- exactly as the panel's slice row is. When the
/// chain carries subscripts of its own, the slice is built through
/// `sliceLineFor` instead, which is what gives a script the identity the whole
/// member notation rests on: `.select(samples[2])` with `.slice(0:5)` and
/// `.select(samples)` with `.slice(0:5, 2)` are the same selection, because a
/// subscript written on the chain binds to the axes the chain appends and to
/// nothing else. `folded` is the chain's own subscripts, from
/// resolveMemberChain; `originRank` is the dataset's rank without them.
[[nodiscard]] std::vector<Step> pipelineOf(const Script& script,
                                           const QStringList& folded,
                                           std::size_t originRank);

/// What a script would do to a dataset of this shape and type, worked out
/// without reading an element of it.
struct ScriptCheck {
    /// The chain, resolved: what it selects and the axes it appends.
    MemberChain chain;
    /// The pipeline, ready to run -- pipelineOf with the chain's subscripts.
    std::vector<Step> pipeline;
    /// The shape the slice sees: the dataset's own, with the chain's axes.
    std::vector<hsize_t> input;
    /// The shape after the last step that resolved.
    std::vector<hsize_t> output;
    /// Why it stops, prefixed with the step that stopped it, or empty.
    QString error;

    [[nodiscard]] bool ok() const { return error.isEmpty(); }
};

/// Resolve the member, check there are numbers under it, and walk the shapes.
/// What the panel's box, a custom plot's box and a custom plot's read all ask,
/// so the three cannot disagree about whether a script can run.
[[nodiscard]] ScriptCheck checkScript(const Script& script,
                                      const std::vector<hsize_t>& originShape,
                                      const h5core::TypeInfo& type);

/// How one step is written in a script: `.max(1)`, `.transpose`. For the
/// messages that say which step something is wrong with.
[[nodiscard]] QString writeStep(const Step& step);

} // namespace postproc
