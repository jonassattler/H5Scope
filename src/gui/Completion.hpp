// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QString>
#include <QStringList>

#include <cstddef>

namespace gui {

/// Which of the three grammars on one typed line the caret is in.
///
/// `/plotting/events[:, 0].position.x` is a path, then a subscript, then a
/// member chain, and what could be written next is a different question in each
/// of them. Deciding which once, here, is what keeps the answer to that
/// question from being three answers with an `if` in front of them.
struct CompletionRequest {
    enum class Part {
        Path,      ///< before any bracket: groups and datasets
        Subscript, ///< between the brackets: nothing to offer, see below
        Member,    ///< after the closing bracket: the datatype's members
    };

    Part part = Part::Path;
    /// Everything before the fragment, kept exactly as it was typed. A
    /// completion is this with a candidate written after it, which is why the
    /// lists this produces are whole lines rather than fragments: the thing
    /// being completed is three grammars deep and a fragment would have to be
    /// spliced back by whoever asked for it.
    QString head;
    /// What has been typed of the thing being completed. Empty is a question
    /// too -- it asks for everything that could go there.
    QString fragment;
    /// The dataset named, for a Member request. Empty otherwise.
    QString path;
    /// What stands between the brackets, for a Member request.
    QString subscript;
};

/// Read a typed line and say what is being completed.
///
/// Nothing here opens a file or knows what any path is; it is the grammar and
/// only the grammar, so it is asserted without one.
///
/// A subscript gets no completions at all, and that is deliberate rather than
/// unfinished: what may be written there is every integer, every range and
/// every combination of them, which is not a list. The thing a reader wants
/// help with -- the subscript that selects the whole of a dataset -- is written
/// for them when the path is completed instead.
[[nodiscard]] CompletionRequest completionRequest(const QString& text);

/// The subscript that selects the whole of a dataset of `rank`: `[:, :]`.
///
/// What Tab writes after a dataset's name, so that completing a path leaves a
/// line that reads rather than one the reader has to count dimensions for.
/// Empty for a scalar, which has nothing to subscript.
[[nodiscard]] QString wholeSubscript(std::size_t rank);

/// The longest head every candidate shares.
///
/// What Tab can write when there is more than one match: `/pl` against
/// `/plotting/` and `/plots/` becomes `/plot`, which is as far as the reader
/// can be taken without choosing for them. The one-candidate case falls out of
/// it -- the longest head of one string is the string.
[[nodiscard]] QString commonHead(const QStringList& candidates);

} // namespace gui
