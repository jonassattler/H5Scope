// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-FileCopyrightText: 2026 Andreas Martin
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// A whole plot: the lines, the view onto them, what the reader has chosen, and
// the picture all of that makes.
//
// What the viewer spreads across DatasetPlot, PlotSurface.qml, PlotFrame.qml
// and PlotItem, in one object a host can drive without knowing any of them.
// The host does three things and nothing else: it hands over the pane's size
// and how to measure text, it forwards the pointer and the wheel, and it
// paints the Scene that comes back -- strokes, rules, text, in pane pixels with
// y down. Every decision between those is plotcore's, so a plot drawn from Qt,
// from Python and from Rust is the same plot: the same ticks at the same
// places, the same gestures moving the same windows, the same line in the same
// colour (tests/golden holds the three to one another).
//
// The viewer's rules hold here as they do there, because the arithmetic is the
// viewer's -- Ticks.hpp and Camera.hpp are PlotFrame's and PlotSurface's,
// held to them by tst_plotcore_parity.qml. Three things are this file's own,
// all of them from #30, all of them about a line rather than the axes:
//
// - **A line may be moved and scaled on its axis** (LineSettings::scale and
//   shift): its window is the common window's share of a window of its own,
//   so one gesture still moves every line and a moved line stays moved.
// - **Lines may be stacked**, each in an equal band of the pane: the same
//   arithmetic, with the scale and shift that put each line's own extent in
//   its band.
// - **A line may carry a time of its own** (Plot::setLineTime), against the
//   one x axis every line shares.
//
// A line on an axis of its own, or stacked, is numbered beside the pane in
// its colour, as the viewer draws a separate axis -- and, as there, only while
// a second line is drawn. A line that is only moved or scaled is not: it is a
// reading in the common axis's units, the crosshair reads it in its own, and
// an axis appearing beside the pane on the first pixel of a drag would widen
// the gutter and move every x under the pointer that is dragging.

#include "plotcore/Camera.hpp"
#include "plotcore/Palettes.hpp"
#include "plotcore/Plot.hpp"
#include "plotcore/Ticks.hpp"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace plotcore {

/// The faces text is set in. A host maps each to a font.
enum class TextRole
{
    /// A number on an axis: Theme.readout.
    Tick,
    /// A raised exponent, and a digit between two numbers: Theme.readoutMinor.
    TickMinor,
    /// The title over the pane: Theme.bodyStrong.
    Title,
    /// An axis's name: Theme.bodySmall.
    AxisName,
    /// A line's name in the legend.
    Legend,
};

/// How a host measures text. Both are in pane pixels.
struct TextMeasure
{
    std::function<double(const std::string& text, TextRole role)> width;
    /// The height of a line of type in `role`.
    std::function<double(TextRole role)> height;
};

/// What the frame is laid out with: Theme's numbers, as a host's own.
struct FigureStyle
{
    double tickPixels = 10.0;
    double tickMinorPixels = 8.0;
    double titlePixels = 13.0;
    double axisNamePixels = 11.0;
    double legendPixels = 11.0;
    double gapXS = 2.0;
    double gapS = 4.0;
    double gapM = 8.0;
    double gapL = 12.0;
    double s3 = 4.0;
    double s9 = 24.0;
    double labelMargin = 32.0;
    double accentWidth = 2.0;
    double ruleWidth = 1.0;
    double tickLength = 4.0;
    double exponentRaise = 4.0;
    double lineWidth = 1.0;
    double markerSize = 4.0;
    int tickTarget = 8;
    int denseFactor = 4;

    /// A measure that needs no font: three fifths of the face's size a
    /// character, and six fifths of it a line. What a headless run and the
    /// golden digests use, so they are about the arithmetic and not a font.
    [[nodiscard]] TextMeasure approximateMeasure() const;
    [[nodiscard]] double pixelsOf(TextRole role) const;
};

enum class Corner
{
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight,
};

/// Everything a reader chooses about the plot as a whole -- PlotSurface's
/// drawing settings.
struct FigureSettings
{
    GridMode grid = GridMode::Loose;
    double gridStepX = 0.0;
    double gridStepY = 0.0;
    bool xLog = false;
    bool yLog = false;
    double xLogBase = 10.0;
    double yLogBase = 10.0;
    /// x up the pane and y across it, as `plot(y, x)` draws it.
    bool flipped = false;
    bool minorNumbers = false;
    bool markers = false;
    bool cursor = true;
    std::string title;
    std::string xLabel;
    std::string yLabel;
    bool legend = true;
    Corner legendCorner = Corner::TopRight;
    LineColours colours = LineColours::defaults(true);
    int highlighted = -1;
    /// Lines drawn under one another, as a bundle of rows is.
    bool bundle = false;
    /// The ground the picture is for.
    bool dark = true;
    /// The picture a page would draw: the light scope, whatever is on screen.
    bool paper = false;
    /// Every drawn line in an equal band of the pane.
    bool stacked = false;
};

/// What a reader chooses about one line.
struct LineSettings
{
    bool visible = true;
    /// A colour of its own, over whatever cycle is in force.
    std::optional<Rgba> colour;
    /// On a y axis of its own: the line alone, linear, zoomed by the common
    /// axis's share. See CLAUDE.md, "A line of a custom tab may also be drawn
    /// against a y axis of its own".
    bool ownAxis = false;
    /// ...and left at its whole extent whatever the zoom.
    bool axisFixed = false;
    std::string axisLabel;
    /// Moved and scaled on its axis: its whole window is the window it would
    /// have, shrunk by `scale` about its centre and moved by `shift` -- in the
    /// axis's positions, decades on a logarithmic one.
    double scale = 1.0;
    double shift = 0.0;
};

/// Which button a press came from.
enum class Button
{
    Left,
    Middle,
    Right,
};

struct Modifiers
{
    bool shift = false;
    bool control = false;
    bool alt = false;
};

/// A straight stroke: a rule, a tick, an axis.
struct Segment
{
    Point from;
    Point to;
    Rgba colour;
    double width = 1.0;
};

/// A piece of text, at a point and an alignment. `base` is set in `role`'s
/// face, `exponent` raised beside it in TickMinor's; `text` is the whole of it
/// in one string with the exponent in superscript digits, for a host that sets
/// one string.
struct TextItem
{
    enum class Align
    {
        Start,
        Middle,
        End,
    };

    std::string text;
    std::string base;
    std::string exponent;
    Point at;
    Align across = Align::Start;
    Align down = Align::Start;
    TextRole role = TextRole::Tick;
    Rgba colour;
    /// Turned a quarter anticlockwise about `at`, as an axis name up the side is.
    bool vertical = false;
};

/// One line, as it is drawn.
struct DrawnLine
{
    int line = -1;
    Rgba colour;
    double width = 1.0;
    /// The strokes as polylines, for a host that strokes them itself.
    std::vector<std::vector<Point>> runs;
    /// ...and as triangles, three points each, for one that fills them.
    std::vector<Point> triangles;
    /// Where a marker goes, when markers are on and the line is its samples.
    std::vector<Point> markers;
    double markerRadius = 2.0;
};

/// One row of the legend: a swatch from `from` to `to`, and the name set in
/// TextRole::Legend starting at `textAt` and centred on it vertically.
struct LegendEntry
{
    int line = -1;
    std::string name;
    Rgba colour;
    Point from;
    Point to;
    Point textAt;
};

/// The sample under the pointer, when there is one.
struct Reading
{
    bool valid = false;
    int line = -1;
    double x = 0.0;
    double y = 0.0;
    /// Where it was drawn.
    Point at;
    /// The two numbers, written as the axes write theirs.
    std::string xText;
    std::string yText;
};

/// One frame.
struct Scene
{
    double width = 0.0;
    double height = 0.0;
    Rgba ground;
    /// The pane inside the gutters, where the lines are.
    PaneRect area;
    std::vector<DrawnLine> lines;
    /// The grid, under the lines.
    std::vector<Segment> rules;
    /// The axes, their ticks, and the crosshair, over them.
    std::vector<Segment> strokes;
    std::vector<TextItem> texts;
    std::vector<LegendEntry> legend;
    PaneRect legendRect;
    Reading reading;
    /// The rectangle a right-drag is drawing, while it is.
    std::optional<PaneRect> band;
    /// Why nothing can be drawn, said over the pane; empty when it can.
    std::string message;
    Span x;
    Span y;
};

class Figure
{
public:
    Figure();

    [[nodiscard]] Plot& plot() { return plot_; }
    [[nodiscard]] const Plot& plot() const { return plot_; }
    [[nodiscard]] Camera& camera() { return camera_; }
    [[nodiscard]] const Camera& camera() const { return camera_; }

    [[nodiscard]] const FigureSettings& settings() const { return settings_; }
    /// Change the settings. A change of either axis's scale or of the flip
    /// resets the view, for PlotSurface's reason: a pan in decades and a pan
    /// in units have no reading that is both.
    void setSettings(const FigureSettings& settings);

    /// Take a line away, and what was chosen about it with it: the lines
    /// after it move down one, settings and all.
    void removeLine(int line);
    void clear();

    /// One line's settings; a line that has none yet gets the defaults.
    [[nodiscard]] LineSettings lineSettings(int line) const;
    void setLineSettings(int line, const LineSettings& settings);

    [[nodiscard]] const FigureStyle& style() const { return style_; }
    void setStyle(const FigureStyle& style);
    /// How the host measures text; one without both halves goes back to the
    /// style's approximation.
    void setMeasure(TextMeasure measure);

    /// The whole picture's size in pane pixels, and how many device pixels
    /// each one is.
    void setSize(double width, double height, double pixelRatio = 1.0);

    // --- gestures, in pane pixels with y down -----------------------------
    /// A wheel turned by `angleDelta` eighths of a degree, as Qt counts them:
    /// 120 is one notch. Shift is x, Control is y; Alt and Control together
    /// scale the line under the pointer and nothing else.
    void wheel(double px, double py, double angleDelta, Modifiers modifiers);
    /// Left drags the view; right draws a band to zoom to; middle, or left
    /// with Alt, moves the line under the pointer up and down its axis.
    /// Control and a left click puts that line back.
    void press(double px, double py, Button button, Modifiers modifiers);
    void move(double px, double py, Modifiers modifiers);
    void release(double px, double py, Button button, Modifiers modifiers);
    /// Left puts the view back to the whole of every line.
    void doubleClick(double px, double py, Button button);
    void hover(double px, double py);
    void leave();
    void resetView();
    /// The window named in values -- the viewer's four boxes -- clipped to
    /// what the axes can draw.
    void setViewRange(double x0, double x1, double y0, double y1);

    // --- the picture -------------------------------------------------------
    [[nodiscard]] Scene scene();

    /// The line drawn nearest (`px`, `py`), within `reach` pixels, or -1.
    [[nodiscard]] int lineAt(double px, double py, double reach = 12.0);

    // --- a view, written down ------------------------------------------------
    /// The settings, every line's settings and the window, as JSON.
    [[nodiscard]] std::string state() const;
    /// Put back what state() wrote. A setting this build does not know is
    /// passed over and costs that setting, not the view. False when `json` is
    /// not a view at all.
    bool restore(const std::string& json);

private:
    struct Layout;
    struct LineWindow;

    void updateBounds();
    [[nodiscard]] std::vector<int> drawnLines() const;
    /// Whether a line is drawn on the common window, and whether its extent is
    /// part of that window's whole -- which a moved line's still is.
    [[nodiscard]] bool commonLine(int line) const;
    [[nodiscard]] bool spansCommon(int line) const;
    /// The window a line is drawn in; with `always`, the one it would have if
    /// it were moved by nothing, which for a line on the common axis is that
    /// axis's view.
    [[nodiscard]] LineWindow windowOf(int line, int position, int drawn, bool always = false) const;
    [[nodiscard]] std::string reason() const;
    [[nodiscard]] double commonFraction(double value) const;
    [[nodiscard]] Layout layout() const;
    [[nodiscard]] PlotView paneView(const Layout& layout) const;
    void scaleLine(int line, double py, double factor);
    void shiftLine(int line, double dy);
    void ensureLine(int line);

    Plot plot_;
    Camera camera_;
    FigureSettings settings_;
    std::vector<LineSettings> lines_;
    FigureStyle style_;
    TextMeasure measure_;
    bool approximate_ = true;
    double width_ = 640.0;
    double height_ = 400.0;
    double pixelRatio_ = 1.0;

    // What a gesture in progress is doing.
    enum class Drag
    {
        None,
        Pan,
        Band,
        Line,
    };
    Drag drag_ = Drag::None;
    Point pressed_;
    Point last_;
    int dragLine_ = -1;
    bool moved_ = false;
    bool controlClick_ = false;
    std::optional<Point> pointer_;
};

} // namespace plotcore
