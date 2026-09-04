// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick

/// Text the pointer can take a copy of.
///
/// A Text is a picture of a string: the reader can see a path or a type name
/// and cannot get it out of the window except by typing it back in. Everything
/// the Information tab shows is a fact about a file that a reader has some
/// other program to paste into -- a path into a script, a filter name into a
/// search, a shape into a note -- so on that tab the strings are these rather
/// than Texts.
///
/// A read-only TextEdit rather than a Text with a selection bolted on, because
/// that is what Qt Quick has: selection lives in the editing types. Read-only
/// keeps it a display -- no caret, no input method, nothing to type into -- and
/// the only thing it adds over a Text is that a drag across it selects, and
/// ctrl+C copies.
///
/// It has no `elide`. TextEdit cannot elide, and the substitute is better here
/// anyway: it wraps. A label that will not fit on one line takes two, which
/// costs a row of height and loses nothing, where an ellipsis loses the end of
/// the string and needs a tooltip to give it back.
TextEdit {
    id: control

    readOnly: true
    selectByMouse: true
    // A read-only editor still draws a caret once it has focus, which on a
    // panel of facts reads as an invitation to type into it.
    cursorVisible: false
    // Values arrive from the file. A dataset called `<b>` is a dataset called
    // `<b>`, not an instruction to the renderer.
    textFormat: Text.PlainText
    wrapMode: Text.Wrap
    color: Theme.textPrimary
    selectionColor: Theme.accent
    selectedTextColor: Theme.accentText
}
