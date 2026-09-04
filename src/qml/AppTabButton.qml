// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import QtQuick.Controls.Basic

/// Tab in the right-hand pane.
///
/// The design system's Tabs carries no ground of its own: an active tab is a
/// 2px signal-white rule beneath it and a step up in text colour, and that is
/// the entire treatment. There is no fill when selected and none on hover -- a
/// filled slab is what this system spends on a pressed menu row and on its one
/// primary button, not on navigation. The labels are machine labels: mono,
/// uppercase, wide-tracked.
///
/// One addition to the upstream spec, which defines no hover state for a tab at
/// all: an unselected label lifts to body colour under the pointer. The system
/// does give every other interactive surface hover feedback, and a tab strip
/// that answers a pointer with nothing reads as disabled.
Button {
    id: control

    property bool selected: false

    implicitHeight: Theme.tabBarHeight
    implicitWidth: label.implicitWidth + Theme.s7 * 2
    hoverEnabled: true

    background: Rectangle {
        color: "transparent"

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: Theme.borderWidthAccent
            color: control.selected ? Theme.accent : Theme.clear(Theme.accent)
        }
    }

    contentItem: Text {
        id: label

        text: control.text
        font: Theme.label
        // A tab this selection cannot offer -- the plot and the image on a
        // dataset of text -- is greyed rather than removed, so the strip keeps
        // its shape and says what is unavailable rather than hiding it.
        color: !control.enabled ? Theme.textDisabled
             : control.selected ? Theme.textEmphasis
             : control.hovered ? Theme.textPrimary : Theme.textSecondary
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
}
