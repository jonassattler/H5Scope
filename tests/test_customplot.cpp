// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

// The custom plot tabs, headless.
//
// Everything the feature is actually about is here rather than in the QML
// suite: which slices a tab holds, what it refuses and in what words, where
// each point lands along x under each of the three axis modes, and what
// survives a file being swapped underneath it. The QML suite covers the
// chrome -- the strip, the rails, the menus -- and none of this.
//
// The points are asserted through fill(), which is the only place the x
// arithmetic is observable, and it is observable there because that is where
// it belongs: the values never cross into QML, so a bulk replace into a series
// is the whole of the boundary. A QLineSeries is a QObject and needs no graph
// to be filled, which is what lets this suite stay free of a QML engine.

#include "gui/AppController.hpp"
#include "gui/CustomPlot.hpp"
#include "gui/CustomPlotSet.hpp"
#include "gui/DatasetLookup.hpp"
#include "gui/DatasetPlot.hpp"
#include "gui/DatasetTableModel.hpp"
#include "gui/TableSetupModel.hpp"
#include "gui/H5Thread.hpp"
#include "support/AsyncModels.hpp"
#include "support/H5Reader.hpp"
#include "support/TestFile.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <QCoreApplication>
#include <QColor>
#include <QScopeGuard>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtGraphs/QLineSeries>

#include <cmath>

using Catch::Matchers::ContainsSubstring;

namespace {

/// A controller over the shared fixture, with one custom tab already made.
///
/// Reads are coalesced onto a zero-timer -- several edits in one turn of the
/// event loop are one job -- so settling means turning the loop as well as
/// draining the thread, and a few times over: adding a dataset resolves its
/// shape in one crossing and reads its lines in the next.
struct PlotFixture {
    h5test::TempFile temp{"custom"};
    gui::AppController controller;

    PlotFixture()
    {
        h5test::onH5([&] { h5test::writeFixture(temp.path()); });
        REQUIRE(h5test::openFileAndSettle(controller,
                                          QString::fromStdString(temp.path())));
    }

    [[nodiscard]] gui::CustomPlotSet* set() const
    {
        return controller.customPlots();
    }

    /// A fresh tab, settled.
    [[nodiscard]] gui::CustomPlot* tab()
    {
        const int index = set()->addPlot();
        settleAll();
        return set()->plotAt(index);
    }

    static void settleAll(int rounds = 8)
    {
        for (int i = 0; i < rounds; ++i) {
            QCoreApplication::processEvents();
            h5test::settle();
        }
    }

    /// One entry added and read, so a test can go straight to asserting.
    static void add(gui::CustomPlot* plot, const QString& expression)
    {
        REQUIRE(plot->addExpression(expression) >= 0);
        settleAll();
    }

    /// The points fill() puts into a series, which is what the graph draws.
    [[nodiscard]] static QList<QPointF> drawn(gui::CustomPlot* plot, int series)
    {
        QLineSeries line;
        plot->fill(&line, series);
        return line.points();
    }

    [[nodiscard]] static QString errorOf(const gui::CustomPlot* plot, int row)
    {
        return plot->data(plot->index(row, 0), gui::CustomPlot::ErrorRole)
            .toString();
    }
};

} // namespace

TEST_CASE_METHOD(PlotFixture, "a custom plot draws slices of several datasets together",
                 "[custom]")
{
    gui::CustomPlot* plot = tab();
    REQUIRE(plot != nullptr);
    REQUIRE(plot->empty());

    add(plot, QStringLiteral("/series/a[:]"));
    add(plot, QStringLiteral("/series/b[:]"));

    CHECK(plot->sourceSeriesCount() == 2);
    CHECK(plot->seriesCount() == 2);
    CHECK(plot->hasData());
    CHECK(errorOf(plot, 0).isEmpty());
    CHECK(errorOf(plot, 1).isEmpty());

    // series_a is i over 64 samples and series_b is 100 - i, so between them
    // they cover 0 to 100 and hold 128 points.
    CHECK(plot->pointCount() == 128);
    CHECK(plot->minimum() == 0.0);
    CHECK(plot->maximum() == 100.0);
    CHECK(plot->sourcePointCount() == 64);
    CHECK_FALSE(plot->thinned());

    SECTION("the entries keep the names they were written under")
    {
        CHECK(plot->seriesLabel(0) == QStringLiteral("/series/a[:]"));
        CHECK(plot->seriesLabel(1) == QStringLiteral("/series/b[:]"));
    }

    SECTION("hiding one takes it out of the drawn set and out of the extent")
    {
        plot->setSeriesVisible(1, false);
        CHECK(plot->seriesCount() == 1);
        CHECK(plot->drawnSeries().size() == 1);
        CHECK(plot->pointCount() == 64);
        CHECK(plot->maximum() == 63.0);
    }

    SECTION("removing one leaves the other alone")
    {
        plot->removeEntry(0);
        settleAll();
        CHECK(plot->sourceSeriesCount() == 1);
        CHECK(plot->seriesLabel(0) == QStringLiteral("/series/b[:]"));
        CHECK(plot->minimum() == 37.0);
    }
}

TEST_CASE_METHOD(PlotFixture, "an entry has to name one line, and says so when it does not",
                 "[custom]")
{
    gui::CustomPlot* plot = tab();

    SECTION("a block is not a line, and the reason names its shape")
    {
        add(plot, QStringLiteral("/matrix[:, :]"));
        CHECK_THAT(errorOf(plot, 0).toStdString(),
                   ContainsSubstring("is one line"));
        CHECK_THAT(errorOf(plot, 0).toStdString(), ContainsSubstring("4"));
        CHECK_FALSE(plot->hasData());
    }

    SECTION("a single element is not a line either")
    {
        add(plot, QStringLiteral("/matrix[0, 0]"));
        CHECK_THAT(errorOf(plot, 0).toStdString(),
                   ContainsSubstring("one element"));
    }

    SECTION("a path that is not there says that rather than anything else")
    {
        add(plot, QStringLiteral("/nowhere[:]"));
        CHECK_THAT(errorOf(plot, 0).toStdString(),
                   ContainsSubstring("nothing at this path"));
    }

    SECTION("a group holds no values")
    {
        add(plot, QStringLiteral("/group"));
        CHECK_THAT(errorOf(plot, 0).toStdString(), ContainsSubstring("dataset"));
    }

    SECTION("text cannot be plotted")
    {
        add(plot, QStringLiteral("/str_vlen[:]"));
        CHECK_THAT(errorOf(plot, 0).toStdString(),
                   ContainsSubstring("only numbers"));
    }

    SECTION("a scalar has no line in it")
    {
        add(plot, QStringLiteral("/scalar_int"));
        CHECK_THAT(errorOf(plot, 0).toStdString(),
                   ContainsSubstring("single value"));
    }

    SECTION("an unbalanced bracket is reported in the subscript parser's words")
    {
        add(plot, QStringLiteral("/series/a[:"));
        CHECK_THAT(errorOf(plot, 0).toStdString(),
                   ContainsSubstring("never closed"));
    }

    SECTION("a bad entry does not stop a good one being drawn")
    {
        add(plot, QStringLiteral("/matrix[:, :]"));
        add(plot, QStringLiteral("/series/a[:]"));
        CHECK_FALSE(errorOf(plot, 0).isEmpty());
        CHECK(errorOf(plot, 1).isEmpty());
        CHECK(plot->hasData());
        CHECK(plot->pointCount() == 64);
    }
}

TEST_CASE_METHOD(PlotFixture, "a line can be called something other than its slice",
                 "[custom]")
{
    gui::CustomPlot* plot = tab();
    add(plot, QStringLiteral("/series/a[:]"));
    add(plot, QStringLiteral("/series/b[:]"));

    // A slice says exactly what a line is and nothing about what it means,
    // which is the right default and the wrong label on a plot of six.
    CHECK(plot->seriesLabel(0) == QStringLiteral("/series/a[:]"));

    plot->setAlias(0, QStringLiteral("morning"));
    CHECK(plot->seriesLabel(0) == QStringLiteral("morning"));
    CHECK(plot->data(plot->index(0, 0), gui::CustomPlot::AliasRole).toString()
          == QStringLiteral("morning"));
    // The other line is untouched, and so is what is actually read: the entry
    // still holds the slice, because that is what the file is asked for.
    CHECK(plot->seriesLabel(1) == QStringLiteral("/series/b[:]"));
    CHECK(plot->data(plot->index(0, 0), gui::CustomPlot::ExpressionRole).toString()
          == QStringLiteral("/series/a[:]"));
    CHECK(plot->pointCount() == 128);

    SECTION("an empty alias hands the slice back")
    {
        plot->setAlias(0, QStringLiteral("   "));
        CHECK(plot->seriesLabel(0) == QStringLiteral("/series/a[:]"));
    }

    SECTION("and it travels with a saved view")
    {
        REQUIRE(set()->saveView(QStringLiteral("named"), 0, {}).isEmpty());
        const int target = set()->addPlot();
        settleAll();
        set()->restoreView(QStringLiteral("named"), target);
        settleAll();
        CHECK(set()->plotAt(target)->seriesLabel(0) == QStringLiteral("morning"));
    }
}

TEST_CASE_METHOD(PlotFixture, "a bare path is the whole of the dataset", "[custom]")
{
    gui::CustomPlot* plot = tab();
    add(plot, QStringLiteral("/series/a"));

    CHECK(errorOf(plot, 0).isEmpty());
    CHECK(plot->pointCount() == 64);
}

TEST_CASE_METHOD(PlotFixture, "a dataset arrives as the lines the plot tab would draw",
                 "[custom]")
{
    gui::CustomPlot* plot = tab();

    SECTION("a vector is one line, written out as the slice it is")
    {
        // Through the set rather than the plot, because that is the call the
        // tree's plus makes: it has an index, not an object.
        set()->addDatasetTo(0, QStringLiteral("/series/a"));
        settleAll();
        CHECK(plot->sourceSeriesCount() == 1);
        // Spelt "[:]" rather than left bare. The two select the same elements,
        // and the written form is the one a reader can edit into something
        // else without first working out what it was.
        CHECK(plot->seriesLabel(0) == QStringLiteral("/series/a[:]"));
    }

    SECTION("a cube is one line per leading coordinate, last dimension along x")
    {
        plot->addDataset(QStringLiteral("/cube"));
        settleAll();
        // 2 x 3 x 4: six lines of four points, which is what the plot tab
        // draws for it.
        CHECK(plot->sourceSeriesCount() == 6);
        CHECK(plot->seriesLabel(0) == QStringLiteral("/cube[0, 0, :]"));
        CHECK(plot->seriesLabel(1) == QStringLiteral("/cube[0, 1, :]"));
        CHECK(plot->seriesLabel(5) == QStringLiteral("/cube[1, 2, :]"));
        CHECK(plot->pointCount() == 24);
        CHECK(plot->sourcePointCount() == 4);
    }

    SECTION("a dataset that cannot be drawn is refused with its reason")
    {
        QSignalSpy said(plot, &gui::CustomPlot::notice);
        plot->addDataset(QStringLiteral("/str_vlen"));
        settleAll();
        CHECK(plot->sourceSeriesCount() == 0);
        REQUIRE(said.count() == 1);
        CHECK_THAT(said.at(0).at(0).toString().toStdString(),
                   ContainsSubstring("only numbers"));
    }
}

TEST_CASE_METHOD(PlotFixture, "the x of a point comes from whichever axis is chosen",
                 "[custom]")
{
    gui::CustomPlot* plot = tab();
    add(plot, QStringLiteral("/series/a[:]"));

    SECTION("by default the element's own index")
    {
        const QList<QPointF> line = drawn(plot, 0);
        REQUIRE(line.size() == 64);
        CHECK(line.first().x() == 0.0);
        CHECK(line.at(7).x() == 7.0);
        CHECK(line.last().x() == 63.0);
        CHECK(line.at(7).y() == 7.0);
    }

    SECTION("a stated range moves the points without changing them")
    {
        plot->setXStart(10.0);
        plot->setXStep(0.5);
        const QList<QPointF> line = drawn(plot, 0);
        REQUIRE(line.size() == 64);
        CHECK(line.first().x() == 10.0);
        CHECK(line.at(4).x() == 12.0);
        CHECK(line.at(4).y() == 4.0);
    }

    SECTION("going back to the index puts the points back on their own places")
    {
        // The surface stops pushing a start and a step down in index mode, so
        // without this the last two it pushed would simply stay and "index"
        // would draw whatever range the reader had stated before it.
        plot->setXMode(gui::CustomPlot::Range);
        plot->setXStart(10.0);
        plot->setXStep(0.5);
        REQUIRE(drawn(plot, 0).first().x() == 10.0);

        plot->setXMode(gui::CustomPlot::Index);
        const QList<QPointF> line = drawn(plot, 0);
        REQUIRE(line.size() == 64);
        CHECK(line.first().x() == 0.0);
        CHECK(line.at(7).x() == 7.0);
        CHECK(line.last().x() == 63.0);
    }

    SECTION("a time series dataset puts each point at that dataset's value")
    {
        plot->setXExpression(QStringLiteral("/series/time[:]"));
        plot->setXMode(gui::CustomPlot::Dataset);
        settleAll();

        REQUIRE(plot->xError().isEmpty());
        REQUIRE(plot->xReady());
        const QList<QPointF> line = drawn(plot, 0);
        REQUIRE(line.size() == 64);
        // series_t is i / 2, so sample 7 of series_a is drawn at 3.5.
        CHECK(line.at(7).x() == 3.5);
        CHECK(line.at(7).y() == 7.0);
        CHECK(line.last().x() == 31.5);
    }

    SECTION("a time series that will not read is said once, not once per line")
    {
        plot->setXExpression(QStringLiteral("/matrix[:, :]"));
        plot->setXMode(gui::CustomPlot::Dataset);
        settleAll();

        CHECK_FALSE(plot->xError().isEmpty());
        CHECK_FALSE(plot->xReady());
        // Nothing is drawable without an x to draw it against, and the plot's
        // one error is the time base's rather than every entry's.
        CHECK_FALSE(plot->hasData());
        CHECK(plot->error() == plot->xError());
    }
}

TEST_CASE_METHOD(PlotFixture, "align and stretch decide where a short line goes",
                 "[custom]")
{
    gui::CustomPlot* plot = tab();
    add(plot, QStringLiteral("/series/a[:]"));    // 64 samples, sets the axis
    add(plot, QStringLiteral("/series/half[:]")); // 32 samples, value 2 * i

    REQUIRE(plot->sourcePointCount() == 64);

    const auto scalable = [&](int row) {
        return plot->data(plot->index(row, 0), gui::CustomPlot::ScalableRole)
            .toBool();
    };
    // The long one is exactly as long as the axis, so the pair means nothing
    // for it and the row shows them disabled.
    CHECK_FALSE(scalable(0));
    CHECK(scalable(1));

    SECTION("align lays it point for point and it stops where it runs out")
    {
        const QList<QPointF> line = drawn(plot, 1);
        REQUIRE(line.size() == 32);
        CHECK(line.first().x() == 0.0);
        CHECK(line.at(5).x() == 5.0);
        CHECK(line.last().x() == 31.0);
        CHECK(line.last().y() == 62.0);
    }

    SECTION("stretch spreads it over the whole axis")
    {
        plot->setScaling(1, gui::CustomPlot::Stretch);
        const QList<QPointF> line = drawn(plot, 1);
        REQUIRE(line.size() == 32);
        // First sample at the start of the axis, last at its end, the rest
        // evenly between: 63 / 31 per step.
        CHECK(line.first().x() == 0.0);
        CHECK(line.last().x() == 63.0);
        CHECK(std::abs(line.at(1).x() - 63.0 / 31.0) < 1e-9);
        CHECK(line.last().y() == 62.0);
    }

    SECTION("stretch against a time series reads the time base at the same share")
    {
        plot->setXExpression(QStringLiteral("/series/time[:]"));
        plot->setXMode(gui::CustomPlot::Dataset);
        settleAll();
        plot->setScaling(1, gui::CustomPlot::Stretch);

        const QList<QPointF> line = drawn(plot, 1);
        REQUIRE(line.size() == 32);
        // The last sample lands on the last of the 64 time values, which is
        // 63 / 2.
        CHECK(line.first().x() == 0.0);
        CHECK(line.last().x() == 31.5);
    }

    SECTION("align against a shorter time base cuts the line where it ends")
    {
        plot->setXExpression(QStringLiteral("/series/half[:]"));
        plot->setXMode(gui::CustomPlot::Dataset);
        settleAll();

        // The axis is now 32 long and series_a is 64, so half of it has no x
        // to be drawn against and is not drawn.
        REQUIRE(plot->sourcePointCount() == 32);
        CHECK(drawn(plot, 0).size() == 32);
    }
}

TEST_CASE("a line longer than the plot draws is thinned by striding its indices",
          "[custom]")
{
    // The function rather than a dataset, because the cap is 2048 points and
    // the shared fixture has nothing that long -- and because what is being
    // asserted is arithmetic, which is the thing a unit is for.
    std::vector<std::vector<hsize_t>> indices;
    indices.emplace_back();
    for (hsize_t i = 0; i < 10000; ++i) {
        indices.front().push_back(i);
    }
    const std::vector<bool> drop{false};

    const int stride = gui::thinToPoints(indices, drop, 2048);
    CHECK(stride == 5);
    CHECK(indices.front().size() == 2000);
    CHECK(indices.front().at(1) == 5);

    SECTION("a line that already fits is left exactly as it was")
    {
        std::vector<std::vector<hsize_t>> small{{0, 1, 2, 3}};
        CHECK(gui::thinToPoints(small, {false}, 2048) == 1);
        CHECK(small.front().size() == 4);
    }
}

TEST_CASE_METHOD(PlotFixture, "an entry is checked as it is typed once its path is known",
                 "[custom]")
{
    gui::CustomPlot* plot = tab();

    // Nothing is known about the file yet, so nothing is claimed to be wrong:
    // a path this session has never resolved is a question, not a mistake.
    CHECK(plot->entryError(0, QStringLiteral("/series/a[:, :]")).isEmpty());

    // A malformed line needs no file at all.
    CHECK_THAT(plot->entryError(0, QStringLiteral("series_a[:]")).toStdString(),
               ContainsSubstring("starts at the root"));
    CHECK_THAT(plot->entryError(0, QStringLiteral("/series/a[:]]")).toStdString(),
               ContainsSubstring("never opened"));

    // Once the path has been read for real, the subscript is checked against
    // its actual shape on every keystroke.
    add(plot, QStringLiteral("/series/a[:]"));
    CHECK(plot->entryError(0, QStringLiteral("/series/a[0:8]")).isEmpty());
    CHECK_THAT(plot->entryError(0, QStringLiteral("/series/a[:, :]")).toStdString(),
               ContainsSubstring("dim"));
}

TEST_CASE_METHOD(PlotFixture, "a crowded plot is asked about rather than refused",
                 "[custom]")
{
    gui::CustomPlot* plot = tab();

    SECTION("a line at a time is never questioned, however many there are")
    {
        QSignalSpy asked(plot, &gui::CustomPlot::crowding);
        for (int i = 0; i < gui::CustomPlot::kCrowdedLines + 8; ++i) {
            REQUIRE(plot->addExpression(QStringLiteral("/series/a[:]")) == i);
        }
        CHECK(plot->sourceSeriesCount() == gui::CustomPlot::kCrowdedLines + 8);
        CHECK(asked.count() == 0);
    }

    SECTION("a dataset small enough to draw goes straight in")
    {
        QSignalSpy asked(plot, &gui::CustomPlot::crowding);
        plot->addDataset(QStringLiteral("/cube")); // six lines
        settleAll();
        CHECK(plot->sourceSeriesCount() == 6);
        CHECK(asked.count() == 0);
    }

    SECTION("one too big to draw unasked adds nothing and says how many")
    {
        QSignalSpy asked(plot, &gui::CustomPlot::crowding);
        // 100 x 100: a hundred lines, which is past the point where strokes
        // over one another stop separating.
        plot->addDataset(QStringLiteral("/compressed"));
        settleAll();

        CHECK(plot->sourceSeriesCount() == 0);
        REQUIRE(asked.count() == 1);
        CHECK(asked.at(0).at(0).toString() == QStringLiteral("/compressed"));
        CHECK(asked.at(0).at(1).toInt() == 100);
    }

    SECTION("and asking again with the answer adds every one of them")
    {
        plot->addDataset(QStringLiteral("/compressed"), true);
        settleAll();
        CHECK(plot->sourceSeriesCount() == 100);
        CHECK(plot->seriesLabel(0) == QStringLiteral("/compressed[0, :]"));
        CHECK(plot->seriesLabel(99) == QStringLiteral("/compressed[99, :]"));
        // Nothing was clipped: a hundred lines of a hundred points each.
        CHECK(plot->pointCount() == 10000);
    }

    SECTION("the set forwards the question with the tab it is about on it")
    {
        QSignalSpy asked(set(), &gui::CustomPlotSet::crowdingWarned);
        set()->addDatasetTo(0, QStringLiteral("/compressed"));
        settleAll();

        REQUIRE(asked.count() == 1);
        CHECK(asked.at(0).at(0).toInt() == 0);
        CHECK(asked.at(0).at(2).toInt() == 100);

        set()->addDatasetTo(0, QStringLiteral("/compressed"), true);
        settleAll();
        CHECK(set()->plotAt(0)->sourceSeriesCount() == 100);
    }
}

TEST_CASE_METHOD(PlotFixture, "a custom plot has no window to go back to",
                 "[custom]")
{
    // The legend's "first N" button puts a table of ten thousand rows back to
    // the window a *selection* opened on. A custom plot opens on nothing and
    // every entry in it was put there on purpose, so there is no such number
    // and -1 is how the legend is told to leave the button out.
    CHECK(gui::CustomPlot::initialSeriesLimit() == -1);
}

TEST_CASE_METHOD(PlotFixture, "the tabs are named, unique and reorderable", "[custom]")
{
    gui::CustomPlotSet* plots = set();
    const int first = plots->addPlot();
    const int second = plots->addPlot();

    CHECK(plots->count() == 2);
    CHECK(plots->plotAt(first)->name() == QStringLiteral("Custom 1"));
    CHECK(plots->plotAt(second)->name() == QStringLiteral("Custom 2"));

    SECTION("a name already taken is refused rather than made unique")
    {
        CHECK_THAT(plots->setName(second, QStringLiteral("Custom 1")).toStdString(),
                   ContainsSubstring("already called"));
        CHECK(plots->plotAt(second)->name() == QStringLiteral("Custom 2"));

        CHECK(plots->setName(second, QStringLiteral("pressure")).isEmpty());
        CHECK(plots->plotAt(second)->name() == QStringLiteral("pressure"));
        CHECK(plots->indexOfName(QStringLiteral("pressure")) == second);
    }

    SECTION("an empty name is refused")
    {
        CHECK_FALSE(plots->setName(first, QStringLiteral("   ")).isEmpty());
    }

    SECTION("a number freed by closing a tab is used again")
    {
        plots->removePlot(first);
        CHECK(plots->addPlot() == 1);
        CHECK(plots->plotAt(1)->name() == QStringLiteral("Custom 1"));
    }

    SECTION("reordering keeps the reader on the tab they were looking at")
    {
        plots->setActiveIndex(second);
        plots->movePlot(second, first);
        CHECK(plots->activeIndex() == first);
        CHECK(plots->plotAt(first)->name() == QStringLiteral("Custom 2"));
        CHECK(plots->plotAt(second)->name() == QStringLiteral("Custom 1"));
    }

    SECTION("a tab in a window of its own is not in the strip")
    {
        plots->setActiveIndex(first);
        plots->setDetached(first, true);
        CHECK(plots->detached(first));
        CHECK(plots->activeIndex() == -1);
    }
}

TEST_CASE_METHOD(PlotFixture, "the tabs belong to the file that is open", "[custom]")
{
    gui::CustomPlot* plot = tab();
    add(plot, QStringLiteral("/series/a[:]"));
    REQUIRE(set()->count() == 1);
    REQUIRE(set()->saveView(QStringLiteral("both"), 0, {}).isEmpty());

    controller.closeFile();
    settleAll();

    // Every entry is a path inside a file, and this is not that file any more.
    CHECK(set()->count() == 0);
    CHECK(set()->activeIndex() == -1);

    // The views are not. A view is a way of looking at data rather than a
    // piece of one file's contents, and it says how much of whatever is open
    // it can still draw instead of going away.
    CHECK(set()->viewNames() == QStringList{QStringLiteral("both")});
    CHECK(set()->stateOf(QStringLiteral("both"))
          == gui::CustomPlotSet::NoMatch);
}

TEST_CASE_METHOD(PlotFixture, "a saved view is put into whichever tab is open",
                 "[custom]")
{
    gui::CustomPlotSet* plots = set();
    const int source = plots->addPlot();
    settleAll();
    gui::CustomPlot* from = plots->plotAt(source);
    add(from, QStringLiteral("/series/a[:]"));
    add(from, QStringLiteral("/series/half[:]"));
    from->setScaling(1, gui::CustomPlot::Stretch);
    from->setXExpression(QStringLiteral("/series/time[:]"));
    from->setXMode(gui::CustomPlot::Dataset);
    settleAll();

    const QVariantMap settings{{QStringLiteral("showMarkers"), true}};
    REQUIRE(plots->saveView(QStringLiteral("pair"), source, settings).isEmpty());
    CHECK(plots->viewNames() == QStringList{QStringLiteral("pair")});

    SECTION("into a second tab, entries, scaling and axis together")
    {
        const int target = plots->addPlot();
        settleAll();
        QSignalSpy restored(plots, &gui::CustomPlotSet::viewRestored);

        plots->restoreView(QStringLiteral("pair"), target);
        settleAll();

        gui::CustomPlot* into = plots->plotAt(target);
        CHECK(into->sourceSeriesCount() == 2);
        CHECK(into->seriesLabel(1) == QStringLiteral("/series/half[:]"));
        CHECK(into->data(into->index(1, 0), gui::CustomPlot::ScalingRole).toInt()
              == static_cast<int>(gui::CustomPlot::Stretch));
        CHECK(into->xMode() == gui::CustomPlot::Dataset);
        CHECK(into->xExpression() == QStringLiteral("/series/time[:]"));
        CHECK(into->pointCount() == 96);

        // The drawing settings were QML's, so they go back to QML rather than
        // being applied here.
        REQUIRE(restored.count() == 1);
        CHECK(restored.at(0).at(0).toInt() == target);
        CHECK(restored.at(0).at(1).toMap() == settings);
    }

    SECTION("a view whose lines all read reports nothing to warn about")
    {
        QSignalSpy checked(plots, &gui::CustomPlotSet::viewChecked);
        plots->checkView(QStringLiteral("pair"));
        settleAll();

        REQUIRE(checked.count() == 1);
        CHECK(checked.at(0).at(1).toInt() == 0);
    }

    SECTION("a view naming what is not there counts the lines that will not draw")
    {
        gui::CustomPlot* broken = plots->plotAt(plots->addPlot());
        settleAll();
        add(broken, QStringLiteral("/series/a[:]"));
        add(broken, QStringLiteral("/gone[:]"));
        add(broken, QStringLiteral("/matrix[:, :]"));
        REQUIRE(plots->saveView(QStringLiteral("stale"),
                                plots->indexOfName(broken->name()), {})
                    .isEmpty());

        QSignalSpy checked(plots, &gui::CustomPlotSet::viewChecked);
        plots->checkView(QStringLiteral("stale"));
        settleAll();

        REQUIRE(checked.count() == 1);
        CHECK(checked.at(0).at(1).toInt() == 2);
        CHECK(checked.at(0).at(2).toStringList().size() == 2);
    }

    SECTION("the tab's own title travels with it")
    {
        const int target = plots->addPlot();
        settleAll();
        REQUIRE(plots->setName(source, QStringLiteral("morning")).isEmpty());
        REQUIRE(plots->saveView(QStringLiteral("named"), source, {}).isEmpty());

        plots->restoreView(QStringLiteral("named"), target);
        settleAll();
        // The tab it was saved from is still open under that name, so this one
        // takes the first free variant rather than being refused -- a view is
        // an arrangement and what it is called is part of one.
        CHECK(plots->plotAt(target)->name() == QStringLiteral("morning 2"));
        CHECK(plots->plotAt(source)->name() == QStringLiteral("morning"));
    }

    SECTION("and takes that title outright once the tab it came from has gone")
    {
        REQUIRE(plots->setName(source, QStringLiteral("morning")).isEmpty());
        REQUIRE(plots->saveView(QStringLiteral("named"), source, {}).isEmpty());
        plots->removePlot(source);
        QCoreApplication::processEvents();
        REQUIRE(plots->count() == 0);

        const int target = plots->addPlot();
        settleAll();
        plots->restoreView(QStringLiteral("named"), target);
        settleAll();

        // Nothing is using the name now, so there is no variant to fall back
        // to and the tab is called what the view was saved as.
        CHECK(plots->plotAt(target)->name() == QStringLiteral("morning"));
    }

    SECTION("saving over a name replaces the view rather than adding a second")
    {
        REQUIRE(plots->saveView(QStringLiteral("other"), source, {}).isEmpty());
        REQUIRE(plots->saveView(QStringLiteral("pair"), source, {}).isEmpty());
        // Both fit the file entirely, so the order between them is the
        // alphabet's.
        CHECK(plots->viewNames()
              == QStringList{QStringLiteral("other"), QStringLiteral("pair")});

        plots->removeView(QStringLiteral("pair"));
        CHECK(plots->viewNames() == QStringList{QStringLiteral("other")});
    }
}

TEST_CASE_METHOD(PlotFixture, "a view says how much of the file in front of it it can draw",
                 "[custom][views]")
{
    gui::CustomPlotSet* plots = set();
    const int source = plots->addPlot();
    settleAll();
    gui::CustomPlot* from = plots->plotAt(source);

    SECTION("everything it names is here")
    {
        add(from, QStringLiteral("/series/a[:]"));
        add(from, QStringLiteral("/series/b[:]"));
        REQUIRE(plots->saveView(QStringLiteral("both"), source, {}).isEmpty());
        CHECK(plots->stateOf(QStringLiteral("both"))
              == gui::CustomPlotSet::FullMatch);
    }

    SECTION("some of it is")
    {
        add(from, QStringLiteral("/series/a[:]"));
        add(from, QStringLiteral("/gone_away[:]"));
        REQUIRE(plots->saveView(QStringLiteral("half"), source, {}).isEmpty());
        CHECK(plots->stateOf(QStringLiteral("half"))
              == gui::CustomPlotSet::PartialMatch);
    }

    SECTION("none of it is")
    {
        add(from, QStringLiteral("/gone_away[:]"));
        add(from, QStringLiteral("/also_gone[:]"));
        REQUIRE(plots->saveView(QStringLiteral("stale"), source, {}).isEmpty());
        CHECK(plots->stateOf(QStringLiteral("stale"))
              == gui::CustomPlotSet::NoMatch);
    }

    SECTION("and a view nobody saved matches nothing")
    {
        CHECK(plots->stateOf(QStringLiteral("never"))
              == gui::CustomPlotSet::NoMatch);
    }
}

TEST_CASE_METHOD(PlotFixture, "the views are offered best fit first, then alphabetically",
                 "[custom][views]")
{
    gui::CustomPlotSet* plots = set();
    const int source = plots->addPlot();
    settleAll();
    gui::CustomPlot* from = plots->plotAt(source);

    // A reader is nearly always looking for something that will actually draw,
    // so that is what the top of the list is for.
    add(from, QStringLiteral("/gone_away[:]"));
    REQUIRE(plots->saveView(QStringLiteral("a red one"), source, {}).isEmpty());

    from->clearEntries();
    add(from, QStringLiteral("/series/a[:]"));
    add(from, QStringLiteral("/gone_away[:]"));
    REQUIRE(plots->saveView(QStringLiteral("z yellow"), source, {}).isEmpty());
    REQUIRE(plots->saveView(QStringLiteral("a yellow"), source, {}).isEmpty());

    from->clearEntries();
    add(from, QStringLiteral("/series/b[:]"));
    REQUIRE(plots->saveView(QStringLiteral("z green"), source, {}).isEmpty());

    CHECK(plots->viewNames()
          == QStringList{QStringLiteral("z green"), QStringLiteral("a yellow"),
                         QStringLiteral("z yellow"), QStringLiteral("a red one")});
}

TEST_CASE_METHOD(PlotFixture, "a saved view is still there next session",
                 "[custom][views]")
{
    // The one thing in this application that is written down besides the list
    // of files opened, and for the same reason: a comparison built once is
    // wanted again next week, on next week's file.
    //
    // Nothing is read or written until a host application has named itself,
    // which is what keeps every other suite off the reader's own settings --
    // so this one names itself, points QSettings at a directory of its own,
    // and puts both back afterwards.
    QTemporaryDir home;
    REQUIRE(home.isValid());
    const QSettings::Format wasFormat = QSettings::defaultFormat();
    const QString wasOrganization = QCoreApplication::organizationName();
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, home.path());
    QCoreApplication::setOrganizationName(QStringLiteral("H5ScopeViewTest"));
    const QScopeGuard restore([&] {
        QCoreApplication::setOrganizationName(wasOrganization);
        QSettings::setDefaultFormat(wasFormat);
    });

    const QVariantMap drawing{{QStringLiteral("showMarkers"), true},
                              {QStringLiteral("colorSingle"),
                               QVariant::fromValue(QColor(Qt::red))}};
    {
        gui::CustomPlotSet writing;
        const int index = writing.addPlot();
        gui::CustomPlot* plot = writing.plotAt(index);
        REQUIRE(plot->addExpression(QStringLiteral("/series/a[:]")) == 0);
        plot->setAlias(0, QStringLiteral("morning"));
        REQUIRE(writing.setName(index, QStringLiteral("runs")).isEmpty());
        REQUIRE(writing.saveView(QStringLiteral("kept"), index, drawing).isEmpty());
    }

    // A second set, built from nothing but what the first one wrote.
    gui::CustomPlotSet reading;
    REQUIRE(reading.viewNames() == QStringList{QStringLiteral("kept")});

    const int into = reading.addPlot();
    reading.restoreView(QStringLiteral("kept"), into);
    gui::CustomPlot* back = reading.plotAt(into);

    CHECK(back->sourceSeriesCount() == 1);
    CHECK(back->seriesLabel(0) == QStringLiteral("morning"));
    CHECK(back->data(back->index(0, 0), gui::CustomPlot::ExpressionRole).toString()
          == QStringLiteral("/series/a[:]"));
    CHECK(back->name() == QStringLiteral("runs"));

    SECTION("and so are the drawing settings, colours and all")
    {
        QSignalSpy restored(&reading, &gui::CustomPlotSet::viewRestored);
        const int second = reading.addPlot();
        reading.restoreView(QStringLiteral("kept"), second);

        REQUIRE(restored.count() == 1);
        const QVariantMap came = restored.at(0).at(1).toMap();
        CHECK(came.value(QStringLiteral("showMarkers")).toBool());
        // A colour is the one value that does not survive a trip through JSON
        // on its own; it is written as "#aarrggbb", which is what a QML colour
        // property reads back without being asked.
        CHECK(QColor(came.value(QStringLiteral("colorSingle")).toString())
              == QColor(Qt::red));
    }

    SECTION("and forgetting one forgets it for good")
    {
        reading.removeView(QStringLiteral("kept"));
        gui::CustomPlotSet third;
        CHECK(third.viewNames().isEmpty());
    }
}

TEST_CASE_METHOD(PlotFixture, "a time base can be named from the tree", "[custom]")
{
    gui::CustomPlotSet* plots = set();
    const int index = plots->addPlot();
    settleAll();

    SECTION("a vector is its own time base")
    {
        plots->setTimeSeriesOf(index, QStringLiteral("/series/time"));
        settleAll();
        CHECK(plots->plotAt(index)->xMode() == gui::CustomPlot::Dataset);
        CHECK(plots->plotAt(index)->xExpression() == QStringLiteral("/series/time"));
    }

    SECTION("anything else takes the line the plot tab would have drawn first")
    {
        plots->setTimeSeriesOf(index, QStringLiteral("/cube"));
        settleAll();
        CHECK(plots->plotAt(index)->xExpression()
              == QStringLiteral("/cube[0, 0, :]"));
    }
}

TEST_CASE_METHOD(PlotFixture, "a line of the plot tab writes itself as a slice",
                 "[custom][plot]")
{
    // What the legend's "add to a custom plot" puts into the entry it makes.
    // It has to be exact: the reader is taking the line they can see, and an
    // expression that selects more of the dataset than the plot drew would
    // hand them a different curve under the same name.
    auto* table = qobject_cast<gui::DatasetTableModel*>(controller.datasetModel());
    REQUIRE(table != nullptr);
    auto* plot = controller.datasetPlot();

    SECTION("every dimension it holds fixed prints as the index it holds")
    {
        REQUIRE(h5test::selectAndSettle(controller, "/cube")); // 2 x 3 x 4
        REQUIRE(plot->seriesFromRows());
        CHECK(plot->seriesExpression(0) == QStringLiteral("/cube[0, 0, :]"));
        CHECK(plot->seriesExpression(3) == QStringLiteral("/cube[1, 0, :]"));
        CHECK(plot->seriesExpression(5) == QStringLiteral("/cube[1, 2, :]"));
    }

    SECTION("a sliced table yields an expression that says what was sliced")
    {
        REQUIRE(h5test::selectAndSettle(controller, "/cube"));
        REQUIRE(controller.applySlice(QStringLiteral(":, :, 1:3")).isEmpty());
        settleAll();

        // Two points per line, and the expression says which two rather than
        // taking the whole dimension back.
        CHECK(plot->pointCount() == 2);
        CHECK(plot->seriesExpression(0) == QStringLiteral("/cube[0, 0, 1:3]"));
    }

    SECTION("a scattered selection comes back bracketed, as numpy brackets one")
    {
        REQUIRE(h5test::selectAndSettle(controller, "/cube"));
        REQUIRE(controller.applySlice(QStringLiteral(":, :, [0,3]")).isEmpty());
        settleAll();
        CHECK(plot->seriesExpression(0) == QStringLiteral("/cube[0, 0, [0,3]]"));
    }

    SECTION("a line running over two dimensions is not a slice, and says so")
    {
        REQUIRE(h5test::selectAndSettle(controller, "/hypercube"));
        auto* setup =
            qobject_cast<gui::TableSetupModel*>(controller.tableSetupModel());
        REQUIRE(setup != nullptr);
        // A second dimension on the columns: a line's points now run over the
        // product of two, and no hyperslab of one dimension is that line.
        setup->setAxis(2, true);
        settleAll();
        CHECK(plot->seriesExpression(0).isEmpty());
    }

    SECTION("and what it writes reads back into a custom plot unchanged")
    {
        REQUIRE(h5test::selectAndSettle(controller, "/cube"));
        const QString written = plot->seriesExpression(4);
        REQUIRE(written == QStringLiteral("/cube[1, 1, :]"));

        gui::CustomPlot* custom = tab();
        add(custom, written);
        CHECK(errorOf(custom, 0).isEmpty());
        CHECK(custom->pointCount() == 4);
        // /cube holds its own flat index, so line (1,1) is
        // 1*12 + 1*4 + k: 16, 17, 18, 19.
        CHECK(custom->minimum() == 16.0);
        CHECK(custom->maximum() == 19.0);
    }
}

TEST_CASE_METHOD(PlotFixture, "reading a whole tab is one crossing of the HDF5 thread",
                 "[custom][cost]")
{
    // The number this feature could get wrong without anyone noticing. Eight
    // entries read one at a time move exactly the same bytes as eight read
    // together and cost eight handshakes instead of one -- which is the same
    // regression the plot's batching was written for, and this is where a
    // custom tab would reintroduce it.
    gui::CustomPlot* plot = tab();
    for (int i = 0; i < 8; ++i) {
        REQUIRE(plot->addExpression(QStringLiteral("/series/a[:]")) == i);
    }
    REQUIRE(plot->addExpression(QStringLiteral("/series/b[:]")) == 8);

    const long long before = gui::H5Thread::instance().crossings();
    settleAll();
    const long long spent = gui::H5Thread::instance().crossings() - before;

    CHECK(plot->pointCount() == 9 * 64);
    CHECK(spent == 1);

    SECTION("and so is a read with a time base in it")
    {
        plot->setXExpression(QStringLiteral("/series/time[:]"));
        plot->setXMode(gui::CustomPlot::Dataset);
        const long long mark = gui::H5Thread::instance().crossings();
        settleAll();
        CHECK(gui::H5Thread::instance().crossings() - mark == 1);
    }

    SECTION("several edits in one turn of the loop are still one read")
    {
        plot->setExpression(0, QStringLiteral("/series/b[:]"));
        plot->setExpression(1, QStringLiteral("/series/b[:]"));
        plot->removeEntry(2);
        const long long mark = gui::H5Thread::instance().crossings();
        settleAll();
        CHECK(gui::H5Thread::instance().crossings() - mark == 1);
    }

    SECTION("reordering and renaming read nothing at all")
    {
        const long long mark = gui::H5Thread::instance().crossings();
        plot->moveEntry(0, 4);
        set()->setName(0, QStringLiteral("pressure"));
        plot->setSeriesVisible(3, false);
        settleAll();
        CHECK(gui::H5Thread::instance().crossings() - mark == 0);
    }
}
