// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

// What a zoom costs, frame by frame.
//
// bench-data measures a view being opened; this measures one being *moved*, and
// it is a separate tool because the two have separate causes. Opening pays for
// a pass over the file. Zooming used to pay for another one at every notch --
// one run, one crossing, a settle's wait -- and that is the number a reader
// feels as the plot being jumpy rather than as the plot being slow.
//
// The gesture it drives is one pinch on a trackpad: from the whole line down to
// one sample per pane column, at one exact position, in twenty frames. Twenty
// frames in a tenth of a second is two hundred a second, and every one of them
// has to show the right data at its own level of detail -- so the tool reports
// the per-frame times *and* whether the picture was right, because a cache that
// is fast and wrong counts exactly like one that works.
//
// It asserts nothing. tests/test_cost.cpp holds the counts, in numbers that
// mean the same thing on every machine; this prints the milliseconds, which do
// not. Run it when someone says the plot feels slow.

#include "BenchReport.hpp"

#include "gui/AppController.hpp"
#include "gui/CustomPlot.hpp"
#include "gui/CustomPlotSet.hpp"
#include "gui/DatasetPlot.hpp"
#include "gui/DatasetTableModel.hpp"
#include "gui/H5Thread.hpp"
#include "gui/PlotItem.hpp"
#include "gui/PlotProjection.hpp"

#include <QCoreApplication>
#include <QString>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>
#include <vector>

namespace {

void settle()
{
    gui::H5Thread::instance().drain();
    QCoreApplication::processEvents();
    gui::H5Thread::instance().drain();
}

/// What one gesture came to.
struct Frames
{
    std::vector<double> milliseconds;
    /// Frames whose drawn line still held the value at the focus.
    int kept = 0;
    /// Frames drawn at one station per pane column or better.
    int resolved = 0;
};

[[nodiscard]] double quantile(std::vector<double> sorted, double fraction)
{
    if (sorted.empty()) {
        return 0.0;
    }
    std::sort(sorted.begin(), sorted.end());
    const auto at = static_cast<std::size_t>(fraction * static_cast<double>(sorted.size() - 1));
    return sorted[std::min(at, sorted.size() - 1)];
}

/// One line's drawn extremes, and whether `wanted` is among its values.
void inspect(const gui::PlotLine& line, double wanted, bool& held)
{
    held = false;
    for (qsizetype i = 0; i < line.count; ++i) {
        if (line.values[i] == wanted) {
            held = true;
            return;
        }
    }
}

/// The per-frame budget a refresh rate implies.
constexpr double kFrameBudget = 5.0; ///< milliseconds, which is 200 Hz

void report(const char* what, const Frames& frames, long long reads, long long crossings,
            int expected)
{
    if (frames.milliseconds.empty()) {
        std::printf("%-8s  (nothing drawn)\n", what);
        return;
    }
    double total = 0.0;
    int over = 0;
    for (const double ms : frames.milliseconds) {
        total += ms;
        if (ms > kFrameBudget) {
            ++over;
        }
    }
    const auto count = static_cast<int>(frames.milliseconds.size());
    std::printf("%-8s %8.1f %7.3f %7.3f %7.3f %7.3f %7lld %7lld %5d/%-3d %5d/%-3d  %s\n", what,
                total, quantile(frames.milliseconds, 0.0), quantile(frames.milliseconds, 0.5),
                quantile(frames.milliseconds, 0.99), quantile(frames.milliseconds, 1.0), reads,
                crossings, frames.kept, count, frames.resolved, count,
                (over == 0 && count == expected) ? "PASS" : "over budget");
}

void usage(const char* program)
{
    std::printf("usage: %s FILE [--dataset PATH] [--at INDEX] [--frames N]\n"
                "            [--columns N] [--warm]\n\n"
                "  --dataset PATH  the line to zoom into (default /plotting/adc_10M)\n"
                "  --at INDEX      the element the pointer is held on\n"
                "                  (default: the most extreme sample in the middle half)\n"
                "  --frames N      frames the gesture is drawn in (default 20)\n"
                "  --columns N     device pixel columns the pane has (default 2048)\n"
                "  --warm          leave the page cache alone (default: evict first)\n",
                program);
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);

    std::string path;
    std::string dataset = "/plotting/adc_10M";
    long long at = -1;
    int frames = 20;
    int columns = 2048;
    bool warm = false;

    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        const auto next = [&](long long fallback) {
            return (i + 1 < argc) ? std::atoll(argv[++i]) : fallback;
        };
        if (argument == "--warm") {
            warm = true;
        }
        else if (argument == "--dataset" && i + 1 < argc) {
            dataset = argv[++i];
        }
        else if (argument == "--at") {
            at = next(at);
        }
        else if (argument == "--frames") {
            frames = static_cast<int>(next(frames));
        }
        else if (argument == "--columns") {
            columns = static_cast<int>(next(columns));
        }
        else if (argument.rfind("--", 0) == 0) {
            usage(argv[0]);
            return 2;
        }
        else if (path.empty()) {
            path = argument;
        }
        else {
            usage(argv[0]);
            return 2;
        }
    }
    if (path.empty()) {
        usage(argv[0]);
        return 2;
    }
    if (!warm && !bench::evict(path)) {
        std::printf("note: could not evict the page cache; times are warm\n");
    }

    try {
        std::vector<bench::Row> rows;
        gui::AppController controller;
        gui::DatasetPlot* plot = controller.datasetPlot();

        bench::phase(
            rows, "open",
            [&] {
                controller.openFile(QString::fromStdString(path));
                settle();
                controller.selectPath(QString::fromStdString(dataset));
                settle();
            },
            0, "");

        // The pane, before anything is drawn: a bucket is a column, so the
        // width decides the resolution every frame below is measured against.
        plot->setPaneColumns(columns);

        long long length = 0;
        bench::phase(
            rows, "whole",
            [&] {
                bench::blocking([&] { (void)plot->pointCount(); });
                settle();
                length = plot->sourcePointCount();
            },
            0, "");
        rows.back().units = length;
        rows.back().unitName = "elements held";

        if (length <= 0 || plot->seriesCount() <= 0) {
            std::printf("%s has no line to draw\n", dataset.c_str());
            gui::H5Thread::shutdown();
            return 1;
        }
        double picked = 0.0;
        bool chosen = false;
        if (at < 0 || at >= length) {
            chosen = true;
            // Somewhere worth zooming into, rather than the middle.
            //
            // A benchmark that puts the pointer on an arbitrary element cannot
            // say anything about whether the picture stayed right: the value
            // there is an unremarkable one that half the buckets in the line
            // contain by accident. The most extreme sample in the middle half
            // is a one-sample impulse on any trace that has one -- which is the
            // thing an envelope exists to keep and a stride loses -- so
            // "the frame still holds it" becomes a real question.
            //
            // Found out of the whole-line summary, which is already in hand:
            // its buckets carry the extremes, so the extreme bucket is the one
            // the extreme sample is in, and one zoomed-in look inside it finds
            // the sample. No extra read either way.
            long long bestAt = length / 2;
            double reach = static_cast<double>(length);

            // Down the pyramid: the extreme bucket of a summary is the bucket
            // the extreme sample is in, so narrowing onto it and asking again
            // is one step closer every time. It ends when the run comes back
            // sample for sample, which is the first answer that names an
            // element rather than a bucket -- and it is the only answer worth
            // having, because the value at a bucket boundary is an ordinary one.
            for (int step = 0; step < 40; ++step) {
                const gui::PlotLine line = plot->lineOf(0);
                if (line.values == nullptr || line.count <= 0) {
                    break;
                }
                double found = 0.0;
                long long foundAt = bestAt;
                double foundValue = 0.0;
                for (qsizetype i = 0; i < line.count; ++i) {
                    const double where =
                        line.positionStart + static_cast<double>(i) * line.positionStep;
                    // The middle half only, and only on the first pass: an
                    // impulse at the very edge of the record is one the zoom
                    // would run off the end of.
                    if (step == 0 && (where < static_cast<double>(length) / 4.0 ||
                                      where > 3.0 * static_cast<double>(length) / 4.0)) {
                        continue;
                    }
                    if (std::abs(line.values[i]) > found) {
                        found = std::abs(line.values[i]);
                        foundAt = std::llround(where);
                        foundValue = line.values[i];
                    }
                }
                bestAt = foundAt;
                // The value is taken from the line that found it, not read back
                // from the element afterwards. On a line too large to hold raw
                // there *is* no sample-for-sample reading without a read, so the
                // position names a bucket rather than an element -- and asking
                // what is at that position would answer with an ordinary value
                // next to the extreme rather than the extreme itself, which is
                // the thing the frames below are being checked for holding.
                picked = foundValue;
                if (line.positionStep <= 1.0) {
                    break; // drawn sample for sample: this is the element itself
                }
                reach = std::max(line.positionStep * 4.0, static_cast<double>(columns));
                plot->setVisibleRange(static_cast<double>(bestAt) - reach,
                                      static_cast<double>(bestAt) + reach);
            }
            at = std::clamp<long long>(bestAt, 0, length - 1);
            plot->clearZoomFocus();
            plot->setVisibleRange(0.0, static_cast<double>(length));
            settle();
        }

        // The value the pointer is held on, which is what every frame below is
        // checked for still holding.
        double focusValue = picked;
        if (!chosen) {
            // Named by hand: find what is there, as finely as the line can be
            // read without going to the file for it.
            plot->setVisibleRange(static_cast<double>(at) - static_cast<double>(columns),
                                  static_cast<double>(at) + static_cast<double>(columns));
            const gui::PlotLine one = plot->lineOf(0);
            double nearest = -1.0;
            for (qsizetype i = 0; i < one.count; ++i) {
                const double where = one.positionStart + static_cast<double>(i) * one.positionStep;
                const double away = std::abs(where - static_cast<double>(at));
                if (nearest < 0.0 || away < nearest) {
                    nearest = away;
                    focusValue = one.values[i];
                }
            }
        }
        plot->clearZoomFocus();
        plot->setVisibleRange(0.0, static_cast<double>(length));
        settle();

        // --- the gesture ----------------------------------------------------
        //
        // The pointer holds still and both edges come in towards it, which is
        // what PlotSurface.zoomedAxis does and what setZoomFocus says is
        // happening. From the whole line to one sample a column, in `frames`.
        const double shrink = std::pow(static_cast<double>(columns) / static_cast<double>(length),
                                       1.0 / static_cast<double>(frames));

        const auto spin = [&](auto&& push) {
            Frames measured;
            double low = 0.0;
            double high = static_cast<double>(length);
            gui::PlotItem item;
            for (int frame = 0; frame < frames; ++frame) {
                const auto start = std::chrono::steady_clock::now();
                push(low, high, frame);
                const gui::PlotLine line = plot->lineOf(0);
                // What the renderer does with it, so that a frame here is a
                // frame rather than a model call: the projection walks every
                // drawn value on every frame and is the other half of the cost.
                const std::vector<QPointF> points = gui::samplesOf(line, plot->drawingAxis());
                const auto end = std::chrono::steady_clock::now();
                measured.milliseconds.push_back(
                    std::chrono::duration<double, std::milli>(end - start).count());

                bool held = false;
                inspect(line, focusValue, held);
                measured.kept += held ? 1 : 0;
                if (line.positionStep > 0.0 &&
                    (high - low) / line.positionStep > static_cast<double>(columns) - 1.0) {
                    ++measured.resolved;
                }
                (void)points;
            }
            return measured;
        };

        Frames going;
        double endLow = 0.0;
        double endHigh = 0.0;
        const long long readsBefore = bench::readSyscalls();
        const long long crossingsBefore = gui::H5Thread::instance().crossings();
        {
            double low = 0.0;
            double high = static_cast<double>(length);
            going = spin([&](double& outLow, double& outHigh, int) {
                plot->setZoomFocus(static_cast<double>(at), 1.0 / shrink);
                low = static_cast<double>(at) - (static_cast<double>(at) - low) * shrink;
                high = static_cast<double>(at) + (high - static_cast<double>(at)) * shrink;
                plot->setVisibleRange(low, high);
                outLow = low;
                outHigh = high;
            });
            endLow = low;
            endHigh = high;
        }
        const long long inReads = bench::readSyscalls() - readsBefore;
        const long long inCrossings = gui::H5Thread::instance().crossings() - crossingsBefore;

        // ...and back out along the same path, which is the direction that used
        // to cost most: the runs the reader came in through had been evicted by
        // the ones read ahead of them.
        const long long backReadsBefore = bench::readSyscalls();
        const long long backCrossingsBefore = gui::H5Thread::instance().crossings();
        Frames back;
        {
            double low = endLow;
            double high = endHigh;
            back = spin([&](double& outLow, double& outHigh, int) {
                plot->setZoomFocus(static_cast<double>(at), shrink);
                low = static_cast<double>(at) - (static_cast<double>(at) - low) / shrink;
                high = static_cast<double>(at) + (high - static_cast<double>(at)) / shrink;
                plot->setVisibleRange(low, high);
                outLow = low;
                outHigh = high;
            });
        }
        const long long backReads = bench::readSyscalls() - backReadsBefore;
        const long long backCrossings = gui::H5Thread::instance().crossings() - backCrossingsBefore;

        // --- the same gesture in a custom tab -------------------------------
        //
        // A reader who puts a dataset on the Plot tab and the same slice in a
        // custom tab is looking at one dataset, and the two must not differ in
        // anything they can see -- which used to include how long it took.
        Frames custom;
        long long customReads = 0;
        long long customCrossings = 0;
        {
            gui::CustomPlotSet* set = controller.customPlots();
            const int index = set->addPlot();
            settle();
            gui::CustomPlot* tab = set->plotAt(index);
            if (tab != nullptr &&
                tab->addExpression(QString::fromStdString(dataset) + QStringLiteral("[:]")) >= 0) {
                settle();
                tab->setPaneColumns(columns);
                settle();

                const long long before = bench::readSyscalls();
                const long long crossings = gui::H5Thread::instance().crossings();
                double low = 0.0;
                double high = static_cast<double>(length);
                gui::PlotItem item;
                for (int frame = 0; frame < frames; ++frame) {
                    const auto start = std::chrono::steady_clock::now();
                    tab->setZoomFocus(static_cast<double>(at), 1.0 / shrink);
                    low = static_cast<double>(at) - (static_cast<double>(at) - low) * shrink;
                    high = static_cast<double>(at) + (high - static_cast<double>(at)) * shrink;
                    tab->setVisibleRange(low, high);
                    const gui::PlotLine line = tab->lineOf(0);
                    const std::vector<QPointF> points = gui::samplesOf(line, tab->drawingAxis());
                    const auto end = std::chrono::steady_clock::now();
                    custom.milliseconds.push_back(
                        std::chrono::duration<double, std::milli>(end - start).count());
                    bool held = false;
                    inspect(line, focusValue, held);
                    custom.kept += held ? 1 : 0;
                    if (line.positionStep > 0.0 &&
                        (high - low) / line.positionStep > static_cast<double>(columns) - 1.0) {
                        ++custom.resolved;
                    }
                    (void)points;
                }
                customReads = bench::readSyscalls() - before;
                customCrossings = gui::H5Thread::instance().crossings() - crossings;
            }
        }

        bench::report(rows);

        std::printf("\n%lld elements, %d frames onto element %lld, %d columns\n"
                    "the value there is %.6g; `kept` counts frames whose drawn line still "
                    "holds it\n"
                    "`res` counts frames drawn at one station per column or better\n"
                    "a frame is %.1f ms at %d Hz\n\n",
                    length, frames, at, columns, focusValue, kFrameBudget,
                    static_cast<int>(1000.0 / kFrameBudget));
        std::printf("%-8s %8s %7s %7s %7s %7s %7s %7s %9s %9s  %s\n", "gesture", "ms", "min", "p50",
                    "p99", "max", "reads", "cross", "kept", "res", "verdict");
        report("in", going, inReads, inCrossings, frames);
        report("out", back, backReads, backCrossings, frames);
        report("custom", custom, customReads, customCrossings, frames);

        gui::H5Thread::shutdown();
        return 0;
    }
    catch (const std::exception& error) {
        std::printf("failed: %s\n", error.what());
        gui::H5Thread::shutdown();
        return 1;
    }
}
