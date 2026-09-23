// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import QtQuick.Controls.Basic

/// The box a pipeline is written in as text: the postprocessing panel's, when
/// visual editing is off, and a custom plot's line, when postprocessing is on
/// for it. One component for both because the two are one grammar -- see
/// postproc::Script -- and a reader who learns the box in one place should
/// find it behaving the same in the other.
///
/// It keeps FilterInput's contract, which every box in this application that
/// acts on data keeps: the border goes amber when the text will not read, the
/// ground steps when what is in the box has not been applied, and nothing is
/// applied until the reader commits. Return commits -- a script is one
/// statement however many lines it is written over, and the line breaks are
/// optional -- so a line break of the reader's own is Shift+Return.
///
/// It grows with its lines up to Theme.scriptFieldMaxHeight and scrolls past
/// that. Long lines wrap rather than scroll sideways, because the rails this
/// sits in are narrow and a slice read a character at a time through a
/// sideways scroll is not being read.
ScrollView {
    id: field

    property alias text: area.text
    property alias placeholderText: area.placeholderText
    property alias cursorPosition: area.cursorPosition
    /// Whether the reader is in the box. Its own name rather than
    /// `activeFocus`, which is the scroll view's and not the text's.
    readonly property alias editing: area.activeFocus

    /// See FilterInput: the text cannot be used.
    property bool invalid: false
    /// See FilterInput: what is in the box reads and has not been applied.
    property bool pending: false

    /// Return, without Shift. What committing the box means.
    signal accepted()
    /// The reader changed the text. Not emitted for text set from outside
    /// while the box does not have focus, which is how the model puts back
    /// what it made of a commit.
    signal textEdited()
    /// Escape.
    signal cancelled()
    /// Tab, just before `completion` is asked to take it: the moment for the
    /// owner to bring the list up to date with what is in the box.
    signal completing()

    /// A CompletionPopup this box drives from the keyboard, or null. Tab
    /// writes as much as every candidate shares and only then chooses; Up and
    /// Down move through the list while it is showing and the caret otherwise.
    /// With nothing to take, Tab is let through and moves focus on, as it does
    /// everywhere else in this window.
    property var completion: null

    function forceEditing() {
        area.forceActiveFocus()
    }

    implicitWidth: Theme.s14 * 2
    implicitHeight: Math.min(area.implicitHeight, Theme.scriptFieldMaxHeight)
    clip: true
    padding: 0
    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

    background: Rectangle {
        radius: Theme.radiusS
        color: field.pending ? Theme.surfacePending : Theme.surfaceInset
        border.width: (area.activeFocus || field.invalid)
                      ? Theme.borderWidthAccent : Theme.borderWidth
        border.color: field.invalid ? Theme.warning
                    : area.activeFocus ? Theme.accent : Theme.border
    }

    TextArea {
        id: area

        objectName: "scriptText"

        font: Theme.monoSmall
        color: Theme.textPrimary
        placeholderTextColor: Theme.textDisabled
        selectionColor: Theme.accent
        selectedTextColor: Theme.accentText
        textFormat: TextEdit.PlainText
        wrapMode: TextEdit.WrapAtWordBoundaryOrAnywhere
        leftPadding: Theme.gapS
        rightPadding: Theme.gapS
        topPadding: Theme.gapXS
        bottomPadding: Theme.gapXS
        // The scroll view draws the ground; a second one here would sit
        // inside it and cover the pending step.
        background: null

        onTextChanged: if (area.activeFocus) field.textEdited()

        Keys.onReturnPressed: (event) => field.commitKey(event)
        Keys.onEnterPressed: (event) => field.commitKey(event)
        Keys.onEscapePressed: field.cancelled()
        Keys.onTabPressed: (event) => {
            field.completing()
            event.accepted = field.completion !== null && field.completion.take()
        }
        Keys.onUpPressed: (event) => field.stepCompletion(event, -1)
        Keys.onDownPressed: (event) => field.stepCompletion(event, 1)
    }

    function stepCompletion(event, by) {
        event.accepted = field.completion !== null && field.completion.visible
        if (event.accepted)
            field.completion.move(by)
    }

    function commitKey(event) {
        if (event.modifiers & Qt.ShiftModifier) {
            event.accepted = false // the text area writes the line break
            return
        }
        event.accepted = true
        field.accepted()
    }
}
