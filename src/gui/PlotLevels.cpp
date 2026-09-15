// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "gui/PlotLevels.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace gui {

Extremes extremesOf(const double* values, long long from, long long to)
{
    Extremes found;
    if (values == nullptr) {
        return found;
    }
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

long long wholeBuckets(long long length, long long bucket)
{
    return length > bucket ? (length / bucket) * bucket : length;
}

void reduceBuckets(const double* values, long long count, long long bucket, std::vector<double>& out)
{
    if (values == nullptr || count <= 0) {
        return;
    }
    const long long width = std::max<long long>(bucket, 1);
    const long long taken = (count + width - 1) / width;
    out.reserve(out.size() + static_cast<std::size_t>(taken) * 2);

    const auto nothing = std::numeric_limits<double>::quiet_NaN();
    for (long long b = 0; b < taken; ++b) {
        const long long from = b * width;
        const long long to = std::min(from + width, count);
        const Extremes found = extremesOf(values, from, to);
        if (!found.found()) {
            // Nothing drawable in the whole bucket. A pair of NaN is a gap, and
            // a gap is what that is -- dropping the bucket instead would slide
            // every later one left and draw the line across the hole.
            out.push_back(nothing);
            out.push_back(nothing);
            continue;
        }
        out.push_back(found.first());
        out.push_back(found.second());
    }
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

} // namespace gui
