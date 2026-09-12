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

    // The dataset the surface is currently pointing at. Outside the loop so
    // that nothing frees it while the surface still has it.
    std::unique_ptr<SyntheticSource> held;

    std::vector<Cell> cells;
    if (options.cellSeries > 0 && options.cellPoints > 0) {
        // Marked as the real workload only if it *is* one. --cell is how the
        // grid gets run one process per cell -- which Qt Graphs requires,
        // because it cannot finish the grid in one -- and a flag that labelled
        // every cell "the real one" would put the marker on all of them.
        const bool real = options.cellPoints == 2048
                          && (options.cellSeries == 64 || options.cellSeries == 10000);
        cells.push_back({options.cellSeries, options.cellPoints, real, false});
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
            BenchReport::appendRow(options.out.isEmpty()
                                       ? QString()
                                       : options.out + QStringLiteral("/results.tsv"),
                                   row);
            continue;
        }

        // Throwing the previous cell's data away, timed on its own. Charging
        // it to the next cell's build would put the cost of discarding ten
        // thousand lines in the row of the sixty-four that replaced them --
        // which is where it first appeared, at 496 seconds, and looked like an
        // impossible build rather than a quadratic teardown.
        QElapsedTimer clock;
        clock.start();
        surface.setSource(nullptr);
        row.clearMs = static_cast<double>(clock.nsecsElapsed()) / 1e6;

        // Only now is the previous cell's data freed, and only because the
        // surface has just been told to stop pointing at it.
        //
        // It was the other way round for one run, and three of the five
        // renderers crashed on their largest cell -- two with std::bad_alloc,
        // one with a segmentation fault, all of them looking exactly like a
        // library that had run out of room. They had not. `held` was released
        // at the end of the previous iteration while the surface still held
        // the pointer, and the next call into it read a freed dataset for its
        // series and point counts. A use-after-free that reports itself as a
        // failed allocation is the most expensive kind of benchmark bug there
        // is: the number it produces is plausible, and it is about the wrong
        // program.
        held.reset();

        // Built after the clock stops. Generating the data is the read path's
        // job in the application and nobody's job here; timing it would add
        // the same number to all three columns and hide the ones that differ.
        held = std::make_unique<SyntheticSource>(options.shape, cell.series,
                                                 cell.points, options.seed);
        const SyntheticSource* source = held.get();
        row.dataMiB = static_cast<double>(source->bytes()) / (1024.0 * 1024.0);

        clock.restart();
        surface.setSource(source);
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
        // On disk before the next cell starts, so a cell that takes the
        // process down takes only itself.
        BenchReport::appendRow(options.out.isEmpty()
                                   ? QString()
                                   : options.out + QStringLiteral("/results.tsv"),
                               row);

        std::fprintf(stderr, "  %-12s %6d x %-9d  cpu p50 %7.2f ms  p95 %7.2f ms\n",
                     row.renderer.c_str(), row.series, row.points,
                     row.frames.cpuMedian, row.frames.cpuP95);
    }

    // And the surface stops pointing at anything before the caller's stack
    // unwinds past `held`.
    surface.setSource(nullptr);
    return report;
}

} // namespace spike
