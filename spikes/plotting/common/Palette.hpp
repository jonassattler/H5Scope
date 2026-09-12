// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// One colour rule for all three spikes.
//
// Not the application's palette. Theme.qml's `spectrum` and `safe` are chosen
// by farthest-point selection in CIELAB and hold twenty and ten entries
// respectively, which is the right answer for a reader looking at twenty lines
// and the wrong one for a check that has to ask whether a thousand lines were
// drawn as a thousand lines: a repeating twenty-entry palette gives the same
// answer whether the renderer drew 1000 curves or capped at 20.
//
// So: a hue sweep, one full turn across the series, at a fixed saturation and
// value. Every line a different colour, no repeats below about three hundred
// and sixty, and -- because it is the same rule in all three spikes -- an image
// from one is comparable with an image from another.

#include <QtGui/QColor>

#include <algorithm>

namespace spike {

[[nodiscard]] inline QColor seriesColour(int index, int count)
{
    const int total = std::max(1, count);
    const int hue = static_cast<int>(359.0 * static_cast<double>(index % total)
                                     / static_cast<double>(total));
    // Value below the maximum so the strokes sit clear of a white ground, and
    // saturation below it so they sit clear of a black one. The images are read
    // by a program, but they are also looked at.
    return QColor::fromHsv(hue, 200, 220);
}

/// The ground every spike draws on. A mid-grey rather than either extreme, so
/// that dominantColour() picks it out unambiguously and no line's own colour
/// can be mistaken for it.
[[nodiscard]] inline QColor plotGround() { return QColor(32, 34, 38); }

} // namespace spike
