// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <hdf5.h>

#include <QString>
#include <QStringList>

#include <vector>

namespace postproc {

/// The indices an expression selects, or why it could not be read.
struct IndexExpression {
    /// In the order the expression names them. Not sorted: `9:4:-1` counts
    /// down and `[3,0]` puts 3 first, and both of those are selections a
    /// reader can ask for in Python, so both are selections this table can
    /// show. Duplicates are dropped, because a table cannot show one element
    /// in two rows.
    std::vector<hsize_t> indices;
    QString error; ///< empty when the text parsed

    /// How the subscript was *written*, as opposed to what it resolved to.
    ///
    /// The slice line prints itself back from this, which is the whole of the
    /// fix for a line that could not hold what was typed into it: `1:2` and
    /// `1` select the same one element, and in Python they are still not the
    /// same subscript -- one keeps the dimension and the other drops it. The
    /// line used to be rebuilt from the resolved indices, so `1:2` came back
    /// as `1` and a reader watched the box rewrite what they had just written.
    enum class Form {
        Whole,     ///< ":" -- the whole dimension, in order
        Single,    ///< one bare index
        Span,      ///< "a:b" at step 1 -- a contiguous ascending run
        Scattered, ///< anything else: a stride, a descent, a list
    };
    Form form = Form::Scattered;
    hsize_t first = 0; ///< Single: the index. Span: its inclusive lower bound.
    hsize_t last = 0;  ///< Span: its inclusive upper bound.

    [[nodiscard]] bool valid() const { return error.isEmpty(); }
};

/// Parse one subscript against a dimension of `extent` elements.
///
/// The grammar is Python's, because that is the notation a reader of HDF5 files
/// already has in their fingers -- h5py is how most of them open one:
///
///     expression := term ("," term)*
///     term       := index | slice
///     index      := ["-"] digits
///     slice      := [bound] ":" [bound] [":" [step]]
///     bound      := ["-"] digits | "start" | "end"
///     step       := ["-"] digits
///
/// and it means what Python means by it:
///
///   * **The upper bound is exclusive.** With extent 10, `0:4` is 0 1 2 3.
///   * **A negative index counts from the end.** `-1` is the last element,
///     `-3:` the last three, `:-1` everything but the last.
///   * **Bounds are clamped, not refused.** `0:100` on a dimension of ten is
///     the whole of it, exactly as it is in Python -- where a bare index of 10
///     is still an error, because that one names an element that is not there.
///   * **A step may be given, and may be negative.** `::2` is every other
///     element and `::-1` is the whole dimension backwards, in that order:
///     these indices are used in the order they come out.
///   * A missing bound is the beginning or the end, and `start` and `end` are
///     words for the same two things.
///
/// Several terms may be listed with commas, as numpy's fancy indexing does:
/// `0,2,5:9`. A whole expression may be wrapped in brackets, which is how the
/// slice line writes a multi-term selection (`[0:4,5,7:10]`), so that line
/// pastes straight back in.
///
/// An expression that parses but selects nothing (`3:3`) is reported as an
/// error rather than as an empty selection: an empty axis would silently blank
/// the grid, where a highlighted box says what actually happened. That is the
/// one place this deliberately parts company with Python, which is happy to
/// hand back an empty array.
[[nodiscard]] IndexExpression parseIndexExpression(const QString& text, hsize_t extent);

/// Read a whole slice line -- one subscript per dimension, comma-separated --
/// against a dataset of `shape`.
///
/// This is the line the bar above the table holds and the line a Slice step in
/// the postprocessing pipeline holds, which are the same line and had better
/// stay the same line. It reads the whole of it before reporting anything: a
/// slice is one statement about every dimension at once, so a subscript that
/// does not parse leaves the caller's selection exactly as it was, and the
/// reason says which dimension and why.
///
/// Two of Python's shorthands come with the grammar. **Trailing dimensions may
/// be left off** -- `0` on a rank-4 dataset is `0, :, :, :` -- and **`...`
/// stands for the dimensions nobody wrote**, of which there may be only one,
/// because two would not say how many each was covering.
///
/// `chosen` receives one entry per dimension and `written` the text each was
/// written as, which is how a Custom subscript prints back as `::2` rather
/// than as the five indices it stands for.
[[nodiscard]] bool readSubscripts(const QString& text,
                                  const std::vector<hsize_t>& shape,
                                  std::vector<IndexExpression>& chosen,
                                  QStringList& written, QString& error);

/// A subscript as the slice line has to print it: bracketed when it is a list
/// of several terms, bare when it is one.
///
/// Without the brackets "[:, 2, 0,3]" reads like four subscripts rather than
/// three, which is exactly the ambiguity numpy brackets its fancy indexing to
/// avoid. A single term needs none -- "::2" is unmistakably one subscript --
/// and keeping it bare is what lets a stride print back as what was typed.
[[nodiscard]] QString bracketedIfListed(const QString& text);

} // namespace postproc
