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
/// left to share it takes the first. That is what makes a list of eight useful
/// without anybody having to arrow through it.
///
/// **A row the reader has moved onto is a choice, and every way of taking one
/// takes it.** Up and Down move onto a row; Tab, Return and a click then all
/// write that row, whatever the rows share. Until the reader moves, no row is
/// chosen, and that is what leaves Return free to mean what it means in every
/// other box here -- apply what is typed. A list that came up with its first
/// row already chosen would have Return write `.time` over a slice the reader
/// had just finished typing and wanted applied.
///
/// All three were broken at once, each in its own way. Tab asked the owner to
/// bring the list up to date before taking from it, and a list handed over
/// anew forgot which row it was on, so Tab wrote the first row whichever one
/// was lit. Return was never offered to the list at all. And a click could not
/// land: a row took the keyboard on the press, the box it was a list for lost
/// it, and the list -- which is only up while the box has the keyboard --
/// was gone before the release arrived. The rows take no keyboard now, and a
/// list handed over again keeps the row it was on wherever that row still is.
///
/// `Popup.Item` for the reason AppComboBox and AppMenu both give: a platform
/// popup arrives in the host's palette and typeface, and this application draws
/// every surface from Theme.
Popup {
    id: popup

    /// The candidates, in the order they should be offered.
    property var options: []
    /// The row the reader has moved onto, or "" while they have not.
    ///
    /// Held as what the row *says* rather than where it was, and that is the
    /// whole of the Tab fix: the owners hand over a fresh array on every
    /// keystroke and on every Tab, and the row that was third a moment ago may
    /// be first now or not there at all. It used to be an index, reset to the
    /// first row on every change, so Tab -- which refreshes the list before it
    /// takes from it -- always took the first.
    property string chosen: ""
    /// Where that row is in the list now, or -1.
    readonly property int highlighted:
        popup.chosen === "" ? -1 : popup.options.indexOf(popup.chosen)
    /// What is in the box, so Tab can tell whether the shared head would add
    /// anything to it.
    property string written: ""

    /// One was taken; `option` is what the box should now hold.
    signal taken(string option)

    // A choice that has left the list is forgotten, so that it does not come
    // back lit when a later keystroke happens to offer it again.
    onOptionsChanged: {
        if (popup.chosen !== "" && popup.options.indexOf(popup.chosen) < 0)
            popup.chosen = ""
    }

    /// Write `option` into the box. Whatever was chosen has now been written,
    /// so nothing is chosen afterwards: the next Return applies it.
    function write(option) {
        popup.chosen = ""
        popup.taken(option)
    }

    /// Take what Tab should take. Returns false when there was nothing to
    /// write, so the box can let Tab do whatever Tab otherwise does -- which
    /// is move to the next box, and which is what a reader expects of a line
    /// that is already complete.
    function take() {
        if (popup.options.length === 0)
            return false
        // A row the reader moved onto is what they want, whatever the rows
        // have in common: they have already made the choice the shared head
        // would have left to them.
        if (popup.highlighted >= 0 && popup.chosen !== popup.written) {
            popup.write(popup.chosen)
            return true
        }
        const shared = AppController.commonCompletion(popup.options)
        if (shared.length > popup.written.length) {
            // Every candidate starts with what is typed, so this only ever
            // adds. There is still a choice to make, so the list stays up.
            popup.write(shared)
            return true
        }
        if (popup.options[0] === popup.written)
            return false
        popup.write(popup.options[0])
        return true
    }

    /// Take what Return should take: the row the reader moved onto, and only
    /// that. False when there is none, so the box can apply what is typed --
    /// which is what Return means everywhere else in this window, and what it
    /// still means here until the reader has chosen a row.
    function takeChosen() {
        if (!popup.visible || popup.highlighted < 0 || popup.chosen === popup.written)
            return false
        popup.write(popup.chosen)
        return true
    }

    /// Move the keyboard down the list, wrapping. From no row, down is the
    /// first and up is the last.
    function move(by) {
        const count = popup.options.length
        if (count === 0)
            return
        const at = popup.highlighted < 0
                   ? (by > 0 ? 0 : count - 1)
                   : ((popup.highlighted + by) % count + count) % count
        popup.chosen = popup.options[at]
        list.positionViewAtIndex(at, ListView.Contain)
    }

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

    // Both halves of the popup say they are part of the box being edited, so
    // that FocusRelease -- which ends an edit on any press outside the box --
    // lets a press here through. The border is a pixel wide and a press on it
    // is still a press on the list.
    background: Rectangle {
        readonly property bool keepsTextFocus: true

        color: Theme.surface
        border.width: Theme.borderWidth
        border.color: Theme.borderGuide
    }

    contentItem: ListView {
        id: list

        objectName: "completionList"

        readonly property bool keepsTextFocus: true

        implicitHeight: Math.min(contentHeight, Theme.smallControlHeight * 8)
        model: popup.options
        clip: true
        currentIndex: popup.highlighted
        boundsBehavior: Flickable.StopAtBounds

        // Neither the bar nor a row takes the keyboard. The list is only up
        // while the box it belongs to has it, so anything in here that took it
        // on a press would close the list before the release could land.
        ScrollBar.vertical: ScrollBar {
            focusPolicy: Qt.NoFocus
        }

        delegate: ItemDelegate {
            id: option

            required property int index
            required property var modelData

            width: ListView.view.width
            height: Theme.smallControlHeight
            padding: 0
            leftPadding: Theme.gapS
            rightPadding: Theme.gapS
            focusPolicy: Qt.NoFocus
            highlighted: popup.highlighted === option.index

            // The pointer lights a row the way the keyboard does, because a
            // click takes it the way Return takes the lit one. Hovering is not
            // choosing, though: Return still takes only what the keyboard is on.
            background: Rectangle {
                color: option.highlighted || option.hovered ? Theme.surfaceHover
                                                            : "transparent"
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

            onClicked: popup.write(option.modelData)
        }
    }
}
