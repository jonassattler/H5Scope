// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "FigureItem.hpp"

#include <QFontMetricsF>
#include <QGuiApplication>
#include <QHoverEvent>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QPainter>
#include <QQuickWindow>
#include <QSGClipNode>
#include <QSGFlatColorMaterial>
#include <QSGGeometryNode>
#include <QSGImageNode>
#include <QSGRectangleNode>
#include <QSGRendererInterface>
#include <QSGSimpleTextureNode>
#include <QSGTextNode>
#include <QSGVertexColorMaterial>
#include <QStyleHints>
#include <QTextLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>

namespace gui {
namespace {

using plotcore::Mark;

QColor colourOf(const plotcore::Rgba& colour)
{
    return QColor::fromRgbF(static_cast<float>(colour.red), static_cast<float>(colour.green),
                            static_cast<float>(colour.blue), static_cast<float>(colour.alpha));
}

plotcore::Rgba rgbaOf(const QColor& colour)
{
    if (!colour.isValid()) {
        return {};
    }
    return plotcore::Rgba::of(colour.redF(), colour.greenF(), colour.blueF(), colour.alphaF());
}

plotcore::Modifiers modifiersOf(Qt::KeyboardModifiers keys)
{
    return plotcore::Modifiers{keys.testFlag(Qt::ShiftModifier), keys.testFlag(Qt::ControlModifier),
                               keys.testFlag(Qt::AltModifier)};
}

std::optional<plotcore::Button> buttonOf(Qt::MouseButton button)
{
    switch (button) {
    case Qt::LeftButton:
        return plotcore::Button::Left;
    case Qt::MiddleButton:
        return plotcore::Button::Middle;
    case Qt::RightButton:
        return plotcore::Button::Right;
    default:
        return std::nullopt;
    }
}

/// Delete every node under `root`: removeAllChildNodes() only unlinks them,
/// and a node this item built is owned by nothing else.
void dropChildren(QSGNode* root)
{
    while (QSGNode* child = root->firstChild()) {
        delete child;
    }
}

/// The vertex colour of a line: its colour, which already carries how strongly
/// it is drawn, premultiplied, as the scene graph blends.
QSGGeometry::ColoredPoint2D vertexOf(const plotcore::Point& point, const plotcore::Rgba& colour)
{
    const double alpha = std::clamp(colour.alpha, 0.0, 1.0);
    const auto channel = [&](double value) {
        return static_cast<uchar>(std::lround(std::clamp(value, 0.0, 1.0) * 255.0 * alpha));
    };
    QSGGeometry::ColoredPoint2D vertex{};
    vertex.set(static_cast<float>(point.x()), static_cast<float>(point.y()), channel(colour.red),
               channel(colour.green), channel(colour.blue),
               static_cast<uchar>(std::lround(alpha * 255.0)));
    return vertex;
}

/// Whether there are fonts to measure: a QGuiApplication. The C++ suites run
/// under a QCoreApplication, where a QFontMetricsF has no font database to ask.
bool hasFonts()
{
    return qobject_cast<QGuiApplication*>(QCoreApplication::instance()) != nullptr;
}

} // namespace

FigureItem::FigureItem(QQuickItem* parent) : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton | Qt::MiddleButton);
    setAcceptHoverEvents(true);
    QQuickItem::setCursor(Qt::OpenHandCursor);
    // The figure draws the lines it is lent, and the plot is told the window
    // through this item, which is what it is a LineSource of.
    figure_.setSource(this);
    settings_ = figure_.settings();
    // The viewer writes the reading in the bar under its plot and says why a
    // view is empty in a way of its own, so neither is drawn in the pane.
    settings_.readout = false;
    settings_.paneMessage = false;
    settings_.legend = false;
    figure_.setSettings(settings_);
    // Where the platform says a drag begins, which is where a band does.
    if (hasFonts() && QGuiApplication::styleHints() != nullptr) {
        plotcore::FigureStyle style = figure_.style();
        style.dragDistance = QGuiApplication::styleHints()->startDragDistance();
        figure_.setStyle(style);
    }
    applyFonts();
    // A plot that changes says so, sometimes several times in one turn and
    // sometimes from inside a question this item asked it; the lines are asked
    // for again once, at the end of the turn, and never in the middle of a
    // frame being laid out.
    refillSoon_.setSingleShot(true);
    refillSoon_.setInterval(0);
    connect(&refillSoon_, &QTimer::timeout, this, &FigureItem::refill);
}

FigureItem::~FigureItem()
{
    // The plot keeps a record of who is reading its lines, and that record
    // must not outlive this item; BorrowedLines watches the object.
    figure_.setSource(nullptr);
}

// --- LineTarget -------------------------------------------------------------------

void FigureItem::setLines(std::vector<PlotLine> lines, const PlotAxis& axis)
{
    lines_ = std::move(lines);
    axis_ = axis;
    learnLines();
    dirty();
}

void FigureItem::clearLines()
{
    lines_.clear();
    about_.clear();
    axis_ = {};
    dirty();
}

LineModel* FigureItem::model() const
{
    return dynamic_cast<LineModel*>(plotObject_.data());
}

void FigureItem::learnLines()
{
    about_.clear();
    LineModel* plot = model();
    if (plot == nullptr) {
        return;
    }
    const QVariantList drawn = plot->drawnSeries();
    common_ = {};
    if (plot->hasData()) {
        const double low = plot->minimum();
        const double high = plot->maximum();
        common_.finite = std::isfinite(low) && std::isfinite(high) && low <= high;
        common_.minimum = low;
        common_.maximum = high;
        const double positive = plot->positiveMinimum();
        common_.positive = positive > 0.0;
        common_.positiveMinimum = positive;
    }
    about_.reserve(lines_.size());
    for (std::size_t i = 0; i < lines_.size(); ++i) {
        Line line;
        line.series = i < static_cast<std::size_t>(drawn.size())
                          ? drawn[static_cast<qsizetype>(i)].toInt()
                          : static_cast<int>(i);
        line.name = plot->seriesLabel(line.series).toStdString();
        const QVariantMap axis = plot->seriesAxis(line.series);
        line.separate = axis.value(QStringLiteral("separate")).toBool();
        if (line.separate) {
            const double low = axis.value(QStringLiteral("low")).toDouble();
            const double high = axis.value(QStringLiteral("high")).toDouble();
            line.own.finite = axis.value(QStringLiteral("finite")).toBool();
            line.own.minimum = low;
            line.own.maximum = high;
            line.own.positive = low > 0.0;
            line.own.positiveMinimum = low > 0.0 ? low : 0.0;
        }
        // What is chosen about a line is the plot's -- its colour, its own
        // axis -- and what a gesture did to it is this item's, kept by series.
        plotcore::LineSettings chosen;
        chosen.ownAxis = line.separate;
        chosen.axisFixed = axis.value(QStringLiteral("fixed")).toBool();
        chosen.axisLabel = axis.value(QStringLiteral("label")).toString().toStdString();
        const QVariant own = plot->seriesOverride(line.series);
        if (own.isValid() && !own.isNull()) {
            chosen.colour = rgbaOf(own.value<QColor>());
        }
        if (const auto found = moved_.find(line.series); found != moved_.end()) {
            chosen.scale = found->second.first;
            chosen.shift = found->second.second;
        }
        figure_.setLineSettings(static_cast<int>(i), chosen);
        about_.push_back(std::move(line));
    }
}

// --- LineSource --------------------------------------------------------------------

int FigureItem::lineCount() const
{
    return static_cast<int>(lines_.size());
}

int FigureItem::series(int line) const
{
    return line >= 0 && static_cast<std::size_t>(line) < about_.size()
               ? about_[static_cast<std::size_t>(line)].series
               : line;
}

std::string FigureItem::name(int line) const
{
    return line >= 0 && static_cast<std::size_t>(line) < about_.size()
               ? about_[static_cast<std::size_t>(line)].name
               : std::string();
}

long long FigureItem::length(int line) const
{
    (void)line;
    return static_cast<long long>(std::max(dataLength_, 1.0));
}

bool FigureItem::ready(int line) const
{
    return line >= 0 && static_cast<std::size_t>(line) < lines_.size();
}

plotcore::LineExtent FigureItem::extent(int line) const
{
    // The plot reports its common extent once, over every line drawn on the
    // common axis -- which is what the union of those lines' own would be --
    // and each line on an axis of its own reports its own.
    if (line >= 0 && static_cast<std::size_t>(line) < about_.size() &&
        about_[static_cast<std::size_t>(line)].separate) {
        return about_[static_cast<std::size_t>(line)].own;
    }
    return common_;
}

plotcore::Span FigureItem::xExtent(int line) const
{
    (void)line;
    return {xMinimum_, xMaximum_};
}

double FigureItem::xPositiveMinimum(int line) const
{
    (void)line;
    return xPositiveMinimum_;
}

void FigureItem::setPaneColumns(int columns)
{
    LineModel* plot = model();
    if (plot == nullptr || !active_ || columns == toldColumns_) {
        return;
    }
    toldColumns_ = columns;
    plot->setPaneColumns(columns);
}

void FigureItem::setView(double xLow, double xHigh, bool xLog)
{
    LineModel* plot = model();
    if (plot == nullptr || !active_) {
        return;
    }
    if (toldLog_ != xLog) {
        toldLog_ = xLog;
        if (plot->xLog() != xLog) {
            plot->setXLog(xLog);
        }
    }
    const std::pair<double, double> view{xLow, xHigh};
    if (toldView_ != view && std::isfinite(xLow) && std::isfinite(xHigh)) {
        toldView_ = view;
        plot->setVisibleRange(xLow, xHigh);
    }
}

void FigureItem::focus(const plotcore::GestureFocus& focus)
{
    if (LineModel* plot = model(); plot != nullptr && active_) {
        plot->setZoomFocus(focus.x, focus.factor);
    }
}

void FigureItem::clearFocus()
{
    if (LineModel* plot = model(); plot != nullptr && active_) {
        plot->clearZoomFocus();
    }
}

PlotLine FigureItem::drawLine(int line) const
{
    return ready(line) ? lines_[static_cast<std::size_t>(line)] : PlotLine{};
}

PlotAxis FigureItem::drawAxis(int line) const
{
    (void)line;
    return axis_;
}

// --- asked of it ------------------------------------------------------------------------

void FigureItem::refill()
{
    LineModel* plot = model();
    if (plot == nullptr || !active_ || !plot->hasData()) {
        clearLines();
        return;
    }
    plot->fill(this);
}

void FigureItem::resetView()
{
    figure_.resetView();
    moved();
}

void FigureItem::setViewRange(double x0, double x1, double y0, double y1)
{
    figure_.setViewRange(x0, x1, y0, y1);
    moved();
}

double FigureItem::dataXAt(double px, double py) const
{
    return figure_.camera().dataXAt(px, py);
}

double FigureItem::dataYAt(double py, double px) const
{
    return figure_.camera().dataYAt(py, px);
}

QColor FigureItem::seriesColor(int series, int position, int count, bool paper) const
{
    std::optional<plotcore::Rgba> own;
    if (LineModel* plot = model(); plot != nullptr) {
        const QVariant chosen = plot->seriesOverride(series);
        if (chosen.isValid() && !chosen.isNull()) {
            own = rgbaOf(chosen.value<QColor>());
        }
    }
    return colourOf(plotcore::seriesColor(settings_.colours, series, position, count, paper,
                                          settings_.dark, own));
}

QVariantMap FigureItem::readingAt(double x, double y)
{
    figure_.hover(x, y);
    compose();
    return reading();
}

QVariantList FigureItem::bandLabels() const
{
    QVariantList out;
    for (const plotcore::BandLabel& label : scene_.bandLabels) {
        out.append(QVariantMap{{QStringLiteral("key"), QString::fromStdString(label.key)},
                               {QStringLiteral("text"), QString::fromStdString(label.text)},
                               {QStringLiteral("x"), label.box.x},
                               {QStringLiteral("y"), label.box.y},
                               {QStringLiteral("width"), label.box.width},
                               {QStringLiteral("height"), label.box.height},
                               {QStringLiteral("turn"), label.turn}});
    }
    return out;
}

QVariantList FigureItem::marks() const
{
    QVariantList out;
    const auto add = [&](const Mark& mark, bool under) {
        out.append(QVariantMap{{QStringLiteral("kind"), static_cast<int>(mark.kind)},
                               {QStringLiteral("layer"), static_cast<int>(mark.layer)},
                               {QStringLiteral("under"), under},
                               {QStringLiteral("clipped"), mark.clipped},
                               {QStringLiteral("x0"), mark.from.x()},
                               {QStringLiteral("y0"), mark.from.y()},
                               {QStringLiteral("x1"), mark.to.x()},
                               {QStringLiteral("y1"), mark.to.y()},
                               {QStringLiteral("fill"), colourOf(mark.fill)},
                               {QStringLiteral("stroke"), colourOf(mark.stroke)},
                               {QStringLiteral("width"), mark.width},
                               {QStringLiteral("radius"), mark.radius},
                               {QStringLiteral("text"), QString::fromStdString(mark.text.text)},
                               {QStringLiteral("role"), static_cast<int>(mark.text.role)},
                               {QStringLiteral("colour"), colourOf(mark.text.colour)},
                               {QStringLiteral("turn"), mark.text.turn}});
    };
    for (const Mark& mark : scene_.under) {
        add(mark, true);
    }
    for (const Mark& mark : scene_.over) {
        add(mark, false);
    }
    return out;
}

QImage FigureItem::picture(double width, double height, double scale, bool publication, bool cursor)
{
    plotcore::SceneRequest request;
    request.geometry.runs = true;
    request.geometry.triangles = false;
    request.geometry.strips = false;
    request.geometry.values = false;
    request.picture = true;
    request.width = width;
    request.height = height;
    request.pixelRatio = scale;
    request.paper = publication;
    request.transparent = publication;
    if (cursor && scene_.reading.valid) {
        request.reading =
            plotcore::PinnedSample{scene_.reading.line, scene_.reading.x, scene_.reading.y};
    }
    const plotcore::Scene picture = figure_.scene(request);
    QImage image(std::max(1, static_cast<int>(std::lround(width * scale))),
                 std::max(1, static_cast<int>(std::lround(height * scale))),
                 QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(scale);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    paintScene(painter, picture, fonts_);
    return image;
}

// --- properties ----------------------------------------------------------------------------

void FigureItem::setPlot(QObject* plot)
{
    if (plotObject_ == plot) {
        return;
    }
    if (plotObject_ != nullptr) {
        disconnect(plotObject_, nullptr, this, nullptr);
    }
    plotObject_ = plot;
    toldView_.reset();
    toldLog_.reset();
    toldColumns_ = -1;
    moved_.clear();
    if (plotObject_ != nullptr) {
        // Both plots announce a change of lines as `changed` and a change of
        // where their points sit along x as `xAxisChanged`; either is a refill.
        connect(plotObject_, SIGNAL(changed()), &refillSoon_, SLOT(start()));
        connect(plotObject_, SIGNAL(xAxisChanged()), &refillSoon_, SLOT(start()));
    }
    emit plotChanged();
    refillSoon_.start();
}

void FigureItem::setActive(bool active)
{
    if (active_ == active) {
        return;
    }
    active_ = active;
    toldView_.reset();
    toldLog_.reset();
    toldColumns_ = -1;
    emit activeChanged();
    refillSoon_.start();
}

void FigureItem::setXMinimum(double value)
{
    if (xMinimum_ != value) {
        xMinimum_ = value;
        emit boundsChanged();
        dirty();
    }
}

void FigureItem::setXMaximum(double value)
{
    if (xMaximum_ != value) {
        xMaximum_ = value;
        emit boundsChanged();
        dirty();
    }
}

void FigureItem::setXPositiveMinimum(double value)
{
    if (xPositiveMinimum_ != value) {
        xPositiveMinimum_ = value;
        emit boundsChanged();
        dirty();
    }
}

void FigureItem::setDataLength(double value)
{
    if (dataLength_ != value) {
        dataLength_ = value;
        emit boundsChanged();
        dirty();
    }
}

QString FigureItem::grid() const
{
    switch (settings_.grid) {
    case plotcore::GridMode::None:
        return QStringLiteral("none");
    case plotcore::GridMode::Dense:
        return QStringLiteral("dense");
    case plotcore::GridMode::Custom:
        return QStringLiteral("custom");
    case plotcore::GridMode::Loose:
        break;
    }
    return QStringLiteral("loose");
}

void FigureItem::setGrid(const QString& grid)
{
    settings_.grid = grid == QStringLiteral("none")     ? plotcore::GridMode::None
                     : grid == QStringLiteral("dense")  ? plotcore::GridMode::Dense
                     : grid == QStringLiteral("custom") ? plotcore::GridMode::Custom
                                                        : plotcore::GridMode::Loose;
    applySettings();
}

void FigureItem::setGridStepX(double step)
{
    settings_.gridStepX = step;
    applySettings();
}

void FigureItem::setGridStepY(double step)
{
    settings_.gridStepY = step;
    applySettings();
}

void FigureItem::setXLog(bool on)
{
    settings_.xLog = on;
    applySettings();
}

void FigureItem::setYLog(bool on)
{
    settings_.yLog = on;
    applySettings();
}

void FigureItem::setXLogBase(double base)
{
    settings_.xLogBase = base;
    applySettings();
}

void FigureItem::setYLogBase(double base)
{
    settings_.yLogBase = base;
    applySettings();
}

void FigureItem::setFlipped(bool on)
{
    settings_.flipped = on;
    applySettings();
}

void FigureItem::setMinorNumbers(bool on)
{
    settings_.minorNumbers = on;
    applySettings();
}

void FigureItem::setMarkers(bool on)
{
    settings_.markers = on;
    applySettings();
}

void FigureItem::setCrosshair(bool on)
{
    settings_.cursor = on;
    applySettings();
}

void FigureItem::setTitle(const QString& title)
{
    settings_.title = title.toStdString();
    applySettings();
}

void FigureItem::setXLabel(const QString& label)
{
    settings_.xLabel = label.toStdString();
    applySettings();
}

void FigureItem::setYLabel(const QString& label)
{
    settings_.yLabel = label.toStdString();
    applySettings();
}

void FigureItem::setLegend(bool on)
{
    settings_.legend = on;
    applySettings();
}

QString FigureItem::legendCorner() const
{
    switch (settings_.legendCorner) {
    case plotcore::Corner::TopLeft:
        return QStringLiteral("top-left");
    case plotcore::Corner::BottomLeft:
        return QStringLiteral("bottom-left");
    case plotcore::Corner::BottomRight:
        return QStringLiteral("bottom-right");
    case plotcore::Corner::TopRight:
        break;
    }
    return QStringLiteral("top-right");
}

void FigureItem::setLegendCorner(const QString& corner)
{
    // plotcore's spelling, or the viewer's own (PlotSurface.legendCorner).
    const QString plain = corner.toLower().remove(QLatin1Char('-'));
    settings_.legendCorner = plain == QStringLiteral("topleft")      ? plotcore::Corner::TopLeft
                             : plain == QStringLiteral("bottomleft") ? plotcore::Corner::BottomLeft
                             : plain == QStringLiteral("bottomright")
                                 ? plotcore::Corner::BottomRight
                                 : plotcore::Corner::TopRight;
    applySettings();
}

void FigureItem::setColourMode(const QString& mode)
{
    settings_.colours.mode = mode.toStdString();
    applySettings();
}

void FigureItem::setColourSingle(const QColor& colour)
{
    colourSingle_ = colour;
    applySettings();
}

void FigureItem::setColourRangeFrom(const QColor& colour)
{
    colourRangeFrom_ = colour;
    applySettings();
}

void FigureItem::setColourRangeTo(const QColor& colour)
{
    colourRangeTo_ = colour;
    applySettings();
}

void FigureItem::setColoursReversed(bool on)
{
    settings_.colours.reversed = on;
    applySettings();
}

void FigureItem::setColourFrom(double at)
{
    settings_.colours.from = at;
    applySettings();
}

void FigureItem::setColourTo(double at)
{
    settings_.colours.to = at;
    applySettings();
}

void FigureItem::setColoursCustom(bool on)
{
    settings_.colours.custom = on;
    applySettings();
}

void FigureItem::setHighlighted(int series)
{
    settings_.highlighted = series;
    applySettings();
}

void FigureItem::setBundle(bool on)
{
    settings_.bundle = on;
    applySettings();
}

void FigureItem::setDark(bool on)
{
    settings_.dark = on;
    applySettings();
}

void FigureItem::setStacked(bool on)
{
    settings_.stacked = on;
    applySettings();
}

void FigureItem::setReadout(bool on)
{
    settings_.readout = on;
    applySettings();
}

void FigureItem::setBandReadout(bool on)
{
    settings_.bandReadout = on;
    applySettings();
}

void FigureItem::setPaneMessage(bool on)
{
    settings_.paneMessage = on;
    applySettings();
}

void FigureItem::setLineGestures(bool on)
{
    settings_.lineGestures = on;
    applySettings();
}

void FigureItem::applySettings()
{
    // A colour nobody chose is not a colour: the figure draws the ground's
    // default until one is (LineColours::custom).
    if (colourSingle_.isValid()) {
        settings_.colours.single = rgbaOf(colourSingle_);
    }
    if (colourRangeFrom_.isValid()) {
        settings_.colours.rangeFrom = rgbaOf(colourRangeFrom_);
    }
    if (colourRangeTo_.isValid()) {
        settings_.colours.rangeTo = rgbaOf(colourRangeTo_);
    }
    const plotcore::FigureSettings before = figure_.settings();
    figure_.setSettings(settings_);
    // The figure refuses a base that is not one, and puts the view back on a
    // change of scale; what it holds is what is in force.
    settings_ = figure_.settings();
    emit settingsChanged();
    if (before.xLog != settings_.xLog || before.yLog != settings_.yLog ||
        before.flipped != settings_.flipped || before.xLogBase != settings_.xLogBase ||
        before.yLogBase != settings_.yLogBase) {
        emit viewChanged();
    }
    dirty();
}

void FigureItem::setFace(std::size_t slot, const QFont& font)
{
    fonts_.faces[slot] = font;
    applyFonts();
    emit fontsChanged();
    dirty();
}

void FigureItem::applyFonts()
{
    // Without fonts the figure measures by its own rule of thumb, which is
    // what a headless figure in plotcore's own tests does too.
    if (!hasFonts()) {
        figure_.setMeasure({});
        return;
    }
    // Read the faces when asked rather than captured now, so a face changed
    // later is the one the next frame is laid out in.
    plotcore::TextMeasure measure;
    measure.width = [this](const std::string& text, plotcore::TextRole role) {
        return QFontMetricsF(fonts_.of(role)).horizontalAdvance(QString::fromStdString(text));
    };
    measure.height = [this](plotcore::TextRole role) {
        return QFontMetricsF(fonts_.of(role)).height();
    };
    figure_.setMeasure(std::move(measure));
}

double FigureItem::zoomX() const
{
    return figure_.camera().x.view.zoom;
}

void FigureItem::setZoomX(double zoom)
{
    if (zoom >= 1.0 && std::isfinite(zoom) && zoom != zoomX()) {
        figure_.camera().x.view.zoom = zoom;
        moved();
    }
}

double FigureItem::panX() const
{
    return figure_.camera().x.view.pan;
}

void FigureItem::setPanX(double pan)
{
    if (std::isfinite(pan) && pan != panX()) {
        figure_.camera().x.view.pan = pan;
        moved();
    }
}

double FigureItem::zoomY() const
{
    return figure_.camera().y.view.zoom;
}

void FigureItem::setZoomY(double zoom)
{
    if (zoom >= 1.0 && std::isfinite(zoom) && zoom != zoomY()) {
        figure_.camera().y.view.zoom = zoom;
        moved();
    }
}

double FigureItem::panY() const
{
    return figure_.camera().y.view.pan;
}

void FigureItem::setPanY(double pan)
{
    if (std::isfinite(pan) && pan != panY()) {
        figure_.camera().y.view.pan = pan;
        moved();
    }
}

double FigureItem::viewMinX() const
{
    return figure_.camera().viewMinX();
}

double FigureItem::viewMaxX() const
{
    return figure_.camera().viewMaxX();
}

double FigureItem::viewMinY() const
{
    return figure_.camera().viewMinY();
}

double FigureItem::viewMaxY() const
{
    return figure_.camera().viewMaxY();
}

bool FigureItem::zoomed() const
{
    return figure_.camera().zoomed();
}

QRectF FigureItem::area() const
{
    return {scene_.area.x, scene_.area.y, scene_.area.width, scene_.area.height};
}

QVariantMap FigureItem::reading() const
{
    const plotcore::Reading& reading = scene_.reading;
    if (!reading.valid) {
        return {{QStringLiteral("valid"), false}};
    }
    return {{QStringLiteral("valid"), true},
            {QStringLiteral("line"), reading.line},
            {QStringLiteral("series"), series(reading.line)},
            {QStringLiteral("x"), reading.x},
            {QStringLiteral("y"), reading.y},
            {QStringLiteral("px"), reading.at.x()},
            {QStringLiteral("py"), reading.at.y()},
            {QStringLiteral("xText"), QString::fromStdString(reading.xText)},
            {QStringLiteral("yText"), QString::fromStdString(reading.yText)}};
}

void FigureItem::setHovered(bool on)
{
    if (hovered_ != on) {
        hovered_ = on;
        emit hoveredChanged();
    }
}

// --- the frame -----------------------------------------------------------------------------

void FigureItem::dirty()
{
    polish();
    update();
}

void FigureItem::moved()
{
    emit viewChanged();
    dirty();
}

void FigureItem::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        dirty();
    }
}

void FigureItem::compose()
{
    const double ratio = window() != nullptr ? window()->effectiveDevicePixelRatio() : 1.0;
    figure_.setSize(std::max(width(), 1.0), std::max(height(), 1.0), ratio);
    const bool software =
        window() != nullptr && window()->rendererInterface() != nullptr &&
        window()->rendererInterface()->graphicsApi() == QSGRendererInterface::Software;
    // The forms of a line this item draws, and no others: strips for the
    // geometry, polylines for QPainter, and no values in the data's units,
    // which are for a host whose axes are its own.
    plotcore::SceneRequest request;
    request.geometry.runs = software || window() == nullptr;
    request.geometry.strips = !software;
    request.geometry.triangles = false;
    request.geometry.values = false;
    const bool wasZoomed = zoomed();
    const double was[4] = {viewMinX(), viewMaxX(), viewMinY(), viewMaxY()};
    scene_ = figure_.scene(request);
    drawnPoints_ = 0;
    drawnRuns_ = 0;
    for (const plotcore::DrawnLine& line : scene_.lines) {
        const auto& strokes = line.runs.empty() ? line.strips : line.runs;
        drawnRuns_ += static_cast<int>(strokes.size());
        for (const auto& stroke : strokes) {
            drawnPoints_ += static_cast<int>(line.runs.empty() ? stroke.size() / 2 : stroke.size());
        }
    }
    // Laying a frame out settles the axes' bounds, and the window with them.
    if (wasZoomed != zoomed() || was[0] != viewMinX() || was[1] != viewMaxX() ||
        was[2] != viewMinY() || was[3] != viewMaxY()) {
        emit viewChanged();
    }
    emit drew();
}

void FigureItem::updatePolish()
{
    compose();
}

QSGNode* FigureItem::updatePaintNode(QSGNode* old, UpdatePaintNodeData*)
{
    QSGNode* root = old != nullptr ? old : new QSGNode;
    // Built again every frame. A frame is a few hundred nodes, and a node that
    // is reused has to be emptied and refilled, which Qt's batch renderer has
    // been known to forget to draw again (PlotItem::buildGeometry says how);
    // a node that is new is always drawn.
    dropChildren(root);
    const bool software =
        window() != nullptr && window()->rendererInterface() != nullptr &&
        window()->rendererInterface()->graphicsApi() == QSGRendererInterface::Software;
    return software ? buildPainted(root) : buildNodes(root);
}

QSGNode* FigureItem::buildPainted(QSGNode* root)
{
    QQuickWindow* target = window();
    const qreal ratio = target != nullptr ? target->effectiveDevicePixelRatio() : 1.0;
    QImage image(std::max(1, static_cast<int>(std::lround(width() * ratio))),
                 std::max(1, static_cast<int>(std::lround(height() * ratio))),
                 QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(ratio);
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        paintScene(painter, scene_, fonts_);
    }
    if (target == nullptr) {
        return root;
    }
    auto* node = new QSGSimpleTextureNode;
    node->setOwnsTexture(true);
    node->setFiltering(QSGTexture::Linear);
    // A texture a frame, which would be indefensible on the path that draws
    // for a reader and is nothing on this one: the software renderer is what
    // a headless run gets, and a headless run draws each frame once.
    node->setTexture(target->createTextureFromImage(image, QQuickWindow::TextureHasAlphaChannel));
    node->setRect(0.0, 0.0, width(), height());
    root->appendChildNode(node);
    return root;
}

QSGNode* FigureItem::buildNodes(QSGNode* root)
{
    QQuickWindow* target = window();
    if (target == nullptr) {
        return root;
    }
    const qreal ratio = target->effectiveDevicePixelRatio();
    const QRectF pane = area();

    const auto clipNode = [&]() {
        auto* clip = new QSGClipNode;
        auto* geometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 4);
        geometry->setDrawingMode(QSGGeometry::DrawTriangleStrip);
        QSGGeometry::updateRectGeometry(geometry, pane);
        clip->setGeometry(geometry);
        clip->setFlag(QSGNode::OwnsGeometry);
        clip->setIsRectangular(true);
        clip->setClipRect(pane);
        return clip;
    };

    const auto rectangle = [&](QSGNode* parent, const QRectF& rect, const QColor& colour) {
        if (rect.width() <= 0.0 || rect.height() <= 0.0 || colour.alpha() == 0) {
            return;
        }
        QSGRectangleNode* node = target->createRectangleNode();
        node->setRect(rect);
        node->setColor(colour);
        parent->appendChildNode(node);
    };

    // A mark that is not a plain rectangle -- a veil with rounded corners, a
    // ring -- is painted with QPainter into an image of its own size. There
    // are a handful of them in a frame, against the hundreds of rules and
    // numbers that are nodes of their own.
    const auto painted = [&](QSGNode* parent, const Mark& mark, const QRectF& bounds) {
        const QRectF outside = bounds.adjusted(-1.0, -1.0, 1.0, 1.0);
        QImage image(std::max(1, static_cast<int>(std::ceil(outside.width() * ratio))),
                     std::max(1, static_cast<int>(std::ceil(outside.height() * ratio))),
                     QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(ratio);
        image.fill(Qt::transparent);
        {
            QPainter painter(&image);
            painter.translate(-outside.topLeft());
            Mark unclipped = mark;
            unclipped.clipped = false;
            paintMark(painter, unclipped, fonts_, pane);
        }
        QSGImageNode* node = target->createImageNode();
        node->setTexture(
            target->createTextureFromImage(image, QQuickWindow::TextureHasAlphaChannel));
        node->setOwnsTexture(true);
        node->setFiltering(QSGTexture::Linear);
        node->setRect(
            QRectF(outside.topLeft(), QSizeF(image.width() / ratio, image.height() / ratio)));
        parent->appendChildNode(node);
    };

    const auto text = [&](QSGNode* parent, const plotcore::TextItem& item) {
        const TextPlacement placed = placeText(item, fonts_);
        QSGTextNode* node = target->createTextNode();
        node->setColor(colourOf(item.colour));
        QMatrix4x4 matrix;
        matrix.translate(static_cast<float>(item.at.x()), static_cast<float>(item.at.y()));
        matrix.rotate(static_cast<float>(item.turn), 0.0F, 0.0F, 1.0F);
        node->setMatrix(matrix);
        const auto lay = [&](const QString& written, const QFont& face, QPointF at) {
            QTextLayout layout(written, face);
            layout.beginLayout();
            QTextLine line = layout.createLine();
            line.setPosition(QPointF(0.0, 0.0));
            layout.endLayout();
            node->addTextLayout(at, &layout);
        };
        lay(QString::fromStdString(item.base.empty() ? item.text : item.base), fonts_.of(item.role),
            QPointF(placed.baseLeft, placed.baseTop));
        if (!item.exponent.empty()) {
            lay(QString::fromStdString(item.exponent), fonts_.of(plotcore::TextRole::TickMinor),
                QPointF(placed.exponentLeft, placed.exponentTop));
        }
        parent->appendChildNode(node);
    };

    const auto segment = [&](QSGNode* parent, const Mark& mark) {
        std::vector<plotcore::Point> strip;
        const plotcore::Point ends[2] = {mark.from, mark.to};
        plotcore::strokeRunInto(ends, 2, mark.width,
                                [&](double x, double y) { strip.emplace_back(x, y); });
        auto* node = new QSGGeometryNode;
        auto* geometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(),
                                         static_cast<int>(strip.size()));
        geometry->setDrawingMode(QSGGeometry::DrawTriangleStrip);
        QSGGeometry::Point2D* vertex = geometry->vertexDataAsPoint2D();
        for (std::size_t i = 0; i < strip.size(); ++i) {
            vertex[i].set(static_cast<float>(strip[i].x()), static_cast<float>(strip[i].y()));
        }
        node->setGeometry(geometry);
        node->setFlag(QSGNode::OwnsGeometry);
        auto* material = new QSGFlatColorMaterial;
        material->setColor(colourOf(mark.stroke));
        node->setMaterial(material);
        node->setFlag(QSGNode::OwnsMaterial);
        parent->appendChildNode(node);
    };

    const auto addMarks = [&](const std::vector<Mark>& marks) {
        // Consecutive clipped marks share one clip node; the order they are
        // appended in is the order they are painted in.
        QSGNode* clipped = nullptr;
        for (const Mark& mark : marks) {
            QSGNode* parent = root;
            if (mark.clipped) {
                if (clipped == nullptr) {
                    clipped = clipNode();
                    root->appendChildNode(clipped);
                }
                parent = clipped;
            }
            else {
                clipped = nullptr;
            }
            const QRectF bounds(QPointF(mark.from.x(), mark.from.y()),
                                QPointF(mark.to.x(), mark.to.y()));
            switch (mark.kind) {
            case Mark::Kind::Box:
                if (mark.radius > 0.0) {
                    painted(parent, mark, bounds);
                    break;
                }
                rectangle(parent, bounds, colourOf(mark.fill));
                if (mark.width > 0.0 && !mark.dashed) {
                    // Edged inside its bounds: four rectangles along them.
                    const double w = mark.width;
                    const QColor edge = colourOf(mark.stroke);
                    rectangle(parent, QRectF(bounds.left(), bounds.top(), bounds.width(), w), edge);
                    rectangle(parent, QRectF(bounds.left(), bounds.bottom() - w, bounds.width(), w),
                              edge);
                    rectangle(parent,
                              QRectF(bounds.left(), bounds.top() + w, w, bounds.height() - 2 * w),
                              edge);
                    rectangle(
                        parent,
                        QRectF(bounds.right() - w, bounds.top() + w, w, bounds.height() - 2 * w),
                        edge);
                }
                else if (mark.width > 0.0) {
                    Mark edge = mark;
                    edge.fill = plotcore::Rgba::of(0.0, 0.0, 0.0, 0.0);
                    painted(parent, edge, bounds);
                }
                break;
            case Mark::Kind::Circle: {
                const double reach = mark.radius + mark.width;
                painted(parent, mark,
                        QRectF(mark.from.x() - reach, mark.from.y() - reach, 2 * reach, 2 * reach));
                break;
            }
            case Mark::Kind::Text:
                text(parent, mark.text);
                break;
            case Mark::Kind::Segment:
                if (mark.width > 0.0) {
                    segment(parent, mark);
                }
                break;
            }
        }
    };

    if (scene_.ground.alpha > 0.0) {
        rectangle(root, QRectF(0.0, 0.0, scene_.width, scene_.height), colourOf(scene_.ground));
    }
    addMarks(scene_.under);

    // The lines: one geometry node of triangle strips, inside the pane. Every
    // break between two strips -- two runs of a line, two lines, two markers
    // -- is bridged by a repeated pair of vertices, which make two triangles
    // of no area and rasterise to nothing.
    int vertices = 0;
    int strips = 0;
    for (const plotcore::DrawnLine& line : scene_.lines) {
        for (const auto& strip : line.strips) {
            if (!strip.empty()) {
                vertices += static_cast<int>(strip.size());
                ++strips;
            }
        }
        strips += static_cast<int>(line.markers.size());
        vertices += plotcore::kMarkerSides * static_cast<int>(line.markers.size());
    }
    if (strips > 0) {
        vertices += 2 * (strips - 1);
        auto* node = new QSGGeometryNode;
        auto* geometry = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), vertices);
        geometry->setDrawingMode(QSGGeometry::DrawTriangleStrip);
        node->setGeometry(geometry);
        node->setFlag(QSGNode::OwnsGeometry);
        node->setMaterial(new QSGVertexColorMaterial);
        node->setFlag(QSGNode::OwnsMaterial);
        auto* vertex = geometry->vertexDataAsColoredPoint2D();
        int at = 0;
        const auto put = [&](const QSGGeometry::ColoredPoint2D& written) {
            if (at < vertices) {
                vertex[at++] = written;
            }
        };
        bool started = false;
        const auto stripOf = [&](const std::vector<plotcore::Point>& points,
                                 const plotcore::Rgba& colour) {
            if (points.empty()) {
                return;
            }
            if (started) {
                put(vertex[at - 1]);
                put(vertexOf(points.front(), colour));
            }
            for (const plotcore::Point& point : points) {
                put(vertexOf(point, colour));
            }
            started = true;
        };
        for (const plotcore::DrawnLine& line : scene_.lines) {
            for (const auto& strip : line.strips) {
                stripOf(strip, line.colour);
            }
        }
        // The markers after every line, so a dot is never under a stroke it
        // belongs to.
        std::vector<plotcore::Point> marker;
        for (const plotcore::DrawnLine& line : scene_.lines) {
            for (const plotcore::Point& centre : line.markers) {
                marker.clear();
                plotcore::markerAt(centre, line.markerRadius, marker);
                stripOf(marker, line.colour);
            }
        }
        while (at < vertices) {
            vertex[at] = vertex[at > 0 ? at - 1 : 0];
            ++at;
        }
        QSGClipNode* clip = clipNode();
        clip->appendChildNode(node);
        root->appendChildNode(clip);
    }

    addMarks(scene_.over);
    return root;
}

// --- gestures -------------------------------------------------------------------------------

bool FigureItem::takesGestures() const
{
    // Nothing to look at is nothing to zoom, and a pane explaining why it
    // cannot draw takes no gestures (PlotSurface's gesture layer).
    return !lines_.empty() && scene_.message.empty();
}

void FigureItem::mousePressEvent(QMouseEvent* event)
{
    const std::optional<plotcore::Button> button = buttonOf(event->button());
    if (!button.has_value() || !takesGestures()) {
        event->ignore();
        return;
    }
    const QPointF at = event->position();
    figure_.press(at.x(), at.y(), *button, modifiersOf(event->modifiers()));
    if (*button == plotcore::Button::Left) {
        QQuickItem::setCursor(Qt::ClosedHandCursor);
    }
    event->accept();
    dirty();
}

void FigureItem::mouseMoveEvent(QMouseEvent* event)
{
    const QPointF at = event->position();
    figure_.move(at.x(), at.y(), modifiersOf(event->modifiers()));
    event->accept();
    moved();
}

void FigureItem::mouseReleaseEvent(QMouseEvent* event)
{
    const std::optional<plotcore::Button> button = buttonOf(event->button());
    if (!button.has_value()) {
        event->ignore();
        return;
    }
    const QPointF at = event->position();
    figure_.release(at.x(), at.y(), *button, modifiersOf(event->modifiers()));
    QQuickItem::setCursor(Qt::OpenHandCursor);
    // What a gesture did to a line is kept by its series.
    for (std::size_t i = 0; i < about_.size(); ++i) {
        const plotcore::LineSettings held = figure_.lineSettings(static_cast<int>(i));
        if (held.scale != 1.0 || held.shift != 0.0) {
            moved_[about_[i].series] = {held.scale, held.shift};
        }
        else {
            moved_.erase(about_[i].series);
        }
    }
    event->accept();
    moved();
}

void FigureItem::mouseDoubleClickEvent(QMouseEvent* event)
{
    const std::optional<plotcore::Button> button = buttonOf(event->button());
    if (!button.has_value() || !takesGestures()) {
        event->ignore();
        return;
    }
    const QPointF at = event->position();
    figure_.doubleClick(at.x(), at.y(), *button);
    event->accept();
    moved();
}

void FigureItem::wheelEvent(QWheelEvent* event)
{
    if (!takesGestures()) {
        event->ignore();
        return;
    }
    // One notch is 120 eighths of a degree. Held down, Shift turns a wheel's
    // vertical delta into a horizontal one on several platforms, so the
    // horizontal one is read when there is no vertical one.
    const QPoint delta = event->angleDelta();
    const double turned = delta.y() != 0 ? delta.y() : delta.x();
    const QPointF at = event->position();
    figure_.wheel(at.x(), at.y(), turned, modifiersOf(event->modifiers()));
    event->accept();
    moved();
}

void FigureItem::hoverEnterEvent(QHoverEvent* event)
{
    hoverMoveEvent(event);
}

void FigureItem::hoverMoveEvent(QHoverEvent* event)
{
    const QPointF at = event->position();
    figure_.hover(at.x(), at.y());
    setHovered(area().contains(at));
    dirty();
}

void FigureItem::hoverLeaveEvent(QHoverEvent* event)
{
    (void)event;
    figure_.leave();
    setHovered(false);
    dirty();
}

} // namespace gui
