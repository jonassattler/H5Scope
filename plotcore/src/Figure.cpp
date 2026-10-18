// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-FileCopyrightText: 2026 Andreas Martin
// SPDX-License-Identifier: GPL-3.0-only

#include "plotcore/Figure.hpp"

#include "Json.hpp"
#include "plotcore/Format.hpp"
#include "plotcore/Projection.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

// The frame is PlotFrame.qml's, the gestures PlotSurface.qml's; where this
// file lays something out it names the property of the QML it follows. The
// numbers are FigureStyle's, which are Theme's.

namespace plotcore {
namespace {

constexpr double kInfinity = std::numeric_limits<double>::infinity();

/// What a pane `pixels` device pixels across is read against: down to the
/// quantum, as PaneColumns rounds it, and within what a line may be thinned to.
int columnsOf(double pixels)
{
    const int columns = static_cast<int>(std::max(pixels, 0.0));
    return std::clamp((columns / kColumnQuantum) * kColumnQuantum, kMinPoints / 2, kMaxPoints / 2);
}

const char* gridName(GridMode mode)
{
    switch (mode) {
    case GridMode::None:
        return "none";
    case GridMode::Dense:
        return "dense";
    case GridMode::Custom:
        return "custom";
    case GridMode::Loose:
        break;
    }
    return "loose";
}

GridMode gridOf(const std::string& name)
{
    if (name == "none") {
        return GridMode::None;
    }
    if (name == "dense") {
        return GridMode::Dense;
    }
    if (name == "custom") {
        return GridMode::Custom;
    }
    return GridMode::Loose;
}

const char* cornerName(Corner corner)
{
    switch (corner) {
    case Corner::TopLeft:
        return "top-left";
    case Corner::BottomLeft:
        return "bottom-left";
    case Corner::BottomRight:
        return "bottom-right";
    case Corner::TopRight:
        break;
    }
    return "top-right";
}

Corner cornerOf(const std::string& name)
{
    if (name == "top-left") {
        return Corner::TopLeft;
    }
    if (name == "bottom-left") {
        return Corner::BottomLeft;
    }
    if (name == "bottom-right") {
        return Corner::BottomRight;
    }
    return Corner::TopRight;
}

/// A triangle strip, as strokeRunInto() emits it, as a list of triangles.
void trianglesOf(const std::vector<Point>& strip, std::vector<Point>& out)
{
    for (std::size_t i = 0; i + 2 < strip.size(); ++i) {
        out.push_back(strip[i]);
        out.push_back(strip[i + 1]);
        out.push_back(strip[i + 2]);
    }
}

} // namespace

// --- the style ----------------------------------------------------------------

double FigureStyle::pixelsOf(TextRole role) const
{
    switch (role) {
    case TextRole::TickMinor:
        return tickMinorPixels;
    case TextRole::Title:
        return titlePixels;
    case TextRole::AxisName:
        return axisNamePixels;
    case TextRole::Legend:
        return legendPixels;
    case TextRole::Tick:
        break;
    }
    return tickPixels;
}

TextMeasure FigureStyle::approximateMeasure() const
{
    const FigureStyle style = *this;
    TextMeasure measure;
    measure.width = [style](const std::string& text, TextRole role) {
        return 0.6 * style.pixelsOf(role) * textLength(text);
    };
    measure.height = [style](TextRole role) { return 1.2 * style.pixelsOf(role); };
    return measure;
}

// --- one line's own y ---------------------------------------------------------

/// The window a line is drawn in, when it is not the common one: in the
/// values, and on which scale.
struct Figure::LineWindow
{
    bool own = false;
    /// Whether it is numbered beside the pane: a line asked onto an axis of
    /// its own, or stacked. A moved or scaled line is not -- see Figure.hpp.
    bool axis = false;
    double low = 0.0;
    double high = 1.0;
    bool logarithmic = false;
    bool fixed = false;
    /// The window the line's scale and shift are measured against, in the
    /// axis's positions: the common axis's whole, the line's own padded
    /// extent, or its band.
    double baseLow = 0.0;
    double baseHigh = 1.0;
};

/// Everything the frame is laid out from.
struct Figure::Layout
{
    PaneRect area;
    TickSet leftTicks;
    TickSet footTicks;
    TickSet xTicks;
    TickSet yTicks;
    bool common = true;
    double tickHeight = 0.0;
    double minorHeight = 0.0;
    double nameHeight = 0.0;
    double titleHeight = 0.0;
    double sideWidth = 0.0;
    double commonWidth = 0.0;
    double footHeight = 0.0;

    struct Side
    {
        int line = -1;
        LineWindow window;
        Rgba colour;
        std::string label;
        TickSet ticks;
        double at = 0.0;
        double extent = 0.0;
        double widest = 0.0;
    };
    std::vector<Side> sides;
};

Figure::Figure()
{
    measure_ = style_.approximateMeasure();
}

void Figure::ensureLine(int line)
{
    if (line >= 0 && static_cast<std::size_t>(line) >= lines_.size()) {
        lines_.resize(static_cast<std::size_t>(line) + 1);
    }
}

LineSettings Figure::lineSettings(int line) const
{
    if (line < 0 || static_cast<std::size_t>(line) >= lines_.size()) {
        return {};
    }
    return lines_[static_cast<std::size_t>(line)];
}

void Figure::setLineSettings(int line, const LineSettings& settings)
{
    if (line < 0) {
        return;
    }
    ensureLine(line);
    LineSettings clean = settings;
    if (!(clean.scale > 0.0) || !std::isfinite(clean.scale)) {
        clean.scale = 1.0;
    }
    if (!std::isfinite(clean.shift)) {
        clean.shift = 0.0;
    }
    lines_[static_cast<std::size_t>(line)] = clean;
}

void Figure::setSettings(const FigureSettings& settings)
{
    FigureSettings next = settings;
    if (!usableBase(next.xLogBase)) {
        next.xLogBase = settings_.xLogBase;
    }
    if (!usableBase(next.yLogBase)) {
        next.yLogBase = settings_.yLogBase;
    }
    const bool reset = next.xLog != settings_.xLog || next.yLog != settings_.yLog ||
                       next.flipped != settings_.flipped ||
                       (next.xLog && next.xLogBase != settings_.xLogBase) ||
                       (next.yLog && next.yLogBase != settings_.yLogBase);
    settings_ = next;
    if (reset) {
        resetView();
    }
}

void Figure::setStyle(const FigureStyle& style)
{
    style_ = style;
    if (approximate_) {
        // A measure that needs no font is the style's own, so it follows it.
        measure_ = style_.approximateMeasure();
    }
}

void Figure::setMeasure(TextMeasure measure)
{
    approximate_ = !(measure.width && measure.height);
    measure_ = approximate_ ? style_.approximateMeasure() : std::move(measure);
}

void Figure::setSize(double width, double height, double pixelRatio)
{
    width_ = std::max(width, 1.0);
    height_ = std::max(height, 1.0);
    pixelRatio_ = pixelRatio > 0.0 ? pixelRatio : 1.0;
}

void Figure::resetView()
{
    camera_.reset();
}

void Figure::setViewRange(double x0, double x1, double y0, double y1)
{
    updateBounds();
    camera_.area = layout().area;
    camera_.setViewRange(x0, x1, y0, y1);
}

void Figure::removeLine(int line)
{
    plot_.removeLine(line);
    if (static_cast<std::size_t>(line) < lines_.size()) {
        lines_.erase(lines_.begin() + line);
    }
    if (settings_.highlighted == line) {
        settings_.highlighted = -1;
    }
    else if (settings_.highlighted > line) {
        --settings_.highlighted;
    }
    if (dragLine_ == line) {
        drag_ = Drag::None;
        dragLine_ = -1;
    }
    else if (dragLine_ > line) {
        --dragLine_;
    }
}

void Figure::clear()
{
    plot_.clear();
    lines_.clear();
    settings_.highlighted = -1;
    drag_ = Drag::None;
    dragLine_ = -1;
    camera_.reset();
}

std::vector<int> Figure::drawnLines() const
{
    std::vector<int> drawn;
    for (int i = 0; i < plot_.lineCount(); ++i) {
        if (lineSettings(i).visible && plot_.ready(i) && plot_.length(i) > 0) {
            drawn.push_back(i);
        }
    }
    return drawn;
}

bool Figure::commonLine(int line) const
{
    // A line on an axis of its own needs a second line to be beside: the
    // request is kept, so a second line brings the axis back. See CLAUDE.md.
    if (drawnLines().size() < 2) {
        return true;
    }
    const LineSettings own = lineSettings(line);
    return !(own.ownAxis || settings_.stacked || own.scale != 1.0 || own.shift != 0.0);
}

bool Figure::spansCommon(int line) const
{
    // A moved or scaled line is still a reading on the common axis's units --
    // its window is a share of that axis's whole -- so it stays in the whole.
    // Taking it out would move the whole under it, and the line with it, on
    // the gesture that was meant to hold the value under the pointer still.
    if (drawnLines().size() < 2) {
        return true;
    }
    return !(lineSettings(line).ownAxis || settings_.stacked);
}

void Figure::updateBounds()
{
    const std::vector<int> drawn = drawnLines();
    double xMin = kInfinity;
    double xMax = -kInfinity;
    double xPositive = 0.0;
    long long longest = 1;
    double yMin = kInfinity;
    double yMax = -kInfinity;
    double yPositive = 0.0;
    bool anyCommon = false;
    for (const int line : drawn) {
        const Span x = plot_.xExtent(line);
        xMin = std::min(xMin, x.low);
        xMax = std::max(xMax, x.high);
        const double positive = plot_.xPositiveMinimum(line);
        if (positive > 0.0 && (xPositive == 0.0 || positive < xPositive)) {
            xPositive = positive;
        }
        longest = std::max(longest, plot_.length(line));
        if (!spansCommon(line)) {
            continue;
        }
        const LineExtent extent = plot_.extent(line);
        if (extent.finite) {
            yMin = std::min(yMin, extent.minimum);
            yMax = std::max(yMax, extent.maximum);
            anyCommon = true;
        }
        if (extent.positive && (yPositive == 0.0 || extent.positiveMinimum < yPositive)) {
            yPositive = extent.positiveMinimum;
        }
    }
    if (!(xMax >= xMin)) {
        xMin = 0.0;
        xMax = 1.0;
    }
    if (xMax == xMin) {
        // One x has no span to be an axis over; the unit either side of it is
        // the honest window, as `padded` gives a flat line one.
        xMin -= 0.5;
        xMax += 0.5;
    }

    camera_.flipped = settings_.flipped;
    camera_.maxZoom = maxZoomFor(longest);
    camera_.x.logarithmic = settings_.xLog;
    camera_.x.base = settings_.xLogBase;
    camera_.x.bounds = xBounds(xMin, xMax, xPositive, settings_.xLog, settings_.xLogBase);
    camera_.x.minimumSpan = std::abs(xMax - xMin) / camera_.maxZoom;

    camera_.y.logarithmic = settings_.yLog;
    camera_.y.base = settings_.yLogBase;
    if (!anyCommon) {
        // No line on the common axis: a nominal one for the gestures to run on,
        // which the frame numbers, names and rules none of.
        camera_.y.bounds = settings_.yLog ? Span{1.0, settings_.yLogBase} : Span{0.0, 1.0};
    }
    else {
        camera_.y.bounds =
            padded(settings_.yLog ? yPositive : yMin, yMax, settings_.yLog, settings_.yLogBase);
    }
    camera_.y.minimumSpan = 0.0;
}

double Figure::commonFraction(double value) const
{
    const CameraAxis& y = camera_.y;
    const double from = axisPosition(y.bounds.low, y.logarithmic, y.base);
    const double to = axisPosition(y.bounds.high, y.logarithmic, y.base);
    if (!(to > from)) {
        return 0.0;
    }
    return (axisPosition(value, y.logarithmic, y.base) - from) / (to - from);
}

Figure::LineWindow Figure::windowOf(int line, int position, int drawn, bool always) const
{
    LineWindow window;
    if (!always && commonLine(line)) {
        return window;
    }
    const LineSettings own = lineSettings(line);
    const bool yLog = settings_.yLog;
    const double base = settings_.yLogBase;
    const LineExtent extent = plot_.extent(line);

    double low = 0.0;
    double high = 1.0;
    if (own.ownAxis && !settings_.stacked) {
        // The line alone, and linear: the axis it would have if it were the
        // only one on the plot.
        const Span whole =
            extent.finite ? padded(extent.minimum, extent.maximum, false, 10.0) : Span{0.0, 1.0};
        window.logarithmic = false;
        low = whole.low;
        high = whole.high;
    }
    else if (settings_.stacked) {
        // Its own extent, on the common scale, in an equal band of the pane:
        // the first line drawn at the top.
        Span whole = Span{0.0, 1.0};
        if (yLog && extent.positive) {
            whole = padded(extent.positiveMinimum, extent.maximum, true, base);
        }
        else if (!yLog && extent.finite) {
            whole = padded(extent.minimum, extent.maximum, false, base);
        }
        else if (yLog) {
            whole = Span{1.0, base};
        }
        window.logarithmic = yLog;
        const double from = axisPosition(whole.low, yLog, base);
        const double to = axisPosition(whole.high, yLog, base);
        const double bandLow = 1.0 - static_cast<double>(position + 1) / drawn;
        const double bandHigh = 1.0 - static_cast<double>(position) / drawn;
        const double span = (to - from) / (bandHigh - bandLow);
        low = axisValue(from - bandLow * span, yLog, base);
        high = axisValue(from - bandLow * span + span, yLog, base);
    }
    else {
        // Moved or scaled on the common axis: a share of the common window,
        // on its scale.
        window.logarithmic = yLog;
        low = camera_.y.bounds.low;
        high = camera_.y.bounds.high;
    }

    const bool log = window.logarithmic;
    window.baseLow = axisPosition(low, log, base);
    window.baseHigh = axisPosition(high, log, base);
    const double centre = (window.baseLow + window.baseHigh) / 2.0;
    const double half = (window.baseHigh - window.baseLow) / 2.0 / own.scale;
    const double homeLow = centre - half + own.shift;
    const double homeSpan = 2.0 * half;
    window.fixed = own.axisFixed;
    const double bottom = window.fixed ? 0.0 : commonFraction(camera_.viewMinY());
    const double top = window.fixed ? 1.0 : commonFraction(camera_.viewMaxY());
    window.own = true;
    window.axis = own.ownAxis || settings_.stacked;
    window.low = axisValue(homeLow + bottom * homeSpan, log, base);
    window.high = axisValue(homeLow + top * homeSpan, log, base);
    return window;
}

// --- the frame ----------------------------------------------------------------

Figure::Layout Figure::layout() const
{
    Layout lay;
    const FigureStyle& s = style_;
    TickStyle ticks;
    ticks.target = s.tickTarget;
    ticks.fontPixels = s.tickPixels;
    ticks.denseFactor = s.denseFactor;

    const std::vector<int> drawn = drawnLines();
    // Numbered while any line is a reading on it -- a moved one included,
    // whose window is a share of the common one's units.
    lay.common = drawn.empty() || std::any_of(drawn.begin(), drawn.end(),
                                              [&](int line) { return spansCommon(line); });
    lay.tickHeight = measure_.height(TextRole::Tick);
    lay.minorHeight = measure_.height(TextRole::TickMinor);
    lay.nameHeight = measure_.height(TextRole::AxisName);
    lay.titleHeight = measure_.height(TextRole::Title);

    const bool flipped = settings_.flipped;
    const bool leftNumbered = flipped || lay.common;
    const bool footNumbered = !flipped || lay.common;
    const std::string& leftName = flipped ? settings_.xLabel : lay.common ? settings_.yLabel : "";
    const std::string& footName = !flipped ? settings_.xLabel : lay.common ? settings_.yLabel : "";

    // The side axes: every line drawn on a window of its own.
    for (std::size_t k = 0; k < drawn.size(); ++k) {
        const LineWindow window =
            windowOf(drawn[k], static_cast<int>(k), static_cast<int>(drawn.size()));
        if (!window.own || !window.axis) {
            continue;
        }
        Layout::Side side;
        side.line = drawn[k];
        side.window = window;
        side.colour = seriesColor(settings_.colours, drawn[k], static_cast<int>(k),
                                  static_cast<int>(drawn.size()), settings_.paper, settings_.dark,
                                  lineSettings(drawn[k]).colour);
        side.label = lineSettings(drawn[k]).axisLabel;
        lay.sides.push_back(side);
    }

    // PlotFrame.gutterTop, footCommonHeight, sideHeight and gutterBottom --
    // none of which depends on a number up the side, which is what lets the
    // height be settled before the left gutter is.
    const double top =
        s.gapS + (settings_.title.empty() ? 0.0 : std::ceil(lay.titleHeight) + s.gapS);
    lay.footHeight = (footNumbered ? std::ceil(lay.tickHeight) + s.gapS : s.gapS) +
                     (footName.empty() ? 0.0 : std::ceil(lay.nameHeight) + s.gapXS);
    double sideHeight = 0.0;
    if (flipped && !lay.sides.empty()) {
        const double numbers = s.accentWidth + s.s3 + std::ceil(lay.tickHeight);
        double at = 0.0;
        for (Layout::Side& side : lay.sides) {
            side.at = at;
            side.extent =
                numbers + (side.label.empty() ? 0.0 : s.gapXS + std::ceil(lay.nameHeight));
            at += side.extent + s.gapM;
        }
        sideHeight = std::ceil(lay.sides.back().at + lay.sides.back().extent + s.gapS);
    }
    const double areaHeight = std::max(1.0, height_ - top - lay.footHeight - sideHeight);

    // The numbers up the side, measured up the pane -- PlotFrame.xValuesUp and
    // yValuesUp, which are what keep the left gutter out of a cycle with them.
    const CameraAxis& x = camera_.x;
    const CameraAxis& y = camera_.y;
    if (flipped) {
        lay.xTicks =
            tickValues(x.viewLow(), x.viewHigh(), x.logarithmic, x.base, areaHeight, true, ticks);
        lay.leftTicks = lay.xTicks;
    }
    else if (lay.common) {
        lay.yTicks =
            tickValues(y.viewLow(), y.viewHigh(), y.logarithmic, y.base, areaHeight, true, ticks);
        lay.leftTicks = lay.yTicks;
    }

    // Each side column, from the outside in: its name, its numbers, a gap, a
    // tick and a rule (PlotFrame.sideColumns).
    double sideWidth = 0.0;
    if (!flipped && !lay.sides.empty()) {
        double at = 0.0;
        for (Layout::Side& side : lay.sides) {
            side.ticks = tickValues(side.window.low, side.window.high, side.window.logarithmic,
                                    y.base, areaHeight, true, ticks);
            for (const TickLabel& label : side.ticks.texts) {
                side.widest = std::max(
                    side.widest, measure_.width(label.base, TextRole::Tick) +
                                     (label.exponent.empty()
                                          ? 0.0
                                          : measure_.width(label.exponent, TextRole::TickMinor)));
            }
            side.at = at;
            side.extent = (side.label.empty() ? 0.0 : std::ceil(lay.nameHeight) + s.gapXS) +
                          std::ceil(side.widest) + s.gapS + s.s3 + s.accentWidth;
            at += side.extent + s.gapM;
        }
        sideWidth = std::ceil(lay.sides.back().at + lay.sides.back().extent + s.gapM);
    }
    lay.sideWidth = sideWidth;

    double widest = 0.0;
    for (const TickLabel& label : lay.leftTicks.texts) {
        widest = std::max(widest, measure_.width(label.base, TextRole::Tick) +
                                      (label.exponent.empty()
                                           ? 0.0
                                           : measure_.width(label.exponent, TextRole::TickMinor)));
    }
    lay.commonWidth = !leftNumbered
                          ? s.gapS
                          : std::max(s.labelMargin, std::ceil(widest) + s.gapM) +
                                (leftName.empty() ? 0.0 : std::ceil(lay.nameHeight) + s.gapXS);
    const double left = sideWidth + lay.commonWidth;
    const double right = s.s9;
    lay.area = PaneRect{left, top, std::max(1.0, width_ - left - right), areaHeight};

    // The numbers along the foot, measured across it.
    if (flipped) {
        if (lay.common) {
            lay.yTicks = tickValues(y.viewLow(), y.viewHigh(), y.logarithmic, y.base,
                                    lay.area.width, false, ticks);
        }
        lay.footTicks = lay.yTicks;
        for (Layout::Side& side : lay.sides) {
            side.ticks = tickValues(side.window.low, side.window.high, side.window.logarithmic,
                                    y.base, lay.area.width, false, ticks);
        }
    }
    else {
        lay.xTicks = tickValues(x.viewLow(), x.viewHigh(), x.logarithmic, x.base, lay.area.width,
                                false, ticks);
        lay.footTicks = lay.xTicks;
    }
    return lay;
}

PlotView Figure::paneView(const Layout& lay) const
{
    PlotView pane;
    pane.xMin = camera_.viewMinX();
    pane.xMax = camera_.viewMaxX();
    pane.yMin = camera_.viewMinY();
    pane.yMax = camera_.viewMaxY();
    pane.xLog = settings_.xLog;
    pane.yLog = settings_.yLog;
    pane.xLogBase = settings_.xLogBase;
    pane.yLogBase = settings_.yLogBase;
    pane.width = lay.area.width;
    pane.height = lay.area.height;
    pane.pixelRatio = pixelRatio_;
    pane.transposed = settings_.flipped;
    return pane;
}

std::string Figure::reason() const
{
    // PlotSurface.logReason: asked of what the axes were offered, not of what
    // they settled on, because both settle on a nominal power.
    const std::vector<int> drawn = drawnLines();
    if (drawn.empty()) {
        return {};
    }
    bool commonPositive = false;
    bool anyCommon = false;
    double positiveX = 0.0;
    double highestX = -kInfinity;
    for (const int line : drawn) {
        highestX = std::max(highestX, plot_.xExtent(line).high);
        const double p = plot_.xPositiveMinimum(line);
        if (p > 0.0 && (positiveX == 0.0 || p < positiveX)) {
            positiveX = p;
        }
        if (spansCommon(line)) {
            anyCommon = true;
            const LineExtent extent = plot_.extent(line);
            commonPositive = commonPositive || (extent.finite && extent.maximum > 0.0);
        }
    }
    if (settings_.yLog && anyCommon && !commonPositive) {
        return "Nothing here is above zero, and a logarithmic y axis has no place to draw a "
               "value that is not.";
    }
    if (settings_.xLog && !(positiveX > 0.0 && highestX > positiveX)) {
        return "No x here is above zero, and a logarithmic x axis has no place to draw a value "
               "that is not.";
    }
    return {};
}

Scene Figure::scene()
{
    updateBounds();
    const Layout lay = layout();
    camera_.area = lay.area;
    const FigureStyle& s = style_;
    const Chrome chrome = chromeFor(settings_.dark, settings_.paper);

    Scene scene;
    scene.width = width_;
    scene.height = height_;
    scene.ground = chrome.ground;
    scene.chrome = chrome;
    scene.area = lay.area;
    scene.x = {camera_.viewMinX(), camera_.viewMaxX()};
    scene.y = {camera_.viewMinY(), camera_.viewMaxY()};

    const std::vector<int> drawn = drawnLines();
    scene.message = reason();

    // --- the lines ---------------------------------------------------------
    const PlotView pane = paneView(lay);
    const PlotView upright = uprightView(pane);
    const double columns = (settings_.flipped ? lay.area.height : lay.area.width) * pixelRatio_;
    plot_.setPaneColumns(columnsOf(columns));
    plot_.setView(camera_.viewMinX(), camera_.viewMaxX(), settings_.xLog);

    std::vector<PlotLine> projected;
    std::vector<PlotAxis> axes;
    std::vector<LineWindow> windows;
    for (std::size_t k = 0; k < drawn.size(); ++k) {
        const int index = drawn[k];
        const int count = static_cast<int>(drawn.size());
        PlotLine line = plot_.drawLine(index);
        const Rgba colour =
            seriesColor(settings_.colours, index, static_cast<int>(k), count, settings_.paper,
                        settings_.dark, lineSettings(index).colour);
        line.colour = colour;
        line.opacity = seriesOpacity(index, count, settings_.highlighted, settings_.bundle,
                                     settings_.dark && !settings_.paper);
        line.width = seriesWidth(index, settings_.highlighted, s.lineWidth);
        const LineWindow window = windowOf(index, static_cast<int>(k), count);
        if (window.own) {
            line.ownY = true;
            line.yMin = window.low;
            line.yMax = window.high;
            line.ownYLog = window.logarithmic;
        }
        const PlotAxis axis = plot_.drawAxis(index);
        projected.push_back(line);
        axes.push_back(axis);
        windows.push_back(window);

        std::vector<Point> points;
        std::vector<PlotRun> runs;
        const PlotView view = lineView(line, upright);
        const PlotProjected result = projectLine(line, axis, view, points, runs);
        // The way back to the values, through the view that projected them,
        // before the points are turned onto a flipped pane.
        const double xFrom = axisPosition(view.xMin, view.xLog, view.xLogBase);
        const double xTo = axisPosition(view.xMax, view.xLog, view.xLogBase);
        const double yFrom = axisPosition(view.yMin, view.yLog, view.yLogBase);
        const double yTo = axisPosition(view.yMax, view.yLog, view.yLogBase);
        const auto valueOf = [&](const Point& p) {
            const double across = view.width > 0.0 ? p.x() / view.width : 0.0;
            const double up = view.height > 0.0 ? 1.0 - p.y() / view.height : 0.0;
            return Point(axisValue(xFrom + across * (xTo - xFrom), view.xLog, view.xLogBase),
                         axisValue(yFrom + up * (yTo - yFrom), view.yLog, view.yLogBase));
        };
        std::vector<Point> values;
        values.reserve(points.size());
        for (const Point& point : points) {
            values.push_back(valueOf(point));
        }
        if (pane.transposed) {
            for (Point& point : points) {
                point = transposedPoint(point, pane);
            }
        }
        DrawnLine drawnLine;
        drawnLine.line = index;
        Rgba ink = colour;
        ink.alpha *= line.opacity;
        drawnLine.colour = ink;
        drawnLine.width = line.width;
        drawnLine.markerRadius = s.markerSize / 2.0;
        drawnLine.y = Span{view.yMin, view.yMax};
        drawnLine.yLogarithmic = view.yLog;
        drawnLine.own = window.own;
        drawnLine.numbered = window.own && window.axis;
        const bool marks = settings_.markers && !line.summarised && !result.decimated;
        for (const PlotRun& run : runs) {
            std::vector<Point> stroke;
            stroke.reserve(static_cast<std::size_t>(run.count));
            for (int i = 0; i < run.count; ++i) {
                const Point& p = points[static_cast<std::size_t>(run.first + i)];
                stroke.emplace_back(p.x() + lay.area.x, p.y() + lay.area.y);
            }
            std::vector<Point> strip;
            strokeRunInto(stroke.data(), static_cast<int>(stroke.size()), line.width,
                          [&](double px, double py) { strip.emplace_back(px, py); });
            trianglesOf(strip, drawnLine.triangles);
            if (marks) {
                drawnLine.markers.insert(drawnLine.markers.end(), stroke.begin(), stroke.end());
            }
            drawnLine.runs.push_back(std::move(stroke));
            drawnLine.values.emplace_back(values.begin() + run.first,
                                          values.begin() + run.first + run.count);
        }
        scene.lines.push_back(std::move(drawnLine));
        scene.legend.push_back(LegendEntry{index, plot_.name(index), colour, {}, {}, {}});
    }

    // --- the rules, under everything ----------------------------------------
    const PaneRect& a = lay.area;
    const auto alongX = [&](double fraction) { return a.x + fraction * a.width; };
    const auto upY = [&](double fraction) { return a.y + a.height - fraction * a.height; };
    const CameraAxis& cx = camera_.x;
    const CameraAxis& cy = camera_.y;
    TickStyle tickStyle;
    tickStyle.target = s.tickTarget;
    tickStyle.fontPixels = s.tickPixels;
    tickStyle.denseFactor = s.denseFactor;
    const std::vector<double> xGrid = gridValues(settings_.grid, cx.viewLow(), cx.viewHigh(),
                                                 settings_.gridStepX, lay.xTicks, tickStyle);
    const std::vector<double> yGrid = lay.common
                                          ? gridValues(settings_.grid, cy.viewLow(), cy.viewHigh(),
                                                       settings_.gridStepY, lay.yTicks, tickStyle)
                                          : std::vector<double>{};
    const auto ruleSegment = [&](double fraction, bool ofX, bool major) {
        const Rgba colour = major ? chrome.ruleMajor : chrome.ruleMinor;
        const bool vertical = ofX != settings_.flipped;
        if (vertical) {
            const double at = alongX(fraction);
            return Segment{Point(at, a.y), Point(at, a.y + a.height), colour, s.ruleWidth};
        }
        const double at = upY(fraction);
        return Segment{Point(a.x, at), Point(a.x + a.width, at), colour, s.ruleWidth};
    };
    for (const Rule& rule : gridAt(settings_.grid, xGrid, lay.xTicks.values, cx.viewLow(),
                                   cx.viewHigh(), cx.logarithmic, cx.base)) {
        if (rule.at >= 0.0 && rule.at <= 1.0) {
            scene.rules.push_back(ruleSegment(rule.at, true, rule.major));
        }
    }
    for (const Rule& rule : gridAt(settings_.grid, yGrid, lay.yTicks.values, cy.viewLow(),
                                   cy.viewHigh(), cy.logarithmic, cy.base)) {
        if (rule.at >= 0.0 && rule.at <= 1.0) {
            scene.rules.push_back(ruleSegment(rule.at, false, rule.major));
        }
    }

    // --- the axes, their ticks and their numbers -----------------------------
    const Rgba axisColour = chrome.axisRule;
    scene.strokes.push_back(
        Segment{Point(a.x, a.y), Point(a.x, a.y + a.height), axisColour, s.ruleWidth});
    scene.strokes.push_back(Segment{Point(a.x, a.y + a.height),
                                    Point(a.x + a.width, a.y + a.height), axisColour, s.ruleWidth});
    const bool leftOfX = settings_.flipped;
    const auto leftFraction = [&](double value) {
        return leftOfX ? fractionOn(value, cx.viewLow(), cx.viewHigh(), cx.logarithmic, cx.base)
                       : fractionOn(value, cy.viewLow(), cy.viewHigh(), cy.logarithmic, cy.base);
    };
    const auto footFraction = [&](double value) {
        return leftOfX ? fractionOn(value, cy.viewLow(), cy.viewHigh(), cy.logarithmic, cy.base)
                       : fractionOn(value, cx.viewLow(), cx.viewHigh(), cx.logarithmic, cx.base);
    };
    const auto addText = [&](const TickLabel& label, Point at, TextItem::Align across,
                             TextItem::Align down, TextRole role, Rgba colour) {
        TextItem item;
        item.text = label.text;
        item.base = label.base;
        item.exponent = label.exponent;
        item.at = at;
        item.across = across;
        item.down = down;
        item.role = role;
        item.colour = colour;
        scene.texts.push_back(std::move(item));
    };
    for (std::size_t i = 0; i < lay.leftTicks.values.size(); ++i) {
        const double f = leftFraction(lay.leftTicks.values[i]);
        if (f < -1e-9 || f > 1.0 + 1e-9) {
            continue;
        }
        const double at = upY(f);
        scene.strokes.push_back(
            Segment{Point(a.x - s.tickLength, at), Point(a.x, at), axisColour, s.ruleWidth});
        addText(lay.leftTicks.texts[i], Point(a.x - s.tickLength - s.gapXS, at),
                TextItem::Align::End, TextItem::Align::Middle, TextRole::Tick, chrome.ink);
    }
    const bool footNumbered = !settings_.flipped || lay.common;
    if (footNumbered) {
        for (std::size_t i = 0; i < lay.footTicks.values.size(); ++i) {
            const double f = footFraction(lay.footTicks.values[i]);
            if (f < -1e-9 || f > 1.0 + 1e-9) {
                continue;
            }
            const double at = alongX(f);
            const double bottom = a.y + a.height;
            scene.strokes.push_back(Segment{Point(at, bottom), Point(at, bottom + s.tickLength),
                                            axisColour, s.ruleWidth});
            addText(lay.footTicks.texts[i], Point(at, bottom + s.gapS), TextItem::Align::Middle,
                    TextItem::Align::Start, TextRole::Tick, chrome.ink);
        }
    }

    // The digits between two numbers, in the smallest face (minorNumbersOf).
    const double minorWidth = measure_.width("0", TextRole::TickMinor);
    const auto minors = [&](const std::vector<double>& grid, const TickSet& found,
                            const CameraAxis& axis, bool up, double extent) {
        return minorNumbersOf(settings_.minorNumbers, settings_.grid, grid, found, axis.viewLow(),
                              axis.viewHigh(), axis.base, extent,
                              up ? std::ceil(lay.minorHeight) : s.gapS, up ? 0.0 : minorWidth);
    };
    const std::vector<double>& leftGrid = settings_.flipped ? xGrid : yGrid;
    const std::vector<double>& footGrid = settings_.flipped ? yGrid : xGrid;
    const TickSet& leftSet = settings_.flipped ? lay.xTicks : lay.yTicks;
    const TickSet& footSet = settings_.flipped ? lay.yTicks : lay.xTicks;
    const CameraAxis& leftAxis = settings_.flipped ? cx : cy;
    const CameraAxis& footAxis = settings_.flipped ? cy : cx;
    if (settings_.flipped || lay.common) {
        for (const MinorNumber& number : minors(leftGrid, leftSet, leftAxis, true, a.height)) {
            addText(TickLabel{number.text, "", number.text},
                    Point(a.x - s.tickLength - s.gapXS, upY(number.at)), TextItem::Align::End,
                    TextItem::Align::Middle, TextRole::TickMinor, chrome.ink);
        }
    }
    if (footNumbered) {
        for (const MinorNumber& number : minors(footGrid, footSet, footAxis, false, a.width)) {
            addText(TickLabel{number.text, "", number.text},
                    Point(alongX(number.at), a.y + a.height + s.gapS), TextItem::Align::Middle,
                    TextItem::Align::Start, TextRole::TickMinor, chrome.ink);
        }
    }

    // The names and the title.
    const std::string& leftName = settings_.flipped ? settings_.xLabel
                                  : lay.common      ? settings_.yLabel
                                                    : std::string();
    const std::string& footName = !settings_.flipped ? settings_.xLabel
                                  : lay.common       ? settings_.yLabel
                                                     : std::string();
    if (!leftName.empty()) {
        TextItem name;
        name.text = name.base = leftName;
        name.at = Point(lay.sideWidth + std::ceil(lay.nameHeight) / 2.0, a.y + a.height / 2.0);
        name.across = TextItem::Align::Middle;
        name.down = TextItem::Align::Middle;
        name.role = TextRole::AxisName;
        name.colour = chrome.ink;
        name.vertical = true;
        scene.texts.push_back(std::move(name));
    }
    if (!footName.empty()) {
        TextItem name;
        name.text = name.base = footName;
        name.at = Point(a.x + a.width / 2.0,
                        a.y + a.height +
                            (footNumbered ? std::ceil(lay.tickHeight) + s.gapS : s.gapS) + s.gapXS);
        name.across = TextItem::Align::Middle;
        name.down = TextItem::Align::Start;
        name.role = TextRole::AxisName;
        name.colour = chrome.ink;
        scene.texts.push_back(std::move(name));
    }
    if (!settings_.title.empty()) {
        TextItem title;
        title.text = title.base = settings_.title;
        title.at = Point(a.x + a.width / 2.0, s.gapS);
        title.across = TextItem::Align::Middle;
        title.down = TextItem::Align::Start;
        title.role = TextRole::Title;
        title.colour = chrome.ink;
        scene.texts.push_back(std::move(title));
    }

    // --- the axes of their own ------------------------------------------------
    for (const Layout::Side& side : lay.sides) {
        const double base = cy.base;
        if (!settings_.flipped) {
            // A column: rule, tick and number right to left from the rule.
            const double rule = side.at + side.extent;
            scene.strokes.push_back(
                Segment{Point(rule, a.y), Point(rule, a.y + a.height), side.colour, s.accentWidth});
            for (std::size_t i = 0; i < side.ticks.values.size(); ++i) {
                const double f = fractionOn(side.ticks.values[i], side.window.low, side.window.high,
                                            side.window.logarithmic, base);
                if (f < -1e-9 || f > 1.0 + 1e-9) {
                    continue;
                }
                const double at = upY(f);
                scene.strokes.push_back(
                    Segment{Point(rule - s.s3, at), Point(rule, at), side.colour, s.ruleWidth});
                addText(side.ticks.texts[i], Point(rule - s.s3 - s.gapS, at), TextItem::Align::End,
                        TextItem::Align::Middle, TextRole::Tick, side.colour);
            }
            if (!side.label.empty()) {
                TextItem name;
                name.text = name.base = side.label;
                name.at = Point(side.at + std::ceil(lay.nameHeight) / 2.0, a.y + a.height / 2.0);
                name.across = TextItem::Align::Middle;
                name.down = TextItem::Align::Middle;
                name.role = TextRole::AxisName;
                name.colour = side.colour;
                name.vertical = true;
                scene.texts.push_back(std::move(name));
            }
            continue;
        }
        // A row under the foot: rule, tick and numbers top to bottom.
        const double rule = a.y + a.height + lay.footHeight + side.at;
        scene.strokes.push_back(
            Segment{Point(a.x, rule), Point(a.x + a.width, rule), side.colour, s.accentWidth});
        for (std::size_t i = 0; i < side.ticks.values.size(); ++i) {
            const double f = fractionOn(side.ticks.values[i], side.window.low, side.window.high,
                                        side.window.logarithmic, base);
            if (f < -1e-9 || f > 1.0 + 1e-9) {
                continue;
            }
            const double at = alongX(f);
            scene.strokes.push_back(
                Segment{Point(at, rule), Point(at, rule + s.s3), side.colour, s.ruleWidth});
            addText(side.ticks.texts[i], Point(at, rule + s.accentWidth + s.s3),
                    TextItem::Align::Middle, TextItem::Align::Start, TextRole::Tick, side.colour);
        }
        if (!side.label.empty()) {
            TextItem name;
            name.text = name.base = side.label;
            name.at = Point(a.x + a.width / 2.0,
                            rule + s.accentWidth + s.s3 + std::ceil(lay.tickHeight) + s.gapXS);
            name.across = TextItem::Align::Middle;
            name.down = TextItem::Align::Start;
            name.role = TextRole::AxisName;
            name.colour = side.colour;
            scene.texts.push_back(std::move(name));
        }
    }

    // --- the legend ----------------------------------------------------------
    if (settings_.legend && !scene.legend.empty()) {
        const double swatch = s.gapL;
        double widest = 0.0;
        for (const LegendEntry& entry : scene.legend) {
            widest = std::max(widest, measure_.width(entry.name, TextRole::Legend));
        }
        const double row = std::ceil(measure_.height(TextRole::Legend));
        const double width = s.gapS + swatch + s.gapS + std::ceil(widest) + s.gapS;
        const double height = s.gapS + row * static_cast<double>(scene.legend.size()) + s.gapS;
        const bool atLeft = settings_.legendCorner == Corner::TopLeft ||
                            settings_.legendCorner == Corner::BottomLeft;
        const bool atTop =
            settings_.legendCorner == Corner::TopLeft || settings_.legendCorner == Corner::TopRight;
        scene.legendRect =
            PaneRect{atLeft ? a.x + s.gapM : a.x + a.width - s.gapM - width,
                     atTop ? a.y + s.gapM : a.y + a.height - s.gapM - height, width, height};
        const PaneRect& box = scene.legendRect;
        for (std::size_t i = 0; i < scene.legend.size(); ++i) {
            LegendEntry& entry = scene.legend[i];
            const double middle = box.y + s.gapS + row * static_cast<double>(i) + row / 2.0;
            entry.from = Point(box.x + s.gapS, middle);
            entry.to = Point(box.x + s.gapS + swatch, middle);
            entry.textAt = Point(box.x + s.gapS + swatch + s.gapS, middle);
        }
    }
    else {
        scene.legend.clear();
    }

    // --- the reading under the pointer -----------------------------------------
    if (settings_.cursor && pointer_.has_value() && drag_ == Drag::None && scene.message.empty()) {
        const double px = pointer_->x() - a.x;
        const double py = pointer_->y() - a.y;
        if (px >= 0.0 && py >= 0.0 && px <= a.width && py <= a.height && !projected.empty()) {
            std::optional<NearestSample> best;
            std::size_t bestAt = 0;
            double bestDistance = kInfinity;
            // One line at a time, each with its own x axis -- a line may carry
            // a time of its own -- and the nearest of all of them taken.
            for (std::size_t k = 0; k < projected.size(); ++k) {
                const std::optional<NearestSample> found =
                    nearestSample(std::span<const PlotLine>(&projected[k], 1),
                                  std::span<const PlotAxis>(&axes[k], 1), pane, px, py);
                if (!found.has_value()) {
                    continue;
                }
                // A sample drawn off the pane is a clip neighbour, kept so the
                // stroke reaches the edge, and not something on screen to read.
                const double along = settings_.flipped ? found->py : found->px;
                if (along < 0.0 || along > (settings_.flipped ? a.height : a.width)) {
                    continue;
                }
                const double distance =
                    (found->px - px) * (found->px - px) + (found->py - py) * (found->py - py);
                if (distance < bestDistance) {
                    bestDistance = distance;
                    best = found;
                    bestAt = k;
                }
            }
            if (best.has_value()) {
                Reading& reading = scene.reading;
                reading.valid = true;
                reading.line = drawn[bestAt];
                reading.x = best->x;
                reading.y = best->y;
                reading.at = Point(a.x + best->px, a.y + best->py);
                reading.xText =
                    axisLabel(best->x, cx.viewLow(), cx.viewHigh(), lay.xTicks.logarithmic);
                const LineWindow& window = windows[bestAt];
                reading.yText =
                    window.own
                        ? axisLabel(best->y, window.low, window.high, window.logarithmic)
                        : axisLabel(best->y, cy.viewLow(), cy.viewHigh(), lay.yTicks.logarithmic);
                scene.strokes.push_back(Segment{Point(reading.at.x(), a.y),
                                                Point(reading.at.x(), a.y + a.height), axisColour,
                                                s.ruleWidth});
                scene.strokes.push_back(Segment{Point(a.x, reading.at.y()),
                                                Point(a.x + a.width, reading.at.y()), axisColour,
                                                s.ruleWidth});
            }
        }
    }

    if (drag_ == Drag::Band) {
        const double left = std::clamp(std::min(pressed_.x(), last_.x()), a.x, a.x + a.width);
        const double right = std::clamp(std::max(pressed_.x(), last_.x()), a.x, a.x + a.width);
        const double topY = std::clamp(std::min(pressed_.y(), last_.y()), a.y, a.y + a.height);
        const double bottomY = std::clamp(std::max(pressed_.y(), last_.y()), a.y, a.y + a.height);
        scene.band = PaneRect{left, topY, right - left, bottomY - topY};
    }
    return scene;
}

// --- gestures -------------------------------------------------------------------

int Figure::lineAt(double px, double py, double reach)
{
    updateBounds();
    const Layout lay = layout();
    camera_.area = lay.area;
    const PlotView pane = paneView(lay);
    const std::vector<int> drawn = drawnLines();
    plot_.setPaneColumns(
        columnsOf((settings_.flipped ? lay.area.height : lay.area.width) * pixelRatio_));
    plot_.setView(camera_.viewMinX(), camera_.viewMaxX(), settings_.xLog);
    int best = -1;
    double bestDistance = reach * reach;
    for (std::size_t k = 0; k < drawn.size(); ++k) {
        PlotLine line = plot_.drawLine(drawn[k]);
        const LineWindow window =
            windowOf(drawn[k], static_cast<int>(k), static_cast<int>(drawn.size()));
        if (window.own) {
            line.ownY = true;
            line.yMin = window.low;
            line.yMax = window.high;
            line.ownYLog = window.logarithmic;
        }
        const PlotAxis axis = plot_.drawAxis(drawn[k]);
        const std::optional<NearestSample> found =
            nearestSample(std::span<const PlotLine>(&line, 1), std::span<const PlotAxis>(&axis, 1),
                          pane, px - lay.area.x, py - lay.area.y);
        if (!found.has_value()) {
            continue;
        }
        const double dx = found->px - (px - lay.area.x);
        const double dy = found->py - (py - lay.area.y);
        if (dx * dx + dy * dy <= bestDistance) {
            bestDistance = dx * dx + dy * dy;
            best = drawn[k];
        }
    }
    return best;
}

void Figure::scaleLine(int line, double py, double factor)
{
    // The value under the pointer stays under it, and the next pan, wheel or
    // band takes the same share of this line's window as of every other.
    const std::vector<int> drawn = drawnLines();
    const auto at = std::find(drawn.begin(), drawn.end(), line);
    if (at == drawn.end() || !(factor > 0.0) || !std::isfinite(factor)) {
        return;
    }
    if (drawn.size() < 2) {
        return; // a lone line's axis is the common one, and the wheel moves it
    }
    // A line on the common axis is drawn in exactly the window a moved one
    // with a scale of one and no shift would have, so that is what is scaled.
    const int position = static_cast<int>(at - drawn.begin());
    const LineWindow window = windowOf(line, position, static_cast<int>(drawn.size()), true);
    ensureLine(line);
    LineSettings& held = lines_[static_cast<std::size_t>(line)];
    const double base = settings_.yLogBase;
    const double low = axisPosition(window.low, window.logarithmic, base);
    const double high = axisPosition(window.high, window.logarithmic, base);
    const PaneRect& a = camera_.area;
    const double up = settings_.flipped ? (py - a.x) / a.width : 1.0 - (py - a.y) / a.height;
    const double held_ = low + std::clamp(up, 0.0, 1.0) * (high - low);
    const double span = (high - low) / factor;
    const double nextLow = held_ - std::clamp(up, 0.0, 1.0) * span;
    const double bottom = window.fixed ? 0.0 : commonFraction(camera_.viewMinY());
    const double top = window.fixed ? 1.0 : commonFraction(camera_.viewMaxY());
    if (!(top > bottom)) {
        return;
    }
    const double homeSpan = span / (top - bottom);
    const double homeLow = nextLow - bottom * homeSpan;
    const double baseSpan = window.baseHigh - window.baseLow;
    const double centre = (window.baseLow + window.baseHigh) / 2.0;
    if (!(homeSpan > 0.0) || !(baseSpan > 0.0)) {
        return;
    }
    held.scale = std::clamp(baseSpan / homeSpan, 1e-12, 1e12);
    held.shift = homeLow + homeSpan / 2.0 - centre;
}

void Figure::shiftLine(int line, double pixels)
{
    const std::vector<int> drawn = drawnLines();
    const auto at = std::find(drawn.begin(), drawn.end(), line);
    if (at == drawn.end() || drawn.size() < 2) {
        return;
    }
    const int position = static_cast<int>(at - drawn.begin());
    const LineWindow window = windowOf(line, position, static_cast<int>(drawn.size()), true);
    const double base = settings_.yLogBase;
    const double span = axisPosition(window.high, window.logarithmic, base) -
                        axisPosition(window.low, window.logarithmic, base);
    const PaneRect& a = camera_.area;
    const double extent = settings_.flipped ? a.width : a.height;
    if (!(extent > 0.0)) {
        return;
    }
    ensureLine(line);
    LineSettings& held = lines_[static_cast<std::size_t>(line)];
    // Down the pane moves the line down, which is its window moving up; across
    // a flipped one, right moves it right.
    held.shift += (settings_.flipped ? -pixels : pixels) / extent * span;
}

void Figure::wheel(double px, double py, double angleDelta, Modifiers modifiers)
{
    updateBounds();
    const Layout lay = layout();
    camera_.area = lay.area;
    if (!reason().empty()) {
        return; // a pane explaining why it cannot draw takes no gestures
    }
    const double factor = std::pow(1.25, angleDelta / 120.0);
    if (modifiers.alt && modifiers.control) {
        const int line = lineAt(px, py);
        if (line >= 0) {
            scaleLine(line, settings_.flipped ? px : py, factor);
        }
        return;
    }
    (void)camera_.zoomAt(px, py, factor, zoomAxesFor(modifiers.shift, modifiers.control));
}

void Figure::press(double px, double py, Button button, Modifiers modifiers)
{
    updateBounds();
    camera_.area = layout().area;
    pressed_ = last_ = Point(px, py);
    pointer_ = Point(px, py);
    moved_ = false;
    controlClick_ = false;
    dragLine_ = -1;
    if (button == Button::Right) {
        drag_ = Drag::Band;
        return;
    }
    if (button == Button::Middle || (button == Button::Left && modifiers.alt)) {
        dragLine_ = lineAt(px, py);
        drag_ = dragLine_ >= 0 ? Drag::Line : Drag::None;
        return;
    }
    controlClick_ = modifiers.control;
    drag_ = Drag::Pan;
}

void Figure::move(double px, double py, Modifiers)
{
    const Point at(px, py);
    pointer_ = at;
    if (drag_ == Drag::None) {
        return;
    }
    const double dx = px - last_.x();
    const double dy = py - last_.y();
    moved_ = moved_ || std::abs(px - pressed_.x()) > 2.0 || std::abs(py - pressed_.y()) > 2.0;
    if (drag_ == Drag::Pan) {
        camera_.panBy(dx, dy);
    }
    else if (drag_ == Drag::Line) {
        shiftLine(dragLine_, settings_.flipped ? dx : dy);
    }
    last_ = at;
}

void Figure::release(double px, double py, Button button, Modifiers)
{
    last_ = Point(px, py);
    if (drag_ == Drag::Band && button == Button::Right) {
        drag_ = Drag::None;
        (void)camera_.zoomToRegion(pressed_.x(), pressed_.y(), px, py);
        return;
    }
    if (drag_ == Drag::Pan && controlClick_ && !moved_) {
        drag_ = Drag::None;
        const int line = lineAt(px, py);
        if (line >= 0) {
            // Put back: on the common axis, unmoved and unscaled.
            ensureLine(line);
            LineSettings& held = lines_[static_cast<std::size_t>(line)];
            held.scale = 1.0;
            held.shift = 0.0;
        }
        return;
    }
    drag_ = Drag::None;
}

void Figure::doubleClick(double, double, Button button)
{
    if (button == Button::Left) {
        drag_ = Drag::None;
        resetView();
    }
}

void Figure::hover(double px, double py)
{
    pointer_ = Point(px, py);
}

void Figure::leave()
{
    pointer_.reset();
}

// --- state ------------------------------------------------------------------------

std::string Figure::state() const
{
    using json::Value;
    Value colours = Value::object();
    colours.set("mode", Value::of(settings_.colours.mode))
        .set("single", Value::of(hexOf(settings_.colours.single)))
        .set("rangeFrom", Value::of(hexOf(settings_.colours.rangeFrom)))
        .set("rangeTo", Value::of(hexOf(settings_.colours.rangeTo)))
        .set("reversed", Value::of(settings_.colours.reversed))
        .set("from", Value::of(settings_.colours.from))
        .set("to", Value::of(settings_.colours.to))
        .set("custom", Value::of(settings_.colours.custom));
    Value lines = Value::array();
    for (const LineSettings& line : lines_) {
        Value one = Value::object();
        one.set("visible", Value::of(line.visible))
            .set("colour", line.colour.has_value() ? Value::of(hexOf(*line.colour)) : Value{})
            .set("ownAxis", Value::of(line.ownAxis))
            .set("axisFixed", Value::of(line.axisFixed))
            .set("axisLabel", Value::of(line.axisLabel))
            .set("scale", Value::of(line.scale))
            .set("shift", Value::of(line.shift));
        lines.items.push_back(std::move(one));
    }
    Value view = Value::object();
    view.set("zoomX", Value::of(camera_.x.view.zoom))
        .set("panX", Value::of(camera_.x.view.pan))
        .set("zoomY", Value::of(camera_.y.view.zoom))
        .set("panY", Value::of(camera_.y.view.pan));
    Value root = Value::object();
    root.set("plotcore", Value::of(1.0))
        .set("grid", Value::of(std::string(gridName(settings_.grid))))
        .set("gridStepX", Value::of(settings_.gridStepX))
        .set("gridStepY", Value::of(settings_.gridStepY))
        .set("xLog", Value::of(settings_.xLog))
        .set("yLog", Value::of(settings_.yLog))
        .set("xLogBase", Value::of(settings_.xLogBase))
        .set("yLogBase", Value::of(settings_.yLogBase))
        .set("flipped", Value::of(settings_.flipped))
        .set("minorNumbers", Value::of(settings_.minorNumbers))
        .set("markers", Value::of(settings_.markers))
        .set("cursor", Value::of(settings_.cursor))
        .set("title", Value::of(settings_.title))
        .set("xLabel", Value::of(settings_.xLabel))
        .set("yLabel", Value::of(settings_.yLabel))
        .set("legend", Value::of(settings_.legend))
        .set("legendCorner", Value::of(std::string(cornerName(settings_.legendCorner))))
        .set("colours", std::move(colours))
        .set("highlighted", Value::of(static_cast<double>(settings_.highlighted)))
        .set("bundle", Value::of(settings_.bundle))
        .set("dark", Value::of(settings_.dark))
        .set("paper", Value::of(settings_.paper))
        .set("stacked", Value::of(settings_.stacked))
        .set("lines", std::move(lines))
        .set("view", std::move(view));
    return json::write(root);
}

bool Figure::restore(const std::string& text)
{
    const std::optional<json::Value> parsed = json::parse(text);
    if (!parsed.has_value() || parsed->kind != json::Value::Kind::Object ||
        parsed->find("plotcore") == nullptr) {
        return false;
    }
    const json::Value& root = *parsed;
    FigureSettings next = settings_;
    next.grid = gridOf(root.textOr("grid", gridName(next.grid)));
    next.gridStepX = root.numberOr("gridStepX", next.gridStepX);
    next.gridStepY = root.numberOr("gridStepY", next.gridStepY);
    next.xLog = root.boolOr("xLog", next.xLog);
    next.yLog = root.boolOr("yLog", next.yLog);
    next.xLogBase = root.numberOr("xLogBase", next.xLogBase);
    next.yLogBase = root.numberOr("yLogBase", next.yLogBase);
    next.flipped = root.boolOr("flipped", next.flipped);
    next.minorNumbers = root.boolOr("minorNumbers", next.minorNumbers);
    next.markers = root.boolOr("markers", next.markers);
    next.cursor = root.boolOr("cursor", next.cursor);
    next.title = root.textOr("title", next.title);
    next.xLabel = root.textOr("xLabel", next.xLabel);
    next.yLabel = root.textOr("yLabel", next.yLabel);
    next.legend = root.boolOr("legend", next.legend);
    next.legendCorner = cornerOf(root.textOr("legendCorner", cornerName(next.legendCorner)));
    if (const json::Value* colours = root.find("colours"); colours != nullptr) {
        next.colours.mode = colours->textOr("mode", next.colours.mode);
        const auto colourOr = [&](const char* key, const Rgba& fallback) {
            const json::Value* found = colours->find(key);
            return found != nullptr && found->kind == json::Value::Kind::String
                       ? colourOf(found->text)
                       : fallback;
        };
        next.colours.single = colourOr("single", next.colours.single);
        next.colours.rangeFrom = colourOr("rangeFrom", next.colours.rangeFrom);
        next.colours.rangeTo = colourOr("rangeTo", next.colours.rangeTo);
        next.colours.reversed = colours->boolOr("reversed", next.colours.reversed);
        next.colours.from = colours->numberOr("from", next.colours.from);
        next.colours.to = colours->numberOr("to", next.colours.to);
        if (colours->find("custom") != nullptr) {
            next.colours.custom = colours->boolOr("custom", next.colours.custom);
        }
        else {
            // A view written before a colour could be left to the ground: its
            // colours are a choice wherever one of them is a colour neither
            // ground would have put there -- the viewer's migration.
            const LineColours dark = LineColours::defaults(true);
            const LineColours light = LineColours::defaults(false);
            const auto chosen = [](const Rgba& c, const Rgba& a, const Rgba& b) {
                const auto same = [](const Rgba& x, const Rgba& y) { return hexOf(x) == hexOf(y); };
                return !same(c, a) && !same(c, b);
            };
            next.colours.custom = chosen(next.colours.single, dark.single, light.single) ||
                                  chosen(next.colours.rangeFrom, dark.rangeFrom, light.rangeFrom) ||
                                  chosen(next.colours.rangeTo, dark.rangeTo, light.rangeTo);
        }
    }
    next.highlighted = static_cast<int>(root.numberOr("highlighted", next.highlighted));
    next.bundle = root.boolOr("bundle", next.bundle);
    next.dark = root.boolOr("dark", next.dark);
    next.paper = root.boolOr("paper", next.paper);
    next.stacked = root.boolOr("stacked", next.stacked);
    setSettings(next);

    if (const json::Value* lines = root.find("lines");
        lines != nullptr && lines->kind == json::Value::Kind::Array) {
        for (std::size_t i = 0; i < lines->items.size(); ++i) {
            const json::Value& one = lines->items[i];
            LineSettings line = lineSettings(static_cast<int>(i));
            line.visible = one.boolOr("visible", line.visible);
            if (const json::Value* colour = one.find("colour"); colour != nullptr) {
                line.colour = colour->kind == json::Value::Kind::String
                                  ? std::optional<Rgba>(colourOf(colour->text))
                                  : std::nullopt;
            }
            line.ownAxis = one.boolOr("ownAxis", line.ownAxis);
            line.axisFixed = one.boolOr("axisFixed", line.axisFixed);
            line.axisLabel = one.textOr("axisLabel", line.axisLabel);
            line.scale = one.numberOr("scale", line.scale);
            line.shift = one.numberOr("shift", line.shift);
            setLineSettings(static_cast<int>(i), line);
        }
    }
    if (const json::Value* view = root.find("view"); view != nullptr) {
        const auto zoomOf = [](double zoom) {
            return zoom >= 1.0 && std::isfinite(zoom) ? zoom : 1.0;
        };
        camera_.x.view = {zoomOf(view->numberOr("zoomX", 1.0)), view->numberOr("panX", 0.0)};
        camera_.y.view = {zoomOf(view->numberOr("zoomY", 1.0)), view->numberOr("panY", 0.0)};
    }
    return true;
}

} // namespace plotcore
