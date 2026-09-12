// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "common/SpikeRunner.hpp"

#include "common/Allocations.hpp"
#include "common/FrameTimer.hpp"

#include <QtCore/QElapsedTimer>
#include <QtCore/QEventLoop>
#include <QtCore/QTimer>
#include <QtQuick/QQuickWindow>

#include <cstdio>
#include <memory>

namespace spike {
namespace {

// Longer than any frame this benchmark is interested in, and short enough that
// a renderer which has stopped drawing does not stop the run. A cell that hits
// this is reported with the frames it managed, which is the honest answer.
constexpr int kFrameTimeoutMs = 4000;

} // namespace

bool drawOneFrame(Surface& surface, FrameTimer& timer)
{
    QQuickWindow* window = surface.window();
    if (window == nullptr) {
        return false;
    }
    const int before = timer.frames();

    QEventLoop loop;
    // Queued, unlike the timer's own connections: this one runs on the GUI
    // thread and must not quit an event loop from inside the render pass.
    const auto framed = QObject::connect(&timer, &FrameTimer::framed, &loop,
                                         &QEventLoop::quit, Qt::QueuedConnection);
    QTimer guard;
    guard.setSingleShot(true);
    QObject::connect(&guard, &QTimer::timeout, &loop, &QEventLoop::quit);
    guard.start(kFrameTimeoutMs);

    window->update();
    loop.exec();
    QObject::disconnect(framed);
    return timer.frames() > before;
}

QImage grab(Surface& surface, FrameTimer& timer, int settleFrames)
{
    QQuickWindow* window = surface.window();
    if (window == nullptr) {
        return {};
    }
    // Several frames before the grab, not one. Qt Graphs builds its series from
    // what the pass before it delivered -- tools/make-screenshots.cpp says so
    // in as many words -- so a plot's first frame after a change of data can be
    // the axes without the line, and a correctness suite that grabbed it would
    // report every renderer as drawing nothing.
    for (int i = 0; i < settleFrames; ++i) {
        drawOneFrame(surface, timer);
    }
    return window->grabWindow();
}

BenchReport runBench(Surface& surface, const Options& options)
{
    BenchReport report;
    QQuickWindow* window = surface.window();
    if (window == nullptr) {
        std::fprintf(stderr, "%s: no window to draw in\n", surface.rendererName());
        return report;
    }

    FrameTimer timer(window);

    if (!allocationCountingActive()) {
        std::fprintf(stderr,
                     "%s: the global operator new replacement is not in this "
                     "binary; the alloc columns would read zero and are "
                     "reported as unmeasured instead\n",
                     surface.rendererName());
    }

    std::vector<Cell> cells;
    if (options.cellSeries > 0 && options.cellPoints > 0) {
        cells.push_back({options.cellSeries, options.cellPoints, true, false});
    } else {
        cells = grid(options.budget);
    }

    for (const Cell& cell : cells) {
        Row row;
        row.renderer = surface.rendererName();
        row.shape = name(options.shape);
        row.series = cell.series;
        row.points = cell.points;
        row.realWorkload = cell.realWorkload;

        if (cell.skipped) {
            row.skipped = true;
            row.note = "past --budget";
            report.add(row);
            continue;
        }

        // Built before the clock starts. Generating the data is the read path's
        // job in the application and nobody's job here; timing it would add the
        // same number to all three columns and hide the ones that differ.
        const auto source = std::make_unique<SyntheticSource>(
            options.shape, cell.series, cell.points, options.seed);
        row.dataMiB = static_cast<double>(source->bytes()) / (1024.0 * 1024.0);

        QElapsedTimer clock;
        clock.start();
        surface.setSource(source.get());
        row.buildMs = static_cast<double>(clock.nsecsElapsed()) / 1e6;

        // Build to first complete frame. A renderer that defers its geometry
        // moves cost from the column above into this one, and a reader waiting
        // for a plot to appear pays the sum of the two.
        timer.reset();
        clock.restart();
        const bool drew = drawOneFrame(surface, timer);
        row.firstFrameMs = static_cast<double>(clock.nsecsElapsed()) / 1e6;
        if (!drew) {
            row.note = "no frame within 4s";
        }

        ViewWindow full;
        full.xMin = source->xStart();
        full.xMax = source->xStart()
                    + source->xStep() * static_cast<double>(source->pointCount() - 1);
        if (source->hasExplicitX()) {
            full.xMin = -1.0;
            full.xMax = 1.0;
        }
        full.yMin = source->minimum();
        full.yMax = source->maximum();
        ScriptedGesture gesture(full, options.frames);

        // Warm-up, discarded. The first frames after a new dataset measure the
        // shader cache, the first vertex upload and whatever the library does
        // once -- all real costs, and all already counted in `first ms`.
        surface.setViewWindow(full);
        for (int i = 0; i < options.warmup; ++i) {
            drawOneFrame(surface, timer);
        }

        timer.reset();
        const AllocationCount allocationsBefore = allocations();
        for (int frame = 0; frame < options.frames; ++frame) {
            surface.setViewWindow(gesture.at(frame));
            if (!drawOneFrame(surface, timer)) {
                row.note = "stopped drawing mid-gesture";
                break;
            }
        }
        const AllocationCount spent = allocations().since(allocationsBefore);

        row.frames = timer.stats();
        row.allocCalls = spent.calls;
        row.allocMiB = spent.bytes / (1024 * 1024);
        row.rssMiB = peakResidentMiB();
        report.add(row);

        std::fprintf(stderr, "  %-12s %6d x %-9d  cpu p50 %7.2f ms  p95 %7.2f ms\n",
                     row.renderer.c_str(), row.series, row.points,
                     row.frames.cpuMedian, row.frames.cpuP95);
    }

    return report;
}

} // namespace spike
