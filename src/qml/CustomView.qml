// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import QtQuick.Layouts
import H5Scope.Backend

/// One custom plot tab.
///
///     [legend]  name [ pressure vs time ] ..... [data settings] [plot settings]
///     +---------------------------------------------+-----------+
///     |  the lines, from wherever they came from     |  the one  |
///     |                                             |   panel   |
///     +---------------------------------------------+           |
///     |  9 entries · 576 datapoints                 |           |
///     +---------------------------------------------+-----------+
///
/// The sibling of DataView, and arranged the same way on purpose: the bar along
/// the top is everything that acts, the rail holds one panel at a time, and the
/// strip underneath is a readout and nothing else. A reader who has learnt the
/// plot tab has learnt this one.
///
/// The two differences from DataView are the two things that make this tab what
/// it is. Where DataView prints a slice line -- one dataset, one selection --
/// this holds a name, because a custom plot is not about any one dataset and
/// the only thing at the head of the bar worth having is what the reader has
/// called it. And where DataView's rail offers data settings, postprocessing
/// and the view's own, this offers two: what is drawn, and how. There is no
/// postprocessing, because an entry is a slice of the file and nothing else --
/// a computed array has no path to write down.
///
/// The plot itself is PlotSurface, unmodified. Everything it asks of a plot
/// object, CustomPlot answers.
Rectangle {
    id: root

    /// The CustomPlot this draws, handed down by the window.
    property var plot
    /// Its place in AppController.customPlots, which is what the invokables
    /// that act on a whole tab take.
    property int plotIndex: -1
    /// Whether this is the tab on screen. The surface samples the file to
    /// answer what it would draw, so it is told rather than guessing.
    property bool active: true
    /// Whether this is being shown in a window of its own, in which case the
    /// control that would put it there has nothing left to offer.
    property bool detached: false

    /// Which panel the rail is showing: "" | "data" | "plot".
    property string rail: ""
    readonly property bool railVisible: rail !== ""

    /// The narrowest the bar can hold its own contents, measured rather than
    /// typed -- every part of it is text, and how wide text comes out is the
    /// host's decision. DataView's note on this is the long version; the same
    /// argument applies here and the window takes the larger of the two.
    readonly property real barMinimumWidth:
        Theme.gapM * 2
        + Theme.gapM * 4
        + legendButton.implicitWidth
        + nameLabel.implicitWidth
        + Theme.sliceWellMinimum
        + dataSettingsButton.implicitWidth
        + viewSettingsButton.implicitWidth
        + detachButton.implicitWidth

    color: Theme.background

    function toggleRail(which) {
        root.rail = (root.rail === which) ? "" : which
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // --- the bar this tab acts from ----------------------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.sliceBarHeight
            color: Theme.surfaceRaised

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.gapM
                anchors.rightMargin: Theme.gapM
                spacing: Theme.gapM

                AppToolButton {
                    id: legendButton

                    objectName: "customLegendButton"

                    text: qsTr("legend")
                    size: "md"
                    variant: plotSurface.legendOpen ? "secondary" : "ghost"
                    opens: "panel"
                    open: plotSurface.legendOpen
                    onClicked: plotSurface.legendOpen = !plotSurface.legendOpen
                }

                Text {
                    id: nameLabel

                    text: qsTr("name")
                    font: Theme.micro
                    color: Theme.textSecondary
                }

                // The tab's name, edited where it is read. A name is the only
                // thing at the head of this bar worth having -- there is no
                // slice, because there is no one dataset -- and a plot the
                // reader has called something is a plot they can find again in
                // a strip of six.
                //
                // Sans rather than mono, which is the one place this bar parts
                // company with the slice line it stands in for: what is in
                // there is an identifier and what is in here is a phrase.
                FilterInput {
                    id: nameField

                    objectName: "customNameField"

                    Layout.fillWidth: true
                    Layout.maximumWidth: Theme.railWidthWide
                    Layout.alignment: Qt.AlignVCenter
                    implicitHeight: Theme.smallControlHeight
                    font: Theme.body
                    text: root.plot ? root.plot.name : ""
                    invalid: internal.nameProblem !== ""
                    pending: root.plot && text !== root.plot.name
                             && internal.nameProblem === ""
                    placeholderText: qsTr("name this plot")

                    onTextEdited: internal.checkName()
                    onAccepted: internal.commitName()
                    onActiveFocusChanged: if (!activeFocus) internal.commitName()
                    Keys.onEscapePressed: {
                        nameField.text = root.plot ? root.plot.name : ""
                        internal.nameProblem = ""
                        nameField.focus = false
                    }
                }

                // Why the name will not do. Beside the box rather than under
                // it: this bar is one line tall and a second one would move
                // the plot every time a reader typed a name already taken.
                Text {
                    id: nameNote

                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.alignment: Qt.AlignVCenter
                    visible: text !== ""
                    text: internal.nameProblem
                    font: Theme.caption
                    color: Theme.warning
                    elide: Text.ElideRight

                    HoverHandler { id: noteHover }

                    AppToolTip {
                        shown: nameNote.truncated && noteHover.hovered
                        text: nameNote.text
                    }
                }

                Item {
                    Layout.fillWidth: true
                    visible: !nameNote.visible
                }

                AppToolButton {
                    id: dataSettingsButton

                    objectName: "customDataButton"

                    text: qsTr("data settings")
                    size: "md"
                    variant: root.rail === "data" ? "secondary" : "ghost"
                    opens: "panel"
                    open: root.rail === "data"
                    onClicked: root.toggleRail("data")
                }

                AppToolButton {
                    id: viewSettingsButton

                    objectName: "customPlotButton"

                    text: qsTr("plot settings")
                    size: "md"
                    variant: root.rail === "plot" ? "secondary" : "ghost"
                    opens: "panel"
                    open: root.rail === "plot"
                    onClicked: root.toggleRail("plot")
                }

                // Take this plot out of the strip and put it in a window of
                // its own. At the end of the bar and drawn rather than
                // labelled, because it is the one control here that is about
                // the tab rather than about what the tab is showing -- and
                // because the two beside it are already the widest things in
                // the bar.
                //
                // Absent in a torn-off window: the way back is closing it, and
                // a button offering to do again what has been done is a button
                // the reader has to work out.
                AppIconButton {
                    id: detachButton

                    objectName: "detachPlot"

                    Layout.alignment: Qt.AlignVCenter
                    visible: !root.detached && root.plotIndex >= 0
                    glyph: "detach"
                    bare: true
                    hint: qsTr("open this plot in a window of its own")
                    onClicked: AppController.customPlots.setDetached(
                                   root.plotIndex, true)
                }
            }

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: Theme.borderWidth
                color: Theme.border
            }
        }

        // --- the lines, and the panel that decides them -------------------
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                PlotSurface {
                    id: plotSurface

                    objectName: "customPlotSurface"

                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    plot: root.plot
                    active: root.active
                    // A custom plot answers for itself whether what it holds
                    // can be drawn; there is no selected datatype to ask about.
                    sourceUsable: root.plot !== null && root.plot !== undefined
                    // Its settings belong to this tab, not to whatever the tree
                    // happens to have highlighted. An empty group switches
                    // DatasetMemory off entirely.
                    memoryGroup: ""
                    plotMemoryGroup: ""

                    // The stated range drives x only when it is what the reader
                    // chose. Under Index the plot counts positions itself, and
                    // under Dataset it reads every x out of another slice --
                    // pushing a start and a step over either would be the
                    // surface overruling the data.
                    rangeDrivesX: root.plot
                                  && root.plot.xMode === CustomPlot.Range
                    dataLength: root.plot ? root.plot.sourcePointCount : 1
                    // Three axes, one apiece. A time base runs between its own
                    // ends; a stated range runs where it was stated to; and
                    // the index runs 0 to the longest line drawn, which is
                    // what "index" means and is not whatever range the reader
                    // stated before they switched back to it.
                    axisMinX: internal.againstDataset ? root.plot.xMinimum
                            : internal.statedRange ? xAxis.minimum : 0
                    axisMaxX: internal.againstDataset ? root.plot.xMaximum
                            : internal.statedRange ? xAxis.maximum
                                                   : plotSurface.dataLength

                    idleReason: {
                        if (!root.plot)
                            return ""
                        if (root.plot.empty)
                            return qsTr("Add a dataset from the tree, or write " +
                                        "one into the data settings panel.")
                        if (internal.againstDataset && !root.plot.xReady)
                            return qsTr("The time series has not been read, so " +
                                        "there is nowhere to draw these lines.")
                        return qsTr("None of these entries has a finite value " +
                                    "in it.")
                    }
                }

                ViewFooter {
                    Layout.fillWidth: true
                    visible: root.plot && !root.plot.empty
                    facts: {
                        if (!root.plot)
                            return []
                        const shown = [counted(root.plot.seriesCount,
                                               qsTr("entry"), qsTr("entries"))]
                        if (root.plot.seriesCount < root.plot.sourceSeriesCount) {
                            shown[0] += qsTr(" of %1")
                                        .arg(root.plot.sourceSeriesCount)
                        }
                        shown.push(counted(root.plot.pointCount,
                                           qsTr("datapoint"), qsTr("datapoints"))
                                   + (root.plot.thinned ? qsTr(" thinned") : ""))
                        if (root.plot.hasData) {
                            shown.push(qsTr("y %1 … %2")
                                       .arg(root.plot.minimum.toPrecision(4))
                                       .arg(root.plot.maximum.toPrecision(4)))
                        }
                        return shown
                    }
                }
            }

            Rectangle {
                Layout.fillHeight: true
                Layout.preferredWidth: Theme.borderWidth
                visible: root.railVisible
                color: Theme.borderStrong
            }

            // --- the rail: one panel, whichever was asked for -------------
            Item {
                Layout.fillHeight: true
                Layout.preferredWidth: Theme.railWidthWide
                visible: root.railVisible

                CustomDataPanel {
                    anchors.fill: parent
                    visible: root.rail === "data"
                    plot: root.plot
                    plotIndex: root.plotIndex
                    surface: plotSurface
                }

                PlotSettingsPanel {
                    anchors.fill: parent
                    visible: root.rail === "plot"
                    target: plotSurface
                    // The x axis is stated in the data panel, where it is one
                    // of three things rather than always three numbers; and an
                    // entry is already a single line, so there is no table left
                    // to read one way round or the other.
                    showXAxis: false
                    showOrientation: false
                }
            }
        }
    }

    QtObject {
        id: internal

        /// Why the name in the box will not do, or empty.
        property string nameProblem: ""

        readonly property bool againstDataset:
            root.plot && root.plot.xMode === CustomPlot.Dataset
        readonly property bool statedRange:
            root.plot && root.plot.xMode === CustomPlot.Range

        function checkName() {
            if (!root.plot || root.plotIndex < 0) {
                internal.nameProblem = ""
                return
            }
            const wanted = nameField.text.trim()
            if (wanted === "" || wanted === root.plot.name) {
                internal.nameProblem = ""
                return
            }
            const clash = AppController.customPlots.indexOfName(wanted)
            internal.nameProblem =
                (clash >= 0 && clash !== root.plotIndex)
                    ? qsTr("another plot is already called \"%1\"").arg(wanted)
                    : ""
        }

        /// Apply what is in the box, or put back what the plot is still called.
        ///
        /// A name that will not do is *not* applied and the box keeps it with
        /// the reason beside it, which is what the pipeline's argument box does
        /// with an argument that will not read -- except that here the set is
        /// the authority, so what it says is what the box ends up holding.
        function commitName() {
            if (!root.plot || root.plotIndex < 0)
                return
            const wanted = nameField.text.trim()
            if (wanted === root.plot.name) {
                internal.nameProblem = ""
                return
            }
            const refused = AppController.customPlots.setName(root.plotIndex,
                                                              wanted)
            internal.nameProblem = refused
            if (refused === "")
                nameField.text = root.plot.name
        }
    }

    // A rename from anywhere else -- a restored view, another window on the
    // same tab -- is still this tab's name, so the box follows it.
    Connections {
        target: root.plot
        function onNameChanged() {
            if (!nameField.activeFocus) {
                nameField.text = root.plot.name
                internal.nameProblem = ""
            }
        }
    }
}
