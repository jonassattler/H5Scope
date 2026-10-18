// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// A plotcore scene, painted with QPainter.
//
// plotcore decides everything about a picture -- where each line, rule, box,
// ring and number goes, and in what colour -- and hands it over as a Scene:
// the ground, the marks under the lines, the lines, and the marks over them, in
// the order they are painted. This is the translation of that list into
// QPainter calls, and nothing else: there is no choice made here that the
// scene did not make first.
//
// Two things paint with it. The figure item on Qt Quick's software renderer,
// which draws no custom geometry at all (see FigureItem), and the picture a
// reader copies out of the application, which is a QImage and wants QPainter
// anyway. On a renderer with a graphics API the figure item builds scene-graph
// nodes from the same scene instead, and the two are held to one picture by
// being translations of one list.

#include <QFont>
#include <QRectF>

#include <array>

class QPainter;

namespace plotcore {
struct Scene;
struct Mark;
struct TextItem;
enum class TextRole;
} // namespace plotcore

namespace gui {

/// The faces a scene is set in, one for each plotcore::TextRole, in its order.
struct SceneFonts
{
    std::array<QFont, 6> faces;

    [[nodiscard]] const QFont& of(plotcore::TextRole role) const;
};

/// Where a text's two parts go: the base's top left and the exponent's, in
/// the text's own frame -- before it is turned about its point.
struct TextPlacement
{
    double baseLeft = 0.0;
    double baseTop = 0.0;
    double exponentLeft = 0.0;
    double exponentTop = 0.0;
};

/// What a text mark measures in `fonts`, and where its parts go: its
/// alignment about its point, and its exponent raised beside its base.
[[nodiscard]] TextPlacement placeText(const plotcore::TextItem& text, const SceneFonts& fonts);

/// Paint one mark. `area` is the pane, which a clipped mark stays inside.
void paintMark(QPainter& painter, const plotcore::Mark& mark, const SceneFonts& fonts,
               const QRectF& area);

/// Paint the whole of `scene`: its ground, the marks under the lines, the
/// lines inside the pane, and the marks over them.
void paintScene(QPainter& painter, const plotcore::Scene& scene, const SceneFonts& fonts);

} // namespace gui
