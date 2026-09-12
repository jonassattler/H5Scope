// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Does the picture tell the truth?
//
// The performance half of this bake-off is easy to run and easy to argue
// about; this half is the one that decides it. A renderer that draws a
// hundred thousand points in a tenth of a millisecond and loses the one sample
// that mattered is not fast, it is wrong quickly -- and the way a reader finds
// out is by trusting a plot that had already thrown the interesting part away.
//
// Eight checks, each asserting a property of the pixels rather than comparing
// against a reference image. A pixel-exact comparison would fail all three
// spikes for differences in antialiasing and line joins that no reader cares
// about, and would say nothing about whether any of them was honest.
//
//   spike      one sample at ten times the amplitude, in a million. Stride
//              thinning loses it. It is the check the *application* fails
//              today, and it is not a property of any library here.
//   gap        a run of non-finite values must be a gap. Dropping the points
//              and handing the rest to a line renderer draws a straight line
//              across the missing data, which is worse than drawing nothing:
//              it invents a measurement.
//   ends       the first and last sample reach the edges of the pane.
//   bigx       x near 1.7e9 at a millisecond step. A float32 vertex written
//              without subtracting an origin first turns it into a staircase.
//   range      six decades either side of one, on a linear axis.
//   lines      a thousand and twenty-four lines drawn as a thousand and
//              twenty-four lines, not silently capped.
//   logy       can the y axis be logarithmic at all.
//   dpr        the grab comes back at device resolution rather than upscaled.
//
// Each writes a PNG beside its row, because a table saying "fail" is an
// accusation and the image is the evidence.

#include "common/PlotBench.hpp"
#include "common/SpikeRunner.hpp"

#include <QtCore/QString>

#include <string>
#include <vector>

namespace spike {

struct Finding {
    std::string renderer;
    std::string check;
    /// "pass", "fail" or "absent" -- the last for a capability the renderer
    /// does not claim, which is not the same as one it claims and gets wrong.
    std::string verdict;
    /// The number the verdict was reached on, so a threshold can be argued
    /// with rather than taken on faith.
    double measure = 0.0;
    std::string detail;
    std::string image;
};

/// Run every check against one surface. Writes the images into
/// `options.out` and returns the findings.
[[nodiscard]] std::vector<Finding> verify(Surface& surface, const Options& options);

/// Print the findings, and append them to `<out>/verify.tsv`.
void report(const std::vector<Finding>& findings, const QString& out);

} // namespace spike
