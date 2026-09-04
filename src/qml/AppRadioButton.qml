// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import QtQuick.Controls.Basic

/// The one round control in the application. Built on the same indicator the
/// file picker's checkbox uses -- inset well, hairline rim, signal-white core
/// when chosen -- because a radio and a checkbox differ in meaning, not in
/// material.
RadioButton {
    id: control

    font: Theme.label
    hoverEnabled: true
    opacity: control.enabled ? 1.0 : 0.4
    padding: 0
    spacing: Theme.gapS

    indicator: Rectangle {
        implicitWidth: Theme.indicatorSize
        implicitHeight: Theme.indicatorSize
        x: control.leftPadding
        anchors.verticalCenter: parent.verticalCenter
        radius: width / 2
        color: Theme.surfaceInset
        border.width: Theme.borderWidth
        border.color: control.hovered ? Theme.borderStrong : Theme.border

        Rectangle {
            anchors.centerIn: parent
            width: parent.width - Theme.borderWidthAccent * 2
            height: width
            radius: width / 2
            color: Theme.accent
            visible: control.checked
        }
    }

    contentItem: Text {
        text: control.text
        font: control.font
        color: control.checked ? Theme.textPrimary : Theme.textSecondary
        verticalAlignment: Text.AlignVCenter
        leftPadding: control.indicator.width + control.spacing
    }
}
