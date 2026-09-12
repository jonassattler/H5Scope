// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "common/Verify.hpp"

#include "common/FrameTimer.hpp"
#include "common/ImageChecks.hpp"
#include "common/SpikeMain.hpp"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QTextStream>
#include <QtQuick/QQuickWindow>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>

namespace spike {
namespace {

/// A file name that says which renderer drew what, and survives a variant name
/// with a slash in it.
QString imageName(const QString& out, const std::string& renderer,
                  const char* check)
{
    QString safe = QString::fromStdString(renderer);
    safe.replace(QLatin1Char('/'), QLatin1Char('-'));
    return out + QStringLiteral("/%1-%2.png").arg(safe, QLatin1String(check));
}

/// The whole extent of a source, which is the view every check starts from.
ViewWindow whole(const SyntheticSource& source)
{
    ViewWindow view;
    view.xMin = source.xStart();
    view.xMax = source.xStart()
                + source.xStep() * static_cast<double>(source.pointCount() - 1);
    if (source.hasExplicitX()) {
        view.xMin = -1.0;
        view.xMax = 1.0;
    }
    view.yMin = source.minimum();
    view.yMax = source.maximum();
    // A hair of headroom, so a sample exactly at the extreme is drawn inside
    // the pane rather than on the line that bounds it -- where half its stroke
    // falls outside and the column it lands in can read as empty.
    const double margin = (view.yMax - view.yMin) * 0.02;
    view.yMin -= margin;
    view.yMax += margin;
    return view;
}

} // namespace

std::vector<Finding> verify(Surface& surface, const Options& options)
{
    std::vector<Finding> findings;
    QQuickWindow* window = surface.window();
    if (window == nullptr) {
        return findings;
    }
    FrameTimer timer(window);
    const QString out = options.out.isEmpty() ? QStringLiteral(".") : options.out;
    const std::string renderer = surface.rendererName();

    // Every check follows the same four steps, so they are written once: build
    // the data, show the whole of it, grab, read the pixels.
    const auto look = [&](Shape shape, int series, int points, const char* check,
                          QImage& image, InkProfile& ink) {
        auto source = std::make_unique<SyntheticSource>(shape, series, points,
                                                        options.seed);
        surface.setSource(source.get());
        surface.setViewWindow(whole(*source));
        image = grab(surface, timer);
        ink = profile(image, 16, kSeriesChroma);
        writePng(image, imageName(out, renderer, check));
        return source;
    };

    QImage image;
    InkProfile ink;
    // The surface keeps a bare pointer to whatever it was last given and draws
    // from it on every frame, so the data has to outlive the grab *and* the
    // frames that follow it. Held here and replaced rather than let go at the
    // end of each block: assigning destroys the previous one only after the
    // surface has already been pointed at the new one.
    std::unique_ptr<SyntheticSource> held;

    // --- spike -------------------------------------------------------------
    {
        held = look(Shape::SpikeInAMillion, 1, 1000000, "spike", image, ink);
        Finding finding{renderer, "spike", "fail", 0.0, {},
                        imageName(out, renderer, "spike").toStdString()};
        if (ink.width > 0) {
            // The highest ink anywhere, and where it is. Searched rather than
            // sampled at a computed column: two of the three renderers draw
            // their axis labels in a gutter inside the window, so the same
            // data x lands at a different image x in each of them, and a check
            // that assumed otherwise would be measuring the gutter.
            int highest = ink.height;
            int at = 0;
            for (int x = 0; x < ink.width; ++x) {
                const int top = ink.topInkRow[static_cast<std::size_t>(x)];
                if (top >= 0 && top < highest) {
                    highest = top;
                    at = x;
                }
            }
            const double fraction = static_cast<double>(highest)
                                    / static_cast<double>(ink.height);
            const double where = static_cast<double>(held->spikeIndex())
                                 / static_cast<double>(held->pointCount());
            const double found = static_cast<double>(at)
                                 / static_cast<double>(ink.width);
            finding.measure = fraction;
            // Two conditions, because either alone is satisfiable by accident.
            // The spike is ten times the amplitude and the y axis is fitted to
            // it, so a renderer that drew it reaches the top tenth of the pane;
            // one that thinned it away tops out at the sine's own maximum, nine
            // tenths of the way down. And it has to reach it *where the spike
            // is*: within a twelfth of the width, which is more than any gutter
            // and far less than the distance to anywhere else.
            finding.verdict =
                (fraction < 0.12 && std::abs(found - where) < 0.08) ? "pass"
                                                                    : "fail";
            // Generic, and deliberately so: the gallery prints one `detail`
            // per check rather than one per renderer, so a string carrying
            // this renderer's own measurements would be captioning the other
            // two with numbers that are not theirs.
            finding.detail = "topmost ink as a fraction of pane height; under "
                             "0.12 and within a twelfth of the width of where "
                             "the spike is means it survived";
            if (finding.verdict == "fail") {
                std::fprintf(stderr,
                             "  %s: highest ink at x=%.3f, spike is at %.3f\n",
                             renderer.c_str(), found, where);
            }
        }
        findings.push_back(finding);
    }

    // --- gap ---------------------------------------------------------------
    {
        held = look(Shape::NaNRun, 1, 100000, "gap", image, ink);
        Finding finding{renderer, "gap", "fail", 0.0, {},
                        imageName(out, renderer, "gap").toStdString()};
        if (ink.width > 0) {
            const double expected =
                static_cast<double>(held->gapLast() - held->gapFirst())
                / static_cast<double>(held->pointCount()) * ink.width;
            const int run = ink.longestEmptyRun();
            finding.measure = static_cast<double>(run);
            // Two thirds of the expected width, because a stroke of finite
            // width eats into a gap from both sides and the last drawn sample
            // sits inside it.
            finding.verdict = run > expected * 0.66 ? "pass" : "fail";
            finding.detail = "widest run of empty columns, against the "
                             + std::to_string(static_cast<int>(expected))
                             + " the data is missing";
        }
        findings.push_back(finding);
    }

    // --- ends --------------------------------------------------------------
    {
        held = look(Shape::Sine, 1, 10000, "ends", image, ink);
        Finding finding{renderer, "ends", "fail", 0.0, {},
                        imageName(out, renderer, "ends").toStdString()};
        if (ink.width > 0) {
            int leftmost = ink.width;
            int rightmost = -1;
            for (int x = 0; x < ink.width; ++x) {
                if (ink.inkPerColumn[static_cast<std::size_t>(x)] > 0) {
                    leftmost = std::min(leftmost, x);
                    rightmost = std::max(rightmost, x);
                }
            }
            const int missing = leftmost + (ink.width - 1 - rightmost);
            finding.measure = static_cast<double>(missing);
            // A renderer that draws its axis labels inside the pane leaves a
            // margin, so this is generous: what it is looking for is a line
            // that stops well short of the data's own ends, not one inset by a
            // gutter.
            finding.verdict = missing < ink.width / 8 ? "pass" : "fail";
            finding.detail = "columns of pane left blank at the two ends";
        }
        findings.push_back(finding);
    }

    // --- bigx --------------------------------------------------------------
    {
        held = look(Shape::BigX, 1, 100000, "bigx", image, ink);
        Finding finding{renderer, "bigx", "fail", 0.0, {},
                        imageName(out, renderer, "bigx").toStdString()};
        if (ink.width > 0) {
            // A float32 x collapses tens of thousands of samples onto one
            // coordinate, which shows up as long runs of columns whose topmost
            // ink is at exactly the same row -- flat treads -- and as columns
            // with no ink between them.
            const int flat = longestFlatRun(ink, ink.width / 8, ink.width * 7 / 8);
            finding.measure = static_cast<double>(flat);
            finding.verdict = flat < 24 ? "pass" : "fail";
            finding.detail = "longest run of columns sharing one topmost row; "
                             "a sine's own crest is a few, a staircase is tens";
        }
        findings.push_back(finding);
    }

    // --- range -------------------------------------------------------------
    {
        held = look(Shape::WideRange, 1, 10000, "range", image, ink);
        Finding finding{renderer, "range", "fail", 0.0, {},
                        imageName(out, renderer, "range").toStdString()};
        const double covered = coverage(ink);
        finding.measure = covered;
        // Something, and not everything. A renderer that gave up draws nothing;
        // one that lost its axis arithmetic to an overflow fills the pane.
        finding.verdict = (covered > 0.0005 && covered < 0.35) ? "pass" : "fail";
        finding.detail = "fraction of the pane inked, with y from 1e-30 to "
                         "1e30 on a linear axis";
        findings.push_back(finding);
    }

    // --- lines -------------------------------------------------------------
    {
        held = look(Shape::Sine, 1024, 1000, "lines", image, ink);
        Finding finding{renderer, "lines", "fail", 0.0, {},
                        imageName(out, renderer, "lines").toStdString()};
        const int colours = distinctColours(image, 24, 16, kSeriesChroma);
        finding.measure = static_cast<double>(colours);
        // The palette is a hue sweep, so a thousand lines produce far more
        // distinct colours than any capped or repeating palette could. Twenty
        // is the number that separates "drew them all" from "drew the first
        // few and stopped".
        finding.verdict = colours > 20 ? "pass" : "fail";
        finding.detail = "distinct quantised colours in the image, from 1024 "
                         "lines each given its own hue";
        findings.push_back(finding);
    }

    // --- logy --------------------------------------------------------------
    {
        Finding finding{renderer, "logy", "absent", 0.0,
                        "the renderer offers no logarithmic y axis", {}};
        if (surface.canDrawLogY()) {
            held = std::make_unique<SyntheticSource>(Shape::WideRange, 1, 10000,
                                                     options.seed);
            surface.setSource(held.get());
            // A positive window, stated rather than taken from the data's
            // extent. WideRange swings either side of zero, so its extent is
            // -1e30 to 1e30 and "the logarithm of that" is not a question with
            // an answer -- each renderer would invent a floor of its own and
            // the two pictures would not be of the same thing. Sixty decades,
            // all positive, is the same request to both.
            ViewWindow log = whole(*held);
            log.yMin = 1e-30;
            log.yMax = 1e30;
            surface.setViewWindow(log);
            if (surface.setLogY(true)) {
                image = grab(surface, timer);
                ink = profile(image, 16, kSeriesChroma);
                writePng(image, imageName(out, renderer, "logy"));
                finding.image = imageName(out, renderer, "logy").toStdString();
                const double covered = coverage(ink);
                finding.measure = covered;
                finding.verdict = covered > 0.0005 ? "pass" : "fail";
                finding.detail = "fraction of the pane inked on a logarithmic "
                                 "y axis";
            } else {
                finding.verdict = "fail";
                finding.detail = "the renderer claims a logarithmic y axis and "
                                 "then refused to set one";
            }
            surface.setLogY(false);
        }
        findings.push_back(finding);
    }

    // --- dpr ---------------------------------------------------------------
    {
        held = look(Shape::Sine, 8, 10000, "dpr", image, ink);
        Finding finding{renderer, "dpr", "fail", 0.0, {},
                        imageName(out, renderer, "dpr").toStdString()};
        const double ratio = window->effectiveDevicePixelRatio();
        const double expected = window->width() * ratio;
        finding.measure = image.width() / std::max(1.0, expected);
        // Within a pixel either way: a window manager may round the window's
        // own size, and what is being asked is whether the grab came back at
        // device resolution or at logical resolution and upscaled.
        finding.verdict =
            std::abs(image.width() - expected) <= 2.0 ? "pass" : "fail";
        finding.detail = "grab width over window width times device pixel "
                         "ratio (" + std::to_string(ratio) + "); run under "
                         "QT_SCALE_FACTOR=2 to make this say something";
        findings.push_back(finding);
    }

    return findings;
}

void report(const std::vector<Finding>& findings, const QString& out)
{
    std::printf("\n%-22s %-8s %-8s %10s   %s\n", "renderer", "check", "verdict",
                "measure", "what was measured");
    std::printf("%-22s %-8s %-8s %10s   %s\n", "----------------------",
                "--------", "--------", "----------",
                "------------------------------------------");
    for (const Finding& finding : findings) {
        std::printf("%-22s %-8s %-8s %10.4f   %s\n", finding.renderer.c_str(),
                    finding.check.c_str(), finding.verdict.c_str(),
                    finding.measure, finding.detail.c_str());
    }
    std::printf("\n");

    if (out.isEmpty()) {
        return;
    }
    const QString path = out + QStringLiteral("/verify.tsv");
    QDir().mkpath(QFileInfo(path).absolutePath());
    const bool fresh = !QFileInfo::exists(path);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        std::fprintf(stderr, "could not write %s\n", qPrintable(path));
        return;
    }
    QTextStream stream(&file);
    if (fresh) {
        stream << "renderer\tcheck\tverdict\tmeasure\tdetail\timage\n";
    }
    for (const Finding& finding : findings) {
        stream << QString::fromStdString(finding.renderer) << '\t'
               << QString::fromStdString(finding.check) << '\t'
               << QString::fromStdString(finding.verdict) << '\t'
               << finding.measure << '\t'
               << QString::fromStdString(finding.detail) << '\t'
               << QFileInfo(QString::fromStdString(finding.image)).fileName()
               << '\n';
    }
}

} // namespace spike
