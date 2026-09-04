// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import H5Scope.Backend

/// The 26px strip along the bottom: ambient machine state, read left to right.
/// `·` is the system's mono metadata separator.
Rectangle {
    id: strip

    implicitHeight: Theme.statusBarHeight
    color: Theme.surface

    Rectangle {
        anchors.top: parent.top
        width: parent.width
        height: Theme.borderWidth
        color: Theme.border
    }

    Text {
        id: leftSegments

        anchors.left: parent.left
        anchors.leftMargin: Theme.gapM
        anchors.verticalCenter: parent.verticalCenter
        anchors.right: activity.left
        anchors.rightMargin: Theme.gapM
        text: AppController.statusLeft.join("  ·  ")
        font: Theme.microLabel
        color: Theme.textSecondary
        elide: Text.ElideMiddle

        HoverHandler { id: leftHover }

        // The selected object's path lives here. A narrow window takes the
        // middle out of it, which is where a path says which of several
        // similarly named things it is.
        AppToolTip {
            shown: leftSegments.truncated && leftHover.hovered
            verbatim: true
            text: AppController.statusLeft.join("   ")
        }
    }

    // Whether the file is being read, in the one place a reader already looks
    // for what the machine is doing.
    //
    // A word rather than a spinner, and it earns its place by what it rules
    // out: every reading in this window arrives from another thread a moment
    // after it is asked for, so a pane that has not filled in yet looks exactly
    // like a pane with nothing in it. This is the difference. It says nothing
    // at all when there is nothing outstanding, which is nearly always.
    Text {
        id: activity

        anchors.right: rightSegments.left
        anchors.rightMargin: visible ? Theme.gapM : 0
        anchors.verticalCenter: parent.verticalCenter
        visible: AppController.busy
        text: qsTr("reading")
        font: Theme.microLabel
        color: Theme.textSecondary
    }

    Text {
        id: rightSegments
        anchors.right: parent.right
        anchors.rightMargin: Theme.gapM
        anchors.verticalCenter: parent.verticalCenter
        text: AppController.statusRight.join("  ·  ")
        font: Theme.microLabel
        color: Theme.textDisabled
    }
}
