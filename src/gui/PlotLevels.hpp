// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// How a line is folded into buckets, and how a fold is folded again.
//
// The arithmetic both plots reduce a line with, in one place because there were
// two of it. DatasetTableModel::sampleFrom walked a read buffer and took the
// extremes of each bucket out of it; CustomPlot::readLine did the same thing
// from its own loop over its own reads, and the two agreed about the values
// only because tests/test_customplot.cpp compared them element for element on
// every change. Agreement asserted after the fact is agreement that has to be
// re-asserted after the next change; this is the same arithmetic called twice
// instead.
//
// Everything here is doubles and a vector. No Qt, no HDF5, no window -- so
// tests/test_plotlevels.cpp asserts all of it with nothing open, which is the
// arrangement PlotProjection.hpp already argues for.
//
// One property of an envelope is load-bearing enough to state on its own, and
// coarsenEnvelope() below is built entirely out of it:
//
//   **An envelope can be coarsened exactly, and only coarsened.**
//
// The smallest and the largest of a run are the smallest and the largest of
// the smallests and largests of its parts, so merging adjacent buckets loses
// nothing at all. Splitting one does not work the other way: a bucket's two
// extremes say nothing about which half of it they came from. That asymmetry
// is the whole shape of the cache above -- read at the finest bucket the budget
// affords, and derive every coarser view from it rather than reading again.

#include "gui/PlotProjection.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace gui {

/// Most elements one read of a line pulls in one hyperslab.
///
/// The walk reads the elements it thins away rather than seeking past them --
/// contiguous is cheap, seeking is not -- and this is the ceiling on what that
/// buffers. It is also the whole cost model of the layer above: a run of `span`
/// elements costs `ceil(span / kReadRun)` reads **whatever bucket it is folded
/// into**, because the folding happens in memory over a buffer that was read
/// anyway. Resolution is free; span is what costs.
///
/// It lives here rather than in DatasetTableModel, which is where it was
/// written, because both plots now read by it and only one of them could reach
/// a private constant of a table model.
inline constexpr long long kReadRun = 1 << 16;

/// The two extremes of a run, and where each of them occurred.
///
/// Lifted out of DatasetTableModel.cpp, where it was a local struct, without
/// changing a line of the arithmetic.
struct Extremes
{
    double lowest = 0.0;
    double highest = 0.0;
    long long lowAt = -1;
    long long highAt = -1;

    /// Whether anything in the run was finite. A run of NaN has no extremes,
    /// which is a gap, which is what it is.
    [[nodiscard]] bool found() const { return lowAt >= 0; }

    /// The two, **in the order they occurred**. A bucket of two elements *is*
    /// its two elements, and emitting them smallest-first would turn every
    /// descending pair in the line the other way up.
    [[nodiscard]] double first() const { return lowAt <= highAt ? lowest : highest; }
    [[nodiscard]] double second() const { return lowAt <= highAt ? highest : lowest; }
};

/// The extremes of `values[from, to)`, skipping whatever is not finite.
[[nodiscard]] Extremes extremesOf(const double* values, long long from, long long to);

/// A read length cut back to a whole number of buckets, so that no read ever
/// stops inside one.
///
/// Without it the walk is still correct -- a bucket a read stopped inside is
/// left for the next one -- but the elements between the bucket's start and
/// that stopping point are then read twice, and "an envelope reads every
/// element exactly once" is a count tests/test_cost.cpp holds both plots to.
/// A bucket wider than a whole read is the one case that cannot be cut back.
[[nodiscard]] long long wholeBuckets(long long length, long long bucket);

/// Fold `count` values into `ceil(count / bucket)` pairs, appended to `out`.
///
/// Two values per bucket, in the order they occurred, and a pair of NaN for a
/// bucket with nothing finite in it. A bucket of one element yields that
/// element twice, which is what the renderer expects of a run it can draw
/// sample for sample.
void reduceBuckets(const double* values, long long count, long long bucket,
                   std::vector<double>& out);

/// Merge every `factor` buckets of an existing envelope into one, appending
/// `ceil(buckets / factor)` pairs to `out`.
///
/// `pairs` is `2 * buckets` doubles as reduceBuckets() wrote them. The result
/// is **exactly** what reduceBuckets() would have produced from the original
/// elements at `factor` times the bucket -- not an approximation of it -- and
/// that is the claim the whole cache rests on, so it is worth writing down why.
///
/// An extreme of a union is an extreme of the extremes, so the values are
/// right. The *order* is right for a less obvious reason: every element of
/// bucket k precedes every element of bucket k+1, and each pair is already in
/// occurrence order, so the pair buffer is itself a sequence in occurrence
/// order. Folding it with a bucket of `2 * factor` therefore asks exactly the
/// question reduceBuckets() asks of the elements -- which is why this is one
/// line rather than a second implementation to keep in step.
void coarsenEnvelope(const double* pairs, long long buckets, long long factor,
                     std::vector<double>& out);

// ---------------------------------------------------------------------------
// Which runs to hold, which to draw from, and which to read next
// ---------------------------------------------------------------------------
//
// The rest of this file is the policy over a set of runs, and it is here for
// the reason the reduction above is: there were two of it. DatasetPlot had
// detailFor/paneBuckets/detailBuckets/heldLevels/drawnLevel/levelServes/
// served/detailWanted/trimLevels, CustomPlot had closerFor/bucketBudget/
// closerBuckets/heldLevels/drawnLevel/closerSuffices/served/closerWanted/
// trimLevels, and the two were the same algorithms written out twice with the
// same nine constants copied between them. The headers said so in prose -- "the
// plot tab's numbers, for the plot tab's reasons" -- which is a comment where a
// shared symbol belongs, and it is why a change to the zoom had to be made
// twice or be wrong in one of the two tabs.
//
// Free functions over plain structs rather than a class the models own, on
// PlotProjection's argument: this is arithmetic, and arithmetic with no state
// of its own can be asserted with nothing open. The runs themselves stay where
// they were -- the *values* are keyed differently in the two models and are
// borrowed by the renderer on terms only their owner knows -- so what crosses
// this boundary is the windows and nothing else.

/// One run in hand, as the policy sees it.
struct HeldLevel
{
    PlotWindow window;

    /// Whether this run holds everything its owner would need to draw from it.
    ///
    /// The one question the two models answer differently, which is why they
    /// answer it rather than this file. DatasetPlot holds one run for the whole
    /// drawn set, so a run read before a line was ticked back on is a run it
    /// cannot draw; CustomPlot holds a run per entry, where the only question is
    /// whether it came back at all.
    bool complete = false;
};

/// What the reader is looking at, in the line's own element positions.
struct LevelView
{
    /// Elements in the line the runs are runs of.
    long long length = 0;
    /// The visible range. Clamped to the data by the functions below, because a
    /// pane showing the end of a line shows some empty axis past it and a run
    /// reaching the last element covers everything there is to draw out there.
    double low = 0.0;
    double high = 0.0;
    /// Buckets the pane itself needs, which is a bucket per column.
    int paneBuckets = 0;
    /// ...and buckets the run the reader is *on* is read into, which is finer
    /// while the budget affords it. See DatasetPlot::detailBuckets.
    int detailBuckets = 0;
    /// Octaves read out ahead of the reader.
    int prefetchOctaves = 2;

    [[nodiscard]] bool usable() const { return length > 0 && paneBuckets > 0 && high > low; }
};

/// Where the reader is zooming, when they are zooming.
///
/// The pointer's position is the one thing the surface always knew and never
/// said: a wheel event carries where it happened, the axis arithmetic uses it
/// to decide what stays still under the pointer, and then only the resulting
/// range crossed into the model. So the runs read ahead of a zoom were centred
/// on the *view*, and a reader zooming into one corner of the pane walked off
/// them after a step or two and waited for the file each time.
struct PlotFocus
{
    /// Where the pointer is, in the line's own element positions.
    double position = 0.0;
    /// Whether the reader is zooming in. A factor above one is in.
    bool inward = true;
    /// Whether there is a focus at all. A pan, a resize or a keyboard step
    /// leaves this false, and everything below falls back to the view's own
    /// centre -- which is what it did before there was a focus to have.
    bool active = false;
};

/// Runs held at once, at most.
///
/// The run the pane is on, the ones read ahead of it in both directions, and
/// the ones the reader has already been through -- because a run is not thrown
/// away when the view leaves it, so zooming back along the way you came costs
/// nothing at all.
///
/// It was five, and five was not a judgement about zooming: it was what sixteen
/// megabytes could hold. A reader who zoomed in six octaves had evicted the way
/// back before they got there and paid for all of it again on the way out. The
/// real bound is memory, and memory is now gui::PlotBudget's to answer -- see
/// each plot's heldLevels(), which divides the share by what one run costs.
/// This is only the ceiling past which another octave is answering a question
/// nobody asks: sixteen of them spans more zoom than any dataset has.
inline constexpr int kHeldLevels = 16;

/// How many octaves in are read towards the focus before the reader asks.
///
/// Four, and it is nearly free: a run costs its span, and each octave in is
/// half the span of the one above it, so four of them together cost less than
/// the *single* octave out that kPrefetchOctaves already reads. That asymmetry
/// is the whole reason this number can be four and that one cannot.
inline constexpr int kFocusOctavesIn = 4;

// --- the numbers both plots work to ----------------------------------------
//
// One definition each, because there were two. Every one of these was written
// out twice with a comment beside the copy saying it had to match -- "the plot
// tab's numbers, for the plot tab's reasons" -- and a comment is not what keeps
// two numbers equal. A reader who puts /plotting/adc_10M on the Plot tab and
// the same slice in a custom tab is looking at one dataset, and the two
// pictures of it differing in any way they can see is a bug in whichever of
// them they are not looking at.

/// Columns assumed until the surface has measured itself. Two of these -- a
/// bucket answers with a low and a high -- is the two thousand points a line
/// used to be thinned to unconditionally.
inline constexpr int kDefaultColumns = 1024;

/// The most points a line is ever thinned to, however wide the pane.
///
/// Eight thousand buckets is a pane eight thousand *device* pixels across --
/// see PlotSurface.pushColumns, which measures in those rather than in logical
/// ones -- so it covers a maximised window on a 5K display and a 4K one at
/// double scaling. Past that there is nothing left to resolve for: a bucket
/// would be narrower than a pixel.
inline constexpr int kMaxPoints = 16384;

/// ...and the fewest, however many lines are sharing the budget. Below this a
/// line stops being a shape and starts being a sketch of one, and nothing is
/// saved that was worth the difference.
inline constexpr int kMinPoints = 256;

/// What the pane's width is rounded down to.
///
/// Down rather than up, and this is not arbitrary: the renderer summarises
/// again if it is handed more than two points per column, and it does so in
/// powers of two -- so a cap a hair above twice the pane costs *half* the
/// horizontal resolution rather than none of it. Staying under the pane keeps
/// the two in step, and a resize then re-reads once every sixty-four pixels
/// rather than once a pixel.
inline constexpr int kColumnQuantum = 64;

/// How long the view has to hold still before a closer look is read.
///
/// Long enough that a wheel spun through six octaves reads once rather than
/// six times, and short enough that it lands before a reader who has stopped to
/// look at something has finished looking at it. The picture does not wait for
/// it: what is already on screen keeps being drawn.
inline constexpr int kSettleMilliseconds = 150;

/// How long the pane has to hold still before it is re-thinned.
///
/// Longer than kSettleMilliseconds, because a window resize is a slower gesture
/// than a wheel spin and the cost at the end of it is higher: a zoom re-reads
/// one run of each line, a resize re-reads all of every one.
inline constexpr int kResizeMilliseconds = 200;

/// How far out a run is read before the reader has asked for it.
///
/// The two directions of a zoom are not the same shape, and this is the one
/// that needs a read. Going *in* is free: a run costs its span, so the run the
/// reader is on is read an octave finer than the pane needs and the step down
/// lands on detail that came with it. Going *out* cannot be free that way,
/// because the next view is wider than the run in hand and no amount of
/// resolution inside it helps; what is needed is another run, twice as wide at
/// twice the bucket. That one costs twice the elements, which is why it is read
/// while the reader is looking rather than while they are waiting.
///
/// Two of them, so a double tap's worth of zooming out is already drawn. Each
/// costs one level of memory and its own span in reads; past two, the spans
/// double again and the whole-line summary is close enough to be the honest
/// answer. Compare kFocusOctavesIn, which can afford to be twice as deep
/// because it is going the cheap way.
inline constexpr int kPrefetchOctaves = 2;

/// The finest run in hand that covers what is on screen and can be drawn from,
/// or -1.
///
/// The whole-line summary covers everything by construction and is what is
/// drawn when this finds nothing, which is why zooming past the runs held is a
/// correct picture immediately rather than half a line for a moment.
[[nodiscard]] int drawnLevel(std::span<const HeldLevel> held, const LevelView& view);

/// Whether some run in hand covers `low`..`high` at a bucket no coarser than
/// `bucket`, and can be drawn from.
///
/// The question the prefetch asks about an octave it is thinking of reading: a
/// run the reader zoomed *in* from is finer than the octave out of where they
/// are now and covers more of the line, so it answers for that octave and
/// reading it again would be a round trip spent on nothing.
[[nodiscard]] bool served(std::span<const HeldLevel> held, const LevelView& view, double low,
                          double high, long long bucket);

/// The run to read next: the one the pane is waiting for, then the octaves in
/// towards the focus, then the octaves out. Nothing when everything worth
/// holding is held.
[[nodiscard]] std::optional<PlotWindow> wantedLevel(std::span<const HeldLevel> held,
                                                    const LevelView& view, const PlotFocus& focus);

/// Which run to give up first when there are more than there is room for.
///
/// Octaves away from the bucket the pane is asking for, so the runs nearest the
/// reader are kept -- they are the ones about to be wanted, and the furthest
/// away are the ones the whole-line summary is closest to standing in for.
///
/// A focus changes that in one way, and it is the way the reader notices. Runs
/// finer than the one on screen are speculation about where the zoom was going;
/// once the pointer has moved somewhere they do not cover, they are speculation
/// about somewhere the reader is not, and they go before anything that is still
/// on the path. Coarser runs are not touched by this -- they are the way back
/// out, and the way back out is where the reader came from.
[[nodiscard]] std::size_t coldestLevel(std::span<const HeldLevel> held, const LevelView& view,
                                       const PlotFocus& focus);

} // namespace gui
