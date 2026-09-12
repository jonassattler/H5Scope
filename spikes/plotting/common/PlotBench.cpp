// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "common/PlotBench.hpp"

#include <QtCore/QByteArray>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QTextStream>
#include <QtGui/QSurfaceFormat>

#include <cstdio>
#include <string>
#include <cstdlib>

namespace spike {
namespace {

// The two axes of the grid. Series first because that is the one the
// application can reach by accident -- `all` in the legend of a wide table --
// where the deep end is reached only by opening a long dataset on purpose.
const std::vector<int> kSeriesCounts = {1, 8, 64, 256, 1024, 10000};
const std::vector<int> kPointCounts = {1000, 100000, 1000000, 10000000};

} // namespace

std::vector<Cell> grid(std::int64_t pointBudget)
{
    std::vector<Cell> cells;
    for (const int series : kSeriesCounts) {
        for (const int points : kPointCounts) {
            Cell cell;
            cell.series = series;
            cell.points = points;
            cells.push_back(cell);
        }
    }
    // The two the application actually performs, appended so they are read
    // last and marked so they are read first.
    cells.push_back({64, 2048, true});
    cells.push_back({10000, 2048, true});

    for (Cell& cell : cells) {
        const std::int64_t total = static_cast<std::int64_t>(cell.series)
                                   * static_cast<std::int64_t>(cell.points);
        // The real cells are never skipped. A budget that excluded the
        // workload the question is about would be answering a different one.
        cell.skipped = !cell.realWorkload && total > pointBudget;
    }
    return cells;
}

void BenchReport::print(const std::string& title) const
{
    std::printf("\n%s\n", title.c_str());
    std::printf("%-10s %7s %9s %9s %9s %9s %8s %8s %8s %9s %8s %9s   %s\n",
                "shape", "series", "points", "clear ms", "build ms", "first ms",
                "cpu p50", "cpu p95", "rss MiB", "data MiB", "allocs",
                "alloc MiB", "");
    std::printf("%-10s %7s %9s %9s %9s %9s %8s %8s %8s %9s %8s %9s\n",
                "----------", "-------", "---------", "---------", "---------",
                "---------", "--------", "--------", "--------", "---------",
                "--------", "---------");

    for (const Row& row : rows_) {
        if (row.skipped) {
            std::printf("%-10s %7d %9d %9s %9s %9s %8s %8s %8s %9s %8s %9s   %s\n",
                        row.shape.c_str(), row.series, row.points, "-", "-", "-",
                        "-", "-", "-", "-", "-", "-", row.note.c_str());
            continue;
        }
        // The marker and the note both, not one or the other: a row that is
        // the real workload *and* stopped drawing must say both things.
        std::string marker = row.realWorkload ? "<- the real one" : "";
        if (!row.note.empty()) {
            marker += marker.empty() ? row.note : ("  " + row.note);
        }
        char rss[24];
        if (row.rssMiB < 0.0) {
            std::snprintf(rss, sizeof rss, "-");
        } else {
            std::snprintf(rss, sizeof rss, "%.0f", row.rssMiB);
        }
        std::printf("%-10s %7d %9d %9.1f %9.1f %9.1f %8.2f %8.2f %8s %9.0f %8llu %9llu   %s\n",
                    row.shape.c_str(), row.series, row.points, row.clearMs,
                    row.buildMs, row.firstFrameMs, row.frames.cpuMedian,
                    row.frames.cpuP95, rss,
                    row.dataMiB,
                    static_cast<unsigned long long>(row.allocCalls),
                    static_cast<unsigned long long>(row.allocMiB),
                    marker.c_str());
    }
    std::printf("\n");
}

bool BenchReport::appendTsv(const QString& path) const
{
    if (path.isEmpty()) {
        return true;
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    const bool fresh = !QFileInfo::exists(path);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        std::fprintf(stderr, "could not write %s\n", qPrintable(path));
        return false;
    }
    QTextStream out(&file);
    if (fresh) {
        out << "renderer\tshape\tseries\tpoints\treal\tskipped\tclear_ms\tbuild_ms\tfirst_ms"
               "\tcpu_p50\tcpu_p95\tcpu_worst\twall_p50\twall_p95\tframes\trss_mib"
               "\tdata_mib\tallocs\talloc_mib\tnote\n";
    }
    for (const Row& row : rows_) {
        out << QString::fromStdString(row.renderer) << '\t'
            << QString::fromStdString(row.shape) << '\t' << row.series << '\t'
            << row.points << '\t' << (row.realWorkload ? 1 : 0) << '\t'
            << (row.skipped ? 1 : 0) << '\t' << row.clearMs << '\t'
            << row.buildMs << '\t'
            << row.firstFrameMs << '\t' << row.frames.cpuMedian << '\t'
            << row.frames.cpuP95 << '\t' << row.frames.cpuWorst << '\t'
            << row.frames.wallMedian << '\t' << row.frames.wallP95 << '\t'
            << row.frames.frames << '\t' << row.rssMiB << '\t' << row.dataMiB
            << '\t' << static_cast<qulonglong>(row.allocCalls) << '\t'
            << static_cast<qulonglong>(row.allocMiB) << '\t'
            << QString::fromStdString(row.note) << '\n';
    }
    return true;
}

Options parseOptions(const QStringList& arguments)
{
    Options options;
    for (int i = 1; i < arguments.size(); ++i) {
        const QString argument = arguments.at(i);
        const auto next = [&](int& into) {
            if (i + 1 < arguments.size()) {
                into = arguments.at(++i).toInt();
            }
        };
        if (argument == QLatin1String("--bench")) {
            options.bench = true;
        } else if (argument == QLatin1String("--verify")) {
            options.verify = true;
        } else if (argument == QLatin1String("--offscreen")) {
            options.offscreen = true;
        } else if (argument == QLatin1String("--help") || argument == QLatin1String("-h")) {
            options.help = true;
        } else if (argument == QLatin1String("--out")) {
            if (i + 1 < arguments.size()) {
                options.out = arguments.at(++i);
            }
        } else if (argument == QLatin1String("--frames")) {
            next(options.frames);
        } else if (argument == QLatin1String("--warmup")) {
            next(options.warmup);
        } else if (argument == QLatin1String("--series")) {
            next(options.series);
        } else if (argument == QLatin1String("--points")) {
            next(options.points);
        } else if (argument == QLatin1String("--seed")) {
            int seed = 7;
            next(seed);
            options.seed = static_cast<std::uint32_t>(seed);
        } else if (argument == QLatin1String("--variant")) {
            if (i + 1 < arguments.size()) {
                options.variant = arguments.at(++i);
            }
        } else if (argument == QLatin1String("--cell")) {
            if (i + 1 < arguments.size()) {
                const QStringList parts = arguments.at(++i).split(QLatin1Char('x'));
                if (parts.size() == 2) {
                    options.cellSeries = parts.at(0).toInt();
                    options.cellPoints = parts.at(1).toInt();
                } else {
                    options.unknownFlag = arguments.at(i);
                }
            }
        } else if (argument == QLatin1String("--budget")) {
            if (i + 1 < arguments.size()) {
                options.budget = arguments.at(++i).toLongLong();
            }
        } else if (argument == QLatin1String("--shape")) {
            if (i + 1 < arguments.size()) {
                const QString wanted = arguments.at(++i);
                if (!parseShape(wanted.toStdString(), options.shape)) {
                    options.unknownFlag = wanted;
                }
            }
        } else {
            options.unknownFlag = argument;
        }
    }
    return options;
}

void printUsage(const char* program)
{
    std::printf(
        "usage: %s [--bench] [--out DIR] [options]\n"
        "\n"
        "  With no --bench the spike opens a window and draws, which is the\n"
        "  half of the comparison a table cannot hold.\n"
        "\n"
        "  --bench            run the grid and exit\n"
        "  --verify           run the correctness checks and exit\n"
        "  --out DIR          write results.tsv and any images here\n"
        "  --frames N         measured frames per cell (default 120)\n"
        "  --warmup N         frames discarded first (default 10)\n"
        "  --budget N         points one cell may hold (default 25000000)\n"
        "  --cell SxP         run one cell instead of the grid, e.g. 64x2048\n"
        "  --variant NAME     a spike's alternative way of drawing, if it has one\n"
        "  --series N         interactive: lines to draw (default 64)\n"
        "  --points N         interactive: points per line (default 2048)\n"
        "  --shape NAME       sine noise spike nan bigx range unsorted constant\n"
        "  --seed N           the generator's seed (default 7)\n"
        "  --offscreen        no display; software renderer, labelled as such\n",
        program);
}

bool prepareRenderEnvironment(const Options& options, std::string& why)
{
    // The basic render loop, always. Threaded rendering would put the frame
    // signals on another thread and, worse, would let the GUI thread run ahead
    // of the frames it is timing -- so a spike could report a build time that
    // had not finished being drawn.
    qputenv("QSG_RENDER_LOOP", "basic");

    // Greyscale antialiasing for text, not subpixel.
    //
    // The correctness checks tell a curve from the chrome around it by colour:
    // the curves are saturated hues, the grid and the labels are grey. Subpixel
    // antialiasing makes every glyph edge *coloured* -- measured, blue and
    // orange fringes of chroma 75 to 94 along the caption -- and those fringes
    // are then the highest "data" ink on the surface, which is how a spike
    // check came to report all three renderers as having lost a spike all three
    // had drawn.
    //
    // QQuickWindow::setTextRenderType(QtTextRendering) is not enough: Qt's
    // distance-field glyph node does subpixel antialiasing too where the
    // surface allows it. This is the knob that actually turns it off.
    qputenv("QSG_DISTANCEFIELD_ANTIALIASING", "gray");

    // No vsync. The benchmark draws as fast as it can and asks how long each
    // frame took; waiting for the display to be ready measures the display.
    // It matters more than that here: on a window the compositor is not
    // consuming, the blocking swap does not settle at the refresh interval,
    // it settles at one to two *seconds* -- measured -- and every renderer
    // then scores the same and the run says nothing.
    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    format.setSwapInterval(0);
    QSurfaceFormat::setDefaultFormat(format);

    if (options.offscreen) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
        why = "offscreen: Qt Quick falls back to the software renderer, so "
              "these numbers compare rasterisers and not the renderers that ship";
        return true;
    }

    // Wayland throttles a client to the compositor's frame callbacks, and a
    // window the compositor is not showing -- unfocused, occluded, on another
    // workspace, or opened by a script -- gets none. Qt's basic render loop
    // then waits, no frame is ever recorded, and every renderer here scores
    // zero milliseconds for having drawn nothing. Measured: identical runs
    // report 12 ms a frame under xcb and no frames at all under wayland.
    //
    // So a benchmark asks for xcb where there is an X server to ask, and says
    // so. An explicit QT_QPA_PLATFORM is never overridden -- someone who set
    // it meant it.
    const bool hasX = !qEnvironmentVariableIsEmpty("DISPLAY");
    const bool hasWayland = !qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY");
    if ((options.bench || options.verify)
        && qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        if (hasX) {
            qputenv("QT_QPA_PLATFORM", "xcb");
        } else if (hasWayland) {
            why = "benchmarking on wayland: if the window is not visible the "
                  "compositor sends no frame callbacks, rendering stalls, and "
                  "every cell reports zero frames. Run it on a visible window, "
                  "or set DISPLAY so xcb can be used instead.";
        }
    }

    const bool hasDisplay = hasX || hasWayland;
    if (!hasDisplay) {
        why = "no DISPLAY or WAYLAND_DISPLAY. Rendering benchmarks refuse to "
              "run under the offscreen platform by accident -- it declares no "
              "RHI capability, so Qt Quick would silently draw through its "
              "software rasteriser and the three spikes would be compared "
              "doing something none of them does in the application. Pass "
              "--offscreen to measure that anyway.";
        return false;
    }
    return true;
}

} // namespace spike
