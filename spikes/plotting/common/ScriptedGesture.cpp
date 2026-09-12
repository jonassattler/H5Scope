// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "common/ScriptedGesture.hpp"

#include <algorithm>
#include <cmath>

namespace spike {
namespace {

// How far in the zoom goes. Two hundred is past the point where decimation can
// help -- at 2048 points across a 1400-pixel plot, a two-hundredth of the span
// is ten samples, drawn one by one.
constexpr double kDeepestZoom = 200.0;

// The pan sweeps a quarter of the span, at a width of a tenth. Enough that
// every frame is a different slice and nothing survives from the frame before.
constexpr double kPanWidth = 0.10;
constexpr double kPanDistance = 0.25;

} // namespace

ScriptedGesture::ScriptedGesture(ViewWindow full, int frames)
    : full_(full)
    , frames_(std::max(3, frames))
{
}

const char* ScriptedGesture::movementAt(int index) const
{
    const double t = static_cast<double>(std::clamp(index, 0, frames_ - 1))
                     / static_cast<double>(frames_ - 1);
    if (t < 1.0 / 3.0) {
        return "pan";
    }
    if (t < 2.0 / 3.0) {
        return "zoom in";
    }
    return "zoom out";
}

ViewWindow ScriptedGesture::at(int index) const
{
    const double t = static_cast<double>(std::clamp(index, 0, frames_ - 1))
                     / static_cast<double>(frames_ - 1);

    const double xSpan = full_.xMax - full_.xMin;
    const double ySpan = full_.yMax - full_.yMin;
    const double xCentre = full_.xMin + xSpan / 2.0;
    const double yCentre = full_.yMin + ySpan / 2.0;

    double zoom = 1.0;
    double offset = 0.0;

    if (t < 1.0 / 3.0) {
        // Pan. Narrow the window once, then slide it.
        const double u = t * 3.0;
        zoom = 1.0 / kPanWidth;
        offset = (u - 0.5) * kPanDistance * xSpan;
    } else if (t < 2.0 / 3.0) {
        // Zoom in, geometrically. A linear ramp would spend most of its frames
        // in the shallow end, where nothing is difficult.
        //
        // It starts at the pan's own width rather than at 1, so the three
        // movements join without the window jumping between two frames.
        const double u = (t - 1.0 / 3.0) * 3.0;
        zoom = std::pow(1.0 / kPanWidth, 1.0 - u) * std::pow(kDeepestZoom, u);
        offset = 0.0;
    } else {
        // Zoom back out to the whole extent.
        const double u = (t - 2.0 / 3.0) * 3.0;
        zoom = std::pow(kDeepestZoom, 1.0 - u);
        offset = 0.0;
    }

    ViewWindow window;
    window.xMin = xCentre + offset - xSpan / (2.0 * zoom);
    window.xMax = xCentre + offset + xSpan / (2.0 * zoom);
    // y follows x, so the zoom is a zoom and not a stretch. The application
    // fits y to the drawn extent instead; that is a policy difference, and
    // holding the aspect here keeps the three spikes doing the same work.
    window.yMin = yCentre - ySpan / (2.0 * zoom);
    window.yMax = yCentre + ySpan / (2.0 * zoom);
    return window;
}

} // namespace spike
