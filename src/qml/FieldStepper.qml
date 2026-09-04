// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import QtQuick.Controls.Basic

/// The pair of arrows at the right-hand end of a number box.
///
/// Two Buttons rather than the Basic style's SpinBox indicators, for the same
/// reason NumberField is not a SpinBox: those are the one piece of chrome in
/// this application Qt would draw instead of Theme. They are drawn here as the
/// system draws everything else -- a hairline separating them from the number,
/// a ground that answers the pointer, and punctuation rather than an icon.
///
/// `autoRepeat` is on. A reader stepping through the planes of a dimension is
/// doing it more than once, and holding an arrow down is how every other
/// number box on the desktop says "keep going".
Column {
    id: stepper

    /// +1 for the upper arrow, -1 for the lower one.
    signal stepped(int direction)

    /// Whether each end is still reachable. A box already at the top of its
    /// range has nowhere to go up to, and the arrow says so rather than
    /// silently doing nothing.
    property bool upEnabled: true
    property bool downEnabled: true

    width: Theme.s7
    spacing: 0

    Repeater {
        model: [
            { glyph: "▴", direction: 1 },
            { glyph: "▾", direction: -1 }
        ]

        delegate: Button {
            id: arrow

            required property var modelData

            width: stepper.width
            height: stepper.height / 2
            enabled: modelData.direction > 0 ? stepper.upEnabled
                                             : stepper.downEnabled
            autoRepeat: true
            padding: 0
            hoverEnabled: true
            focusPolicy: Qt.NoFocus
            opacity: arrow.enabled ? 1.0 : 0.4
            onClicked: stepper.stepped(arrow.modelData.direction)

            background: Rectangle {
                color: arrow.down ? Theme.surfaceActive
                     : arrow.hovered ? Theme.surfaceHover
                                     : Theme.clear(Theme.surfaceHover)

                // The seam between the number and the controls that move it.
                Rectangle {
                    anchors.left: parent.left
                    width: Theme.borderWidth
                    height: parent.height
                    color: Theme.border
                }
            }

            contentItem: Text {
                text: arrow.modelData.glyph
                font: Theme.caret
                color: arrow.hovered ? Theme.textPrimary : Theme.textSecondary
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }
    }
}
