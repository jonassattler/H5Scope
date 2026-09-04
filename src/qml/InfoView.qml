// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import H5Scope.Backend

/// "Information" tab: everything known about the selection, as a reflowing set
/// of Panels -- object, dataspace, datatype, storage, attributes.
///
/// Laid out in columns rather than in a grid. A grid puts the panels in rows,
/// and a row is as tall as the tallest thing in it -- so one panel carrying a
/// sentence-long warning left every panel beside it sitting over a hole the
/// height of that warning, which is the "too much space between the cards"
/// that has nothing visible causing it. Columns have no rows to line up, so a
/// short panel is followed by the next one and nothing else.
///
/// The attributes panel spans the full width below them, because attribute
/// values are free text and are the one thing here that benefits from the room.
Rectangle {
    id: root

    color: Theme.background

    readonly property var panels: AppController.infoPanels

    /// The one panel that takes the whole width. Named rather than tested for
    /// in three places.
    readonly property string wideTitle: "attributes"

    ScrollView {
        id: scroll

        anchors.fill: parent
        anchors.margins: Theme.gapXL
        contentWidth: availableWidth
        clip: true
        visible: root.panels.length > 0

        Column {
            id: page

            width: scroll.availableWidth
            spacing: Theme.gutter

            // The CSS is repeat(auto-fit, minmax(340px, 1fr)); this is the
            // same rule arithmetic, since QML has no auto-fit.
            readonly property int columns:
                Math.max(1, Math.floor((width + Theme.gutter)
                                       / (Theme.panelMinWidth + Theme.gutter)))
            readonly property real columnWidth:
                (width - Theme.gutter * (columns - 1)) / columns

            /// The panels dealt into `columns` lists, in order, each panel
            /// going to whichever column is currently shortest.
            ///
            /// Shortest by row count, not by measured height: a layout that
            /// reads the heights it is deciding is a binding loop, and a row
            /// count is a good enough proxy because every row is one line of
            /// the same two faces. It is only choosing which column, and being
            /// a row or two out chooses the same column nearly always.
            readonly property var columnised: {
                const lists = []
                const weights = []
                for (let c = 0; c < columns; ++c) {
                    lists.push([])
                    weights.push(0)
                }
                for (const panel of root.panels) {
                    if (panel.title === root.wideTitle)
                        continue
                    let shortest = 0
                    for (let c = 1; c < columns; ++c) {
                        if (weights[c] < weights[shortest])
                            shortest = c
                    }
                    lists[shortest].push(panel)
                    // The header, plus a row each -- or the sentence a panel
                    // with nothing to list says instead, which is about two.
                    weights[shortest] +=
                        2 + (panel.rows === undefined ? 0 : panel.rows.length)
                }
                return lists
            }

            readonly property var widePanels:
                root.panels.filter(panel => panel.title === root.wideTitle)

            Row {
                width: parent.width
                spacing: Theme.gutter

                Repeater {
                    model: page.columnised

                    delegate: Column {
                        required property var modelData

                        width: page.columnWidth
                        spacing: Theme.gutter

                        Repeater {
                            model: parent.modelData
                            delegate: InfoPanel { width: parent.width }
                        }
                    }
                }
            }

            Repeater {
                model: page.widePanels
                delegate: InfoPanel { width: page.width }
            }
        }
    }

    /// One panel and its rows. An inline component so the column layout above
    /// can instantiate it in two places without either being a copy.
    component InfoPanel: Panel {
        id: panel

        required property var modelData

        readonly property bool empty: panel.modelData.emptyText !== undefined
                                      && panel.modelData.emptyText !== ""
        readonly property real bodyWidth:
            panel.width - panel.contentPadding * 2 - panel.border.width * 2

        title: modelData.title
        meta: modelData.meta
        accent: modelData.accent

        // Nothing to list, and a panel that says so in words. An empty table
        // headed "attributes" holding a single row reading "Attributes  0"
        // reads as something that failed to load rather than as an object that
        // simply has none.
        Text {
            width: panel.bodyWidth
            visible: panel.empty
            height: visible ? implicitHeight + Theme.gapS * 2 : 0
            topPadding: Theme.gapS
            bottomPadding: Theme.gapS
            text: panel.empty ? panel.modelData.emptyText : ""
            font: Theme.caption
            color: Theme.textDisabled
            wrapMode: Text.WordWrap
        }

        Repeater {
            model: panel.empty ? [] : panel.modelData.rows

            delegate: Item {
                id: infoRow

                required property int index
                required property var modelData

                readonly property bool last:
                    infoRow.index === panel.modelData.rows.length - 1

                width: panel.bodyWidth
                implicitHeight: Math.max(key.implicitHeight,
                                         value.implicitHeight)
                                + Theme.gapS + Theme.gapS

                // The label column, wide enough for the longest label the
                // application produces and never more than a share of the row
                // -- a panel spanning the window must not spend a third of it
                // on the word "Path". It had been a flat 116px, which is
                // narrower than "IMAGE_SUBCLASS" and so cut the names of the
                // attributes off in the one panel that is all names.
                SelectableText {
                    id: key

                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.topMargin: Theme.gapS
                    width: Math.min(Theme.infoLabelWidth,
                                    infoRow.width * 0.45)
                    text: infoRow.modelData.label
                    font: Theme.micro
                    color: Theme.textSecondary
                }

                SelectableText {
                    id: value

                    anchors.left: key.right
                    anchors.leftMargin: Theme.gapL
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.topMargin: Theme.gapS
                    text: infoRow.modelData.value
                    font: Theme.mono
                    color: infoRow.modelData.isWarning
                           ? Theme.warning : Theme.textPrimary
                    // Words first, and only inside a word when the word is
                    // wider than the column -- which a path is and a sentence
                    // is not. It had been WrapAnywhere for every value alike,
                    // so a warning broke mid-syllable: "which thi / s shape".
                    wrapMode: Text.Wrap
                }

                // Between the rows, not under the last of them: a rule along
                // the foot of the panel is the panel's own border drawn twice,
                // a hairline's width above itself.
                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: Theme.borderWidth
                    color: Theme.surfaceRaised
                    visible: !infoRow.last
                }
            }
        }
    }

    Column {
        anchors.centerIn: parent
        spacing: Theme.gapS
        visible: root.panels.length === 0

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("no object selected")
            font: Theme.micro
            color: Theme.textSecondary
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: AppController.hasFile
                  ? qsTr("Select a group or dataset in the tree to describe it.")
                  : qsTr("Open an HDF5 file to begin.")
            font: Theme.body
            color: Theme.textSecondary
        }
    }
}
