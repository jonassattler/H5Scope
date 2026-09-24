// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

// The fold and the ladder, with nothing open.
//
// Everything in gui::PlotLevels is doubles and a vector: which runs to hold,
// which to draw from, which to read next, which to give up, and how a bucket is
// made out of elements. So it is asserted here directly rather than through a
// model that would have to open a file and cross a thread to be asked -- the
// same argument test_plotprojection.cpp makes about the projection, and the
// reason both of them were split out of the classes that used to hold them.
//
// The ladder is where a zoom stops being jumpy. The inward octaves are read
// towards where the pointer is, which is the one thing the surface always knew
// and never said.

#include "gui/PlotLevels.hpp"
#include "gui/PlotProjection.hpp"
#include "gui/PlotPyramid.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>

using Catch::Approx;

namespace {

const double kNaN = std::numeric_limits<double>::quiet_NaN();

gui::HeldLevel held(long long first, long long span, long long bucket, int columns,
                    bool complete = true)
{
    return gui::HeldLevel{gui::PlotWindow{first, span, bucket, columns}, complete};
}

} // namespace

TEST_CASE("a bucket answers with its two extremes in the order they occurred", "[levels]")
{
    // Descending, so smallest-first would be visible as the line turning the
    // wrong way up -- which is the failure the ordering rule exists for.
    const std::vector<double> falling{4.0, 3.0, 2.0, 1.0};
    std::vector<double> folded;
    gui::reduceBuckets(falling.data(), 4, 4, folded);

    REQUIRE(folded.size() == 2);
    CHECK(folded[0] == 4.0);
    CHECK(folded[1] == 1.0);

    SECTION("and rising the other way")
    {
        const std::vector<double> rising{1.0, 2.0, 3.0, 4.0};
        std::vector<double> out;
        gui::reduceBuckets(rising.data(), 4, 4, out);
        REQUIRE(out.size() == 2);
        CHECK(out[0] == 1.0);
        CHECK(out[1] == 4.0);
    }

    SECTION("a bucket of one element is that element twice")
    {
        const std::vector<double> one{7.0};
        std::vector<double> out;
        gui::reduceBuckets(one.data(), 1, 1, out);
        REQUIRE(out.size() == 2);
        CHECK(out[0] == 7.0);
        CHECK(out[1] == 7.0);
    }

    SECTION("a bucket with nothing finite in it is a gap, not a missing bucket")
    {
        // Dropping it would slide every later bucket left and draw the line
        // across the hole, which is a reading of data that was never taken.
        const std::vector<double> gapped{1.0, 2.0, kNaN, kNaN, 5.0, 6.0};
        std::vector<double> out;
        gui::reduceBuckets(gapped.data(), 6, 2, out);
        REQUIRE(out.size() == 6);
        CHECK(out[0] == 1.0);
        CHECK(std::isnan(out[2]));
        CHECK(std::isnan(out[3]));
        CHECK(out[5] == 6.0);
    }

    SECTION("a last bucket shorter than the rest is summarised from what there is")
    {
        const std::vector<double> odd{1.0, 2.0, 3.0, 4.0, 5.0};
        std::vector<double> out;
        gui::reduceBuckets(odd.data(), 5, 2, out);
        REQUIRE(out.size() == 6);
        CHECK(out[4] == 5.0);
        CHECK(out[5] == 5.0);
    }
}

TEST_CASE("a read of up to kReadRun never stops inside a bucket", "[levels]")
{
    // "An envelope reads every element exactly once" is a count test_cost holds
    // both plots to, and it is this function that keeps it true: a read cut
    // back to whole buckets leaves no bucket half-read for the next one to read
    // again from its start.
    CHECK(gui::wholeBuckets(1000, 64) == 960);
    CHECK(gui::wholeBuckets(64, 64) == 64);
    // A bucket wider than a whole read is the one case that cannot be cut back.
    CHECK(gui::wholeBuckets(50, 64) == 50);
}

TEST_CASE("the finest run that covers the pane is the one drawn", "[levels]")
{
    gui::LevelView view;
    view.length = 100000;
    view.low = 40000.0;
    view.high = 41000.0;
    view.paneBuckets = 512;

    const std::vector<gui::HeldLevel> ladder{
        held(0, 100000, 64, 1563),    // the whole line, coarse
        held(32768, 32768, 16, 2048), // covers, finer
        held(40960, 4096, 2, 2048),   // finer still, but starts inside the view
    };

    // The third is the finest but does not cover 40000, so the second is drawn.
    CHECK(gui::drawnLevel(ladder, view) == 1);

    SECTION("a run that has not come back yet is not drawn from")
    {
        std::vector<gui::HeldLevel> partial = ladder;
        partial[1].complete = false;
        CHECK(gui::drawnLevel(partial, view) == 0);
    }

    SECTION("nothing at all when no run covers")
    {
        const std::vector<gui::HeldLevel> away{held(90000, 4096, 2, 2048)};
        CHECK(gui::drawnLevel(away, view) == -1);
    }
}

TEST_CASE("the ladder reads towards where the reader is zooming", "[levels]")
{
    gui::LevelView view;
    view.length = 1000000;
    view.low = 500000.0;
    view.high = 510000.0;
    view.paneBuckets = 1024;
    view.detailBuckets = 2048;

    // A run that already answers the pane, so what is left is idle work.
    const std::optional<gui::PlotWindow> on =
        gui::windowFor(view.low, view.high, view.length, view.detailBuckets);
    REQUIRE(on.has_value());
    const std::vector<gui::HeldLevel> ladder{held(on->first, on->span, on->bucket, on->columns)};

    SECTION("with no focus it reads outwards, as it always did")
    {
        const gui::PlotFocus none;
        const std::optional<gui::PlotWindow> next = gui::wantedLevel(ladder, view, none);
        REQUIRE(next.has_value());
        // Wider than the run in hand: the octave out is the direction that
        // cannot be had for free.
        CHECK(next->span > on->span);
    }

    SECTION("zooming in, it reads the octaves in towards the pointer")
    {
        gui::PlotFocus focus;
        focus.active = true;
        focus.inward = true;
        // Near the left edge of the pane rather than in the middle, which is
        // the case the view's own centre got wrong.
        focus.position = 501000.0;

        const std::optional<gui::PlotWindow> next = gui::wantedLevel(ladder, view, focus);
        REQUIRE(next.has_value());
        // Finer and narrower than the run in hand...
        CHECK(next->bucket < on->bucket);
        CHECK(next->span < on->span);
        // ...and it covers where the pointer is, which is the whole point.
        CHECK(next->covers(focus.position, focus.position));
    }

    SECTION("the whole inward ladder costs less than one step out")
    {
        // A run costs its span, and each octave in is half the span of the one
        // above it. This is why kFocusOctavesIn can be four where
        // kPrefetchOctaves has to be two.
        gui::PlotFocus focus;
        focus.active = true;
        focus.inward = true;
        focus.position = 505000.0;

        std::vector<gui::HeldLevel> going = ladder;
        long long inward = 0;
        for (int step = 0; step < gui::kFocusOctavesIn; ++step) {
            const std::optional<gui::PlotWindow> next = gui::wantedLevel(going, view, focus);
            REQUIRE(next.has_value());
            if (next->span >= on->span) {
                break; // reached the outward half of the ladder
            }
            inward += next->span;
            going.push_back(held(next->first, next->span, next->bucket, next->columns));
        }
        CHECK(inward > 0);
        CHECK(inward < 2 * on->span);
    }

    SECTION("a run already in hand is never asked for twice")
    {
        gui::PlotFocus focus;
        focus.active = true;
        focus.inward = true;
        focus.position = 505000.0;

        std::vector<gui::HeldLevel> going = ladder;
        for (int step = 0; step < 24; ++step) {
            const std::optional<gui::PlotWindow> next = gui::wantedLevel(going, view, focus);
            if (!next.has_value()) {
                break;
            }
            for (const gui::HeldLevel& already : going) {
                INFO("step " << step);
                REQUIRE_FALSE(already.window == *next);
            }
            going.push_back(held(next->first, next->span, next->bucket, next->columns));
        }
        // ...and it does run out, rather than asking for ever finer runs of a
        // line that has no more to give.
        CHECK(going.size() < 24);
    }
}

TEST_CASE("a pointer that moved gives up the guesses made about the old one", "[levels]")
{
    gui::LevelView view;
    view.length = 1000000;
    view.low = 500000.0;
    view.high = 510000.0;
    view.paneBuckets = 1024;
    view.detailBuckets = 2048;

    const std::optional<gui::PlotWindow> on =
        gui::windowFor(view.low, view.high, view.length, view.detailBuckets);
    REQUIRE(on.has_value());

    const std::vector<gui::HeldLevel> ladder{
        held(0, 1000000, 1024, 977),                        // 0: the way back out
        held(on->first, on->span, on->bucket, on->columns), // 1: on screen
        held(500000, 1024, 1, 1024),                        // 2: read towards the old pointer
    };

    gui::PlotFocus focus;
    focus.active = true;
    focus.inward = true;
    // The reader has moved to the other end of the pane. Level 2 is finer than
    // what is drawn and covers nowhere near this, so it is speculation about
    // somewhere they are not.
    focus.position = 509500.0;

    CHECK(gui::coldestLevel(ladder, view, focus) == 2);

    SECTION("but the way back out is kept, however far from the bucket on screen")
    {
        // Level 0 is further from the pane's bucket than level 2 is, so ranking
        // by distance alone would give it up first -- and it is the run the
        // reader lands on the moment they zoom out, which is the direction that
        // cannot be answered by anything finer.
        std::vector<gui::HeldLevel> without{ladder[0], ladder[1]};
        CHECK(gui::coldestLevel(without, view, focus) == 0);
    }

    SECTION("a guess that still covers the pointer is not given up for it")
    {
        std::vector<gui::HeldLevel> still = ladder;
        still[2] = held(509312, 1024, 1, 1024); // where the pointer actually is
        CHECK(gui::coldestLevel(still, view, focus) != 2);
    }

    SECTION("with no focus it ranks by distance alone, as it always did")
    {
        const gui::PlotFocus none;
        CHECK(gui::coldestLevel(ladder, view, none) == 0);
    }
}

TEST_CASE("a view with nothing to say asks for nothing", "[levels]")
{
    const std::vector<gui::HeldLevel> ladder{held(0, 1024, 1, 1024)};
    const gui::PlotFocus none;

    gui::LevelView empty;
    CHECK_FALSE(empty.usable());
    CHECK_FALSE(gui::wantedLevel(ladder, empty, none).has_value());
    CHECK(gui::drawnLevel(ladder, empty) == -1);

    SECTION("nor does one zoomed out past what a second read could add")
    {
        gui::LevelView whole;
        whole.length = 1000;
        whole.low = 0.0;
        whole.high = 1000.0;
        whole.paneBuckets = 1024;
        whole.detailBuckets = 2048;
        // The whole-line summary is already finer than a bucket, so there is
        // nothing to read.
        CHECK_FALSE(gui::wantedLevel(ladder, whole, none).has_value());
    }
}

// ---------------------------------------------------------------------------
// The pyramid
// ---------------------------------------------------------------------------
//
// One claim holds the whole cache up: a run folded out of a held level is the
// same run a read would have answered with, value for value. Everything below
// checks that against reduceBuckets() over the raw elements, which is what the
// file path does -- so a derived level and a read level are compared the way
// test_customplot compares the two plots.

namespace {

/// A line with the shapes that break a careless fold: a trend, a one-sample
/// spike between any two strides, and a run of gaps.
std::vector<double> testLine(long long count)
{
    std::vector<double> line(static_cast<std::size_t>(count));
    for (long long i = 0; i < count; ++i) {
        line[static_cast<std::size_t>(i)] = std::sin(static_cast<double>(i) / 613.0) * 100.0 +
                                            std::sin(static_cast<double>(i) / 7.0) * 3.0;
    }
    for (const long long at : {13LL, 5011LL, 40009LL}) {
        if (at < count) {
            line[static_cast<std::size_t>(at)] = 9999.0;
        }
    }
    for (long long i = 2000; i < std::min<long long>(2400, count); ++i) {
        line[static_cast<std::size_t>(i)] = kNaN;
    }
    return line;
}

/// What a read of `[first, first + span)` at `bucket` would have answered.
std::vector<double> readWould(const std::vector<double>& line, long long first, long long span,
                              long long bucket)
{
    const long long take = std::min(span, static_cast<long long>(line.size()) - first);
    std::vector<double> want;
    if (bucket == 1) {
        want.assign(line.begin() + first, line.begin() + first + take);
        return want;
    }
    gui::reduceBuckets(line.data() + first, take, bucket, want);
    return want;
}

/// Equal, counting NaN as equal to NaN -- a gap is a value here.
void same(const std::vector<double>& got, const std::vector<double>& want)
{
    REQUIRE(got.size() == want.size());
    for (std::size_t i = 0; i < want.size(); ++i) {
        INFO("at " << i);
        if (std::isnan(want[i])) {
            REQUIRE(std::isnan(got[i]));
        }
        else {
            REQUIRE(got[i] == want[i]);
        }
    }
}

} // namespace

TEST_CASE("an envelope coarsened is an envelope read at that bucket", "[levels][pyramid]")
{
    // The property the cache is built out of, asserted directly rather than
    // through anything that uses it.
    const std::vector<double> line = testLine(9000);

    for (const long long fine : {1LL, 2LL, 8LL, 64LL}) {
        for (const long long factor : {2LL, 4LL, 16LL}) {
            INFO("fine " << fine << " factor " << factor);
            std::vector<double> at;
            gui::reduceBuckets(line.data(), (long long)line.size(), fine, at);

            std::vector<double> coarsened;
            gui::coarsenEnvelope(at.data(), (long long)at.size() / 2, factor, coarsened);

            std::vector<double> read;
            gui::reduceBuckets(line.data(), (long long)line.size(), fine * factor, read);
            same(coarsened, read);
        }
    }
}

TEST_CASE("a pyramid answers every window the way the file would", "[levels][pyramid]")
{
    // Large enough that the build spreads over the pool rather than staying on
    // this thread, which is the path a reader actually takes.
    const std::vector<double> line = testLine(200000);
    const gui::LinePyramid pyramid = gui::pyramidOf(line.data(), (long long)line.size(), 1);

    REQUIRE_FALSE(pyramid.empty());
    CHECK(pyramid.length == 200000);
    CHECK(pyramid.baseBucket() == 1);
    // Finest first, each four times the one below, ending in a handful.
    REQUIRE(pyramid.levels.size() > 3);
    for (std::size_t i = 1; i < pyramid.levels.size(); ++i) {
        CHECK(pyramid.levels[i].bucket == pyramid.levels[i - 1].bucket * 4);
    }
    CHECK(pyramid.levels.back().buckets() <= 1);

    SECTION("at every bucket and every alignment the plot can ask for")
    {
        for (const long long bucket : {1LL, 2LL, 4LL, 8LL, 32LL, 256LL, 4096LL}) {
            for (const long long first : {0LL, 4096LL, 65536LL, 131072LL}) {
                const long long span = std::min<long long>(bucket * 1024, 200000 - first);
                if (span <= 0) {
                    continue;
                }
                INFO("bucket " << bucket << " first " << first);
                const gui::PlotWindow window{first, span, bucket,
                                             static_cast<int>((span + bucket - 1) / bucket)};
                std::vector<double> got;
                REQUIRE(gui::fillWindow(pyramid, window, got));
                same(got, readWould(line, first, span, bucket));
            }
        }
    }

    SECTION("a one-sample spike survives every level of it")
    {
        // The reason the plot reads an envelope and not a stride, asserted all
        // the way up the pyramid rather than only at the bottom.
        for (const gui::PyramidLevel& level : pyramid.levels) {
            const long long at = 5011 / level.bucket;
            if (at >= level.buckets()) {
                continue;
            }
            INFO("bucket " << level.bucket);
            if (level.bucket == 1) {
                CHECK(level.values[static_cast<std::size_t>(at)] == 9999.0);
                continue;
            }
            const double low = level.values[static_cast<std::size_t>(at) * 2];
            const double high = level.values[static_cast<std::size_t>(at) * 2 + 1];
            CHECK((low == 9999.0 || high == 9999.0));
        }
    }

    SECTION("a window finer than the base is refused rather than approximated")
    {
        const gui::LinePyramid coarse = gui::pyramidOf(line.data(), (long long)line.size(), 16);
        REQUIRE(coarse.baseBucket() == 16);
        std::vector<double> got;
        CHECK_FALSE(gui::fillWindow(coarse, gui::PlotWindow{0, 1024, 1, 1024}, got));
        CHECK_FALSE(gui::fillWindow(coarse, gui::PlotWindow{0, 4096, 8, 512}, got));
        // ...and at the base and above it answers as the file would.
        CHECK(gui::fillWindow(coarse, gui::PlotWindow{0, 16384, 16, 1024}, got));
        same(got, readWould(line, 0, 16384, 16));
    }
}

TEST_CASE("a pyramid over a coarse base still answers exactly", "[levels][pyramid]")
{
    // The hundred-million and billion-element case: too large to hold raw, so
    // the base is an envelope and every level above it is derived from that.
    const std::vector<double> line = testLine(100000);
    const gui::LinePyramid pyramid = gui::pyramidOf(line.data(), (long long)line.size(), 8);

    REQUIRE(pyramid.baseBucket() == 8);
    for (const long long bucket : {8LL, 16LL, 32LL, 128LL, 1024LL}) {
        INFO("bucket " << bucket);
        const long long span = std::min<long long>(bucket * 512, 100000);
        std::vector<double> got;
        REQUIRE(gui::fillWindow(
            pyramid, gui::PlotWindow{0, span, bucket, static_cast<int>(span / bucket)}, got));
        same(got, readWould(line, 0, span, bucket));
    }
}

TEST_CASE("the whole-line summary comes out of the pyramid", "[levels][pyramid]")
{
    const std::vector<double> line = testLine(50000);
    const gui::LinePyramid pyramid = gui::pyramidOf(line.data(), (long long)line.size(), 1);

    long long stride = 0;
    double step = 0.0;
    std::vector<double> got;
    REQUIRE(gui::fillWhole(pyramid, 1024, got, stride, step));

    // Exactly what a read at that stride would have answered -- which is what
    // makes this a replacement for the read rather than a second opinion.
    CHECK(stride == (50000 + 1023) / 1024);
    CHECK(step == static_cast<double>(stride) / 2.0);
    same(got, readWould(line, 0, 50000, stride));

    SECTION("and a coarse base rounds the stride up to one it can answer exactly")
    {
        const gui::LinePyramid coarse = gui::pyramidOf(line.data(), (long long)line.size(), 16);
        REQUIRE(gui::fillWhole(coarse, 1024, got, stride, step));
        CHECK(stride % 16 == 0);
        CHECK(stride >= (50000 + 1023) / 1024);
        same(got, readWould(line, 0, 50000, stride));
    }
}

TEST_CASE("a pyramid streamed in pieces is the pyramid of the whole line", "[levels][pyramid]")
{
    // Both plots build their pyramids this way -- a read at a time, of up to
    // kReadRun elements, with whatever does not fill a bucket carried into the
    // next read -- and nothing asserted it against the one-buffer build. A
    // carry that dropped or doubled one element would shift every bucket after
    // it by one, and draw a line that is nearly right everywhere.
    const std::vector<double> line = testLine(50000);
    const auto length = static_cast<long long>(line.size());

    for (const long long base : {1LL, 4LL, 16LL}) {
        // Pieces that land on bucket boundaries, pieces that never do, and one
        // element at a time.
        for (const long long piece : {4096LL, 1000LL, 7LL, 1LL}) {
            INFO("base " << base << " piece " << piece);
            gui::PyramidBuilder builder(length, base);
            for (long long at = 0; at < length; at += piece) {
                builder.add(line.data() + at, std::min(piece, length - at));
            }
            CHECK(builder.taken() == length);
            const gui::LinePyramid streamed = builder.finish();
            const gui::LinePyramid whole = gui::pyramidOf(line.data(), length, base);

            REQUIRE(streamed.length == whole.length);
            REQUIRE(streamed.levels.size() == whole.levels.size());
            for (std::size_t level = 0; level < whole.levels.size(); ++level) {
                INFO("level " << level);
                CHECK(streamed.levels[level].bucket == whole.levels[level].bucket);
                same(streamed.levels[level].values, whole.levels[level].values);
            }
        }
    }
}

TEST_CASE("a pyramid whose reads stopped short answers only for what it holds", "[levels][pyramid]")
{
    // A pyramid is built for the length the line was said to have, and every
    // fold, column and extreme afterwards indexes its base up to that length.
    // A walk that stopped part of the way -- a read that failed on the third
    // hyperslab of ten -- left a base shorter than the length it carried, and
    // those reads went off the end of it. The length is now what was handed
    // in, so what is answered is the part of the line that exists.
    const std::vector<double> line = testLine(20000);
    constexpr long long kPromised = 50000;

    for (const long long base : {1LL, 8LL}) {
        INFO("base " << base);
        gui::PyramidBuilder builder(kPromised, base);
        builder.add(line.data(), static_cast<long long>(line.size()));
        const gui::LinePyramid pyramid = builder.finish();

        CHECK(pyramid.length == 20000);

        // The whole-line summary is of the part there is...
        long long stride = 0;
        double step = 0.0;
        std::vector<double> got;
        REQUIRE(gui::fillWhole(pyramid, 512, got, stride, step));
        same(got, readWould(line, 0, 20000, stride));

        // ...a run reaching past it stops where it does...
        std::vector<double> run;
        REQUIRE(gui::fillWindow(pyramid, gui::PlotWindow{16384, 16384, 16, 1024}, run));
        same(run, readWould(line, 16384, 16384, 16));

        // ...and the extremes of a range over the end are the extremes of
        // what is inside it.
        const gui::Extremes found = gui::extremesOver(pyramid, 19000, kPromised);
        REQUIRE(found.found());
        std::vector<double> tail;
        gui::reduceBuckets(line.data() + 19000, 1000, 1000, tail);
        CHECK(std::min(tail[0], tail[1]) == found.lowest);
        CHECK(std::max(tail[0], tail[1]) == found.highest);
    }
}

TEST_CASE("the base bucket is the finest the budget affords", "[levels][pyramid]")
{
    // A pyramid costs its base and a third again, so the budget decides how
    // fine the base can be -- and below the base the file is the only answer,
    // which is the one thing this number changes.
    CHECK(gui::baseBucketFor(10000000, 1 << 28) == 1); // 10M raw in 256M doubles
    CHECK(gui::baseBucketFor(10000000, 1 << 20) > 1);  // ...and not in one
    CHECK(gui::baseBucketFor(0, 1 << 20) == 1);

    for (const long long length : {1000LL, 10000000LL, 1000000000LL}) {
        for (const long long budget : {1LL << 16, 1LL << 22, 1LL << 28}) {
            const long long base = gui::baseBucketFor(length, budget);
            INFO("length " << length << " budget " << budget << " base " << base);
            CHECK(base >= 1);
            // A power of two, because every window's bucket is one and a level
            // that did not divide them could answer none of them.
            CHECK((base & (base - 1)) == 0);
            // It fits, unless a single bucket does not -- which cannot happen.
            CHECK(gui::pyramidDoubles(length, base) <= budget);
        }
    }
}

TEST_CASE("a pyramid is coarsened in place when the budget is turned down",
          "[levels][pyramid]")
{
    // The direction that is free, and the one that matters: a reader who
    // notices this program holding three gigabytes and turns the RAM budget
    // down wants the memory back now, not when they next select a dataset.
    // Coarsening is exact, so they get it in the same call.
    std::vector<double> line(1 << 16);
    for (std::size_t i = 0; i < line.size(); ++i) {
        line[i] = std::sin(static_cast<double>(i) / 97.0);
    }

    gui::LinePyramid fine =
        gui::pyramidOf(line.data(), static_cast<long long>(line.size()), 1);
    REQUIRE(fine.baseBucket() == 1);
    const std::size_t before = fine.doubles();

    SECTION("what it costs afterwards is what a pyramid at that base costs")
    {
        REQUIRE(gui::coarsenTo(fine, 16));
        CHECK(fine.baseBucket() == 16);
        CHECK(fine.doubles() < before / 8);
        CHECK(static_cast<long long>(fine.doubles())
              <= gui::pyramidDoubles(static_cast<long long>(line.size()), 16));
    }

    SECTION("every picture at or above the new base is the picture it was")
    {
        gui::LinePyramid coarse = fine;
        REQUIRE(gui::coarsenTo(coarse, 16));

        for (const long long bucket : {16LL, 64LL, 256LL, 1024LL}) {
            gui::PlotWindow window{0, static_cast<long long>(line.size()), bucket};
            std::vector<double> before;
            std::vector<double> after;
            REQUIRE(gui::fillWindow(fine, window, before));
            REQUIRE(gui::fillWindow(coarse, window, after));
            INFO("bucket " << bucket);
            CHECK(before == after);
        }
    }

    SECTION("a base already coarse enough is left alone, and one level survives")
    {
        CHECK_FALSE(gui::coarsenTo(fine, 1));
        CHECK(fine.baseBucket() == 1);

        // Past the top: there is nothing finer to give up, and a pyramid of the
        // whole line in a handful of buckets is still a pyramid.
        REQUIRE(gui::coarsenTo(fine, 1LL << 40));
        CHECK(fine.levels.size() == 1);
        CHECK_FALSE(fine.empty());
    }
}

// ---------------------------------------------------------------------------
// A logarithmic x axis: one envelope per column
// ---------------------------------------------------------------------------

TEST_CASE("any run's extremes come out of the pyramid as the elements' own",
          "[levels][pyramid][log]")
{
    // A column of a logarithmic pane is a run of any length starting anywhere,
    // not an aligned bucket -- so this is asked of runs that line up with
    // nothing, across the NaN stretch and the spikes testLine plants.
    const std::vector<double> line = testLine(50000);
    const auto count = static_cast<long long>(line.size());

    SECTION("over a base of one")
    {
        const gui::LinePyramid pyramid = gui::pyramidOf(line.data(), count, 1);
        for (const long long first : {0LL, 1LL, 13LL, 1999LL, 2003LL, 4095LL, 40000LL}) {
            for (const long long take : {1LL, 2LL, 3LL, 17LL, 400LL, 4097LL, 9999LL}) {
                const long long last = std::min(first + take, count);
                INFO("run " << first << ".." << last);
                const gui::Extremes want = gui::extremesOf(line.data(), first, last);
                const gui::Extremes got = gui::extremesOver(pyramid, first, last);
                REQUIRE(got.found() == want.found());
                if (!want.found()) {
                    continue; // a run of nothing but NaN is a gap, and says so
                }
                CHECK(got.lowest == want.lowest);
                CHECK(got.highest == want.highest);
                // ...and in the order they occurred, which is what keeps a
                // falling stretch of the line drawn falling.
                CHECK(got.first() == want.first());
                CHECK(got.second() == want.second());
            }
        }
    }

    SECTION("over a coarse base, rounded out to it")
    {
        const gui::LinePyramid pyramid = gui::pyramidOf(line.data(), count, 8);
        for (const long long first : {0LL, 5LL, 1003LL, 40001LL}) {
            for (const long long take : {1LL, 9LL, 1000LL}) {
                const long long last = std::min(first + take, count);
                const long long from = (first / 8) * 8;
                const long long to = std::min(((last + 7) / 8) * 8, count);
                INFO("run " << first << ".." << last << " rounded to " << from << ".." << to);
                const gui::Extremes want = gui::extremesOf(line.data(), from, to);
                const gui::Extremes got = gui::extremesOver(pyramid, first, last);
                REQUIRE(got.found() == want.found());
                if (want.found()) {
                    CHECK(got.lowest == want.lowest);
                    CHECK(got.highest == want.highest);
                }
            }
        }
    }
}

namespace {

/// Where `x` falls across a logarithmic pane from `low` to `high`, as a
/// fraction of it.
double logFraction(double x, double low, double high)
{
    return (std::log2(x) - std::log2(low)) / (std::log2(high) - std::log2(low));
}

} // namespace

TEST_CASE("a logarithmic pane is folded one envelope per column, from the first element",
          "[levels][pyramid][log]")
{
    // The failure this replaces: a line of a million elements on an axis from
    // 1 to 1e6 was bucketed six hundred elements at a time, so the first three
    // decades -- half the pane -- were one bucket. It drew nothing at all below
    // the first bucket's half-way point and a few straight strokes above it,
    // and no zoom repaired it because every run was bucketed the same way.
    constexpr long long kLength = 1000000;
    constexpr int kColumns = 1000;
    std::vector<double> line(kLength);
    for (long long i = 0; i < kLength; ++i) {
        line[static_cast<std::size_t>(i)] = std::sin(static_cast<double>(i) / 7.0);
    }
    const gui::LinePyramid pyramid = gui::pyramidOf(line.data(), kLength, 1);

    const double low = 1.0;
    const double high = static_cast<double>(kLength);
    const std::optional<gui::LogColumns> columns = gui::logColumnsFor(low, high, kColumns);
    REQUIRE(columns.has_value());

    std::vector<double> edges;
    gui::edgesAlong(*columns, 0.0, 1.0, edges);
    gui::ColumnFold fold;
    gui::foldColumns(pyramid, edges, fold);

    REQUIRE(fold.values.size() == fold.positions.size());
    CHECK(fold.summarised);
    // Bounded by the pane, not by the line: two points a column at most, and
    // the columns are between one and two a pixel with half a pane of margin
    // either side.
    CHECK(fold.values.size() <= 2 * edges.size());
    CHECK(fold.values.size() <= 8 * static_cast<std::size_t>(kColumns) + 2);
    REQUIRE(std::is_sorted(fold.positions.begin(), fold.positions.end()));

    SECTION("the line starts at the first element there is a place for")
    {
        // Element 0 sits at x = 0, which this axis has no place for; element 1
        // is the first it does, and it is drawn as itself.
        REQUIRE_FALSE(fold.positions.empty());
        CHECK(fold.positions.front() == 1.0);
        CHECK(fold.values.front() == line[1]);
    }

    SECTION("every tenth of the pane is drawn as finely as the data allows")
    {
        // Counted per tenth of the pane rather than per point, which is what a
        // reader sees: the old fold put one or two points in each of the first
        // five tenths and thousands in the last one.
        for (int tenth = 0; tenth < 10; ++tenth) {
            const double from = std::exp2(std::log2(high) * tenth / 10.0);
            const double to = std::exp2(std::log2(high) * (tenth + 1) / 10.0);
            const auto drawn = std::count_if(
                fold.positions.begin(), fold.positions.end(),
                [&](double at) { return at >= from && at < to; });
            // As many elements as there are, where there are fewer than the
            // tenth has columns; a column's worth or more everywhere else.
            const double elements = std::ceil(to) - std::ceil(from);
            const double wanted = std::min(elements, kColumns / 10.0);
            INFO("tenth " << tenth << " x " << from << ".." << to << " drew " << drawn);
            CHECK(static_cast<double>(drawn) >= wanted);
        }
    }

    SECTION("no two consecutive points are further apart than a column or two elements")
    {
        for (std::size_t i = 1; i < fold.positions.size(); ++i) {
            const double a = fold.positions[i - 1];
            const double b = fold.positions[i];
            if (a < low || b > high) {
                continue; // the margin either side of the pane
            }
            const double apart = logFraction(b, low, high) - logFraction(a, low, high);
            INFO("points at " << a << " and " << b);
            CHECK((apart <= 2.0 / kColumns || b - a <= 1.0));
        }
    }

    SECTION("each point is a value its own column holds")
    {
        // An envelope pair is the extremes of the elements between two edges.
        // Walk the edges and check each column against the elements themselves.
        std::size_t at = 0;
        for (std::size_t c = 0; c + 1 < edges.size() && at < fold.positions.size(); ++c) {
            const auto first = static_cast<long long>(std::ceil(std::max(edges[c], 0.0)));
            const auto last = static_cast<long long>(
                std::min(std::ceil(edges[c + 1]), static_cast<double>(kLength)));
            if (last <= first) {
                continue;
            }
            const gui::Extremes want = gui::extremesOf(line.data(), first, last);
            INFO("column " << c << " elements " << first << ".." << last);
            if (last - first <= 2) {
                for (long long i = first; i < last; ++i, ++at) {
                    REQUIRE(fold.positions[at] == static_cast<double>(i));
                    REQUIRE(fold.values[at] == line[static_cast<std::size_t>(i)]);
                }
                continue;
            }
            REQUIRE(at + 1 < fold.values.size());
            CHECK(fold.positions[at] == static_cast<double>(first));
            CHECK(fold.values[at] == want.first());
            CHECK(fold.values[at + 1] == want.second());
            at += 2;
        }
        CHECK(at == fold.values.size());
    }
}

TEST_CASE("a logarithmic pane's columns are aligned, and change only an octave at a time",
          "[levels][log]")
{
    const std::optional<gui::LogColumns> here = gui::logColumnsFor(10.0, 1e5, 1000);
    REQUIRE(here.has_value());

    SECTION("a pan inside the margin is served by the columns in hand")
    {
        // Half a pane of margin in the logarithm, so a pan of a tenth of the
        // pane either way needs nothing new -- which is what keeps a drag from
        // folding on every frame of it.
        const double tenth = std::pow(1e4, 0.1);
        CHECK(gui::logColumnsServe(*here, 10.0 * tenth, 1e5 * tenth, 1000));
        CHECK(gui::logColumnsServe(*here, 10.0 / tenth, 1e5 / tenth, 1000));
        // A whole pane is past the margin.
        CHECK_FALSE(gui::logColumnsServe(*here, 1e5, 1e9, 1000));
    }

    SECTION("a pan that does need new columns finds the same edges where they overlap")
    {
        // Aligned to a fixed grid in log2(x), so the edges are not derived from
        // the view: two views folded apart agree edge for edge where they meet,
        // and the envelope between two edges cannot change under the pointer.
        const std::optional<gui::LogColumns> there = gui::logColumnsFor(100.0, 1e6, 1000);
        REQUIRE(there.has_value());
        CHECK(there->density == here->density);
        const long long k = std::max(here->first, there->first);
        CHECK(here->edge(k) == there->edge(k));
    }

    SECTION("the density steps by octaves, and between one and two edges land in a column")
    {
        for (const double decades : {1.0, 2.0, 3.3, 6.0, 12.0}) {
            const double octaves = decades * std::log2(10.0);
            const std::optional<gui::LogColumns> grid =
                gui::logColumnsFor(1.0, std::pow(10.0, decades), 1000);
            REQUIRE(grid.has_value());
            INFO("decades " << decades);
            const double perColumn = static_cast<double>(grid->density) * octaves / 1000.0;
            CHECK(perColumn >= 1.0);
            CHECK(perColumn < 2.0);
            CHECK((grid->density & (grid->density - 1)) == 0);
        }
    }

    SECTION("under an octave there is nothing to fold")
    {
        // No column is twice as wide as another there, and the linear path --
        // which also reads below the pyramid's base -- is the right one.
        CHECK_FALSE(gui::logColumnsFor(1000.0, 1900.0, 1000).has_value());
        CHECK(gui::logColumnsFor(1000.0, 2000.0, 1000).has_value());
        // ...and a window this axis cannot have is not folded either.
        CHECK_FALSE(gui::logColumnsFor(0.0, 1000.0, 1000).has_value());
        CHECK_FALSE(gui::logColumnsFor(-5.0, 1000.0, 1000).has_value());
        CHECK_FALSE(gui::logColumnsFor(1.0, 1000.0, 0).has_value());
    }

    SECTION("the edges come out ascending whichever way the axis runs")
    {
        std::vector<double> forwards;
        std::vector<double> backwards;
        gui::edgesAlong(*here, 0.0, 2.0, forwards);
        gui::edgesAlong(*here, 1e6, -2.0, backwards);
        REQUIRE_FALSE(forwards.empty());
        CHECK(std::is_sorted(forwards.begin(), forwards.end()));
        CHECK(std::is_sorted(backwards.begin(), backwards.end()));
        // x = start + p * step, so the first edge at 2^(first / density) is
        // position edge / 2 on the forward axis.
        CHECK(forwards.front() == Approx(here->edge(here->first) / 2.0));
    }
}

TEST_CASE("the smallest value above zero comes out of the line, not out of its summary",
          "[levels][pyramid][log]")
{
    // Where a logarithmic axis starts. A summary keeps each bucket's smallest,
    // so a bucket holding a zero and a thousandth says zero and the thousandth
    // is gone -- which was the bottom of a time base's axis, and a decade and a
    // third of its data off the pane.
    const auto brute = [](const std::vector<double>& line) {
        double best = std::numeric_limits<double>::infinity();
        for (const double value : line) {
            if (value > 0.0 && std::isfinite(value)) {
                best = std::min(best, value);
            }
        }
        return best;
    };

    SECTION("a time base from zero")
    {
        std::vector<double> time(20000);
        for (std::size_t i = 0; i < time.size(); ++i) {
            time[i] = static_cast<double>(i) / 1000.0;
        }
        const gui::LinePyramid pyramid = gui::pyramidOf(time.data(), 20000, 1);
        double smallest = 0.0;
        REQUIRE(gui::smallestPositive(pyramid, smallest));
        CHECK(smallest == 0.001);

        // The summary's answer, for contrast: the first pair is the zero and
        // the end of the first bucket.
        std::vector<double> summary;
        gui::reduceBuckets(time.data(), 20000, 22, summary);
        CHECK(brute(summary) > 0.02);
    }

    SECTION("a line crossing zero everywhere, with NaN in it")
    {
        std::vector<double> line = testLine(30000);
        for (std::size_t i = 0; i < line.size(); ++i) {
            if (std::isfinite(line[i])) {
                line[i] -= 0.5; // push most of the line below zero
            }
        }
        line[17777] = 1e-9; // the answer, hidden next to negatives
        const gui::LinePyramid pyramid = gui::pyramidOf(line.data(), 30000, 1);
        double smallest = 0.0;
        REQUIRE(gui::smallestPositive(pyramid, smallest));
        CHECK(smallest == brute(line));
        CHECK(smallest == 1e-9);

        SECTION("and over a coarse base, never below the truth")
        {
            const gui::LinePyramid coarse = gui::pyramidOf(line.data(), 30000, 16);
            double answer = 0.0;
            REQUIRE(gui::smallestPositive(coarse, answer));
            CHECK(answer >= 1e-9);
        }
    }

    SECTION("nothing above zero is no answer")
    {
        const std::vector<double> line{-1.0, 0.0, -3.0, kNaN, -0.0};
        const gui::LinePyramid pyramid = gui::pyramidOf(line.data(), 5, 1);
        double smallest = 42.0;
        CHECK_FALSE(gui::smallestPositive(pyramid, smallest));
    }
}
