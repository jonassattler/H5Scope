// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// The data the three spikes draw, in exactly the shape DatasetPlot hands to a
// renderer: an x origin, an x step, and one `std::vector<double>` per line.
//
// No HDF5 anywhere. What is being compared is three renderers, and a read path
// in the middle of the measurement would put the same number in all three
// columns while making every one of them noisier. The application's own read
// path is already measured, by tools/bench-data, in counts.
//
// Every shape below is here because some renderer or some thinning strategy
// gets it wrong, and the correctness suite names which:
//
//   Sine             the ordinary case, and the only one that proves nothing
//   Noise            worst case for decimation: no structure to throw away
//   SpikeInAMillion  one sample at ten times the amplitude. Stride sampling
//                    loses it; a min/max envelope keeps it. This is the shape
//                    that indicts the application as it stands.
//   NaNRun           a run of non-finite values. A gap is the truth; a line
//                    drawn straight across it, or down to zero, is a lie.
//   BigX             x near 1e9 with a 1e-3 step -- an epoch timestamp. The
//                    scene graph stores vertices as float32, so any GPU path
//                    that casts without subtracting an origin first draws a
//                    staircase instead of a line.
//   WideRange        y over 1e-30 .. 1e30 on a linear axis.
//   NonMonotonicX    x that goes backwards. A phase portrait is not sorted,
//                    and a renderer that assumes sorted x either sorts it
//                    (wrong) or draws it (right).
//   Constant         every value identical, so the y extent is zero. The
//                    axis arithmetic divides by that span somewhere.

#include <cstdint>
#include <string>
#include <vector>

namespace spike {

enum class Shape {
    Sine,
    Noise,
    SpikeInAMillion,
    NaNRun,
    BigX,
    WideRange,
    NonMonotonicX,
    Constant,
};

/// The spelling used on the command line and in the results tables.
const char* name(Shape shape);

/// Parse one of those spellings. Returns false and leaves `out` alone if the
/// name is not one of them -- a misspelled shape must not silently become the
/// sine, which is the one shape that would pass everything.
bool parseShape(const std::string& text, Shape& out);

/// Every shape, in the order the correctness suite reports them.
const std::vector<Shape>& everyShape();

class SyntheticSource
{
public:
    /// Build `seriesCount` lines of `pointsPerSeries` values each.
    ///
    /// Deterministic: the seed is the only source of variation, so two spikes
    /// given the same arguments draw the same picture and a difference between
    /// their images is a difference between renderers.
    SyntheticSource(Shape shape, int seriesCount, int pointsPerSeries,
                    std::uint32_t seed = 7);

    [[nodiscard]] Shape shape() const { return shape_; }
    [[nodiscard]] int seriesCount() const { return seriesCount_; }
    [[nodiscard]] int pointCount() const { return pointsPerSeries_; }

    /// The values of one line. Never empty for a series in range.
    [[nodiscard]] const std::vector<double>& line(int series) const;

    /// x as an origin and a step, which is what DatasetPlot carries and what
    /// every renderer here is given unless hasExplicitX().
    [[nodiscard]] double xStart() const { return xStart_; }
    [[nodiscard]] double xStep() const { return xStep_; }

    /// NonMonotonicX is the one shape whose x cannot be expressed as a step,
    /// so it carries its own column. Shared by every series.
    [[nodiscard]] bool hasExplicitX() const { return !xValues_.empty(); }
    [[nodiscard]] const std::vector<double>& xValues() const { return xValues_; }

    /// The extent of every finite value across every line. The y axis the
    /// application would choose, computed once here so three spikes cannot
    /// disagree about it and be compared anyway.
    [[nodiscard]] double minimum() const { return minimum_; }
    [[nodiscard]] double maximum() const { return maximum_; }

    /// Bytes of `double` held. The honest denominator for the rss column: a
    /// renderer's overhead is what it costs *beyond* the data it was given.
    [[nodiscard]] std::size_t bytes() const;

    /// Total points across every line, for the grid's budget arithmetic.
    [[nodiscard]] std::int64_t totalPoints() const
    {
        return static_cast<std::int64_t>(seriesCount_)
               * static_cast<std::int64_t>(pointsPerSeries_);
    }

    /// Where SpikeInAMillion put the spike, as an index into the line. The
    /// correctness suite needs to know where to look; recomputing the rule in
    /// two places is how the check comes to agree with itself and not with the
    /// data.
    [[nodiscard]] int spikeIndex() const { return spikeIndex_; }

    /// The half-open range of indices NaNRun blanked, likewise.
    [[nodiscard]] int gapFirst() const { return gapFirst_; }
    [[nodiscard]] int gapLast() const { return gapLast_; }

private:
    Shape shape_;
    int seriesCount_;
    int pointsPerSeries_;
    double xStart_ = 0.0;
    double xStep_ = 1.0;
    double minimum_ = 0.0;
    double maximum_ = 0.0;
    int spikeIndex_ = -1;
    int gapFirst_ = -1;
    int gapLast_ = -1;
    std::vector<std::vector<double>> lines_;
    std::vector<double> xValues_;
};

} // namespace spike
