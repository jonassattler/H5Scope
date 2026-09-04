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

#include "gui/H5TreeModel.hpp"
#include "gui/TreeFilterProxyModel.hpp"
#include "h5core/Error.hpp"
#include "h5core/File.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QModelIndex>
#include <QString>

#include <fcntl.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>
#include <utility>
#include <vector>

namespace {

/// Evict the file from the page cache, so the run measures the disk rather
/// than the memory the last run left it in. posix_fadvise on clean pages needs
/// no privilege, which drop_caches does -- and it drops only this file, so the
/// rest of the machine is left alone.
///
/// It is the honest default for this benchmark. A large HDF5 file is opened
/// once, cold, by a reader who then waits; measuring the second open of it
/// measures a state the complaint was never about.
bool evict(const std::string& path)
{
    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        return false;
    }
    ::fsync(fd);
    const bool ok = ::posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED) == 0;
    ::close(fd);
    return ok;
}

/// Read syscalls this process has made, from /proc/self/io's `syscr`.
///
/// The number that matters most here, and the reason it is measured at all.
/// Wall-clock on a warm local NVMe says almost nothing about the machine the
/// complaint came from: every one of these is a page-cache hit at a
/// microsecond here and a round trip at several milliseconds over a network
/// filesystem, which is where large HDF5 files usually live. A count is the
/// same number on every machine; a duration is not.
long long readSyscalls()
{
    std::FILE* io = std::fopen("/proc/self/io", "re");
    if (io == nullptr) {
        return -1;
    }
    char line[128] = {};
    long long value = -1;
    while (std::fgets(line, sizeof(line), io) != nullptr) {
        if (std::sscanf(line, "syscr: %lld", &value) == 1) {
            break;
        }
    }
    std::fclose(io);
    return value;
}

struct Row
{
    const char* name;
    double milliseconds;
    long long reads;
    long long units;
    const char* unitName;
};

void report(const std::vector<Row>& rows)
{
    std::printf("\n%-10s %10s %12s %10s %9s   %s\n", "phase", "ms", "reads",
                "count", "reads/ea", "unit");
    std::printf("%-10s %10s %12s %10s %9s   %s\n", "----------", "----------",
                "------------", "----------", "---------",
                "------------------------");
    long long totalReads = 0;
    double totalMs = 0.0;
    for (const Row& row : rows) {
        totalReads += row.reads;
        totalMs += row.milliseconds;
        if (row.units > 0) {
            std::printf("%-10s %10.1f %12lld %10lld %9.2f   %s\n", row.name,
                        row.milliseconds, row.reads, row.units,
                        static_cast<double>(row.reads) / static_cast<double>(row.units),
                        row.unitName);
        } else {
            std::printf("%-10s %10.1f %12lld %10s %9s   %s\n", row.name,
                        row.milliseconds, row.reads, "-", "-", "");
        }
    }
    std::printf("%-10s %10.1f %12lld\n\n", "total", totalMs, totalReads);
}

/// One measured phase: how long it took and how many read syscalls it cost.
/// Steady clock rather than QElapsedTimer's default so the numbers mean the
/// same thing on every platform this is compared across.
template<typename F>
std::pair<double, long long> measure(F&& body)
{
    const long long readsBefore = readSyscalls();
    const auto start = std::chrono::steady_clock::now();
    body();
    const auto end = std::chrono::steady_clock::now();
    return {std::chrono::duration<double, std::milli>(end - start).count(),
            readSyscalls() - readsBefore};
}

/// `rows.push_back({name, ...measure(body)..., units, unit})` without writing
/// the structured binding out seven times.
template<typename F>
void phase(std::vector<Row>& rows, const char* name, F&& body, long long& units,
           const char* unitName)
{
    const auto [milliseconds, reads] = measure(std::forward<F>(body));
    rows.push_back({name, milliseconds, reads, units, unitName});
}

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
    for (const int role : kDelegateRoles) {
        (void)model.data(index, role);
    }
}

/// Populate every group down to `depth`, counting the rows that appear. This
/// is `expandRecursively(-1, depth)` in the view, which is what View -> Expand
/// runs and what a reader does by hand on the way to anything.
long long expand(gui::H5TreeModel& model, const QModelIndex& parent, int depth)
{
    if (depth < 0) {
        return 0;
    }
    const int rows = model.rowCount(parent);
    long long total = rows;
    for (int row = 0; row < rows; ++row) {
        total += expand(model, model.index(row, 0, parent), depth - 1);
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
        if (cold && !evict(path)) {
            std::fprintf(stderr,
                         "bench-tree: could not evict %s from the page cache; "
                         "the numbers below are warm\n",
                         path.c_str());
        }

        std::vector<Row> rows;
        long long none = 0;

        std::shared_ptr<h5core::File> file;
        phase(rows, "open", [&] { file = std::make_shared<h5core::File>(path); },
              none, "");

        gui::H5TreeModel model;
        long long topLevel = 0;
        phase(rows, "root", [&] {
            model.setFile(file);
            topLevel = model.rowCount({});
        }, topLevel, "");
        rows.back().units = topLevel;
        rows.back().unitName = "top-level rows";

        long long populated = 0;
        phase(rows, "expand",
              [&] { populated = expand(model, QModelIndex{}, depth); }, populated,
              "rows populated");
        rows.back().units = populated;

        std::vector<QModelIndex> visited;
        collect(model, QModelIndex{}, visited);
        long long rendered = static_cast<long long>(visited.size());

        phase(rows, "rows", [&] {
            for (const QModelIndex& index : visited) {
                askEveryRole(model, index);
            }
        }, rendered, "rows rendered, cold");

        // A second pass over one screenful, on rows whose readout is now
        // cached: what scrolling back over ground already seen costs, which is
        // the difference between a tree that feels alive and one that does not.
        long long screenful = std::min<long long>(viewport, rendered);
        phase(rows, "viewport", [&] {
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
            gui::H5TreeModel cold;
            cold.setFile(file);
            const QModelIndex again = cold.indexForPath(widePath);
            long long listed = 0;
            phase(rows, "listing", [&] { listed = cold.rowCount(again); }, listed,
                  "members listed on expand");
            rows.back().units = listed;

            long long shownRows = std::min<long long>(viewport, listed);
            phase(rows, "screenful", [&] {
                for (long long row = 0; row < shownRows; ++row) {
                    askEveryRole(cold, cold.index(static_cast<int>(row), 0, again));
                }
            }, shownRows, "rows the viewport shows");
            std::printf("widest group: %s (%d members)\n",
                        widePath.toUtf8().constData(), members);
        }

        gui::TreeFilterProxyModel proxy;
        proxy.setSourceModel(&model);
        long long shown = 0;
        phase(rows, "filter", [&] {
            proxy.setFilterText(filterText);
            shown = proxy.rowCount({});
        }, rendered, "rows tested");
        std::printf("filter \"%s\": %lld top-level rows survive\n",
                    filterText.toUtf8().constData(), shown);
        proxy.setFilterText(QString{});

        const QString target =
            model.pathAt(visited.empty() ? QModelIndex{} : visited.back());
        phase(rows, "path", [&] { (void)model.indexForPath(target); }, none, "");

        report(rows);
        return 0;
    }
    catch (const std::exception& error) {
        std::fprintf(stderr, "bench-tree: %s\n", error.what());
        return 1;
    }
}
