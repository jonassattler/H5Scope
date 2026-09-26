// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "gui/PlotLevels.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace gui {

namespace {

/// The careful reading: every element tested for being drawable before it is
/// compared. Correct for anything, and the slow path of the one below.
[[nodiscard]] Extremes carefulExtremes(const double* values, long long from, long long to)
{
    Extremes found;
    for (long long i = from; i < to; ++i) {
        const double value = values[i];
        if (!std::isfinite(value)) {
            continue;
        }
        if (found.lowAt < 0 || value < found.lowest) {
            found.lowest = value;
            found.lowAt = i;
        }
        if (found.highAt < 0 || value > found.highest) {
            found.highest = value;
            found.highAt = i;
        }
    }
    return found;
}

} // namespace

Extremes extremesOf(const double* values, long long from, long long to)
{
    Extremes found;
    if (values == nullptr) {
        return found;
    }

    // Two comparisons an element and nothing else in the loop.
    //
    // This is the innermost loop of the whole plot: it reads every element of
    // a line on the way to the summary and folds every level of the pyramid
    // above it, which together are about half of what a ten-million-element
    // dataset costs between being clicked on and being drawn. The version this
    // replaces asked three further questions per element -- isfinite(), and
    // whether either extreme had been seen yet -- and only the *first* element
    // of a run ever answers the last two differently.
    //
    // What lets all three go: **NaN fails every comparison**. Seeded with the
    // infinities, the first drawable element takes both branches and a NaN
    // takes neither, so being drawable needs no test of its own. An actual
    // infinity in the data does pass one of them, and that is the one case this
    // cannot decide -- so it hands the run to carefulExtremes() instead. Which
    // is rare: an infinity in a dataset is unusual, a NaN is not, and the two
    // used to cost the same.
    double lowest = std::numeric_limits<double>::infinity();
    double highest = -std::numeric_limits<double>::infinity();
    long long lowAt = -1;
    long long highAt = -1;
    for (long long i = from; i < to; ++i) {
        const double value = values[i];
        // Both, not one or the other: a run that only descends would otherwise
        // never take the second branch and would report no highest at all.
        if (value < lowest) {
            lowest = value;
            lowAt = i;
        }
        if (value > highest) {
            highest = value;
            highAt = i;
        }
    }

    if (lowAt < 0 && highAt < 0) {
        return found; // nothing passed either comparison: all NaN, and a gap
    }
    if (!std::isfinite(lowest) || !std::isfinite(highest)) {
        return carefulExtremes(values, from, to);
    }
    found.lowest = lowest;
    found.highest = highest;
    found.lowAt = lowAt;
    found.highAt = highAt;
    return found;
}

long long wholeBuckets(long long length, long long bucket)
{
    return length > bucket ? (length / bucket) * bucket : length;
}

void reduceBucketsInto(const double* values, long long count, long long bucket, double* out)
{
    if (values == nullptr || out == nullptr || count <= 0) {
        return;
    }
    const long long width = std::max<long long>(bucket, 1);
    const long long taken = (count + width - 1) / width;

    const auto nothing = std::numeric_limits<double>::quiet_NaN();
    for (long long b = 0; b < taken; ++b) {
        const long long from = b * width;
        const long long to = std::min(from + width, count);
        const Extremes found = extremesOf(values, from, to);
        if (!found.found()) {
            // Nothing drawable in the whole bucket. A pair of NaN is a gap, and
            // a gap is what that is -- dropping the bucket instead would slide
            // every later one left and draw the line across the hole.
            out[b * 2] = nothing;
            out[b * 2 + 1] = nothing;
            continue;
        }
        out[b * 2] = found.first();
        out[b * 2 + 1] = found.second();
    }
}

void reduceBuckets(const double* values, long long count, long long bucket,
                   std::vector<double>& out)
{
    if (values == nullptr || count <= 0) {
        return;
    }
    const long long width = std::max<long long>(bucket, 1);
    const long long taken = (count + width - 1) / width;
    const std::size_t was = out.size();
    out.resize(was + static_cast<std::size_t>(taken) * 2);
    reduceBucketsInto(values, count, width, out.data() + was);
}

void coarsenEnvelopeInto(const double* pairs, long long buckets, long long factor, double* out)
{
    if (pairs == nullptr || out == nullptr || buckets <= 0) {
        return;
    }
    // See the header: the pair buffer is already a sequence in occurrence
    // order, so folding it by twice the factor is the same question
    // reduceBuckets() asks of the elements themselves.
    reduceBucketsInto(pairs, buckets * 2, std::max<long long>(factor, 1) * 2, out);
}

void coarsenEnvelope(const double* pairs, long long buckets, long long factor,
                     std::vector<double>& out)
{
    if (pairs == nullptr || buckets <= 0) {
        return;
    }
    reduceBuckets(pairs, buckets * 2, std::max<long long>(factor, 1) * 2, out);
}

namespace {

/// The visible range clamped to the data, which is what a run has to cover.
void drawnRange(const LevelView& view, double& low, double& high)
{
    low = std::max(view.low, 0.0);
    high = std::min(view.high, static_cast<double>(view.length));
}

/// Octaves between two buckets.
double octavesApart(long long bucket, long long from)
{
    return std::abs(std::log2(static_cast<double>(std::max<long long>(bucket, 1))) -
                    std::log2(static_cast<double>(std::max<long long>(from, 1))));
}

} // namespace

int drawnLevel(std::span<const HeldLevel> held, const LevelView& view)
{
    if (held.empty() || !view.usable()) {
        return -1;
    }
    double low = 0.0;
    double high = 0.0;
    drawnRange(view, low, high);

    // The finest run that covers the pane. They all hold about the same number
    // of points, so a finer one is more detail on screen for the same cost --
    // and the coarser ones are still here for the moment the reader zooms out
    // past this one, which is the whole reason there is more than one.
    int best = -1;
    for (std::size_t i = 0; i < held.size(); ++i) {
        const HeldLevel& level = held[i];
        if (!level.complete || !level.window.covers(low, high)) {
            continue;
        }
        if (best < 0 || level.window.bucket < held[static_cast<std::size_t>(best)].window.bucket) {
            best = static_cast<int>(i);
        }
    }
    return best;
}

bool served(std::span<const HeldLevel> held, const LevelView& view, double low, double high,
            long long bucket)
{
    low = std::max(low, 0.0);
    high = std::min(high, static_cast<double>(view.length));
    return std::any_of(held.begin(), held.end(), [&](const HeldLevel& level) {
        return level.complete && level.window.bucket <= bucket && level.window.covers(low, high);
    });
}

std::optional<PlotWindow> wantedLevel(std::span<const HeldLevel> held, const LevelView& view,
                                      const PlotFocus& focus)
{
    if (!view.usable()) {
        return {};
    }
    double low = 0.0;
    double high = 0.0;
    drawnRange(view, low, high);

    const std::optional<PlotWindow> needed =
        windowFor(view.low, view.high, view.length, view.paneBuckets);
    if (!needed.has_value()) {
        // Zoomed out far enough that the whole-line summary is already at this
        // bucket or finer. There is nothing a second read could add.
        return {};
    }

    const int at = drawnLevel(held, view);
    const bool answered = at >= 0 && held[static_cast<std::size_t>(at)].window.bucket <= needed->bucket;
    if (!answered) {
        // What the pane is waiting for, an octave finer than it asked. First,
        // because it is the only one the reader can see.
        std::optional<PlotWindow> want =
            windowFor(view.low, view.high, view.length, view.detailBuckets);
        if (!want.has_value()) {
            // The finer run would reach past the end of the line, so there is
            // no octave to take -- but the pane still wants a finer bucket than
            // the whole-line summary has. Only lines between one and two panes'
            // worth of buckets long land here.
            want = needed;
        }
        return want;
    }

    // The pane is answered, so what is left is idle work.
    //
    // Inward first, and only towards a focus. A run costs its span and each
    // octave in is half the span of the one above, so the whole inward ladder
    // is cheaper than one step out -- which is why it can be four octaves deep
    // and why it is worth reading before the outward one. Without a focus there
    // is nothing to aim it at: the view's own centre is where detailBuckets()
    // already reads an octave finer, and guessing further in about a reader who
    // may be about to zoom out is spending the file on a coin toss.
    if (focus.active && focus.inward) {
        for (int octave = 1; octave <= kFocusOctavesIn; ++octave) {
            // Where the view goes if the reader keeps zooming about this point:
            // the pointer holds still and both edges come in towards it, which
            // is exactly what PlotSurface.zoomedAxis does.
            const double shrink = 1.0 / static_cast<double>(1LL << octave);
            const double wantLow = focus.position - (focus.position - view.low) * shrink;
            const double wantHigh = focus.position + (view.high - focus.position) * shrink;
            const std::optional<PlotWindow> in =
                windowFor(wantLow, wantHigh, view.length, view.paneBuckets);
            if (!in.has_value()) {
                break; // finer than the line has anything to say about
            }
            if (!served(held, view, wantLow, wantHigh, in->bucket)) {
                return in;
            }
        }
    }

    // ...and then the octaves out, nearest first.
    //
    // Measured from the *run* the pane is being drawn from rather than from the
    // view, and that is not a detail. A run is twice the pane wide and steps by
    // a quarter of itself, so panning about inside one leaves this arithmetic
    // untouched -- which is what keeps "panning reads nothing" true of the
    // prefetch as well as of the picture. Taken from the view, every pan would
    // shift the octaves a little and eventually ask for one.
    if (at < 0) {
        return {};
    }
    const PlotWindow base = held[static_cast<std::size_t>(at)].window;
    const double half = static_cast<double>(base.span) / 2.0;
    double centre = static_cast<double>(base.first) + half;
    if (focus.active) {
        // A zoom out about a pointer near one edge extends mostly the other
        // way, so the centre leans towards it -- halfway, not all the way,
        // because the run on screen still has to come back covered. Only a
        // wheel sets a focus, so a pan cannot move this.
        centre = (centre + focus.position) / 2.0;
    }
    for (int octave = 1; octave <= view.prefetchOctaves; ++octave) {
        const auto reach = static_cast<double>(1LL << octave);
        double wantLow = centre - half * reach;
        double wantHigh = centre + half * reach;
        // Whatever the lean did, never less than the run it came from: a step
        // out that did not cover what is already on screen would be a step out
        // into a blank.
        wantLow = std::min(wantLow, static_cast<double>(base.first));
        wantHigh = std::max(wantHigh, static_cast<double>(base.first + base.span));

        const std::optional<PlotWindow> out =
            windowFor(wantLow, wantHigh, view.length, view.paneBuckets);
        if (!out.has_value()) {
            // That far out the whole-line summary already covers it, and so
            // does everything past it.
            break;
        }
        // Asked about the range rather than about the window, because a run
        // already in hand may answer for this octave without being the run this
        // would have read: one the reader zoomed in from is finer and wider
        // than what is being asked for here, and re-reading it would be a round
        // trip spent on nothing.
        if (!served(held, view, wantLow, wantHigh, out->bucket)) {
            return out;
        }
    }
    return {};
}

std::size_t coldestLevel(std::span<const HeldLevel> held, const LevelView& view,
                         const PlotFocus& focus)
{
    if (held.empty()) {
        return 0;
    }
    const std::optional<PlotWindow> needed =
        windowFor(view.low, view.high, view.length, view.paneBuckets);
    const int at = drawnLevel(held, view);
    const long long drawing =
        at >= 0 ? held[static_cast<std::size_t>(at)].window.bucket : std::numeric_limits<long long>::max();

    // Abandoned speculation first: a run finer than the one on screen was read
    // on a guess about where the zoom was going, and a focus somewhere it does
    // not cover says the guess was wrong. Ranked above every distance, so it
    // goes before anything still on the reader's path.
    const auto forsaken = [&](const HeldLevel& level) {
        if (!focus.active || at < 0 || level.window.bucket >= drawing) {
            return false;
        }
        return !level.window.covers(focus.position, focus.position);
    };

    const auto distance = [&](const HeldLevel& level) {
        if (!needed.has_value()) {
            return 0.0;
        }
        return octavesApart(level.window.bucket, needed->bucket);
    };

    std::size_t worst = 0;
    for (std::size_t i = 1; i < held.size(); ++i) {
        const bool lost = forsaken(held[i]);
        const bool losing = forsaken(held[worst]);
        if (lost != losing) {
            if (lost) {
                worst = i;
            }
            continue;
        }
        if (distance(held[i]) > distance(held[worst])) {
            worst = i;
        }
    }
    return worst;
}

std::optional<LogColumns> logColumnsFor(double low, double high, int columns)
{
    if (columns <= 0 || !(low > 0.0) || !(high > low) || !std::isfinite(high)) {
        return {};
    }
    const double from = std::log2(low);
    const double to = std::log2(high);
    const double octaves = to - from;
    // Under an octave the linear path is the right one; see the header.
    if (!(octaves >= 1.0) || !std::isfinite(octaves)) {
        return {};
    }
    // Rounded up to a power of two, so between one and two edges land in each
    // pixel column. The bound is the pane: at an octave exactly it is
    // `columns` rounded up, and it only falls from there.
    const double wanted = static_cast<double>(columns) / octaves;
    long long density = 1;
    while (static_cast<double>(density) < wanted && density < (1LL << 30)) {
        density <<= 1;
    }
    const double margin = octaves / 2.0;
    LogColumns grid;
    grid.density = density;
    grid.first = static_cast<long long>(std::floor((from - margin) * static_cast<double>(density)));
    grid.last = static_cast<long long>(std::ceil((to + margin) * static_cast<double>(density)));
    return grid;
}

bool logColumnsServe(const LogColumns& held, double low, double high, int columns)
{
    const std::optional<LogColumns> wanted = logColumnsFor(low, high, columns);
    return wanted.has_value() && wanted->density == held.density && held.covers(low, high);
}

void edgesAlong(const LogColumns& columns, double start, double step, std::vector<double>& out)
{
    out.clear();
    if (columns.density <= 0 || columns.last < columns.first || !std::isfinite(start) ||
        !std::isfinite(step) || !(std::abs(step) > 0.0)) {
        return;
    }
    out.reserve(static_cast<std::size_t>(columns.last - columns.first + 1));
    for (long long k = columns.first; k <= columns.last; ++k) {
        out.push_back((columns.edge(k) - start) / step);
    }
    if (step < 0.0) {
        std::reverse(out.begin(), out.end());
    }
}

PaneColumns::Step PaneColumns::request(int columns)
{
    // Down to the quantum, and never to nothing. See kColumnQuantum for why
    // down rather than to the nearest.
    const int quantised = std::clamp((std::max(columns, 0) / kColumnQuantum) * kColumnQuantum,
                                     kMinPoints / 2, kMaxPoints / 2);
    if (quantised == wanted) {
        return Step::Nothing;
    }
    wanted = quantised;
    if (wanted == applied) {
        return Step::Cancel;
    }
    if (!measured) {
        // The surface measuring itself for the first time. There is no gesture
        // to wait out: whatever has been read so far was read at an assumed
        // width, so waiting would open every plot at the wrong resolution and
        // re-read every line of it a fifth of a second later.
        measured = true;
        return Step::Apply;
    }
    // The read is at the end of the drag, not once per sixty-four pixels of it.
    return Step::Wait;
}

bool PaneColumns::apply()
{
    if (wanted == applied) {
        return false;
    }
    applied = wanted;
    return true;
}

void ZoomFocus::set(double atX, double factor)
{
    if (!std::isfinite(atX) || !std::isfinite(factor) || !(factor > 0.0)) {
        clear();
        return;
    }
    x = atX;
    inward = factor > 1.0;
    active = true;
}

} // namespace gui
