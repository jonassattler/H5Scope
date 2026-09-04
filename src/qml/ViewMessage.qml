// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick

/// What one of the Data Viewer's presentations says when it has nothing to
/// draw: the reason, centred, and nothing else.
///
/// All three views need this and all three needed it worded the same way, so
/// it is one component. An error names what is wrong and states the
/// consequence; it never apologises.
Item {
    id: message

    property string title
    property string text
    /// Hazard amber rather than muted grey: something is wrong with the data
    /// rather than merely absent.
    property bool warning: false

    Column {
        anchors.centerIn: parent
        width: Math.min(420, message.width - Theme.gapXL * 2)
        spacing: Theme.gapS

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: message.title !== ""
            text: message.title
            font: Theme.micro
            color: Theme.textSecondary
        }

        Text {
            width: parent.width
            text: message.text
            font: Theme.body
            color: message.warning ? Theme.warning : Theme.textSecondary
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }
    }
}
