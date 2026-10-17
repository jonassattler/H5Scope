// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "plotcore/Camera.hpp"

#include "plotcore/Format.hpp"
#include "plotcore/Ticks.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>

// The functions of the same names in src/qml/PlotSurface.qml, with the state
// PlotSurface reads off its own properties passed in. The arguments for each
// are written over the QML.

namespace plotcore {

ZoomAxes zoomAxesFor(bool shift, bool control)
{
    if (shift && !control) {
        return ZoomAxes::X;
    }
    if (control && !shift) {
        return ZoomAxes::Y;
    }
    return ZoomAxes::Both;
}

double axisPosition(double value, bool logarithmic, double base)
{
    return logarithmic ? logOf(value, base) : value;
}

double axisValue(double position, bool logarithmic, double base)
{
    return logarithmic ? std::pow(base, position) : position;
}

double valueAlong(double low, double high, double at, bool logarithmic, double base)
{
    const double from = axisPosition(low, logarithmic, base);
    const double to = axisPosition(high, logarithmic, base);
    return axisValue(from + at * (to - from), logarithmic, base);
}

double visibleLow(double low, double high, double zoom, double pan, bool logarithmic, double base)
{
    const double from = axisPosition(low, logarithmic, base);
    const double to = axisPosition(high, logarithmic, base);
    return axisValue((from + to) / 2.0 + pan - (to - from) / zoom / 2.0, logarithmic, base);
}

double visibleHigh(double low, double high, double zoom, double pan, bool logarithmic, double base)
{
    const double from = axisPosition(low, logarithmic, base);
    const double to = axisPosition(high, logarithmic, base);
    return axisValue((from + to) / 2.0 + pan + (to - from) / zoom / 2.0, logarithmic, base);
}

Span padded(double low, double high, bool logarithmic, double base)
{
    if (logarithmic) {
        if (!(low > 0.0) || !(high > 0.0) || !usableBase(base)) {
            return {1.0, base > 1.0 ? base : 10.0};
        }
        double from = logOf(low, base);
        double to = logOf(high, base);
        if (!(to > from)) {
            const double at = std::abs(from - jsRound(from)) < 1e-10 ? jsRound(from) : from;
            const bool whole = std::isfinite(at) && std::floor(at) == at;
            from = whole ? at - 1.0 : std::floor(at);
            to = whole ? at + 1.0 : std::ceil(at);
        }
        const double air = (to - from) * 0.05;
        return {std::pow(base, from - air), std::pow(base, to + air)};
    }
    const double air = high > low ? (high - low) * 0.05 : 1.0;
    return {low - air, high + air};
}

double baseFor(const char* mode, double custom)
{
    if (mode != nullptr && std::strcmp(mode, "2") == 0) {
        return 2.0;
    }
    if (mode != nullptr && std::strcmp(mode, "e") == 0) {
        return std::numbers::e;
    }
    if (mode != nullptr && std::strcmp(mode, "custom") == 0) {
        return usableBase(custom) ? custom : 10.0;
    }
    return 10.0;
}

double clampPan(double pan, double zoom, double low, double high, bool logarithmic, double base,
                double keep)
{
    const double full =
        axisPosition(high, logarithmic, base) - axisPosition(low, logarithmic, base);
    const double span = full / zoom;
    const double room = full / 2.0 + span * (0.5 - keep);
    return std::max(-room, std::min(room, pan));
}

double logZoomCeiling(double full, double held, double fraction, double minimumSpan, double base,
                      double maxZoom)
{
    if (!(full > 0.0) || !(minimumSpan > 0.0)) {
        return maxZoom;
    }
    const auto width = [&](double span) {
        return std::pow(base, held + (1.0 - fraction) * span) -
               std::pow(base, held - fraction * span);
    };
    if (!(width(full) > minimumSpan)) {
        return 1.0;
    }
    double narrow = 0.0;
    double wide = full;
    for (int i = 0; i < 60; ++i) {
        const double mid = (narrow + wide) / 2.0;
        if (width(mid) >= minimumSpan) {
            wide = mid;
        }
        else {
            narrow = mid;
        }
    }
    return std::max(1.0, full / wide);
}

AxisZoom zoomedAxis(AxisZoom view, double low, double high, double fraction, double factor,
                    bool logarithmic, double base, double minimumSpan, double maxZoom, double keep)
{
    const double from = axisPosition(low, logarithmic, base);
    const double to = axisPosition(high, logarithmic, base);
    const double full = to - from;
    const double span = full / view.zoom;
    const double held = (from + to) / 2.0 + view.pan - span / 2.0 + fraction * span;
    const double ceiling = logarithmic && minimumSpan > 0.0
                               ? logZoomCeiling(full, held, fraction, minimumSpan, base, maxZoom)
                               : maxZoom;
    const double next = std::max(1.0, std::min(std::max(ceiling, view.zoom), view.zoom * factor));
    const double nextSpan = full / next;
    const double centre = held - fraction * nextSpan + nextSpan / 2.0;
    return {next, clampPan(centre - (from + to) / 2.0, next, low, high, logarithmic, base, keep)};
}

AxisZoom viewedAxis(double low, double high, double from, double to, bool logarithmic, double base,
                    double minimumSpan, double maxZoom, double keep)
{
    const double axisFrom = axisPosition(low, logarithmic, base);
    const double axisTo = axisPosition(high, logarithmic, base);
    const double wantFrom = axisPosition(from, logarithmic, base);
    const double wantTo = axisPosition(to, logarithmic, base);
    const double full = axisTo - axisFrom;
    const double span = std::abs(wantTo - wantFrom);
    if (!(full > 0.0) || !(span > 0.0)) {
        return {1.0, 0.0};
    }
    const double centre = (wantFrom + wantTo) / 2.0;
    const double ceiling = logarithmic && minimumSpan > 0.0
                               ? logZoomCeiling(full, centre, 0.5, minimumSpan, base, maxZoom)
                               : maxZoom;
    const double zoom = std::max(1.0, std::min(ceiling, full / span));
    return {zoom,
            clampPan(centre - (axisFrom + axisTo) / 2.0, zoom, low, high, logarithmic, base, keep)};
}

Span positiveSpan(double from, double to, double low, double high, bool logarithmic)
{
    if (!logarithmic) {
        return {from, to};
    }
    if (!(to > 0.0)) {
        return {low, high};
    }
    return {std::max(from, low), to};
}

double gridPositiveMin(double low, double high, double step)
{
    if (!(high > 0.0)) {
        return 0.0;
    }
    if (low > 0.0) {
        return low;
    }
    const double stride = std::abs(step);
    if (!(stride > 0.0)) {
        return high;
    }
    const double at = low + (std::floor(-low / stride) + 1.0) * stride;
    return at > 0.0 && at <= high ? at : high;
}

Span xBounds(double low, double high, double positiveMin, bool logarithmic, double base)
{
    if (!logarithmic) {
        return {low, high};
    }
    if (!(positiveMin > 0.0) || !(high > positiveMin)) {
        return {1.0, base};
    }
    return {positiveMin, high};
}

double maxZoomFor(long long length)
{
    return std::max(256.0, static_cast<double>(length) / 16.0);
}

double CameraAxis::viewLow() const
{
    return visibleLow(bounds.low, bounds.high, view.zoom, view.pan, logarithmic, base);
}

double CameraAxis::viewHigh() const
{
    return visibleHigh(bounds.low, bounds.high, view.zoom, view.pan, logarithmic, base);
}

double CameraAxis::fullSpan() const
{
    return axisPosition(bounds.high, logarithmic, base) -
           axisPosition(bounds.low, logarithmic, base);
}

double CameraAxis::viewSpan() const
{
    return fullSpan() / view.zoom;
}

bool Camera::zoomed() const
{
    return x.view.zoom != 1.0 || y.view.zoom != 1.0 || x.view.pan != 0.0 || y.view.pan != 0.0;
}

void Camera::reset()
{
    x.view = {};
    y.view = {};
}

std::optional<GestureFocus> Camera::zoomAt(double px, double py, double factor, ZoomAxes axes)
{
    if (area.width <= 0.0 || area.height <= 0.0) {
        return std::nullopt;
    }
    const double across = std::max(0.0, std::min(1.0, (px - area.x) / area.width));
    const double up = 1.0 - std::max(0.0, std::min(1.0, (py - area.y) / area.height));
    const double fx = flipped ? up : across;
    const double fy = flipped ? across : up;
    ZoomAxes which = axes;
    if (flipped) {
        which = axes == ZoomAxes::X ? ZoomAxes::Y : axes == ZoomAxes::Y ? ZoomAxes::X : axes;
    }
    std::optional<GestureFocus> focus;
    if (which != ZoomAxes::Y) {
        focus = GestureFocus{valueAlong(viewMinX(), viewMaxX(), fx, x.logarithmic, x.base), factor};
        x.view = zoomedAxis(x.view, x.bounds.low, x.bounds.high, fx, factor, x.logarithmic, x.base,
                            x.minimumSpan, maxZoom, panKeep);
    }
    if (which != ZoomAxes::X) {
        y.view = zoomedAxis(y.view, y.bounds.low, y.bounds.high, fy, factor, y.logarithmic, y.base,
                            y.minimumSpan, maxZoom, panKeep);
    }
    return focus;
}

void Camera::panBy(double dx, double dy)
{
    if (area.width <= 0.0 || area.height <= 0.0) {
        return;
    }
    const double spanX = x.viewSpan();
    const double spanY = y.viewSpan();
    const double alongX = flipped ? -dy / area.height : dx / area.width;
    const double alongY = flipped ? dx / area.width : -dy / area.height;
    x.view.pan = clampPan(x.view.pan - alongX * spanX, x.view.zoom, x.bounds.low, x.bounds.high,
                          x.logarithmic, x.base, panKeep);
    y.view.pan = clampPan(y.view.pan - alongY * spanY, y.view.zoom, y.bounds.low, y.bounds.high,
                          y.logarithmic, y.base, panKeep);
}

void Camera::setViewRange(double x0, double x1, double y0, double y1)
{
    const Span wantX = positiveSpan(std::min(x0, x1), std::max(x0, x1), x.bounds.low, x.bounds.high,
                                    x.logarithmic);
    const Span wantY = positiveSpan(std::min(y0, y1), std::max(y0, y1), y.bounds.low, y.bounds.high,
                                    y.logarithmic);
    x.view = viewedAxis(x.bounds.low, x.bounds.high, wantX.low, wantX.high, x.logarithmic, x.base,
                        x.minimumSpan, maxZoom, panKeep);
    y.view = viewedAxis(y.bounds.low, y.bounds.high, wantY.low, wantY.high, y.logarithmic, y.base,
                        y.minimumSpan, maxZoom, panKeep);
}

double Camera::dataXAt(double px, double py) const
{
    if (flipped) {
        if (area.height <= 0.0) {
            return viewMinX();
        }
        const double up = std::max(0.0, std::min(1.0, (py - area.y) / area.height));
        return valueAlong(viewMinX(), viewMaxX(), 1.0 - up, x.logarithmic, x.base);
    }
    if (area.width <= 0.0) {
        return viewMinX();
    }
    const double at = std::max(0.0, std::min(1.0, (px - area.x) / area.width));
    return valueAlong(viewMinX(), viewMaxX(), at, x.logarithmic, x.base);
}

double Camera::dataYAt(double py, double px) const
{
    if (flipped) {
        if (area.width <= 0.0) {
            return viewMinY();
        }
        const double across = std::max(0.0, std::min(1.0, (px - area.x) / area.width));
        return valueAlong(viewMinY(), viewMaxY(), across, y.logarithmic, y.base);
    }
    if (area.height <= 0.0) {
        return viewMinY();
    }
    const double at = std::max(0.0, std::min(1.0, (py - area.y) / area.height));
    return valueAlong(viewMinY(), viewMaxY(), 1.0 - at, y.logarithmic, y.base);
}

bool Camera::zoomToRegion(double px0, double py0, double px1, double py1,
                          std::optional<GestureFocus>* focus)
{
    if (area.width <= 0.0 || area.height <= 0.0) {
        return false;
    }
    const double left = std::min(px0, px1);
    const double right = std::max(px0, px1);
    const double top = std::min(py0, py1);
    const double bottom = std::max(py0, py1);
    if (right - left < minimumBand || bottom - top < minimumBand) {
        return false;
    }
    const double x0 = dataXAt(left, bottom);
    const double x1 = dataXAt(right, top);
    const double y0 = dataYAt(bottom, left);
    const double y1 = dataYAt(top, right);
    const double span = axisPosition(viewMaxX(), x.logarithmic, x.base) -
                        axisPosition(viewMinX(), x.logarithmic, x.base);
    const double band =
        axisPosition(x1, x.logarithmic, x.base) - axisPosition(x0, x.logarithmic, x.base);
    if (focus != nullptr && span > 0.0 && band > 0.0) {
        *focus = GestureFocus{valueAlong(x0, x1, 0.5, x.logarithmic, x.base), span / band};
    }
    setViewRange(x0, x1, y0, y1);
    return true;
}

} // namespace plotcore
