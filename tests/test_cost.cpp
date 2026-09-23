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
#include "gui/PlotBudget.hpp"
#include "gui/PlotItem.hpp"
#include "gui/PlotLevels.hpp"
#include "gui/TableLayout.hpp"
#include "gui/TreeFilterProxyModel.hpp"
#include "postproc/Pipeline.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QModelIndex>
#include <QSignalSpy>
#include <QString>

#include <algorithm>
#include <cmath>
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
struct Counted
{
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
    struct Cost
    {
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
        return {H5Thread::instance().crossings() - before, source->reads(), source->elementsRead()};
    }

    /// Ask the grid for one cell, as a delegate painting it would.
    void paint(int row, int column) { (void)table.data(table.index(row, column), Qt::DisplayRole); }

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

TEST_CASE("the grid reads a block at a time, whatever it is asked for", "[cost][table]")
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
        const auto cost = counted.measure([&] { (void)counted.table.widestCell(0, 900, 0, 900); });

        CHECK(cost.reads == 0);
    }
}

TEST_CASE("a table read as one line is read as one hyperslab", "[cost][table]")
{
    Counted counted({4, 1000});

    SECTION("consecutive columns coalesce into one read per row")
    {
        const auto cost =
            counted.measure([&] { (void)counted.table.sampleValues(0, -1, 8, 0, -1, 4096); });

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

        const auto cost =
            counted.measure([&] { (void)counted.table.sampleValues(0, -1, 8, 0, -1, 4096); });

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
        CHECK(wide.plot.pointCount() <= DatasetPlot::kMaxPoints);

        // Every element of both lines, and not one more.
        //
        // That is what an envelope costs and it is the number worth watching:
        // the plot used to read one element per drawn point and miss any spike
        // that fell between two of them. It reads the whole of each bucket now
        // and reports its extremes, in the same number of round trips -- the
        // reads below are one per bucket either way, and what changed is how
        // much each of them moves.
        CHECK(cost.elements == 2 * 100000);
        CHECK(cost.reads <= 2 * DatasetPlot::kMaxPoints);
    }

    SECTION("zooming and panning read nothing at all")
    {
        // What keeps a drag smooth, stated as a count rather than as a
        // duration. The window onto the data is the renderer's business and
        // the samples are already in hand, so moving it must not reach the
        // file -- and a change that made it do so would not look like a bug,
        // it would look like the plot had become slow.
        //
        // This is the *renderer's* window, and it is still true of it word for
        // word. The model has a window of its own now -- see the closer-look
        // case below -- and the boundary between them is exactly this: the item
        // never reads, the model reads once the view has stopped moving.
        gui::PlotItem item;
        (void)counted.plot.pointCount();
        Counted::settleAll();
        counted.plot.fill(&item);
        REQUIRE(item.lineCount() == DatasetPlot::initialSeriesLimit());

        const auto cost = counted.measure([&] {
            for (int step = 0; step < 100; ++step) {
                item.setXMin(static_cast<double>(step));
                item.setXMax(200.0 + static_cast<double>(step) * 0.5);
                item.setYMin(-static_cast<double>(step));
                item.setYMax(static_cast<double>(step));
            }
        });

        CHECK(cost.crossings == 0);
        CHECK(cost.reads == 0);
        CHECK(cost.elements == 0);
    }

    SECTION("a closer look is folded out of the line, not read again")
    {
        // The whole point of the closer look: zooming in resolves detail
        // instead of stretching a summary. It used to cost a read to do it --
        // one run, one crossing, a settle's wait -- and this section counted
        // that read and held it to exactly one.
        //
        // It is now none. The whole-line read keeps the elements it already
        // touched, at the finest bucket the budget affords, so a closer look is
        // a fold of a buffer in hand rather than a second question to the file.
        // Zero is the tighter bound and the one worth asserting: a read
        // reappearing here would not look like a bug, it would look like the
        // plot had gone back to being slow.
        Counted wide({1, 1000000});
        (void)wide.plot.pointCount(); // the whole line, and the pyramid under it
        Counted::settleAll();

        const auto cost = wide.measure([&] { wide.plot.setVisibleRange(0.0, 10000.0); });

        CHECK(cost.crossings == 0);
        CHECK(cost.reads == 0);
        CHECK(cost.elements == 0);

        // ...and it is drawn at the finer bucket in the same call, with no
        // settle waited out and no round trip. This is the assertion the count
        // above is only half of: nothing read *and* the picture resolved.
        CHECK(wide.plot.pointCount() > 2 * 1024);

        // Then the octaves read ahead, which are also folds now, and then
        // nothing. What must still hold is that it stops: a ladder that went on
        // asking for ever finer runs of a line with no more to give would be a
        // loop rather than a wait.
        const auto ahead = wide.measure([&] { h5test::settleFor(900); });
        CHECK(ahead.crossings == 0);
        CHECK(ahead.reads == 0);

        const auto quiet = wide.measure([&] { h5test::settleFor(500); });
        CHECK(quiet.crossings == 0);
        CHECK(quiet.reads == 0);
    }

    SECTION("a gesture in flight reads nothing")
    {
        Counted wide({1, 1000000});
        (void)wide.plot.pointCount();
        Counted::settleAll();

        // Sixty pushes of the view, as a wheel spun through six octaves or a
        // drag across the pane. Not one of them may reach the file: the run is
        // only worth reading once the reader has stopped somewhere.
        const auto cost = wide.measure([&] {
            for (int step = 0; step < 60; ++step) {
                const double span = 400000.0 / (1.0 + static_cast<double>(step));
                wide.plot.setVisibleRange(500000.0 - span, 500000.0 + span);
            }
        });

        CHECK(cost.crossings == 0);
        CHECK(cost.reads == 0);
    }

    SECTION("a gesture that says where it is going is answered without reading")
    {
        // The other half of the section above, and the reason it is still true
        // rather than merely still passing: a drag has nowhere it is heading,
        // so it waits. A *zoom* does, and waiting was costing the reader a
        // tenth of a second at the end of every gesture for a picture that
        // could have been arriving while they span the wheel.
        //
        // Dropping the wait was the first half of that and this is the second:
        // there is nothing left to wait for. Sixty notches through six octaves
        // are sixty folds of a buffer already in hand, so the count that used
        // to be "a handful rather than sixty" is now none at all -- and the
        // picture is right at every one of them rather than at the end.
        Counted wide({1, 1000000});
        (void)wide.plot.pointCount();
        Counted::settleAll();

        const auto cost = wide.measure([&] {
            // Sixty notches in, about a point a fifth of the way across the
            // pane -- the corner case, not the middle, because the middle is
            // what the arithmetic used to assume.
            double low = 100000.0;
            double high = 900000.0;
            const double focus = low + 0.2 * (high - low);
            for (int step = 0; step < 60; ++step) {
                wide.plot.setZoomFocus(focus, 1.25);
                low = focus - (focus - low) / 1.25;
                high = focus + (high - focus) / 1.25;
                wide.plot.setVisibleRange(low, high);
                // Asked in the same turn the range was pushed in, which is what
                // a frame does.
                (void)wide.plot.pointCount();
            }
        });

        CHECK(cost.crossings == 0);
        CHECK(cost.reads == 0);
        CHECK(cost.elements == 0);

        // Sixty notches is six octaves and more, so the end of the spin is the
        // line itself, sample for sample -- reached without a single read. That
        // is the claim: not that the last picture is right, which the whole-line
        // summary could have managed, but that the whole descent to it was
        // answered out of one pass over the file.
        //
        // What each of those frames actually drew is asserted in "a zoom from
        // the whole line to a single sample" below, value for value against the
        // elements themselves. A count cannot see a cache that is fast and
        // wrong.
        CHECK_FALSE(wide.plot.thinned());

        // And it goes quiet, which is what it always had to do.
        h5test::settleFor(1500);
        Counted::settleAll();
        const auto quiet = wide.measure([&] { h5test::settleFor(500); });
        CHECK(quiet.crossings == 0);
        CHECK(quiet.reads == 0);

        // ...and a pan afterwards is a pan: no focus, so the settle is back.
        wide.plot.clearZoomFocus();
        const auto dragging = wide.measure([&] {
            for (int step = 0; step < 20; ++step) {
                const double at = 200000.0 + 1000.0 * static_cast<double>(step);
                wide.plot.setVisibleRange(at, at + 50000.0);
            }
        });
        CHECK(dragging.crossings == 0);
        CHECK(dragging.reads == 0);
    }

    SECTION("panning inside the run in hand reads nothing")
    {
        Counted wide({1, 1000000});
        (void)wide.plot.pointCount();
        wide.plot.setVisibleRange(0.0, 10000.0);
        // Long enough for the octaves read ahead of the reader as well, so what
        // is counted below is the pan and not the prefetch finishing.
        h5test::settleFor(1200);
        Counted::settleAll();

        // A run is twice the pane and steps by a quarter of itself, so a pan of
        // most of a screenful is still the same run -- which is what the
        // alignment buys, stated in the only units that matter.
        const auto cost = wide.measure([&] {
            wide.plot.setVisibleRange(4000.0, 14000.0);
            wide.plot.setVisibleRange(8000.0, 18000.0);
            h5test::settleFor(300);
        });

        CHECK(cost.crossings == 0);
        CHECK(cost.reads == 0);
    }

    SECTION("zooming back out reads nothing")
    {
        Counted wide({1, 1000000});
        (void)wide.plot.pointCount();
        wide.plot.setVisibleRange(0.0, 10000.0);
        h5test::settleFor(1200);
        Counted::settleAll();

        // The whole-line summary was never thrown away, so going back to it is
        // a draw and not a read. That is the reason the runs sit beside it
        // rather than replacing it.
        const auto cost = wide.measure([&] {
            wide.plot.setVisibleRange(0.0, 1000000.0);
            h5test::settleFor(300);
        });

        CHECK(cost.crossings == 0);
        CHECK(cost.reads == 0);
        CHECK(wide.plot.pointCount() == 2 * 1024);
    }

    SECTION("the next octave in is already in hand")
    {
        // The prefetch, counted. A run used to be read one octave finer than
        // the pane needed -- the cheapest read there was, because a run costs
        // its span and not its resolution -- so that the step down landed on
        // detail that had come with it.
        //
        // The pyramid generalises that from one octave to all of them. There is
        // no "arriving" half any more: the run the pane is on and the run one
        // step in are both folds of the same held line, so both halves of this
        // are zero and the octave that used to be a bargain is now free.
        Counted wide({1, 1000000});
        (void)wide.plot.pointCount();
        Counted::settleAll();

        const auto arriving = wide.measure([&] {
            wide.plot.setVisibleRange(0.0, 10000.0);
            h5test::settleFor(1200);
        });
        CHECK(arriving.crossings == 0);
        CHECK(arriving.reads == 0);

        const auto stepping = wide.measure([&] {
            // Half the span, about the same centre: one octave in.
            wide.plot.setVisibleRange(2500.0, 7500.0);
            h5test::settleFor(1200);
        });
        CHECK(stepping.crossings == 0);
        CHECK(stepping.reads == 0);
        CHECK(stepping.elements == 0);

        // ...and it is drawn at the finer bucket rather than stretched, which
        // is what makes the step worth holding the line for in the first place.
        CHECK(wide.plot.thinned());
        CHECK(wide.plot.pointCount() > 2 * 1024);
    }

    SECTION("the octaves out are already in hand")
    {
        // The direction that used to flicker. Zooming out past the run in hand
        // fell back to the whole-line summary -- correct, and as many octaves
        // too coarse as the reader was zoomed in -- and then sharpened a tenth
        // of a second later when the next run landed. The runs read ahead of
        // them are what removes that: each is four times as wide as the one
        // below it, so a handful of octaves are covered by two of them.
        Counted wide({1, 1000000});
        (void)wide.plot.pointCount();
        const double coarse = wide.plot.lineOf(0).positionStep;

        // Somewhere in the middle of the line, so that nothing is answered by
        // the run simply running into the end of the data.
        wide.plot.setVisibleRange(400000.0, 410000.0);
        h5test::settleFor(1200);
        Counted::settleAll();
        const double close = wide.plot.lineOf(0).positionStep;
        REQUIRE(close < coarse);

        // Three octaves out, one at a time. None of them reads, and none of
        // them falls back to the whole-line summary.
        double low = 400000.0;
        double high = 410000.0;
        for (int octave = 1; octave <= 3; ++octave) {
            const double centre = (low + high) / 2.0;
            const double half = (high - low);
            low = centre - half;
            high = centre + half;

            // The gesture itself: nothing is read, and the frame after it is
            // already drawn from a run rather than from the whole-line summary.
            // That second half is the whole point -- the flicker was never a
            // read, it was the picture coarsening while one was on its way.
            const auto gesture = wide.measure([&] { wide.plot.setVisibleRange(low, high); });
            CHECK(gesture.crossings == 0);
            CHECK(gesture.reads == 0);
            CHECK(wide.plot.lineOf(0).positionStep < coarse);

            // Behind it the prefetch may take another octave, which the reader
            // never waits for: the picture above was right before it started.
            h5test::settleFor(400);
            Counted::settleAll();
            CHECK(wide.plot.lineOf(0).positionStep < coarse);
        }
    }

    SECTION("a thousand lines drawn at once read no closer look at all")
    {
        // Past a few hundred strokes over one another the picture is a
        // distribution rather than a line. Re-reading every one of them on
        // every settled zoom would spend the whole cost of the selection again
        // to sharpen something nobody can follow, so the closer look stops
        // being offered and the lines stretch as they always did.
        Counted many({1000, 100000});
        many.plot.selectAll();
        (void)many.plot.pointCount();
        Counted::settleAll();
        REQUIRE(many.plot.seriesCount() == 1000);

        const auto cost = many.measure([&] {
            many.plot.setVisibleRange(0.0, 1000.0);
            h5test::settleFor(300);
        });

        CHECK(cost.crossings == 0);
        CHECK(cost.reads == 0);
    }

    SECTION("a wider pane refolds the same elements into more buckets")
    {
        // What a line is thinned to follows the pane -- a bucket is a column --
        // so a wider window asks for more buckets. What it must not do is read
        // more of the file, or make more round trips for them.
        //
        // It used to re-read the whole line at the new bucket, which was
        // defensible while the elements were not kept: an envelope reads every
        // element exactly once either way, so a wider pane cost the same reads
        // as a narrower one and only the fold differed. Now the elements *are*
        // kept, and "only the fold differed" is the whole of what is left --
        // so a resize costs nothing at all and the bucket counts are the only
        // thing that changes.
        Counted wide({1, 1000000});

        // The first pass, measured: this is where the file is read, and the
        // count that used to be asserted of every resize belongs here now.
        const auto first = wide.measure([&] {
            (void)wide.plot.pointCount();
            Counted::settleAll();
        });
        // The whole line, exactly once. Not "about the whole line": a read is
        // cut back to a whole number of buckets precisely so that no element is
        // read twice at the seam between two of them.
        CHECK(first.elements == 1000000);
        // Sixteen reads of sixty-five thousand cover a million, and cutting
        // each of them back to a whole number of buckets can cost one more.
        // This is the number that used to follow the *bucket* count -- one read
        // per bucket -- and it is where the cost of the legend's `all` on a
        // ten-thousand-line table was.
        CHECK(first.reads >= 1000000 / 65536);
        CHECK(first.reads <= 2 + 1000000 / 65536);
        CHECK(first.crossings == 1);

        const auto resize = [&](int columns) {
            return wide.measure([&] {
                wide.plot.setPaneColumns(columns);
                h5test::settleFor(DatasetPlot::kResizeMilliseconds + 200);
                (void)wide.plot.pointCount();
            });
        };

        const auto narrow = resize(512);
        const int few = wide.plot.pointCount();

        const auto broad = resize(2048);
        const int many = wide.plot.pointCount();

        // The bucket is a ceiling and so is the count of them, so a budget of
        // n buckets is n or a few less -- 1e6 into 2048 is a bucket of 489,
        // which is 2045 of them. Written as the arithmetic rather than as the
        // answer, because the answer is not the round number and pretending it
        // is would be a test that agrees with a comment instead of with a read.
        const auto bucketsFor = [](int budget) {
            const int stride = (1000000 + budget - 1) / budget;
            return (1000000 + stride - 1) / stride;
        };
        CHECK(few == 2 * bucketsFor(512));
        CHECK(many == 2 * bucketsFor(2048));

        // And neither of them touched the file. A window dragged across a dozen
        // column quanta is a dozen folds now, where it was a dozen readings of
        // a million elements.
        CHECK(narrow.reads == 0);
        CHECK(broad.reads == 0);
        CHECK(narrow.crossings == 0);
        CHECK(broad.crossings == 0);
    }

    SECTION("a bucket of eight elements is not a read of eight elements")
    {
        // The shape of the legend's `all`, in miniature: many lines, so few
        // points each, so a small bucket. What must not happen is a round trip
        // per bucket -- the reads follow the length of the line, not the number
        // of answers it is folded into.
        Counted many({64, 4096});
        many.plot.selectAll();
        const auto cost = many.measure([&] {
            (void)many.plot.pointCount();
            Counted::settleAll();
        });
        REQUIRE(many.plot.seriesCount() == 64);
        CHECK(cost.elements == 64 * 4096);
        // One read a line, because a line is four thousand elements and a read
        // carries sixty-five thousand. One per bucket would be sixty-four
        // times that.
        CHECK(cost.reads <= 2 * 64);
    }

    SECTION("a pane being dragged reads nothing at all")
    {
        // The counted form of "the plot must not flicker under the reader's
        // hand". A window dragged from narrow to wide crosses a column quantum
        // every sixty-four pixels, and each crossing used to be every drawn
        // line read again -- so a drag was dozens of reads of the whole file
        // and dozens of blank frames, to arrive at a width the reader had not
        // chosen yet.
        Counted wide({1, 1000000});
        // The surface measuring itself for the first time is not a gesture and
        // does not wait -- see DatasetPlot::setPaneColumns -- so the pane is
        // measured once here and the drag below is a drag.
        wide.plot.setPaneColumns(512);
        (void)wide.plot.pointCount();
        Counted::settleAll();

        const auto dragging = wide.measure([&] {
            for (int columns = 576; columns <= 2048; columns += 64) {
                wide.plot.setPaneColumns(columns);
                (void)wide.plot.pointCount();
            }
        });
        CHECK(dragging.crossings == 0);
        CHECK(dragging.reads == 0);
        CHECK(dragging.elements == 0);

        // ...and the moment it stops, still nothing: the new width is a
        // different fold of elements already held, so what the debounce used to
        // be protecting -- one read of the whole file at the end of the drag
        // instead of one per quantum during it -- has no read left to defer.
        const auto settled = wide.measure([&] {
            h5test::settleFor(DatasetPlot::kResizeMilliseconds + 200);
            (void)wide.plot.pointCount();
        });
        CHECK(settled.crossings == 0);
        CHECK(settled.elements == 0);
        // The pane is drawn at the width it was dragged to, which is the half
        // of this that a count of zero would otherwise be happy to lie about.
        CHECK(wide.plot.pointCount() == 2 * ((1000000 + 488) / 489));
    }

    SECTION("a selection of thousands holds fewer points in each line")
    {
        // kMaxPoints each was right while a selection was sixty-four lines.
        // `all` on ten thousand would be a hundred and sixty megabytes held and
        // twenty million doubles walked on every frame of a drag, to draw lines
        // the renderer then summarises to about a hundred points each anyway.
        Counted many({10000, 4096});
        (void)many.plot.pointCount();
        Counted::settleAll();
        const int few = many.plot.pointCount();

        many.plot.selectAll();
        (void)many.plot.pointCount();
        Counted::settleAll();

        CHECK(many.plot.seriesCount() == 10000);
        // Two values a bucket and a bucket a column, at the width a pane is
        // assumed to have until the surface measures itself.
        CHECK(few == 2 * DatasetPlot::kDefaultColumns);
        CHECK(many.plot.pointCount() <= DatasetPlot::kMinPoints);
        // ...and it is still an envelope, so nothing has been skipped over.
        CHECK(many.plot.thinned());
    }
}

// ---------------------------------------------------------------------------
// The whole descent, at the rate a hand moves
// ---------------------------------------------------------------------------
//
// The benchmark this was all for: a reader puts the pointer on one exact
// position of a ten-million-element line and zooms from the whole of it down to
// a single sample per column, in twenty frames -- a tenth of a second at two
// hundred a second, which is one pinch on a trackpad.
//
// Each of those frames has to show the *right* data at its own level of detail,
// and that is the half a cost test cannot see: a cache that is fast and subtly
// wrong counts exactly like one that works. So every frame is checked against
// the elements themselves here, and the milliseconds are tools/bench-zoom's to
// print. This asserts what makes the speed possible and what makes it correct;
// it does not assert a duration, because a duration measures the machine.
//
// Twenty reads of runs the reader has already left, on a thread that runs one
// job after another, is what this used to be. It is now none.

namespace {

/// How many frames the gesture is drawn in, and how fine it ends.
constexpr int kZoomFrames = 20;
constexpr long long kZoomLine = 10000000;
constexpr int kZoomColumns = 2048;

/// Check one drawn line against the elements it claims to summarise.
///
/// CountingSource answers a cell with its own row-major position, so a line of
/// `{1, N}` is `f(i) = i` -- which makes every drawn value predictable to the
/// last digit rather than approximately right. Two rules pin the whole run:
///
///   - an envelope bucket holds `[p, p + bucket)` and answers with its two
///     extremes in occurrence order, so on a rising line those are `p` and
///     `p + bucket - 1`;
///   - the two sit half a bucket apart, so the *even* sample of each pair sits
///     at the bucket's own start -- and its value is therefore exactly its own
///     axis position.
///
/// Both hold unchanged for a run drawn sample for sample, where the step is one
/// and each element is its own bucket. So one rule covers every resolution, and
/// an off-by-one in the alignment, the bucket, the offset or the order of the
/// pair fails it immediately.
void drawnMatchesTheFile(const gui::PlotLine& line, long long length)
{
    REQUIRE(line.values != nullptr);
    REQUIRE(line.count > 0);
    const double step = line.positionStep;
    for (qsizetype i = 0; i < line.count; ++i) {
        const double at = line.positionStart + static_cast<double>(i) * step;
        // The far end of the bucket, clipped by the end of the line -- the last
        // bucket of a run that reaches it is summarised from what there is.
        const double want =
            (i % 2 == 0) ? at : std::min(at + step - 1.0, static_cast<double>(length - 1));
        INFO("sample " << i << " of " << line.count << " at " << at << " step " << step);
        REQUIRE(line.values[i] == want);
    }
}

} // namespace

TEST_CASE("a zoom from the whole line to a single sample reads nothing", "[cost][plot][zoom]")
{
    Counted big({1, static_cast<hsize_t>(kZoomLine)});
    big.plot.setPaneColumns(kZoomColumns);
    (void)big.plot.pointCount(); // the one pass over the file
    Counted::settleAll();

    REQUIRE(big.plot.seriesCount() == 1);

    // One exact, well-known position: the middle of the record. A zoom holds
    // the pointer still and brings both edges in towards it, which is what
    // PlotSurface.zoomedAxis does and what setZoomFocus says is happening.
    const double focus = 4999999.0;
    double low = 0.0;
    double high = static_cast<double>(kZoomLine);
    // Twenty frames from the whole line to one sample a column.
    const double shrink =
        std::pow(static_cast<double>(kZoomColumns) / static_cast<double>(kZoomLine),
                 1.0 / static_cast<double>(kZoomFrames));

    int checked = 0;
    int resolved = 0;
    const auto cost = big.measure([&] {
        for (int frame = 0; frame < kZoomFrames; ++frame) {
            big.plot.setZoomFocus(focus, 1.0 / shrink);
            low = focus - (focus - low) * shrink;
            high = focus + (high - focus) * shrink;
            big.plot.setVisibleRange(low, high);

            // Drawn in the same turn the range was pushed in. No settle, no
            // event loop, no waiting on a thread: this is what a frame does.
            const gui::PlotLine line = big.plot.lineOf(0);
            drawnMatchesTheFile(line, kZoomLine);

            // ...and at the resolution the frame asked for. A bucket is a
            // column, so the run on screen carries at least one drawn station
            // per pane column -- fewer than that is a summary stretched over
            // the pane rather than the detail the reader zoomed in for, which
            // is the failure this whole arrangement exists to remove.
            //
            // One rather than the two an envelope aims for, because the bucket
            // is rounded up to a power of two: a span a hair over an octave
            // boundary is drawn at the next bucket up and loses half its
            // stations, which is deliberate -- an arbitrary bucket would
            // reshuffle which samples each column held on every pixel of zoom.
            //
            // Asked only while there is a closer look to have. The first frame
            // or two are still showing half the dataset, where the whole-line
            // summary is the finest reading of it there is -- it is one bucket
            // per column of the *whole* line, so a view of part of it holds
            // that fraction of them and no read could improve on it.
            const double stations = (high - low) / line.positionStep;
            INFO("frame " << frame << " span " << (high - low) << " stations " << stations);
            if (gui::windowFor(low, high, kZoomLine, kZoomColumns).has_value()) {
                CHECK(stations > kZoomColumns - 1.0);
                ++resolved;
            }
            ++checked;
        }
    });

    CHECK(checked == kZoomFrames);
    // All but the opening frames, which are the ones still showing so much of
    // the line that the summary is the whole answer.
    CHECK(resolved >= kZoomFrames - 2);
    // The whole descent, out of one pass over the file.
    CHECK(cost.crossings == 0);
    CHECK(cost.reads == 0);
    CHECK(cost.elements == 0);
    // And it ends on the line itself rather than on a summary of it.
    CHECK_FALSE(big.plot.thinned());

    SECTION("and coming back out reads nothing either")
    {
        // Retracing a zoom is the direction that used to cost most: the runs
        // the reader came in through had been evicted by the ones read ahead of
        // them, so every step out was a read of a wider span than the step in
        // had been. Out of a held line there is nothing to evict and nothing to
        // re-read.
        const auto back = big.measure([&] {
            for (int frame = 0; frame < kZoomFrames; ++frame) {
                big.plot.setZoomFocus(focus, shrink);
                low = focus - (focus - low) / shrink;
                high = focus + (high - focus) / shrink;
                big.plot.setVisibleRange(low, high);
                drawnMatchesTheFile(big.plot.lineOf(0), kZoomLine);
            }
        });
        CHECK(back.crossings == 0);
        CHECK(back.reads == 0);
    }
}

namespace {

/// How many of `line`'s drawn points land in each tenth of a logarithmic pane
/// from `low` to `high`, and how many elements that tenth holds.
///
/// The shape of the failure this is written against: on a logarithmic axis the
/// left of the pane holds few elements per column and the right holds many,
/// and a fold that buckets by element put one or two points in each of the
/// left tenths and thousands in the last. Counted per tenth because that is
/// what a reader sees.
void everyTenthIsDrawn(const gui::PlotLine& line, double low, double high, int columns)
{
    REQUIRE(line.xs != nullptr);
    for (int tenth = 0; tenth < 10; ++tenth) {
        const double from = std::exp2(std::log2(low) + (std::log2(high / low)) * tenth / 10.0);
        const double to = std::exp2(std::log2(low) + (std::log2(high / low)) * (tenth + 1) / 10.0);
        long long drawn = 0;
        for (qsizetype i = 0; i < line.count; ++i) {
            drawn += (line.xs[i] >= from && line.xs[i] < to) ? 1 : 0;
        }
        const double elements = std::ceil(to) - std::ceil(from);
        INFO("tenth " << tenth << " x " << from << ".." << to << " drew " << drawn);
        CHECK(static_cast<double>(drawn) >= std::min(elements, columns / 10.0));
    }
}

/// Check a fold against the elements it claims to summarise, on a line whose
/// element `i` *is* `i` -- see drawnMatchesTheFile. A drawn value is then the
/// index of the element it came from, so it has to lie in the column its point
/// is drawn in: within a column or two of its own x, on the pane's scale.
void foldMatchesTheFile(const gui::PlotLine& line, double low, double high, int columns)
{
    REQUIRE(line.xs != nullptr);
    const double column = std::log2(high / low) / columns; // octaves per column
    for (qsizetype i = 0; i < line.count; ++i) {
        const double value = line.values[i];
        const double x = line.xs[i];
        INFO("point " << i << " at x " << x << " holds " << value);
        REQUIRE(value >= 1.0);
        REQUIRE(value == std::floor(value));
        REQUIRE(std::abs(std::log2(value) - std::log2(x)) <= 2.0 * column + 1e-12);
    }
}

} // namespace

TEST_CASE("a zoom across a logarithmic axis reads nothing and draws every column",
          "[cost][plot][zoom][log]")
{
    // The Plot tab on a logarithmic x axis. The whole-line summary and every
    // run under it are bucketed by element, and on this axis that put the
    // first half of the pane into one bucket at every zoom: blank below the
    // bucket's middle and a few straight strokes above it. What replaces it is
    // a fold per pixel column out of the pyramid already held -- so on top of
    // being right, it has to cost what the linear zoom costs, which is nothing.
    constexpr long long kLine = 1LL << 20;
    constexpr int kColumns = 1024;
    Counted big({1, static_cast<hsize_t>(kLine)});
    big.plot.setPaneColumns(kColumns);
    (void)big.plot.pointCount(); // the one pass over the file
    Counted::settleAll();
    big.plot.setXLog(true);

    // The index axis starts at the first element there is a place for.
    const double low = 1.0;
    const double high = static_cast<double>(kLine);

    for (const double focus : {3.0, 1000.0, 900000.0}) {
        DYNAMIC_SECTION("zooming in at x = " << focus)
        {
            double from = low;
            double to = high;
            int folded = 0;
            const auto cost = big.measure([&] {
                for (int frame = 0; frame < 24; ++frame) {
                    // What PlotSurface.zoomedAxis does on this axis: hold the
                    // pointer's *logarithm* still and bring both edges in
                    // towards it by the same share of the decades.
                    big.plot.setZoomFocus(focus, 1.6);
                    from = std::exp2(std::log2(focus) - (std::log2(focus) - std::log2(from)) / 1.6);
                    to = std::exp2(std::log2(focus) + (std::log2(to) - std::log2(focus)) / 1.6);
                    if (to - from < 16.0) {
                        break; // the surface stops here; see minimumSpanX
                    }
                    big.plot.setVisibleRange(from, to);
                    const gui::PlotLine line = big.plot.lineOf(0);
                    INFO("frame " << frame << " view " << from << ".." << to);
                    if (to / from >= 2.0) {
                        // An octave or more across the pane: the fold.
                        foldMatchesTheFile(line, from, to, kColumns);
                        everyTenthIsDrawn(line, from, to, kColumns);
                        ++folded;
                    }
                    else {
                        // Under an octave, the linear path takes over, and it
                        // is the one the rest of this file already holds.
                        CHECK(line.xs == nullptr);
                        drawnMatchesTheFile(line, kLine);
                    }
                }
            });
            CHECK(folded > 0);
            CHECK(cost.crossings == 0);
            CHECK(cost.reads == 0);
        }
    }

    SECTION("the whole axis, unzoomed, starts at its first element")
    {
        big.plot.setVisibleRange(low, high);
        const gui::PlotLine line = big.plot.lineOf(0);
        REQUIRE(line.xs != nullptr);
        REQUIRE(line.count > 0);
        // Element 1, drawn as itself. It used to be the middle of the first
        // bucket, half a thousand elements along.
        CHECK(line.xs[0] == 1.0);
        CHECK(line.values[0] == 1.0);
        everyTenthIsDrawn(line, low, high, kColumns);
        // Bounded by the pane rather than by the line.
        CHECK(line.count <= 8 * kColumns + 2);
    }

    SECTION("a pan inside the fold neither folds again nor asks to be refilled")
    {
        big.plot.setVisibleRange(10.0, 100000.0);
        (void)big.plot.lineOf(0);
        const gui::PlotLine before = big.plot.lineOf(0);

        QSignalSpy changed(&big.plot, &DatasetPlot::changed);
        // A twentieth of the pane at a time, both ways -- well inside the half
        // pane of margin the fold is made with.
        const double step = std::pow(10000.0, 1.0 / 20.0);
        double at = 1.0;
        for (const double by : {step, step, 1.0 / step, 1.0 / step, 1.0 / step}) {
            at *= by;
            big.plot.setVisibleRange(10.0 * at, 100000.0 * at);
        }
        CHECK(changed.count() == 0);
        // ...and the same buffers are drawn, so nothing was even retired.
        CHECK(big.plot.lineOf(0).values == before.values);
        CHECK(big.plot.retiredDoubles() == 0);
    }

    SECTION("turning the scale off puts the linear picture back")
    {
        big.plot.setXLog(false);
        big.plot.setVisibleRange(0.0, high);
        const gui::PlotLine line = big.plot.lineOf(0);
        CHECK(line.xs == nullptr);
        drawnMatchesTheFile(line, kLine);
    }
}

TEST_CASE("a window dragged past the ends of the line draws what is there and reads nothing",
          "[cost][plot][zoom][log]")
{
    // The view may be dragged past either end of the data, as far as leaves a
    // quarter of the pane on it -- see PlotSurface.panKeep. So the plot is now
    // asked for windows that reach below the first element and beyond the
    // last, on either scale, and has to answer for the part that exists
    // without reading and without inventing anything for the part that does
    // not.
    constexpr long long kLine = 1LL << 20;
    constexpr int kColumns = 1024;
    Counted big({1, static_cast<hsize_t>(kLine)});
    big.plot.setPaneColumns(kColumns);
    (void)big.plot.pointCount();
    Counted::settleAll();

    const auto n = static_cast<double>(kLine);
    const auto cost = big.measure([&] {
        // Linear: three quarters of the pane before the first element, then
        // after the last, at the whole line's width and zoomed in sixteen times.
        for (const double width : {n, n / 16.0}) {
            for (const double from : {-0.75 * width, n - 0.25 * width}) {
                INFO("linear window " << from << ".." << from + width);
                big.plot.setVisibleRange(from, from + width);
                const gui::PlotLine line = big.plot.lineOf(0);
                drawnMatchesTheFile(line, kLine);
            }
        }

        // Logarithmic: the same share of the decades either side.
        big.plot.setXLog(true);
        const double decades = std::log10(n);
        for (const double at : {-0.75 * decades, 0.75 * decades}) {
            const double from = std::pow(10.0, at);
            const double to = std::pow(10.0, at + decades);
            INFO("logarithmic window " << from << ".." << to);
            big.plot.setVisibleRange(from, to);
            const gui::PlotLine line = big.plot.lineOf(0);
            REQUIRE(line.xs != nullptr);
            REQUIRE(line.count > 0);
            for (qsizetype i = 0; i < line.count; ++i) {
                // Nothing drawn outside the line: every point is an element
                // that exists, at an x that one of them has.
                REQUIRE(line.xs[i] >= 0.0);
                REQUIRE(line.xs[i] < n);
                REQUIRE(line.values[i] >= 0.0);
                REQUIRE(line.values[i] < n);
            }
        }
    });
    CHECK(cost.crossings == 0);
    CHECK(cost.reads == 0);
}

TEST_CASE("turning the budget down gives the memory back, and turning it up reads",
          "[cost][plot][zoom]")
{
    // What applyBudget() used to do was trim the run ladder, under a comment
    // saying nothing was re-read. That was true while the only held thing was a
    // handful of runs and became false the moment a whole line was held beside
    // them: a reader who noticed this program holding gigabytes and turned the
    // budget down saw no change at all until they selected another dataset.
    Counted big({1, static_cast<hsize_t>(kZoomLine)});
    big.plot.setPaneColumns(kZoomColumns);
    (void)big.plot.pointCount();
    Counted::settleAll();

    // Pinned rather than taken from the machine: `low` and `greedy` are
    // fractions of physical memory, and on a machine with enough of it both are
    // large enough to hold a ten-million-element line at bucket one -- so the
    // assertion below would pass or fail according to how much RAM the runner
    // has, which is the one thing this suite exists not to do.
    auto& budget = gui::PlotBudget::instance();
    const long long was = budget.pinnedTotal();
    constexpr long long kMegabyte = 1024LL * 1024LL;

    budget.setPinnedTotal(512 * kMegabyte);
    Counted::settleAll();
    const long long roomy = big.plot.heldDoubles();
    REQUIRE(roomy > 0);

    SECTION("down is free, and it is honoured in the call that asks for it")
    {
        const auto cost = big.measure([&] { budget.setPinnedTotal(8 * kMegabyte); });
        CHECK(big.plot.heldDoubles() < roomy);
        // Coarsening an envelope is exact, so nothing is read to do it.
        CHECK(cost.crossings == 0);
        CHECK(cost.reads == 0);

        // ...and what is still held draws the same picture it drew before.
        big.plot.setVisibleRange(0.0, static_cast<double>(kZoomLine));
        drawnMatchesTheFile(big.plot.lineOf(0), kZoomLine);
    }

    SECTION("up is a read, because a finer base is elements it no longer has")
    {
        budget.setPinnedTotal(8 * kMegabyte);
        Counted::settleAll();
        const long long small = big.plot.heldDoubles();
        REQUIRE(small < roomy);

        const auto cost = big.measure([&] { budget.setPinnedTotal(512 * kMegabyte); });
        CHECK(big.plot.heldDoubles() > small);
        CHECK(cost.reads > 0);
        drawnMatchesTheFile(big.plot.lineOf(0), kZoomLine);
    }

    budget.setPinnedTotal(was);
    Counted::settleAll();
}

TEST_CASE("a burst of zoom frames does not pile up the retired store",
          "[cost][plot][zoom]")
{
    // Every frame of a zoom trims a level and retires its vectors, and fill()
    // -- the one moment a renderer that was borrowing them has just been handed
    // something else -- is the only thing that empties them. Several refreshes
    // inside one turn of the event loop therefore accumulate several levels'
    // worth. That is bounded by the ladder, and the bound was reasoned about
    // and never measured; this is the measurement.
    Counted big({1, static_cast<hsize_t>(kZoomLine)});
    big.plot.setPaneColumns(kZoomColumns);
    (void)big.plot.pointCount();
    Counted::settleAll();

    const double focus = 4999999.0;
    double low = 0.0;
    double high = static_cast<double>(kZoomLine);
    const double shrink =
        std::pow(static_cast<double>(kZoomColumns) / static_cast<double>(kZoomLine),
                 1.0 / static_cast<double>(kZoomFrames));

    long long worst = 0;
    for (int frame = 0; frame < kZoomFrames; ++frame) {
        big.plot.setZoomFocus(focus, 1.0 / shrink);
        low = focus - (focus - low) * shrink;
        high = focus + (high - focus) * shrink;
        big.plot.setVisibleRange(low, high);
        worst = std::max(worst, big.plot.retiredDoubles());
    }

    // Nothing has drawn, so nothing has been handed a replacement and nothing
    // has been freed: this is the whole gesture's worth. A run is about
    // 2 * pane columns doubles per line, and the ladder is kHeldLevels deep, so
    // a generous bound is a few ladders' worth -- and what would break it is a
    // retire per frame that nothing ever clears, which is the shape this is
    // here to catch.
    const long long run = 2LL * kZoomColumns;
    INFO("worst " << worst << " doubles, a run is " << run);
    CHECK(worst <= run * gui::kHeldLevels * 4);
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
        const auto result =
            postproc::run(enormous, {{postproc::OperationKind::Slice, QStringLiteral("...")}}, 1);

        CHECK_FALSE(result.usable());
        CHECK(enormous.reads() == 0);
    }

    SECTION("a step that only rearranges reads nothing further")
    {
        const auto sliced = postproc::run(
            source, {{postproc::OperationKind::Slice, QStringLiteral("0:10, 0:10")}}, 1);
        const int afterSlice = source.reads();

        const auto transposed =
            postproc::run(source,
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
struct WideFile
{
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

        REQUIRE(h5test::openFileAndSettle(controller, QString::fromStdString(temp.path())));
    }

    [[nodiscard]] gui::H5TreeModel* tree() const
    {
        return qobject_cast<gui::H5TreeModel*>(controller.treeModel());
    }

    [[nodiscard]] gui::TreeFilterProxyModel* proxy() const
    {
        return qobject_cast<gui::TreeFilterProxyModel*>(controller.filteredTreeModel());
    }
};

/// How many of a parent's rows have had their object header read.
int resolvedChildren(gui::H5TreeModel* tree, const QModelIndex& parent)
{
    int resolved = 0;
    const int rows = tree->rowCount(parent);
    for (int row = 0; row < rows; ++row) {
        if (tree->data(tree->index(row, 0, parent), gui::H5TreeModel::IsResolvedRole).toBool()) {
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
        CHECK(tree->data(group, gui::H5TreeModel::MetaRole).toString() ==
              QStringLiteral("64 items"));
        // ...and the group it counted is still unlisted.
        CHECK_FALSE(tree->isPopulated(group));
    }
}

TEST_CASE("choosing an object is one round trip, not eight", "[cost][selection]")
{
    WideFile file;
    const QString dataset = QStringLiteral("/runs/run_0000/detectors/det_00/channel_00");
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
    const QString other = QStringLiteral("/runs/run_0000/detectors/det_00/channel_01");
    const long long before = H5Thread::instance().crossings();
    REQUIRE(h5test::selectAndSettle(file.controller, other));
    const long long crossings = H5Thread::instance().crossings() - before;

    INFO("crossings for one selection: " << crossings);
    CHECK(crossings <= 4);
    CHECK(file.controller.currentPath() == other);
    CHECK(file.controller.datasetTabVisible());
}

TEST_CASE("rearranging the table reads nothing until it is painted", "[cost][table]")
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
