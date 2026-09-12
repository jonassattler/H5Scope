// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// A line plot drawn directly on the Qt Quick scene graph.
//
// Carried over from spikes/plotting/scenegraph on the plot-library-evaluation
// branch, where it was measured against Qt Graphs and QCustomPlot over a
// fifteen-cell grid. Nothing here is wired into the application yet: the item
// compiles, registers into H5Scope.Backend, and waits for PlotSurface.qml to
// stop importing QtGraphs. The migration is written down in
// instructions/scene-graph-migration.md.
//
// It exists because four things a scientific plot needs are things no library
// on offer gets right, and all four are cheap once the projection is ours:
//
// 1. Decimation to the pixel width, by min/max envelope rather than by stride.
//    The application thins by stride today, and DatasetTableModel.hpp:205-213
//    names the cost: "a spike narrower than one stride is not drawn". An
//    envelope draws the same number of points and cannot lose an extremum,
//    because the extremum is what it selects.
//
// 2. Double precision on the way to the vertex. QSGGeometry stores float32.
//    An x of 1.7e9 -- seconds since the epoch, which is what every logger in
//    the world writes -- has a float spacing of 128, so a line sampled every
//    millisecond collapses into a staircase of flat treads. The fix is not to
//    store doubles; it is to project in double and cast the *result*, because
//    a pixel coordinate is at most a few thousand. That is why this item does
//    its own projection instead of uploading data coordinates and letting a
//    matrix in the vertex shader do it, which is the faster arrangement and the
//    one that loses the data.
//
// 3. A gap where the data is absent. Dropping non-finite points and handing
//    the rest to a line renderer -- which is what DatasetPlot::fill does --
//    draws a straight line *across* the missing data rather than a gap. Here a
//    run of non-finite values ends the strip and the next run starts a new one.
//
// 4. A logarithmic y axis, which 2-D Qt Graphs cannot do at any price.

#include <QtGui/QColor>
#include <QtQml/qqmlregistration.h>
#include <QtQuick/QQuickItem>

#include <vector>

namespace gui {

/// One line to draw.
///
/// `values` is **borrowed**: this item reads it on every frame and never
/// copies it. The owner is whoever handed it over -- DatasetPlot and
/// CustomPlot both already hold their lines as `std::vector<double>`, and a
/// copy of a ten-thousand-line selection would be a hundred and sixty
/// megabytes of it -- so the contract is that the owner calls clear() before
/// it touches the vectors again. There is no way for the item to notice; that
/// is the price of not copying, and it is the reason this struct says so here
/// rather than leaving it to be discovered.
struct PlotLine {
    const double* values = nullptr;
    qsizetype count = 0;
    /// Supplied by the caller rather than chosen here: every colour in this
    /// application resolves through Theme.qml, and a renderer that picked its
    /// own would be the one place that did not.
    QColor colour;
};

class PlotItem : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT

    /// The window the item is showing, in data coordinates. Written by QML;
    /// the existing zoom and pan arithmetic in PlotSurface.qml:284-405 already
    /// resolves to exactly this pair of ranges.
    Q_PROPERTY(double xMin READ xMin WRITE setXMin NOTIFY viewChanged FINAL)
    Q_PROPERTY(double xMax READ xMax WRITE setXMax NOTIFY viewChanged FINAL)
    Q_PROPERTY(double yMin READ yMin WRITE setYMin NOTIFY viewChanged FINAL)
    Q_PROPERTY(double yMax READ yMax WRITE setYMax NOTIFY viewChanged FINAL)

    /// A logarithmic y axis. The chrome has to agree -- decade ticks, and a
    /// label format to match -- or the grid lies about where the curve is.
    Q_PROPERTY(bool logY READ logY WRITE setLogY NOTIFY viewChanged FINAL)

    /// One draw call for the whole plot instead of one per line.
    ///
    /// Measured on the evaluation branch at ten thousand lines of a thousand
    /// points: 346.85 ms a frame with a node per line, 12.05 ms batched. And
    /// measured again on memory, where it is the wrong way round: 3871 MiB
    /// against 1282. This implementation batches into *disjoint segments* with
    /// twenty-byte coloured vertices, which doubles the vertex count to buy the
    /// draw call. Batched strips with an index buffer would buy the same thing
    /// for less, and that is the work the migration plan schedules before this
    /// mode becomes the default.
    Q_PROPERTY(bool batched READ batched WRITE setBatched NOTIFY batchedChanged FINAL)

    /// Vertices submitted for the last frame. The count that travels -- it does
    /// not depend on the machine, it is bounded by the item's pixel width
    /// rather than by the dataset, and a change that makes it scale with the
    /// file is a change that has broken the decimation. tests/test_cost.cpp is
    /// where an assertion on it belongs.
    Q_PROPERTY(int vertexCount READ vertexCount NOTIFY drew FINAL)

public:
    explicit PlotItem(QQuickItem* parent = nullptr);
    ~PlotItem() override;

    /// Hand over the lines to draw. See PlotLine: the values are borrowed.
    void setLines(std::vector<PlotLine> lines, double xStart, double xStep);

    /// Draw nothing, and stop reading whatever was handed over. Must be called
    /// before the owner of those vectors modifies or frees them.
    void clear();

    [[nodiscard]] double xMin() const { return xMin_; }
    [[nodiscard]] double xMax() const { return xMax_; }
    [[nodiscard]] double yMin() const { return yMin_; }
    [[nodiscard]] double yMax() const { return yMax_; }
    [[nodiscard]] bool logY() const { return logY_; }
    [[nodiscard]] bool batched() const { return batched_; }
    [[nodiscard]] int vertexCount() const { return vertices_; }

    void setXMin(double value);
    void setXMax(double value);
    void setYMin(double value);
    void setYMax(double value);
    void setLogY(bool on);
    void setBatched(bool on);

Q_SIGNALS:
    void viewChanged();
    void batchedChanged();
    /// Emitted after a frame's geometry has been built, so anything reading
    /// vertexCount reports the frame on screen rather than the one before it.
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
    int project(const PlotLine& line);

    std::vector<PlotLine> lines_;
    double xStart_ = 0.0;
    double xStep_ = 1.0;
    double xMin_ = 0.0;
    double xMax_ = 1.0;
    double yMin_ = 0.0;
    double yMax_ = 1.0;
    bool logY_ = false;
    bool batched_ = false;
    int vertices_ = 0;

    // Reused between frames. The whole point of the envelope is that the
    // vertex count is bounded by the width of the item, so these settle at a
    // few thousand entries on the first frame and never grow again -- which is
    // what keeps the per-frame allocation count flat as the file gets bigger.
    std::vector<QPointF> points_;
    std::vector<Run> runs_;
    std::vector<int> runStart_;
};

} // namespace gui
