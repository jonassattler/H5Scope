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
/// Three more since the strip stopped being four fixed tabs. A custom plot can
/// be closed, so it carries a cross -- shown only under the pointer or while it
/// is the tab on screen, because a row of crosses down a strip is a row of
/// things to press by accident. The control that *makes* one is a tab-shaped
/// button with a glyph instead of a label, so that it stands in the strip at
/// the strip's own height rather than as a button dropped into it. And a tab
/// the reader has named draws that name as they wrote it: the uppercase is for
/// the four words this program chose, not for a phrase somebody typed into a
/// box that is showing it back to them unchanged.
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
    /// Draw the label as it was written rather than uppercased. For a tab
    /// whose name the reader typed; see Theme.labelVerbatim.
    property bool verbatimLabel: false

    signal closeRequested()

    /// How far above the line box the strip's lettering actually sits.
    ///
    /// A Text centred in an item centres its *line box*, which reserves room
    /// under the baseline for descenders. A machine label is all capitals and
    /// has none, so its letters sit half a descent above the middle of the
    /// item -- and a glyph centred on the item lands that far below the words
    /// beside it, which at 28 pixels is a mark the eye reads as dropped.
    ///
    /// Only for the capitals. A tab the reader has named is set as they wrote
    /// it, descenders and all, so there the line box is the right thing to
    /// centre on and this is zero.
    readonly property real inkOffset:
        control.verbatimLabel ? 0 : -Math.round(capitals.descent / 2)

    FontMetrics {
        id: capitals

        font: Theme.label
    }

    // Carried by the control rather than set on the label inside it, which is
    // how Qt Quick Controls means a font to be handed down: anything asking
    // this button what it is set in -- the suite that pins the strip's
    // capitalization, a layout measuring it -- asks the button, and there is
    // one answer rather than one per item.
    font: control.verbatimLabel ? Theme.labelVerbatim : Theme.label

    implicitHeight: Theme.tabBarHeight
    implicitWidth: control.glyph !== ""
                   ? Theme.tabBarHeight
                   : label.implicitWidth + Theme.s7 * 2
                     + (control.closable ? Theme.iconSize : 0)
    hoverEnabled: true

    background: Rectangle {
        // A tab carries no ground of its own, and the glyph button in the
        // strip is not a tab: it is a control that happens to live there, and
        // with no label to lift under the pointer it has nothing else to
        // answer with. The four real tabs keep the transparent ground the
        // design gives them.
        color: (control.glyph !== "" && control.hovered)
               ? Theme.surfaceHover : "transparent"

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

        // The caret is Caret's own geometry rather than an AppIcon path: it
        // is the same chevron the tree and the panel buttons draw, and there
        // is one of it.
        Caret {
            anchors.centerIn: parent
            anchors.verticalCenterOffset: control.inkOffset
            visible: control.glyph === "caret"
            size: Theme.iconSize
            angle: 90
            color: !control.enabled ? Theme.textDisabled
                 : control.hovered ? control.ink
                                   : Theme.mix(Theme.background, control.ink, 0.7)
        }

        AppIcon {
            anchors.centerIn: parent
            anchors.verticalCenterOffset: control.inkOffset
            visible: control.glyph !== "" && control.glyph !== "caret"
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
            anchors.right: parent.right
            // Room for the cross only on a tab that has one. Anchored to the
            // edge with a margin rather than to the cross itself: the cross is
            // hidden on the four fixed tabs and an anchor to a hidden item
            // still holds its place, which took eighteen pixels off every
            // label in the strip and elided all four of them.
            anchors.rightMargin: control.closable ? Theme.iconSize : 0
            anchors.verticalCenter: parent.verticalCenter
            visible: control.glyph === ""
            text: control.text
            font: control.font
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
            anchors.verticalCenterOffset: control.inkOffset
            // The icon at its own size with no rim. A cross drawn into twelve
            // pixels on a 24-unit grid is a one-pixel stroke inset to eight of
            // them, which is a smudge rather than a cross and a target the
            // pointer has to be aimed at.
            width: Theme.iconSize
            height: Theme.iconSize
            padding: 0
            visible: control.closable && (control.hovered || control.selected)
            glyph: "close"
            bare: true
            ink: Theme.danger
            onClicked: control.closeRequested()
        }
    }
}
