// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import QtQuick.Controls.Basic

/// A labelled toggle, built on the same indicator as the file picker's
/// checkbox and the table setup panel's axis boxes: inset well, hairline rim,
/// signal-white core when ticked. A checkbox and a radio differ in meaning,
/// not in material, so only the corner radius separates this from
/// AppRadioButton.
CheckBox {
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
        radius: Theme.radiusS
        color: Theme.surfaceInset
        border.width: Theme.borderWidth
        border.color: control.hovered ? Theme.borderStrong : Theme.border

        Rectangle {
            anchors.centerIn: parent
            width: parent.width - Theme.borderWidthAccent * 2
            height: width
            radius: Theme.radiusS
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
