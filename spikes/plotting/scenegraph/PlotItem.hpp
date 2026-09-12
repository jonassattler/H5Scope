// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// A line plot written directly on the Qt Quick scene graph.
//
// The question this spike exists to answer is not "can a QQuickItem draw a
// polyline" -- it obviously can -- but whether the four things a scientific
// plot needs, and that no library here gets right on its own, are cheap enough
// to be worth owning:
//
// 1. Decimation to the pixel width, by min/max envelope rather than by stride.
//    The application thins by stride today, and DatasetTableModel.hpp:205-213
//    names it: "a spike narrower than one stride is not drawn". An envelope
//    reads the same number of *drawn* points and cannot lose an extremum,
//    because the extremum is what it selects. It is the M4 rule minus the two
//    horizontal samples, which matter for a bar chart and not for a line.
//
// 2. Double precision on the way to the vertex. QSGGeometry stores float32.
//    An x of 1.7e9 -- seconds since the epoch, which is what every logger in
//    the world writes -- has a float spacing of 128, so a line sampled every
//    millisecond collapses into a staircase of flat treads. The fix is not to
//    store doubles; it is to project in double and cast the *result*: a pixel
//    coordinate is at most a few thousand, where float has room to spare. The
//    same reasoning is why this item does its own projection instead of
//    uploading data coordinates and letting a matrix in the vertex shader do
//    it, which is the faster arrangement and the one that loses the data.
//
// 3. A gap where the data is absent. Dropping non-finite points and handing
//    the rest to a line renderer -- which is what DatasetPlot::fill does --
//    does not draw a gap, it draws a straight line across one. Here a run of
//    non-finite values ends the strip and the next run starts a new one.
//
// 4. A y axis that can be logarithmic, which 2-D Qt Graphs cannot do at all.
//    Three lines of arithmetic, because the projection is ours.
//
// Two ways of getting the geometry to the GPU, chosen with --variant, because
// which one wins depends entirely on how many lines there are and guessing
// would be the wrong way to find out:
//
//   per-series  one QSGGeometryNode per line, QSGFlatColorMaterial. Simple,
//               and one draw call per line.
//   batched     every line in one interleaved coloured-vertex buffer drawn as
//               disjoint segments, QSGVertexColorMaterial. One draw call for
//               the whole plot, at the cost of a wider vertex and twice as
//               many of them.

#include "common/ScriptedGesture.hpp"
#include "common/SyntheticSource.hpp"

#include <QtQml/qqmlregistration.h>
#include <QtQuick/QQuickItem>

#include <vector>

namespace spike {

class PlotItem : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT

public:
    explicit PlotItem(QQuickItem* parent = nullptr);
    ~PlotItem() override;

    enum class Batching { PerSeries, Batched };

    void setSource(const SyntheticSource* source);
    void setView(const ViewWindow& view);
    void setBatching(Batching batching);
    /// Returns false when the data cannot be drawn on a log axis at all --
    /// every value non-positive -- rather than drawing an empty pane.
    bool setLogY(bool on);

    /// Vertices submitted for the last frame. The count that travels: it does
    /// not depend on the machine, it is bounded by the pixel width rather than
    /// by the dataset, and a build where it starts scaling with the file is a
    /// build where the decimation has stopped working.
    [[nodiscard]] int verticesLastFrame() const { return vertices_; }

    /// Lines whose every visible sample was non-finite or, on a log axis,
    /// non-positive. Reported rather than silently skipped.
    [[nodiscard]] int emptyLinesLastFrame() const { return emptyLines_; }

    [[nodiscard]] bool logY() const { return logY_; }

Q_SIGNALS:
    /// Emitted after a frame's geometry has been built, so anything reading
    /// verticesLastFrame() reports the frame on screen rather than the one
    /// before it.
    void drew();

protected:
    QSGNode* updatePaintNode(QSGNode* old, UpdatePaintNodeData* data) override;
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;

private:
    /// One run of consecutive, drawable vertices. A line with two gaps in it
    /// is three runs.
    struct Run {
        int first = 0;
        int count = 0;
    };

    /// Project one line into `points_` in item coordinates, splitting at gaps,
    /// and append the runs to `runs_`. Returns how many runs it added.
    int project(const std::vector<double>& values);

    const SyntheticSource* source_ = nullptr;
    ViewWindow view_;
    Batching batching_ = Batching::PerSeries;
    bool logY_ = false;
    int vertices_ = 0;
    int emptyLines_ = 0;

    // Reused between frames. The whole point of the envelope is that the
    // vertex count is bounded by the width of the item, so these settle at a
    // few thousand entries on the first frame and never grow again -- which is
    // what keeps the allocation count per frame at zero.
    std::vector<QPointF> points_;
    std::vector<Run> runs_;
    std::vector<int> runStart_;
};

} // namespace spike
