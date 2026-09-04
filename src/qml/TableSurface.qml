// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import H5Scope.Backend

/// The Data Viewer's table presentation: the grid, and everything that is not
/// a grid because the values are not numbers.
///
/// What it shows depends on what was selected, because a dataset of numbers and
/// a dataset of text are not the same kind of thing and a single grid serves
/// only the first of them well:
///
///   numbers        the value grid
///   one string     one text pane, filling the view and scrolling inside it
///   many strings   the grid as an overview, and beneath it every string in
///                  full, one pane each, each as tall as its own text
///   compounds      the grid as an overview, and beneath it the picked
///                  element's members, named, and the same element as JSON
///   unreadable     the reason, stated plainly
///   no dataset     what to do instead
///
/// Text and compounds get the same shape of answer for the same reason: a grid
/// cell can hold a number, and neither of the other two fits in one. They
/// differ in how many are shown at once -- every string is worth scrolling
/// past, where one struct at a time is what a reader is actually reading.
///
/// The grid pages its reads through DatasetTableModel and the stack of panes
/// goes through the same model via DatasetStringListModel, so neither of them
/// reads more of the file than it is showing.
Item {
    id: surface

    // --- settings, written by TableSettingsPanel ------------------------
    property int columnWidth: Theme.s13 + Theme.s9
    /// Fit the columns to their contents. On by default; the width above is
    /// what the reader falls back to when they turn it off.
    property bool autoWidth: true
    property bool striped: true
    property bool gridLines: true

    // --- cells filled by what is in them ---------------------------------
    // The image's reading of a table, put back over the table: every cell on a
    // colour ramp between two values.
    //
    // Not merely the same *kind* of controls as the image settings panel's --
    // the same answers. Ramp, direction and band are read straight off
    // AppController.datasetImage, which is where the picture keeps them, so a
    // band dragged in either tab is the band both tabs are drawing. They had
    // been separate copies, and a reader who set a range on the picture found
    // the table still stretched to its own extremes.
    //
    // Whether to fill at all stays the table's own: a grid of numbers is
    // legible without colour and a picture is not, so the question does not
    // even arise on the other side.
    property bool colorCells: false

    readonly property var image: AppController.datasetImage
    /// A name in Theme.colorRamps, or "gray" for the plain black-to-white one.
    readonly property string colorRamp: surface.image.rampName
    readonly property bool colorsReversed: surface.image.invert
    /// Take the band from the table's own values, or from the two below.
    readonly property bool colorAutoRange: surface.image.autoRange
    readonly property real colorMinimum: surface.image.rangeMinimum
    readonly property real colorMaximum: surface.image.rangeMaximum

    /// Bumped whenever the table underneath becomes a different one.
    /// valueExtent() is a call rather than a role, so nothing else would tell
    /// the binding below to run again.
    property int extentRevision: 0

    /// The extent of the table's values, `{ minimum, maximum, valid }`, or an
    /// empty map when nothing has been asked.
    ///
    /// Guarded on `colorCells`: asking costs one read of the file, and a table
    /// nobody has asked to colour must not pay for it. Guarded on the datatype
    /// too -- a grid of strings has no extent to take.
    readonly property var valueExtent:
        (surface.colorCells && AppController.datasetIsNumeric
         && surface.extentRevision >= 0)
            ? AppController.datasetModel.valueExtent() : ({})

    /// What the band's handles run between, and what `auto` resolves to.
    readonly property real dataLow:
        surface.valueExtent.valid ? surface.valueExtent.minimum : 0.0
    readonly property real dataHigh:
        surface.valueExtent.valid ? surface.valueExtent.maximum : 1.0

    /// The band in force. Turned round by the reader is still a band: two
    /// handles, and nothing stops one being dragged through the other.
    readonly property real colorLow:
        surface.colorAutoRange ? surface.dataLow
                               : Math.min(surface.colorMinimum,
                                          surface.colorMaximum)
    readonly property real colorHigh:
        surface.colorAutoRange ? surface.dataHigh
                               : Math.max(surface.colorMinimum,
                                          surface.colorMaximum)

    /// How a float is written in the grid. Passed through to the model, which
    /// is where the cells are made, rather than held here -- and read back off
    /// it, so the panel shows what is actually in effect.
    readonly property var model: AppController.datasetModel

    /// Which presentation applies, decided in one place so the views below can
    /// each just say when they are the answer.
    readonly property string mode: {
        if (!AppController.datasetTabVisible)
            return "empty"
        if (AppController.datasetMessage !== "")
            return "message"
        if (AppController.datasetIsString)
            return AppController.datasetElementCount === 1 ? "text" : "strings"
        if (AppController.datasetIsCompound)
            return "compound"
        return "table"
    }

    readonly property bool showsGrid: mode === "table" || mode === "strings"
                                      || mode === "compound"
    readonly property int rows: grid.rows
    readonly property int columns: grid.columns

    // --- which element the compound pane is showing ----------------------
    // The grid reports what was clicked; before anything has been, the first
    // cell stands in, so the pane says what a struct in this dataset looks like
    // rather than sitting empty until the reader guesses that it wants a click.
    // The pane's own heading names the element either way.
    readonly property int detailRow: grid.currentRow >= 0 ? grid.currentRow : 0
    readonly property int detailColumn: grid.currentColumn >= 0 ? grid.currentColumn : 0

    /// Bumped whenever the element under those coordinates becomes a different
    /// one. elementAt() is a call rather than a role, so nothing else would
    /// tell the binding below to run again.
    property int elementRevision: 0

    readonly property var element:
        (surface.mode === "compound" && surface.rows > 0 && surface.columns > 0
         && surface.elementRevision >= 0)
            ? AppController.datasetModel.elementAt(surface.detailRow,
                                                   surface.detailColumn)
            : ({})

    // How the grid is drawn is a setting about this dataset: a column width
    // fitted to one table is a guess about the next, and a fill turned on for a
    // field of numbers means nothing over a grid of strings.
    DatasetMemory {
        subject: surface
        group: "tableView"
        names: ["columnWidth", "autoWidth", "striped", "gridLines", "colorCells"]
    }

    Connections {
        target: AppController
        function onSelectionChanged() {
            surface.elementRevision++
            surface.extentRevision++
        }
        function onTableLayoutChanged() {
            surface.elementRevision++
            surface.extentRevision++
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // --- numbers: the grid alone -----------------------------------
        ValueGrid {
            id: grid

            objectName: "valueGrid"

            Layout.fillWidth: true
            Layout.fillHeight: surface.mode === "table"
            // Where the grid is a navigator rather than the content -- text
            // and compounds both -- it takes a fixed slice of the view, and no
            // more than its own rows need.
            Layout.preferredHeight:
                (surface.mode === "strings" || surface.mode === "compound")
                    ? Math.min(Theme.stringGridHeight,
                               Theme.rowHeight * (grid.rows + 1)) : -1
            visible: surface.showsGrid
            cellWidth: surface.columnWidth
            autoWidth: surface.autoWidth
            striped: surface.striped
            gridLines: surface.gridLines
            // Only where a cell holds a number. A string grid and a compound
            // grid are both here too, and neither has a value to put on a ramp.
            colorCells: surface.colorCells && AppController.datasetIsNumeric
            colorRamp: surface.colorRamp
            colorsReversed: surface.colorsReversed
            colorLow: surface.colorLow
            colorHigh: surface.colorHigh
            rampBegin: surface.image.rampBegin
            rampEnd: surface.image.rampEnd

            // Picking a cell in the overview scrolls the stack to that string,
            // which is the whole reason the grid is still here in text mode.
            // In compound mode the pane below simply follows currentRow and
            // currentColumn, so there is nothing to do here but let it.
            onCellActivated: (row, column) => {
                if (surface.mode !== "strings")
                    return
                const index = AppController.datasetStringModel.indexOfCell(row, column)
                if (index < 0)
                    return
                paneStack.currentIndex = index
                paneStack.positionViewAtIndex(index, ListView.Beginning)
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.borderWidth
            visible: surface.mode === "strings" || surface.mode === "compound"
            color: Theme.borderStrong
        }

        // --- one compound: its members, and the same element as JSON ----
        Flickable {
            id: compoundScroller

            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: surface.mode === "compound"
            clip: true
            contentWidth: width
            contentHeight: compoundPane.height + Theme.gapM * 2
            boundsBehavior: Flickable.StopAtBounds

            ScrollBar.vertical: ScrollBar {}

            CompoundPane {
                id: compoundPane

                x: Theme.gapM
                y: Theme.gapM
                width: compoundScroller.width - Theme.gapM * 2
                element: surface.element
            }
        }

        // --- many strings: every value in full, stacked -----------------
        // A ListView rather than a Column: it builds only the panes on screen,
        // so a dataset of a hundred thousand strings costs the same as one of
        // ten, and each pane's own read goes through the windowed table model.
        ListView {
            id: paneStack

            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: surface.mode === "strings"
            clip: true
            spacing: Theme.gapM
            topMargin: Theme.gapM
            bottomMargin: Theme.gapM
            leftMargin: Theme.gapM
            rightMargin: Theme.gapM
            currentIndex: -1
            // Unbound in every other mode: a hidden ListView keeps whatever
            // size it last had, and would go on building panes -- and reading
            // cells for them -- for a dataset that has no text in it at all.
            model: surface.mode === "strings" ? AppController.datasetStringModel : null
            boundsBehavior: Flickable.StopAtBounds

            ScrollBar.vertical: ScrollBar {}

            delegate: TextPane {
                required property int index
                required property string label
                required property string value
                required property int length

                // ListView's own margins are the outer inset; the delegate
                // spans what is left.
                width: paneStack.width - paneStack.leftMargin - paneStack.rightMargin
                title: label
                meta: qsTr("%1 chars").arg(length)
                text: value
                // The pane the overview grid points at is the one wearing the
                // accent, so a click up there lands somewhere visible.
                border.color: index === paneStack.currentIndex ? Theme.accent
                                                               : Theme.border
            }
        }

        // --- one string: a single browsable pane ------------------------
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: surface.mode === "text"

            Repeater {
                // One row, one column: the model is the binding that keeps the
                // text current as the selection moves.
                model: surface.mode === "text" ? AppController.datasetStringModel : null

                delegate: TextPane {
                    required property string value
                    required property int length

                    anchors.fill: parent
                    anchors.margins: Theme.gapM
                    scrolls: true
                    // Not the path: the header is a machine label and would
                    // uppercase it, and the mode bar right above already
                    // carries the path with its case intact.
                    title: qsTr("value")
                    meta: qsTr("%1 chars").arg(length)
                    text: value
                }
            }
        }

        // --- message shown instead of the data when it is unreadable ----
        ViewMessage {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: surface.mode === "message"
            text: AppController.datasetMessage
            warning: true
        }

        // --- nothing to show --------------------------------------------
        ViewMessage {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: surface.mode === "empty"
            title: qsTr("no dataset selected")
            text: AppController.currentPath === ""
                  ? qsTr("Select a dataset in the tree to read its values.")
                  : qsTr("%1 is a group. Select a dataset in the tree to read its values.")
                    .arg(AppController.currentPath)
        }
    }
}
