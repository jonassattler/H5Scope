// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

// What the Data Viewer costs, measured through the models the window binds to.
//
// bench-tree measures the walk: listing groups and saying something true beside
// every row. This is the other half -- what it costs to *look* at a dataset once
// one has been chosen. The two are separate tools because they have separate
// causes and separate fixes, and because a number that averaged them would
// describe neither.
//
//   open      the file, and the selection that follows it
//   paint     one screenful of the grid, cold: what the first frame costs
//   scroll    a page down, i.e. the next block
//   revisit   the same screenful again, warm. Should read nothing at all; a
//             number here is a cache that is not working.
//   widths    fitting the columns to their contents, which must read nothing
//   extent    valueExtent(), the sample a colour ramp is stretched between
//   plot      the lines a selection opens on -- sixty-four of them
//   plotall   every line of the table. The phase that used to be one blocking
//             round trip per line; `crossings` is where that shows.
//   image     the raster, thinned to what a screen can hold
//   slice     a postprocessing run over the selection
//
// The columns are bench-tree's, plus `crossings`. See tools/BenchReport.hpp for
// what each of them means and why a count travels where a duration does not.
//
// Run against the scale file:
//   make-example-file /tmp/h5bench --scale
//   bench-data /tmp/h5bench/example_scale.h5
//
// tests/test_cost.cpp is the half of this that fails a build. It asserts on the
// same two counts over a source that needs no file at all; this one puts real
// milliseconds beside them on a file of a realistic size.

#include "BenchReport.hpp"

#include "gui/AppController.hpp"
#include "gui/DatasetImage.hpp"
#include "gui/DatasetPlot.hpp"
#include "gui/DatasetTableModel.hpp"
#include "gui/H5Thread.hpp"
#include "h5core/Error.hpp"
#include "h5core/File.hpp"
#include "h5core/Types.hpp"
#include "postproc/Operations.hpp"
#include "postproc/Pipeline.hpp"

#include <QCoreApplication>
#include <QModelIndex>
#include <QString>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>
#include <string_view>
#include <vector>

namespace {

/// Wait for everything asked of the HDF5 thread. Every phase below ends with
/// one of these: the models answer with what they know and ask for the rest, so
/// a number taken without it is a number for the part that happened to be done.
void settle()
{
    gui::H5Thread::instance().drain();
    QCoreApplication::processEvents();
    gui::H5Thread::instance().drain();
}

/// The biggest numeric dataset in the file, by element count.
///
/// Chosen rather than named, so the tool is useful on a file nobody wrote this
/// benchmark for. It is a bounded walk -- the point is to find something worth
/// drawing, not to survey the file -- and `--dataset` overrides it.
struct Found {
    std::string path;
    hsize_t elements = 0;
    std::vector<hsize_t> shape;
};

void searchForDataset(h5core::File& file, const std::string& where, int depth,
                      int& budget, Found& best)
{
    if (depth < 0 || budget <= 0) {
        return;
    }
    std::vector<h5core::NodeInfo> children;
    try {
        children = file.children(where, h5core::File::Resolve::Objects);
    } catch (const h5core::H5Error&) {
        return;
    }

    for (const h5core::NodeInfo& child : children) {
        if (--budget <= 0) {
            return;
        }
        if (child.kind == h5core::NodeKind::Group) {
            searchForDataset(file, child.path, depth - 1, budget, best);
            continue;
        }
        if (child.kind != h5core::NodeKind::Dataset) {
            continue;
        }
        try {
            const h5core::Dataset dataset(file, child.path);
            const auto& info = dataset.info();
            if (!info.isNumeric() || !info.readable() || info.rank() < 2) {
                continue;
            }
            if (info.elementCount() > best.elements) {
                best = {child.path, info.elementCount(), info.shape};
            }
        } catch (const h5core::H5Error&) {
            // A dataset that will not open is not the one being looked for.
        }
    }
}

void usage(const char* program)
{
    std::fprintf(stderr,
                 "usage: %s FILE [--dataset PATH] [--rows N] [--columns N]\n"
                 "            [--lines N] [--warm]\n"
                 "\n"
                 "  --dataset PATH  what to draw (default: the largest numeric one)\n"
                 "  --rows N        rows in one screenful (default 40)\n"
                 "  --columns N     columns in one screenful (default 20)\n"
                 "  --lines N       lines for the `plotall` phase (default: every one)\n"
                 "  --warm          leave the page cache alone (default: evict first)\n",
                 program);
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);

    std::string path;
    std::string dataset;
    int viewportRows = 40;
    int viewportColumns = 20;
    int lines = -1;
    bool cold = true;

    for (int i = 1; i < argc; ++i) {
        const std::string_view argument = argv[i];
        if (argument == "--dataset" && i + 1 < argc) {
            dataset = argv[++i];
        } else if (argument == "--rows" && i + 1 < argc) {
            viewportRows = std::atoi(argv[++i]);
        } else if (argument == "--columns" && i + 1 < argc) {
            viewportColumns = std::atoi(argv[++i]);
        } else if (argument == "--lines" && i + 1 < argc) {
            lines = std::atoi(argv[++i]);
        } else if (argument == "--warm") {
            cold = false;
        } else if (argument.starts_with("--")) {
            usage(argv[0]);
            return 2;
        } else {
            path = argument;
        }
    }
    if (path.empty() || viewportRows <= 0 || viewportColumns <= 0) {
        usage(argv[0]);
        return 2;
    }

    try {
        if (cold && !bench::evict(path)) {
            std::fprintf(stderr,
                         "bench-data: could not evict %s from the page cache; "
                         "the numbers below are warm\n",
                         path.c_str());
        }

        // Which dataset, before the clock starts. Finding one is a walk of the
        // file and is bench-tree's subject, not this one's.
        if (dataset.empty()) {
            auto& h5 = gui::H5Thread::instance();
            const Found found = h5.invoke([&](gui::H5Session& session) {
                session.open(path);
                Found best;
                int budget = 4096;
                searchForDataset(*session.file(), "/", 4, budget, best);
                session.close();
                return best;
            });
            if (found.path.empty()) {
                std::fprintf(stderr,
                             "bench-data: no numeric dataset of rank 2 or more "
                             "in %s -- name one with --dataset\n",
                             path.c_str());
                gui::H5Thread::shutdown();
                return 1;
            }
            dataset = found.path;
            std::printf("dataset: %s (%llu elements)\n", dataset.c_str(),
                        static_cast<unsigned long long>(found.elements));
            if (cold) {
                bench::evict(path); // the search warmed it; put it back
            }
        }

        std::vector<bench::Row> rows;
        gui::AppController controller;

        auto* table = qobject_cast<gui::DatasetTableModel*>(controller.datasetModel());
        gui::DatasetPlot* plot = controller.datasetPlot();
        gui::DatasetImage* image = controller.datasetImage();
        if (table == nullptr || plot == nullptr || image == nullptr) {
            std::fprintf(stderr, "bench-data: the controller has no data models\n");
            gui::H5Thread::shutdown();
            return 1;
        }

        bench::phase(rows, "open", [&] {
            controller.openFile(QString::fromStdString(path));
            settle();
            controller.selectPath(QString::fromStdString(dataset));
            settle();
        }, 0, "");

        const long long tableRows = table->rowCount();
        const long long tableColumns = table->columnCount();
        std::printf("table: %lld x %lld\n", tableRows, tableColumns);
        if (tableRows <= 0 || tableColumns <= 0) {
            std::fprintf(stderr,
                         "bench-data: %s draws no table -- it may not be "
                         "readable\n",
                         dataset.c_str());
            gui::H5Thread::shutdown();
            return 1;
        }

        // One layout pass over a viewport: every cell of it asks the model,
        // and the block behind them is fetched once.
        const auto paint = [&](int firstRow, int firstColumn) {
            const int lastRow =
                static_cast<int>(std::min<long long>(firstRow + viewportRows, tableRows));
            const int lastColumn = static_cast<int>(
                std::min<long long>(firstColumn + viewportColumns, tableColumns));
            for (int r = firstRow; r < lastRow; ++r) {
                for (int c = firstColumn; c < lastColumn; ++c) {
                    bench::blocking([&] {
                        (void)table->data(table->index(r, c), Qt::DisplayRole);
                    });
                }
            }
            settle();
            // Again, so the cells the answer filled in are actually read --
            // which is what a repaint after the block lands does.
            for (int r = firstRow; r < lastRow; ++r) {
                for (int c = firstColumn; c < lastColumn; ++c) {
                    bench::blocking([&] {
                        (void)table->data(table->index(r, c), Qt::DisplayRole);
                    });
                }
            }
        };

        const long long painted =
            std::min<long long>(viewportRows, tableRows)
            * std::min<long long>(viewportColumns, tableColumns);

        bench::phase(rows, "paint", [&] { paint(0, 0); }, painted, "cells, cold");

        const int nextPage = static_cast<int>(
            std::min<long long>(viewportRows, std::max<long long>(tableRows - 1, 0)));
        bench::phase(rows, "scroll", [&] { paint(nextPage, 0); }, painted,
                     "cells, next block");

        bench::phase(rows, "revisit", [&] { paint(nextPage, 0); }, painted,
                     "cells, warm");

        bench::phase(rows, "widths", [&] {
            bench::blocking([&] {
                (void)table->widestCell(0, static_cast<int>(tableRows), 0,
                                        static_cast<int>(tableColumns));
            });
        }, painted, "cells measured");

        bench::phase(rows, "extent", [&] {
            bench::blocking([&] { (void)table->valueExtent(); });
            settle();
        }, 0, "");

        // The plot, twice: the sixty-four lines it opens on, and then every
        // line there is. `crossings` on the second is the number the batching
        // exists to hold down -- one per batch rather than one per line.
        long long opening = 0;
        bench::phase(rows, "plot", [&] {
            bench::blocking([&] { (void)plot->pointCount(); });
            settle();
            opening = plot->seriesCount();
        }, opening, "lines");
        rows.back().units = opening;

        const long long wanted =
            (lines > 0) ? std::min<long long>(lines, tableRows) : tableRows;
        long long drawn = 0;
        bench::phase(rows, "plotall", [&] {
            bench::blocking([&] { plot->selectFirst(static_cast<int>(wanted)); });
            bench::blocking([&] { (void)plot->pointCount(); });
            settle();
            drawn = plot->seriesCount();
        }, drawn, "lines");
        rows.back().units = drawn;

        bench::phase(rows, "image", [&] {
            bench::blocking([&] { (void)image->hasData(); });
            settle();
            bench::blocking([&] { (void)image->render(); });
        }, static_cast<long long>(image->width()) * image->height(),
                     "pixels rendered");
        rows.back().units = static_cast<long long>(image->width()) * image->height();

        // One postprocessing run over the whole selection: the slice is read
        // out of the file as hyperslabs and everything after it is arithmetic.
        long long computed = 0;
        bench::phase(rows, "slice", [&] {
            auto& h5 = gui::H5Thread::instance();
            computed = h5.invoke([&](gui::H5Session& session) -> long long {
                const h5core::Dataset* opened =
                    session.dataset(dataset);
                if (opened == nullptr) {
                    return 0;
                }
                const postproc::RunResult result = postproc::run(
                    *opened,
                    {{postproc::OperationKind::Slice, QStringLiteral("...")}}, 1);
                return result.usable()
                           ? static_cast<long long>(result.array.size())
                           : 0;
            });
        }, computed, "elements computed");
        rows.back().units = computed;

        bench::report(rows);
        gui::H5Thread::shutdown();
        return 0;
    }
    catch (const std::exception& error) {
        std::fprintf(stderr, "bench-data: %s\n", error.what());
        gui::H5Thread::shutdown();
        return 1;
    }
}
