// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "h5core/DataSource.hpp"
#include "postproc/Array.hpp"
#include "postproc/Operations.hpp"

#include <hdf5.h>

#include <QString>

#include <cstddef>
#include <vector>

namespace postproc {

/// How many elements a pipeline will read into memory before it refuses.
///
/// Everything below the panel streams: the table reads the 64x64 block it is
/// about to paint and the plot reads one line at a time, which is why this
/// application opens a dataset of 10^9 elements without noticing. A transpose
/// or a reduction cannot be done that way -- an axis is not a block -- so the
/// selection is materialised, and a cap is the difference between a refusal and
/// a swap storm.
///
/// 2^24 doubles is 128 MB, which is roughly the biggest thing a reader can
/// usefully look at anyway: a 4096 x 4096 image is a quarter of it.
inline constexpr hsize_t kMaxElements = 1u << 24u;

/// The shape after each step, and where the pipeline stopped.
///
/// Pure arithmetic over the shapes -- no element is read -- so the panel prints
/// a shape against every row while the reader is still typing, on a dataset far
/// too large to run.
struct Trace {
    /// One entry per step asked for, in order. A step after a failure gets an
    /// entry with no shape and no error: it did not run and it is not the
    /// reason.
    std::vector<ShapeResult> stages;
    /// The shape the views would draw: the last one that resolved.
    std::vector<hsize_t> output;
    /// How many steps ran. Less than `stages.size()` when one of them refused.
    std::size_t ran = 0;
    /// Why it stopped, or empty when it did not.
    QString error;

    [[nodiscard]] bool ok() const { return error.isEmpty(); }
};

/// Walk `steps` from an input of `shape`, running at most `upTo` of them.
///
/// `steps[0]` is the slice -- the same slice the bar above the table holds,
/// which is why the pipeline has one it did not add and cannot remove. On a
/// scalar there is nothing to subscript, so that first step is passed over
/// rather than refused.
///
/// A step that cannot run stops the walk. Nothing after it is attempted and
/// nothing goes blank: `output` is the shape at the last step that worked,
/// which is the same thing clicking a row asks for.
[[nodiscard]] Trace trace(const std::vector<hsize_t>& shape,
                          const std::vector<Step>& steps,
                          std::size_t upTo);

/// What running a pipeline produced.
///
/// `ran` is what separates a pipeline that failed from one that stopped: a
/// step that refuses leaves everything above it computed and real, and that is
/// what the views draw while the reason is printed beside the row that gave
/// it. Only `ran == 0` means there is nothing to show.
struct RunResult {
    Array array;
    std::size_t ran = 0; ///< steps that ran; 1 is the slice by itself
    QString error;       ///< why it stopped, or empty when it did not

    [[nodiscard]] bool usable() const { return ran > 0; }
};

/// The same walk, against the elements.
///
/// Reads the selection `steps[0]` names straight out of `source` -- as
/// hyperslabs, so a contiguous slice is one read and a strided one is a read
/// per run -- and then applies the rest in memory. Refuses above kMaxElements.
[[nodiscard]] RunResult run(const h5core::DataSource& source,
                            const std::vector<Step>& steps,
                            std::size_t upTo);

/// Read exactly the elements `indices` names, dropping the dimensions `drop`
/// marks. Exposed for the tests, which read a selection without a pipeline
/// around it.
[[nodiscard]] ArrayResult read(const h5core::DataSource& source,
                               const std::vector<std::vector<hsize_t>>& indices,
                               const std::vector<bool>& drop);

} // namespace postproc
