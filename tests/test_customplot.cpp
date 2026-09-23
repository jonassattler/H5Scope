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
// The points are asserted through lineOf() and drawingAxis(), which together
// are what fill() hands a renderer, and gui::samplesOf() over the pair is
// exactly what fill() used to put into a QXYSeries. That is the whole of the
// boundary -- the values never cross into QML -- and asserting it here rather
// than through something rendered is what lets this suite stay free of a QML
// engine, a window and a scene graph.

#include "gui/AppController.hpp"
#include "gui/CustomPlot.hpp"
#include "gui/CustomPlotSet.hpp"
#include "gui/DatasetLookup.hpp"
#include "gui/DatasetPlot.hpp"
#include "gui/DatasetTableModel.hpp"
#include "gui/H5Thread.hpp"
#include "gui/PlotItem.hpp"
#include "gui/PlotLevels.hpp"
#include "gui/PlotProjection.hpp"
#include "gui/TableSetupModel.hpp"
#include "support/AsyncModels.hpp"
#include "support/H5Reader.hpp"
#include "support/TestFile.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <QColor>
#include <QCoreApplication>
#include <QScopeGuard>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <hdf5.h>

#include <algorithm>
#include <limits>
#include <cmath>
#include <vector>

using Catch::Approx;
using Catch::Matchers::ContainsSubstring;

namespace {

/// A controller over the shared fixture, with one custom tab already made.
///
/// Reads are coalesced onto a zero-timer -- several edits in one turn of the
/// event loop are one job -- so settling means turning the loop as well as
/// draining the thread, and a few times over: adding a dataset resolves its
/// shape in one crossing and reads its lines in the next.
struct PlotFixture
{
    h5test::TempFile temp{"custom"};
    gui::AppController controller;

    PlotFixture()
    {
        h5test::onH5([&] { h5test::writeFixture(temp.path()); });
        REQUIRE(h5test::openFileAndSettle(controller, QString::fromStdString(temp.path())));
    }

    [[nodiscard]] gui::CustomPlotSet* set() const { return controller.customPlots(); }

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

    /// The points fill() hands a renderer, in data coordinates: where each
    /// sample of entry `series` lands along x, with the undrawable ones absent.
    [[nodiscard]] static QList<QPointF> drawn(gui::CustomPlot* plot, int series)
    {
        const std::vector<QPointF> points =
            gui::samplesOf(plot->lineOf(series), plot->drawingAxis());
        QList<QPointF> out;
        out.reserve(static_cast<qsizetype>(points.size()));
        for (const QPointF& point : points) {
            out.append(point);
        }
        return out;
    }

    [[nodiscard]] static QString errorOf(const gui::CustomPlot* plot, int row)
    {
        return plot->data(plot->index(row, 0), gui::CustomPlot::ErrorRole).toString();
    }
};

} // namespace

TEST_CASE_METHOD(PlotFixture, "a custom plot draws slices of several datasets together", "[custom]")
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

TEST_CASE("an expression may name a member after its subscript", "[custom][member]")
{
    // Pure text. The rule is that a chain is recognised only after a closing
    // bracket, because a link name holds a '.' as freely as it holds a '[' and
    // telling `/data/run.3` from member 3 of `run` would mean asking the file.

    SECTION("a chain after the subscript is taken off the path")
    {
        const gui::Expression parts =
            gui::splitExpression(QStringLiteral("/events[0:100].energy"));
        REQUIRE(parts.valid());
        CHECK(parts.path == QStringLiteral("/events"));
        CHECK(parts.subscript == QStringLiteral("0:100"));
        CHECK(parts.member == QStringLiteral(".energy"));
    }

    SECTION("the chain keeps its own subscripts")
    {
        const gui::Expression parts =
            gui::splitExpression(QStringLiteral("/events[3, :].samples[2]"));
        REQUIRE(parts.valid());
        CHECK(parts.path == QStringLiteral("/events"));
        CHECK(parts.subscript == QStringLiteral("3, :"));
        CHECK(parts.member == QStringLiteral(".samples[2]"));
    }

    SECTION("a dotted link name is still a link name")
    {
        // The case the rule exists for. There is no `].` here, so this parses
        // exactly as it did before any of this was written.
        const gui::Expression parts =
            gui::splitExpression(QStringLiteral("/data/run.3[:]"));
        REQUIRE(parts.valid());
        CHECK(parts.path == QStringLiteral("/data/run.3"));
        CHECK(parts.subscript == QStringLiteral(":"));
        CHECK(parts.member.isEmpty());
    }

    SECTION("a bracket in a link name is still a bracket in a link name")
    {
        const gui::Expression parts =
            gui::splitExpression(QStringLiteral("/stress/awkward[1][0:4]"));
        REQUIRE(parts.valid());
        CHECK(parts.path == QStringLiteral("/stress/awkward[1]"));
        CHECK(parts.subscript == QStringLiteral("0:4"));
        CHECK(parts.member.isEmpty());
    }

    SECTION("everything without a chain reads as it always did")
    {
        const gui::Expression whole = gui::splitExpression(QStringLiteral("/series/a"));
        CHECK(whole.path == QStringLiteral("/series/a"));
        CHECK(whole.subscript.isEmpty());
        CHECK(whole.member.isEmpty());

        CHECK_FALSE(gui::splitExpression(QStringLiteral("/a[0]]")).valid());
        CHECK_FALSE(gui::splitExpression(QStringLiteral("/a[0")).valid());
        CHECK_FALSE(gui::splitExpression(QStringLiteral("a[0]")).valid());
    }
}

TEST_CASE_METHOD(PlotFixture, "a custom tab draws a member of a compound",
                 "[custom][member]")
{
    // /compound is {id: int32, value: float64} x 2, holding {7, 1.5} and
    // {9, 2.5}. Nothing about it is drawable until a member is named.
    gui::CustomPlot* plot = tab();
    REQUIRE(plot != nullptr);

    SECTION("naming a member makes a line out of a struct")
    {
        add(plot, QStringLiteral("/compound[:].value"));
        CHECK(errorOf(plot, 0).isEmpty());
        CHECK(plot->hasData());
        CHECK(plot->pointCount() == 2);
        CHECK(plot->minimum() == 1.5);
        CHECK(plot->maximum() == 2.5);
        // The entry keeps the name it was written under, chain and all.
        CHECK(plot->seriesLabel(0) == QStringLiteral("/compound[:].value"));
    }

    SECTION("two members of one dataset are two lines")
    {
        add(plot, QStringLiteral("/compound[:].value"));
        add(plot, QStringLiteral("/compound[:].id"));
        CHECK(plot->sourceSeriesCount() == 2);
        CHECK(errorOf(plot, 0).isEmpty());
        CHECK(errorOf(plot, 1).isEmpty());
        CHECK(plot->minimum() == 1.5);
        CHECK(plot->maximum() == 9.0);
    }

    SECTION("a compound with no member named says to name one")
    {
        add(plot, QStringLiteral("/compound[:]"));
        const QString problem = errorOf(plot, 0);
        CHECK_THAT(problem.toStdString(), ContainsSubstring("name one of its members"));
        // And it suggests one, because the names are in the file and the
        // reader is being told they cannot have what they asked for.
        CHECK_THAT(problem.toStdString(), ContainsSubstring(".id"));
    }

    SECTION("a member that is not there says what is")
    {
        add(plot, QStringLiteral("/compound[:].nonesuch"));
        const QString problem = errorOf(plot, 0);
        CHECK_THAT(problem.toStdString(), ContainsSubstring("nonesuch"));
        CHECK_THAT(problem.toStdString(), ContainsSubstring("value"));
    }
}

TEST_CASE_METHOD(PlotFixture,
                 "a member costs what the same line costs as a dataset of its own",
                 "[custom][member][cost]")
{
    // /series/trace_pairs.v holds /trace again, element for element. So these
    // are one line read two ways, and the two readings had better agree about
    // both things: what the values are, and what it took to get them.
    //
    // The second is the one nothing else would notice. A member read that had
    // fallen back to one round trip per element -- or to reading the whole
    // struct and keeping a field of it -- would draw exactly the right picture,
    // which is how a custom tab once went a release asking HDF5 for one bucket
    // at a time with nothing anywhere that minded.
    //
    // A tab each, because adding an entry re-reads the ones already in the tab,
    // so two entries side by side would count one of them twice.
    gui::CustomPlot* plain = tab();
    const long long beforePlain = gui::CustomPlot::hyperslabs();
    add(plain, QStringLiteral("/trace[:]"));
    const long long plainReads = gui::CustomPlot::hyperslabs() - beforePlain;

    gui::CustomPlot* member = set()->plotAt(set()->addPlot());
    settleAll();
    REQUIRE(member != nullptr);
    const long long beforeMember = gui::CustomPlot::hyperslabs();
    add(member, QStringLiteral("/series/trace_pairs[:].v"));
    const long long memberReads = gui::CustomPlot::hyperslabs() - beforeMember;

    REQUIRE(errorOf(plain, 0).isEmpty());
    REQUIRE(errorOf(member, 0).isEmpty());

    // Read for read.
    CHECK(plainReads == 1);
    CHECK(memberReads == plainReads);

    // And value for value, down to the one-sample spike an envelope has to keep.
    CHECK(member->minimum() == plain->minimum());
    CHECK(member->maximum() == plain->maximum());
    CHECK(member->maximum() == Approx(9.0));
    CHECK(member->pointCount() == plain->pointCount());
    CHECK(member->sourcePointCount() == 20000);
}

TEST_CASE_METHOD(PlotFixture, "a zoom into a member reads nothing either",
                 "[custom][member][cost]")
{
    // /series/trace_pairs.v is /trace, element for element, at the length every
    // assertion about reading and zooming is written against. What this checks
    // is that a pyramid built out of a *member* read behaves like one built out
    // of a dataset read -- which it should, the pyramid sitting above
    // DataSource and having no idea which it was handed, but "should" is not
    // the same as checked, and this is the invariant that would be expensive to
    // lose.
    gui::CustomPlot* plot = tab();

    const long long before = gui::CustomPlot::hyperslabs();
    add(plot, QStringLiteral("/series/trace_pairs[:].v"));
    const long long spent = gui::CustomPlot::hyperslabs() - before;

    REQUIRE(plot->seriesCount() == 1);
    REQUIRE(plot->sourcePointCount() == 20000);
    // The same bound the dataset gets: the round trips follow the length of the
    // line, not the number of buckets it is folded into.
    CHECK(spent == (20000 + gui::kReadRun - 1) / gui::kReadRun);
    CHECK(spent == 1);
    // The one-sample spike survived the fold, so the reads changed and the
    // arithmetic did not.
    CHECK(plot->thinned());
    CHECK(plot->maximum() == Approx(9.0));

    const gui::PlotLine whole = plot->lineOf(0);
    const double summaryStep = whole.positionStep;

    const long long asked = gui::CustomPlot::hyperslabs();
    plot->setVisibleRange(12000.0, 12800.0);
    settleAll();
    h5test::settleFor(gui::CustomPlot::kSettleMilliseconds + 200);
    settleAll();
    CHECK(gui::CustomPlot::hyperslabs() - asked == 0);

    // ...and it resolved rather than stretching, which is the half a count of
    // zero would otherwise be perfectly happy to lie about.
    const gui::PlotLine near = plot->lineOf(0);
    REQUIRE(near.values != nullptr);
    CHECK(near.positionStep < summaryStep);
    double highest = 0.0;
    for (qsizetype i = 0; i < near.count; ++i) {
        highest = std::max(highest, near.values[i]);
    }
    CHECK(highest == Approx(9.0));
}

TEST_CASE_METHOD(PlotFixture, "an entry has to name one line, and says so when it does not",
                 "[custom]")
{
    gui::CustomPlot* plot = tab();

    SECTION("a block is not a line, and the reason names its shape")
    {
        add(plot, QStringLiteral("/matrix[:, :]"));
        CHECK_THAT(errorOf(plot, 0).toStdString(), ContainsSubstring("is one line"));
        CHECK_THAT(errorOf(plot, 0).toStdString(), ContainsSubstring("4"));
        CHECK_FALSE(plot->hasData());
    }

    SECTION("a single element is not a line either")
    {
        add(plot, QStringLiteral("/matrix[0, 0]"));
        CHECK_THAT(errorOf(plot, 0).toStdString(), ContainsSubstring("one element"));
    }

    SECTION("a path that is not there says that rather than anything else")
    {
        add(plot, QStringLiteral("/nowhere[:]"));
        CHECK_THAT(errorOf(plot, 0).toStdString(), ContainsSubstring("nothing at this path"));
    }

    SECTION("a group holds no values")
    {
        add(plot, QStringLiteral("/group"));
        CHECK_THAT(errorOf(plot, 0).toStdString(), ContainsSubstring("dataset"));
    }

    SECTION("text cannot be plotted")
    {
        add(plot, QStringLiteral("/str_vlen[:]"));
        CHECK_THAT(errorOf(plot, 0).toStdString(), ContainsSubstring("only numbers"));
    }

    SECTION("a scalar has no line in it")
    {
        add(plot, QStringLiteral("/scalar_int"));
        CHECK_THAT(errorOf(plot, 0).toStdString(), ContainsSubstring("single value"));
    }

    SECTION("an unbalanced bracket is reported in the subscript parser's words")
    {
        add(plot, QStringLiteral("/series/a[:"));
        CHECK_THAT(errorOf(plot, 0).toStdString(), ContainsSubstring("never closed"));
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

TEST_CASE_METHOD(PlotFixture, "a line can be called something other than its slice", "[custom]")
{
    gui::CustomPlot* plot = tab();
    add(plot, QStringLiteral("/series/a[:]"));
    add(plot, QStringLiteral("/series/b[:]"));

    // A slice says exactly what a line is and nothing about what it means,
    // which is the right default and the wrong label on a plot of six.
    CHECK(plot->seriesLabel(0) == QStringLiteral("/series/a[:]"));

    plot->setAlias(0, QStringLiteral("morning"));
    CHECK(plot->seriesLabel(0) == QStringLiteral("morning"));
    CHECK(plot->data(plot->index(0, 0), gui::CustomPlot::AliasRole).toString() ==
          QStringLiteral("morning"));
    // The other line is untouched, and so is what is actually read: the entry
    // still holds the slice, because that is what the file is asked for.
    CHECK(plot->seriesLabel(1) == QStringLiteral("/series/b[:]"));
    CHECK(plot->data(plot->index(0, 0), gui::CustomPlot::ExpressionRole).toString() ==
          QStringLiteral("/series/a[:]"));
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

TEST_CASE_METHOD(PlotFixture, "a dataset arrives as the lines the plot tab would draw", "[custom]")
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
        CHECK_THAT(said.at(0).at(0).toString().toStdString(), ContainsSubstring("only numbers"));
    }
}

TEST_CASE_METHOD(PlotFixture, "the x of a point comes from whichever axis is chosen", "[custom]")
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

TEST_CASE_METHOD(PlotFixture, "align and stretch decide where a short line goes", "[custom]")
{
    gui::CustomPlot* plot = tab();
    add(plot, QStringLiteral("/series/a[:]"));    // 64 samples, sets the axis
    add(plot, QStringLiteral("/series/half[:]")); // 32 samples, value 2 * i

    REQUIRE(plot->sourcePointCount() == 64);

    const auto scalable = [&](int row) {
        return plot->data(plot->index(row, 0), gui::CustomPlot::ScalableRole).toBool();
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

TEST_CASE_METHOD(PlotFixture, "a time base is read at the same share of itself the line is",
                 "[custom]")
{
    // The case every short fixture hides. /series/time is 64 elements, so it is
    // never thinned and its values sit one per axis position -- which makes an
    // axis lookup indexed by the axis position accidentally right. A time base
    // the length of a real log is summarised like every other line, and then it
    // holds a couple of thousand values for twenty thousand positions: reading
    // it at the position is reading ten times past its end, which drew nine
    // tenths of the line with no x at all and put the tenth that was left
    // against times it was never taken at.
    gui::CustomPlot* plot = tab();
    add(plot, QStringLiteral("/trace[:]"));
    plot->setXExpression(QStringLiteral("/trace_time[:]"));
    plot->setXMode(gui::CustomPlot::Dataset);
    settleAll();

    REQUIRE(plot->xError().isEmpty());
    REQUIRE(plot->xReady());
    REQUIRE(plot->sourcePointCount() == 20000);

    const gui::PlotLine held = plot->lineOf(0);
    REQUIRE(held.count > 1000); // thinned, but nothing like sample for sample

    const QList<QPointF> line = drawn(plot, 0);
    // Every drawn point has an x. Nothing is dropped, because nothing asks the
    // axis for a position it does not have.
    CHECK(line.size() == held.count);
    // And they run the way the time base does: /trace_time is i / 1000, so the
    // line spans zero to twenty seconds and never doubles back.
    CHECK(line.first().x() == Approx(0.0).margin(0.02));
    CHECK(line.last().x() == Approx(20.0).margin(0.05));
    bool ascending = true;
    for (qsizetype i = 1; i < line.size(); ++i) {
        ascending = ascending && line.at(i).x() >= line.at(i - 1).x();
    }
    CHECK(ascending);
}

TEST_CASE("a line longer than the plot draws is thinned by striding its indices", "[custom]")
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

TEST_CASE_METHOD(PlotFixture, "the same slice draws the same in both plots", "[custom][plot]")
{
    // A reader who puts a dataset on the Plot tab and the same slice in a
    // custom tab is looking at one dataset. The two pictures of it differing in
    // any way they can see is a bug in whichever of them they are not looking
    // at -- and they did differ, badly: past four million elements a custom
    // entry gave up on the envelope and fell back to stride, so
    // /plotting/adc_10M drew on the Plot tab as a band of +/-32000 with all
    // seventeen of its impulses in it and in a custom tab as an aliased sine of
    // +/-13000 with none of them.
    //
    // Not "close enough": the same values, in the same order, at the same
    // positions. Both reduce the line the same way because there is only one
    // right way to reduce it.
    REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/trace")));
    gui::DatasetPlot* tabPlot = controller.datasetPlot();
    REQUIRE(tabPlot != nullptr);
    REQUIRE(tabPlot->seriesCount() == 1);

    gui::CustomPlot* custom = tab();
    add(custom, QStringLiteral("/trace[:]"));
    REQUIRE(custom->seriesCount() == 1);

    const gui::PlotLine mine = tabPlot->lineOf(0);
    const gui::PlotLine theirs = custom->lineOf(0);

    REQUIRE(mine.count > 0);
    REQUIRE(theirs.count == mine.count);
    CHECK(theirs.positionStart == mine.positionStart);
    CHECK(theirs.positionStep == Approx(mine.positionStep));
    for (qsizetype i = 0; i < mine.count; ++i) {
        INFO("point " << i);
        REQUIRE(theirs.values[i] == mine.values[i]);
    }

    // Including the one-sample spike, which is the whole reason an envelope is
    // worth its round trips: a stride of twenty lands on 12345 only by luck,
    // and neither of these is relying on luck.
    CHECK(tabPlot->maximum() == Approx(9.0));
    CHECK(custom->maximum() == Approx(9.0));
}

TEST_CASE_METHOD(PlotFixture, "a custom plot reads a line by the hyperslab, not by the bucket",
                 "[custom][cost]")
{
    // The other half of the test above, and the half that was missing.
    //
    // That one asserts the two plots agree about every value, and they always
    // did. What they did not agree about was what it cost to find them out: the
    // Plot tab read a line in hyperslabs of up to gui::kReadRun and folded the
    // buckets out of the buffer, and a custom tab asked HDF5 for one bucket at
    // a time. Same elements, same picture, a thousand times the round trips --
    // which is invisible to a test that compares values and is the whole of
    // what a reader feels on a dataset of any size.
    //
    // Counted rather than timed, for tests/test_cost.cpp's reason: a duration
    // measures the machine, a count measures the program.
    gui::CustomPlot* plot = tab();
    REQUIRE(plot != nullptr);

    const long long before = gui::CustomPlot::hyperslabs();
    add(plot, QStringLiteral("/trace[:]"));
    const long long spent = gui::CustomPlot::hyperslabs() - before;

    // Twenty thousand elements is one hyperslab of sixty-four thousand, however
    // many buckets they are folded into. It used to be one per bucket, which at
    // the default pane is a thousand of them.
    REQUIRE(plot->seriesCount() == 1);
    REQUIRE(plot->sourcePointCount() == 20000);
    CHECK(spent == (20000 + gui::kReadRun - 1) / gui::kReadRun);
    CHECK(spent == 1);

    // And the fold is still a fold: the one-sample spike at 12345 survives it,
    // which is what says the batching changed the reads and not the arithmetic.
    CHECK(plot->thinned());
    CHECK(plot->maximum() == Approx(9.0));

    SECTION("and a closer look is not read at all")
    {
        // The one pass above kept what it read, at the finest bucket the budget
        // affords, so zooming in is a fold of a buffer already in hand. This
        // used to be "a bounded number of hyperslabs rather than one per drawn
        // point", which was the right bound while a closer look was a read; the
        // bound now is none.
        //
        // The Plot tab does the same thing on the same code -- see
        // tests/test_cost.cpp, "a zoom from the whole line to a single sample
        // reads nothing" -- which is what keeps the two tabs one program.
        const gui::PlotLine whole = plot->lineOf(0);
        const double summaryStep = whole.positionStep;

        const long long asked = gui::CustomPlot::hyperslabs();
        plot->setVisibleRange(12000.0, 12800.0);
        settleAll();
        h5test::settleFor(gui::CustomPlot::kSettleMilliseconds + 200);
        settleAll();
        const long long closer = gui::CustomPlot::hyperslabs() - asked;

        CHECK(closer == 0);

        // ...and it resolved rather than stretching, which is the half a count
        // of zero would otherwise be perfectly happy to lie about.
        const gui::PlotLine near = plot->lineOf(0);
        REQUIRE(near.values != nullptr);
        CHECK(near.positionStep < summaryStep);
        // The spike is still in it: the run covers 12000..12800 and 12345 is
        // inside, so whatever the bucket, one of these values is the spike.
        double highest = 0.0;
        for (qsizetype i = 0; i < near.count; ++i) {
            highest = std::max(highest, near.values[i]);
        }
        CHECK(highest == Approx(9.0));
    }
}

TEST_CASE_METHOD(PlotFixture, "a wider pane is read at a finer bucket", "[custom][plot]")
{
    // What a line is thinned to is a property of the pane it is drawn in, not a
    // constant. A constant is wrong in both directions -- fewer buckets than
    // there are pixel columns draws an envelope as a hatch of separated teeth
    // instead of a band, and more points than can be told apart are read and
    // held for nothing.
    REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/trace")));
    gui::DatasetPlot* tabPlot = controller.datasetPlot();
    gui::CustomPlot* custom = tab();
    add(custom, QStringLiteral("/trace[:]"));

    // Twenty thousand elements into a thousand-odd buckets is a bucket of
    // twenty, which is a thousand of them and two values each -- a little under
    // the budget, because the bucket is a ceiling and the last one is short.
    const int assumed = tabPlot->pointCount();
    CHECK(assumed == 2000);
    CHECK(assumed <= 2 * gui::DatasetPlot::kDefaultColumns);
    REQUIRE(custom->pointCount() == assumed);

    tabPlot->setPaneColumns(2048);
    custom->setPaneColumns(2048);
    // Both debounce: a drag of the window's edge crosses a dozen quanta and is
    // one read at the end of it rather than one per sixty-four pixels.
    h5test::settleFor(gui::CustomPlot::kResizeMilliseconds + 200);
    settleAll();

    // Twice the columns, twice the buckets, and the two still agree.
    CHECK(tabPlot->pointCount() > assumed);
    CHECK(custom->pointCount() == tabPlot->pointCount());

    // ...and a pane narrower than the default asks for less rather than more.
    tabPlot->setPaneColumns(256);
    custom->setPaneColumns(256);
    h5test::settleFor(gui::CustomPlot::kResizeMilliseconds + 200);
    settleAll();
    CHECK(tabPlot->pointCount() < assumed);
    CHECK(custom->pointCount() == tabPlot->pointCount());
}

TEST_CASE_METHOD(PlotFixture, "a tab zoomed a notch at a time keeps the pane covered",
                 "[custom]")
{
    // A touchpad is not a wheel with smaller notches; it is a wheel that sends a
    // hundred of them, so the view crosses the boundary between one held run and
    // the next one step at a time rather than an octave at a time.
    //
    // Asked of the *item* rather than of lineOf(), and that distinction is the
    // whole of the test. lineOf() re-decides which run covers the pane every
    // time it is called, so it is right by construction; what is on screen is
    // whatever fill() last handed over, and fill() runs only when this object
    // says something changed. A missed signal is invisible to a test that asks
    // the model and plain to one that asks the renderer -- and it looks like a
    // line drawn across part of the frame with nothing either side of it, which
    // a further gesture then repairs.
    //
    // So this mirrors PlotSurface exactly: a refill on `changed`, and nothing
    // else. Anything it misses, the reader sees.
    gui::CustomPlot* plot = tab();
    add(plot, QStringLiteral("/trace[:]"));
    REQUIRE(plot->seriesCount() == 1);
    REQUIRE(plot->sourcePointCount() == 20000);

    constexpr double kLength = 20000.0;
    constexpr double kPaneWidth = 844.0;

    gui::PlotItem item;
    item.setWidth(kPaneWidth);
    item.setHeight(300.0);

    bool dirty = true;
    QObject::connect(plot, &gui::CustomPlot::changed, plot, [&dirty] { dirty = true; });
    QObject::connect(plot, &gui::CustomPlot::xAxisChanged, plot, [&dirty] { dirty = true; });

    double zoom = 1.0;
    double pan = 0.0;
    const auto low = [&] { return kLength / 2.0 + pan - kLength / zoom / 2.0; };
    const auto high = [&] { return kLength / 2.0 + pan + kLength / zoom / 2.0; };
    const auto spin = [&](double notches, double at) {
        const double next = std::clamp(zoom * std::pow(1.25, notches), 1.0, kLength / 16.0);
        const double span = kLength / zoom;
        const double held = low() + at * span;
        const double nextSpan = kLength / next;
        const double centre = held - at * nextSpan + nextSpan / 2.0;
        const double room = kLength * (1.0 - 1.0 / next) / 2.0;
        zoom = next;
        pan = std::clamp(centre - kLength / 2.0, -room, room);
    };

    const auto refill = [&] {
        if (!dirty) {
            return;
        }
        dirty = false;
        plot->setVisibleRange(low(), high());
        item.setXMin(low());
        item.setXMax(high());
        item.setYMin(plot->minimum() - 1.0);
        item.setYMax(plot->maximum() + 1.0);
        plot->fill(&item);
    };

    const auto push = [&] {
        plot->setVisibleRange(low(), high());
        item.setXMin(low());
        item.setXMax(high());
        refill();
    };

    /// Where what the item is holding begins and ends, read back through the
    /// pointers it was actually handed.
    const auto covers = [&](const char* when) {
        const QVariantMap left = item.nearestSample(0.0, item.height() / 2.0);
        const QVariantMap right = item.nearestSample(item.width(), item.height() / 2.0);
        INFO(when << ": view " << low() << ".." << high() << " zoom " << zoom);
        REQUIRE(left.value(QStringLiteral("valid")).toBool());
        REQUIRE(right.value(QStringLiteral("valid")).toBool());
        // One drawn point of slack at each edge: a point stands for the run of
        // elements it summarises, and the stroke to the next one leaves the
        // frame rather than stopping inside it.
        const double slack = (high() - low()) / kPaneWidth * 4.0;
        CHECK(left.value(QStringLiteral("x")).toDouble() <= std::max(low(), 0.0) + slack);
        CHECK(right.value(QStringLiteral("x")).toDouble()
              >= std::min(high(), kLength) - slack);
    };

    refill();
    h5test::settleFor(400);
    settleAll();
    refill();
    covers("before anything was touched");

    // In, pausing often enough that the runs really are read and held -- the
    // prefetch keeps several at once, and it is the step from one of them to
    // the next that this is about.
    for (int step = 0; step < 150; ++step) {
        spin(1.0 / 12.0, 0.5);
        push();
        covers("mid-gesture, inwards");
        if (step % 25 == 24) {
            h5test::settleFor(300);
            settleAll();
            refill();
            covers("paused, inwards");
        }
    }

    // ...and back out a notch at a time, which is where the view walks off one
    // held run and onto the next.
    for (int step = 0; step < 150; ++step) {
        spin(-1.0 / 12.0, 0.5);
        push();
        covers("mid-gesture, outwards");
        if (step % 25 == 24) {
            h5test::settleFor(300);
            settleAll();
            refill();
            covers("paused, outwards");
        }
    }
}

TEST_CASE_METHOD(PlotFixture, "an entry the reader has zoomed into is read again", "[custom]")
{
    // The same closer look the plot tab takes, resolved per entry: these lines
    // come from all over the file and need not be the same length, so the run
    // one of them is asked for is a run of its *own* elements.
    gui::CustomPlot* plot = tab();
    add(plot, QStringLiteral("/trace[:]")); // 20000, with a one-sample spike at 12345
    REQUIRE(plot->seriesCount() == 1);

    const gui::PlotLine whole = plot->lineOf(0);
    REQUIRE(whole.count == 2000); // 1000 buckets of twenty, two values each
    CHECK(whole.positionStart == 0.0);
    CHECK(whole.positionStep == Approx(10.0));
    // ...and every one of those is the extreme of a bucket rather than a
    // sample, which is what says whether a marker may be drawn on it.
    CHECK(whole.summarised);
    const double high = plot->maximum();
    CHECK(high == Approx(9.0));

    SECTION("the run on screen, at a finer bucket")
    {
        plot->setVisibleRange(0.0, 4000.0);
        h5test::settleFor(300);
        settleAll();

        const gui::PlotLine closer = plot->lineOf(0);
        CHECK(closer.summarised);
        CHECK(closer.positionStart == 0.0);
        // Twice the visible span, in twice the buckets the pane has columns --
        // the prefetch octave, which reads the same elements and has the next
        // step in already in hand. The plot tab takes the same octave, which is
        // what keeps the two pictures identical.
        CHECK(closer.positionStep == Approx(2.0));
        CHECK(closer.count == 4096);

        const QList<QPointF> points = drawn(plot, 0);
        REQUIRE_FALSE(points.isEmpty());
        CHECK(points.first().x() >= 0.0);
        CHECK(points.last().x() <= 8192.0);
        // ...and the extent is still the whole line's, so the axis holds still
        // while the detail arrives.
        CHECK(plot->maximum() == high);
    }

    SECTION("a run one position apart is still a summary")
    {
        // The case a step cannot answer. An envelope at bucket two puts its
        // pair of extremes one position apart, so the step is exactly what a
        // line of elements has -- and until the line said so itself, that is
        // the zoom where markers appeared on points nobody measured.
        plot->setVisibleRange(0.0, 2000.0);
        h5test::settleFor(300);
        settleAll();

        const gui::PlotLine closer = plot->lineOf(0);
        INFO("step " << closer.positionStep << " count " << closer.count);
        CHECK(closer.positionStep == Approx(1.0));
        CHECK(closer.summarised);
    }

    SECTION("and at the closest look, the file's own samples")
    {
        plot->setVisibleRange(12000.0, 12400.0);
        h5test::settleFor(300);
        settleAll();

        const gui::PlotLine closest = plot->lineOf(0);
        CHECK(closest.positionStep == Approx(1.0));
        CHECK_FALSE(closest.summarised);
        CHECK(closest.positionStart == Approx(11776.0)); // aligned, not the view's edge
        REQUIRE(closest.count == 2048);

        const auto at = static_cast<qsizetype>(12345 - 11776);
        CHECK(closest.values[at] == Approx(9.0));
    }

    SECTION("zooming back out is the summary again, in the same call")
    {
        plot->setVisibleRange(0.0, 4000.0);
        h5test::settleFor(300);
        settleAll();
        REQUIRE(plot->lineOf(0).positionStep == Approx(2.0));

        plot->setVisibleRange(0.0, 20000.0);
        const gui::PlotLine back = plot->lineOf(0);
        CHECK(back.values == whole.values);
        CHECK(back.positionStep == Approx(10.0));
    }

    SECTION("the next step in is already in hand")
    {
        // The prefetch: a run is read finer than the pane needs, so the
        // reader's next step down lands on values that are already here.
        //
        // This used to be asserted as the pointer -- the same buffer, not a new
        // one -- because that was the only way to say "nothing was read"
        // without counting. There is a count now, and it says it directly;
        // which matters, because the step down no longer hands back the *same*
        // buffer. A run is held finer than the pane can show and folded to what
        // it can, so stepping in re-folds it at an octave finer. Different
        // pointer, finer picture, still no read, which is the thing that was
        // being asserted all along.
        plot->setVisibleRange(0.0, 4000.0);
        h5test::settleFor(300);
        settleAll();
        const gui::PlotLine closer = plot->lineOf(0);
        REQUIRE(closer.values != whole.values);

        const long long before = gui::CustomPlot::hyperslabs();
        plot->setVisibleRange(1000.0, 3000.0); // half the span, same centre
        h5test::settleFor(300);
        settleAll();

        const gui::PlotLine stepped = plot->lineOf(0);
        CHECK(gui::CustomPlot::hyperslabs() == before);
        CHECK(stepped.positionStep <= closer.positionStep);
        CHECK(stepped.positionStart == Approx(closer.positionStart));

        // And it is the same data underneath, whichever buffer it is folded
        // into: the fold is exact, so a point of the coarser picture is the
        // extreme of the finer points it was folded from.
        REQUIRE(stepped.count > 0);
        double lowest = stepped.values[0];
        double highest = stepped.values[0];
        for (qsizetype i = 0; i < stepped.count; ++i) {
            lowest = std::min(lowest, stepped.values[i]);
            highest = std::max(highest, stepped.values[i]);
        }
        double wasLowest = closer.values[0];
        double wasHighest = closer.values[0];
        for (qsizetype i = 0; i < closer.count; ++i) {
            wasLowest = std::min(wasLowest, closer.values[i]);
            wasHighest = std::max(wasHighest, closer.values[i]);
        }
        CHECK(lowest >= wasLowest);
        CHECK(highest <= wasHighest);
    }

    SECTION("the octaves out are already in hand")
    {
        // The direction that used to flicker. Zooming out past the run in hand
        // fell back to the whole-line summary -- correct, and as many octaves
        // too coarse as the reader was zoomed in -- and then sharpened a tenth
        // of a second later. The runs read ahead of the reader remove that, and
        // the plot tab does exactly the same thing: see test_cost.
        plot->setVisibleRange(8000.0, 9000.0); // mid-line, so nothing is
        h5test::settleFor(1200);               // answered by the end of the data
        settleAll();
        REQUIRE(plot->lineOf(0).positionStep < whole.positionStep);

        double low = 8000.0;
        double high = 9000.0;
        for (int octave = 1; octave <= 3; ++octave) {
            const double centre = (low + high) / 2.0;
            const double half = high - low;
            low = centre - half;
            high = centre + half;

            // In the same call as the gesture: the picture is already drawn
            // from a run rather than from the whole-line summary, which is what
            // "no flicker" means -- the flicker was never a read, it was the
            // picture coarsening while one was on its way.
            plot->setVisibleRange(low, high);
            CHECK(plot->lineOf(0).positionStep < whole.positionStep);

            h5test::settleFor(400);
            settleAll();
            CHECK(plot->lineOf(0).positionStep < whole.positionStep);
        }
    }

    SECTION("an entry short enough to be drawn whole is never read again")
    {
        add(plot, QStringLiteral("/series/a[:]")); // 64 elements
        const gui::PlotLine before = plot->lineOf(1);
        REQUIRE(before.count == 64);

        plot->setVisibleRange(0.0, 40.0);
        h5test::settleFor(300);
        settleAll();

        const gui::PlotLine after = plot->lineOf(1);
        CHECK(after.values == before.values);
        CHECK(after.positionStart == 0.0);
        CHECK(after.positionStep == Approx(1.0));
    }
}

TEST_CASE_METHOD(PlotFixture, "a stretched entry is looked at closely where it is drawn",
                 "[custom]")
{
    // Stretch spreads a line over the whole axis whatever its length, so the
    // run of elements under the pane is not the run of positions under it. The
    // map is affine and this is it, run backwards and then forwards again.
    gui::CustomPlot* plot = tab();
    add(plot, QStringLiteral("/trace[:]"));        // 20000: the axis is this long
    add(plot, QStringLiteral("/trace[0:10000]"));  // half as long, stretched over it
    plot->setScaling(1, gui::CustomPlot::Stretch);
    settleAll();
    REQUIRE(plot->sourcePointCount() == 20000);

    const gui::PlotLine whole = plot->lineOf(1);
    const qsizetype summarised = whole.count;

    plot->setVisibleRange(0.0, 4000.0);
    h5test::settleFor(300);
    settleAll();

    const gui::PlotLine closer = plot->lineOf(1);
    // Two elements of this entry to one position of the axis, so a run of its
    // elements covers twice as much axis as it would under align -- which is
    // the whole of what stretching means, applied to a piece of a line.
    CHECK(closer.positionStep == Approx(1.0 * 19999.0 / 9999.0));
    CHECK(closer.positionStart == Approx(0.0));

    const QList<QPointF> points = drawn(plot, 1);
    REQUIRE_FALSE(points.isEmpty());
    CHECK(points.first().x() >= 0.0);
    CHECK(points.last().x() <= 8200.0);
    // Denser than the summary was over the same stretch of axis.
    CHECK(points.size() > summarised / 4);
}

TEST_CASE_METHOD(PlotFixture, "a tab drawn against a time base that doubles back keeps its "
                              "whole summary",
                 "[custom]")
{
    // A time base is a lookup table rather than an affine map, so the way back
    // from a range of x to a range of elements is a bisection -- and a
    // bisection of a run that is not sorted answers nothing. `/trace` is a sine
    // with a spike in it, so it is the same value at a hundred places and there
    // is no run to narrow to. Zooming there does what it always did: it
    // stretches, and nothing is read.
    gui::CustomPlot* plot = tab();
    add(plot, QStringLiteral("/trace[:]"));
    plot->setXMode(gui::CustomPlot::Dataset);
    plot->setXExpression(QStringLiteral("/trace[:]"));
    settleAll();
    REQUIRE(plot->xReady());

    const gui::PlotLine before = plot->lineOf(0);
    const long long asked = gui::CustomPlot::hyperslabs();
    plot->setVisibleRange(-0.5, 0.5);
    h5test::settleFor(300);
    settleAll();

    const gui::PlotLine after = plot->lineOf(0);
    CHECK(after.values == before.values);
    CHECK(after.count == before.count);
    CHECK(after.positionStart == 0.0);
    CHECK(gui::CustomPlot::hyperslabs() - asked == 0);
    CHECK_FALSE(plot->drawingAxis().hasCloser());
}

TEST_CASE_METHOD(PlotFixture, "a tab drawn against a time base is read closer where the reader "
                              "zooms in",
                 "[custom][plot]")
{
    // The bug this is here for: a time series was the one axis of the three
    // that could not be zoomed at all. Both halves of the picture stood still
    // -- the line, because a range of x was refused outright, and the axis,
    // because a time base was read once at a pane's worth of points and never
    // again. The second is the half that is easy to miss: resolving the line
    // under an axis that has not resolved hands every sample in a column the
    // same x, and the curve draws as a staircase of vertical treads.
    //
    // /trace is 20000 elements and /trace_time is i/1000 over the same 20000,
    // so an element of the line and a second of the axis are the same index and
    // every assertion below can be written in either.
    gui::CustomPlot* plot = tab();
    add(plot, QStringLiteral("/trace[:]"));
    plot->setXExpression(QStringLiteral("/trace_time[:]"));
    plot->setXMode(gui::CustomPlot::Dataset);
    settleAll();
    REQUIRE(plot->xError().isEmpty());
    REQUIRE(plot->xReady());
    REQUIRE(plot->sourcePointCount() == 20000);

    const gui::PlotLine whole = plot->lineOf(0);
    REQUIRE(whole.positionStep == Approx(10.0)); // 1000 buckets of twenty
    const double extent = plot->maximum();

    SECTION("the line resolves, and so does the axis under it")
    {
        // The spike is element 12345, which is 12.345 seconds in.
        plot->setVisibleRange(12.3, 12.4);
        h5test::settleFor(300);
        settleAll();

        const gui::PlotLine closest = plot->lineOf(0);
        CHECK(closest.positionStep == Approx(1.0)); // the file's own samples
        const gui::PlotAxis axis = plot->drawingAxis();
        CHECK(axis.hasCloser());
        CHECK(axis.closerStep == Approx(1.0));

        // Every drawn point sits at the time the file records for the element
        // it is, and no two of them share one. That pair is the whole of what
        // "the axis resolved too" means: before, the hundred samples on screen
        // took their x from one drawn point of the summary and landed on top of
        // one another.
        const QList<QPointF> points = drawn(plot, 0);
        REQUIRE(points.size() > 100);
        int shared = 0;
        for (qsizetype i = 1; i < points.size(); ++i) {
            if (points.at(i).x() == points.at(i - 1).x()) {
                ++shared;
            }
        }
        CHECK(shared == 0);
        for (qsizetype i = 0; i < points.size(); ++i) {
            const auto element = static_cast<long long>(
                std::llround(closest.positionStart + static_cast<double>(i)));
            CHECK(points.at(i).x() == Approx(static_cast<double>(element) / 1000.0));
        }

        // The spike is in it, at its own time, and the extent is still the
        // whole line's so the y axis holds still while detail arrives.
        const auto at = static_cast<qsizetype>(12345 - std::llround(closest.positionStart));
        REQUIRE(at >= 0);
        REQUIRE(at < closest.count);
        CHECK(closest.values[at] == Approx(9.0));
        CHECK(plot->maximum() == extent);
    }

    SECTION("and none of it costs a read")
    {
        // The pyramid's whole claim, now made of the axis as well: the time
        // base was held by the one pass that read it, so every fold of it is
        // arithmetic over a buffer already in hand.
        const long long asked = gui::CustomPlot::hyperslabs();
        double low = 0.0;
        double high = 20.0;
        for (int frame = 0; frame < 20; ++frame) {
            const double span = (high - low) / 2.0;
            low = 12.345 - span / 2.0;
            high = 12.345 + span / 2.0;
            plot->setZoomFocus(12.345, 2.0);
            plot->setVisibleRange(low, high);
            QCoreApplication::processEvents();
            CHECK(plot->lineOf(0).count > 1);
        }
        h5test::settleFor(300);
        settleAll();
        CHECK(gui::CustomPlot::hyperslabs() - asked == 0);
    }

    SECTION("zooming back out is the whole summary again, in the same call")
    {
        plot->setVisibleRange(12.3, 12.4);
        h5test::settleFor(300);
        settleAll();
        REQUIRE(plot->lineOf(0).positionStep == Approx(1.0));

        plot->setVisibleRange(0.0, 20.0);
        const gui::PlotLine back = plot->lineOf(0);
        CHECK(back.values == whole.values);
        CHECK(back.positionStep == Approx(10.0));
        CHECK_FALSE(plot->drawingAxis().hasCloser());
    }
}

namespace {

/// /trace, element for element: see h5test::writeFixture.
double traceAt(long long i)
{
    return i == 12345 ? 9.0 : std::sin(static_cast<double>(i) / 300.0);
}

} // namespace

TEST_CASE_METHOD(PlotFixture, "a tab on a logarithmic x axis is folded per column, on every axis",
                 "[custom][plot][log]")
{
    // The custom tab's half of the logarithmic fold -- see LogColumns in
    // PlotLevels.hpp. It has two things the Plot tab does not: a line stretched
    // over an axis longer than itself, and a time base, where the way from an x
    // back to an element is a search rather than a division. Both have to land
    // every point where the axis would, or the fold is right about the values
    // and wrong about where they are.
    gui::CustomPlot* plot = tab();
    add(plot, QStringLiteral("/trace[:]"));
    plot->setPaneColumns(800);
    h5test::settleFor(300);
    settleAll();
    plot->setXLog(true);

    SECTION("against a time base")
    {
        // /trace_time is i / 1000, so the first element is at zero -- which
        // this axis has no place for -- and the rest run four decades from a
        // thousandth of a second to twenty.
        plot->setXExpression(QStringLiteral("/trace_time[:]"));
        plot->setXMode(gui::CustomPlot::Dataset);
        settleAll();
        REQUIRE(plot->xReady());

        // The axis starts at the first time above zero, which is the second
        // element -- not at the end of the summary's first bucket, which is
        // where the smallest positive value of a summary is.
        CHECK(plot->xPositiveMinimum() == 0.001);

        const long long asked = gui::CustomPlot::hyperslabs();
        plot->setVisibleRange(0.001, 19.999);
        const gui::PlotLine line = plot->lineOf(0);
        REQUIRE(line.xs != nullptr);
        REQUIRE(line.count > 0);
        // The first element with a place, drawn as itself at its own time --
        // where the time base's summary used to put the first drawn point ten
        // milliseconds along, and the first two decades had nothing in them.
        CHECK(line.xs[0] == Approx(0.001));
        CHECK(line.values[0] == Approx(traceAt(1)));

        // Every point sits at a time the file records, and holds a value of an
        // element within a column or two of it.
        const double column = std::log2(19.999 / 0.001) / 800.0;
        int raw = 0;
        for (qsizetype i = 0; i < line.count; ++i) {
            const double x = line.xs[i];
            INFO("point " << i << " at " << x);
            REQUIRE(std::abs(x * 1000.0 - std::round(x * 1000.0)) < 1e-6);
            const auto element = static_cast<long long>(std::llround(x * 1000.0));
            if (line.values[i] == traceAt(element)) {
                ++raw;
            }
            bool nearby = false;
            const auto reach = static_cast<long long>(std::ceil(x * 1000.0 * column * 2.0)) + 1;
            for (long long e = std::max(element - reach, 0LL); e <= element + reach && !nearby;
                 ++e) {
                nearby = line.values[i] == traceAt(e);
            }
            CHECK(nearby);
        }
        // The left of the pane is the elements themselves.
        CHECK(raw > 50);

        // Zoomed about the spike, through an octave and under it: nothing read.
        double low = 0.001;
        double high = 19.999;
        for (int frame = 0; frame < 16; ++frame) {
            low = std::exp2((std::log2(low) + std::log2(12.345)) / 2.0);
            high = std::exp2((std::log2(high) + std::log2(12.345)) / 2.0);
            plot->setZoomFocus(12.345, 2.0);
            plot->setVisibleRange(low, high);
            CHECK(plot->lineOf(0).count > 1);
        }
        h5test::settleFor(300);
        settleAll();
        CHECK(gui::CustomPlot::hyperslabs() - asked == 0);
    }

    SECTION("on a stated range")
    {
        plot->setXStart(0.5);
        plot->setXStep(0.25);
        plot->setVisibleRange(0.75, 5000.25);
        const gui::PlotLine line = plot->lineOf(0);
        REQUIRE(line.xs != nullptr);
        // x = 0.5 + i / 4, so element 1 is the first inside the window and the
        // margin before it holds element 0.
        CHECK(line.xs[0] == Approx(0.5));
        for (qsizetype i = 0; i < line.count; ++i) {
            const double element = (line.xs[i] - 0.5) * 4.0;
            INFO("point " << i << " at " << line.xs[i]);
            CHECK(element >= 0.0);
            CHECK(element < 20000.0);
        }
    }

    SECTION("stretched over an axis longer than itself")
    {
        // /series/half is 32 elements spread over the 20000 of /trace, so each
        // of its elements is 645 positions along the axis and the line's last
        // element sits at the axis's last position.
        add(plot, QStringLiteral("/series/half[:]"));
        plot->setScaling(1, gui::CustomPlot::Stretch);
        settleAll();
        plot->setVisibleRange(1.0, 19999.0);
        const gui::PlotLine line = plot->lineOf(1);
        REQUIRE(line.xs != nullptr);
        REQUIRE(line.count == 32 - 1); // every element but the one at x = 0
        const double scale = 19999.0 / 31.0;
        for (qsizetype i = 0; i < line.count; ++i) {
            INFO("point " << i);
            CHECK(line.xs[i] == Approx(static_cast<double>(i + 1) * scale));
            CHECK(line.values[i] == Approx(2.0 * static_cast<double>(i + 1)));
        }
    }

    SECTION("zoomed in under an octave, the runs take over again")
    {
        plot->setVisibleRange(12300.0, 12400.0);
        h5test::settleFor(300);
        settleAll();
        const gui::PlotLine line = plot->lineOf(0);
        CHECK(line.xs == nullptr);
        CHECK(line.positionStep == Approx(1.0));
    }
}

TEST_CASE_METHOD(PlotFixture, "a descending time base is a time base", "[custom]")
{
    // /series/b is 100 - i: monotonic, and the other way up. The axis is drawn
    // right to left and the inversion runs the same way round; nothing above
    // the bisection cares which.
    gui::CustomPlot* plot = tab();
    add(plot, QStringLiteral("/series/a[:]")); // 64 elements, i
    plot->setXExpression(QStringLiteral("/series/b[:]"));
    plot->setXMode(gui::CustomPlot::Dataset);
    settleAll();
    REQUIRE(plot->xReady());

    // Sixty-four elements are drawn sample for sample already, so there is
    // nothing finer to resolve to -- what is under test is that the view is
    // inverted at all, which is what levelView() refuses when it cannot be.
    const QList<QPointF> points = drawn(plot, 0);
    REQUIRE(points.size() == 64);
    CHECK(points.first().x() == Approx(100.0));
    CHECK(points.last().x() == Approx(37.0));
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

TEST_CASE_METHOD(PlotFixture, "a crowded plot is asked about rather than refused", "[custom]")
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

TEST_CASE_METHOD(PlotFixture, "a custom plot has no window to go back to", "[custom]")
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

TEST_CASE_METHOD(PlotFixture, "a saved view remembers the member it was drawing",
                 "[custom][member]")
{
    // A view is text, and the chain is part of the text: the grammar is a
    // superset of the one every view already in a settings file was written
    // under, so nothing had to be migrated and nothing can stop reading.
    gui::CustomPlot* plot = tab();
    add(plot, QStringLiteral("/compound[:].value"));
    REQUIRE(plot->hasData());
    REQUIRE(set()->saveView(QStringLiteral("the values"), 0, {}).isEmpty());

    plot->removeEntry(0);
    settleAll();
    REQUIRE(plot->empty());

    set()->restoreView(QStringLiteral("the values"), 0);
    settleAll();
    CHECK(plot->sourceSeriesCount() == 1);
    CHECK(plot->seriesLabel(0) == QStringLiteral("/compound[:].value"));
    CHECK(errorOf(plot, 0).isEmpty());
    CHECK(plot->minimum() == 1.5);

    // And the view knows it can still be drawn, which is a question about the
    // *path* -- the chain rides along inside the entry, where it belongs.
    set()->checkView(QStringLiteral("the values"));
    settleAll();
    CHECK(set()->stateOf(QStringLiteral("the values"))
          == gui::CustomPlotSet::FullMatch);
}

TEST_CASE_METHOD(PlotFixture, "a line can be a pipeline", "[custom][postproc]")
{
    gui::CustomPlot* plot = tab();
    REQUIRE(plot != nullptr);
    const auto expressionOf = [plot](int row) {
        return plot->data(plot->index(row, 0), gui::CustomPlot::ExpressionRole).toString();
    };
    const auto postprocessed = [plot](int row) {
        return plot->data(plot->index(row, 0), gui::CustomPlot::PostprocessRole).toBool();
    };

    SECTION("and draws what the pipeline leaves")
    {
        // /matrix is r*10 + c over 4 x 3, so the largest of each row is its
        // last column: 2, 12, 22, 32.
        REQUIRE(plot->addScript(QStringLiteral("/matrix.max(1)")) == 0);
        settleAll();
        CHECK(errorOf(plot, 0).isEmpty());
        CHECK(postprocessed(0));
        CHECK(plot->pointCount() == 4);
        CHECK(plot->minimum() == 2.0);
        CHECK(plot->maximum() == 32.0);
        // Kept formatted, a step to a line, and named on one line in the
        // legend, because a label that breaks is not a label.
        CHECK(expressionOf(0) == QStringLiteral("/matrix\n.max(1)"));
        CHECK(plot->seriesLabel(0) == QStringLiteral("/matrix.max(1)"));
    }

    SECTION("of a member, named with select")
    {
        // /compound's values are 1.5 and 2.5, so their running total ends on 4.
        REQUIRE(plot->addScript(QStringLiteral("/compound\n.select(value)\n.cumsum")) == 0);
        settleAll();
        CHECK(errorOf(plot, 0).isEmpty());
        CHECK(plot->pointCount() == 2);
        CHECK(plot->maximum() == 4.0);
    }

    SECTION("and says so when what it leaves is not a line")
    {
        REQUIRE(plot->addScript(QStringLiteral("/matrix\n.abs")) == 0);
        settleAll();
        CHECK_THAT(errorOf(plot, 0).toStdString(), ContainsSubstring("4 × 3"));
        CHECK_THAT(errorOf(plot, 0).toStdString(), ContainsSubstring("one dimension"));
        CHECK_FALSE(plot->hasData());

        // ...and the box says it while it is typed, once the path is known.
        CHECK_THAT(plot->entryError(0, QStringLiteral("/matrix\n.max(0)\n.max(0)"))
                       .toStdString(),
                   ContainsSubstring("single value"));
        CHECK(plot->entryError(0, QStringLiteral("/matrix\n.max(0)")).isEmpty());
    }

    SECTION("ticking it rewrites the slice as the script that says the same thing")
    {
        add(plot, QStringLiteral("/series/a[0:10]"));
        REQUIRE(plot->pointCount() == 10);
        plot->setPostprocess(0, true);
        settleAll();
        CHECK(postprocessed(0));
        CHECK(expressionOf(0) == QStringLiteral("/series/a\n.slice(0:10)"));
        CHECK(errorOf(plot, 0).isEmpty());
        CHECK(plot->pointCount() == 10);
        CHECK(plot->maximum() == 9.0);

        // /series/a is 0..63, so the running total of its first ten is 45.
        plot->setExpression(0, expressionOf(0) + QStringLiteral(".cumsum"));
        settleAll();
        CHECK(plot->maximum() == 45.0);

        SECTION("and unticking it writes the slice back, and ticking brings the steps back")
        {
            plot->setPostprocess(0, false);
            settleAll();
            CHECK_FALSE(postprocessed(0));
            CHECK(expressionOf(0) == QStringLiteral("/series/a[0:10]"));
            CHECK(plot->maximum() == 9.0);

            plot->setPostprocess(0, true);
            settleAll();
            CHECK(expressionOf(0) == QStringLiteral("/series/a\n.slice(0:10)\n.cumsum"));
            CHECK(plot->maximum() == 45.0);
        }
    }
}

TEST_CASE_METHOD(PlotFixture, "a pipeline of no steps costs what its slice costs",
                 "[custom][postproc][cost]")
{
    // Ticking the box on a line must not change what reading it costs: a
    // script with nothing but a slice in it is streamed as the slice is, never
    // materialised. Read for read, as the member case is held.
    gui::CustomPlot* plain = tab();
    const long long beforePlain = gui::CustomPlot::hyperslabs();
    add(plain, QStringLiteral("/trace[:]"));
    const long long plainReads = gui::CustomPlot::hyperslabs() - beforePlain;

    gui::CustomPlot* scripted = set()->plotAt(set()->addPlot());
    settleAll();
    REQUIRE(scripted != nullptr);
    const long long beforeScript = gui::CustomPlot::hyperslabs();
    REQUIRE(scripted->addScript(QStringLiteral("/trace\n.slice(:)")) == 0);
    settleAll();
    const long long scriptReads = gui::CustomPlot::hyperslabs() - beforeScript;

    REQUIRE(errorOf(plain, 0).isEmpty());
    REQUIRE(errorOf(scripted, 0).isEmpty());
    CHECK(scriptReads == plainReads);
    CHECK(scripted->minimum() == plain->minimum());
    CHECK(scripted->maximum() == plain->maximum());
    CHECK(scripted->pointCount() == plain->pointCount());
    CHECK(scripted->sourcePointCount() == 20000);
}

TEST_CASE_METHOD(PlotFixture, "a saved view remembers which lines are pipelines",
                 "[custom][postproc][views]")
{
    gui::CustomPlot* plot = tab();
    REQUIRE(plot->addScript(QStringLiteral("/series/a\n.cumsum")) == 0);
    add(plot, QStringLiteral("/series/b[:]"));
    REQUIRE(plot->maximum() == 2016.0); // 0 + 1 + ... + 63
    REQUIRE(set()->saveView(QStringLiteral("totals"), 0, {}).isEmpty());

    plot->clearEntries();
    settleAll();
    set()->restoreView(QStringLiteral("totals"), 0);
    settleAll();
    REQUIRE(plot->sourceSeriesCount() == 2);
    CHECK(plot->data(plot->index(0, 0), gui::CustomPlot::PostprocessRole).toBool());
    CHECK_FALSE(plot->data(plot->index(1, 0), gui::CustomPlot::PostprocessRole).toBool());
    CHECK(plot->maximum() == 2016.0);

    // A slice is written down as it always was, with no flag beside it, so a
    // view saved before any line could be a pipeline reads back unchanged.
    const QVariantList rows = plot->state().value(QStringLiteral("entries")).toList();
    CHECK(rows.at(0).toMap().value(QStringLiteral("postprocess")).toBool());
    CHECK_FALSE(rows.at(1).toMap().contains(QStringLiteral("postprocess")));

    set()->checkView(QStringLiteral("totals"));
    settleAll();
    CHECK(set()->stateOf(QStringLiteral("totals")) == gui::CustomPlotSet::FullMatch);
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
    CHECK(set()->stateOf(QStringLiteral("both")) == gui::CustomPlotSet::NoMatch);
}

TEST_CASE_METHOD(PlotFixture, "a saved view is put into whichever tab is open", "[custom]")
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
        CHECK(into->data(into->index(1, 0), gui::CustomPlot::ScalingRole).toInt() ==
              static_cast<int>(gui::CustomPlot::Stretch));
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
        REQUIRE(plots->saveView(QStringLiteral("stale"), plots->indexOfName(broken->name()), {})
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
        CHECK(plots->viewNames() == QStringList{QStringLiteral("other"), QStringLiteral("pair")});

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
        CHECK(plots->stateOf(QStringLiteral("both")) == gui::CustomPlotSet::FullMatch);
    }

    SECTION("some of it is")
    {
        add(from, QStringLiteral("/series/a[:]"));
        add(from, QStringLiteral("/gone_away[:]"));
        REQUIRE(plots->saveView(QStringLiteral("half"), source, {}).isEmpty());
        CHECK(plots->stateOf(QStringLiteral("half")) == gui::CustomPlotSet::PartialMatch);
    }

    SECTION("none of it is")
    {
        add(from, QStringLiteral("/gone_away[:]"));
        add(from, QStringLiteral("/also_gone[:]"));
        REQUIRE(plots->saveView(QStringLiteral("stale"), source, {}).isEmpty());
        CHECK(plots->stateOf(QStringLiteral("stale")) == gui::CustomPlotSet::NoMatch);
    }

    SECTION("and a view nobody saved matches nothing")
    {
        CHECK(plots->stateOf(QStringLiteral("never")) == gui::CustomPlotSet::NoMatch);
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

    CHECK(plots->viewNames() == QStringList{QStringLiteral("z green"), QStringLiteral("a yellow"),
                                            QStringLiteral("z yellow"),
                                            QStringLiteral("a red one")});
}

TEST_CASE_METHOD(PlotFixture, "a saved view is still there next session", "[custom][views]")
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

    const QVariantMap drawing{
        {QStringLiteral("showMarkers"), true},
        {QStringLiteral("colorSingle"), QVariant::fromValue(QColor(Qt::red))}};
    {
        gui::CustomPlotSet writing;
        const int index = writing.addPlot();
        gui::CustomPlot* plot = writing.plotAt(index);
        REQUIRE(plot->addExpression(QStringLiteral("/series/a[:]")) == 0);
        plot->setAlias(0, QStringLiteral("morning"));
        plot->setEntryColor(0, QColor(Qt::magenta));
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
    CHECK(back->data(back->index(0, 0), gui::CustomPlot::ExpressionRole).toString() ==
          QStringLiteral("/series/a[:]"));
    CHECK(back->name() == QStringLiteral("runs"));
    // ...and the colour the reader gave that line, which travels in the plot's
    // own state rather than in the drawing settings beside it: it is a
    // property of the line and not of how the tab was being looked at.
    CHECK(back->seriesOverride(0).value<QColor>() == QColor(Qt::magenta));

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
        CHECK(QColor(came.value(QStringLiteral("colorSingle")).toString()) == QColor(Qt::red));
    }

    SECTION("and forgetting one forgets it for good")
    {
        reading.removeView(QStringLiteral("kept"));
        gui::CustomPlotSet third;
        CHECK(third.viewNames().isEmpty());
    }
}

TEST_CASE_METHOD(PlotFixture, "a line keeps the colour the reader gave it", "[custom]")
{
    gui::CustomPlotSet* plots = set();
    const int index = plots->addPlot();
    gui::CustomPlot* plot = plots->plotAt(index);
    REQUIRE(plot->addExpression(QStringLiteral("/series/a[:]")) == 0);
    REQUIRE(plot->addExpression(QStringLiteral("/series/b[:]")) == 1);
    settleAll();

    // Nothing given is not the same answer as a colour: in QML the first is
    // `undefined` and the second is a colour the reader could have chosen.
    CHECK_FALSE(plot->seriesOverride(0).isValid());
    CHECK_FALSE(plot->seriesOverride(1).isValid());
    // ...and neither is a row that does not exist.
    CHECK_FALSE(plot->seriesOverride(7).isValid());

    SECTION("it is stored, announced, and clears back to nothing")
    {
        QSignalSpy changed(plot, &gui::CustomPlot::changed);
        plot->setEntryColor(0, QColor(Qt::magenta));

        CHECK(plot->seriesOverride(0).value<QColor>() == QColor(Qt::magenta));
        CHECK(plot->data(plot->index(0, 0), gui::CustomPlot::ColourRole).value<QColor>() ==
              QColor(Qt::magenta));
        // Saying it about one line says nothing about the other.
        CHECK_FALSE(plot->seriesOverride(1).isValid());
        // The surface restyles on `changed`; without it the line keeps the
        // colour it had until something else happened to move.
        CHECK(changed.count() == 1);

        // Said twice is said once: nothing is re-read and nothing is redrawn.
        plot->setEntryColor(0, QColor(Qt::magenta));
        CHECK(changed.count() == 1);

        plot->clearEntryColor(0);
        CHECK_FALSE(plot->seriesOverride(0).isValid());
        CHECK(changed.count() == 2);
    }

    SECTION("and travels through the plot's own state")
    {
        plot->setEntryColor(1, QColor(Qt::green));
        const QVariantMap state = plot->state();

        const QVariantList rows = state.value(QStringLiteral("entries")).toList();
        REQUIRE(rows.size() == 2);
        // Written only where there is one, so a view whose lines take the
        // cycle is the same document it was before any of this existed.
        CHECK_FALSE(rows.at(0).toMap().contains(QStringLiteral("colour")));
        CHECK(rows.at(1).toMap().value(QStringLiteral("colour")).value<QColor>() ==
              QColor(Qt::green));

        const int into = plots->addPlot();
        plots->plotAt(into)->setState(state);
        CHECK_FALSE(plots->plotAt(into)->seriesOverride(0).isValid());
        CHECK(plots->plotAt(into)->seriesOverride(1).value<QColor>() == QColor(Qt::green));
    }

    SECTION("a view saved before any of this existed reads back unchanged")
    {
        // The migration, which is no migration at all: an entry with no colour
        // in it is an entry whose line takes the cycle, which is what every
        // view written before 0.6.3 says and what most of them will go on
        // saying. Written out by hand rather than round-tripped, because what
        // is being tested is the *old* shape.
        const QVariantMap old{{QStringLiteral("name"), QStringLiteral("before")},
                              {QStringLiteral("entries"),
                               QVariantList{QVariantMap{
                                   {QStringLiteral("expression"), QStringLiteral("/series/a[:]")},
                                   {QStringLiteral("alias"), QStringLiteral("morning")},
                                   {QStringLiteral("scaling"), QStringLiteral("align")},
                                   {QStringLiteral("drawn"), true}}}},
                              {QStringLiteral("xMode"), QStringLiteral("index")},
                              {QStringLiteral("xExpression"), QString()}};

        const int into = plots->addPlot();
        plots->plotAt(into)->setState(old);
        CHECK(plots->plotAt(into)->sourceSeriesCount() == 1);
        CHECK(plots->plotAt(into)->seriesLabel(0) == QStringLiteral("morning"));
        CHECK_FALSE(plots->plotAt(into)->seriesOverride(0).isValid());
    }

    SECTION("and a stored colour that is not one is nothing rather than black")
    {
        const QVariantMap broken{
            {QStringLiteral("entries"),
             QVariantList{
                 QVariantMap{{QStringLiteral("expression"), QStringLiteral("/series/a[:]")},
                             {QStringLiteral("colour"), QStringLiteral("not a colour")}}}},
            {QStringLiteral("xMode"), QStringLiteral("index")}};

        const int into = plots->addPlot();
        plots->plotAt(into)->setState(broken);
        REQUIRE(plots->plotAt(into)->sourceSeriesCount() == 1);
        CHECK_FALSE(plots->plotAt(into)->seriesOverride(0).isValid());
    }
}

TEST_CASE_METHOD(PlotFixture, "a line can be read against a y axis of its own", "[custom][axes]")
{
    gui::CustomPlotSet* plots = set();
    const int index = plots->addPlot();
    gui::CustomPlot* plot = plots->plotAt(index);
    // series_a is i over 64 samples and series_b is 100 - i: 0..63 and 37..100.
    REQUIRE(plot->addExpression(QStringLiteral("/series/a[:]")) == 0);
    REQUIRE(plot->addExpression(QStringLiteral("/series/b[:]")) == 1);
    settleAll();

    CHECK(plot->sharedSeriesCount() == 2);
    CHECK_FALSE(plot->seriesAxis(0).value(QStringLiteral("separate")).toBool());
    // A row that does not exist is on no axis of its own.
    CHECK_FALSE(plot->seriesAxis(7).value(QStringLiteral("separate")).toBool());

    SECTION("the common axis spans only the lines left on it")
    {
        QSignalSpy changed(plot, &gui::CustomPlot::changed);
        const long long before = gui::CustomPlot::hyperslabs();

        plot->setSeparateAxis(1, true);
        settleAll();

        CHECK(plot->data(plot->index(1, 0), gui::CustomPlot::SeparateAxisRole).toBool());
        CHECK(plot->sharedSeriesCount() == 1);
        CHECK(plot->minimum() == 0.0);
        CHECK(plot->maximum() == 63.0);
        // ...while the line on its own axis is the axis that line would have
        // if it were the only one on the plot.
        const QVariantMap own = plot->seriesAxis(1);
        CHECK(own.value(QStringLiteral("separate")).toBool());
        CHECK(own.value(QStringLiteral("finite")).toBool());
        CHECK(own.value(QStringLiteral("low")).toDouble() == 37.0);
        CHECK(own.value(QStringLiteral("high")).toDouble() == 100.0);
        // Everything is still drawn, and still counted.
        CHECK(plot->hasData());
        CHECK(plot->pointCount() == 128);
        CHECK(changed.count() >= 1);
        // A different map over the same values: nothing is read again.
        CHECK(gui::CustomPlot::hyperslabs() == before);

        plot->setSeparateAxis(1, false);
        CHECK(plot->sharedSeriesCount() == 2);
        CHECK(plot->maximum() == 100.0);
        CHECK(gui::CustomPlot::hyperslabs() == before);
    }

    SECTION("a plot with every line on its own axis has no common one")
    {
        plot->setSeparateAxis(0, true);
        plot->setSeparateAxis(1, true);
        CHECK(plot->sharedSeriesCount() == 0);
        CHECK(plot->hasData());
        CHECK(plot->seriesAxis(0).value(QStringLiteral("high")).toDouble() == 63.0);
        CHECK(plot->seriesAxis(1).value(QStringLiteral("low")).toDouble() == 37.0);
    }

    SECTION("a plot of one line has one axis, and remembers the request")
    {
        plot->setSeparateAxis(1, true);
        plot->setSeriesVisible(0, false);
        // One line drawn: there is nothing for it to be separate from.
        CHECK_FALSE(plot->seriesAxis(1).value(QStringLiteral("separate")).toBool());
        CHECK(plot->sharedSeriesCount() == 1);
        CHECK(plot->maximum() == 100.0);
        // The box still says what the reader asked for...
        CHECK(plot->data(plot->index(1, 0), gui::CustomPlot::SeparateAxisRole).toBool());

        // ...and a second line brings it back.
        plot->setSeriesVisible(0, true);
        CHECK(plot->seriesAxis(1).value(QStringLiteral("separate")).toBool());
        CHECK(plot->sharedSeriesCount() == 1);

        // Removing the other line is the same case, answered at once rather
        // than when the re-read lands.
        plot->removeEntry(0);
        CHECK_FALSE(plot->seriesAxis(0).value(QStringLiteral("separate")).toBool());
        CHECK(plot->sharedSeriesCount() == 1);
    }

    SECTION("exclude from zooming is a question about a separate axis")
    {
        plot->setAxisFixed(1, true);
        CHECK(plot->data(plot->index(1, 0), gui::CustomPlot::AxisFixedRole).toBool());
        // Asked of a line on the common axis, it has nothing to hold still.
        CHECK_FALSE(plot->seriesAxis(1).value(QStringLiteral("fixed")).toBool());
        plot->setSeparateAxis(1, true);
        CHECK(plot->seriesAxis(1).value(QStringLiteral("fixed")).toBool());
    }

    SECTION("the renderer is handed every line on the common axis")
    {
        // Which axis a line is on is a styling the surface sets after the
        // fill, as it sets the colour; the model hands over values and no map.
        plot->setSeparateAxis(1, true);
        CHECK_FALSE(plot->lineOf(1).ownY);
    }

    SECTION("and both requests travel through the plot's own state")
    {
        plot->setSeparateAxis(1, true);
        plot->setAxisFixed(1, true);
        const QVariantMap state = plot->state();
        const QVariantList rows = state.value(QStringLiteral("entries")).toList();
        REQUIRE(rows.size() == 2);
        // Written only where they say something, so a view whose lines share
        // one axis is the same document it was before separate axes existed.
        CHECK_FALSE(rows.at(0).toMap().contains(QStringLiteral("separateAxis")));
        CHECK_FALSE(rows.at(0).toMap().contains(QStringLiteral("axisFixed")));
        CHECK(rows.at(1).toMap().value(QStringLiteral("separateAxis")).toBool());
        CHECK(rows.at(1).toMap().value(QStringLiteral("axisFixed")).toBool());

        const int into = plots->addPlot();
        gui::CustomPlot* restored = plots->plotAt(into);
        restored->setState(state);
        settleAll();
        CHECK_FALSE(restored->data(restored->index(0, 0), gui::CustomPlot::SeparateAxisRole)
                        .toBool());
        CHECK(restored->data(restored->index(1, 0), gui::CustomPlot::SeparateAxisRole).toBool());
        CHECK(restored->data(restored->index(1, 0), gui::CustomPlot::AxisFixedRole).toBool());
        CHECK(restored->seriesAxis(1).value(QStringLiteral("separate")).toBool());
        CHECK(restored->sharedSeriesCount() == 1);
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
        CHECK(plots->plotAt(index)->xExpression() == QStringLiteral("/cube[0, 0, :]"));
    }
}

TEST_CASE_METHOD(PlotFixture, "a line of the plot tab writes itself as a slice", "[custom][plot]")
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
        auto* setup = qobject_cast<gui::TableSetupModel*>(controller.tableSetupModel());
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

// --- the hand-over to a renderer ------------------------------------------
//
// fill() gives a PlotItem pointers straight into the entries' own vectors and
// copies nothing, which is the whole reason a ten-thousand-line selection does
// not cost a second hundred and sixty megabytes. The price is a contract the
// compiler cannot check: whatever was filled must be emptied before those
// vectors are freed. These cases are that contract, asserted rather than
// commented.

TEST_CASE_METHOD(PlotFixture, "a custom plot hands every drawn entry over at once", "[custom]")
{
    gui::CustomPlot* plot = tab();
    REQUIRE(plot != nullptr);
    add(plot, QStringLiteral("/series/a[:]"));
    add(plot, QStringLiteral("/series/b[:]"));

    gui::PlotItem item;
    plot->fill(&item);
    CHECK(item.lineCount() == 2);

    SECTION("a hidden entry is not handed over")
    {
        plot->setSeriesVisible(0, false);
        settleAll();
        plot->fill(&item);
        CHECK(item.lineCount() == 1);
    }

    SECTION("an empty tab hands over nothing")
    {
        plot->clearEntries();
        settleAll();
        plot->fill(&item);
        CHECK(item.lineCount() == 0);
    }
}

TEST_CASE_METHOD(PlotFixture, "what a custom plot filled is emptied before it is freed", "[custom]")
{
    // The ways an entry stops being a reading of anything: the row removed, the
    // tab cleared, the expression retyped. There the item is emptied, and
    // reporting no lines is the observable form of "it stopped reading".
    //
    // The ways an entry is merely re-read -- a wider pane, a coarser or finer
    // bucket, a line ticked in the legend -- are in the case below, and the
    // difference between the two is the whole design: a coarser reading of the
    // same data is not a reason to blank the pane.
    gui::PlotItem item;

    SECTION("removing the row it was drawing")
    {
        gui::CustomPlot* plot = tab();
        add(plot, QStringLiteral("/series/a[:]"));
        plot->fill(&item);
        REQUIRE(item.lineCount() == 1);
        plot->removeEntry(0);
        CHECK(item.lineCount() == 0);
    }

    SECTION("clearing the tab")
    {
        gui::CustomPlot* plot = tab();
        add(plot, QStringLiteral("/series/a[:]"));
        plot->fill(&item);
        REQUIRE(item.lineCount() == 1);
        plot->clearEntries();
        CHECK(item.lineCount() == 0);
    }

    SECTION("retyping an expression, which re-reads every value")
    {
        gui::CustomPlot* plot = tab();
        add(plot, QStringLiteral("/series/a[:]"));
        plot->fill(&item);
        REQUIRE(item.lineCount() == 1);
        plot->setExpression(0, QStringLiteral("/series/b[:]"));
        CHECK(item.lineCount() == 0);
    }

}

TEST_CASE_METHOD(PlotFixture,
                 "what a custom plot filled goes on being drawn until it is replaced", "[custom]")
{
    // The other half of the contract. Each of these destroys values a renderer
    // is holding a pointer into, and each of them is the same data read again
    // rather than different data -- so the values outlive the change and the
    // pane keeps its picture until fill() hands over the replacement.
    //
    // Asserted by dereferencing rather than by counting: the item still has its
    // lines, and reading through them still gives what it gave. A count alone
    // would pass just as happily over freed memory, which is exactly the
    // failure this is about.
    gui::CustomPlot* plot = tab();
    add(plot, QStringLiteral("/series/a[:]"));
    add(plot, QStringLiteral("/series/b[:]"));

    gui::PlotItem item;
    item.setWidth(400.0);
    item.setHeight(300.0);
    item.setXMin(0.0);
    item.setXMax(64.0);
    item.setYMin(0.0);
    item.setYMax(100.0);
    plot->fill(&item);
    REQUIRE(item.lineCount() == 2);

    const auto readsBack = [&item] {
        const QVariantMap found = item.nearestSample(10.0, 150.0);
        return found.value(QStringLiteral("valid")).toBool()
                   ? found.value(QStringLiteral("y")).toDouble()
                   : std::numeric_limits<double>::quiet_NaN();
    };
    const double drawn = readsBack();
    REQUIRE(std::isfinite(drawn));

    SECTION("reordering, which moves the values out from under the drawing order")
    {
        plot->moveEntry(0, 1);
        CHECK(item.lineCount() == 2);
        CHECK(readsBack() == Catch::Approx(drawn));
    }

    SECTION("the read that replaces every value when it lands")
    {
        plot->invalidate();
        settleAll();
        CHECK(item.lineCount() == 2);
        CHECK(std::isfinite(readsBack()));
        plot->fill(&item);
        CHECK(item.lineCount() == 2);
        CHECK(readsBack() == Catch::Approx(drawn));
    }

    SECTION("hiding the line it was drawing")
    {
        plot->setSeriesVisible(0, false);
        CHECK(item.lineCount() == 2);
        CHECK(readsBack() == Catch::Approx(drawn));
        plot->fill(&item);
        CHECK(item.lineCount() == 1);
    }

    SECTION("the pane changing width, which re-reads every entry")
    {
        plot->setPaneColumns(256);
        // Debounced, so nothing has happened yet -- which is the point of it: a
        // drag of the window's edge crosses a dozen of these.
        CHECK(item.lineCount() == 2);
        CHECK(readsBack() == Catch::Approx(drawn));
        h5test::settleFor(gui::CustomPlot::kResizeMilliseconds + 200);
        settleAll();
        CHECK(item.lineCount() == 2);
        CHECK(std::isfinite(readsBack()));
        plot->fill(&item);
        CHECK(item.lineCount() == 2);
        CHECK(readsBack() == Catch::Approx(drawn));
    }
}

TEST_CASE_METHOD(PlotFixture, "the axis handed over is the one the tab is drawn against",
                 "[custom]")
{
    gui::CustomPlot* plot = tab();
    add(plot, QStringLiteral("/series/a[:]"));

    SECTION("a stated range is a start and a step")
    {
        plot->setXStart(100.0);
        plot->setXStep(0.25);
        const gui::PlotAxis axis = plot->drawingAxis();
        CHECK_FALSE(axis.explicitX());
        CHECK(axis.start == 100.0);
        CHECK(axis.step == 0.25);
    }

    SECTION("a time base is an array, and the line reads it point for point")
    {
        plot->setXMode(gui::CustomPlot::Dataset);
        plot->setXExpression(QStringLiteral("/series/b[:]"));
        settleAll();
        REQUIRE(plot->xReady());

        const gui::PlotAxis axis = plot->drawingAxis();
        CHECK(axis.explicitX());
        CHECK(axis.count == 64);
        // series_b is 100 - i, so the first sample of series_a sits at x = 100.
        CHECK(gui::xOf(plot->lineOf(0), axis, 0) == 100.0);
    }

    SECTION("a time base that has not read draws nothing at all")
    {
        // Rather than falling back to positions: the reader asked for these
        // values against *those* x, and the same line on an axis of indices is
        // a different plot wearing the same label.
        plot->setXMode(gui::CustomPlot::Dataset);
        plot->setXExpression(QStringLiteral("/nothing/here[:]"));
        settleAll();
        REQUIRE_FALSE(plot->xReady());
        CHECK(plot->lineOf(0).count == 0);

        gui::PlotItem item;
        plot->fill(&item);
        CHECK(item.drawnPointCount() == 0);
    }
}

// --- what thinning keeps --------------------------------------------------

namespace {

/// A hundred thousand samples of nothing, with one spike in the middle of them.
///
/// The index is chosen to be exactly what stride sampling misses. A line of
/// this length drawn at two thousand points has a stride of forty-nine, and
/// 54321 is not a multiple of it -- so the old reading of this file drew a flat
/// line at zero and reported an extent of nothing, which is a picture of a
/// dataset that does not exist.
void writeSpike(const std::string& path)
{
    const hid_t file = H5Fcreate(path.c_str(), H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
    REQUIRE(file >= 0);
    const hsize_t dims[1] = {100000};
    const hid_t space = H5Screate_simple(1, dims, nullptr);
    const hid_t set =
        H5Dcreate2(file, "/trace", H5T_IEEE_F64LE, space, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    std::vector<double> values(100000, 0.0);
    values[54321] = 9.0;
    values[76543] = -7.0;
    REQUIRE(H5Dwrite(set, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT, values.data()) >= 0);
    H5Dclose(set);
    H5Sclose(space);
    H5Fclose(file);
}

} // namespace

TEST_CASE("a spike one sample wide survives the thinning", "[custom][plot]")
{
    // The claim the whole exercise rests on, asserted through both plots
    // because they read by different routes and each had to be fixed on its
    // own terms.
    h5test::TempFile temp{"spike"};
    h5test::onH5([&] { writeSpike(temp.path()); });

    gui::AppController controller;
    REQUIRE(h5test::openFileAndSettle(controller, QString::fromStdString(temp.path())));

    SECTION("read as the selected dataset")
    {
        REQUIRE(h5test::selectAndSettle(controller, "/trace"));
        gui::DatasetPlot* plot = controller.datasetPlot();
        REQUIRE(plot->hasData());
        REQUIRE(plot->thinned());
        CHECK(plot->pointCount() <= gui::DatasetPlot::kMaxPoints);

        // The extent is the line's own, not the extent of a sample of it --
        // which is what puts the spike inside the pane rather than off the top
        // of it.
        CHECK(plot->maximum() == 9.0);
        CHECK(plot->minimum() == -7.0);

        // ...and both are among the points actually handed to the renderer.
        const std::vector<QPointF> drawn = gui::samplesOf(plot->lineOf(0), plot->drawingAxis());
        double highest = 0.0;
        double lowest = 0.0;
        for (const QPointF& point : drawn) {
            highest = std::max(highest, point.y());
            lowest = std::min(lowest, point.y());
        }
        CHECK(highest == 9.0);
        CHECK(lowest == -7.0);

        // Where it is drawn is where it is in the data, to within the bucket it
        // was the extreme of.
        double at = 0.0;
        for (const QPointF& point : drawn) {
            if (point.y() == 9.0) {
                at = point.x();
            }
        }
        CHECK(at == Approx(54321.0).margin(64.0));
    }

    SECTION("read as a custom plot entry")
    {
        gui::CustomPlotSet* set = controller.customPlots();
        const int index = set->addPlot();
        PlotFixture::settleAll();
        gui::CustomPlot* plot = set->plotAt(index);
        REQUIRE(plot != nullptr);
        REQUIRE(plot->addExpression(QStringLiteral("/trace[:]")) >= 0);
        PlotFixture::settleAll();

        REQUIRE(plot->hasData());
        REQUIRE(plot->thinned());
        CHECK(plot->pointCount() <= gui::CustomPlot::kMaxPoints);
        CHECK(plot->maximum() == 9.0);
        CHECK(plot->minimum() == -7.0);

        const std::vector<QPointF> drawn = gui::samplesOf(plot->lineOf(0), plot->drawingAxis());
        double highest = 0.0;
        double at = 0.0;
        for (const QPointF& point : drawn) {
            if (point.y() > highest) {
                highest = point.y();
                at = point.x();
            }
        }
        CHECK(highest == 9.0);
        CHECK(at == Approx(54321.0).margin(64.0));
    }
}
