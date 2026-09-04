// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import QtQuick.Shapes

/// A line icon, drawn as vector geometry rather than set as a glyph.
///
/// Two things rule out a character: the design system ships no icon font and
/// specifies none, and the four faces this binary carries are the four it uses
/// -- an arrow taken from whatever the host happens to have would be the one
/// dependency the fonts chapter of the README exists to refuse, and a missing
/// glyph is a tofu box in the middle of a toolbar. So the paths are here, in
/// the one place a colour and a weight can be taken from Theme like everything
/// else.
///
/// Every icon is laid out on a 24-unit grid and scaled to whatever size it is
/// given, stroke and all, so one design serves every button size.
Item {
    id: icon

    /// "rotateLeft" | "rotateRight" | "flipHorizontal" | "flipVertical"
    /// | "close" | "grip"
    property string name
    property color color: Theme.textPrimary
    /// Stroke weight on the 24-unit grid; 2 is the system's line icon weight.
    property real thickness: 2

    implicitWidth: Theme.iconSize
    implicitHeight: Theme.iconSize

    readonly property real unit: Math.min(width, height) / 24

    Loader {
        anchors.fill: parent
        sourceComponent: {
            switch (icon.name) {
            case "rotateLeft":     return rotateLeftIcon
            case "rotateRight":    return rotateRightIcon
            case "flipHorizontal": return flipHorizontalIcon
            case "flipVertical":   return flipVerticalIcon
            case "close":          return closeIcon
            case "grip":           return gripIcon
            default:               return null
            }
        }
    }

    // Two strokes crossing, inset from the grid so the cross sits inside the
    // button rather than against its rim. This is the remove control on a
    // pipeline step, and the reason it is drawn rather than typed is the one
    // in the header: a multiplication sign borrowed from the host font is a
    // dependency, and a missing glyph is a tofu box where the button was.
    Component {
        id: closeIcon

        Shape {
            preferredRendererType: Shape.CurveRenderer

            ShapePath {
                strokeColor: icon.color
                strokeWidth: icon.thickness * icon.unit
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap

                startX: 7 * icon.unit
                startY: 7 * icon.unit
                PathLine { x: 17 * icon.unit; y: 17 * icon.unit }
            }

            ShapePath {
                strokeColor: icon.color
                strokeWidth: icon.thickness * icon.unit
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap

                startX: 17 * icon.unit
                startY: 7 * icon.unit
                PathLine { x: 7 * icon.unit; y: 17 * icon.unit }
            }
        }
    }

    // Three rules stacked, which is what every list in every application draws
    // to say "this row can be picked up". Wider apart than a texture so it
    // reads at the 22px a step row gives it.
    Component {
        id: gripIcon

        Shape {
            preferredRendererType: Shape.CurveRenderer

            ShapePath {
                strokeColor: icon.color
                strokeWidth: icon.thickness * icon.unit
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap

                startX: 8 * icon.unit
                startY: 8 * icon.unit
                PathLine { x: 16 * icon.unit; y: 8 * icon.unit }
                PathMove { x: 8 * icon.unit; y: 12 * icon.unit }
                PathLine { x: 16 * icon.unit; y: 12 * icon.unit }
                PathMove { x: 8 * icon.unit; y: 16 * icon.unit }
                PathLine { x: 16 * icon.unit; y: 16 * icon.unit }
            }
        }
    }

    // A rotation is a half turn over the top with the head coming down on the
    // side it is turning towards -- the way every image viewer draws it. Qt
    // measures an arc from 3 o'clock with positive angles running clockwise
    // on screen, so the two differ only in the sign of the sweep.
    Component {
        id: rotateRightIcon

        Shape {
            preferredRendererType: Shape.CurveRenderer

            ShapePath {
                strokeColor: icon.color
                strokeWidth: icon.thickness * icon.unit
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin

                PathAngleArc {
                    centerX: 12 * icon.unit
                    centerY: 13 * icon.unit
                    radiusX: 6.5 * icon.unit
                    radiusY: 6.5 * icon.unit
                    startAngle: 180
                    sweepAngle: 180
                }
            }

            // The head, at the end of the sweep and pointing the way it was
            // going: straight down the right-hand side. Solid rather than two
            // strokes, because at eighteen pixels a chevron on the end of an
            // arc reads as a serif and a triangle reads as an arrow.
            ShapePath {
                strokeWidth: 0
                fillColor: icon.color

                startX: 15 * icon.unit
                startY: 11.5 * icon.unit
                PathLine { x: 22 * icon.unit; y: 11.5 * icon.unit }
                PathLine { x: 18.5 * icon.unit; y: 17.5 * icon.unit }
                PathLine { x: 15 * icon.unit; y: 11.5 * icon.unit }
            }
        }
    }

    Component {
        id: rotateLeftIcon

        Shape {
            preferredRendererType: Shape.CurveRenderer

            ShapePath {
                strokeColor: icon.color
                strokeWidth: icon.thickness * icon.unit
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin

                PathAngleArc {
                    centerX: 12 * icon.unit
                    centerY: 13 * icon.unit
                    radiusX: 6.5 * icon.unit
                    radiusY: 6.5 * icon.unit
                    startAngle: 0
                    sweepAngle: -180
                }
            }

            ShapePath {
                strokeWidth: 0
                fillColor: icon.color

                startX: 2 * icon.unit
                startY: 11.5 * icon.unit
                PathLine { x: 9 * icon.unit; y: 11.5 * icon.unit }
                PathLine { x: 5.5 * icon.unit; y: 17.5 * icon.unit }
                PathLine { x: 2 * icon.unit; y: 11.5 * icon.unit }
            }
        }
    }

    // A flip is a shape and its mirror image with the mirror between them --
    // the dashed rule is the axis, and it is what separates these two from a
    // pair of arrows meaning "wider" or "taller".
    Component {
        id: flipHorizontalIcon

        Shape {
            preferredRendererType: Shape.CurveRenderer

            ShapePath {
                strokeColor: icon.color
                strokeWidth: icon.thickness * icon.unit
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin

                startX: 9 * icon.unit
                startY: 3.5 * icon.unit
                PathLine { x: 1.5 * icon.unit; y: 12 * icon.unit }
                PathLine { x: 9 * icon.unit; y: 20.5 * icon.unit }
                PathLine { x: 9 * icon.unit; y: 3.5 * icon.unit }

                PathMove { x: 15 * icon.unit; y: 3.5 * icon.unit }
                PathLine { x: 22.5 * icon.unit; y: 12 * icon.unit }
                PathLine { x: 15 * icon.unit; y: 20.5 * icon.unit }
                PathLine { x: 15 * icon.unit; y: 3.5 * icon.unit }
            }

            ShapePath {
                strokeColor: icon.color
                strokeWidth: icon.thickness * icon.unit
                strokeStyle: ShapePath.DashLine
                dashPattern: [1.5, 1.5]
                fillColor: "transparent"
                capStyle: ShapePath.FlatCap

                startX: 12 * icon.unit
                startY: 2.5 * icon.unit
                PathLine { x: 12 * icon.unit; y: 21.5 * icon.unit }
            }
        }
    }

    Component {
        id: flipVerticalIcon

        Shape {
            preferredRendererType: Shape.CurveRenderer

            ShapePath {
                strokeColor: icon.color
                strokeWidth: icon.thickness * icon.unit
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin

                startX: 3.5 * icon.unit
                startY: 9 * icon.unit
                PathLine { x: 12 * icon.unit; y: 1.5 * icon.unit }
                PathLine { x: 20.5 * icon.unit; y: 9 * icon.unit }
                PathLine { x: 3.5 * icon.unit; y: 9 * icon.unit }

                PathMove { x: 3.5 * icon.unit; y: 15 * icon.unit }
                PathLine { x: 12 * icon.unit; y: 22.5 * icon.unit }
                PathLine { x: 20.5 * icon.unit; y: 15 * icon.unit }
                PathLine { x: 3.5 * icon.unit; y: 15 * icon.unit }
            }

            ShapePath {
                strokeColor: icon.color
                strokeWidth: icon.thickness * icon.unit
                strokeStyle: ShapePath.DashLine
                dashPattern: [1.5, 1.5]
                fillColor: "transparent"
                capStyle: ShapePath.FlatCap

                startX: 2.5 * icon.unit
                startY: 12 * icon.unit
                PathLine { x: 21.5 * icon.unit; y: 12 * icon.unit }
            }
        }
    }
}
