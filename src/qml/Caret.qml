// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import QtQuick.Shapes

/// The expander mark: one chevron, drawn as vector geometry rather than set as
/// a glyph.
///
/// It was `▸` and `▾` from the mono face, which is the same dependency AppIcon
/// exists to refuse -- the shape of a caret was whatever the bundled face drew,
/// and what it drew was a soft little triangle. This one is specified here: two
/// arms swept back from a point at a 63-degree included angle, every corner
/// mitred, both free ends tapering to a point of their own. Nothing about it is
/// rounded and nothing is cut square.
///
/// Laid out on the same 24-unit grid AppIcon uses and scaled to whatever `size`
/// it is given, so one design serves the tree, the panel buttons and the menu
/// drawers alike. The chevron is inscribed in that square -- 18 units across by
/// 22 down, centred on (12, 12) -- so a quarter turn still lands inside it and
/// the caret can be rotated without the layout moving.
///
/// A filled path rather than a stroked one, because a stroke has ends and ends
/// have caps: a flat cap is a blunt cut and a round cap is the shape this is
/// replacing. Only an outline can come to a point.
Item {
    id: caret

    /// The side of the square the chevron is drawn in. Its own extent is
    /// smaller -- see above -- so this is the slot to reserve, not the ink.
    property real size: Theme.gapL
    property color color: Theme.textSecondary
    /// Where the point aims, in degrees clockwise from east: 0 is `>`, and a
    /// closed branch; 90 is `v`, and an open one.
    ///
    /// The turn is applied to the geometry rather than to this Item, so the
    /// hit area of any handler an owner attaches keeps the shape the layout
    /// gave it. A caret in a 10-wide column that rotated whole would reach 26
    /// pixels across a tree row and swallow the name beside it.
    property real angle: 0

    implicitWidth: caret.size
    implicitHeight: caret.size

    readonly property real unit: caret.size / 24

    Shape {
        anchors.centerIn: parent
        width: caret.size
        height: caret.size
        rotation: caret.angle
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            strokeWidth: 0
            fillColor: caret.color

            // Point, back down the lower edge to the trailing point, forward
            // to the notch, and back up to where it started.
            startX: 21 * caret.unit
            startY: 12 * caret.unit
            PathLine { x: 3 * caret.unit;  y: 23 * caret.unit }
            PathLine { x: 12 * caret.unit; y: 12 * caret.unit }
            PathLine { x: 3 * caret.unit;  y: 1 * caret.unit }
            PathLine { x: 21 * caret.unit; y: 12 * caret.unit }
        }
    }
}
