// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import QtQuick.Controls.Basic
import H5Scope.Backend

/// The dataset as a grid: a fixed index column, a sticky header row, and a
/// TableView that pulls cells from DatasetTableModel one block at a time, so a
/// dataset far larger than RAM stays scrollable.
///
/// Extracted from the Data Viewer because it is now one of that tab's several
/// presentations rather than the whole of it -- a string dataset shows this
/// grid as an overview and the full text beneath it.
///
/// Headers are index tuples rather than positions, because with more than one
/// dimension on an axis a position names nothing: a cell in row "(1,2,_)" and
/// column "(_,_,3)" is element (1,2,3) and says so.
Item {
    id: grid

    /// The width a column takes when `autoWidth` is off. The slider in the
    /// table settings panel writes this.
    property int cellWidth: Theme.s13 + Theme.s9
    /// Fit the columns to what is in them rather than to that number. On by
    /// default: a fixed width is a guess, and a guess about a column of numbers
    /// is either an ellipsis or a stripe of empty space.
    property bool autoWidth: true
    /// The banding on alternate rows. Texture, not decoration -- but it is the
    /// reader's eye, so it is theirs to turn off.
    property bool striped: true
    /// Hairlines between the cells. Off leaves the values to line themselves
    /// up in the mono face, which is quieter on a dense table.
    property bool gridLines: true

    // --- cells filled by what is in them ---------------------------------
    // The image's reading of a table, applied to the table itself: every cell
    // on a colour ramp between two values. It answers the question a grid of
    // numbers is worst at -- where in this dataset is it large -- without the
    // reader having to leave the numbers to find out.
    /// Whether cells are filled from their own value.
    property bool colorCells: false
    /// Which ramp, by name in Theme.colorRamps. Anything not in there -- "gray"
    /// -- is the plain black-to-white one, exactly as the image treats it.
    property string colorRamp: "gray"
    property bool colorsReversed: false
    /// The band the ramp is stretched between. The owner resolves this from
    /// the table's own extent or from what the reader typed; the grid only
    /// draws it.
    property real colorLow: 0.0
    property real colorHigh: 1.0
    /// Which stretch of the ramp those values are spread over, 0 to 1 along
    /// it. The same pair the picture reads, because a cell filled by its own
    /// value *is* the picture put back over the numbers.
    property real rampBegin: 0.0
    property real rampEnd: 1.0

    /// Striping and filling are two textures over one grid, and a stripe under
    /// a fill is either invisible or a second thing the eye has to discount.
    /// The fill wins, and the settings panel says so by disabling the box.
    readonly property bool banded: grid.striped && !grid.colorCells

    /// The colour cell holding `number` takes, or nothing when the cells are
    /// not filled and for a cell that holds no number.
    function fillFor(number) {
        if (!grid.colorCells || !isFinite(number))
            return "transparent"
        const span = grid.colorHigh - grid.colorLow
        // A band of no height has no ramp to spread over. The middle of it
        // says "one value everywhere", where an end would read as an extreme
        // -- the same answer DatasetImage gives a flat picture.
        let at = span > 0 ? (number - grid.colorLow) / span : 0.5
        at = Math.max(0, Math.min(1, at))
        if (grid.colorsReversed)
            at = 1 - at
        // ...and then where that lands inside the stretch of the ramp the
        // reader kept. Same order as DatasetImage::render, so the fill and the
        // picture cannot disagree about a value.
        at = grid.rampBegin + at * (grid.rampEnd - grid.rampBegin)
        const stops = Theme.colorRamps[grid.colorRamp]
        return stops ? Theme.rampColor(stops, at) : Qt.rgba(at, at, at, 1)
    }

    readonly property var model: AppController.datasetModel

    /// Whether the cells hold something that reads rather than something that
    /// lines up. A string and a compound are both sentences as far as a column
    /// is concerned: they want the width and they start at the left, where a
    /// number wants a narrow column and its last digit under the one above.
    readonly property bool textual: AppController.datasetIsString
                                    || AppController.datasetIsCompound

    // --- everything the grid is ruled by, on the device pixel grid --------
    //
    // A table is a field of rules, and a rule is one physical pixel wide. At a
    // fractional display scale -- 125%, 150%, what every desktop now offers --
    // a whole number of logical pixels is not a whole number of physical ones,
    // so a column 61 logical pixels across is 91.5 physical ones and the seams
    // of a table of them fall alternately on and between the pixels of the
    // screen. Every other rule then rasterises to one pixel and the ones
    // between it to two, and the grid comes out visibly striped with lines of
    // two different weights -- which is not something a reader can unsee.
    //
    // Snapping the cell *size* is what fixes it, because every seam after the
    // first is a multiple of that size: make one cell a whole number of
    // physical pixels and they all land on the grid. At 100%, and at any whole
    // scale factor, Theme.snap is the identity and none of this costs
    // anything.
    readonly property real rowHeight: Theme.snap(Theme.rowHeight)
    /// A rule, at the one width that comes out the same everywhere it is drawn.
    readonly property real ruleWidth: Theme.hairline

    /// Wide enough for the longest row label this dataset can produce, which
    /// grows with rank. The last row is the widest: every index in it is at
    /// its maximum number of digits.
    readonly property real indexWidth:
        Theme.snap(Math.max(Theme.indexColumnWidth,
                            Math.ceil(widestLabel.advanceWidth) + Theme.gapM * 2))

    TextMetrics {
        id: widestLabel

        font: Theme.mono
        text: grid.rows > 0 ? grid.model.rowLabel(grid.rows - 1) : ""
    }

    /// How wide a character is in the cell face. The grid is set in mono, so
    /// this times a character count is a width -- which is what lets a column
    /// fit its contents without measuring every string in it.
    TextMetrics {
        id: character

        font: Theme.mono
        text: "0"
    }

    /// The widest column head this table can produce, measured rather than
    /// counted: the heads are set in the label face, which is not the cell
    /// face, so a character count would be the wrong currency.
    ///
    /// The last column is the widest -- an index tuple grows with its digits --
    /// so one measurement answers for all of them. A column fitted to its
    /// values alone would elide its own head, which is the one thing in the
    /// column that says which elements are in it.
    TextMetrics {
        id: widestHead

        font: Theme.micro
        text: (grid.model && grid.modelColumns > 0)
              ? grid.model.columnLabel(grid.modelColumns - 1) : ""
    }

    /// The widest cell seen so far in this table, in characters, or 0 before
    /// anything has been measured.
    ///
    /// Only what the model has already read is walked, so this costs no read of
    /// its own -- see DatasetTableModel::widestCell. It is re-asked when the
    /// table changes underneath and when the view scrolls onto new rows, both
    /// through `measure()` below rather than through a binding: a width that
    /// re-derived itself from the view's own geometry is exactly the binding
    /// loop this file has a comment about.
    ///
    /// It only ever grows within one table, and starts again at the next one.
    /// A column that also narrowed would resize itself under a moving pointer
    /// every time the reader scrolled past a shorter value, which is a worse
    /// answer than a column with some room to spare in it.
    property int widestCell: 0

    /// The number of columns, taken from the model rather than from the
    /// TableView.
    ///
    /// This is the whole of the fix for a column width that moved the header
    /// and left the cells behind. `columnWidth` has to be re-applied with
    /// forceLayout(), and forceLayout() makes TableView re-announce `columns`;
    /// while the textual branch below read *that*, calling it closed a loop, so
    /// it was never called -- and only the header, which binds the width
    /// directly, ever moved. The model's count is the same number and does not
    /// answer to the layout, which breaks the cycle. It is assigned rather than
    /// bound because columnCount() is a call, and nothing would tell a binding
    /// on it to run again.
    property int modelColumns: 0

    /// Numbers want a column as wide as their widest value and line up in it.
    /// Text does not: an elided string tells the reader nothing, so a string
    /// grid spreads its columns across whatever width the view has.
    readonly property real columnWidth: {
        const floor = grid.autoWidth ? grid.fittedWidth : grid.cellWidth
        if (grid.textual && grid.modelColumns > 0) {
            const available = grid.width - grid.indexWidth - Theme.gapXL
            return Theme.snap(Math.max(floor,
                                       Math.floor(available / grid.modelColumns)))
        }
        return Theme.snap(floor)
    }

    /// The width the measured contents ask for, floored so a column of single
    /// digits still has a column's worth of room and capped so one pathological
    /// string cannot take the window. The bounds are the slider's own, so
    /// turning auto off lands somewhere the slider can express.
    readonly property int fittedWidth: {
        const values = Math.ceil(grid.widestCell * character.advanceWidth)
        const head = Math.ceil(widestHead.advanceWidth)
        const wanted = Math.max(values, head) + Theme.gapM * 2
        return Math.max(Theme.s11, Math.min(Theme.s13 * 2, wanted))
    }

    /// Re-measure the cells the view is over, and re-read the column count.
    ///
    /// Called rather than bound, for the reason `modelColumns` gives, and
    /// driven off the TableView's visible-range properties rather than off its
    /// scroll offset: those change once per row or column crossed instead of
    /// once per pixel.
    function measure() {
        if (!grid.model)
            return
        grid.modelColumns = grid.model.columnCount()
        if (!grid.autoWidth)
            return
        const firstColumn = Math.max(0, table.leftColumn)
        const measured = grid.model.widestCell(
            Math.max(0, table.topRow),
            Math.max(1, table.bottomRow - table.topRow + 1),
            firstColumn,
            Math.max(1, table.rightColumn - firstColumn + 1))
        grid.widestCell = Math.max(grid.widestCell, measured)
    }

    /// Forget what was measured and measure again. A different table, or the
    /// same values written a different way, is a different set of widths.
    function remeasure() {
        grid.widestCell = 0
        grid.measure()
    }

    onWidthChanged: Qt.callLater(table.forceLayout)
    onAutoWidthChanged: grid.remeasure()
    // Now safe: nothing `columnWidth` reads is written by forceLayout().
    onColumnWidthChanged: Qt.callLater(table.forceLayout)

    Component.onCompleted: grid.measure()
    /// Emitted when a cell is clicked, so a view built around this one can
    /// follow the selection.
    signal cellActivated(int row, int column)

    readonly property int rows: Math.max(table.rows, 0)
    readonly property int columns: Math.max(table.columns, 0)
    readonly property int currentRow: internal.currentRow
    readonly property int currentColumn: internal.currentColumn

    QtObject {
        id: internal
        property int currentRow: -1
        property int currentColumn: -1
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.surfaceInset
    }

    // Fixed corner where the index column meets the header row.
    Rectangle {
        id: corner

        width: grid.indexWidth
        height: grid.rowHeight
        color: Theme.surface
        z: 3

        Text {
            anchors.right: parent.right
            anchors.rightMargin: Theme.gapM
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("index")
            font: Theme.micro
            color: Theme.textSecondary
        }
    }

    // Column headers, scrolled in step with the table.
    ListView {
        id: headerRow

        anchors.left: corner.right
        anchors.right: parent.right
        anchors.top: parent.top
        height: grid.rowHeight
        orientation: ListView.Horizontal
        interactive: false
        clip: true
        z: 2
        model: grid.columns
        contentX: table.contentX

        delegate: Rectangle {
            required property int index

            width: grid.columnWidth
            height: grid.rowHeight
            color: Theme.surface

            HoverHandler { id: headHover }

            // The head follows its column's own alignment, or a wide text
            // column ends up labelled at the far end of itself.
            Text {
                id: head

                anchors.left: parent.left
                anchors.leftMargin: Theme.gapM
                anchors.right: parent.right
                anchors.rightMargin: Theme.gapM
                anchors.verticalCenter: parent.verticalCenter
                text: grid.model.columnLabel(index)
                font: Theme.micro
                elide: Text.ElideRight
                horizontalAlignment: grid.textual ? Text.AlignLeft
                                                  : Text.AlignRight
                color: index === grid.currentColumn ? Theme.textEmphasis
                                                    : Theme.textSecondary

                // An index tuple grows with rank, and at rank 12 it does not
                // fit a column of numbers. Which element a column names is not
                // optional information.
                AppToolTip {
                    shown: head.truncated && headHover.hovered
                    verbatim: true
                    text: head.text
                }
            }

            Rectangle {
                anchors.left: parent.left
                width: grid.ruleWidth
                height: parent.height
                color: Theme.surfaceRaised
            }
        }
    }

    // Row indices, scrolled in step with the table.
    ListView {
        id: indexColumn

        anchors.left: parent.left
        anchors.top: corner.bottom
        anchors.bottom: parent.bottom
        width: grid.indexWidth
        interactive: false
        clip: true
        z: 2
        model: grid.rows
        contentY: table.contentY

        delegate: Rectangle {
            required property int index

            width: grid.indexWidth
            height: grid.rowHeight
            color: Theme.surface

            HoverHandler { id: indexHover }

            Text {
                id: rowLabel

                anchors.left: parent.left
                anchors.leftMargin: Theme.gapS
                anchors.right: parent.right
                anchors.rightMargin: Theme.gapM
                anchors.verticalCenter: parent.verticalCenter
                text: grid.model.rowLabel(index)
                font: Theme.mono
                elide: Text.ElideLeft
                horizontalAlignment: Text.AlignRight
                color: index === grid.currentRow ? Theme.textPrimary
                                                 : Theme.textDisabled

                AppToolTip {
                    shown: rowLabel.truncated && indexHover.hovered
                    verbatim: true
                    text: rowLabel.text
                }
            }
        }
    }

    // The header is separated from the data by a rule one step stronger than a
    // plain hairline, as the mockup has it.
    Rectangle {
        anchors.top: parent.top
        anchors.topMargin: grid.rowHeight
        width: parent.width
        height: grid.ruleWidth
        color: Theme.borderStrong
        z: 4
    }

    Rectangle {
        anchors.left: parent.left
        anchors.leftMargin: grid.indexWidth
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: grid.ruleWidth
        color: Theme.borderStrong
        z: 4
    }

    TableView {
        id: table

        anchors.fill: parent
        anchors.topMargin: grid.rowHeight
        anchors.leftMargin: grid.indexWidth
        clip: true
        model: AppController.datasetModel
        columnSpacing: 0
        rowSpacing: 0

        ScrollBar.vertical: ScrollBar {}
        ScrollBar.horizontal: ScrollBar {}

        columnWidthProvider: () => grid.columnWidth
        rowHeightProvider: () => grid.rowHeight

        // The rows and columns on screen, which is the rectangle a fitted
        // column is fitted to. These change once per row or column crossed,
        // where contentX/contentY change once per pixel.
        onTopRowChanged: Qt.callLater(grid.measure)
        onBottomRowChanged: Qt.callLater(grid.measure)
        onLeftColumnChanged: Qt.callLater(grid.measure)
        onRightColumnChanged: Qt.callLater(grid.measure)

        delegate: Rectangle {
            id: cell

            required property string display
            /// The same value unrounded and unelided. The grid draws what fits;
            /// the pointer reaches the whole of it.
            required property string toolTip
            /// The same cell as a double, which is what the fill is computed
            /// from. NaN for anything that is not a number.
            required property real number
            required property int row
            required property int column

            readonly property bool current: row === internal.currentRow
                                            && column === internal.currentColumn

            /// Whether this cell carries a value the ramp could place.
            readonly property bool filled: grid.colorCells && isFinite(number)
            readonly property color fill: grid.fillFor(cell.number)

            implicitWidth: grid.columnWidth
            implicitHeight: grid.rowHeight
            // The fill outranks both the picked cell and the banding: it is
            // the value itself, where those two are the reader's place in the
            // table. What marks the picked cell instead is the accent outline
            // below, which sits over the fill rather than replacing it.
            color: cell.filled ? cell.fill
                 : current ? Theme.surfaceActive
                 : (grid.banded && row % 2 !== 0) ? Theme.rowStripe
                 : "transparent"

            HoverHandler { id: cellHover }

            Text {
                id: value

                anchors.fill: parent
                anchors.leftMargin: Theme.gapM
                anchors.rightMargin: Theme.gapM
                text: cell.display
                font: Theme.mono
                // The value has to read on whatever the ramp put behind it,
                // and half of every ramp is too pale for the body ink. See
                // Theme.inkOn.
                color: cell.filled ? Theme.inkOn(cell.fill) : Theme.textPrimary
                elide: Text.ElideRight
                // Numbers line up on the right; text reads from the left.
                horizontalAlignment: grid.textual ? Text.AlignLeft
                                                  : Text.AlignRight
                verticalAlignment: Text.AlignVCenter
            }

            // Shown for every cell, not only for a truncated one: what the grid
            // draws may be a rounded float as well as an elided string, and
            // neither of those says so by looking short.
            AppToolTip {
                shown: cellHover.hovered && cell.toolTip !== ""
                verbatim: true
                text: cell.toolTip
            }

            TapHandler {
                onTapped: {
                    internal.currentRow = cell.row
                    internal.currentColumn = cell.column
                    grid.cellActivated(cell.row, cell.column)
                }
            }

            // Where the picked cell is marked once its ground is spoken for.
            // An outline rather than a fill, because the fill is carrying the
            // value and taking it away to say "you clicked here" would answer
            // a question with the wrong datum.
            Rectangle {
                anchors.fill: parent
                visible: cell.current && cell.filled
                color: "transparent"
                border.width: Theme.borderWidthAccent
                border.color: Theme.accent
            }

            // line-1, the weight the design system rules a table with. These
            // were drawn at `surface` -- n1 on an n0 ground -- which is a black
            // line on black, so the setting that turns them on did nothing
            // visible at all.
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: grid.ruleWidth
                visible: grid.gridLines
                color: Theme.border
            }

            Rectangle {
                anchors.left: parent.left
                width: grid.ruleWidth
                height: parent.height
                visible: grid.gridLines
                color: Theme.border
            }
        }
    }

    // The same values written a different way are a different set of widths,
    // and a column fitted to the old ones would clip the new. The model emits
    // this rather than resetting, so nothing else here has to react to it.
    Connections {
        target: grid.model
        function onFloatFormatChanged() { Qt.callLater(grid.remeasure) }
    }

    // A new dataset invalidates any cell the user had picked in the old one,
    // and so does a rearranged one: the cell under those coordinates is a
    // different element now.
    Connections {
        target: AppController
        function onSelectionChanged() {
            internal.currentRow = -1
            internal.currentColumn = -1
            Qt.callLater(grid.remeasure)
        }
        function onTableLayoutChanged() {
            internal.currentRow = -1
            internal.currentColumn = -1
            Qt.callLater(grid.remeasure)
            // The sticky header and index column are ListViews of their own,
            // so their labels are not bound to anything the model reset
            // touches; re-ask for them.
            headerRow.model = 0
            indexColumn.model = 0
            headerRow.model = Qt.binding(() => grid.columns)
            indexColumn.model = Qt.binding(() => grid.rows)
        }
    }
}
