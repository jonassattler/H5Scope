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
///
/// Two more since the strip stopped being four fixed tabs. A custom plot can be
/// closed, so it carries a cross -- shown only under the pointer or while it is
/// the tab on screen, because a row of crosses down a strip is a row of things
/// to press by accident. And the control that *makes* one is a tab-shaped
/// button with a glyph instead of a label, so that it stands in the strip at
/// the strip's own height rather than as a button dropped into it.
Button {
    id: control

    property bool selected: false
    /// Draw a cross at the right-hand end, and report it when pressed.
    property bool closable: false
    /// An AppIcon name instead of a label. The "+" at the end of the strip is
    /// the only user of this.
    property string glyph: ""
    /// What the glyph is drawn in. Only meaningful with `glyph` set.
    property color ink: Theme.textSecondary

    signal closeRequested()

    implicitHeight: Theme.tabBarHeight
    implicitWidth: control.glyph !== ""
                   ? Theme.tabBarHeight
                   : label.implicitWidth + Theme.s7 * 2
                     + (control.closable ? Theme.gapL : 0)
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

    contentItem: Item {
        implicitWidth: control.glyph !== "" ? Theme.iconSize : label.implicitWidth
        implicitHeight: Theme.tabBarHeight

        AppIcon {
            anchors.centerIn: parent
            visible: control.glyph !== ""
            name: control.glyph
            // Dimmed at rest and full strength under the pointer, which is the
            // treatment every bare glyph in this application gets: with no
            // slab to fill, the glyph itself is what answers the pointer.
            color: !control.enabled ? Theme.textDisabled
                 : control.hovered ? control.ink
                                   : Theme.mix(Theme.background, control.ink, 0.7)
        }

        Text {
            id: label

            anchors.left: parent.left
            anchors.right: closeMark.left
            anchors.verticalCenter: parent.verticalCenter
            visible: control.glyph === ""
            text: control.text
            font: Theme.label
            // A tab this selection cannot offer -- the plot and the image on a
            // dataset of text -- is greyed rather than removed, so the strip
            // keeps its shape and says what is unavailable rather than hiding
            // it.
            color: !control.enabled ? Theme.textDisabled
                 : control.selected ? Theme.textEmphasis
                 : control.hovered ? Theme.textPrimary : Theme.textSecondary
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight

            AppToolTip {
                shown: label.truncated && control.hovered
                verbatim: true
                text: label.text
            }
        }

        AppIconButton {
            id: closeMark

            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            width: Theme.gapL
            height: Theme.gapL
            padding: 0
            visible: control.closable && (control.hovered || control.selected)
            glyph: "close"
            bare: true
            ink: Theme.danger
            hint: qsTr("close this plot")
            onClicked: control.closeRequested()
        }
    }
}
