// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import QtQuick.Controls.Basic

/// The rule between two groups of menu rows.
///
/// A hairline like every other boundary in the system, inset by the drawer's
/// own vertical padding so the groups either side of it keep the same rhythm
/// as the rows within them.
MenuSeparator {
    id: control

    padding: 0
    topPadding: Theme.s3
    bottomPadding: Theme.s3

    contentItem: Rectangle {
        implicitHeight: Theme.borderWidth
        color: Theme.border
    }

    background: null
}
