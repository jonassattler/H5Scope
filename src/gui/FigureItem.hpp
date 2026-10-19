// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// The plot, as plotcore draws it, on the Qt Quick scene graph.
//
// Everything that decides the picture -- the axes, their numbers and rules,
// the gestures, the colours, the crosshair, the legend on the plot, the band
// and what it writes -- is plotcore::Figure's. This item is a host of it, and
// does what a host does and nothing more: it hands the figure the pane's size
// and how its fonts measure, forwards the pointer and the wheel, and draws the
// Scene that comes back. The same figure is what the library's Qt, Python and
// Rust hosts draw, so the viewer and the library are one plot by construction
// rather than by a parity suite.
//
// The lines are the viewer's own, read out of a file on a thread and held under
// a shared budget, so the figure is given this item as its LineSource: what a
// plot lends (LineTarget) is what the figure draws, what the plot already knows
// about its extent is what the axes are drawn between, and what the figure is
// told about the window -- where it is, how many columns it has, where a zoom
// is heading -- goes on to the plot, which is what decides what to fold or
// read.
//
// It carries **two** renderers, and that is not a hedge:
//
//   * Scene-graph nodes, which is what runs on a machine with a graphics API:
//     the rules and boxes rectangles, the numbers text nodes, and the lines a
//     single QSGGeometryNode of mitred triangle strips -- one node and one
//     draw call for the whole plot. Measured on the evaluation branch at ten
//     thousand lines, 346.85 ms a frame with a node per line against 12.05 ms
//     batched.
//
//   * QPainter (ScenePainter) into one texture, which is what runs under Qt
//     Quick's software renderer. That renderer draws no custom geometry at
//     all -- it knows rectangles, images, nine-patches and glyphs, and
//     silently drops everything else. The offscreen platform declares no RHI
//     capability, so the whole QML suite and tools/make-screenshots run on
//     exactly that renderer, and an item that drew nothing there would take
//     twenty pixel tests with it.
//
// Both are translations of one Scene and decide nothing, so the fallback is a
// page of painting and not a second plot. Nothing else in this application
// needs a fallback, because nothing else in it draws anything a rectangle
// cannot.

#include "gui/LineModel.hpp"
#include "gui/LineTarget.hpp"
#include "gui/ScenePainter.hpp"

#include "plotcore/Figure.hpp"

#include <QColor>
#include <QFont>
#include <QImage>
#include <QPointer>
#include <QTimer>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <QtQuick/QQuickItem>

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace gui {

class FigureItem : public QQuickItem, public LineTarget, public plotcore::LineSource
{
    Q_OBJECT
    QML_ELEMENT

    // --- the lines ---------------------------------------------------------------
    /// The plot whose lines this draws: DatasetPlot or a custom tab's
    /// CustomPlot, both of which are a LineModel.
    Q_PROPERTY(QObject* plot READ plot WRITE setPlot NOTIFY plotChanged FINAL)
    /// Whether to ask the plot for anything at all. Reading a plot samples the
    /// file, and a reader browsing a large dataset as a table must not pay for
    /// a plot of it nobody asked to see (PlotSurface.active).
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged FINAL)
    /// What the x axis runs between, and its smallest x above zero -- the
    /// surface's, worked out from the range or the time base -- and how long
    /// the data is, which is what the zoom's ceiling follows.
    Q_PROPERTY(double xMinimum READ xMinimum WRITE setXMinimum NOTIFY boundsChanged FINAL)
    Q_PROPERTY(double xMaximum READ xMaximum WRITE setXMaximum NOTIFY boundsChanged FINAL)
    Q_PROPERTY(double xPositiveMinimum READ xPositiveMinimum WRITE setXPositiveMinimum NOTIFY
                   boundsChanged FINAL)
    Q_PROPERTY(double dataLength READ dataLength WRITE setDataLength NOTIFY boundsChanged FINAL)

    // --- the settings, as plotcore names them -------------------------------------
    Q_PROPERTY(QString grid READ grid WRITE setGrid NOTIFY settingsChanged FINAL)
    Q_PROPERTY(double gridStepX READ gridStepX WRITE setGridStepX NOTIFY settingsChanged FINAL)
    Q_PROPERTY(double gridStepY READ gridStepY WRITE setGridStepY NOTIFY settingsChanged FINAL)
    Q_PROPERTY(bool xLog READ xLog WRITE setXLog NOTIFY settingsChanged FINAL)
    Q_PROPERTY(bool yLog READ yLog WRITE setYLog NOTIFY settingsChanged FINAL)
    Q_PROPERTY(double xLogBase READ xLogBase WRITE setXLogBase NOTIFY settingsChanged FINAL)
    Q_PROPERTY(double yLogBase READ yLogBase WRITE setYLogBase NOTIFY settingsChanged FINAL)
    Q_PROPERTY(bool flipped READ flipped WRITE setFlipped NOTIFY settingsChanged FINAL)
    Q_PROPERTY(
        bool minorNumbers READ minorNumbers WRITE setMinorNumbers NOTIFY settingsChanged FINAL)
    Q_PROPERTY(bool markers READ markers WRITE setMarkers NOTIFY settingsChanged FINAL)
    /// Whether pointing at the plot reads the sample under the pointer.
    Q_PROPERTY(bool crosshair READ crosshair WRITE setCrosshair NOTIFY settingsChanged FINAL)
    Q_PROPERTY(QString title READ title WRITE setTitle NOTIFY settingsChanged FINAL)
    Q_PROPERTY(QString xLabel READ xLabel WRITE setXLabel NOTIFY settingsChanged FINAL)
    Q_PROPERTY(QString yLabel READ yLabel WRITE setYLabel NOTIFY settingsChanged FINAL)
    Q_PROPERTY(bool legend READ legend WRITE setLegend NOTIFY settingsChanged FINAL)
    /// "top-left", "top-right", "bottom-left" or "bottom-right" -- or the
    /// viewer's own spelling of them, "topLeft" and so on.
    Q_PROPERTY(
        QString legendCorner READ legendCorner WRITE setLegendCorner NOTIFY settingsChanged FINAL)
    Q_PROPERTY(QString colourMode READ colourMode WRITE setColourMode NOTIFY settingsChanged FINAL)
    Q_PROPERTY(
        QColor colourSingle READ colourSingle WRITE setColourSingle NOTIFY settingsChanged FINAL)
    Q_PROPERTY(QColor colourRangeFrom READ colourRangeFrom WRITE setColourRangeFrom NOTIFY
                   settingsChanged FINAL)
    Q_PROPERTY(
        QColor colourRangeTo READ colourRangeTo WRITE setColourRangeTo NOTIFY settingsChanged FINAL)
    Q_PROPERTY(bool coloursReversed READ coloursReversed WRITE setColoursReversed NOTIFY
                   settingsChanged FINAL)
    Q_PROPERTY(double colourFrom READ colourFrom WRITE setColourFrom NOTIFY settingsChanged FINAL)
    Q_PROPERTY(double colourTo READ colourTo WRITE setColourTo NOTIFY settingsChanged FINAL)
    Q_PROPERTY(
        bool coloursCustom READ coloursCustom WRITE setColoursCustom NOTIFY settingsChanged FINAL)
    Q_PROPERTY(int highlighted READ highlighted WRITE setHighlighted NOTIFY settingsChanged FINAL)
    Q_PROPERTY(bool bundle READ bundle WRITE setBundle NOTIFY settingsChanged FINAL)
    Q_PROPERTY(bool dark READ dark WRITE setDark NOTIFY settingsChanged FINAL)
    Q_PROPERTY(bool stacked READ stacked WRITE setStacked NOTIFY settingsChanged FINAL)
    Q_PROPERTY(bool readout READ readout WRITE setReadout NOTIFY settingsChanged FINAL)
    Q_PROPERTY(bool bandReadout READ bandReadout WRITE setBandReadout NOTIFY settingsChanged FINAL)
    Q_PROPERTY(bool paneMessage READ paneMessage WRITE setPaneMessage NOTIFY settingsChanged FINAL)
    Q_PROPERTY(
        bool lineGestures READ lineGestures WRITE setLineGestures NOTIFY settingsChanged FINAL)

    // --- the faces, one per plotcore::TextRole ---------------------------------------
    Q_PROPERTY(QFont tickFont READ tickFont WRITE setTickFont NOTIFY fontsChanged FINAL)
    Q_PROPERTY(
        QFont tickMinorFont READ tickMinorFont WRITE setTickMinorFont NOTIFY fontsChanged FINAL)
    Q_PROPERTY(QFont titleFont READ titleFont WRITE setTitleFont NOTIFY fontsChanged FINAL)
    Q_PROPERTY(QFont axisNameFont READ axisNameFont WRITE setAxisNameFont NOTIFY fontsChanged FINAL)
    Q_PROPERTY(QFont legendFont READ legendFont WRITE setLegendFont NOTIFY fontsChanged FINAL)
    Q_PROPERTY(QFont noteFont READ noteFont WRITE setNoteFont NOTIFY fontsChanged FINAL)

    // --- the view ---------------------------------------------------------------------
    /// The window as a zoom and a pan on each axis -- what a dataset's memory
    /// files and puts back -- and as the four numbers it resolves to.
    Q_PROPERTY(double zoomX READ zoomX WRITE setZoomX NOTIFY viewChanged FINAL)
    Q_PROPERTY(double panX READ panX WRITE setPanX NOTIFY viewChanged FINAL)
    Q_PROPERTY(double zoomY READ zoomY WRITE setZoomY NOTIFY viewChanged FINAL)
    Q_PROPERTY(double panY READ panY WRITE setPanY NOTIFY viewChanged FINAL)
    Q_PROPERTY(double viewMinX READ viewMinX NOTIFY viewChanged FINAL)
    Q_PROPERTY(double viewMaxX READ viewMaxX NOTIFY viewChanged FINAL)
    Q_PROPERTY(double viewMinY READ viewMinY NOTIFY viewChanged FINAL)
    Q_PROPERTY(double viewMaxY READ viewMaxY NOTIFY viewChanged FINAL)
    /// How far x may be magnified -- the data's length over sixteen, and never
    /// less than 256 -- and so the narrowest x window there is; and the share
    /// of the window a drag keeps over the data.
    Q_PROPERTY(double maxZoom READ maxZoom NOTIFY viewChanged FINAL)
    Q_PROPERTY(double minimumSpanX READ minimumSpanX NOTIFY viewChanged FINAL)
    Q_PROPERTY(double panKeep READ panKeep CONSTANT FINAL)
    /// How many lines the legend on the plot names before it counts the rest,
    /// and how much of the pane a pointer is taken to cover: plotcore's
    /// FigureStyle, which is where the argument for each is.
    Q_PROPERTY(int legendRowLimit READ legendRowLimit CONSTANT FINAL)
    Q_PROPERTY(double pointerSize READ pointerSize CONSTANT FINAL)
    Q_PROPERTY(bool zoomed READ zoomed NOTIFY viewChanged FINAL)
    /// What each axis runs between at a zoom of one: x's ends, or one empty
    /// power of the base where a logarithmic x has nothing to run between,
    /// and y's padded extent of the lines on the common axis.
    Q_PROPERTY(double xBoundLow READ xBoundLow NOTIFY viewChanged FINAL)
    Q_PROPERTY(double xBoundHigh READ xBoundHigh NOTIFY viewChanged FINAL)
    Q_PROPERTY(double yBoundLow READ yBoundLow NOTIFY viewChanged FINAL)
    Q_PROPERTY(double yBoundHigh READ yBoundHigh NOTIFY viewChanged FINAL)
    /// The pane inside the gutters, in this item's coordinates.
    Q_PROPERTY(QRectF area READ area NOTIFY drew FINAL)
    /// The band being drawn, and the legend on the plot, in this item's
    /// coordinates; an empty rectangle where there is none.
    Q_PROPERTY(QRectF band READ band NOTIFY drew FINAL)
    Q_PROPERTY(QRectF legendRect READ legendRect NOTIFY drew FINAL)

    // --- what the last frame said ----------------------------------------------------
    /// The sample under the pointer: `{ valid, line, series, x, y, px, py,
    /// xText, yText }`, `line` being its place among the drawn lines.
    Q_PROPERTY(QVariantMap reading READ reading NOTIFY drew FINAL)
    /// Whether a band is being drawn (PlotSurface.selecting).
    Q_PROPERTY(bool selecting READ selecting NOTIFY drew FINAL)
    /// Why nothing can be drawn, and the same in a few words; empty when it can.
    Q_PROPERTY(QString message READ message NOTIFY drew FINAL)
    Q_PROPERTY(QString messageTitle READ messageTitle NOTIFY drew FINAL)
    /// Whether the pointer is over the pane: what Ctrl+C copies the plot by.
    Q_PROPERTY(bool hovered READ hovered NOTIFY hoveredChanged FINAL)
    /// Points and unbroken strokes drawn for the last frame, over every line
    /// together: two counts, which do not depend on the machine, and which the
    /// cost tests hold a zoom to.
    Q_PROPERTY(int drawnPointCount READ drawnPointCount NOTIFY drew FINAL)
    Q_PROPERTY(int drawnRunCount READ drawnRunCount NOTIFY drew FINAL)
    /// Whether a drawn line has been moved or scaled off where it was read.
    Q_PROPERTY(bool linesMoved READ linesMoved NOTIFY drew FINAL)

public:
    explicit FigureItem(QQuickItem* parent = nullptr);
    ~FigureItem() override;

    // --- LineTarget: what the plot lends --------------------------------------------
    void setLines(std::vector<PlotLine> lines, const PlotAxis& axis) override;
    void clearLines() override;
    [[nodiscard]] QObject* lineObject() override { return this; }

    // --- plotcore::LineSource: what the figure asks ----------------------------------
    Q_INVOKABLE [[nodiscard]] int lineCount() const override;
    [[nodiscard]] int series(int line) const override;
    [[nodiscard]] std::string name(int line) const override;
    [[nodiscard]] long long length(int line) const override;
    [[nodiscard]] bool ready(int line) const override;
    [[nodiscard]] plotcore::LineExtent extent(int line) const override;
    [[nodiscard]] plotcore::Span xExtent(int line) const override;
    [[nodiscard]] double xPositiveMinimum(int line) const override;
    void setPaneColumns(int columns) override;
    void setView(double xLow, double xHigh, bool xLog) override;
    void focus(const plotcore::GestureFocus& focus) override;
    void clearFocus() override;
    [[nodiscard]] PlotLine drawLine(int line) const override;
    [[nodiscard]] PlotAxis drawAxis(int line) const override;

    // --- asked of it -----------------------------------------------------------------
    /// Ask the plot for its lines again, now.
    Q_INVOKABLE void refill();
    Q_INVOKABLE void resetView();
    /// The window named in values -- the four boxes -- clipped to what the
    /// axes can draw.
    Q_INVOKABLE void setViewRange(double x0, double x1, double y0, double y1);
    /// The gestures, named rather than performed with a pointer: a wheel
    /// turned by `factor` about (`px`, `py`) on the pane's "x", "y" or "both";
    /// a drag of (`dx`, `dy`) pixels; a band from one corner to another, false
    /// when it is too small to be one. Each goes the way the pointer's would.
    Q_INVOKABLE void zoomAt(double px, double py, double factor, const QString& axes);
    Q_INVOKABLE void panBy(double dx, double dy);
    Q_INVOKABLE bool zoomToRegion(double px0, double py0, double px1, double py1);
    /// A value written as its axis writes its ticks.
    Q_INVOKABLE [[nodiscard]] QString xNumber(double value);
    Q_INVOKABLE [[nodiscard]] QString yNumber(double value);
    /// Every line back where it was read: what Control and a click do to one.
    Q_INVOKABLE void putLinesBack();
    /// The value under a pixel of this item, as the band reads it.
    Q_INVOKABLE [[nodiscard]] double dataXAt(double px, double py) const;
    Q_INVOKABLE [[nodiscard]] double dataYAt(double py, double px) const;
    /// The colour line `series` is drawn in as the `position`th of `count`,
    /// on the screen or on paper: what the legend beside the plot shows.
    Q_INVOKABLE [[nodiscard]] QColor seriesColor(int series, int position, int count,
                                                 bool paper = false) const;
    /// What the crosshair reads with the pointer at (`x`, `y`) of this item:
    /// the frame laid out again with it there, and its `reading`.
    Q_INVOKABLE QVariantMap readingAt(double x, double y);
    /// The numbers a band being drawn writes beside itself, as `{ key, text,
    /// x, y, width, height, turn }` in this item's coordinates.
    Q_INVOKABLE [[nodiscard]] QVariantList bandLabels() const;
    /// The scene's marks, for a test: `{ kind, layer, under, clipped, x0, y0,
    /// x1, y1, fill, stroke, width, radius, dashed, text, base, exponent,
    /// role, across, down, colour, turn }` each, the ones under the lines
    /// first.
    Q_INVOKABLE [[nodiscard]] QVariantList marks() const;
    /// The lines the last frame drew, for a test: `{ line, series, colour,
    /// width, runs, points, markers, own, yLow, yHigh }` each, in the order
    /// they were drawn -- the colour with the opacity it was drawn at.
    Q_INVOKABLE [[nodiscard]] QVariantList drawnLines() const;
    /// The legend on the plot as it was laid out: `{ line, series, name,
    /// colour, x0, y0, x1, y1 }` a row, the swatch from (x0, y0) to (x1, y1).
    Q_INVOKABLE [[nodiscard]] QVariantList legendRows() const;
    /// An axis of the last frame as it was numbered and ruled, in its own
    /// values (plotcore::SceneAxis): `"x"` or `"y"` for a common one, or the
    /// line whose own axis it is. `{ line, low, high, logarithmic, base,
    /// logNumbers, ticks: [{ value, at, text, base, exponent, width }], grid:
    /// [{ at, major }], minor: [{ at, text }] }`, every `at` a fraction of the
    /// axis; empty where there is no such axis. `"picture x"` and `"picture
    /// y"` are the common axes of the last picture() -- its axes are kept, and
    /// not the rest of its scene, so that a test can ask what the picture was
    /// numbered on without this item holding a second copy of every line.
    Q_INVOKABLE [[nodiscard]] QVariantMap axis(const QVariant& which) const;
    /// The axes of the lines' own, in the order they stand beside the pane --
    /// each as axis() answers, with the `series` its line is, and `colour`,
    /// `label` and the box it stands in (`x`, `y`, `width`, `height`) -- or
    /// the last picture's, when `picture`.
    Q_INVOKABLE [[nodiscard]] QVariantList sideAxes(bool picture = false) const;

    // --- the arithmetic the surface answers with, which is plotcore's ---------------
    Q_INVOKABLE [[nodiscard]] static bool usableBase(double base);
    Q_INVOKABLE [[nodiscard]] static double baseFor(const QString& mode, double custom);
    Q_INVOKABLE [[nodiscard]] static double valueAlong(double low, double high, double at,
                                                       bool logarithmic, double base);
    Q_INVOKABLE [[nodiscard]] static double axisPosition(double value, bool logarithmic,
                                                         double base);
    /// The picture that leaves the application: the pane laid out again at
    /// `width` by `height` units, drawn at `scale` device pixels a unit, in the
    /// page's colours on no ground when `publication`, with the crosshair
    /// where it is now when `cursor`.
    Q_INVOKABLE [[nodiscard]] QImage picture(double width, double height, double scale,
                                             bool publication, bool cursor);

    // --- properties --------------------------------------------------------------------
    [[nodiscard]] QObject* plot() const { return plotObject_; }
    void setPlot(QObject* plot);
    [[nodiscard]] bool active() const { return active_; }
    void setActive(bool active);
    [[nodiscard]] double xMinimum() const { return xMinimum_; }
    void setXMinimum(double value);
    [[nodiscard]] double xMaximum() const { return xMaximum_; }
    void setXMaximum(double value);
    [[nodiscard]] double xPositiveMinimum() const { return xPositiveMinimum_; }
    void setXPositiveMinimum(double value);
    [[nodiscard]] double dataLength() const { return dataLength_; }
    void setDataLength(double value);

    [[nodiscard]] QString grid() const;
    void setGrid(const QString& grid);
    [[nodiscard]] double gridStepX() const { return settings_.gridStepX; }
    void setGridStepX(double step);
    [[nodiscard]] double gridStepY() const { return settings_.gridStepY; }
    void setGridStepY(double step);
    [[nodiscard]] bool xLog() const { return settings_.xLog; }
    void setXLog(bool on);
    [[nodiscard]] bool yLog() const { return settings_.yLog; }
    void setYLog(bool on);
    [[nodiscard]] double xLogBase() const { return settings_.xLogBase; }
    void setXLogBase(double base);
    [[nodiscard]] double yLogBase() const { return settings_.yLogBase; }
    void setYLogBase(double base);
    [[nodiscard]] bool flipped() const { return settings_.flipped; }
    void setFlipped(bool on);
    [[nodiscard]] bool minorNumbers() const { return settings_.minorNumbers; }
    void setMinorNumbers(bool on);
    [[nodiscard]] bool markers() const { return settings_.markers; }
    void setMarkers(bool on);
    [[nodiscard]] bool crosshair() const { return settings_.cursor; }
    void setCrosshair(bool on);
    [[nodiscard]] QString title() const { return QString::fromStdString(settings_.title); }
    void setTitle(const QString& title);
    [[nodiscard]] QString xLabel() const { return QString::fromStdString(settings_.xLabel); }
    void setXLabel(const QString& label);
    [[nodiscard]] QString yLabel() const { return QString::fromStdString(settings_.yLabel); }
    void setYLabel(const QString& label);
    [[nodiscard]] bool legend() const { return settings_.legend; }
    void setLegend(bool on);
    [[nodiscard]] QString legendCorner() const;
    void setLegendCorner(const QString& corner);
    [[nodiscard]] QString colourMode() const
    {
        return QString::fromStdString(settings_.colours.mode);
    }
    void setColourMode(const QString& mode);
    [[nodiscard]] QColor colourSingle() const { return colourSingle_; }
    void setColourSingle(const QColor& colour);
    [[nodiscard]] QColor colourRangeFrom() const { return colourRangeFrom_; }
    void setColourRangeFrom(const QColor& colour);
    [[nodiscard]] QColor colourRangeTo() const { return colourRangeTo_; }
    void setColourRangeTo(const QColor& colour);
    [[nodiscard]] bool coloursReversed() const { return settings_.colours.reversed; }
    void setColoursReversed(bool on);
    [[nodiscard]] double colourFrom() const { return settings_.colours.from; }
    void setColourFrom(double at);
    [[nodiscard]] double colourTo() const { return settings_.colours.to; }
    void setColourTo(double at);
    [[nodiscard]] bool coloursCustom() const { return settings_.colours.custom; }
    void setColoursCustom(bool on);
    [[nodiscard]] int highlighted() const { return settings_.highlighted; }
    void setHighlighted(int series);
    [[nodiscard]] bool bundle() const { return settings_.bundle; }
    void setBundle(bool on);
    [[nodiscard]] bool dark() const { return settings_.dark; }
    void setDark(bool on);
    [[nodiscard]] bool stacked() const { return settings_.stacked; }
    void setStacked(bool on);
    [[nodiscard]] bool readout() const { return settings_.readout; }
    void setReadout(bool on);
    [[nodiscard]] bool bandReadout() const { return settings_.bandReadout; }
    void setBandReadout(bool on);
    [[nodiscard]] bool paneMessage() const { return settings_.paneMessage; }
    void setPaneMessage(bool on);
    [[nodiscard]] bool lineGestures() const { return settings_.lineGestures; }
    void setLineGestures(bool on);

    [[nodiscard]] QFont tickFont() const { return fonts_.faces[0]; }
    void setTickFont(const QFont& font) { setFace(0, font); }
    [[nodiscard]] QFont tickMinorFont() const { return fonts_.faces[1]; }
    void setTickMinorFont(const QFont& font) { setFace(1, font); }
    [[nodiscard]] QFont titleFont() const { return fonts_.faces[2]; }
    void setTitleFont(const QFont& font) { setFace(2, font); }
    [[nodiscard]] QFont axisNameFont() const { return fonts_.faces[3]; }
    void setAxisNameFont(const QFont& font) { setFace(3, font); }
    [[nodiscard]] QFont legendFont() const { return fonts_.faces[4]; }
    void setLegendFont(const QFont& font) { setFace(4, font); }
    [[nodiscard]] QFont noteFont() const { return fonts_.faces[5]; }
    void setNoteFont(const QFont& font) { setFace(5, font); }

    [[nodiscard]] double zoomX() const;
    void setZoomX(double zoom);
    [[nodiscard]] double panX() const;
    void setPanX(double pan);
    [[nodiscard]] double zoomY() const;
    void setZoomY(double zoom);
    [[nodiscard]] double panY() const;
    void setPanY(double pan);
    [[nodiscard]] double viewMinX() const;
    [[nodiscard]] double viewMaxX() const;
    [[nodiscard]] double viewMinY() const;
    [[nodiscard]] double viewMaxY() const;
    [[nodiscard]] double maxZoom() const { return figure_.camera().maxZoom; }
    [[nodiscard]] double minimumSpanX() const { return figure_.camera().x.minimumSpan; }
    [[nodiscard]] double panKeep() const { return figure_.camera().panKeep; }
    [[nodiscard]] int legendRowLimit() const { return figure_.style().legendRows; }
    [[nodiscard]] double pointerSize() const { return figure_.style().pointerSize; }
    [[nodiscard]] bool zoomed() const;
    [[nodiscard]] double xBoundLow() const { return figure_.camera().x.bounds.low; }
    [[nodiscard]] double xBoundHigh() const { return figure_.camera().x.bounds.high; }
    [[nodiscard]] double yBoundLow() const { return figure_.camera().y.bounds.low; }
    [[nodiscard]] double yBoundHigh() const { return figure_.camera().y.bounds.high; }
    [[nodiscard]] QRectF area() const;
    [[nodiscard]] QRectF band() const;
    [[nodiscard]] QRectF legendRect() const;
    [[nodiscard]] QVariantMap reading() const;
    [[nodiscard]] bool selecting() const { return scene_.band.has_value(); }
    [[nodiscard]] QString message() const { return QString::fromStdString(scene_.message); }
    [[nodiscard]] QString messageTitle() const
    {
        return QString::fromStdString(scene_.messageTitle);
    }
    [[nodiscard]] bool hovered() const { return hovered_; }
    [[nodiscard]] int drawnPointCount() const { return drawnPoints_; }
    [[nodiscard]] int drawnRunCount() const { return drawnRuns_; }
    [[nodiscard]] bool linesMoved() const;

    /// The figure, for a test that asks it what it holds.
    [[nodiscard]] const plotcore::Figure& figure() const { return figure_; }
    /// The last frame's scene.
    [[nodiscard]] const plotcore::Scene& lastScene() const { return scene_; }
    /// Lay the frame out now, as the next frame would: what a test asks
    /// before it reads the scene, so that a setting changed a line before is
    /// in the answer without waiting for a frame to be drawn.
    Q_INVOKABLE void compose();

Q_SIGNALS:
    void plotChanged();
    void activeChanged();
    void boundsChanged();
    void settingsChanged();
    void fontsChanged();
    void viewChanged();
    void hoveredChanged();
    /// Emitted after a frame is laid out, so anything reading the scene's
    /// answers reads the frame on screen and not the one before.
    void drew();

protected:
    void updatePolish() override;
    QSGNode* updatePaintNode(QSGNode* old, UpdatePaintNodeData* data) override;
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void hoverEnterEvent(QHoverEvent* event) override;
    void hoverMoveEvent(QHoverEvent* event) override;
    void hoverLeaveEvent(QHoverEvent* event) override;

private:
    void setFace(std::size_t slot, const QFont& font);
    void applySettings();
    void applyFonts();
    /// Something about the picture changed: lay it out again before the next
    /// frame.
    void dirty();
    /// What the drawn lines' series answer about themselves, asked once a
    /// lending rather than once a frame.
    void learnLines();
    [[nodiscard]] LineModel* model() const;
    [[nodiscard]] bool takesGestures() const;
    void setHovered(bool on);
    /// The view changed under a gesture: say so, and draw it.
    void moved();
    /// moved(), and the frame laid out at once: see the .cpp.
    void movedNow();
    /// Hand what a gesture did to a line back to the plot, which keeps it.
    void keepMoves();

    [[nodiscard]] QSGNode* buildNodes(QSGNode* root);
    [[nodiscard]] QSGNode* buildPainted(QSGNode* root);

    plotcore::Figure figure_;
    plotcore::FigureSettings settings_;
    plotcore::Scene scene_;
    /// Whether anything has changed since the frame was last laid out, and
    /// the window it was laid out for -- which decides the forms of a line.
    bool stale_ = true;
    QQuickWindow* composedIn_ = nullptr;
    struct PictureAxes
    {
        plotcore::SceneAxis x;
        plotcore::SceneAxis y;
        std::vector<plotcore::SceneAxis> sides;
    };
    PictureAxes pictureAxes_;
    SceneFonts fonts_;

    QPointer<QObject> plotObject_;
    bool active_ = true;
    double xMinimum_ = 0.0;
    double xMaximum_ = 1.0;
    double xPositiveMinimum_ = 0.0;
    double dataLength_ = 1.0;
    QColor colourSingle_;
    QColor colourRangeFrom_;
    QColor colourRangeTo_;

    // What the plot lent, and what its series answer about themselves.
    std::vector<PlotLine> lines_;
    PlotAxis axis_;
    struct Line
    {
        int series = -1;
        std::string name;
        bool separate = false;
        plotcore::LineExtent own;
        /// Where the plot says a gesture has put it (LineModel::seriesMove):
        /// what the figure is given, and what a gesture's result is compared
        /// against before it is written back.
        double scale = 1.0;
        double shift = 0.0;
    };
    std::vector<Line> about_;
    plotcore::LineExtent common_;

    // The last news handed on to the plot, so it is handed on once.
    std::optional<std::pair<double, double>> toldView_;
    std::optional<bool> toldLog_;
    int toldColumns_ = -1;

    QTimer refillSoon_;
    bool hovered_ = false;
    int drawnPoints_ = 0;
    int drawnRuns_ = 0;
};

} // namespace gui
