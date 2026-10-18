// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Where the reader is looking, and how a gesture moves it.
//
// A port of the viewer's PlotSurface.qml: zoom and pan are each axis's own, a
// window is a zoom and a pan over the axis's bounds, and every gesture -- the
// wheel, the drag, the band, the four boxes -- is arithmetic over those two
// numbers. The free functions are PlotSurface's, under its names, and
// tst_plotcore_parity.qml holds them to it; Camera is the state they act on,
// which PlotSurface keeps in its properties.
//
// The rules that hold it up are written over the QML and are not repeated
// here, only named:
//
// - **A gesture is arithmetic over decades** on a logarithmic axis
//   (axisPosition is the one place that knows it), which is why a pan is in
//   decades there and a change of scale resets the view.
// - **The view may be dragged past the data, not off it**: clampPan keeps a
//   quarter of the window over the data on each axis.
// - **The zoom ceiling is a window, not a magnification** on a logarithmic x
//   axis (logZoomCeiling), so the narrowest window is the same number of units
//   wherever the pointer is.
// - **A window below zero is clipped, not discarded** (positiveSpan).
// - **Nothing above zero is one empty decade, never a NaN** (padded, xBounds).
//
// Pixels come in pane-local and with y down, as every toolkit delivers them;
// fractions go out from the bottom of the pane.

#include <optional>

namespace plotcore {

/// Two ends of an axis.
struct Span
{
    double low = 0.0;
    double high = 1.0;

    [[nodiscard]] bool operator==(const Span&) const = default;
};

/// One axis's view onto its bounds: how far in, and how far along, in the
/// axis's own positions (decades on a logarithmic one).
struct AxisZoom
{
    double zoom = 1.0;
    double pan = 0.0;

    [[nodiscard]] bool operator==(const AxisZoom&) const = default;
};

/// Which axes a wheel moves. PlotSurface.zoomAxesFor.
enum class ZoomAxes
{
    Both,
    X,
    Y,
};

/// Shift alone is x, Control alone is y, anything else is both.
[[nodiscard]] ZoomAxes zoomAxesFor(bool shift, bool control);

/// A zoom says where it is going: the x under the pointer and the factor, for
/// the store to read ahead towards. See PlotFocus.
struct GestureFocus
{
    double x = 0.0;
    double factor = 1.0;
};

// --- PlotSurface.qml's arithmetic ------------------------------------------

[[nodiscard]] double axisPosition(double value, bool logarithmic, double base);
[[nodiscard]] double axisValue(double position, bool logarithmic, double base);
[[nodiscard]] double valueAlong(double low, double high, double at, bool logarithmic, double base);
[[nodiscard]] double visibleLow(double low, double high, double zoom, double pan, bool logarithmic,
                                double base);
[[nodiscard]] double visibleHigh(double low, double high, double zoom, double pan, bool logarithmic,
                                 double base);

/// `low`..`high` with a little air at each end, so a line at an extreme is a
/// line and not part of the frame.
[[nodiscard]] Span padded(double low, double high, bool logarithmic, double base);

/// The resolved base of a mode and its number: "10", "2", "e" or "custom".
[[nodiscard]] double baseFor(const char* mode, double custom);

[[nodiscard]] double clampPan(double pan, double zoom, double low, double high, bool logarithmic,
                              double base, double keep = 0.25);
[[nodiscard]] double logZoomCeiling(double full, double held, double fraction, double minimumSpan,
                                    double base, double maxZoom);
[[nodiscard]] AxisZoom zoomedAxis(AxisZoom view, double low, double high, double fraction,
                                  double factor, bool logarithmic, double base, double minimumSpan,
                                  double maxZoom, double keep = 0.25);
[[nodiscard]] AxisZoom viewedAxis(double low, double high, double from, double to, bool logarithmic,
                                  double base, double minimumSpan, double maxZoom,
                                  double keep = 0.25);
[[nodiscard]] Span positiveSpan(double from, double to, double low, double high, bool logarithmic);

/// The smallest x above zero on a stated axis of `low + j * step`, or 0.
/// PlotSurface.gridPositiveMinX: arithmetic over three numbers, no pass.
[[nodiscard]] double gridPositiveMin(double low, double high, double step);

/// The x axis's ends as it is drawn between them: its own, or one empty power
/// of the base on a logarithmic axis with nothing positive to run between.
[[nodiscard]] Span xBounds(double low, double high, double positiveMin, bool logarithmic,
                           double base);

/// The viewer's magnification ceiling for a line of `length` elements.
[[nodiscard]] double maxZoomFor(long long length);

/// One axis, as the camera holds it: its bounds, its scale, and the view.
struct CameraAxis
{
    Span bounds;
    bool logarithmic = false;
    double base = 10.0;
    AxisZoom view;
    /// The narrowest window a zoom may reach, in the axis's own units; zero
    /// for none. The viewer sets it on x only.
    double minimumSpan = 0.0;

    [[nodiscard]] double viewLow() const;
    [[nodiscard]] double viewHigh() const;
    /// The current window, in the units a pan is measured in.
    [[nodiscard]] double viewSpan() const;
    /// The whole axis, in those units.
    [[nodiscard]] double fullSpan() const;
};

/// A pane, in pixels of whoever holds it, y down.
struct PaneRect
{
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
};

/// Where the reader is looking, and the gestures that move it.
///
/// PlotSurface's state and its stateful functions. A pane is `area`; x runs
/// along it and y up it, or the other way round when `flipped`.
class Camera
{
public:
    CameraAxis x;
    CameraAxis y;
    bool flipped = false;
    PaneRect area;
    double maxZoom = 256.0;
    double panKeep = 0.25;
    /// A band under this many pixels on either side is a slip, not a window.
    double minimumBand = 12.0;

    [[nodiscard]] double viewMinX() const { return x.viewLow(); }
    [[nodiscard]] double viewMaxX() const { return x.viewHigh(); }
    [[nodiscard]] double viewMinY() const { return y.viewLow(); }
    [[nodiscard]] double viewMaxY() const { return y.viewHigh(); }

    [[nodiscard]] bool zoomed() const;
    void reset();

    /// The wheel, held still under (`px`, `py`). The focus a zoom of x says it
    /// is heading towards, when x moved.
    std::optional<GestureFocus> zoomAt(double px, double py, double factor, ZoomAxes axes);
    /// A drag by (`dx`, `dy`) pixels. Clears any focus: a pan has none.
    void panBy(double dx, double dy);
    /// The four boxes: a window named in values, clipped to what the axes can
    /// draw.
    void setViewRange(double x0, double x1, double y0, double y1);
    /// The band: the pixels between two corners, refused when either side is
    /// under minimumBand. The focus is written to `focus` when it moved x.
    bool zoomToRegion(double px0, double py0, double px1, double py1,
                      std::optional<GestureFocus>* focus = nullptr);

    /// The value under a pane pixel. The band resolves through these, so its
    /// readout has to ask them too.
    [[nodiscard]] double dataXAt(double px, double py) const;
    [[nodiscard]] double dataYAt(double py, double px) const;
};

} // namespace plotcore
