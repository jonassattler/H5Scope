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

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <vector>

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
