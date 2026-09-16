// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import QtQuick.Controls.Basic
import H5Scope.Backend

/// What could be written next, listed under the box it is being written in.
///
/// Two boxes in this application are typed into rather than chosen from: a
/// custom plot's entry, which names a dataset, a subscript and a member all on
/// one line, and the slice bar's own box, which over a compound holds
/// everything after the path. The names in both come out of the file and
/// nowhere the reader can see them, so this is not a convenience -- without it
/// the only way to write `/plotting/events[:].position.x` is to have the tree
/// open beside you and count the dimensions yourself.
///
/// The owner supplies `options` and nothing else. They are whole strings: what
/// the box would hold if one were taken, rather than fragments to splice --
/// because what is being completed is three grammars deep and splicing is work
/// for the thing that knows the grammar.
///
/// Tab behaves the way a shell's does, which is the behaviour a reader already
/// has: it writes as much as every candidate shares, and when there is nothing
/// left to share it takes the one the keyboard is on. That is what makes a list
/// of eight useful without anybody having to arrow through it.
///
/// `Popup.Item` for the reason AppComboBox and AppMenu both give: a platform
/// popup arrives in the host's palette and typeface, and this application draws
/// every surface from Theme.
Popup {
    id: popup

    /// The candidates, in the order they should be offered.
    property var options: []
    /// Which one the keyboard is on. Reset whenever the list changes: the row
    /// that was third a keystroke ago is not the same row now.
    property int highlighted: 0
    /// What is in the box, so Tab can tell whether the shared head would add
    /// anything to it.
    property string written: ""

    /// One was taken; `option` is what the box should now hold.
    signal taken(string option)

    /// Take what Tab should take. Returns false when there was nothing to
    /// write, so the box can let Tab do whatever Tab otherwise does -- which
    /// is move to the next box, and which is what a reader expects of a line
    /// that is already complete.
    function take() {
        if (popup.options.length === 0)
            return false
        const shared = AppController.commonCompletion(popup.options)
        if (shared.length > popup.written.length) {
            // Every candidate starts with what is typed, so this only ever
            // adds. There is still a choice to make, so the list stays up.
            popup.taken(shared)
            return true
        }
        const at = Math.max(0, Math.min(popup.highlighted,
                                        popup.options.length - 1))
        if (popup.options[at] === popup.written)
            return false
        popup.taken(popup.options[at])
        return true
    }

    /// Move the keyboard down the list, wrapping.
    function move(by) {
        if (popup.options.length === 0)
            return
        const count = popup.options.length
        popup.highlighted = ((popup.highlighted + by) % count + count) % count
    }

    onOptionsChanged: popup.highlighted = 0

    /// The widest candidate, measured off the font the rows are set in.
    ///
    /// A Text that elides reports the *elided* line as its implicit width, and
    /// a list sized from its own rows would therefore settle at the width of an
    /// ellipsis -- the latch SliceField carries the same note about. Metrics
    /// cannot elide.
    ///
    /// FontMetrics and its `advanceWidth(text)` *function*, rather than a
    /// TextMetrics whose `text` is assigned in the loop: assigning a property
    /// that the same binding then reads is a binding loop, and QML says so on
    /// every keystroke that changes the list.
    readonly property real widestOption: {
        let widest = 0
        for (let i = 0; i < popup.options.length; ++i) {
            widest = Math.max(widest, metrics.advanceWidth(popup.options[i]))
        }
        return Math.ceil(widest)
    }

    FontMetrics {
        id: metrics

        font: Theme.monoSmall
    }

    y: parent ? parent.height : 0
    // At least as wide as the box it drops from, because that is where the line
    // is going; wider when the box is narrower than what it is offering, which
    // the slice bar's is -- it is only ever as wide as the line in it. Capped,
    // because past the cap what is being given up is the head of a path and the
    // rows already elide from the left.
    width: Math.max(parent ? parent.width : 0,
                    Math.min(Theme.completionWidthMax,
                             popup.widestOption + Theme.gapS * 2
                             + Theme.borderWidth * 2 + Theme.gapM))
    padding: Theme.borderWidth
    popupType: Popup.Item
    // Never takes the keyboard, and never opens or closes itself: the reader
    // is typing in the box and the list is an offer rather than a mode. Whether
    // it is up is a binding the owner writes -- open() and close() would set
    // `visible` imperatively and silently replace that binding, which is the
    // trap AppSlider and AppComboBox both carry a note about.
    closePolicy: Popup.NoAutoFocus

    background: Rectangle {
        color: Theme.surface
        border.width: Theme.borderWidth
        border.color: Theme.borderGuide
    }

    contentItem: ListView {
        id: list

        objectName: "completionList"

        implicitHeight: Math.min(contentHeight, Theme.smallControlHeight * 8)
        model: popup.options
        clip: true
        currentIndex: popup.highlighted
        boundsBehavior: Flickable.StopAtBounds

        ScrollBar.vertical: ScrollBar {}

        delegate: ItemDelegate {
            id: option

            required property int index
            required property var modelData

            width: ListView.view.width
            height: Theme.smallControlHeight
            padding: 0
            leftPadding: Theme.gapS
            rightPadding: Theme.gapS
            highlighted: popup.highlighted === option.index

            background: Rectangle {
                color: option.highlighted ? Theme.surfaceHover : "transparent"
            }

            contentItem: Text {
                id: label

                text: option.modelData
                font: Theme.monoSmall
                color: option.highlighted ? Theme.textEmphasis : Theme.textPrimary
                // A path elides from the left, as it does everywhere else in
                // this application: the end of it is the half that tells one
                // candidate from another.
                elide: Text.ElideLeft
                verticalAlignment: Text.AlignVCenter

                AppToolTip {
                    shown: label.truncated && option.hovered
                    verbatim: true
                    text: option.modelData
                }
            }

            onClicked: popup.taken(option.modelData)
        }
    }
}
