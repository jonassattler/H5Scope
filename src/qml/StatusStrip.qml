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
        anchors.right: rightSegments.left
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
