// SPDX-FileCopyrightText: 2026 Andreas Martin
// SPDX-License-Identifier: GPL-3.0-only

#include "plotcore/plotcore_c.h"

#include "gui/PlotLevels.hpp"
#include "gui/PlotProjection.hpp"
#include "plotcore/LineStore.hpp"
#include "plotcore/View.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace {

double niceStep(double span, int target)
{
    if (!(span > 0.0) || !std::isfinite(span)) {
        return 0.0;
    }
    const double raw = span / static_cast<double>(std::max(1, target));
    const double magnitude = std::pow(10.0, std::floor(std::log10(raw)));
    if (!(magnitude > 0.0)) {
        return 0.0;
    }
    const double scaled = raw / magnitude;
    const double factor = scaled <= 1.0 ? 1.0 : scaled <= 2.0 ? 2.0 : scaled <= 5.0 ? 5.0 : 10.0;
    return magnitude * factor;
}

/// How many numbered ticks the pane has room for. PlotFrame.logTickRoom, with
/// Theme.readout at ten pixels: two of them up the side, three along the foot.
int logTickRoom(double extent, bool vertical)
{
    const double per = (vertical ? 2.0 : 3.0) * 10.0;
    const int room = static_cast<int>(std::floor(std::max(0.0, extent) / per));
    return std::max(2, std::min(9, room));
}

/// Which whole multiples of a power are numbered. PlotFrame.logSublabels:
/// the power itself once more than one power is crossed, and a subset of the
/// multiples (1, 2, 3, 4, 6 on base ten) inside a single power.
std::vector<int> logSublabels(double low, double high, double base)
{
    if (!(low > 0.0) || !(high > low) || !(std::isfinite(base) && base > 1.0)) {
        return {1};
    }
    // log(x)/log(b), not logOf. log(0.001)/log(10) misses -3 by an ulp, and
    // that ulp is which multiples get a number.
    const double lmin = std::log(low) / std::log(base);
    const double lmax = std::log(high) / std::log(base);
    const double below = lmin == std::floor(lmin) ? lmin - 1.0 : std::floor(lmin);
    const int crossed = static_cast<int>(std::floor(lmax) - below);
    if (crossed > 1) {
        return {1};
    }
    std::vector<int> found;
    if (lmax - lmin > 0.4) {
        const int count = static_cast<int>(std::floor(std::floor(base) / 2.0)) + 1;
        for (int i = 0; i < count; ++i) {
            const double at = count > 1 ? static_cast<double>(i) / static_cast<double>(count - 1) : 0.0;
            const int coefficient = static_cast<int>(std::lround(std::pow(base, at)));
            if (std::find(found.begin(), found.end(), coefficient) == found.end()) {
                found.push_back(coefficient);
            }
        }
        return found;
    }
    for (int coefficient = 1; coefficient <= static_cast<int>(base); ++coefficient) {
        found.push_back(coefficient);
    }
    return found;
}

struct LogTicks
{
    std::vector<double> majors;
    std::vector<double> minors;
    bool linear = false;
};

/// PlotFrame.logLocate. Majors are powers of the base. Minors are the whole
/// multiples between them, and only while the powers themselves are not
/// already strided. One tick in view falls back to round linear steps.
LogTicks logLocate(double low, double high, double base, int request)
{
    LogTicks located;
    if (!(low > 0.0) || !(high > low) || !(std::isfinite(base) && base > 1.0)) {
        return located;
    }
    const double efmin = gui::logOf(low, base);
    const double efmax = gui::logOf(high, base);
    const int emin = static_cast<int>(std::ceil(efmin - 1e-10));
    const int emax = static_cast<int>(std::floor(efmax + 1e-10));
    const int avail = emax - emin + 1;

    int wanted = std::max(2, request);
    int stride = static_cast<int>(std::floor(static_cast<double>(avail) / static_cast<double>(wanted + 1))) + 1;
    const int got = static_cast<int>(std::ceil(static_cast<double>(avail) / static_cast<double>(std::max(stride, 1))));
    if (got <= wanted) {
        wanted = got;
    }
    std::vector<int> decades;
    if (wanted <= 0) {
        decades = {emin - 1, emax + 1};
        stride = decades[1] - decades[0];
    } else if (wanted == 1) {
        const int mid = static_cast<int>(std::lround((efmin + efmax) / 2.0));
        stride = std::max(mid - (emin - 1), (emax + 1) - mid);
        decades = {mid - stride, mid, mid + stride};
    } else {
        stride = (avail - 1) / (wanted - 1);
        if (static_cast<double>(stride) < static_cast<double>(avail) / static_cast<double>(wanted)) {
            stride = avail / wanted;
        }
        if (stride < 1) {
            stride = 1;
        }
        const int olo = std::max(avail - stride * wanted, 0);
        const int ohi = std::min(avail - stride * (wanted - 1), stride);
        int offset = ((-emin) % stride + stride) % stride;
        if (!(olo <= offset && offset < ohi)) {
            offset = olo;
        }
        for (int exponent = emin + offset - stride;
             exponent <= emax + stride && decades.size() < 1024; exponent += stride) {
            decades.push_back(exponent);
        }
    }

    const auto inView = [&](double value) {
        return value >= low * (1.0 - 1e-12) && value <= high * (1.0 + 1e-12);
    };
    for (int exponent : decades) {
        const double value = std::pow(base, static_cast<double>(exponent));
        if (std::isfinite(value) && inView(value)) {
            located.majors.push_back(value);
        }
    }

    if (avail < 10 && base >= 3.0 && (stride == 1 || avail <= 1)) {
        for (int exponent = emin - 1; exponent <= emax && located.minors.size() < 4096; ++exponent) {
            const double power = std::pow(base, static_cast<double>(exponent));
            for (int multiple = 2; multiple < base && located.minors.size() < 4096; ++multiple) {
                const double value = static_cast<double>(multiple) * power;
                if (std::isfinite(value) && inView(value)) {
                    located.minors.push_back(value);
                }
            }
        }
    }

    if (stride == 1 && static_cast<int>(located.majors.size() + located.minors.size()) <= 1) {
        const double step = niceStep(high - low, 8);
        located.minors.clear();
        if (step > 0.0 && high > low) {
            const double first = std::ceil(low / step - 1e-9);
            for (int i = 0; located.minors.size() < 64; ++i) {
                const double value = (first + static_cast<double>(i)) * step;
                if (value > high + step * 1e-9) {
                    break;
                }
                if (!std::isfinite(value)) {
                    continue;
                }
                bool near = false;
                for (double major : located.majors) {
                    if (std::abs(major - value) < step / 2.0) {
                        near = true;
                    }
                }
                if (!near) {
                    located.minors.push_back(value);
                }
            }
        }
        located.linear = true;
    }
    return located;
}

bool coefficientLabeled(double value, double base, const std::vector<int>& allowed)
{
    if (!(value > 0.0) || !(base > 1.0)) {
        return false;
    }
    const double fx = std::log(value) / std::log(base);
    const bool decade = std::abs(fx - std::round(fx)) < 1e-10;
    const double exponent = decade ? std::round(fx) : std::floor(fx);
    const int coefficient = static_cast<int>(std::lround(std::pow(base, fx - exponent)));
    return std::find(allowed.begin(), allowed.end(), coefficient) != allowed.end();
}

void placeTick(std::vector<H5PlotTick>& ticks, const gui::AxisMapping& map, const gui::PlotView& view,
               int axisCode, int series, double value, bool labeled, bool logarithmic, unsigned char red,
               unsigned char green, unsigned char blue)
{
    if (!map.draws(value)) {
        return;
    }
    const double fraction = map.fractionOf(value);
    if (fraction < -0.001 || fraction > 1.001) {
        return;
    }
    H5PlotTick tick{};
    tick.value = value;
    tick.axis = axisCode;
    tick.series = series;
    tick.red = red;
    tick.green = green;
    tick.blue = blue;
    tick.alpha = 255;
    tick.labeled = labeled ? 1 : 0;
    tick.logarithmic = logarithmic ? 1 : 0;
    const bool vertical = axisCode != 0;
    if (vertical) {
        tick.x = 0.0;
        tick.y = view.height - fraction * view.height;
    } else {
        tick.x = fraction * view.width;
        tick.y = view.height;
    }
    ticks.push_back(tick);
}

void appendTicks(std::vector<H5PlotTick>& ticks, const gui::PlotView& view, int axisCode, int series,
                 unsigned char red, unsigned char green, unsigned char blue)
{
    const bool vertical = axisCode != 0;
    const bool logarithmic = vertical ? view.yLog : view.xLog;
    const double base = vertical ? view.yLogBase : view.xLogBase;
    const double low = vertical ? view.yMin : view.xMin;
    const double high = vertical ? view.yMax : view.xMax;
    const gui::AxisMapping map = gui::mappingOver(low, high, logarithmic, base);
    if (!map.usable || !(view.width > 0.0) || !(view.height > 0.0)) {
        return;
    }

    if (logarithmic && low > 0.0 && high > low) {
        const double extent = vertical ? view.height : view.width;
        const LogTicks located = logLocate(low, high, base, logTickRoom(extent, vertical));
        if (!located.majors.empty() || !located.minors.empty()) {
            const std::vector<int> allowed = logSublabels(low, high, base);
            for (double value : located.majors) {
                const bool labeled = located.linear || coefficientLabeled(value, base, allowed);
                placeTick(ticks, map, view, axisCode, series, value, labeled, !located.linear, red, green,
                          blue);
            }
            for (double value : located.minors) {
                const bool labeled = located.linear || coefficientLabeled(value, base, allowed);
                placeTick(ticks, map, view, axisCode, series, value, labeled, !located.linear, red, green,
                          blue);
            }
            return;
        }
    }

    const double step = niceStep(high - low, 6);
    if (!(step > 0.0)) {
        return;
    }
    const double first = std::ceil(low / step - 1e-9) * step;
    int placed = 0;
    for (double value = first; value <= high + step * 0.5 && placed < 12; value += step) {
        if (std::isfinite(value)) {
            placeTick(ticks, map, view, axisCode, series, value, true, false, red, green, blue);
            ++placed;
        }
    }
}

} // namespace

struct Pose
{
    double shiftX = 0.0;
    double shiftY = 0.0;
    /// 1 is the common axis. See PlotCamera::lineSpan.
    double scaleY = 1.0;
    bool own = false;
    /// The scale this line's own axis is drawn on. Copied from the camera
    /// when the line leaves the common axis, and left alone when the camera
    /// changes afterwards: the checkbox is that axis, not this one.
    bool yLog = false;
    double ySpan = 1.0;
};

struct H5Plot
{
    gui::LineStore store;
    gui::PlotCamera camera;
    std::vector<Pose> poses;
    double xSpan = 1.0;
    std::vector<gui::PlotLine> lines;
    gui::PlotAxis axis;
    std::vector<QPointF> points;
    std::vector<QPointF> dataPoints;
    std::vector<gui::PlotRun> runs;
    std::vector<int> lineRuns;
    std::vector<int> runLines;
    std::vector<H5PlotRun> painted;
    std::vector<H5PlotTick> ticks;
    int width = 640;
    int height = 400;
    double pixelRatio = 1.0;

    void syncExtent()
    {
        camera.setDataExtent(store.xMin(), store.xMax(), store.minimum(), store.maximum(),
                             store.xPositiveMinimum(), store.positiveMinimum(), store.length());
    }

    int paneColumns() const
    {
        const int pixels = std::max(1, static_cast<int>(std::lround(width * pixelRatio)));
        return std::max(gui::kColumnQuantum, (pixels / gui::kColumnQuantum) * gui::kColumnQuantum);
    }
};

extern "C" {

H5Plot* h5plot_create(void)
{
    return new H5Plot();
}

void h5plot_destroy(H5Plot* plot)
{
    delete plot;
}

int h5plot_add_line(H5Plot* plot, const double* y, long long n, int red, int green, int blue)
{
    if (plot == nullptr) {
        return -1;
    }
    const int index = plot->store.addLine(y, static_cast<qsizetype>(n), QColor(red, green, blue));
    if (index >= 0) {
        plot->poses.emplace_back();
    }
    plot->syncExtent();
    plot->camera.reset();
    return index;
}

int h5plot_begin_line(H5Plot* plot, long long n, int red, int green, int blue)
{
    if (plot == nullptr) {
        return -1;
    }
    const int index = plot->store.beginLine(n, QColor(red, green, blue));
    if (index >= 0) {
        plot->poses.emplace_back();
    }
    return index;
}

void h5plot_add_samples(H5Plot* plot, int index, const double* y, long long n)
{
    if (plot != nullptr) {
        plot->store.addSamples(index, y, n);
    }
}

void h5plot_finish_line(H5Plot* plot, int index)
{
    if (plot == nullptr) {
        return;
    }
    plot->store.finishLine(index);
    plot->syncExtent();
    plot->camera.reset();
}

void h5plot_set_reader(H5Plot* plot, int index, H5PlotRead read, void* user)
{
    if (plot != nullptr) {
        plot->store.setWindowReader(index, read, user);
    }
}

void h5plot_set_axis(H5Plot* plot, const double* x, long long n)
{
    if (plot == nullptr) {
        return;
    }
    plot->store.setAxis(x, static_cast<qsizetype>(std::max<long long>(n, 0)));
    plot->syncExtent();
    plot->camera.reset();
}

void h5plot_begin_axis(H5Plot* plot, long long n)
{
    if (plot != nullptr) {
        plot->store.beginAxis(n);
    }
}

void h5plot_add_axis_samples(H5Plot* plot, const double* x, long long n)
{
    if (plot != nullptr) {
        plot->store.addAxisSamples(x, n);
    }
}

void h5plot_finish_axis(H5Plot* plot)
{
    if (plot == nullptr) {
        return;
    }
    plot->store.finishAxis();
    plot->syncExtent();
    plot->camera.reset();
}

void h5plot_set_axis_reader(H5Plot* plot, H5PlotRead read, void* user)
{
    if (plot != nullptr) {
        plot->store.setAxisReader(read, user);
    }
}

int h5plot_set_line_axis(H5Plot* plot, int index, const double* x, long long n)
{
    if (plot == nullptr) {
        return 0;
    }
    const bool ok =
        plot->store.setLineAxis(index, x, static_cast<qsizetype>(std::max<long long>(n, 0)));
    if (!ok) {
        return 0;
    }
    plot->syncExtent();
    plot->camera.reset();
    return 1;
}

int h5plot_begin_line_axis(H5Plot* plot, int index, long long n)
{
    if (plot == nullptr) {
        return 0;
    }
    return plot->store.beginLineAxis(index, n) ? 1 : 0;
}

void h5plot_add_line_axis_samples(H5Plot* plot, int index, const double* x, long long n)
{
    if (plot != nullptr) {
        plot->store.addLineAxisSamples(index, x, n);
    }
}

int h5plot_finish_line_axis(H5Plot* plot, int index)
{
    if (plot == nullptr) {
        return 0;
    }
    if (!plot->store.finishLineAxis(index)) {
        return 0;
    }
    plot->syncExtent();
    plot->camera.reset();
    return 1;
}

void h5plot_set_line_axis_reader(H5Plot* plot, int index, H5PlotRead read, void* user)
{
    if (plot != nullptr) {
        plot->store.setLineAxisReader(index, read, user);
    }
}

void h5plot_clear(H5Plot* plot)
{
    if (plot == nullptr) {
        return;
    }
    plot->store.clearLines();
    plot->poses.clear();
    plot->xSpan = 1.0;
    plot->lines.clear();
    plot->points.clear();
    plot->dataPoints.clear();
    plot->runs.clear();
    plot->runLines.clear();
    plot->painted.clear();
    plot->ticks.clear();
}

void h5plot_set_pane(H5Plot* plot, int width, int height, double pixel_ratio)
{
    if (plot == nullptr) {
        return;
    }
    plot->width = std::max(1, width);
    plot->height = std::max(1, height);
    plot->pixelRatio = pixel_ratio > 0.0 ? pixel_ratio : 1.0;
}

namespace {

bool lineBand(const H5Plot* plot, int index, bool logarithmic, double& low, double& high)
{
    double rawLow = 0.0;
    double rawHigh = 0.0;
    if (plot == nullptr || !plot->store.lineExtent(index, rawLow, rawHigh)) {
        return false;
    }
    double positive = 0.0;
    const bool hasPositive = plot->store.linePositiveMinimum(index, positive);
    if (gui::PlotCamera::bandSpan(logarithmic, rawLow, rawHigh, positive, hasPositive, low, high)) {
        return true;
    }
    // A logarithmic axis with nothing above zero still has a column. The
    // empty decade is what the common axis falls back to, and every sample
    // is a gap, so the stroke is absent rather than the column disappearing.
    if (!logarithmic) {
        return false;
    }
    low = 1.0;
    high = plot->camera.yLogBase() > 1.0 ? plot->camera.yLogBase() : 10.0;
    return high > low;
}

struct HeldWindow
{
    std::size_t index = 0;
    double low = 0.0;
    double high = 0.0;
    bool yLog = false;
    double lineLow = 0.0;
    double lineHigh = 0.0;
};

std::vector<HeldWindow> holdOwn(const H5Plot* plot)
{
    std::vector<HeldWindow> held;
    for (std::size_t i = 0; i < plot->poses.size(); ++i) {
        const Pose& pose = plot->poses[i];
        if (!pose.own) {
            continue;
        }
        double lineLow = 0.0;
        double lineHigh = 0.0;
        if (!lineBand(plot, static_cast<int>(i), pose.yLog, lineLow, lineHigh)) {
            continue;
        }
        const gui::PlotCamera::Span window =
            plot->camera.lineSpan(pose.scaleY, pose.shiftY, pose.yLog, lineLow, lineHigh);
        if (!(window.high > window.low) || !std::isfinite(window.low) || !std::isfinite(window.high)) {
            continue;
        }
        held.push_back(HeldWindow{i, window.low, window.high, pose.yLog, lineLow, lineHigh});
    }
    return held;
}

void restoreOwn(H5Plot* plot, const std::vector<HeldWindow>& held)
{
    for (const HeldWindow& item : held) {
        Pose& pose = plot->poses[item.index];
        double scale = pose.scaleY;
        double shift = pose.shiftY;
        if (plot->camera.fitLine(scale, shift, item.yLog, item.lineLow, item.lineHigh, item.low,
                                 item.high)) {
            pose.scaleY = scale;
            pose.shiftY = shift;
        }
    }
}

void claimOwn(Pose& pose, const gui::PlotCamera& camera)
{
    if (!pose.own) {
        pose.yLog = camera.yLog();
    }
    pose.own = true;
}

} // namespace

void h5plot_set_ylog(H5Plot* plot, int on)
{
    if (plot == nullptr || plot->camera.yLog() == (on != 0)) {
        return;
    }
    // The shift is stored in the common axis's units. Changing that scale
    // would read it back in the new ones and the bands would jump. The
    // window is taken first, in the data's own units, and put back after.
    const std::vector<HeldWindow> held = holdOwn(plot);
    plot->camera.setYLog(on != 0);
    plot->store.setYLog(on != 0);
    restoreOwn(plot, held);
}

void h5plot_set_xlog(H5Plot* plot, int on)
{
    if (plot == nullptr) {
        return;
    }
    plot->camera.setXLog(on != 0);
    plot->store.setXLog(on != 0);
}

void h5plot_set_x_log_base(H5Plot* plot, double base)
{
    if (plot != nullptr) {
        plot->camera.setXLogBase(base);
    }
}

void h5plot_set_y_log_base(H5Plot* plot, double base)
{
    if (plot == nullptr) {
        return;
    }
    const double before = plot->camera.yLogBase();
    const std::vector<HeldWindow> held = holdOwn(plot);
    plot->camera.setYLogBase(base);
    if (plot->camera.yLogBase() != before) {
        restoreOwn(plot, held);
    }
}

double h5plot_x_log_base(const H5Plot* plot)
{
    return plot != nullptr ? plot->camera.xLogBase() : 10.0;
}

double h5plot_y_log_base(const H5Plot* plot)
{
    return plot != nullptr ? plot->camera.yLogBase() : 10.0;
}

void h5plot_reset_view(H5Plot* plot)
{
    if (plot != nullptr) {
        plot->camera.reset();
    }
}

void h5plot_wheel(H5Plot* plot, double px, double py, double factor, int shift, int control, int alt)
{
    if (plot == nullptr || plot->width <= 0 || plot->height <= 0) {
        return;
    }
    const double fx = px / static_cast<double>(plot->width);
    const double fy = 1.0 - py / static_cast<double>(plot->height);
    // Alt+Ctrl names one curve. The hit is the same 14 pixels as the drag
    // that gives a curve its own axis: a wheel in empty space is not a
    // request to scale whichever stroke happens to be nearest on the pane,
    // and it is not a request to zoom the frame either.
    if (alt != 0 && control != 0 && shift == 0) {
        const int index = gui::nearestLine(plot->points, plot->runs, plot->lineRuns, px, py, 14.0);
        if (index < 0 || index >= static_cast<int>(plot->poses.size())) {
            return;
        }
        Pose& pose = plot->poses[static_cast<std::size_t>(index)];
        claimOwn(pose, plot->camera);
        double lineLow = 0.0;
        double lineHigh = 0.0;
        if (lineBand(plot, index, pose.yLog, lineLow, lineHigh)) {
            plot->camera.scaleLine(pose.scaleY, pose.shiftY, fy, factor, pose.yLog, lineLow,
                                   lineHigh);
        } else {
            plot->camera.scaleLine(pose.scaleY, pose.shiftY, fy, factor);
        }
        return;
    }
    plot->camera.zoomAt(fx, fy, factor, shift != 0 && control == 0, control != 0 && shift == 0);
}

void h5plot_pan(H5Plot* plot, double dx, double dy)
{
    if (plot != nullptr) {
        plot->camera.panBy(dx, dy, plot->width, plot->height);
    }
}

int h5plot_zoom_rect(H5Plot* plot, double x0, double y0, double x1, double y1)
{
    if (plot == nullptr) {
        return 0;
    }
    return plot->camera.zoomToRegion(x0, y0, x1, y1, plot->width, plot->height) ? 1 : 0;
}

void h5plot_stack_lines(H5Plot* plot)
{
    if (plot == nullptr) {
        return;
    }
    plot->syncExtent();
    const int count = plot->store.lineCount();
    if (plot->poses.size() != static_cast<std::size_t>(count)) {
        plot->poses.resize(static_cast<std::size_t>(count));
    }
    struct Item
    {
        int index;
        double low;
        double high;
    };
    std::vector<Item> items;
    items.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        double low = 0.0;
        double high = 0.0;
        if (!plot->store.lineExtent(i, low, high)) {
            continue;
        }
        double positive = 0.0;
        const bool hasPositive = plot->store.linePositiveMinimum(i, positive);
        double from = 0.0;
        double to = 0.0;
        if (!gui::PlotCamera::bandSpan(plot->camera.yLog(), low, high, positive, hasPositive, from,
                                       to)) {
            continue;
        }
        items.push_back(Item{i, from, to});
    }
    if (items.empty()) {
        return;
    }
    plot->camera.resetY();
    const double bands = static_cast<double>(items.size());
    for (std::size_t band = 0; band < items.size(); ++band) {
        const Item& item = items[band];
        const double at = static_cast<double>(band);
        double scale = 1.0;
        double shift = 0.0;
        if (!plot->camera.placeLine(scale, shift, item.low, item.high, (bands - 1.0 - at) / bands,
                                    (bands - at) / bands)) {
            continue;
        }
        Pose& pose = plot->poses[static_cast<std::size_t>(item.index)];
        pose.scaleY = scale;
        pose.shiftY = shift;
        // placeLine used the camera's scale, so the pose has to name that
        // one. A scale the line had before the stack was in other units.
        pose.yLog = plot->camera.yLog();
        pose.own = true;
    }
}

void h5plot_set_range(H5Plot* plot, double x0, double x1, double y0, double y1)
{
    if (plot == nullptr) {
        return;
    }
    plot->camera.setViewRange(x0, x1, y0, y1);
}

void h5plot_set_own_axis(H5Plot* plot, int index, int on)
{
    if (plot == nullptr || index < 0 || index >= static_cast<int>(plot->poses.size())) {
        return;
    }
    Pose& pose = plot->poses[static_cast<std::size_t>(index)];
    if (on != 0) {
        claimOwn(pose, plot->camera);
        return;
    }
    pose.own = false;
    pose.shiftY = 0.0;
    pose.scaleY = 1.0;
    pose.yLog = false;
}

int h5plot_line_count(const H5Plot* plot)
{
    return plot != nullptr ? plot->store.lineCount() : 0;
}

int h5plot_own_axis(const H5Plot* plot, int index)
{
    if (plot == nullptr || index < 0 || index >= static_cast<int>(plot->poses.size())) {
        return 0;
    }
    return plot->poses[static_cast<std::size_t>(index)].own ? 1 : 0;
}

int h5plot_own_count(const H5Plot* plot)
{
    if (plot == nullptr) {
        return 0;
    }
    int count = 0;
    for (const Pose& pose : plot->poses) {
        if (pose.own) {
            ++count;
        }
    }
    return count;
}

int h5plot_shared_count(const H5Plot* plot)
{
    return plot != nullptr ? plot->store.sharedCount() : 0;
}

int h5plot_nearest(const H5Plot* plot, double px, double py)
{
    if (plot == nullptr) {
        return -1;
    }
    return gui::nearestLine(plot->points, plot->runs, plot->lineRuns, px, py, 14.0);
}

int h5plot_sample(const H5Plot* plot, double px, double py, H5PlotSample* out)
{
    if (plot == nullptr || out == nullptr || !(plot->width > 0.0) || !(plot->height > 0.0)) {
        return 0;
    }
    const double w = static_cast<double>(plot->width);
    const double h = static_cast<double>(plot->height);
    // The curve is drawn in the pane and nowhere else. A pointer on the y
    // axis is not over a sample: the numbers sit in the gutter, and the
    // stroke never reaches them. Answering anyway snaps the reading to
    // whichever sample is nearest that gutter, which is a point the reader
    // is not looking at.
    if (px < 0.0 || py < 0.0 || px > w || py > h) {
        return 0;
    }
    const gui::PlotView view =
        plot->camera.frame(w, h, plot->pixelRatio, static_cast<int>(plot->lines.size()));
    const gui::AxisMapping xMap = gui::xMappingOf(view);
    if (!xMap.usable || plot->lines.empty()) {
        return 0;
    }
    // The same walk PlotItem::nearestSample takes. The value is the sample
    // that was drawn, placed by the map that drew it, so a time base and a
    // line on its own y cannot disagree with the stroke. A later line wins a
    // tie, because it is the one on top.
    double bestDistance = std::numeric_limits<double>::infinity();
    int bestLine = -1;
    double bestX = 0.0;
    double bestY = 0.0;
    double bestPx = 0.0;
    double bestPy = 0.0;
    QColor bestColour;
    for (std::size_t index = 0; index < plot->lines.size(); ++index) {
        const gui::PlotLine& line = plot->lines[index];
        if (line.values == nullptr || line.count <= 0) {
            continue;
        }
        gui::PlotView drawn = view;
        if (index < plot->poses.size() && plot->poses[index].own) {
            const Pose& pose = plot->poses[index];
            double lineLow = 0.0;
            double lineHigh = 0.0;
            const gui::PlotCamera::Span span =
                lineBand(plot, static_cast<int>(index), pose.yLog, lineLow, lineHigh)
                    ? plot->camera.lineSpan(pose.scaleY, pose.shiftY, pose.yLog, lineLow, lineHigh)
                    : plot->camera.lineSpan(pose.scaleY, pose.shiftY);
            drawn.yMin = span.low;
            drawn.yMax = span.high;
            drawn.yLog = pose.yLog;
            drawn.yLogBase = plot->camera.yLogBase();
        }
        const gui::AxisMapping yMap = gui::yMappingOf(drawn);
        if (!yMap.usable) {
            continue;
        }
        for (qsizetype i = 0; i < line.count; ++i) {
            const double value = line.values[i];
            if (!std::isfinite(value)) {
                continue;
            }
            const double x = gui::xOf(line, plot->axis, i);
            if (!std::isfinite(x) || !xMap.draws(x) || !yMap.draws(value)) {
                continue;
            }
            const double sx = xMap.fractionOf(x) * w;
            const double sy = h - yMap.fractionOf(value) * h;
            // A sample kept so the stroke can enter the pane is not a sample
            // on it. Tracking one of those draws the reading in the gutter,
            // where the curve is not.
            if (sx < 0.0 || sy < 0.0 || sx > w || sy > h) {
                continue;
            }
            const double distance = (sx - px) * (sx - px) + (sy - py) * (sy - py);
            if (distance <= bestDistance) {
                bestDistance = distance;
                bestLine = static_cast<int>(index);
                bestX = x;
                bestY = value;
                bestPx = sx;
                bestPy = sy;
                bestColour = line.colour;
            }
        }
    }
    if (bestLine < 0) {
        return 0;
    }
    out->line = bestLine;
    out->x = bestX;
    out->y = bestY;
    out->px = bestPx;
    out->py = bestPy;
    out->red = static_cast<unsigned char>(bestColour.red());
    out->green = static_cast<unsigned char>(bestColour.green());
    out->blue = static_cast<unsigned char>(bestColour.blue());
    out->alpha = 255;
    return 1;
}

void h5plot_shift_line(H5Plot* plot, int index, double /*dx*/, double dy)
{
    if (plot == nullptr || index < 0 || index >= static_cast<int>(plot->poses.size())) {
        return;
    }
    // Only y. An x shift would move the curve off the position the shared x
    // axis still names. The line gains its own y axis so the numbers beside
    // it move with it; without that, the common ticks would keep describing
    // the place the curve just left.
    Pose& pose = plot->poses[static_cast<std::size_t>(index)];
    claimOwn(pose, plot->camera);
    const double scale = pose.scaleY > 0.0 ? pose.scaleY : 1.0;
    if (plot->height > 0 && pose.ySpan != 0.0) {
        pose.shiftY += dy / static_cast<double>(plot->height) * pose.ySpan / scale;
    }
}

double h5plot_view_min_x(const H5Plot* plot)
{
    return plot != nullptr ? plot->camera.viewMinX() : 0.0;
}

double h5plot_view_max_x(const H5Plot* plot)
{
    return plot != nullptr ? plot->camera.viewMaxX() : 1.0;
}

double h5plot_view_min_y(const H5Plot* plot)
{
    return plot != nullptr ? plot->camera.viewMinY() : 0.0;
}

double h5plot_view_max_y(const H5Plot* plot)
{
    return plot != nullptr ? plot->camera.viewMaxY() : 1.0;
}

double h5plot_data_x_at(const H5Plot* plot, double px)
{
    if (plot == nullptr) {
        return 0.0;
    }
    return plot->camera.dataXAt(px, static_cast<double>(plot->width));
}

double h5plot_data_y_at(const H5Plot* plot, double py)
{
    if (plot == nullptr) {
        return 0.0;
    }
    return plot->camera.dataYAt(py, static_cast<double>(plot->height));
}

int h5plot_project(H5Plot* plot)
{
    if (plot == nullptr) {
        return 0;
    }
    plot->store.setPaneColumns(plot->paneColumns());
    const double ySpan = plot->camera.viewMaxY() - plot->camera.viewMinY();
    const double yPixels = std::max(1.0, static_cast<double>(plot->height) * plot->pixelRatio);
    const double yPerPixel =
        !plot->camera.yLog() && ySpan > 0.0 && std::isfinite(ySpan) ? ySpan / yPixels : 0.0;
    plot->store.setYPerPixel(yPerPixel);
    plot->store.setVisibleRange(plot->camera.viewMinX(), plot->camera.viewMaxX());
    plot->store.fillInto(plot->lines, plot->axis);
    plot->syncExtent();

    plot->points.clear();
    plot->dataPoints.clear();
    plot->runs.clear();
    plot->runLines.clear();
    plot->painted.clear();
    plot->ticks.clear();
    const auto count = plot->lines.size();
    if (plot->poses.size() != count) {
        plot->poses.resize(count);
    }
    plot->xSpan = plot->camera.xSpan();
    const double commonY = plot->camera.ySpan();
    plot->lineRuns.assign(count + 1, 0);
    const gui::PlotView view =
        plot->camera.frame(plot->width, plot->height, plot->pixelRatio, static_cast<int>(count));
    bool anyShared = false;
    for (std::size_t i = 0; i < count; ++i) {
        Pose& pose = plot->poses[i];
        gui::PlotLine& line = plot->lines[i];
        pose.ySpan = commonY;
        line.ownY = false;
        gui::PlotView drawn = view;
        if (pose.own) {
            // The camera's scale is the common axis. This line's bit is its
            // own, and the stroke breaks where that scale has no place — a
            // value at or below zero — the same way a NaN already does.
            double lineLow = 0.0;
            double lineHigh = 0.0;
            const gui::PlotCamera::Span span =
                lineBand(plot, static_cast<int>(i), pose.yLog, lineLow, lineHigh)
                    ? plot->camera.lineSpan(pose.scaleY, pose.shiftY, pose.yLog, lineLow, lineHigh)
                    : plot->camera.lineSpan(pose.scaleY, pose.shiftY);
            drawn.yMin = span.low;
            drawn.yMax = span.high;
            drawn.yLog = pose.yLog;
            drawn.yLogBase = plot->camera.yLogBase();
            pose.ySpan = plot->camera.shiftUnits(pose.scaleY, pose.yLog, span.low, span.high);
        } else {
            anyShared = true;
        }
        plot->lineRuns[i] = static_cast<int>(plot->runs.size());
        const int runsBefore = static_cast<int>(plot->runs.size());
        const gui::PlotProjected projected =
            gui::projectLine(line, plot->axis, drawn, plot->points, plot->runs, &plot->dataPoints);
        for (int r = runsBefore; r < static_cast<int>(plot->runs.size()); ++r) {
            plot->runLines.push_back(static_cast<int>(i));
        }
        const QColor colour = line.colour;
        const auto alpha = static_cast<unsigned char>(
            std::lround(std::clamp(colour.alphaF() * line.opacity, 0.0, 1.0) * 255.0));
        const auto summarised = static_cast<unsigned char>(projected.decimated ? 1 : 0);
        for (int r = plot->lineRuns[i]; r < static_cast<int>(plot->runs.size()); ++r) {
            const gui::PlotRun& run = plot->runs[static_cast<std::size_t>(r)];
            plot->painted.push_back(H5PlotRun{run.first, run.count, static_cast<unsigned char>(colour.red()),
                                              static_cast<unsigned char>(colour.green()),
                                              static_cast<unsigned char>(colour.blue()), alpha,
                                              static_cast<float>(line.width), summarised});
        }
        if (pose.own) {
            appendTicks(plot->ticks, drawn, 2, static_cast<int>(i),
                        static_cast<unsigned char>(colour.red()),
                        static_cast<unsigned char>(colour.green()),
                        static_cast<unsigned char>(colour.blue()));
        }
    }
    plot->lineRuns[count] = static_cast<int>(plot->runs.size());
    appendTicks(plot->ticks, view, 0, -1, 180, 180, 180);
    if (anyShared || count == 0) {
        appendTicks(plot->ticks, view, 1, -1, 180, 180, 180);
    }
    plot->store.releaseRetired();
    return static_cast<int>(plot->points.size());
}

int h5plot_point_count(const H5Plot* plot)
{
    return plot != nullptr ? static_cast<int>(plot->points.size()) : 0;
}

void h5plot_copy_points(const H5Plot* plot, double* xy)
{
    if (plot == nullptr || xy == nullptr) {
        return;
    }
    for (std::size_t i = 0; i < plot->points.size(); ++i) {
        xy[i * 2] = plot->points[i].x();
        xy[i * 2 + 1] = plot->points[i].y();
    }
}

void h5plot_copy_data(const H5Plot* plot, double* xy)
{
    if (plot == nullptr || xy == nullptr) {
        return;
    }
    for (std::size_t i = 0; i < plot->dataPoints.size(); ++i) {
        xy[i * 2] = plot->dataPoints[i].x();
        xy[i * 2 + 1] = plot->dataPoints[i].y();
    }
}

void h5plot_copy_run_lines(const H5Plot* plot, int* lines)
{
    if (plot == nullptr || lines == nullptr) {
        return;
    }
    for (std::size_t i = 0; i < plot->runLines.size(); ++i) {
        lines[i] = plot->runLines[i];
    }
}

int h5plot_x_log(const H5Plot* plot)
{
    return plot != nullptr && plot->camera.xLog() ? 1 : 0;
}

int h5plot_y_log(const H5Plot* plot)
{
    return plot != nullptr && plot->camera.yLog() ? 1 : 0;
}

void h5plot_set_line_y_log(H5Plot* plot, int index, int on)
{
    if (plot == nullptr || index < 0 || index >= static_cast<int>(plot->poses.size())) {
        return;
    }
    Pose& pose = plot->poses[static_cast<std::size_t>(index)];
    // A line still on the common axis is that axis. The checkbox is the
    // way to change it; this call is the line's own column.
    if (!pose.own) {
        return;
    }
    const bool next = on != 0;
    if (pose.yLog == next) {
        return;
    }
    double rawLow = 0.0;
    double rawHigh = 0.0;
    if (!plot->store.lineExtent(index, rawLow, rawHigh)) {
        return;
    }
    double positive = 0.0;
    const bool hasPositive = plot->store.linePositiveMinimum(index, positive);
    double scale = pose.scaleY;
    double shift = pose.shiftY;
    if (!plot->camera.retargetLine(scale, shift, pose.yLog, next, rawLow, rawHigh, positive,
                                   hasPositive)) {
        return;
    }
    pose.scaleY = scale;
    pose.shiftY = shift;
    pose.yLog = next;
}

int h5plot_line_y_log(const H5Plot* plot, int index)
{
    if (plot == nullptr || index < 0 || index >= static_cast<int>(plot->poses.size())) {
        return 0;
    }
    const Pose& pose = plot->poses[static_cast<std::size_t>(index)];
    if (!pose.own) {
        return plot->camera.yLog() ? 1 : 0;
    }
    return pose.yLog ? 1 : 0;
}

void h5plot_line_y_range(const H5Plot* plot, int index, double* low, double* high)
{
    if (plot == nullptr || low == nullptr || high == nullptr) {
        return;
    }
    if (index >= 0 && index < static_cast<int>(plot->poses.size()) &&
        plot->poses[static_cast<std::size_t>(index)].own) {
        const Pose& pose = plot->poses[static_cast<std::size_t>(index)];
        double lineLow = 0.0;
        double lineHigh = 0.0;
        const gui::PlotCamera::Span span =
            lineBand(plot, index, pose.yLog, lineLow, lineHigh)
                ? plot->camera.lineSpan(pose.scaleY, pose.shiftY, pose.yLog, lineLow, lineHigh)
                : plot->camera.lineSpan(pose.scaleY, pose.shiftY);
        *low = span.low;
        *high = span.high;
        return;
    }
    *low = plot->camera.viewMinY();
    *high = plot->camera.viewMaxY();
}

int h5plot_run_count(const H5Plot* plot)
{
    return plot != nullptr ? static_cast<int>(plot->painted.size()) : 0;
}

void h5plot_copy_runs(const H5Plot* plot, H5PlotRun* runs)
{
    if (plot == nullptr || runs == nullptr) {
        return;
    }
    for (std::size_t i = 0; i < plot->painted.size(); ++i) {
        runs[i] = plot->painted[i];
    }
}

int h5plot_tick_count(const H5Plot* plot)
{
    return plot != nullptr ? static_cast<int>(plot->ticks.size()) : 0;
}

void h5plot_copy_ticks(const H5Plot* plot, H5PlotTick* ticks)
{
    if (plot == nullptr || ticks == nullptr) {
        return;
    }
    for (std::size_t i = 0; i < plot->ticks.size(); ++i) {
        ticks[i] = plot->ticks[i];
    }
}

} // extern "C"
