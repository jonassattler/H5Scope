// SPDX-FileCopyrightText: 2026 Andreas Martin
// SPDX-License-Identifier: GPL-3.0-only

// The QWidget plot and the RAM store under it, without a window manager.
//
// LineStore is DatasetPlot with the file taken out: a borrowed buffer, a
// pyramid, a whole-line summary and a closer look. The cases below are the
// reasons that object exists -- a spike the envelope must keep, a zoom that
// folds from the pyramid rather than stretching the summary, and a widget
// that empties when the store is cleared.

#include "plotcore/LineStore.hpp"
#include "plotcore/PlotWidget.hpp"
#include "plotcore/View.hpp"

#include <QApplication>

#include <cmath>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <span>
#include <utility>
#include <vector>

using Catch::Approx;

namespace {

/// Before QApplication exists. A static Qt on Windows asks for the Vista
/// style as it constructs the application, and that style is a plugin this
/// binary does not have -- the lookup is an access violation, which is why
/// these three cases died and every other case in the process did not. Fusion
/// is the one the offscreen plugin can actually draw with. The platform is
/// set here as well as by ctest, so running the binary by hand does the same.
const bool kHeadless = [] {
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    }
    if (qEnvironmentVariableIsEmpty("QT_STYLE_OVERRIDE")) {
        qputenv("QT_STYLE_OVERRIDE", QByteArrayLiteral("Fusion"));
    }
    return true;
}();

QApplication& qt()
{
    (void)kHeadless;
    static int argc = 1;
    static char arg0[] = "test_plotcore";
    static char* argv[] = {arg0, nullptr};
    // Leaked. Destroying the application at exit tears the platform plugin
    // down after the colour table and the thread pool, and that is a crash
    // on Windows. The process is a test; it does not need the shutdown.
    static auto* app = new QApplication(argc, argv);
    return *app;
}

std::vector<double> impulse(int count, int at, double spike)
{
    std::vector<double> values(static_cast<std::size_t>(count), 0.0);
    values[static_cast<std::size_t>(at)] = spike;
    return values;
}

bool holds(std::span<const double> values, double wanted)
{
    return std::any_of(values.begin(), values.end(),
                       [wanted](double value) { return value == wanted; });
}

} // namespace

TEST_CASE("the store keeps a one-sample spike in the whole-line summary", "[plotcore]")
{
    const std::vector<double> values = impulse(10'000, 4'000, 17.0);
    gui::LineStore store;
    REQUIRE(store.addLine(values.data(), static_cast<qsizetype>(values.size())) == 0);
    REQUIRE(store.minimum() == Approx(0.0));
    REQUIRE(store.maximum() == Approx(17.0));
    REQUIRE(store.length() == 10'000);
    store.setPaneColumns(256);
    REQUIRE(holds(store.drawnValues(0), 17.0));
}

TEST_CASE("a closer look is a run of the line, not a stretch of the summary", "[plotcore]")
{
    qt();
    std::vector<double> values(50'000, 0.0);
    values[25'000] = 9.0;
    values[25'001] = -3.0;

    gui::LineStore store;
    store.addLine(values.data(), static_cast<qsizetype>(values.size()));
    store.setPaneColumns(256);
    const auto whole = store.drawnValues(0);
    REQUIRE(holds(whole, 9.0));
    REQUIRE(whole.size() < values.size());

    store.setVisibleRange(24'950.0, 25'050.0);
    const auto close = store.drawnValues(0);
    REQUIRE(holds(close, 9.0));
    REQUIRE(holds(close, -3.0));
    // The closer fold covers a hundred elements; the whole-line summary
    // covered the whole line. If the zoom had only stretched the summary,
    // both pointers would be the same buffer.
    REQUIRE(close.data() != whole.data());
    REQUIRE((close.size() < whole.size() || close.size() < 400));
}

TEST_CASE("clearLines empties the widget it last filled", "[plotcore]")
{
    qt();
    const std::vector<double> values = {0.0, 1.0, 0.5};
    gui::LineStore store;
    store.addLine(values.data(), static_cast<qsizetype>(values.size()));
    gui::PlotWidget widget;
    std::vector<gui::PlotLine> lines;
    gui::PlotAxis axis;
    store.fillInto(lines, axis);
    widget.setLines(std::move(lines), axis);
    REQUIRE(widget.lineCount() == 1);
    store.clearLines();
    widget.clear();
    REQUIRE(widget.lineCount() == 0);
    REQUIRE(store.lineCount() == 0);
}

TEST_CASE("the widget's full view covers the padded data", "[plotcore]")
{
    qt();
    std::vector<double> values(1'000);
    for (int i = 0; i < 1'000; ++i) {
        values[static_cast<std::size_t>(i)] = static_cast<double>(i);
    }
    gui::LineStore store;
    store.addLine(values.data(), static_cast<qsizetype>(values.size()));

    gui::PlotWidget widget;
    widget.resize(640, 400);
    std::vector<gui::PlotLine> lines;
    gui::PlotAxis axis;
    store.fillInto(lines, axis);
    widget.setLines(std::move(lines), axis);
    widget.setDataExtent(store.xMin(), store.xMax(), store.minimum(), store.maximum(),
                         store.xPositiveMinimum(), store.positiveMinimum(), store.length());
    widget.resetView();

    REQUIRE(widget.viewMinX() < store.xMin());
    REQUIRE(widget.viewMaxX() > store.xMax());
    REQUIRE(widget.viewMinY() < store.minimum());
    REQUIRE(widget.viewMaxY() > store.maximum());
}

TEST_CASE("a logarithmic y axis starts at the smallest positive value", "[plotcore]")
{
    const std::vector<double> values = {0.0, -2.0, 0.25, 4.0};
    gui::LineStore store;
    store.addLine(values.data(), static_cast<qsizetype>(values.size()));
    REQUIRE(store.minimum() == Approx(-2.0));
    REQUIRE(store.maximum() == Approx(4.0));
    REQUIRE(store.positiveMinimum() == Approx(0.25));
}

namespace {

gui::PlotCamera cameraOver(double yMin, double yMax)
{
    gui::PlotCamera camera;
    camera.setDataExtent(0.0, 100.0, yMin, yMax, 0.0, yMin > 0.0 ? yMin : 0.0, 1'000);
    camera.reset();
    return camera;
}

double fractionOf(double value, double low, double high)
{
    return (value - low) / (high - low);
}

} // namespace

TEST_CASE("scaling one line in y holds the value under the pointer", "[plotcore]")
{
    gui::PlotCamera camera = cameraOver(0.0, 10.0);
    const double y0 = camera.viewMinY();
    const double y1 = camera.viewMaxY();
    double scale = 1.0;
    double shift = 0.0;
    constexpr double at = 0.25;
    camera.scaleLine(scale, shift, at, 2.0);

    REQUIRE(camera.viewMinY() == Approx(y0));
    REQUIRE(camera.viewMaxY() == Approx(y1));
    const gui::PlotCamera::Span window = camera.lineSpan(scale, shift);
    const double held = y0 + at * (y1 - y0);
    REQUIRE(window.low + at * (window.high - window.low) == Approx(held));
    REQUIRE(window.high - window.low == Approx((y1 - y0) / 2.0));

    // Shift zooms x. The line's y window is not part of that gesture.
    const gui::PlotCamera::Span beforeX = window;
    camera.zoomAt(0.4, 0.7, 3.0, true, false);
    const gui::PlotCamera::Span afterX = camera.lineSpan(scale, shift);
    REQUIRE(afterX.low == Approx(beforeX.low));
    REQUIRE(afterX.high == Approx(beforeX.high));
}

TEST_CASE("a wheel on y zooms a scaled line about the same point", "[plotcore]")
{
    gui::PlotCamera camera = cameraOver(0.0, 10.0);
    double scale = 1.0;
    double shift = 0.0;
    camera.scaleLine(scale, shift, 0.2, 2.0);
    constexpr double at = 0.35;
    const gui::PlotCamera::Span before = camera.lineSpan(scale, shift);
    const double pointer = before.low + at * (before.high - before.low);
    const double common = camera.viewMinY() + at * (camera.viewMaxY() - camera.viewMinY());
    camera.zoomAt(0.5, at, 2.0, false, true);
    const gui::PlotCamera::Span after = camera.lineSpan(scale, shift);
    REQUIRE(after.low + at * (after.high - after.low) == Approx(pointer));
    REQUIRE(camera.viewMinY() + at * (camera.viewMaxY() - camera.viewMinY()) == Approx(common));
    REQUIRE(after.high - after.low == Approx((before.high - before.low) / 2.0));
}

TEST_CASE("a pan moves a scaled line by the same fraction of the pane", "[plotcore]")
{
    gui::PlotCamera camera = cameraOver(0.0, 10.0);
    double scale = 1.0;
    double shift = 0.0;
    camera.scaleLine(scale, shift, 0.5, 2.0);
    const gui::PlotCamera::Span before = camera.lineSpan(scale, shift);
    const double common0 = camera.viewMinY();
    const double commonSpan = camera.viewMaxY() - common0;
    camera.panBy(0.0, 20.0, 200.0, 100.0);
    const gui::PlotCamera::Span after = camera.lineSpan(scale, shift);
    const double commonMoved = (camera.viewMinY() - common0) / commonSpan;
    const double lineMoved = (after.low - before.low) / (before.high - before.low);
    REQUIRE(commonMoved == Approx(0.2));
    REQUIRE(lineMoved == Approx(commonMoved));
}

TEST_CASE("a rectangle zoom takes the same fraction of a scaled line", "[plotcore]")
{
    gui::PlotCamera camera = cameraOver(0.0, 10.0);
    double scale = 1.0;
    double shift = 0.0;
    // Off centre, so the shift is not zero and a bug that only works for a
    // centred scale still fails.
    camera.scaleLine(scale, shift, 0.2, 2.0);
    const gui::PlotCamera::Span before = camera.lineSpan(scale, shift);
    const double common0 = camera.viewMinY();
    const double common1 = camera.viewMaxY();
    REQUIRE(camera.zoomToRegion(50.0, 25.0, 150.0, 75.0, 200.0, 100.0));
    const gui::PlotCamera::Span after = camera.lineSpan(scale, shift);
    REQUIRE(fractionOf(camera.viewMinY(), common0, common1) == Approx(0.25));
    REQUIRE(fractionOf(camera.viewMaxY(), common0, common1) == Approx(0.75));
    REQUIRE(fractionOf(after.low, before.low, before.high) == Approx(0.25));
    REQUIRE(fractionOf(after.high, before.low, before.high) == Approx(0.75));
}

TEST_CASE("a rectangle zoom keeps a shifted line on the same fraction", "[plotcore]")
{
    gui::PlotCamera camera = cameraOver(0.0, 10.0);
    constexpr double shift = 1.5;
    const gui::PlotCamera::Span before = camera.lineSpan(1.0, shift);
    REQUIRE(camera.zoomToRegion(50.0, 25.0, 150.0, 75.0, 200.0, 100.0));
    const gui::PlotCamera::Span after = camera.lineSpan(1.0, shift);
    REQUIRE(after.low == Approx(camera.viewMinY() + shift));
    REQUIRE(after.high == Approx(camera.viewMaxY() + shift));
    REQUIRE(fractionOf(after.low, before.low, before.high) == Approx(0.25));
    REQUIRE(fractionOf(after.high, before.low, before.high) == Approx(0.75));
}

TEST_CASE("a rectangle on a logarithmic y takes the same fraction of a scaled line", "[plotcore]")
{
    gui::PlotCamera camera = cameraOver(1.0, 1'000.0);
    camera.setYLog(true);
    double scale = 1.0;
    double shift = 0.0;
    camera.scaleLine(scale, shift, 0.4, 2.0);
    const gui::PlotCamera::Span before = camera.lineSpan(scale, shift);
    REQUIRE(camera.zoomToRegion(50.0, 25.0, 150.0, 75.0, 200.0, 100.0));
    const gui::PlotCamera::Span after = camera.lineSpan(scale, shift);
    const double low = std::log(before.low);
    const double high = std::log(before.high);
    REQUIRE(fractionOf(std::log(after.low), low, high) == Approx(0.25));
    REQUIRE(fractionOf(std::log(after.high), low, high) == Approx(0.75));
}

void spanOf(const gui::PlotLine& line, const gui::PlotAxis& axis, double& low, double& high)
{
    low = std::numeric_limits<double>::infinity();
    high = -low;
    for (qsizetype i = 0; i < line.count; ++i) {
        const double x = gui::xOf(line, axis, i);
        if (!std::isfinite(x)) {
            continue;
        }
        low = std::min(low, x);
        high = std::max(high, x);
    }
}

TEST_CASE("a line with its own time is drawn on the shared clock", "[plotcore]")
{
    // One clock, two traces that do not share a length. The long one runs
    // 0..10. The short one is only 5..6, which is the middle of that clock.
    constexpr int nLong = 10'001;
    std::vector<double> yLong(static_cast<std::size_t>(nLong), 1.0);
    std::vector<double> tLong(static_cast<std::size_t>(nLong));
    for (int i = 0; i < nLong; ++i) {
        tLong[static_cast<std::size_t>(i)] =
            10.0 * static_cast<double>(i) / static_cast<double>(nLong - 1);
    }
    const std::vector<double> yShort(5, 2.0);
    const std::vector<double> tShort = {5.0, 5.25, 5.5, 5.75, 6.0};
    std::vector<double> yShared(11, 3.0);
    std::vector<double> tShared(11);
    for (int i = 0; i < 11; ++i) {
        tShared[static_cast<std::size_t>(i)] = static_cast<double>(i);
    }

    gui::LineStore store;
    REQUIRE(store.addLine(yLong.data(), static_cast<qsizetype>(yLong.size())) == 0);
    REQUIRE(store.addLine(yShort.data(), static_cast<qsizetype>(yShort.size())) == 1);
    REQUIRE(store.addLine(yShared.data(), static_cast<qsizetype>(yShared.size())) == 2);
    store.setAxis(tShared.data(), static_cast<qsizetype>(tShared.size()));
    REQUIRE(store.setLineAxis(0, tLong.data(), static_cast<qsizetype>(tLong.size())));
    REQUIRE(store.setLineAxis(1, tShort.data(), static_cast<qsizetype>(tShort.size())));
    // A time that is not this line's length is not a time for it.
    REQUIRE_FALSE(store.setLineAxis(2, tShort.data(), static_cast<qsizetype>(tShort.size())));
    REQUIRE(store.xMin() == Approx(0.0));
    REQUIRE(store.xMax() == Approx(10.0));
    store.setPaneColumns(256);

    const auto picture = [&](double t0, double t1) {
        store.setVisibleRange(t0, t1);
        std::vector<gui::PlotLine> lines;
        gui::PlotAxis axis;
        store.fillInto(lines, axis);
        return std::make_pair(std::move(lines), axis);
    };

    {
        const auto drawn = picture(0.0, 10.0);
        const auto& lines = drawn.first;
        const auto& axis = drawn.second;
        double low = 0.0;
        double high = 0.0;
        spanOf(lines[1], axis, low, high);
        REQUIRE(low == Approx(5.0));
        REQUIRE(high == Approx(6.0));
        REQUIRE((low + high) / 2.0 == Approx(5.5));
        spanOf(lines[0], axis, low, high);
        REQUIRE(low == Approx(0.0).margin(0.05));
        REQUIRE(high == Approx(10.0).margin(0.05));
        // No time of its own: the shared axis places it, sample for sample.
        REQUIRE(lines[2].xs == nullptr);
        REQUIRE(gui::xOf(lines[2], axis, 0) == Approx(0.0));
        REQUIRE(gui::xOf(lines[2], axis, lines[2].count - 1) == Approx(10.0));
    }
    {
        const auto drawn = picture(5.0, 6.0);
        const auto& lines = drawn.first;
        const auto& axis = drawn.second;
        double low = 0.0;
        double high = 0.0;
        spanOf(lines[1], axis, low, high);
        REQUIRE(low == Approx(5.0).margin(0.05));
        REQUIRE(high == Approx(6.0).margin(0.05));
        spanOf(lines[0], axis, low, high);
        // The closer run is wider than the window, and it is not the whole line.
        REQUIRE(high - low < 6.0);
        REQUIRE(low > 1.0);
        REQUIRE(low < 5.2);
        REQUIRE(high > 5.8);
    }
}

TEST_CASE("stacking puts each line in an equal band", "[plotcore]")
{
    gui::PlotCamera camera;
    camera.setDataExtent(0.0, 10.0, -8.0, 40.0, 0.0, 1.0, 100);
    camera.reset();
    camera.zoomAt(0.4, 0.5, 3.0, true, false);
    const double x0 = camera.viewMinX();
    const double x1 = camera.viewMaxX();
    camera.zoomAt(0.5, 0.4, 4.0, false, true);
    camera.resetY();
    const gui::PlotCamera::Span full = camera.paddedY();
    REQUIRE(camera.viewMinY() == Approx(full.low));
    REQUIRE(camera.viewMaxY() == Approx(full.high));
    REQUIRE(camera.viewMinX() == Approx(x0));
    REQUIRE(camera.viewMaxX() == Approx(x1));

    // Three spans, none of them the common axis. The first band is the top
    // third. The edges are the padded span, the same air the common axis uses.
    const double lows[] = {0.0, 10.0, -4.0};
    const double highs[] = {1.0, 30.0, -1.0};
    for (int i = 0; i < 3; ++i) {
        double scale = 1.0;
        double shift = 0.0;
        const double f0 = (3.0 - 1.0 - i) / 3.0;
        const double f1 = (3.0 - i) / 3.0;
        REQUIRE(camera.placeLine(scale, shift, lows[i], highs[i], f0, f1));
        const gui::PlotCamera::Span window = camera.lineSpan(scale, shift);
        const double air = (highs[i] - lows[i]) * 0.05;
        REQUIRE(fractionOf(lows[i] - air, window.low, window.high) == Approx(f0));
        REQUIRE(fractionOf(highs[i] + air, window.low, window.high) == Approx(f1));
    }
}

TEST_CASE("a line keeps its band when its own scale becomes logarithmic", "[plotcore]")
{
    gui::PlotCamera camera;
    camera.setDataExtent(0.0, 10.0, -8.0, 40.0, 0.0, 1.0, 100);
    camera.reset();

    double keptScale = 1.0;
    double keptShift = 0.0;
    REQUIRE(camera.placeLine(keptScale, keptShift, -4.0, -1.0, 0.0, 0.5));
    const gui::PlotCamera::Span kept = camera.lineSpan(keptScale, keptShift);

    double scale = 1.0;
    double shift = 0.0;
    REQUIRE(camera.placeLine(scale, shift, 1.0, 100.0, 0.5, 1.0));
    REQUIRE(camera.retargetLine(scale, shift, false, true, 1.0, 100.0, 1.0, true));
    const gui::PlotCamera::Span logged = camera.lineSpan(scale, shift, true, 1.0, 100.0);
    REQUIRE(logged.low > 0.0);
    REQUIRE(logged.high > logged.low);

    // Five percent of the decades, the air padded() gives a logarithmic axis.
    // The padded edges still sit on the top half of the pane.
    const double from = std::log(1.0) / std::log(10.0);
    const double to = std::log(100.0) / std::log(10.0);
    const double air = (to - from) * 0.05;
    const double padLow = std::pow(10.0, from - air);
    const double padHigh = std::pow(10.0, to + air);
    const auto logFrac = [](double value, double low, double high) {
        return (std::log(value) - std::log(low)) / (std::log(high) - std::log(low));
    };
    REQUIRE(logFrac(padLow, logged.low, logged.high) == Approx(0.5));
    REQUIRE(logFrac(padHigh, logged.low, logged.high) == Approx(1.0));

    // The other line was not the one that changed scale.
    const gui::PlotCamera::Span still = camera.lineSpan(keptScale, keptShift);
    REQUIRE(still.low == Approx(kept.low));
    REQUIRE(still.high == Approx(kept.high));

    // A value at or below zero has no place. The positive part of the span
    // is what the band is drawn from.
    double crossedScale = 1.0;
    double crossedShift = 0.0;
    REQUIRE(camera.placeLine(crossedScale, crossedShift, -2.0, 8.0, 0.0, 0.5));
    REQUIRE(camera.retargetLine(crossedScale, crossedShift, false, true, -2.0, 8.0, 0.25, true));
    const gui::PlotCamera::Span crossed =
        camera.lineSpan(crossedScale, crossedShift, true, 0.25, 8.0);
    REQUIRE(crossed.low > 0.0);
    REQUIRE(crossed.high > crossed.low);

    // The common checkbox resets the camera. This line's window is put back
    // in the data's own units, so the band does not jump with it.
    const double keptLow = logged.low;
    const double keptHigh = logged.high;
    camera.setYLog(true);
    REQUIRE(camera.fitLine(scale, shift, true, 1.0, 100.0, keptLow, keptHigh));
    const gui::PlotCamera::Span after = camera.lineSpan(scale, shift, true, 1.0, 100.0);
    REQUIRE(after.low == Approx(keptLow));
    REQUIRE(after.high == Approx(keptHigh));
}
