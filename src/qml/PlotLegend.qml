// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import H5Scope.Backend

/// The lines in the plot, named and tickable: a panel that opens over the
/// left edge of the plot itself rather than into the rail on the right.
///
/// It is on the left because it belongs to the picture and not to the settings.
/// The rail answers "how should this be drawn"; the legend answers "which line
/// is that one", which is a question about what is on screen and is asked with
/// the pointer already over it.
///
/// Every line in the table is listed, not only the ones being drawn. A new
/// selection opens on the first sixty-four -- see DatasetPlot::reseed -- and
/// this is where that window is widened, narrowed or replaced: ticking a line
/// draws it whether or not the window covers it, and the three buttons at the
/// foot are the whole of the way to all of them, none of them, and back to
/// what the selection opened on.
///
/// A ListView rather than a Column: a table of half a million rows has half a
/// million lines to list, and only the dozen on screen are ever built.
Rectangle {
    id: legend

    property bool open: false
    /// The PlotSurface this describes. Its `highlighted` is what a click on a
    /// name writes.
    property var target

    readonly property var plot: AppController.datasetPlot
    /// Lines in the table, which is how many rows the list has.
    readonly property int total: plot ? plot.sourceSeriesCount : 0
    /// How many a new selection opens on. Read off the plot rather than
    /// written here, so the number in the button is the number in force.
    readonly property int limit: plot ? plot.initialSeriesLimit : 0

    /// Bumped whenever the drawn set changes. seriesVisible() is a call rather
    /// than a role, so nothing else would tell a delegate's tick to update.
    property int revision: 0

    width: Theme.railWidth
    color: Theme.surface
    visible: x > -width

    // Parked off the left edge rather than hidden, so `contentLeft` -- which
    // is where the graph starts -- is arithmetic over this one number in both
    // states rather than a second case.
    x: legend.open ? 0 : -width

    Connections {
        target: AppController.datasetPlot
        function onChanged() { legend.revision++ }
    }

    // The edge against the plot. The rail's own seam is `borderStrong`; this
    // one matches it, because it is the same kind of boundary.
    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: Theme.borderWidth
        color: Theme.borderStrong
        z: 1
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.rightMargin: Theme.borderWidth
        spacing: 0

        // --- heading ------------------------------------------------------
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.treeHeaderHeight

            Text {
                anchors.left: parent.left
                anchors.leftMargin: Theme.gapM
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("legend")
                font: Theme.micro
                color: Theme.textSecondary
            }

            // How many of the table's lines are being drawn. This is what
            // makes the opening window honest -- "64 / 10000" says both what
            // is on screen and what is not -- and it turns amber past a few
            // hundred rather than refusing: nothing here caps what a reader
            // may ask for, but a plot of ten thousand lines takes a while to
            // draw and this is where they are told so beforehand.
            Text {
                id: count

                anchors.right: parent.right
                anchors.rightMargin: Theme.gapM
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("%1 / %2").arg(legend.plot ? legend.plot.seriesCount : 0)
                                     .arg(legend.total)
                font: Theme.readout
                color: (legend.plot && legend.plot.seriesCount > 500)
                       ? Theme.warning : Theme.textDisabled

                AppToolTip {
                    shown: countHover.hovered
                    text: (legend.plot && legend.plot.seriesCount > 500)
                          ? qsTr("drawing this many lines is slow")
                          : qsTr("lines drawn, of the table's own")
                }
            }

            HoverHandler { id: countHover }

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: Theme.borderWidth
                color: Theme.border
            }
        }

        // --- the lines ----------------------------------------------------
        ListView {
            id: lines

            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            // Unbound while closed: a hidden ListView goes on building
            // delegates, and each of these asks the plot a question.
            model: legend.open ? legend.total : 0
            boundsBehavior: Flickable.StopAtBounds

            ScrollBar.vertical: ScrollBar {}

            delegate: Item {
                id: row

                required property int index

                readonly property bool drawn:
                    legend.revision >= 0 && legend.plot
                    && legend.plot.seriesVisible(row.index)
                readonly property bool picked:
                    legend.target && legend.target.highlighted === row.index

                width: lines.width
                height: Theme.treeRowHeight

                Rectangle {
                    anchors.fill: parent
                    color: row.picked ? Theme.surfaceActive
                         : rowHover.hovered ? Theme.surfaceHover
                                            : Theme.clear(Theme.surfaceHover)
                }

                HoverHandler { id: rowHover }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.gapM
                    anchors.rightMargin: Theme.gapM
                    spacing: Theme.gapS

                    AppCheckBox {
                        checked: row.drawn
                        onToggled: legend.plot.setSeriesVisible(row.index, checked)
                    }

                    // The colour the line is actually drawn in, which is the
                    // whole point of the swatch: it is what ties the name to
                    // the stroke on the plot.
                    Rectangle {
                        Layout.preferredWidth: Theme.gapL
                        Layout.preferredHeight: Theme.borderWidthAccent * 2
                        radius: Theme.radiusS
                        visible: row.drawn
                        color: {
                            if (!legend.target || !legend.plot)
                                return Theme.accent
                            const series = legend.plot.drawnSeries
                            const at = series.indexOf(row.index)
                            return legend.target.seriesColor(Math.max(at, 0),
                                                             series.length)
                        }
                    }

                    // A line that is not drawn has no colour to show, but the
                    // name still has to sit where the others do.
                    Item {
                        Layout.preferredWidth: Theme.gapL
                        visible: !row.drawn
                    }

                    Text {
                        id: name

                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        text: legend.plot ? legend.plot.seriesLabel(row.index) : ""
                        font: Theme.monoSmall
                        color: row.picked ? Theme.textEmphasis
                             : row.drawn ? Theme.textPrimary : Theme.textDisabled
                        elide: Text.ElideMiddle
                        verticalAlignment: Text.AlignVCenter

                        AppToolTip {
                            shown: name.truncated && rowHover.hovered
                            verbatim: true
                            text: name.text
                        }
                    }
                }

                // Clicking the name picks the line out of the bundle; clicking
                // it again puts it back. Only the name, not the row: the tick
                // is a different question and has its own target.
                TapHandler {
                    onTapped: {
                        if (!legend.target)
                            return
                        legend.target.highlighted =
                            legend.target.highlighted === row.index ? -1 : row.index
                    }
                }
            }
        }

        // --- all, none ----------------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.smallControlHeight + Theme.gapS * 2
            color: Theme.surface

            Rectangle {
                anchors.top: parent.top
                width: parent.width
                height: Theme.borderWidth
                color: Theme.border
            }

            RowLayout {
                anchors.fill: parent
                anchors.margins: Theme.gapS
                spacing: Theme.gapS

                AppToolButton {
                    Layout.fillWidth: true
                    text: qsTr("all")
                    size: "sm"
                    // Amber rather than an outline once "all" is a great many:
                    // the button still does what it says, and the reader is
                    // told what they are about to ask for before they ask.
                    variant: legend.total > 500 ? "caution" : "secondary"
                    onClicked: legend.plot.selectAll()

                    AppToolTip {
                        shown: parent.hovered
                        text: legend.total > 500
                              ? qsTr("%1 lines - this will take a moment")
                                .arg(legend.total)
                              : qsTr("draw every line in the table")
                    }
                }

                // The way back to what the selection opened on. Absent when
                // the table is shorter than that window, because there it says
                // exactly what "all" says.
                AppToolButton {
                    Layout.fillWidth: true
                    text: qsTr("first %1").arg(legend.limit)
                    size: "sm"
                    visible: legend.total > legend.limit
                    onClicked: legend.plot.selectFirst(legend.limit)

                    AppToolTip {
                        shown: parent.hovered
                        text: qsTr("back to what this dataset opened on")
                    }
                }

                AppToolButton {
                    Layout.fillWidth: true
                    text: qsTr("none")
                    size: "sm"
                    onClicked: legend.plot.selectNone()
                }
            }
        }
    }
}
