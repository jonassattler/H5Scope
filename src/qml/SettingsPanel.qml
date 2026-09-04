// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick

/// Chrome shared by every panel in the Data Viewer's right-hand rail: the
/// heading bar the table setup panel already draws, and a scrolling column
/// beneath it.
///
/// One rail holds one panel at a time, so these all have to line up with each
/// other -- and with TableSetupPanel, which is the one that established the
/// shape.
Rectangle {
    id: panel

    property string title
    default property alias content: body.data

    color: Theme.surface

    Rectangle {
        id: heading

        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: Theme.treeHeaderHeight
        color: Theme.surface

        Text {
            anchors.left: parent.left
            anchors.leftMargin: Theme.gapM
            anchors.verticalCenter: parent.verticalCenter
            text: panel.title
            font: Theme.micro
            color: Theme.textSecondary
        }

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: Theme.borderWidth
            color: Theme.border
        }
    }

    Flickable {
        id: scroller

        anchors.top: heading.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        clip: true
        contentHeight: body.implicitHeight + Theme.gapM * 2
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: body

            x: Theme.gapM
            y: Theme.gapM
            width: scroller.width - Theme.gapM * 2
            spacing: Theme.gapM
        }
    }
}
