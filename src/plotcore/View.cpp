// SPDX-FileCopyrightText: 2026 Andreas Martin
// SPDX-License-Identifier: GPL-3.0-only

#include "plotcore/View.hpp"

#include <algorithm>
#include <cmath>

namespace gui {
namespace {

bool usableBase(double base)
{
    return std::isfinite(base) && base > 1.0;
}

} // namespace

void PlotCamera::setDataExtent(double xMin, double xMax, double yMin, double yMax,
                               double xPositiveMin, double yPositiveMin, long long samples)
{
    dataXMin_ = xMin;
    dataXMax_ = xMax;
    dataYMin_ = yMin;
    dataYMax_ = yMax;
    dataXPositive_ = xPositiveMin;
    dataYPositive_ = yPositiveMin;
    samples_ = std::max<long long>(samples, 0);
}

void PlotCamera::setXLog(bool on)
{
    if (xLog_ == on) {
        return;
    }
    xLog_ = on;
    reset();
}

void PlotCamera::setYLog(bool on)
{
    if (yLog_ == on) {
        return;
    }
    yLog_ = on;
    reset();
}

void PlotCamera::setXLogBase(double base)
{
    // A base at or below one is not a base. There is no nearest legal value
    // to correct it to, so it is refused and the axis stays as it was.
    if (!usableBase(base) || xLogBase_ == base) {
        return;
    }
    xLogBase_ = base;
    if (xLog_) {
        reset();
    }
}

void PlotCamera::setYLogBase(double base)
{
    if (!usableBase(base) || yLogBase_ == base) {
        return;
    }
    yLogBase_ = base;
    if (yLog_) {
        reset();
    }
}

void PlotCamera::reset()
{
    zoomX_ = 1.0;
    zoomY_ = 1.0;
    panX_ = 0.0;
    panY_ = 0.0;
}

void PlotCamera::resetY()
{
    zoomY_ = 1.0;
    panY_ = 0.0;
}

PlotCamera::Span PlotCamera::padded(double low, double high, bool logarithmic, double base)
{
    if (logarithmic) {
        if (!(low > 0.0) || !(high > 0.0) || !usableBase(base)) {
            return {1.0, usableBase(base) ? base : 10.0};
        }
        double from = logOf(low, base);
        double to = logOf(high, base);
        if (!(to > from)) {
            const double at = std::abs(from - std::round(from)) < 1e-10 ? std::round(from) : from;
            const bool exact = std::abs(at - std::round(at)) < 1e-10;
            from = exact ? at - 1.0 : std::floor(at);
            to = exact ? at + 1.0 : std::ceil(at);
        }
        const double air = (to - from) * 0.05;
        return {std::pow(base, from - air), std::pow(base, to + air)};
    }
    const double air = high > low ? (high - low) * 0.05 : 1.0;
    return {low - air, high + air};
}

PlotCamera::Span PlotCamera::paddedX() const
{
    if (xLog_) {
        return padded(dataXPositive_, dataXMax_, true, xLogBase_);
    }
    return padded(dataXMin_, dataXMax_, false, xLogBase_);
}

PlotCamera::Span PlotCamera::paddedY() const
{
    if (yLog_) {
        return padded(dataYPositive_, dataYMax_, true, yLogBase_);
    }
    return padded(dataYMin_, dataYMax_, false, yLogBase_);
}

double PlotCamera::axisPosition(double value, bool logarithmic, double base) const
{
    return logarithmic ? logOf(value, base) : value;
}

double PlotCamera::axisValue(double position, bool logarithmic, double base) const
{
    return logarithmic ? std::pow(base, position) : position;
}

double PlotCamera::clampPan(double pan, double zoom, double low, double high, bool logarithmic,
                            double base) const
{
    const double full = axisPosition(high, logarithmic, base) - axisPosition(low, logarithmic, base);
    const double span = full / zoom;
    const double room = full / 2.0 + span * (0.5 - kPanKeep);
    return std::clamp(pan, -room, room);
}

double PlotCamera::viewMinX() const
{
    const Span axis = paddedX();
    const double from = axisPosition(axis.low, xLog_, xLogBase_);
    const double to = axisPosition(axis.high, xLog_, xLogBase_);
    const double span = (to - from) / zoomX_;
    return axisValue((from + to) / 2.0 + panX_ - span / 2.0, xLog_, xLogBase_);
}

double PlotCamera::viewMaxX() const
{
    const Span axis = paddedX();
    const double from = axisPosition(axis.low, xLog_, xLogBase_);
    const double to = axisPosition(axis.high, xLog_, xLogBase_);
    const double span = (to - from) / zoomX_;
    return axisValue((from + to) / 2.0 + panX_ + span / 2.0, xLog_, xLogBase_);
}

double PlotCamera::viewMinY() const
{
    const Span axis = paddedY();
    const double from = axisPosition(axis.low, yLog_, yLogBase_);
    const double to = axisPosition(axis.high, yLog_, yLogBase_);
    const double span = (to - from) / zoomY_;
    return axisValue((from + to) / 2.0 + panY_ - span / 2.0, yLog_, yLogBase_);
}

double PlotCamera::viewMaxY() const
{
    const Span axis = paddedY();
    const double from = axisPosition(axis.low, yLog_, yLogBase_);
    const double to = axisPosition(axis.high, yLog_, yLogBase_);
    const double span = (to - from) / zoomY_;
    return axisValue((from + to) / 2.0 + panY_ + span / 2.0, yLog_, yLogBase_);
}

void PlotCamera::zoomedAxis(double& zoom, double& pan, double low, double high, double fraction,
                            double factor, bool logarithmic, double base, double minimumSpan)
{
    const double from = axisPosition(low, logarithmic, base);
    const double to = axisPosition(high, logarithmic, base);
    const double full = to - from;
    const double span = full / zoom;
    const double held = (from + to) / 2.0 + pan - span / 2.0 + fraction * span;
    double ceiling = maxZoom();
    if (logarithmic && minimumSpan > 0.0 && usableBase(base) && full > 0.0) {
        const auto width = [&](double nextSpan) {
            return std::pow(base, held + (1.0 - fraction) * nextSpan) -
                   std::pow(base, held - fraction * nextSpan);
        };
        if (!(width(full) > minimumSpan)) {
            ceiling = 1.0;
        } else {
            double narrow = 0.0;
            double wide = full;
            for (int i = 0; i < 60; ++i) {
                const double mid = (narrow + wide) / 2.0;
                if (width(mid) >= minimumSpan) {
                    wide = mid;
                } else {
                    narrow = mid;
                }
            }
            ceiling = full / wide;
        }
    }
    const double next = std::max(1.0, std::min(std::max(ceiling, zoom), zoom * factor));
    const double nextSpan = full / next;
    const double centre = held - fraction * nextSpan + nextSpan / 2.0;
    zoom = next;
    pan = clampPan(centre - (from + to) / 2.0, next, low, high, logarithmic, base);
}

void PlotCamera::zoomAt(double fx, double fy, double factor, bool onlyX, bool onlyY)
{
    fx = std::clamp(fx, 0.0, 1.0);
    fy = std::clamp(fy, 0.0, 1.0);
    const Span xAxis = paddedX();
    const Span yAxis = paddedY();
    if (!onlyY) {
        zoomedAxis(zoomX_, panX_, xAxis.low, xAxis.high, fx, factor, xLog_, xLogBase_,
                   minimumSpanX());
    }
    if (!onlyX) {
        zoomedAxis(zoomY_, panY_, yAxis.low, yAxis.high, fy, factor, yLog_, yLogBase_, 0.0);
    }
}

void PlotCamera::panBy(double dx, double dy, double areaWidth, double areaHeight)
{
    if (!(areaWidth > 0.0) || !(areaHeight > 0.0)) {
        return;
    }
    const Span xAxis = paddedX();
    const Span yAxis = paddedY();
    const double spanX =
        (axisPosition(xAxis.high, xLog_, xLogBase_) - axisPosition(xAxis.low, xLog_, xLogBase_)) /
        zoomX_;
    const double spanY =
        (axisPosition(yAxis.high, yLog_, yLogBase_) - axisPosition(yAxis.low, yLog_, yLogBase_)) /
        zoomY_;
    panX_ = clampPan(panX_ - dx * spanX / areaWidth, zoomX_, xAxis.low, xAxis.high, xLog_,
                     xLogBase_);
    panY_ = clampPan(panY_ + dy * spanY / areaHeight, zoomY_, yAxis.low, yAxis.high, yLog_,
                     yLogBase_);
}

double PlotCamera::valueAlong(double low, double high, double at, bool logarithmic,
                              double base) const
{
    const double from = axisPosition(low, logarithmic, base);
    const double to = axisPosition(high, logarithmic, base);
    return axisValue(from + at * (to - from), logarithmic, base);
}

double PlotCamera::dataXAt(double px, double areaWidth) const
{
    if (!(areaWidth > 0.0)) {
        return viewMinX();
    }
    const double at = std::clamp(px / areaWidth, 0.0, 1.0);
    return valueAlong(viewMinX(), viewMaxX(), at, xLog_, xLogBase_);
}

double PlotCamera::dataYAt(double py, double areaHeight) const
{
    if (!(areaHeight > 0.0)) {
        return viewMinY();
    }
    // y grows downward on the pane and upward on the axis.
    const double at = std::clamp(py / areaHeight, 0.0, 1.0);
    return valueAlong(viewMinY(), viewMaxY(), 1.0 - at, yLog_, yLogBase_);
}

double PlotCamera::logZoomCeiling(double full, double held, double fraction, double minimumSpan,
                                  double base) const
{
    if (!(full > 0.0) || !(minimumSpan > 0.0) || !usableBase(base)) {
        return maxZoom();
    }
    const auto width = [&](double span) {
        return std::pow(base, held + (1.0 - fraction) * span) - std::pow(base, held - fraction * span);
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
        } else {
            narrow = mid;
        }
    }
    return std::max(1.0, full / wide);
}

PlotCamera::AxisPlacement PlotCamera::viewedAxis(double low, double high, double from, double to,
                                                 bool logarithmic, double base,
                                                 double minimumSpan) const
{
    const double axisFrom = axisPosition(low, logarithmic, base);
    const double axisTo = axisPosition(high, logarithmic, base);
    const double wantFrom = axisPosition(from, logarithmic, base);
    const double wantTo = axisPosition(to, logarithmic, base);
    const double full = axisTo - axisFrom;
    const double span = std::abs(wantTo - wantFrom);
    if (!(full > 0.0) || !(span > 0.0)) {
        return {};
    }
    const double centre = (wantFrom + wantTo) / 2.0;
    const double ceiling = logarithmic && minimumSpan > 0.0
                               ? logZoomCeiling(full, centre, 0.5, minimumSpan, base)
                               : maxZoom();
    const double zoom = std::max(1.0, std::min(ceiling, full / span));
    return {zoom, clampPan(centre - (axisFrom + axisTo) / 2.0, zoom, low, high, logarithmic, base)};
}

void PlotCamera::setViewRange(double x0, double x1, double y0, double y1)
{
    const Span xAxis = paddedX();
    const Span yAxis = paddedY();
    double xFrom = std::min(x0, x1);
    double xTo = std::max(x0, x1);
    double yFrom = std::min(y0, y1);
    double yTo = std::max(y0, y1);
    // A bound at or below zero is not a place on a logarithmic axis. Clip to
    // the part that exists; a window with nothing above zero is the whole axis.
    if (xLog_) {
        if (!(xTo > 0.0)) {
            xFrom = xAxis.low;
            xTo = xAxis.high;
        } else {
            xFrom = std::max(xFrom, xAxis.low);
        }
    }
    if (yLog_) {
        if (!(yTo > 0.0)) {
            yFrom = yAxis.low;
            yTo = yAxis.high;
        } else {
            yFrom = std::max(yFrom, yAxis.low);
        }
    }
    const AxisPlacement x =
        viewedAxis(xAxis.low, xAxis.high, xFrom, xTo, xLog_, xLogBase_, minimumSpanX());
    const AxisPlacement y = viewedAxis(yAxis.low, yAxis.high, yFrom, yTo, yLog_, yLogBase_, 0.0);
    zoomX_ = x.zoom;
    panX_ = x.pan;
    zoomY_ = y.zoom;
    panY_ = y.pan;
}

bool PlotCamera::zoomToRegion(double px0, double py0, double px1, double py1, double areaWidth,
                              double areaHeight)
{
    if (!(areaWidth > 0.0) || !(areaHeight > 0.0)) {
        return false;
    }
    px0 = std::clamp(px0, 0.0, areaWidth);
    px1 = std::clamp(px1, 0.0, areaWidth);
    py0 = std::clamp(py0, 0.0, areaHeight);
    py1 = std::clamp(py1, 0.0, areaHeight);
    const double left = std::min(px0, px1);
    const double right = std::max(px0, px1);
    const double top = std::min(py0, py1);
    const double bottom = std::max(py0, py1);
    if (right - left < kMinimumBand || bottom - top < kMinimumBand) {
        return false;
    }
    // The top of the band is the larger y. dataYAt already turns the pane
    // upside down, so the two corners are asked in the order they sit.
    setViewRange(dataXAt(left, areaWidth), dataXAt(right, areaWidth), dataYAt(bottom, areaHeight),
                 dataYAt(top, areaHeight));
    return true;
}

double PlotCamera::maxZoom() const
{
    // A handful of samples across the pane. The width of the axis is the
    // wrong length for that: a few hundred seconds holding two million
    // samples stopped the zoom while every column was still a stack of
    // them. PlotSurface.maxZoom counts the elements, and so does this.
    const double length = samples_ > 1 ? static_cast<double>(samples_)
                                       : std::abs(dataXMax_ - dataXMin_) + 1.0;
    return std::max(256.0, length / 16.0);
}

double PlotCamera::minimumSpanX() const
{
    return std::abs(paddedX().high - paddedX().low) / maxZoom();
}

PlotCamera::Span PlotCamera::ownSpan(double lineLow, double lineHigh, double shift) const
{
    const Span whole = padded(lineLow, lineHigh, false, 10.0);
    const Span common = paddedY();
    const double cFrom = axisPosition(common.low, yLog_, yLogBase_);
    const double cTo = axisPosition(common.high, yLog_, yLogBase_);
    const double vFrom = axisPosition(viewMinY(), yLog_, yLogBase_);
    const double vTo = axisPosition(viewMaxY(), yLog_, yLogBase_);
    double lowF = 0.0;
    double highF = 1.0;
    if (cTo > cFrom) {
        lowF = (vFrom - cFrom) / (cTo - cFrom);
        highF = (vTo - cFrom) / (cTo - cFrom);
    }
    const double span = whole.high - whole.low;
    return {whole.low + lowF * span + shift, whole.low + highF * span + shift};
}

PlotCamera::Span PlotCamera::lineSpan(double scale, double shift) const
{
    const double from = axisPosition(viewMinY(), yLog_, yLogBase_);
    const double to = axisPosition(viewMaxY(), yLog_, yLogBase_);
    // Scale one is the shift on its own, and it is this formula rather than
    // the general one below so that a shifted line does not move by an ulp
    // when nothing about it changed. (from - low) / full * full is not
    // always from - low.
    if (!(scale > 0.0) || scale == 1.0) {
        return {axisValue(from + shift, yLog_, yLogBase_), axisValue(to + shift, yLog_, yLogBase_)};
    }
    const Span axis = paddedY();
    const double low = axisPosition(axis.low, yLog_, yLogBase_);
    const double high = axisPosition(axis.high, yLog_, yLogBase_);
    const double full = high - low;
    const double view = to - from;
    if (!(full > 0.0) || !(view > 0.0)) {
        return {axisValue(from + shift, yLog_, yLogBase_), axisValue(to + shift, yLog_, yLogBase_)};
    }
    const double lowF = (from - low) / full;
    const double highF = (to - low) / full;
    const double home = full / scale;
    const double homeFrom = (low + high) / 2.0 + shift - home / 2.0;
    return {axisValue(homeFrom + lowF * home, yLog_, yLogBase_),
            axisValue(homeFrom + highF * home, yLog_, yLogBase_)};
}

PlotCamera::Span PlotCamera::shiftedSpan(double shift) const
{
    return lineSpan(1.0, shift);
}

void PlotCamera::scaleLine(double& scale, double& shift, double fractionUp, double factor) const
{
    if (!(factor > 0.0) || !std::isfinite(factor)) {
        return;
    }
    if (!(scale > 0.0) || !std::isfinite(scale)) {
        scale = 1.0;
    }
    const Span axis = paddedY();
    const double low = axisPosition(axis.low, yLog_, yLogBase_);
    const double high = axisPosition(axis.high, yLog_, yLogBase_);
    const double full = high - low;
    const double from = axisPosition(viewMinY(), yLog_, yLogBase_);
    const double to = axisPosition(viewMaxY(), yLog_, yLogBase_);
    const double view = to - from;
    if (!(full > 0.0) || !(view > 0.0)) {
        return;
    }
    const Span window = lineSpan(scale, shift);
    const double visFrom = axisPosition(window.low, yLog_, yLogBase_);
    const double visTo = axisPosition(window.high, yLog_, yLogBase_);
    const double span = visTo - visFrom;
    if (!(span > 0.0)) {
        return;
    }
    const double g = std::clamp(fractionUp, 0.0, 1.0);
    const double held = visFrom + g * span;
    // The same two limits as a wheel on the common axis, in the units a pan
    // is measured in. The whole axis is as far out as zoom 1, and the
    // ceiling is as far in as maxZoom. A line already past either one stays
    // there: the limit is not a reason to throw the reader back.
    const double minSpan = full / maxZoom();
    const double maxSpan = full;
    double next = span / factor;
    if (factor >= 1.0) {
        next = std::max(next, std::min(span, minSpan));
    } else {
        next = std::min(next, std::max(span, maxSpan));
    }
    if (!(next > 0.0)) {
        return;
    }
    const double lowF = (from - low) / full;
    const double highF = (to - low) / full;
    const double viewFrac = highF - lowF;
    if (!(viewFrac > 0.0)) {
        return;
    }
    const double newFrom = held - g * next;
    const double newHome = next / viewFrac;
    if (!(newHome > 0.0)) {
        return;
    }
    scale = full / newHome;
    const double homeFrom = newFrom - lowF * newHome;
    shift = homeFrom + newHome / 2.0 - (low + high) / 2.0;
}

bool PlotCamera::bandSpan(bool logarithmic, double low, double high, double positive,
                          bool hasPositive, double& from, double& to)
{
    if (!std::isfinite(low) || !std::isfinite(high) || !(high >= low)) {
        return false;
    }
    if (logarithmic) {
        if (!(high > 0.0)) {
            return false;
        }
        if (!(low > 0.0)) {
            if (!hasPositive || !(positive > 0.0) || !(high > positive)) {
                return false;
            }
            low = positive;
        }
    }
    from = low;
    to = high;
    return true;
}

bool PlotCamera::placeLine(double& scale, double& shift, double low, double high,
                           double fractionLow, double fractionHigh) const
{
    if (!(fractionHigh > fractionLow) || !std::isfinite(low) || !std::isfinite(high) ||
        !(high >= low)) {
        return false;
    }
    if (yLog_ && (!(low > 0.0) || !(high > 0.0))) {
        return false;
    }
    const Span band = padded(low, high, yLog_, yLogBase_);
    const double p0 = axisPosition(band.low, yLog_, yLogBase_);
    const double p1 = axisPosition(band.high, yLog_, yLogBase_);
    const double dataSpan = p1 - p0;
    const Span axis = paddedY();
    const double axisLow = axisPosition(axis.low, yLog_, yLogBase_);
    const double axisHigh = axisPosition(axis.high, yLog_, yLogBase_);
    const double full = axisHigh - axisLow;
    const double from = axisPosition(viewMinY(), yLog_, yLogBase_);
    const double to = axisPosition(viewMaxY(), yLog_, yLogBase_);
    const double viewFrac = (to - from) / full;
    if (!(dataSpan > 0.0) || !(full > 0.0) || !(viewFrac > 0.0)) {
        return false;
    }
    // The visible window, in the axis's own positions. The data occupies
    // fractionLow..fractionHigh of it, so the rest of the window is the
    // other bands.
    const double visSpan = dataSpan / (fractionHigh - fractionLow);
    const double visFrom = p0 - fractionLow * visSpan;
    const double lowF = (from - axisLow) / full;
    const double home = visSpan / viewFrac;
    if (!(home > 0.0)) {
        return false;
    }
    scale = full / home;
    const double homeFrom = visFrom - lowF * home;
    shift = homeFrom + home / 2.0 - (axisLow + axisHigh) / 2.0;
    return std::isfinite(scale) && std::isfinite(shift) && scale > 0.0;
}

bool PlotCamera::viewFrame(double& axisLow, double& axisHigh, double& from, double& to) const
{
    const Span axis = paddedY();
    axisLow = axisPosition(axis.low, yLog_, yLogBase_);
    axisHigh = axisPosition(axis.high, yLog_, yLogBase_);
    from = axisPosition(viewMinY(), yLog_, yLogBase_);
    to = axisPosition(viewMaxY(), yLog_, yLogBase_);
    return (axisHigh > axisLow) && (to > from) && std::isfinite(axisLow) && std::isfinite(axisHigh) &&
           std::isfinite(from) && std::isfinite(to);
}

bool PlotCamera::referenceSpan(bool logarithmic, double lineLow, double lineHigh, double& full,
                               double& center) const
{
    // The same units lineSpan will read the shift back in. Mixing the common
    // axis's units into a line on another scale is how a stored shift jumps
    // when the checkbox changes and the line does not.
    double low = 0.0;
    double high = 0.0;
    if (logarithmic == yLog_) {
        const Span axis = paddedY();
        low = axisPosition(axis.low, yLog_, yLogBase_);
        high = axisPosition(axis.high, yLog_, yLogBase_);
    } else {
        const Span band = padded(lineLow, lineHigh, logarithmic, yLogBase_);
        low = axisPosition(band.low, logarithmic, yLogBase_);
        high = axisPosition(band.high, logarithmic, yLogBase_);
    }
    full = high - low;
    center = (low + high) / 2.0;
    return full > 0.0 && std::isfinite(full) && std::isfinite(center);
}

PlotCamera::Span PlotCamera::lineSpan(double scale, double shift, bool logarithmic, double lineLow,
                                      double lineHigh) const
{
    if (logarithmic == yLog_) {
        return lineSpan(scale, shift);
    }
    double axisLow = 0.0;
    double axisHigh = 0.0;
    double from = 0.0;
    double to = 0.0;
    double full = 0.0;
    double center = 0.0;
    if (!viewFrame(axisLow, axisHigh, from, to) ||
        !referenceSpan(logarithmic, lineLow, lineHigh, full, center)) {
        return {lineLow, lineHigh};
    }
    if (!(scale > 0.0) || !std::isfinite(scale)) {
        scale = 1.0;
    }
    const double common = axisHigh - axisLow;
    const double lowF = (from - axisLow) / common;
    const double highF = (to - axisLow) / common;
    const double home = full / scale;
    const double homeFrom = center + shift - home / 2.0;
    return {axisValue(homeFrom + lowF * home, logarithmic, yLogBase_),
            axisValue(homeFrom + highF * home, logarithmic, yLogBase_)};
}

bool PlotCamera::placeLine(double& scale, double& shift, double low, double high, double fractionLow,
                           double fractionHigh, bool logarithmic) const
{
    if (logarithmic == yLog_) {
        return placeLine(scale, shift, low, high, fractionLow, fractionHigh);
    }
    if (!(fractionHigh > fractionLow) || !std::isfinite(low) || !std::isfinite(high) ||
        !(high >= low)) {
        return false;
    }
    if (logarithmic && (!(low > 0.0) || !(high > 0.0))) {
        return false;
    }
    const Span band = padded(low, high, logarithmic, yLogBase_);
    const double p0 = axisPosition(band.low, logarithmic, yLogBase_);
    const double p1 = axisPosition(band.high, logarithmic, yLogBase_);
    const double dataSpan = p1 - p0;
    double axisLow = 0.0;
    double axisHigh = 0.0;
    double from = 0.0;
    double to = 0.0;
    double full = 0.0;
    double center = 0.0;
    if (!viewFrame(axisLow, axisHigh, from, to) ||
        !referenceSpan(logarithmic, low, high, full, center)) {
        return false;
    }
    const double common = axisHigh - axisLow;
    const double viewFrac = (to - from) / common;
    if (!(dataSpan > 0.0) || !(viewFrac > 0.0)) {
        return false;
    }
    const double visSpan = dataSpan / (fractionHigh - fractionLow);
    const double visFrom = p0 - fractionLow * visSpan;
    const double lowF = (from - axisLow) / common;
    const double home = visSpan / viewFrac;
    if (!(home > 0.0)) {
        return false;
    }
    scale = full / home;
    const double homeFrom = visFrom - lowF * home;
    shift = homeFrom + home / 2.0 - center;
    return std::isfinite(scale) && std::isfinite(shift) && scale > 0.0;
}

bool PlotCamera::fitLine(double& scale, double& shift, bool logarithmic, double lineLow,
                         double lineHigh, double windowLow, double windowHigh) const
{
    if (!std::isfinite(windowLow) || !std::isfinite(windowHigh) || !(windowHigh > windowLow)) {
        return false;
    }
    if (logarithmic && (!(windowLow > 0.0) || !(windowHigh > 0.0))) {
        return false;
    }
    double axisLow = 0.0;
    double axisHigh = 0.0;
    double from = 0.0;
    double to = 0.0;
    double full = 0.0;
    double center = 0.0;
    if (!viewFrame(axisLow, axisHigh, from, to) ||
        !referenceSpan(logarithmic, lineLow, lineHigh, full, center)) {
        return false;
    }
    const double common = axisHigh - axisLow;
    const double lowF = (from - axisLow) / common;
    const double highF = (to - axisLow) / common;
    const double viewFrac = highF - lowF;
    const double visFrom = axisPosition(windowLow, logarithmic, yLogBase_);
    const double visTo = axisPosition(windowHigh, logarithmic, yLogBase_);
    const double visSpan = visTo - visFrom;
    if (!(viewFrac > 0.0) || !(visSpan > 0.0)) {
        return false;
    }
    const double home = visSpan / viewFrac;
    if (!(home > 0.0)) {
        return false;
    }
    scale = full / home;
    const double homeFrom = visFrom - lowF * home;
    shift = homeFrom + home / 2.0 - center;
    return std::isfinite(scale) && std::isfinite(shift) && scale > 0.0;
}

bool PlotCamera::retargetLine(double& scale, double& shift, bool fromLog, bool toLog, double low,
                              double high, double positive, bool hasPositive) const
{
    double fromA = 0.0;
    double toA = 0.0;
    double fractionLow = 0.0;
    double fractionHigh = 1.0;
    if (bandSpan(fromLog, low, high, positive, hasPositive, fromA, toA)) {
        const Span window = lineSpan(scale, shift, fromLog, fromA, toA);
        const Span data = padded(fromA, toA, fromLog, yLogBase_);
        const double p0 = axisPosition(data.low, fromLog, yLogBase_);
        const double p1 = axisPosition(data.high, fromLog, yLogBase_);
        const double v0 = axisPosition(window.low, fromLog, yLogBase_);
        const double v1 = axisPosition(window.high, fromLog, yLogBase_);
        const double vis = v1 - v0;
        if (!(vis > 0.0) || !(p1 > p0)) {
            return false;
        }
        fractionLow = (p0 - v0) / vis;
        fractionHigh = (p1 - v0) / vis;
        if (!(fractionHigh > fractionLow)) {
            return false;
        }
    }
    double fromB = 0.0;
    double toB = 0.0;
    if (!bandSpan(toLog, low, high, positive, hasPositive, fromB, toB)) {
        // Nothing on the new scale has a place. The empty decade keeps the
        // band, and every sample is a gap.
        if (!toLog) {
            return false;
        }
        fromB = 1.0;
        toB = usableBase(yLogBase_) ? yLogBase_ : 10.0;
    }
    return placeLine(scale, shift, fromB, toB, fractionLow, fractionHigh, toLog);
}

void PlotCamera::scaleLine(double& scale, double& shift, double fractionUp, double factor,
                           bool logarithmic, double lineLow, double lineHigh) const
{
    if (logarithmic == yLog_) {
        scaleLine(scale, shift, fractionUp, factor);
        return;
    }
    if (!(factor > 0.0) || !std::isfinite(factor)) {
        return;
    }
    if (!(scale > 0.0) || !std::isfinite(scale)) {
        scale = 1.0;
    }
    double axisLow = 0.0;
    double axisHigh = 0.0;
    double from = 0.0;
    double to = 0.0;
    double full = 0.0;
    double center = 0.0;
    if (!viewFrame(axisLow, axisHigh, from, to) ||
        !referenceSpan(logarithmic, lineLow, lineHigh, full, center)) {
        return;
    }
    const double common = axisHigh - axisLow;
    const double lowF = (from - axisLow) / common;
    const double highF = (to - axisLow) / common;
    const double viewFrac = highF - lowF;
    if (!(viewFrac > 0.0)) {
        return;
    }
    const Span window = lineSpan(scale, shift, logarithmic, lineLow, lineHigh);
    const double visFrom = axisPosition(window.low, logarithmic, yLogBase_);
    const double visTo = axisPosition(window.high, logarithmic, yLogBase_);
    const double span = visTo - visFrom;
    if (!(span > 0.0)) {
        return;
    }
    const double g = std::clamp(fractionUp, 0.0, 1.0);
    const double held = visFrom + g * span;
    const double minSpan = full / maxZoom();
    const double maxSpan = full;
    double next = span / factor;
    if (factor >= 1.0) {
        next = std::max(next, std::min(span, minSpan));
    } else {
        next = std::min(next, std::max(span, maxSpan));
    }
    if (!(next > 0.0)) {
        return;
    }
    const double newFrom = held - g * next;
    const double newHome = next / viewFrac;
    if (!(newHome > 0.0)) {
        return;
    }
    scale = full / newHome;
    const double homeFrom = newFrom - lowF * newHome;
    shift = homeFrom + newHome / 2.0 - center;
}

double PlotCamera::shiftUnits(double scale, bool logarithmic, double windowLow,
                              double windowHigh) const
{
    if (logarithmic == yLog_ || !(scale > 0.0)) {
        return ySpan();
    }
    const double vis = axisPosition(windowHigh, logarithmic, yLogBase_) -
                       axisPosition(windowLow, logarithmic, yLogBase_);
    if (!(vis > 0.0) || !std::isfinite(vis)) {
        return ySpan();
    }
    return vis * scale;
}

double PlotCamera::xSpan() const
{
    return axisPosition(viewMaxX(), xLog_, xLogBase_) - axisPosition(viewMinX(), xLog_, xLogBase_);
}

double PlotCamera::ySpan() const
{
    return axisPosition(viewMaxY(), yLog_, yLogBase_) - axisPosition(viewMinY(), yLog_, yLogBase_);
}

PlotView PlotCamera::frame(double width, double height, double pixelRatio, int lineCount) const
{
    PlotView view;
    view.xMin = viewMinX();
    view.xMax = viewMaxX();
    view.yMin = viewMinY();
    view.yMax = viewMaxY();
    view.xLog = xLog_;
    view.yLog = yLog_;
    view.xLogBase = xLogBase_;
    view.yLogBase = yLogBase_;
    view.width = width;
    view.height = height;
    view.pixelRatio = pixelRatio;
    const int lines = std::max(lineCount, 1);
    view.maxColumns = std::max(1, kMaxVertices / (4 * lines));
    return view;
}

namespace {

double distanceToSegment(double px, double py, double ax, double ay, double bx, double by)
{
    const double dx = bx - ax;
    const double dy = by - ay;
    const double length = dx * dx + dy * dy;
    double t = 0.0;
    if (length > 0.0) {
        t = std::clamp(((px - ax) * dx + (py - ay) * dy) / length, 0.0, 1.0);
    }
    const double x = ax + t * dx - px;
    const double y = ay + t * dy - py;
    return std::sqrt(x * x + y * y);
}

} // namespace

int nearestLine(const std::vector<QPointF>& points, const std::vector<PlotRun>& runs,
                const std::vector<int>& lineRuns, double px, double py, double maxPixels)
{
    if (lineRuns.size() < 2 || !(maxPixels > 0.0)) {
        return -1;
    }
    int found = -1;
    double best = maxPixels;
    const int lines = static_cast<int>(lineRuns.size()) - 1;
    for (int line = 0; line < lines; ++line) {
        const int from = lineRuns[static_cast<std::size_t>(line)];
        const int to = lineRuns[static_cast<std::size_t>(line) + 1];
        for (int run = from; run < to; ++run) {
            const PlotRun& stroke = runs[static_cast<std::size_t>(run)];
            for (int i = 1; i < stroke.count; ++i) {
                const QPointF& a = points[static_cast<std::size_t>(stroke.first + i - 1)];
                const QPointF& b = points[static_cast<std::size_t>(stroke.first + i)];
                const double distance = distanceToSegment(px, py, a.x(), a.y(), b.x(), b.y());
                // Later lines are drawn on top, so an equal distance is theirs.
                if (distance <= best) {
                    best = distance;
                    found = line;
                }
            }
        }
    }
    return found;
}

} // namespace gui
