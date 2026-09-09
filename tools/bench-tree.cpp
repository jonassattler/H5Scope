// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

// What the object tree costs, measured through the model the window actually
// binds to.
//
// The UI's complaint on a large file is never "the data is slow to read" --
// nothing in the tree reads an element. It is the walk: listing a group,
// resolving every link in it, and then saying something true beside every row
// the viewport shows. Each of those is a separate number here, because they
// have separate causes and only one of them is bounded by what is on screen.
//
//   open      h5core::File construction -- the superblock and the root group
//   root      H5TreeModel::setFile plus the first rowCount, i.e. what the
//             window pays before it can draw anything at all
//   expand    populating every group to a given depth: the walk
//   rows      every role of every populated row, which is what the delegate
//             asks for as the viewport passes over it. The interesting one:
//             this is per *visible* row, so a number here that scales with the
//             file is a bug rather than a cost.
//   viewport  the same, restricted to one screenful -- what a scroll costs
//   filter    a keystroke in the filter box, over the loaded tree
//   path      resolving an absolute path to an index, as the address bar does
//
// Run against the scale file:
//   make-example-file /tmp/h5bench --scale
//   bench-tree /tmp/h5bench/example_scale.h5

#include "BenchReport.hpp"

#include "gui/H5Thread.hpp"
#include "gui/H5TreeModel.hpp"
#include "gui/TreeFilterProxyModel.hpp"
#include "h5core/Error.hpp"
#include "h5core/File.hpp"

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

/// Every role the tree delegate declares as a required property, asked of one
/// index. This is the unit of work a row costs when it scrolls into view, and
/// asking for anything less would measure a delegate nobody wrote.
constexpr int kDelegateRoles[] = {
    gui::H5TreeModel::NameRole,            gui::H5TreeModel::PathRole,
    gui::H5TreeModel::IsGroupRole,         gui::H5TreeModel::IsCyclicRole,
    gui::H5TreeModel::IsLinkRole,          gui::H5TreeModel::LinkResolvesRole,
    gui::H5TreeModel::HasAttributesRole,   gui::H5TreeModel::AttributeCountRole,
    gui::H5TreeModel::IsImageRole,         gui::H5TreeModel::ImageSubclassRole,
    gui::H5TreeModel::LinkDescriptionRole, gui::H5TreeModel::MetaRole,
    gui::H5TreeModel::IsLastChildRole,     gui::H5TreeModel::AncestorLinesRole,
};

void askEveryRole(const gui::H5TreeModel& model, const QModelIndex& index)
{
    bench::blocking([&] {
        for (const int role : kDelegateRoles) {
            (void)model.data(index, role);
        }
    });
}

/// Populate every group down to `depth`, counting the rows that appear. This
/// is `expandRecursively(-1, depth)` in the view, which is what View -> Expand
/// runs and what a reader does by hand on the way to anything.
long long expand(gui::H5TreeModel& model, const QModelIndex& parent, int depth)
{
    // Level by level rather than depth-first, which is both how the view's own
    // expandRecursively() walks and the only way to measure the model honestly:
    // asking about a whole level and then letting the file answer is what turns
    // a thousand questions into one job. A depth-first walk that waited after
    // every node would measure a thousand round trips and call the batching a
    // failure.
    auto& h5 = gui::H5Thread::instance();
    std::vector<QModelIndex> level{parent};
    long long total = 0;

    for (int step = 0; step <= depth && !level.empty(); ++step) {
        for (const QModelIndex& node : level) {
            bench::blocking([&] { (void)model.rowCount(node); });
        }
        h5.drain();

        std::vector<QModelIndex> next;
        for (const QModelIndex& node : level) {
            const int rows = bench::blocking([&] { return model.rowCount(node); });
            total += rows;
            for (int row = 0; row < rows; ++row) {
                next.push_back(model.index(row, 0, node));
            }
        }
        h5.drain();
        level = std::move(next);
    }
    return total;
}

/// Every row that expand() brought in, in the order the view would meet them.
void collect(gui::H5TreeModel& model, const QModelIndex& parent,
             std::vector<QModelIndex>& out)
{
    const int rows = model.rowCount(parent);
    for (int row = 0; row < rows; ++row) {
        const QModelIndex index = model.index(row, 0, parent);
        out.push_back(index);
        if (model.isPopulated(index)) {
            collect(model, index, out);
        }
    }
}

/// The widest group in the file, and how many members it has. Expanding this
/// one node is the operation a reader is most likely to find slow, so the
/// benchmark reports it separately rather than averaging it away.
QModelIndex widest(gui::H5TreeModel& model, const std::vector<QModelIndex>& rows,
                   int& members)
{
    QModelIndex found;
    members = 0;
    for (const QModelIndex& index : rows) {
        if (!model.isPopulated(index)) {
            continue;
        }
        const int count = model.rowCount(index);
        if (count > members) {
            members = count;
            found = index;
        }
    }
    return found;
}

void usage(const char* program)
{
    std::fprintf(stderr,
                 "usage: %s FILE [--depth N] [--viewport N] [--filter TEXT]\n"
                 "\n"
                 "  --depth N     how many levels to populate (default 3)\n"
                 "  --viewport N  rows in one screenful (default 40)\n"
                 "  --filter TEXT what to type in the filter box (default \"channel\")\n"
                 "  --warm        leave the page cache alone (default: evict first)\n",
                 program);
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);

    std::string path;
    int depth = 3;
    int viewport = 40;
    QString filterText = QStringLiteral("channel");
    bool cold = true;

    for (int i = 1; i < argc; ++i) {
        const std::string_view argument = argv[i];
        if (argument == "--depth" && i + 1 < argc) {
            depth = std::atoi(argv[++i]);
        } else if (argument == "--viewport" && i + 1 < argc) {
            viewport = std::atoi(argv[++i]);
        } else if (argument == "--filter" && i + 1 < argc) {
            filterText = QString::fromUtf8(argv[++i]);
        } else if (argument == "--warm") {
            cold = false;
        } else if (argument.starts_with("--")) {
            usage(argv[0]);
            return 2;
        } else {
            path = argument;
        }
    }
    if (path.empty()) {
        usage(argv[0]);
        return 2;
    }

    try {
        if (cold && !bench::evict(path)) {
            std::fprintf(stderr,
                         "bench-tree: could not evict %s from the page cache; "
                         "the numbers below are warm\n",
                         path.c_str());
        }

        std::vector<bench::Row> rows;
        const long long none = 0;

        auto& h5 = gui::H5Thread::instance();
        bench::phase(rows, "open", [&] {
            h5.invoke([&](gui::H5Session& session) {
                session.open(path);
                return 0;
            });
        }, none, "");

        gui::H5TreeModel model;
        long long topLevel = 0;
        bench::phase(rows, "root", [&] {
            model.open();
            // The model reads asynchronously now, so every phase below settles
            // the queue before it stops the clock. What is being measured is
            // the same work; it is simply no longer being done on this thread.
            h5.drain();
            topLevel = model.rowCount({});
            h5.drain();
        }, topLevel, "");
        rows.back().units = topLevel;
        rows.back().unitName = "top-level rows";

        long long populated = 0;
        bench::phase(rows, "expand",
              [&] { populated = expand(model, QModelIndex{}, depth); }, populated,
              "rows populated");
        rows.back().units = populated;

        std::vector<QModelIndex> visited;
        collect(model, QModelIndex{}, visited);
        long long rendered = static_cast<long long>(visited.size());

        bench::phase(rows, "rows", [&] {
            for (const QModelIndex& index : visited) {
                askEveryRole(model, index);
            }
            gui::H5Thread::instance().drain();
        }, rendered, "rows rendered, cold");

        // A second pass over one screenful, on rows whose readout is now
        // cached: what scrolling back over ground already seen costs, which is
        // the difference between a tree that feels alive and one that does not.
        long long screenful = std::min<long long>(viewport, rendered);
        bench::phase(rows, "viewport", [&] {
            for (long long i = 0; i < screenful; ++i) {
                askEveryRole(model, visited[static_cast<std::size_t>(i)]);
            }
        }, screenful, "rows re-rendered, warm");

        // One group expanded from cold, in a model that has seen nothing else.
        // Split in two, because a view does these two things separately and
        // only the first one blocks the click:
        //
        //   listing   what rowCount() costs -- the link table, read whole,
        //             before a single row can be drawn. This is the number
        //             behind "expanding the tree takes a really long time".
        //   screenful the forty rows the viewport then shows. Everything below
        //             the fold is never asked for at all.
        int members = 0;
        const QModelIndex wide = widest(model, visited, members);
        if (wide.isValid()) {
            const QString widePath = model.pathAt(wide);
            // `fresh` rather than `cold`, which is the name of the page-cache
            // flag above it. Two things called cold in one function, one of
            // them a bool and the other a tree, is what MSVC's C4456 is for.
            gui::H5TreeModel fresh;
            fresh.open();
            h5.drain();
            fresh.revealPath(widePath);
            h5.drain();
            const QModelIndex again = fresh.indexForPath(widePath);
            long long listed = 0;
            bench::phase(rows, "listing", [&] {
                // The click, and then the answer. `ui ms` on this row is what
                // the click itself cost the window.
                (void)bench::blocking([&] { return fresh.rowCount(again); });
                gui::H5Thread::instance().drain();
                listed = bench::blocking([&] { return fresh.rowCount(again); });
            }, listed, "members listed on expand");
            rows.back().units = listed;

            long long shownRows = std::min<long long>(viewport, listed);
            bench::phase(rows, "screenful", [&] {
                // One layout pass of forty rows, which the model turns into one
                // job, and then the frame in which the answers land.
                for (long long row = 0; row < shownRows; ++row) {
                    askEveryRole(fresh, fresh.index(static_cast<int>(row), 0, again));
                }
                gui::H5Thread::instance().drain();
                for (long long row = 0; row < shownRows; ++row) {
                    askEveryRole(fresh, fresh.index(static_cast<int>(row), 0, again));
                }
            }, shownRows, "rows the viewport shows");
            std::printf("widest group: %s (%d members)\n",
                        widePath.toUtf8().constData(), members);
        }

        gui::TreeFilterProxyModel proxy;
        proxy.setSourceModel(&model);
        long long shown = 0;
        bench::phase(rows, "filter", [&] {
            proxy.setFilterText(filterText);
            shown = proxy.rowCount({});
        }, rendered, "rows tested");
        std::printf("filter \"%s\": %lld top-level rows survive\n",
                    filterText.toUtf8().constData(), shown);
        proxy.setFilterText(QString{});

        const QString target =
            model.pathAt(visited.empty() ? QModelIndex{} : visited.back());
        bench::phase(rows, "path", [&] { (void)model.indexForPath(target); }, none, "");

        bench::report(rows);
        gui::H5Thread::shutdown();
        return 0;
    }
    catch (const std::exception& error) {
        std::fprintf(stderr, "bench-tree: %s\n", error.what());
        gui::H5Thread::shutdown();
        return 1;
    }
}
