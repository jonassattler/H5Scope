// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// The gesture the benchmark performs, expressed as a sequence of view windows
// rather than as a sequence of mouse events.
//
// Synthesised input would have been the more faithful thing to measure, and it
// is the wrong thing to compare: the three spikes reach their axes by three
// different routes -- Qt Graphs through ValueAxis::zoom and ::pan, the scene
// graph spike through its own item, QCustomPlot through QCPAxis::setRange --
// so a wheel event would be measuring three input handlers on its way to
// measuring three renderers. A window is the quantity all three agree on, and
// it is what the application's own zoom arithmetic in PlotSurface.qml:284-297
// resolves to anyway.
//
// The schedule is fixed and deterministic. Three movements, because they cost
// different things:
//
//   pan      the window slides across the data at full width. Every frame
//            shows a different slice, so nothing can be cached, and any
//            renderer that re-decimates per frame pays for it here.
//   zoom in  down to a two-hundredth of the span. Decimation stops helping;
//            the renderer draws individual samples.
//   zoom out back to the whole, which is where a renderer that holds one
//            buffer per zoom level shows what it kept.

#include <cstdint>

namespace spike {

struct ViewWindow {
    double xMin = 0.0;
    double xMax = 1.0;
    double yMin = 0.0;
    double yMax = 1.0;
};

class ScriptedGesture
{
public:
    /// `full` is the whole data extent; the gesture never leaves it, so every
    /// frame has something to draw and none of them is measuring an empty
    /// plot.
    ScriptedGesture(ViewWindow full, int frames);

    [[nodiscard]] int frames() const { return frames_; }

    /// The window frame `index` should be showing. Clamped, so a renderer that
    /// paints one frame more than it was asked for does not fall off the end.
    [[nodiscard]] ViewWindow at(int index) const;

    /// The three movements, for a report that wants to say which one hurt.
    [[nodiscard]] const char* movementAt(int index) const;

private:
    ViewWindow full_;
    int frames_;
};

} // namespace spike
