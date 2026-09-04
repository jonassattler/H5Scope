// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick

/// The design system's one card treatment: a hairline border, a 2px radius, a
/// mono uppercase header bar and no drop shadow. Nothing in this application
/// is a "card" in any other sense.
///
/// `accent` draws the 2px signal-white rule along the top edge. The system
/// rations that to one Panel per screen, so the Information tab marks only the
/// object panel with it.
Rectangle {
    id: panel

    property string title
    property string meta
    property bool accent: false
    property int contentPadding: Theme.gapL

    default property alias content: body.data

    // One step above the page, so the row separators inside -- which sit a
    // further step up -- stay visible against it.
    color: Theme.surface
    radius: Theme.radiusS
    border.width: Theme.borderWidth
    border.color: Theme.border

    implicitHeight: headerBar.height + body.implicitHeight + contentPadding * 2
                    + border.width * 2

    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: panel.radius
        anchors.rightMargin: panel.radius
        height: Theme.borderWidthAccent
        color: Theme.accent
        visible: panel.accent
        z: 1
    }

    Item {
        id: headerBar

        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: panel.border.width
        height: Theme.treeHeaderHeight

        Text {
            anchors.left: parent.left
            anchors.leftMargin: Theme.gapM
            anchors.verticalCenter: parent.verticalCenter
            text: panel.title
            font: Theme.micro
            color: Theme.textSecondary
        }

        Text {
            id: metaLabel

            anchors.right: parent.right
            anchors.rightMargin: Theme.gapM
            anchors.verticalCenter: parent.verticalCenter
            text: panel.meta
            font: Theme.readout
            color: Theme.textDisabled
            elide: Text.ElideRight

            HoverHandler { id: metaHover }

            AppToolTip {
                shown: metaLabel.truncated && metaHover.hovered
                verbatim: true
                text: panel.meta
            }
        }

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: Theme.borderWidth
            color: Theme.border
        }
    }

    Column {
        id: body

        anchors.top: headerBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: panel.contentPadding
        anchors.leftMargin: panel.contentPadding + panel.border.width
        anchors.rightMargin: panel.contentPadding + panel.border.width
    }
}
