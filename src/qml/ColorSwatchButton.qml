// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs

/// A block of colour that opens a colour picker when pressed.
///
/// The picker is Qt's own `ColorDialog`, and it is the one piece of chrome in
/// this application not drawn from Theme. Everything else here is built rather
/// than borrowed -- the menu bar, the file picker, every control in the rail --
/// because the platform's versions carry visual opinions this design system
/// contradicts. A colour picker is the case where that argument loses: a wheel,
/// a value ramp, an alpha channel and a hex box are a solved problem, Qt ships
/// the solution, and a hand-rolled one would be worse at the only job it has.
///
/// So the deviation is deliberate and it is bounded: the dialog appears, the
/// reader picks, it closes, and everything they see before and after is the
/// system's own.
Item {
    id: swatch

    property color value: Theme.accent
    /// What the dialog calls itself, so a reader with two of these open in
    /// sequence knows which one they are answering.
    property string label: qsTr("colour")

    /// A colour the reader actually chose.
    signal picked(color chosen)

    implicitWidth: Theme.smallControlHeight
    implicitHeight: Theme.smallControlHeight

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusS
        color: swatch.value
        // The rim is what separates a dark swatch from the panel behind it,
        // and it steps up under the pointer like every other control here.
        border.width: Theme.borderWidth
        border.color: hover.hovered ? Theme.accent : Theme.borderStrong
    }

    HoverHandler { id: hover }

    TapHandler {
        onTapped: {
            dialog.selectedColor = swatch.value
            dialog.open()
        }
    }

    AppToolTip {
        shown: hover.hovered
        text: swatch.label
    }

    ColorDialog {
        id: dialog

        title: swatch.label
        onAccepted: swatch.picked(dialog.selectedColor)
    }
}
