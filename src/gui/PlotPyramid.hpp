// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// A whole line, held once, at every resolution it will ever be drawn at.
//
// The plot used to answer a closer look by reading the file again: a run came
// back a round trip later, one at a time, and a reader zooming through a dozen
// octaves waited for a dozen reads on a thread that runs them one after
// another. That is a tenth of a second per notch on a ten-million-element
// dataset, and no amount of reading *ahead* fixes it, because the thing being
// waited for is the read.
//
// What makes it unnecessary is a property of the envelope, stated in
// PlotLevels.hpp and spent here:
//
//   **An envelope can be coarsened exactly, and only coarsened.**
//
// So one pass over a line -- the pass the whole-line summary already makes,
// which already touches every element and then throws all but two thousand of
// them away -- can keep a base at the finest bucket the budget affords and
// derive every coarser view from it in memory. After that pass, every run at
// every resolution is arithmetic over a buffer that is already in hand: a fold
// of a few thousand doubles, microseconds, and no file at all.
//
// The levels step by *four* rather than two. A frame folds from the finest
// level no coarser than the bucket it wants, so a factor-of-four ladder costs
// at most four input buckets per output bucket -- still O(pane columns), which
// is the number that makes this constant-time in the size of the dataset --
// while the levels above the base add only a third of the base's size instead
// of all of it.
//
// No Qt in this header, for PlotProjection.hpp's reason: it is arithmetic over
// doubles and tests/test_plotlevels.cpp asserts it with no file, no thread and
// no window. The .cpp uses QThreadPool for the build, which is the one part
// that is worth spreading over the machine.

#include "gui/PlotLevels.hpp"
#include "gui/PlotProjection.hpp"

#include <cstddef>
#include <span>
#include <vector>

namespace gui {

/// Octaves between one held level and the next.
///
/// Two, so each level is four times the bucket of the one below it. See the
/// header: it is the trade between what the ladder costs to hold and what one
/// frame costs to fold out of it, and both ends of that trade are comfortable
/// here -- a third of the base in memory, four input buckets per output bucket
/// in time.
inline constexpr int kPyramidOctaves = 2;

/// One resolution of a line.
struct PyramidLevel
{
    /// Elements per bucket. A power of two.
    long long bucket = 1;

    /// The line at this resolution.
    ///
    /// A bucket of one element is the element itself, so a base at bucket 1
    /// holds the raw values -- one double each, not two. That is not a special
    /// case bolted on: it is what DatasetTableModel::sampleFrom already does
    /// when the stride comes out at one, because an envelope of a single
    /// element would be that element written down twice. Everything coarser is
    /// a pair per bucket, its two extremes in the order they occurred.
    std::vector<double> values;

    [[nodiscard]] long long buckets() const
    {
        const auto size = static_cast<long long>(values.size());
        return bucket == 1 ? size : size / 2;
    }
};

/// A line at every resolution, finest first.
struct LinePyramid
{
    /// Elements in the line this is a pyramid of.
    long long length = 0;

    /// Finest first, each four times the bucket of the one before it. The last
    /// is the whole line in a handful of buckets.
    std::vector<PyramidLevel> levels;

    [[nodiscard]] bool empty() const { return levels.empty() || length <= 0; }

    /// The finest bucket held. Below this the file is the only answer.
    [[nodiscard]] long long baseBucket() const
    {
        return levels.empty() ? 0 : levels.front().bucket;
    }

    /// What it costs, in doubles, for the budget to weigh against.
    [[nodiscard]] std::size_t doubles() const;
};

/// Doubles a whole pyramid over `length` elements at base bucket `base` costs.
///
/// The base is `length` doubles when `base` is 1 and `2 * ceil(length / base)`
/// otherwise; the levels above it add a third as much again, because each is a
/// quarter of the one below and a quarter sums to a third.
[[nodiscard]] long long pyramidDoubles(long long length, long long base);

/// The finest base bucket whose whole pyramid fits `budget` doubles.
///
/// A power of two, at least one -- so a line the budget cannot hold at all
/// still gets a pyramid, just a coarse one, and the octaves below its base are
/// the only ones that cost a read. Those are also the cheapest reads there are:
/// below the base a run's span is at most a paneful of base buckets, which is a
/// single hyperslab however large the dataset.
[[nodiscard]] long long baseBucketFor(long long length, long long budget);

/// Build every level above `pyramid.levels.front()`, which the caller has
/// filled. Spread over the machine; the ranges of one level are disjoint.
void buildLevels(LinePyramid& pyramid);

/// Throw away every level finer than `base`, and say whether anything went.
///
/// The free direction of the budget, and the one that matters. A reader who
/// notices this program holding three gigabytes and turns the RAM budget down
/// is asking for the memory back *now*; coarsening is exact, so they get it in
/// the same call, with no read and no change to any picture drawn at or above
/// the new base. Going the other way is not arithmetic at all -- a finer base
/// is elements this no longer has -- so there is no refineTo() beside this and
/// there cannot be one.
///
/// At least one level always survives: a pyramid of the whole line in a handful
/// of buckets is still a pyramid, and the octaves below it are reads rather
/// than a blank pane.
bool coarsenTo(LinePyramid& pyramid, long long base);

/// A pyramid built a hyperslab at a time, while the elements are still warm.
///
/// The streaming form, and the one both plots read through: a line of a hundred
/// million elements is not a buffer anyone hands over whole, so it arrives in
/// reads of up to kReadRun and each of those is folded as it lands.
///
/// The base is filled as the reads land; the levels above it are built in
/// finish(), whole and spread over the machine. Folding them chunk by chunk as
/// well was tried -- a level made out of a few kilobytes still in cache ought to
/// beat one made out of a re-read of the whole base -- and it is half again as
/// slow. The fold is *compute* bound rather than memory bound, so what decides
/// it is how many cores are folding, and chunk by chunk that is one.
class PyramidBuilder
{
public:
    PyramidBuilder(long long length, long long base);

    /// Append `count` elements of the line, in order.
    ///
    /// Any length is accepted: what does not fill a whole base bucket is
    /// carried into the next call, because a bucket summarised from half of
    /// itself is simply wrong and the error would grow down the line.
    void add(const double* values, long long count);

    /// How many elements have been added.
    [[nodiscard]] long long taken() const { return taken_; }

    /// Close the base's last bucket, build every level above it, and hand the
    /// pyramid over.
    ///
    /// Its length is what was added, where that is less than the length it was
    /// built for: a line whose reads stopped part of the way is that much of a
    /// line, and never a promise of elements the base does not hold.
    [[nodiscard]] LinePyramid finish();

private:
    LinePyramid pyramid_;
    long long base_ = 1;
    long long taken_ = 0;
    /// Elements read but not yet folded into the base.
    std::vector<double> carry_;
};

/// A whole pyramid out of one contiguous buffer of raw elements.
///
/// The test path and the small-line path. The streaming build in
/// DatasetTableModel is the same arithmetic with the base filled a hyperslab at
/// a time, because a hundred-million-element line is not a buffer anyone hands
/// over whole.
[[nodiscard]] LinePyramid pyramidOf(const double* values, long long count, long long base);

/// Fold the run `window` names out of the pyramid into `out`.
///
/// `out` is left holding exactly what a read of that window would have
/// answered with: `window.columns` raw values when the bucket is one, and a
/// pair per bucket otherwise. False when the pyramid cannot answer -- it holds
/// nothing, or the window is finer than its base, or the window is not aligned
/// to a bucket the pyramid has. The caller reads the file for those.
[[nodiscard]] bool fillWindow(const LinePyramid& pyramid, const PlotWindow& window,
                              std::vector<double>& out);

/// Fold the whole line into about `buckets` buckets, as the whole-line summary.
///
/// `stride` and `points` come back as DatasetTableModel::NumericGrid would have
/// reported them, because this is what replaces that read. The stride is
/// `ceil(length / buckets)` rounded up to a multiple of the base bucket, which
/// is a distinction only a line too large to hold raw ever notices: with a base
/// of one every stride is a multiple of it and the answer is what the file
/// would have given, element for element.
[[nodiscard]] bool fillWhole(const LinePyramid& pyramid, int buckets, std::vector<double>& out,
                             long long& stride, double& step);

/// The extremes of elements `[first, last)`, out of whichever levels cover
/// that run in the fewest buckets.
///
/// Any run at all, not only an aligned one: it is cut into the largest aligned
/// buckets that fit -- a few from each level on the way up and the way back
/// down -- and an extreme of a union is an extreme of the extremes, so the
/// answer is exact. The ends are rounded out to the base bucket, because below
/// the base there is nothing left to cut with; with a base of one that rounds
/// nothing.
///
/// `lowAt` and `highAt` say where each extreme sat closely enough to put the
/// two in the order they occurred -- the element itself at the base, a bucket's
/// start or middle above it -- which is all Extremes::first() asks of them.
[[nodiscard]] Extremes extremesOver(const LinePyramid& pyramid, long long first, long long last);

/// The smallest value above zero anywhere in the line, into `out`; false when
/// there is none.
///
/// Where a logarithmic axis starts, and not a question a summary can answer.
/// An envelope keeps each bucket's smallest and largest, so a bucket holding a
/// zero and a thousandth answers with the zero and the thousandth is gone: a
/// time base from 0 in steps of a thousandth, summarised twenty to a bucket,
/// started its axis at two hundredths, and the first decade and a third of the
/// data were off the pane. Asked of the pyramid instead, top down: a bucket
/// whose smallest is above zero answers with it, one whose largest is not
/// holds nothing, and only the buckets that straddle zero are opened -- so a
/// line that crosses zero once costs a few dozen buckets, and one that crosses
/// it everywhere costs, at worst, the walk of its base.
///
/// Exact over a base of one. Above that the finest bucket that straddles zero
/// cannot be opened, and its largest value is the answer given for it: never
/// below the true one, so the axis loses no more than that bucket's worth.
[[nodiscard]] bool smallestPositive(const LinePyramid& pyramid, double& out);

/// A line folded onto columns that are not all the same width.
///
/// What a logarithmic x axis is drawn from: see LogColumns in PlotLevels.hpp.
/// A column narrow enough to hold one or two elements holds *them*, drawn at
/// their own positions, because an envelope of two elements is those two
/// elements with a claim of summary on them; anything wider is its extremes,
/// in the order they occurred, at its first element and half way along it --
/// half a bucket apart, as every envelope here is.
struct ColumnFold
{
    std::vector<double> values;
    /// The element position each of `values` stands at. Ascending.
    std::vector<double> positions;
    /// Whether any of them is an envelope rather than an element. See
    /// PlotLine::summarised.
    bool summarised = false;
};

/// Fold the line into the columns between consecutive `edges`, which are
/// element positions, ascending and not necessarily whole.
///
/// A column holds the elements `i` with `edges[c] <= i < edges[c + 1]`, so
/// every element falls in exactly one column and none is folded twice -- and a
/// column narrower than one element holds nothing and adds nothing. A column
/// with nothing finite in it is one NaN, which is a gap.
///
/// `out` is cleared first. Its size is at most twice the number of columns.
void foldColumns(const LinePyramid& pyramid, std::span<const double> edges, ColumnFold& out);

} // namespace gui
