// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick

/// One setting: its name above, its control below.
///
/// Stacked rather than side by side because the rail is 248px wide and a
/// slider next to a label has room for neither. The label takes the system's
/// machine-label face, like every other name of a thing in this application.
///
/// Columns all the way down rather than anchors and `childrenRect`: a height
/// bound to `childrenRect.height` on an item whose children are anchored to it
/// is the shape a binding loop takes, and this application treats a QML
/// warning as a build failure.
Column {
    id: row

    property string label
    default property alias content: holder.data

    width: parent ? parent.width : 0
    spacing: Theme.gapXS

    Text {
        text: row.label
        font: Theme.microLabel
        color: Theme.textDisabled
    }

    Column {
        id: holder

        width: parent.width
        spacing: Theme.gapXS
    }
}
