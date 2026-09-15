// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// A line plot drawn directly on the Qt Quick scene graph.
//
// The arithmetic is next door in PlotProjection.hpp, which has no renderer in
// it and says why the plot is ours at all. This file is the part that has a
// window: it holds the lines, turns them into geometry once a frame, and hands
// QML the two questions the chrome has to ask -- where does a value sit, and
// what value sits there -- so that the ticks and the curve cannot disagree.
//
// It carries **two** renderers, and that is not a hedge:
//
//   * A single QSGGeometryNode of mitred triangle strips, which is what runs
//     on a machine with a graphics API. One node and one draw call for the
//     whole plot: measured on the evaluation branch at ten thousand lines,
//     346.85 ms a frame with a node per line against 12.05 ms batched.
//
//   * A QPainter fallback, which is what runs under Qt Quick's software
//     renderer. That renderer draws no custom geometry at all -- it knows
//     rectangles, images, nine-patches and glyphs, and silently drops
//     everything else. The offscreen platform declares no RHI capability, so
//     the whole QML suite and tools/make-screenshots run on exactly that
//     renderer, and an item that drew nothing there would take twenty pixel
//     tests with it. The projection is shared, so the fallback is the last
//     twenty lines of drawing and not a second plot.
//
// Nothing else in this application needs a fallback, because nothing else in
// it draws anything a rectangle cannot.

#include "gui/PlotProjection.hpp"

#include <QtGui/QColor>
#include <QtQml/qqmlregistration.h>
#include <QtQuick/QQuickItem>

#include <vector>

namespace gui {

class PlotItem : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT

    /// The window the item is showing, in data coordinates. Written by QML;
    /// the zoom and pan arithmetic in PlotSurface.qml already resolves to
    /// exactly this pair of ranges.
    Q_PROPERTY(double xMin READ xMin WRITE setXMin NOTIFY viewChanged FINAL)
    Q_PROPERTY(double xMax READ xMax WRITE setXMax NOTIFY viewChanged FINAL)
    Q_PROPERTY(double yMin READ yMin WRITE setYMin NOTIFY viewChanged FINAL)
    Q_PROPERTY(double yMax READ yMax WRITE setYMax NOTIFY viewChanged FINAL)

    /// Punctuation on the line: a dot at every sample.
    ///
    /// Only where the line is drawn sample for sample. Once anything is
    /// summarising -- this item's own envelope, or the model's on the way out
    /// of the file -- a drawn point is two samples out of a column of a
    /// thousand and a dot on it would mark nothing, so a summarised line
    /// carries no markers however this is set. Zooming in is what brings them
    /// back, because that is what makes a bucket one element.
    Q_PROPERTY(bool markers READ markers WRITE setMarkers NOTIFY markersChanged FINAL)
    /// How wide a marker is, across. Theme.plotMarkerSize.
    Q_PROPERTY(double markerSize READ markerSize WRITE setMarkerSize NOTIFY markersChanged FINAL)

    /// Points projected for the last frame, over every line together.
    ///
    /// The count that travels. It does not depend on the machine, it is
    /// bounded by the pane rather than by the file, and a change that makes it
    /// scale with the dataset is a change that has broken the decimation.
    /// tests/test_cost.cpp is where an assertion on it belongs.
    ///
    /// Reported rather than the vertex count because the vertex count is
    /// backend-dependent -- the software fallback submits none -- and this is
    /// the same number on both, with the geometry exactly twice it.
    Q_PROPERTY(int drawnPointCount READ drawnPointCount NOTIFY drew FINAL)

    /// Unbroken strokes drawn for the last frame. One per line when nothing is
    /// missing, and one more for every gap, which is how a test asserts that a
    /// run of NaN produced a gap rather than a line drawn across it.
    Q_PROPERTY(int drawnRunCount READ drawnRunCount NOTIFY drew FINAL)

public:
    explicit PlotItem(QQuickItem* parent = nullptr);
    ~PlotItem() override;

    /// Hand over the lines to draw, and the axis they are drawn against.
    ///
    /// See PlotLine: the values are borrowed and never copied, so the owner
    /// must call clear() before it touches them again.
    void setLines(std::vector<PlotLine> lines, const PlotAxis& axis);

    /// Draw nothing, and stop reading whatever was handed over. Must be called
    /// before the owner of those vectors modifies or frees them.
    Q_INVOKABLE void clear();

    /// How many lines were handed over. QML needs it to drive the loops below
    /// without holding a second copy of the drawn set.
    [[nodiscard]] Q_INVOKABLE int lineCount() const;

    /// Restyle one line without re-reading or re-filling anything.
    ///
    /// This is what the plot could not do before. Qt Graphs redraws a series
    /// when its *points* change and not when its colour does, so recolouring
    /// meant re-filling every series from the model -- sixty-four crossings
    /// into C++, each building a QList<QPointF> of a couple of thousand points,
    /// to change a hue. Here a colour is a property of the line and the frame
    /// is rebuilt from the values already in hand.
    Q_INVOKABLE void setSeriesColor(int index, const QColor& colour);
    Q_INVOKABLE void setSeriesOpacity(int index, double opacity);
    Q_INVOKABLE void setSeriesWidth(int index, double width);

    /// What line `index` is actually going to be drawn in.
    ///
    /// Nothing in the application reads this back -- the colour goes one way,
    /// from the surface into the frame. It is here so that a test can hold the
    /// rule that decides it: which colour a line takes is arithmetic done in
    /// QML over the drawn set, and a colour that is only ever written is a
    /// rule no test can check. Unticking one line used to recolour the rest
    /// under a palette, and it went unnoticed because the only witness was the
    /// screen.
    [[nodiscard]] Q_INVOKABLE QColor seriesColor(int index) const;

    /// Where `value` sits up the pane, as a fraction from the bottom, and back
    /// again. The chrome's ticks go through these rather than deriving the
    /// mapping a second time, because a tick drawn where the curve is not is
    /// worse than no tick at all.
    [[nodiscard]] Q_INVOKABLE double yFraction(double value) const;
    [[nodiscard]] Q_INVOKABLE double valueAt(double fraction) const;
    [[nodiscard]] Q_INVOKABLE double xFraction(double x) const;
    [[nodiscard]] Q_INVOKABLE double xAt(double fraction) const;

    /// The drawn sample nearest to a point in this item, as
    /// `{ valid, line, x, y, px, py }` -- the line's index in the handed-over
    /// set, the sample in data coordinates, and where it was drawn.
    ///
    /// What the crosshair snaps to, and the reason snapping is done here
    /// rather than in QML: the values never cross the boundary, so the only
    /// thing on the other side is a pixel. It is also the only place that can
    /// do it cheaply. x is affine in the sample index on every axis but a time
    /// base, so the nearest index is arithmetic rather than a search, and the
    /// whole answer costs one step per *line* instead of one per sample.
    [[nodiscard]] Q_INVOKABLE QVariantMap nearestSample(double px, double py) const;

    [[nodiscard]] double xMin() const { return view_.xMin; }
    [[nodiscard]] double xMax() const { return view_.xMax; }
    [[nodiscard]] double yMin() const { return view_.yMin; }
    [[nodiscard]] double yMax() const { return view_.yMax; }
    [[nodiscard]] bool markers() const { return markers_; }
    [[nodiscard]] double markerSize() const { return markerSize_; }
    [[nodiscard]] int drawnPointCount() const { return drawnPoints_; }
    [[nodiscard]] int drawnRunCount() const { return drawnRuns_; }

    void setXMin(double value);
    void setXMax(double value);
    void setYMin(double value);
    void setYMax(double value);
    void setMarkers(bool on);
    void setMarkerSize(double size);

Q_SIGNALS:
    void viewChanged();
    void markersChanged();
    /// Emitted after a frame's geometry has been built, so anything reading
    /// the counts above reports the frame on screen rather than the one before.
    void drew();

protected:
    QSGNode* updatePaintNode(QSGNode* old, UpdatePaintNodeData* data) override;
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;

private:
    /// Which of the two renderers built the child node. Switching is not
    /// something that happens at run time -- the graphics API is settled before
    /// the first frame -- but the node has to be discarded rather than cast if
    /// it ever did.
    enum class Drawn
    {
        Nothing,
        Geometry,
        Painted
    };

    /// The view, with the pane's measurements and the decimation budget filled
    /// in. Kept as the projection's own struct rather than as loose members so
    /// that there is one description of the view and not two.
    [[nodiscard]] PlotView viewForFrame() const;
    /// Project every line into points_ / runs_ / lineRuns_.
    void projectAll();
    /// Whether line `line` carries markers this frame: asked for, given a
    /// size, and drawn sample for sample rather than summarised.
    [[nodiscard]] bool marksLine(std::size_t line) const;
    QSGNode* buildGeometry(QSGNode* root);
    QSGNode* buildPainted(QSGNode* root);

    std::vector<PlotLine> lines_;
    PlotAxis axis_;
    PlotView view_;
    Drawn drawn_ = Drawn::Nothing;
    bool markers_ = false;
    double markerSize_ = 4.0;
    int drawnPoints_ = 0;
    int drawnRuns_ = 0;

    // Reused between frames. The whole point of the envelope is that the point
    // count is bounded by the pane, so these settle at a few thousand entries
    // on the first frame and never grow again -- which is what keeps the
    // per-frame allocation count flat as the file gets bigger.
    std::vector<QPointF> points_;
    std::vector<PlotRun> runs_;
    /// Where each line's runs start in runs_, with one past the end appended,
    /// so the drawing loops can find a line's strokes without searching.
    std::vector<int> lineRuns_;
    /// Whether each line was summarised rather than drawn sample for sample,
    /// which is what decides whether it carries markers.
    std::vector<bool> lineDecimated_;
    std::vector<QPointF> stroke_;
};

} // namespace gui
