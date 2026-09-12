// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "common/SpikeMain.hpp"

#include "common/Allocations.hpp"
#include "common/FrameTimer.hpp"
#include "common/PlotBench.hpp"
#include "common/Verify.hpp"

#include <QtGui/QGuiApplication>
#include <QtWidgets/QApplication>
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlContext>
#include <QtQuick/QQuickWindow>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <vector>

namespace spike {

int runSpike(int argc, char** argv, const QUrl& qmlEntry,
             const std::function<QmlSurface*(QQmlApplicationEngine&)>& create,
             bool needsWidgets)
{
    // Parsed before QGuiApplication exists, because --offscreen has to set the
    // platform plugin and that is read during construction.
    QStringList arguments;
    arguments.reserve(argc);
    for (int i = 0; i < argc; ++i) {
        arguments.append(QString::fromLocal8Bit(argv[i]));
    }
    const Options options = parseOptions(arguments);
    if (options.help) {
        printUsage(argv[0]);
        return 0;
    }
    if (!options.unknownFlag.isEmpty()) {
        std::fprintf(stderr, "unrecognised argument: %s\n",
                     qPrintable(options.unknownFlag));
        printUsage(argv[0]);
        return 2;
    }

    std::string why;
    if (!prepareRenderEnvironment(options, why)) {
        std::fprintf(stderr, "%s\n", why.c_str());
        return 3;
    }

    // Held through a pointer so the choice between the two can be made at run
    // time. QApplication is a QGuiApplication, so everything after this is the
    // same code either way -- the difference is what got initialised, and for
    // the widgets case that is a second event dispatcher, the widget style
    // machinery and the QtWidgets library on the link line.
    std::unique_ptr<QGuiApplication> application;
    if (needsWidgets) {
        application = std::make_unique<QApplication>(argc, argv);
    } else {
        application = std::make_unique<QGuiApplication>(argc, argv);
    }
    // Distance-field text, not the platform's native rasteriser.
    //
    // Native text on an RGB display is subpixel-antialiased, which means every
    // glyph edge is *coloured* -- and the correctness checks tell a curve from
    // the chrome around it by exactly that, because the curves are saturated
    // hues and the grid and labels are grey. Measured before this line existed:
    // the caption at the top of the window put ink of chroma 90 into row 9, and
    // all three renderers were reported as having lost a spike that all three
    // had drawn. Distance-field glyphs are greyscale-antialiased, so the only
    // coloured thing on the surface is the data.
    //
    // It also makes the three comparable: a spike whose labels cost a different
    // rasteriser from another's is a spike being timed on its text.
    QQuickWindow::setTextRenderType(QQuickWindow::QtTextRendering);

    QQmlApplicationEngine engine;

    QmlSurface* surface = create(engine);
    if (surface == nullptr) {
        std::fprintf(stderr, "the spike built no surface\n");
        return 4;
    }
    surface->setParent(&engine);
    surface->setVariant(options.variant);

    // The interactive defaults are set before the scene loads, so the window
    // that appears already has something in it rather than filling in a frame
    // later. A spike that opened empty would be judged on its startup.
    const auto initial = std::make_unique<SyntheticSource>(
        options.shape, options.series, options.points, options.seed);

    engine.load(qmlEntry);
    if (engine.rootObjects().isEmpty()) {
        std::fprintf(stderr, "the QML scene did not load\n");
        return 5;
    }
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
    if (window == nullptr) {
        std::fprintf(stderr, "the QML scene's root is not a window\n");
        return 6;
    }
    surface->attach(window);
    window->resize(kWindowWidth, kWindowHeight);
    window->show();

    surface->setSource(initial.get());

    if (options.verify) {
        const std::vector<Finding> findings = verify(*surface, options);
        report(findings, options.out);
        // The exit status is the verdict, so ctest does not need a wrapper to
        // tell a failed check from a crashed spike.
        const bool failed =
            std::any_of(findings.begin(), findings.end(),
                        [](const Finding& f) { return f.verdict == "fail"; });
        return failed ? 1 : 0;
    }

    if (!options.bench) {
        // Interactive. The half of this comparison a table cannot hold: whether
        // the thing feels like an instrument under the hand.
        return QGuiApplication::exec();
    }

    if (!why.empty()) {
        std::fprintf(stderr, "warning: %s\n", why.c_str());
    }

    // One frame before the grid starts, so the first cell is not the one that
    // pays for creating the swapchain.
    {
        FrameTimer settle(window);
        drawOneFrame(*surface, settle);
    }

    BenchReport report = runBench(*surface, options);
    report.print(std::string(surface->rendererName()) + " -- "
                 + name(options.shape) + ", " + std::to_string(options.frames)
                 + " frames per cell"
                 + (options.offscreen ? " (SOFTWARE RENDERER)" : ""));
    if (!options.out.isEmpty()) {
        report.appendTsv(options.out + QStringLiteral("/results.tsv"));
    }
    if (!allocationCountingActive()) {
        std::fprintf(stderr, "note: allocation counts were not measured\n");
    }
    return 0;
}

} // namespace spike
