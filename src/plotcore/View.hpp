// SPDX-FileCopyrightText: 2026 Andreas Martin
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Zoom, pan and padded extents, with no widget and no Qt.
//
// PlotSurface.qml owns this arithmetic in the application; PlotWidget.cpp
// used to own a second copy. The Python host cannot call either of those, so
// the math lives here once. A pan is still in the axis's own scale (decades
// while logarithmic), and a quarter of the window stays over the data.

#include "gui/PlotProjection.hpp"

namespace gui {

class PlotCamera
{
public:
    struct Span
    {
        double low = 0.0;
        double high = 1.0;
    };

    static constexpr int kMaxVertices = 2 << 20;
    static constexpr double kPanKeep = 0.25;
    static constexpr double kMinimumBand = 12.0;

    void setDataExtent(double xMin, double xMax, double yMin, double yMax, double xPositiveMin,
                       double yPositiveMin, long long samples);
    void setXLog(bool on);
    void setYLog(bool on);
    void setXLogBase(double base);
    void setYLogBase(double base);
    void reset();

    [[nodiscard]] bool xLog() const { return xLog_; }
    [[nodiscard]] bool yLog() const { return yLog_; }
    [[nodiscard]] double xLogBase() const { return xLogBase_; }
    [[nodiscard]] double yLogBase() const { return yLogBase_; }

    [[nodiscard]] double viewMinX() const;
    [[nodiscard]] double viewMaxX() const;
    [[nodiscard]] double viewMinY() const;
    [[nodiscard]] double viewMaxY() const;

    /// `fx`/`fy` are fractions of the pane (y up). `onlyX` / `onlyY` are the
    /// Shift / Ctrl modifiers PlotSurface uses.
    void zoomAt(double fx, double fy, double factor, bool onlyX, bool onlyY);
    void panBy(double dx, double dy, double areaWidth, double areaHeight);

    /// Pane-local pixels, origin at the top left, y downward. A band under
    /// `kMinimumBand` pixels on either side is a slip, not a window, and is
    /// refused whole: a two-pixel-tall band is a magnification the reader
    /// never asked for, and which axis they meant is not something to guess.
    /// The same refusal as PlotSurface.zoomToRegion.
    bool zoomToRegion(double px0, double py0, double px1, double py1, double areaWidth,
                      double areaHeight);

    /// The same window a rectangle would open, named in data values. A bound
    /// at or below zero on a logarithmic axis is clipped to the part that
    /// exists; a window with nothing above zero is the whole axis.
    void setViewRange(double x0, double x1, double y0, double y1);

    [[nodiscard]] PlotView frame(double width, double height, double pixelRatio,
                                 int lineCount) const;

    /// The linear window a line on its own axis shows. The same share of its
    /// padded extent that the common axis is showing of its own, then `shift`
    /// in the line's own units. One gesture still zooms every axis; the shift
    /// is the part that moves only this line. PlotSurface.separateAxes is the
    /// share.
    [[nodiscard]] Span ownSpan(double lineLow, double lineHigh, double shift) const;

    /// The common window, shifted by `shift` in the units a pan is measured in.
    /// A line drawn in this window sits that far off the common axis, and the
    /// numbers on the window are the values the line is drawn at. The scale
    /// stays the common axis's, logarithm included, so a shift does not change
    /// what a reading means. `lineSpan(1, shift)`.
    [[nodiscard]] Span shiftedSpan(double shift) const;

    /// The y window one line is drawn in.
    ///
    /// A scale of one and a shift of zero is the common window. A scale above
    /// one shows less of the axis: the line's whole window is the common
    /// axis's whole window, shrunk by `scale` about its centre and then moved
    /// by `shift`, and what is on screen is the same fraction of that whole
    /// window that the common axis is showing of its own.
    ///
    /// The fraction is what makes the next gesture agree. A pan, a wheel and
    /// a rectangle all change which fraction of the common axis is on screen,
    /// and this line gives that same fraction of its own window. The band is
    /// one region of the pane, so every line fills the pane with what was
    /// inside the band, however far apart their scales have gone. A scale
    /// stored against the window then on screen would be multiplied by the
    /// common zoom, and the samples inside the band would leave the pane.
    [[nodiscard]] Span lineSpan(double scale, double shift) const;

    /// Zoom this line's y by `factor` about `fractionUp` of the pane, and
    /// leave the common axis where it is. Zero is the bottom. The value under
    /// the pointer stays under it, and the result is written back into
    /// `scale` and `shift`. Zooming in stops at the same ceiling as the
    /// common axis; zooming out stops when the line shows the whole axis.
    /// Neither limit pulls a line back from where a previous zoom put it.
    void scaleLine(double& scale, double& shift, double fractionUp, double factor) const;

    /// Put `low`..`high` on the fraction `fractionLow`..`fractionHigh` of the
    /// pane, and write the scale and shift that do it. Zero is the bottom.
    ///
    /// The span that lands on the band is the padded one: five percent of air,
    /// or a unit when the line is flat, the same air the common axis gets.
    /// What is stored is still a home window, so the next pan or wheel takes
    /// the same fraction of every band. False when the band or the span is
    /// empty, and on a logarithmic axis when either end is not above zero —
    /// there is no place to put a value the axis cannot draw.
    bool placeLine(double& scale, double& shift, double low, double high, double fractionLow,
                   double fractionHigh) const;

    /// `placeLine` on `logarithmic` rather than on the common axis's scale.
    /// When the two agree this is that call. When they do not, the stored
    /// scale is measured against this line's own padded span, and the
    /// fraction of the pane is still the common axis's.
    bool placeLine(double& scale, double& shift, double low, double high, double fractionLow,
                   double fractionHigh, bool logarithmic) const;

    /// The y window one line is drawn in, on `logarithmic`.
    ///
    /// When that is the common axis's scale this is `lineSpan(scale, shift)`,
    /// and a line that has not left that scale does not move by an ulp. When
    /// it is not, scale and shift are in the units of this line's own padded
    /// span (`lineLow`..`lineHigh`). The share of the pane is still the
    /// common axis's, so one wheel moves every line by the same share of the
    /// window it is drawn in.
    [[nodiscard]] Span lineSpan(double scale, double shift, bool logarithmic, double lineLow,
                                double lineHigh) const;

    /// The scale and shift whose `lineSpan` is `windowLow`..`windowHigh`.
    /// False when that window has no span the scale can draw.
    bool fitLine(double& scale, double& shift, bool logarithmic, double lineLow, double lineHigh,
                 double windowLow, double windowHigh) const;

    /// Keep the padded span on the same share of the pane, and write the
    /// scale and shift that do it on `toLog`. A line with nothing above zero
    /// is given the empty decade, so the band stays and the stroke does not.
    /// False when the span that is there cannot be placed.
    bool retargetLine(double& scale, double& shift, bool fromLog, bool toLog, double low,
                      double high, double positive, bool hasPositive) const;

    /// `scaleLine` on a line whose scale is `logarithmic`. When that is the
    /// common axis's scale the extents are not read.
    void scaleLine(double& scale, double& shift, double fractionUp, double factor, bool logarithmic,
                   double lineLow, double lineHigh) const;

    /// What a drag stores as the line's y span, so that dividing by `scale`
    /// afterwards is one share of the window. On the common scale it is that
    /// axis's own span. On a line's scale it is the window's span times
    /// `scale`, which comes back to the window once the drag divides.
    [[nodiscard]] double shiftUnits(double scale, bool logarithmic, double windowLow,
                                    double windowHigh) const;

    /// The whole y axis, back on screen. X is left where it is.
    void resetY();

    /// The current window, in the units a pan is measured in.
    [[nodiscard]] double xSpan() const;
    [[nodiscard]] double ySpan() const;

    [[nodiscard]] Span paddedX() const;
    [[nodiscard]] Span paddedY() const;

    /// The span a stacked band is drawn from. On a logarithmic axis a low end
    /// at or below zero is replaced by `positive` when there is one. False
    /// when nothing on the line can be drawn — the line is left as it was.
    [[nodiscard]] static bool bandSpan(bool logarithmic, double low, double high, double positive,
                                       bool hasPositive, double& from, double& to);

    /// The data value under a pane-local pixel. zoomToRegion resolves a band
    /// through these, so a readout of that band has to ask them too.
    [[nodiscard]] double dataXAt(double px, double areaWidth) const;
    [[nodiscard]] double dataYAt(double py, double areaHeight) const;

private:
    [[nodiscard]] static Span padded(double low, double high, bool logarithmic, double base);
    [[nodiscard]] double axisPosition(double value, bool logarithmic, double base) const;
    [[nodiscard]] double axisValue(double position, bool logarithmic, double base) const;
    [[nodiscard]] double clampPan(double pan, double zoom, double low, double high, bool logarithmic,
                                  double base) const;
    void zoomedAxis(double& zoom, double& pan, double low, double high, double fraction,
                    double factor, bool logarithmic, double base, double minimumSpan);
    [[nodiscard]] double valueAlong(double low, double high, double at, bool logarithmic,
                                    double base) const;
    /// The common window, in the units a pan on that axis is measured in.
    bool viewFrame(double& axisLow, double& axisHigh, double& from, double& to) const;
    /// The span scale and shift are measured against. The common axis when
    /// `logarithmic` is its scale, and this line's padded span otherwise.
    bool referenceSpan(bool logarithmic, double lineLow, double lineHigh, double& full,
                       double& center) const;
    [[nodiscard]] double maxZoom() const;
    [[nodiscard]] double minimumSpanX() const;
    [[nodiscard]] double logZoomCeiling(double full, double held, double fraction,
                                        double minimumSpan, double base) const;

    struct AxisPlacement
    {
        double zoom = 1.0;
        double pan = 0.0;
    };

    [[nodiscard]] AxisPlacement viewedAxis(double low, double high, double from, double to,
                                           bool logarithmic, double base, double minimumSpan) const;

    double dataXMin_ = 0.0;
    double dataXMax_ = 1.0;
    double dataYMin_ = 0.0;
    double dataYMax_ = 1.0;
    double dataXPositive_ = 0.0;
    double dataYPositive_ = 0.0;
    /// Elements along x. The zoom ceiling is counted in these, not in the
    /// width of the axis. Zero means the extent itself is the only length.
    long long samples_ = 0;
    bool xLog_ = false;
    bool yLog_ = false;
    double xLogBase_ = 10.0;
    double yLogBase_ = 10.0;
    double zoomX_ = 1.0;
    double zoomY_ = 1.0;
    double panX_ = 0.0;
    double panY_ = 0.0;
};

/// The line whose stroke passes closest to `(px, py)`, or -1 when none comes
/// within `maxPixels`. `lineRuns` holds one past the last run of each line,
/// the same sentinel projectLine's callers keep.
[[nodiscard]] int nearestLine(const std::vector<QPointF>& points, const std::vector<PlotRun>& runs,
                              const std::vector<int>& lineRuns, double px, double py,
                              double maxPixels);

} // namespace gui
