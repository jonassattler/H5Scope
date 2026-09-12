// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// The interface the three spikes present, and the one driver that measures
// them through it.
//
// Written once rather than three times on purpose. A benchmark whose loop is
// copied per candidate is a benchmark where a difference between two columns
// can always be blamed on a difference between two loops, and the first thing
// anyone will do with the result of this bake-off is look for a reason to
// disbelieve it. Everything a renderer is free to do differently is behind
// Surface; everything about how it is timed is here.
//
// Surface is deliberately small. It is the same shape the application's own
// seam already has -- DatasetPlot hands a renderer a set of lines and an x
// origin, and QML tells it what part of them to show -- so a spike that cannot
// implement it is a spike whose renderer would not fit the application either,
// and that is a finding rather than an inconvenience.

#include "common/PlotBench.hpp"
#include "common/ScriptedGesture.hpp"
#include "common/SyntheticSource.hpp"

#include <QtGui/QImage>

QT_BEGIN_NAMESPACE
class QQuickWindow;
QT_END_NAMESPACE

namespace spike {

class Surface
{
public:
    virtual ~Surface() = default;

    /// The name this renderer goes by in every table and every file name.
    [[nodiscard]] virtual const char* rendererName() const = 0;

    /// The window whose frames are timed. Must be visible before runBench().
    [[nodiscard]] virtual QQuickWindow* window() const = 0;

    /// Hand the renderer a whole dataset, the way DatasetPlot::fill() hands it
    /// one line at a time. The call must return with the data submitted --
    /// whatever "submitted" means for the library -- because the difference
    /// between a renderer that copies here and one that copies at draw time is
    /// exactly what the build and first-frame columns are there to show.
    ///
    /// `source` outlives the call and every frame drawn from it.
    virtual void setSource(const SyntheticSource* source) = 0;

    /// Show this part of the data. Called once per frame during the gesture.
    virtual void setViewWindow(const ViewWindow& window) = 0;

    /// `--variant NAME`, for a spike with more than one way of drawing the
    /// same thing. Called once, before any data. A spike with only one way
    /// ignores it, and rendererName() is expected to carry the variant so two
    /// runs cannot be confused for each other in the results file.
    virtual void setVariant(const QString&) {}

    /// Whether the y axis can be made logarithmic at all. Not a measurement --
    /// a capability, answered by the library rather than about it, and the one
    /// the feature matrix cannot infer from an image.
    [[nodiscard]] virtual bool canDrawLogY() const { return false; }

    /// Ask for a logarithmic y axis. Returning false is a legitimate answer and
    /// the matrix records it.
    virtual bool setLogY(bool) { return false; }
};

/// Draw one frame and return when it has been drawn. Never blocks for longer
/// than a couple of seconds: a renderer that has stopped producing frames is a
/// result, not a reason for the harness to hang.
bool drawOneFrame(Surface& surface, class FrameTimer& timer);

/// Grab what is on screen, after making sure something is.
[[nodiscard]] QImage grab(Surface& surface, FrameTimer& timer, int settleFrames = 6);

/// Run the whole grid against one surface and return the rows.
[[nodiscard]] BenchReport runBench(Surface& surface, const Options& options);

} // namespace spike
