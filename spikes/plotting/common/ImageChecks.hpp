// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// What landed on screen, read back as numbers.
//
// The correctness half of this bake-off cannot be settled by asking a library
// what it did -- every one of them will say it drew the data it was given. It
// has to be settled on the pixels, because the interesting failures are all
// failures of the path between the data and the pixels: a spike thinned away
// before it was ever submitted, a gap closed by a line drawn straight across
// it, a float32 vertex quantising an epoch timestamp into a staircase. None of
// those is an error anything reports.
//
// The predicates below are deliberately coarse. They answer questions of the
// form "is there ink in this column", "is this column empty", "how many
// distinct colours are on this image" -- properties that survive a difference
// in line width, antialiasing or theme between the three spikes, and that
// would still be true of a correct drawing made by a fourth. A pixel-exact
// comparison against a reference image would fail for all three for reasons
// that have nothing to do with whether any of them told the truth.
//
// The background is found rather than assumed: it is the modal colour of the
// image. Three spikes with three themes would otherwise need three constants,
// and a constant that drifts out of date turns every check green.

#include <QtGui/QImage>
#include <QtGui/QRgb>

#include <vector>

namespace spike {

struct InkProfile {
    int width = 0;
    int height = 0;
    QRgb background = 0;
    /// Non-background pixels in each column.
    std::vector<int> inkPerColumn;
    /// Topmost and bottommost non-background row in each column, -1 for none.
    /// Row 0 is the top of the image, so a *smaller* topInkRow means a
    /// *higher* value on a plot whose y axis points up.
    std::vector<int> topInkRow;
    std::vector<int> bottomInkRow;

    [[nodiscard]] int columnsWithInk() const;
    /// Longest run of columns with no ink at all.
    [[nodiscard]] int longestEmptyRun() const;
    /// Where that run starts, or -1.
    [[nodiscard]] int longestEmptyRunStart() const;
};

/// The modal colour of the image -- what the plot is drawn *on*.
[[nodiscard]] QRgb dominantColour(const QImage& image);

/// `tolerance` is a per-channel distance from the background below which a
/// pixel counts as background. Antialiasing puts a halo of near-background
/// pixels around every stroke; counting those as ink would make a one-pixel
/// line three pixels wide and every gap narrower than it is.
///
/// `minChroma` is the other half, and without it most of these checks are
/// unanswerable. All three spikes draw chrome -- a grid, axis rules, tick
/// labels -- and a horizontal grid line puts ink in *every* column, so "is
/// there a gap here" and "is the topmost ink flat across these columns" both
/// come back yes for a picture that is perfectly correct. The chrome is grey
/// and the data is not: Palette.hpp gives every line a fully saturated hue on
/// purpose, so requiring a minimum spread between the channels separates the
/// curves from everything drawn around them. It was measured before this
/// existed: the gap check failed for all three renderers, and all three were
/// drawing the gap.
[[nodiscard]] InkProfile profile(const QImage& image, int tolerance = 16,
                                 int minChroma = 0);

/// Distinct colours, quantised to `step` per channel and ignoring anything
/// within `tolerance` of the background. The count that says whether a
/// thousand lines were drawn as a thousand lines or silently capped.
[[nodiscard]] int distinctColours(const QImage& image, int step = 24,
                                  int tolerance = 16, int minChroma = 0);

/// The fraction of pixels that are not background. A plot that drew nothing
/// and a plot that filled the pane are both wrong, and both look like a pass
/// to any check that only asks whether *something* appeared.
[[nodiscard]] double coverage(const InkProfile& ink);

/// Whether the topmost ink follows a straight enough line across `first` to
/// `last`. A float32 vertex for an epoch timestamp does not bend the line --
/// it flattens it into steps -- so what this looks for is runs of identical
/// values far longer than a correct drawing would ever produce.
[[nodiscard]] int longestFlatRun(const InkProfile& ink, int first, int last);

/// The chroma a series stroke clears and no grey chrome does. Measured
/// against the palette and the three spikes' greys, with room either side.
inline constexpr int kSeriesChroma = 40;

bool writePng(const QImage& image, const QString& path);

} // namespace spike
