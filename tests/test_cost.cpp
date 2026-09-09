// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

// What the views cost, asserted rather than measured.
//
// tools/bench-tree and tools/bench-data report durations and read counts over
// a file of a few gigabytes. Neither of them is run by anything: they are for
// a person looking into a complaint, and the numbers in the DEVLOG beside them
// are a snapshot of one machine on one day. This suite is the other half --
// the part that fails a build.
//
// It asserts on *counts*, never on time. A duration measures the machine the
// test happens to run on; a count measures the program, is the same number on
// every machine, and is the thing that actually regresses. Two counts matter
// and they move independently:
//
//   reads      calls into the DataSource. Bounded by how the code is written.
//   crossings  jobs sent to the HDF5 thread. Each is a queued call and, for
//              the blocking form, a wait on both sides -- so a loop that asks
//              per row rather than per screenful moves exactly the same bytes
//              and takes a thousand times as long. This is the number that
//              caught the plot reading one line per round trip.
//
// Nothing here opens a file. h5test::CountingSource is a DataSource that
// answers like a dataset and writes down what it was asked, and H5Session will
// hold any DataSource -- which is how postprocessing already reaches the views.
// So the real DatasetTableModel, DatasetPlot and DatasetImage run over an
// instrumented source with no file on disk and no timing anywhere.

#include "ExampleFile.hpp"
#include "support/AsyncModels.hpp"
#include "support/CountingSource.hpp"
#include "support/TestFile.hpp"

#include "gui/AppController.hpp"
#include "gui/DatasetImage.hpp"
#include "gui/DatasetPlot.hpp"
#include "gui/DatasetTableModel.hpp"
#include "gui/H5Thread.hpp"
#include "gui/H5TreeModel.hpp"
#include "gui/TableLayout.hpp"
#include "gui/TreeFilterProxyModel.hpp"
#include "postproc/Pipeline.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QModelIndex>
#include <QString>

#include <algorithm>
#include <memory>
#include <vector>

using gui::DatasetImage;
using gui::DatasetPlot;
using gui::DatasetTableModel;
using gui::H5Thread;
using h5test::CountingSource;

namespace {

/// A table, a plot and an image over a counted source, with no file involved.
///
/// The models are the application's own. What is replaced is the thing under
/// them, which is the one seam this needs: `H5Session::setComputed` installs
/// any DataSource as what the views read, and that is not a hook added for
/// testing -- it is how a postprocessing pipeline's output reaches them.
struct Counted {
    std::shared_ptr<CountingSource> source;
    DatasetTableModel table;
    DatasetPlot plot{&table};
    DatasetImage image{&table};

    explicit Counted(std::vector<hsize_t> shape)
        : source(std::make_shared<CountingSource>(std::move(shape)))
    {
        install();
        table.setSource(true, source->info(), QStringLiteral("/counted"));
        settleAll();
        source->reset();
    }

    ~Counted()
    {
        // The session outlives this fixture, and a source left in it would be
        // read by whatever ran next.
        H5Thread::instance().invoke([](gui::H5Session& session) {
            session.setComputed(nullptr);
            return 0;
        });
    }

    Counted(const Counted&) = delete;
    Counted& operator=(const Counted&) = delete;
    Counted(Counted&&) = delete;
    Counted& operator=(Counted&&) = delete;

    void install()
    {
        H5Thread::instance().invoke([this](gui::H5Session& session) {
            session.setComputed(source);
            return 0;
        });
    }

    /// Let everything asked for arrive. Block reads are asynchronous, so a
    /// count taken without this is a count of what happened to be finished.
    static void settleAll()
    {
        h5test::settle();
        QCoreApplication::processEvents();
        h5test::settle();
    }

    /// Crossings of the HDF5 thread across `body`, and the reads it caused.
    struct Cost {
        long long crossings = 0;
        int reads = 0;
        hsize_t elements = 0;
    };

    template<typename F>
    Cost measure(F&& body)
    {
        source->reset();
        const long long before = H5Thread::instance().crossings();
        body();
        settleAll();
        return {H5Thread::instance().crossings() - before, source->reads(),
                source->elementsRead()};
    }

    /// Ask the grid for one cell, as a delegate painting it would.
    void paint(int row, int column)
    {
        (void)table.data(table.index(row, column), Qt::DisplayRole);
    }

    /// Ask for a rectangle of cells, as one layout pass over a viewport does.
    void paintRect(int firstRow, int rows, int firstColumn, int columns)
    {
        for (int r = firstRow; r < firstRow + rows; ++r) {
            for (int c = firstColumn; c < firstColumn + columns; ++c) {
                paint(r, c);
            }
        }
    }
};

} // namespace

// ---------------------------------------------------------------------------
// The table: a block at a time, and only the block
// ---------------------------------------------------------------------------

TEST_CASE("the grid reads a block at a time, whatever it is asked for",
          "[cost][table]")
{
    Counted counted({1000, 1000});

    SECTION("one screenful is one crossing and one block")
    {
        // Forty rows of twenty columns is 800 cells, and every one of them
        // asks the model. What it must not be is 800 reads: the block behind
        // them is fetched once, and the block is 64 rows of runs.
        const auto cost = counted.measure([&] { counted.paintRect(0, 40, 0, 20); });

        CHECK(cost.crossings == 1);
        CHECK(cost.reads == 64);         // one run per row of the block
        CHECK(cost.elements == 64 * 64); // and the block is 64 x 64
        // A row of the block is one hyperslab, not sixty-four of them.
        CHECK(counted.source->largestRead() == 64);
    }

    SECTION("scrolling inside the block that is already there reads nothing")
    {
        counted.paintRect(0, 40, 0, 20);
        Counted::settleAll();

        const auto cost = counted.measure([&] { counted.paintRect(20, 40, 20, 40); });

        CHECK(cost.crossings == 0);
        CHECK(cost.reads == 0);
    }

    SECTION("scrolling out of it fetches exactly one more block")
    {
        counted.paintRect(0, 40, 0, 20);
        Counted::settleAll();

        const auto cost = counted.measure([&] { counted.paint(100, 0); });

        CHECK(cost.crossings == 1);
        CHECK(cost.reads == 64);
        CHECK(cost.elements == 64 * 64);
    }

    SECTION("a hundred cells of one unread block still ask for it once")
    {
        // The guard `asked_` is: the first paint of a screenful asks about a
        // hundred cells before any answer lands, and without a record of what
        // is already on its way each of those queues a read of the same block.
        const auto cost = counted.measure([&] {
            for (int i = 0; i < 100; ++i) {
                counted.paint(200 + (i % 30), 200 + (i % 30));
            }
        });

        CHECK(cost.crossings == 1);
        CHECK(cost.reads == 64);
    }

    SECTION("a viewport straddling two blocks is fetched once and stays")
    {
        // Forty rows starting at row 40 cover rows 40 to 79: the end of the
        // block at 0 and the start of the block at 64. With a cache of one
        // block the two evicted each other, and repainting a window nobody had
        // scrolled cost a crossing and four thousand elements every frame.
        // bench-data's `revisit` phase is where that showed.
        const auto first = counted.measure([&] { counted.paintRect(40, 40, 0, 20); });

        // Two blocks, each read once. Three would be the straddle asking for
        // one of them twice, which is what a single outstanding-request slot
        // used to cause.
        CHECK(first.crossings == 2);
        CHECK(first.reads == 2 * 64);

        const auto again = counted.measure([&] { counted.paintRect(40, 40, 0, 20); });
        CHECK(again.crossings == 0);
        CHECK(again.reads == 0);
    }

    SECTION("scrolling past what the cache holds drops the oldest, not the newest")
    {
        // Four blocks are kept, so a walk through five puts the first out and
        // leaves the four just seen. Coming back to the newest is free; coming
        // back to the one that fell off is a read, which is the cache doing
        // what a bounded cache is for.
        for (int block = 0; block < 5; ++block) {
            counted.paintRect(block * 64, 8, 0, 8);
            Counted::settleAll();
        }

        const auto recent = counted.measure([&] { counted.paintRect(4 * 64, 8, 0, 8); });
        CHECK(recent.crossings == 0);

        const auto evicted = counted.measure([&] { counted.paintRect(0, 8, 0, 8); });
        CHECK(evicted.crossings == 1);
    }

    SECTION("fitting a column to its contents never reads outside the block")
    {
        counted.paintRect(0, 10, 0, 10);
        Counted::settleAll();

        // widestCell over a rectangle far outside the cached block. It is a
        // column width, and a column width is not worth a read.
        const auto cost =
            counted.measure([&] { (void)counted.table.widestCell(0, 900, 0, 900); });

        CHECK(cost.reads == 0);
    }
}

TEST_CASE("a table read as one line is read as one hyperslab", "[cost][table]")
{
    Counted counted({4, 1000});

    SECTION("consecutive columns coalesce into one read per row")
    {
        const auto cost = counted.measure(
            [&] { (void)counted.table.sampleValues(0, -1, 8, 0, -1, 4096); });

        CHECK(cost.crossings == 1);
        CHECK(cost.reads == 4); // one per row, not one per column
        CHECK(cost.elements == 4 * 1000);
    }

    SECTION("a scattered selection costs a read per run and no more")
    {
        gui::TableLayout layout = counted.table.layout();
        // Four columns that share no run: 0, 2, 4, 6.
        layout.indices[1] = {0, 2, 4, 6};
        counted.table.setLayout(layout);
        Counted::settleAll();

        const auto cost = counted.measure(
            [&] { (void)counted.table.sampleValues(0, -1, 8, 0, -1, 4096); });

        CHECK(cost.crossings == 1);
        // Four rows of four runs of one. That is the honest price of asking
        // for scattered indices, and the assertion is that it is not worse.
        CHECK(cost.reads == 16);
        CHECK(cost.elements == 16);
    }
}

// ---------------------------------------------------------------------------
// The plot: lines in batches, never one round trip each
// ---------------------------------------------------------------------------

TEST_CASE("the plot reads its lines in batches", "[cost][plot]")
{
    Counted counted({1000, 500});

    SECTION("the sixty-four lines a selection opens on are one crossing")
    {
        const auto cost = counted.measure([&] { (void)counted.plot.pointCount(); });

        CHECK(counted.plot.seriesCount() == DatasetPlot::initialSeriesLimit());
        CHECK(cost.crossings == 1);
        CHECK(cost.reads == 64); // one line each, in one job
    }

    SECTION("every line of a thousand-row table is four crossings, not a thousand")
    {
        // The regression this suite was written for. Read one line per
        // crossing, `all` on this table is a thousand blocking round trips and
        // a window that stops answering; the reads are identical either way.
        (void)counted.plot.pointCount(); // the opening sixty-four
        Counted::settleAll();

        const auto cost = counted.measure([&] {
            counted.plot.selectAll();
            (void)counted.plot.pointCount();
        });

        CHECK(counted.plot.seriesCount() == 1000);
        // 936 lines not already held, in batches of 256.
        CHECK(cost.crossings == 4);
        CHECK(cost.reads == 936);
    }

    SECTION("a line already drawn is not read again")
    {
        (void)counted.plot.pointCount();
        Counted::settleAll();

        const auto cost = counted.measure([&] {
            counted.plot.setSeriesVisible(1, false);
            (void)counted.plot.pointCount();
        });

        // Sixty-three lines still drawn, every one of them already held.
        // Hiding costs nothing, and -- the point -- it does not re-read the
        // ones that stayed.
        CHECK(counted.plot.seriesCount() == 63);
        CHECK(cost.crossings == 0);
        CHECK(cost.reads == 0);
    }

    SECTION("a line hidden and shown again without a draw between costs nothing")
    {
        (void)counted.plot.pointCount();
        Counted::settleAll();

        const auto cost = counted.measure([&] {
            counted.plot.setSeriesVisible(0, false);
            counted.plot.setSeriesVisible(0, true);
            (void)counted.plot.pointCount();
        });

        // The cache is pruned when the drawn set is next read, not when it is
        // edited, so a line that left and came back before anything was drawn
        // never left at all. Ticking a box twice is not a read.
        CHECK(cost.crossings == 0);
        CHECK(cost.reads == 0);
    }

    SECTION("a line hidden, drawn without, and shown again is read once")
    {
        (void)counted.plot.pointCount();
        Counted::settleAll();
        counted.plot.setSeriesVisible(0, false);
        (void)counted.plot.pointCount(); // the draw that prunes it
        Counted::settleAll();

        const auto cost = counted.measure([&] {
            counted.plot.setSeriesVisible(0, true);
            (void)counted.plot.pointCount();
        });

        // One line, and one crossing -- not one per line still drawn. The
        // cache exists so that showing a sixty-fourth line does not re-read
        // the sixty-three beside it.
        CHECK(cost.crossings == 1);
        CHECK(cost.reads == 1);
    }

    SECTION("asking the plot four questions reads once")
    {
        const auto cost = counted.measure([&] {
            (void)counted.plot.pointCount();
            (void)counted.plot.minimum();
            (void)counted.plot.maximum();
            (void)counted.plot.hasData();
            (void)counted.plot.thinned();
        });

        CHECK(cost.crossings == 1);
        CHECK(cost.reads == 64);
    }

    SECTION("a line longer than the plot can draw is thinned, not truncated")
    {
        Counted wide({2, 100000});
        const auto cost = wide.measure([&] { (void)wide.plot.pointCount(); });

        CHECK(cost.crossings == 1);
        // Two lines, each read as runs capped at kReadRun rather than whole.
        CHECK(wide.plot.pointCount() <= 2048);
        CHECK(cost.elements <= 2 * 100000);
    }
}

// ---------------------------------------------------------------------------
// The image: every plane in one crossing
// ---------------------------------------------------------------------------

TEST_CASE("the image reads every plane in one crossing", "[cost][image]")
{
    Counted counted({64, 64, 4});

    SECTION("grayscale is one plane")
    {
        const auto cost = counted.measure([&] { (void)counted.image.hasData(); });

        CHECK(counted.image.colorMode() == DatasetImage::ColorMode::Grayscale);
        CHECK(cost.crossings == 1);
    }

    SECTION("three planes still cross once")
    {
        counted.image.setChannelDimension(2);
        counted.image.setColorMode(DatasetImage::ColorMode::Rgb);
        Counted::settleAll();

        const auto cost = counted.measure([&] { (void)counted.image.hasData(); });

        CHECK(counted.image.colorMode() == DatasetImage::ColorMode::Rgb);
        CHECK(cost.crossings == 1);
    }

    SECTION("and so do four")
    {
        counted.image.setChannelDimension(2);
        counted.image.setColorMode(DatasetImage::ColorMode::Rgba);
        Counted::settleAll();

        const auto cost = counted.measure([&] { (void)counted.image.hasData(); });

        CHECK(counted.image.colorMode() == DatasetImage::ColorMode::Rgba);
        CHECK(cost.crossings == 1);
    }

    SECTION("recolouring re-reads nothing")
    {
        (void)counted.image.hasData();
        Counted::settleAll();

        const auto cost = counted.measure([&] {
            counted.image.setRampBegin(0.2);
            counted.image.setRampEnd(0.8);
            counted.image.setRampName(QStringLiteral("viridis"));
            (void)counted.image.render();
        });

        // The values already sampled land on different colours. Nothing about
        // which values they are has changed, so nothing is read.
        CHECK(cost.crossings == 0);
        CHECK(cost.reads == 0);
    }

    SECTION("asking the image its size does not read it twice")
    {
        const auto cost = counted.measure([&] {
            (void)counted.image.width();
            (void)counted.image.height();
            (void)counted.image.minimum();
            (void)counted.image.maximum();
            (void)counted.image.thinned();
            (void)counted.image.render();
        });

        CHECK(cost.crossings == 1);
    }

    SECTION("a raster larger than the cap is thinned rather than read whole")
    {
        Counted huge({4096, 4096});
        const auto cost = huge.measure([&] { (void)huge.image.hasData(); });

        CHECK(cost.crossings == 1);
        // kMaxExtent is 1024 a side, so at most a millionth-scale sample of
        // the sixteen million elements -- and never all of them.
        CHECK(huge.image.width() <= 1024);
        CHECK(huge.image.height() <= 1024);
        CHECK(cost.elements < hsize_t{4096} * 4096);
    }
}

// ---------------------------------------------------------------------------
// Postprocessing: the slice is the read
// ---------------------------------------------------------------------------

TEST_CASE("a pipeline reads the slice it names and nothing else", "[cost][postproc]")
{
    const CountingSource source({100, 100});

    SECTION("a contiguous slice is one hyperslab")
    {
        const auto result = postproc::run(
            source, {{postproc::OperationKind::Slice, QStringLiteral("0:10, 0:10")}}, 1);

        REQUIRE(result.usable());
        CHECK(source.reads() == 1);
        CHECK(source.elementsRead() == 100);
    }

    SECTION("a strided slice is one read per run")
    {
        const auto result = postproc::run(
            source, {{postproc::OperationKind::Slice, QStringLiteral("0:4, ::2")}}, 1);

        REQUIRE(result.usable());
        // Four rows, and along the last dimension no two chosen indices are
        // consecutive, so each is its own hyperslab.
        CHECK(source.reads() == 4 * 50);
        CHECK(source.elementsRead() == 4 * 50);
    }

    SECTION("a selection above the cap is refused before anything is read")
    {
        const CountingSource enormous({8192, 8192});
        const auto result = postproc::run(
            enormous, {{postproc::OperationKind::Slice, QStringLiteral("...")}}, 1);

        CHECK_FALSE(result.usable());
        CHECK(enormous.reads() == 0);
    }

    SECTION("a step that only rearranges reads nothing further")
    {
        const auto sliced = postproc::run(
            source, {{postproc::OperationKind::Slice, QStringLiteral("0:10, 0:10")}}, 1);
        const int afterSlice = source.reads();

        const auto transposed = postproc::run(
            source,
            {{postproc::OperationKind::Slice, QStringLiteral("0:10, 0:10")},
             {postproc::OperationKind::Transpose, QString{}}},
            2);

        REQUIRE(sliced.usable());
        REQUIRE(transposed.usable());
        // The second run reads its slice once, as the first did. A transpose
        // on top of it is arithmetic over strides and reads nothing at all.
        CHECK(source.reads() == afterSlice * 2);
    }
}

// ---------------------------------------------------------------------------
// The tree: what a listing does not do
// ---------------------------------------------------------------------------

namespace {

/// A file whose shape is the one a tree gets wrong: one very wide group, and a
/// level of groups that each hold many members.
///
/// Small enough to write in a fraction of a second -- what is being asserted is
/// that the cost does not scale with it, and that is as visible at a thousand
/// members as at eight thousand.
struct WideFile {
    h5test::TempFile temp{"cost"};
    gui::AppController controller;

    WideFile()
    {
        // The scale file's own generator, at a size that writes in a moment.
        // What is asserted below is that the cost does *not* grow with these
        // numbers, and that is as visible at a thousand members as at eight
        // thousand -- so the suite pays for a thousand.
        h5example::ScaleSpec spec;
        spec.runs = 2;
        spec.detectorsPerRun = 1;
        spec.channelsPerDetector = 2;
        spec.settingsPerRun = 2;
        spec.flatChildren = 1024;
        spec.sessions = 32;
        spec.framesPerSession = 64;
        spec.frames = 1;
        spec.rows = 4;
        spec.columns = 4;
        h5example::writeScaleFile(temp.path(), spec);

        REQUIRE(h5test::openFileAndSettle(controller,
                                          QString::fromStdString(temp.path())));
    }

    [[nodiscard]] gui::H5TreeModel* tree() const
    {
        return qobject_cast<gui::H5TreeModel*>(controller.treeModel());
    }

    [[nodiscard]] gui::TreeFilterProxyModel* proxy() const
    {
        return qobject_cast<gui::TreeFilterProxyModel*>(
            controller.filteredTreeModel());
    }
};

/// How many of a parent's rows have had their object header read.
int resolvedChildren(gui::H5TreeModel* tree, const QModelIndex& parent)
{
    int resolved = 0;
    const int rows = tree->rowCount(parent);
    for (int row = 0; row < rows; ++row) {
        if (tree->data(tree->index(row, 0, parent), gui::H5TreeModel::IsResolvedRole)
                .toBool()) {
            ++resolved;
        }
    }
    return resolved;
}

} // namespace

TEST_CASE("listing a group does not open what is in it", "[cost][tree]")
{
    WideFile file;
    gui::H5TreeModel* tree = file.tree();
    REQUIRE(tree != nullptr);

    const QModelIndex flat = h5test::reveal(*tree, QStringLiteral("/flat"));
    REQUIRE(flat.isValid());
    const int members = h5test::settledRowCount(tree, flat);
    REQUIRE(members == 1024);

    SECTION("every name comes back and no object header is read")
    {
        // The link table is one traversal of one structure. Following each
        // name to see what it points at is a separate read per link, scattered
        // across the file, and a viewport shows forty of a thousand rows.
        CHECK(resolvedChildren(tree, flat) == 0);
    }

    SECTION("drawing forty rows resolves forty")
    {
        for (int row = 0; row < 40; ++row) {
            (void)tree->data(tree->index(row, 0, flat), gui::H5TreeModel::MetaRole);
        }
        Counted::settleAll();

        // Bounded by the viewport rather than by the group. This is the whole
        // property, and it is the one that stops being true silently.
        CHECK(resolvedChildren(tree, flat) == 40);
    }

    SECTION("one layout pass over forty rows is one crossing")
    {
        const long long before = H5Thread::instance().crossings();
        for (int row = 0; row < 40; ++row) {
            (void)tree->data(tree->index(row, 0, flat), gui::H5TreeModel::MetaRole);
        }
        Counted::settleAll();
        const long long crossings = H5Thread::instance().crossings() - before;

        // The row requests of one frame are batched behind a zero-delay timer,
        // so forty questions are one job. Two would be a batch that had been
        // split; forty would be the batching gone.
        CHECK(crossings <= 2);
    }
}

TEST_CASE("a group's member count does not cost its members", "[cost][tree]")
{
    WideFile file;
    gui::H5TreeModel* tree = file.tree();
    REQUIRE(tree != nullptr);

    const QModelIndex sessions = h5test::reveal(*tree, QStringLiteral("/sessions"));
    REQUIRE(sessions.isValid());
    const int groups = h5test::settledRowCount(tree, sessions);
    REQUIRE(groups == 32);

    // The readout beside a group row says how many members it has. Taken by
    // listing the group, drawing one level of thirty-two groups walks the
    // entire level below it -- which is the quadratic case, and the one no
    // viewport limit rescues, because the work is proportional to the level
    // that is not on screen.
    for (int row = 0; row < groups; ++row) {
        (void)tree->data(tree->index(row, 0, sessions), gui::H5TreeModel::MetaRole);
    }
    Counted::settleAll();

    for (int row = 0; row < groups; ++row) {
        const QModelIndex group = tree->index(row, 0, sessions);
        INFO(tree->pathAt(group).toStdString());
        // The count arrived...
        CHECK(tree->data(group, gui::H5TreeModel::MetaRole).toString()
              == QStringLiteral("64 items"));
        // ...and the group it counted is still unlisted.
        CHECK_FALSE(tree->isPopulated(group));
    }
}

TEST_CASE("choosing an object is one round trip, not eight", "[cost][selection]")
{
    WideFile file;
    const QString dataset =
        QStringLiteral("/runs/run_0000/detectors/det_00/channel_00");
    REQUIRE(h5test::selectAndSettle(file.controller, dataset));

    // Describing a selection wants its kind, its attribute count, its full
    // description, its attributes and the Information tab's panels. Those used
    // to be separate reads made from the GUI thread one after another; they are
    // gathered in a single job now, which is both why choosing an object no
    // longer blocks and why it is no longer eight round trips.
    //
    // The bound rather than an exact number, because the source the views are
    // put on afterwards is a job of its own and a second selection of the same
    // kind may or may not need to reopen the dataset. What is being held down
    // is the order of magnitude: a handful, not one per thing asked about --
    // it measured 2 when this was written.
    const QString other =
        QStringLiteral("/runs/run_0000/detectors/det_00/channel_01");
    const long long before = H5Thread::instance().crossings();
    REQUIRE(h5test::selectAndSettle(file.controller, other));
    const long long crossings = H5Thread::instance().crossings() - before;

    INFO("crossings for one selection: " << crossings);
    CHECK(crossings <= 4);
    CHECK(file.controller.currentPath() == other);
    CHECK(file.controller.datasetTabVisible());
}

TEST_CASE("rearranging the table reads nothing until it is painted",
          "[cost][table]")
{
    Counted counted({64, 64, 8});

    // The panel is the authority on what the table shows, and telling the table
    // is arithmetic over the layout: which indices land on which axis. Not one
    // element of it is read until something asks for a cell, which is what makes
    // dragging a dimension between the axes immediate on a dataset far too
    // large to draw.
    gui::TableLayout layout = counted.table.layout();
    const auto cost = counted.measure([&] {
        layout.onX[0] = true;
        layout.onX[2] = false;
        counted.table.setLayout(layout);
    });

    CHECK(cost.crossings == 0);
    CHECK(cost.reads == 0);

    // ...and then the first paint after it reads the block, once.
    const auto painted = counted.measure([&] { counted.paint(0, 0); });
    CHECK(painted.crossings == 1);
}

TEST_CASE("the colour ramp's extent is sampled once per table", "[cost][table]")
{
    Counted counted({500, 500});

    const auto first = counted.measure([&] { (void)counted.table.valueExtent(); });
    CHECK(first.crossings == 1);

    // Held until the table changes underneath, so turning the fill on costs one
    // read and scrolling costs none. A ramp recomputed from whatever is on
    // screen would change a cell's colour as the reader scrolled past it, which
    // is the one thing a colour that means a value must not do.
    const auto again = counted.measure([&] {
        for (int i = 0; i < 10; ++i) {
            (void)counted.table.valueExtent();
        }
    });
    CHECK(again.crossings == 0);
    CHECK(again.reads == 0);

    // A different table is a different extent, and the old one would be reading
    // the new numbers on the old scale.
    gui::TableLayout layout = counted.table.layout();
    layout.indices[0] = {0, 1, 2, 3};
    counted.table.setLayout(layout);
    Counted::settleAll();

    const auto rebuilt = counted.measure([&] { (void)counted.table.valueExtent(); });
    CHECK(rebuilt.crossings == 1);
}

TEST_CASE("the filter does not read what nobody has expanded", "[cost][tree]")
{
    WideFile file;
    gui::H5TreeModel* tree = file.tree();
    REQUIRE(tree != nullptr);
    REQUIRE(h5test::settledRowCount(tree) > 0);

    gui::TreeFilterProxyModel* proxy = file.proxy();
    REQUIRE(proxy != nullptr);

    const long long before = H5Thread::instance().crossings();
    proxy->setFilterText(QStringLiteral("frame"));
    (void)proxy->rowCount({});
    Counted::settleAll();
    const long long crossings = H5Thread::instance().crossings() - before;

    // Typing in the filter box must not walk the file. QSortFilterProxyModel's
    // own recursive filtering asks every candidate parent for its children,
    // and in this model asking is what reads them.
    CHECK(crossings == 0);
}
