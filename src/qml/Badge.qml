// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick

/// Status carried by colour and mono text, never by an icon glyph -- the
/// system has no check or warning symbols. 18px tall, which is the one badge
/// size it defines; `compact` is the tighter variant the tree's tags use,
/// where three of these have to fit between a name and its shape.
Rectangle {
    id: badge

    property string text
    /// neutral | ok | warn | crit | info
    property string tone: "neutral"
    property bool solid: false
    /// Shorter and tighter, for a badge sitting inside a 26px table row.
    property bool compact: false

    readonly property color toneColor: {
        switch (tone) {
        case "ok":   return Theme.accent
        case "warn": return Theme.warning
        case "crit": return Theme.danger
        case "info": return Theme.info
        default:     return Theme.textSecondary
        }
    }

    readonly property int sidePad: compact ? Theme.gapXS : Theme.gapS

    implicitHeight: compact ? Theme.badgeHeightCompact : Theme.badgeHeight
    implicitWidth: label.implicitWidth + sidePad * 2
    radius: Theme.radiusS
    color: badge.solid ? toneColor : "transparent"
    border.width: Theme.borderWidth
    border.color: toneColor

    Text {
        id: label

        anchors.centerIn: parent
        // The system's machine labels are tracked, and tracking is applied
        // *after* the last glyph as well as between them -- so a centred run
        // of them sits half a track left of centre, which on a one-letter tag
        // is the difference between a badge and a badge with something wrong
        // with it. Give the trailing space back.
        anchors.horizontalCenterOffset: label.font.letterSpacing / 2
        text: badge.text
        font: Theme.micro
        color: badge.solid ? Theme.accentText : badge.toneColor
    }
}
