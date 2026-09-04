// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

/// One row in a menu drawer.
///
/// 22px -- deliberately denser than every other control in the UI, because a
/// menu is measured against desktop convention rather than against the panels
/// beside it. Hovering inverts the row to solid signal white with pure black
/// ink, which is the design system's one filled state.
///
/// Every row in every drawer is one of these, including the rows nobody
/// declares: AppMenu hands this type to Qt as its `delegate`, so a submenu's
/// title row is drawn on the same gutter, in the same faces, as the rows above
/// and below it. It used to be left to the Basic style's own MenuItem, which
/// arrives in Qt's palette rather than in Theme's -- black ink on the drawer's
/// near-black ground, which reads as a row that has been disabled.
MenuItem {
    id: control

    /// Right-aligned in the row. An Action's `shortcut` is functional but is
    /// never rendered by MenuItem, so the text is drawn here -- and read back
    /// off the action itself, so the binding and the label cannot drift apart.
    property string shortcutText:
        (control.action && typeof control.action.shortcut === "string")
        ? control.action.shortcut : ""

    /// Ceiling on the trailing column. A key name is four characters and needs
    /// none; a row whose second fact is a folder needs one, or a single deep
    /// path would set the width of the whole drawer. Zero leaves it unbounded.
    property int shortcutMaxWidth: 0

    /// Draws the bullet in the mark gutter. The design system has no checkmark
    /// glyph -- a set state is a dot. Kept separate from MenuItem's own
    /// `checked` so the mark can follow a binding (Theme.dark, the current tab)
    /// that triggering the row would otherwise overwrite.
    property bool marked: false

    /// Whether this row opens a drawer of its own. Qt sets `subMenu` on the
    /// item it creates for a nested Menu, which is what the caret below reads.
    readonly property bool opensSubMenu: control.subMenu !== null

    // How wide the two texts want to be, measured rather than read off the
    // Texts that draw them.
    //
    // Both of those elide, and a Text that elides reports an implicit width
    // that depends on the width it was given -- which, through this row's own
    // implicitWidth and the drawer's, is a width derived from it. The loop
    // settles somewhere narrower than the string, which is why every shortcut
    // in the File drawer came out as "...rl+O". TextMetrics answers the same
    // question about the same string with nothing in the way.
    //
    // It answers it in a slightly different currency, though: a Text laying out
    // "Ctrl+W" wants 35.36px where TextMetrics reports 35.00, and a column cut
    // to the second elides by a third of a pixel. Hence the widest of the two
    // measurements and a hairline on top -- the alternative is a fraction of a
    // pixel of slack that shows up as a lost character.
    TextMetrics {
        id: labelMetrics

        font: Theme.bodySmall
        text: control.text
    }

    TextMetrics {
        id: shortcutMetrics

        font: Theme.readout
        text: control.shortcutText
    }

    function measured(metrics) {
        return Math.ceil(Math.max(metrics.width, metrics.advanceWidth)) + Theme.s1
    }

    /// The trailing column's width: the caret's slot for a row that opens a
    /// drawer, otherwise the shortcut, bounded.
    readonly property int trailingWidth: {
        if (control.opensSubMenu)
            return Theme.menuMarkWidth
        const wanted = control.measured(shortcutMetrics)
        return control.shortcutMaxWidth > 0
               ? Math.min(wanted, control.shortcutMaxWidth) : wanted
    }

    implicitHeight: Theme.tinyControlHeight
    // Padding, the mark gutter, the two RowLayout gaps, and the two columns.
    implicitWidth: Theme.gapL * 2 + Theme.menuMarkWidth + Theme.gapL * 2
                   + control.measured(labelMetrics) + control.trailingWidth

    leftPadding: Theme.gapL
    rightPadding: Theme.gapL
    topPadding: 0
    bottomPadding: 0

    hoverEnabled: true
    // Disabled is opacity rather than a second palette, exactly as
    // AppToolButton does it, so the row stays legible on the drawer behind it.
    opacity: control.enabled ? 1.0 : 0.45

    // Neither the checkbox indicator nor the submenu arrow the Basic style
    // would otherwise draw has a place here: the mark is a dot in the gutter
    // below, and the caret is drawn in the row's own trailing column, in the
    // same glyph and the same face the tree draws its expanders with.
    indicator: null
    arrow: null

    background: Rectangle {
        color: control.highlighted ? Theme.accent : Theme.clear(Theme.accent)
    }

    contentItem: RowLayout {
        spacing: Theme.gapL

        Text {
            Layout.preferredWidth: Theme.menuMarkWidth
            Layout.fillHeight: true
            text: control.marked ? "•" : ""
            font: Theme.readout
            color: control.highlighted ? Theme.accentText : Theme.textPrimary
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }

        Text {
            id: label

            Layout.fillWidth: true
            Layout.fillHeight: true
            // The row's subject, and the last thing to give up space: when the
            // drawer is narrower than the row wants, the trailing column is
            // what elides. Without a floor here a long folder path takes the
            // whole row and the file name it belongs to disappears.
            Layout.minimumWidth: Theme.s12
            text: control.text
            font: Theme.bodySmall
            color: control.highlighted ? Theme.accentText : Theme.textPrimary
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter

            AppToolTip {
                shown: label.truncated && control.hovered
                text: control.text
            }
        }

        // The row's second fact: the key that triggers it, the folder a file
        // came out of, or -- for a row that opens a drawer -- the caret saying
        // so. Never two of the three, so they share one column and the rows of
        // a drawer keep one right edge.
        Item {
            Layout.fillHeight: true
            Layout.preferredWidth: control.trailingWidth
            Layout.maximumWidth: control.trailingWidth
            // Shrinks before the label does; see the note there.
            Layout.minimumWidth: 0

            Text {
                id: shortcutLabel

                anchors.fill: parent
                visible: !control.opensSubMenu
                text: control.shortcutText
                font: Theme.readout
                // On an inverted row the shortcut steps back to --n-6 rather
                // than taking the label's black: it is a hint, not the row's
                // subject.
                color: control.highlighted ? Theme.borderStrong
                                           : Theme.textDisabled
                // A folder is read from its tail, which is the part that says
                // which of two similarly-named directories this is.
                elide: Text.ElideLeft
                horizontalAlignment: Text.AlignRight
                verticalAlignment: Text.AlignVCenter

                AppToolTip {
                    shown: shortcutLabel.truncated && control.hovered
                    verbatim: true
                    text: control.shortcutText
                }
            }

            Text {
                anchors.fill: parent
                visible: control.opensSubMenu
                // The tree's expander glyph, in the tree's expander face: a
                // caret is punctuation everywhere in this application, and a
                // drawer that opens to the side points at where it opens.
                text: "▸"
                font: Theme.caret
                color: control.highlighted ? Theme.accentText
                                           : Theme.textSecondary
                horizontalAlignment: Text.AlignRight
                verticalAlignment: Text.AlignVCenter
            }
        }
    }
}
