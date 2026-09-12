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

    /// Read off the surface rather than off the controller, so a legend is
    /// always describing the plot it is sitting on. The custom plot tabs are
    /// what made that distinction matter: this file used to name
    /// AppController.datasetPlot itself, which is right for exactly one of the
    /// plots this component now serves.
    readonly property var plot: legend.target ? legend.target.plot : null
    /// Lines in the table, which is how many rows the list has.
    readonly property int total: plot ? plot.sourceSeriesCount : 0
    /// How many a new selection opens on, or -1 for a plot that opened on
    /// nothing. Read off the plot rather than written here, so the number in
    /// the button is the number in force.
    readonly property int limit: plot ? plot.initialSeriesLimit : 0

    /// Whether this legend can offer a line to a custom plot. It can when it
    /// is describing the plot tab's own plot and not a custom one's.
    readonly property bool offersCustom:
        legend.plot === AppController.datasetPlot

    /// Why line `index` cannot be taken to a custom plot, or empty when it
    /// can be.
    ///
    /// One function rather than a condition on the menu row and a sentence
    /// beside it, because the two have to agree: a row greyed for one reason
    /// and explained by another is worse than either.
    function customRefusal(index) {
        if (AppController.postprocessActive)
            return qsTr("a post-processed line has no path to record")
        if (!legend.plot || legend.plot.seriesExpression(index) === "")
            return qsTr("this line is not a slice of one dimension")
        if (AppController.customPlots.count === 0)
            return qsTr("no custom plots yet — make one with the +")
        return ""
    }

    /// Widen or narrow the panel by `amount`, within its bounds.
    ///
    /// Clamped against the same bounds `width` is, so that dragging past an
    /// end and back again returns the edge to the pointer rather than leaving
    /// it behind by however far the drag overshot.
    function resizeBy(amount) {
        legend.panelWidth = Math.max(
            legend.minimumWidth,
            Math.min(legend.width + amount, legend.maximumWidth))
    }

    /// `text` with its leading path taken off: "…/pressure[0:2048]".
    ///
    /// What a legend row drops first when it will not fit. A slice's name is
    /// the last thing in it and its subscript is the last thing after that,
    /// and those two are what tell one line from another -- the group it sits
    /// in is usually the same for every line in the list, so it is the part
    /// that carries no information and the part to lose. What is left is then
    /// elided down the middle, which keeps the beginning and the end of the
    /// name itself.
    function withoutPath(text) {
        const cut = text.lastIndexOf("/")
        return cut > 0 ? "…" + text.substring(cut) : text
    }

    /// What a line can be taken to, exposed for the QML suite: a Popup is not
    /// in the item tree, so there is no walking to one.
    readonly property alias lineMenu: lineMenu

    /// Open the line menu over line `index`.
    function openRowMenu(index) {
        lineMenu.series = index
        lineMenu.popup()
    }

    /// Bumped whenever the drawn set changes. seriesVisible() is a call rather
    /// than a role, so nothing else would tell a delegate's tick to update.
    property int revision: 0

    /// How wide the panel is, which the reader can drag.
    ///
    /// A legend lists slices, and a slice is as long as the path that names
    /// it: `/run/2026-03-11/sensors/pressure[0:2048]` does not go in 212
    /// pixels and no amount of eliding makes it. Every other panel in this
    /// window is a fixed width because every other panel holds controls, whose
    /// widths this file decides; this one holds the file's own names, whose
    /// widths it does not.
    property real panelWidth: Theme.railWidth
    /// Narrow enough to be worth doing, and wide enough that a legend cannot
    /// be dragged over the whole plot it is describing.
    readonly property real minimumWidth: Theme.railWidth / 2
    readonly property real maximumWidth:
        legend.parent ? Math.max(legend.minimumWidth, legend.parent.width * 0.7)
                      : Theme.railWidthWide

    width: Math.max(legend.minimumWidth,
                    Math.min(legend.panelWidth, legend.maximumWidth))
    color: Theme.surface
    visible: x > -width

    // Parked off the left edge rather than hidden, so `contentLeft` -- which
    // is where the graph starts -- is arithmetic over this one number in both
    // states rather than a second case.
    x: legend.open ? 0 : -width

    Connections {
        target: legend.plot
        function onChanged() { legend.revision++ }
    }

    // What a line can be taken to. One drawer for the whole list, opened over
    // whichever row was pressed -- a legend of ten thousand rows building one
    // apiece would build ten thousand.
    //
    // A running pipeline is the one case this refuses. What the legend is
    // listing then is a computed array: it has no path in the file, and an
    // entry is a path and nothing else, so there is nothing to write down.
    // Refused with its reason rather than quietly recording the slice
    // underneath, which would put the raw data into the plot when what was
    // clicked was a reduction of it.
    AppMenu {
        id: lineMenu

        /// The line this was opened over.
        property int series: -1

        /// Why this line cannot be offered, or empty when it can be.
        readonly property string refusal:
            lineMenu.series >= 0 ? legend.customRefusal(lineMenu.series) : ""

        AddToCustomMenu {
            title: qsTr("Add to Custom Plot")
            enabled: lineMenu.refusal === ""
            onPicked: (index) => {
                const plot = AppController.customPlots.plotAt(index)
                if (!plot || lineMenu.refusal !== "")
                    return
                plot.addExpression(legend.plot.seriesExpression(lineMenu.series))
            }
        }

        // Why the row above is greyed, when it is. A disabled row with no
        // reason beside it is a row the reader reads as broken.
        AppMenuItem {
            text: lineMenu.refusal
            enabled: false
            visible: lineMenu.refusal !== ""
        }
    }

    // The edge against the plot, which is also what widens the panel.
    //
    // The rail's own seam is `borderStrong` and this matches it, because it is
    // the same kind of boundary -- but it is a grab target four pixels wide
    // painted as a hairline, exactly as the window's own splitter is, so the
    // seam reads the same until it is touched.
    Item {
        id: grip

        objectName: "legendGrip"

        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: Theme.splitHandleWidth
        z: 1

        HoverHandler {
            id: gripHover

            cursorShape: Qt.SizeHorCursor
        }

        DragHandler {
            id: gripDrag

            target: null
            yAxis.enabled: false
            cursorShape: Qt.SizeHorCursor

            /// The movement already applied. DragHandler reports the whole of
            /// it since the press, and the width wants the step.
            property real applied: 0

            onActiveChanged: applied = 0
            onActiveTranslationChanged: {
                legend.resizeBy(activeTranslation.x - gripDrag.applied)
                gripDrag.applied = activeTranslation.x
            }
        }

        Rectangle {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: Theme.borderWidth
            color: gripDrag.active ? Theme.accent
                 : gripHover.hovered ? Theme.borderGuide
                                     : Theme.borderStrong
        }
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

                /// What this line is called, whole.
                readonly property string label:
                    legend.plot ? legend.plot.seriesLabel(row.index) : ""

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
                        onToggled: {
                            if (legend.plot)
                                legend.plot.setSeriesVisible(row.index, checked)
                        }
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

                    // advanceWidth, not width: TextMetrics.width rounds down
                    // to whole pixels and a Text elides the moment it is given
                    // half a pixel less than it needs, which costs a whole
                    // character and an ellipsis on top of it. The tree's
                    // readout is measured the same way for the same reason.
                    TextMetrics {
                        id: whole

                        font: name.font
                        text: row.label
                    }

                    Text {
                        id: name

                        // A preferred width of one is what keeps this from
                        // measuring itself: with fillWidth and no preferred
                        // width a RowLayout asks the item how wide it would
                        // like to be, which here depends on the text, which
                        // below depends on the width. That is a binding loop,
                        // and this application treats a QML warning as a build
                        // failure.
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        Layout.fillHeight: true
                        /// The path goes before the name does. See
                        /// legend.withoutPath.
                        text: whole.advanceWidth <= name.width
                              ? row.label : legend.withoutPath(row.label)
                        font: Theme.monoSmall
                        color: row.picked ? Theme.textEmphasis
                             : row.drawn ? Theme.textPrimary : Theme.textDisabled
                        elide: Text.ElideMiddle
                        verticalAlignment: Text.AlignVCenter

                        AppToolTip {
                            shown: rowHover.hovered
                                   && (name.truncated || name.text !== row.label)
                            verbatim: true
                            text: row.label
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

                // ...and the right button offers to take this one line
                // somewhere else. Only on the plot tab: a custom plot's own
                // legend has nothing to offer, because the line is already in
                // a custom plot and the entry row beside it is where it is
                // edited.
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    enabled: legend.offersCustom
                    onTapped: legend.openRowMenu(row.index)
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
                    onClicked: { if (legend.plot) legend.plot.selectAll() }

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
                // exactly what "all" says -- and absent altogether for a plot
                // that opened on nothing, which says so with -1: a custom
                // plot's lines were each put there on purpose, so there is no
                // earlier state to go back to.
                AppToolButton {
                    Layout.fillWidth: true
                    text: qsTr("first %1").arg(legend.limit)
                    size: "sm"
                    visible: legend.limit > 0 && legend.total > legend.limit
                    onClicked: { if (legend.plot) legend.plot.selectFirst(legend.limit) }

                    AppToolTip {
                        shown: parent.hovered
                        text: qsTr("back to what this dataset opened on")
                    }
                }

                AppToolButton {
                    Layout.fillWidth: true
                    text: qsTr("none")
                    size: "sm"
                    onClicked: { if (legend.plot) legend.plot.selectNone() }
                }
            }
        }
    }
}
