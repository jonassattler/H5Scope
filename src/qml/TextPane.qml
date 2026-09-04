// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import QtQuick.Controls.Basic

/// One string, shown whole.
///
/// A grid cell can only ever show the first few characters of a string and an
/// ellipsis, which for text is the same as showing nothing. This is the other
/// treatment: the Panel chrome the rest of the application uses, with the
/// entire value inside it, wrapped, selectable and copyable.
///
/// Two sizings, one component. `scrolls: false` grows the pane to its content,
/// so a stack of them shows every string in full and the outer view does the
/// scrolling. `scrolls: true` takes the height it is given and scrolls inside,
/// which is what a single long document wants.
Rectangle {
    id: pane

    property string title
    property string meta
    property alias text: body.text
    property bool scrolls: false

    color: Theme.surface
    radius: Theme.radiusS
    border.width: Theme.borderWidth
    border.color: Theme.border

    implicitHeight: scrolls
        ? Theme.textPaneMinHeight
        : Math.max(Theme.textPaneMinHeight,
                   headerBar.height + body.contentHeight
                   + Theme.gapM * 2 + border.width * 2)

    Item {
        id: headerBar

        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: pane.border.width
        height: Theme.treeHeaderHeight

        Text {
            anchors.left: parent.left
            anchors.leftMargin: Theme.gapM
            anchors.verticalCenter: parent.verticalCenter
            text: pane.title
            font: Theme.micro
            color: Theme.textSecondary
        }

        Text {
            anchors.right: parent.right
            anchors.rightMargin: Theme.gapM
            anchors.verticalCenter: parent.verticalCenter
            text: pane.meta
            font: Theme.readout
            color: Theme.textDisabled
        }

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: Theme.borderWidth
            color: Theme.border
        }
    }

    Flickable {
        id: viewport

        anchors.top: headerBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Theme.gapM
        anchors.topMargin: Theme.gapM
        clip: pane.scrolls
        interactive: pane.scrolls
        contentWidth: width
        contentHeight: body.contentHeight
        boundsBehavior: Flickable.StopAtBounds

        ScrollBar.vertical: ScrollBar {
            policy: pane.scrolls ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
        }

        TextEdit {
            id: body

            width: viewport.width
            // Read-only but not inert: text one cannot select is text one
            // cannot get out of the program.
            readOnly: true
            selectByMouse: true
            selectByKeyboard: true
            persistentSelection: true
            textFormat: TextEdit.PlainText
            wrapMode: TextEdit.Wrap
            font: Theme.mono
            color: Theme.textPrimary
            selectionColor: Theme.accent
            selectedTextColor: Theme.accentText
        }
    }
}
