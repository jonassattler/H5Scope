// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import QtQuick.Controls.Basic

/// One integer, typed or stepped. The table setup panel's index and range
/// boxes, and the image panel's channel indices.
///
/// A TextField with a FieldStepper rather than a SpinBox: the Basic style's
/// up/down indicators are the one piece of chrome in this application that
/// would be drawn by Qt instead of by Theme, and its layout reserves room for a
/// pair of them either side of the number. This keeps the box a box and hangs
/// the two arrows off its right-hand end, which is where every other number
/// entry on the desktop puts them.
///
/// The model is the authority -- `value` stays bound to it, and whatever is
/// typed or stepped goes out through `committed` and comes back clamped.
TextField {
    id: control

    property int value: 0
    property int from: 0
    property int to: 0

    /// How far one press of an arrow moves it. One, always: these boxes count
    /// indices, and the next index is the next number there is.
    readonly property int stepSize: 1

    /// A number the user actually entered, clamped to [from, to].
    signal committed(int amount)

    text: String(control.value)
    validator: IntValidator { bottom: control.from; top: control.to }
    font: Theme.monoSmall
    color: Theme.textPrimary
    selectionColor: Theme.accent
    selectedTextColor: Theme.accentText
    horizontalAlignment: Qt.AlignHCenter
    // Six mono digits, its padding, and the arrows. Wider starves the slider
    // beside it, and a dimension needing seven digits is one nobody scrolls to
    // by hand anyway.
    implicitWidth: Theme.s11 + stepper.width
    implicitHeight: Theme.smallControlHeight
    leftPadding: Theme.gapS
    // The arrows stand in the box, so the number stops before them.
    rightPadding: stepper.width + Theme.gapXS
    opacity: control.enabled ? 1.0 : 0.4

    onEditingFinished: control.commit()

    // The keyboard says the same thing as the arrows, which is what every
    // number box the reader has used before does.
    Keys.onUpPressed: control.nudge(1)
    Keys.onDownPressed: control.nudge(-1)

    /// The number the box is showing, which is what a step moves from -- not
    /// `value`, or typing 40 and then pressing the arrow would step from
    /// whatever was selected before the 40 was typed.
    function shown() {
        const entered = parseInt(control.text, 10)
        return isNaN(entered) ? control.value : entered
    }

    function nudge(direction) {
        const wanted = Math.max(control.from,
                                Math.min(control.to,
                                         control.shown() + direction * control.stepSize))
        control.committed(wanted)
        // The model may have clamped it or refused it outright, so the box goes
        // back to showing what is actually selected, exactly as commit() does.
        control.text = Qt.binding(() => String(control.value))
    }

    function commit() {
        const entered = parseInt(control.text, 10)
        if (!isNaN(entered))
            control.committed(Math.max(control.from, Math.min(control.to, entered)))
        // Typing broke the binding on `text`; restore it, because the model
        // may have clamped the number or refused it outright and the box has
        // to show what is actually selected.
        control.text = Qt.binding(() => String(control.value))
    }

    // Focus swaps the border to signal white. The system forbids a glow on an
    // input.
    background: Rectangle {
        radius: Theme.radiusS
        color: Theme.surfaceInset
        border.width: control.activeFocus ? Theme.borderWidthAccent
                                          : Theme.borderWidth
        border.color: control.activeFocus ? Theme.accent : Theme.border
    }

    FieldStepper {
        id: stepper

        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.margins: Theme.borderWidth
        enabled: control.enabled
        upEnabled: control.value < control.to
        downEnabled: control.value > control.from
        onStepped: direction => control.nudge(direction)
    }
}
