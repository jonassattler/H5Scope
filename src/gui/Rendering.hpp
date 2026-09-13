// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

namespace gui {

/// Ask the scene graph to multisample.
///
/// Everything this application draws is a rectangle, a glyph or an icon, and
/// none of those needed it: rectangles are axis-aligned, glyphs carry their own
/// antialiasing, and the icons are drawn as paths by Qt Quick Shapes, which
/// antialiases in the shader. The plot is the one thing that is neither.
///
/// A stroke built out of triangles is rasterised by coverage like any other
/// triangle, so a diagonal hairline on a true-black ground comes out as a
/// staircase -- which is what the reader sees first and what Qt Graphs did not
/// have, because Qt Quick Shapes' curve renderer computes its own analytic
/// coverage. Four samples is the difference between a staircase and a line.
///
/// The alternative was to build the antialiasing into the geometry, feathering
/// each stroke's edge with a band of vertices at zero alpha. That is three
/// times the vertices for the same line, and the plot's whole memory argument
/// is about how many vertices a ten-thousand-line selection submits.
///
/// Must be called before the first window is created. Requesting more samples
/// than the driver offers is not an error: the format is a request, and a
/// machine that cannot honour it renders exactly as it did before.
void askForMultisampling();

} // namespace gui
