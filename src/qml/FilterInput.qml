// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import QtQuick.Controls.Basic

/// The tree's filter box. Mono, because what it matches are identifiers.
/// Focus swaps the border to signal white; the system forbids a glow here.
///
/// It carries no width of its own: it is anchored across the foot of the tree
/// pane and takes whatever that pane is.
TextField {
    id: control

    /// The text cannot be used. The border goes to hazard amber, which is the
    /// only colour in this system that means "look at this".
    property bool invalid: false
    /// What is in the box reads, but has not been applied to anything yet.
    ///
    /// The boxes in this application that act on the data all keep the same
    /// contract -- every keystroke is checked and nothing is applied until the
    /// reader commits -- and until this there was nothing on screen that said
    /// which of the two states a box was in. A reader who typed a slice and
    /// then looked at the table was reading numbers for the *previous* slice
    /// with no way to tell.
    ///
    /// The ground says so, rather than the border: the border is already
    /// spoken for by focus and by `invalid`, and an edit that has not been
    /// applied is a fact about what is *in* the box. It is a step of ground
    /// rather than a colour, so it is legible without being a warning -- this
    /// is not a mistake, it is an unfinished sentence.
    property bool pending: false
    /// A short readout at the right-hand end -- how many rows the filter found,
    /// say. Inside the box rather than beside it, because what it reports is a
    /// property of what is written here and a box with a number hanging off it
    /// is two controls where the reader sees one. Empty draws nothing and costs
    /// no room.
    property string hint: ""

    implicitHeight: Theme.controlHeight
    font: Theme.monoSmall
    color: Theme.textPrimary
    placeholderTextColor: Theme.textDisabled
    selectionColor: Theme.accent
    selectedTextColor: Theme.accentText
    leftPadding: Theme.gapS
    // Enough room for the readout, so a long filter runs under it rather than
    // through it.
    rightPadding: Theme.gapS + (hintLabel.visible ? hintLabel.width + Theme.gapS : 0)

    Text {
        id: hintLabel

        anchors.right: parent.right
        anchors.rightMargin: Theme.gapS
        anchors.verticalCenter: parent.verticalCenter
        visible: control.hint !== ""
        text: control.hint
        font: Theme.micro
        color: Theme.textSecondary
    }

    background: Rectangle {
        radius: Theme.radiusS
        color: control.pending ? Theme.surfacePending : Theme.surfaceInset
        border.width: (control.activeFocus || control.invalid)
                      ? Theme.borderWidthAccent : Theme.borderWidth
        border.color: control.invalid ? Theme.warning
                    : control.activeFocus ? Theme.accent : Theme.border
    }
}
