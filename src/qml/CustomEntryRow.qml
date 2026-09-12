// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import QtQuick.Layouts
import H5Scope.Backend

/// One line of a custom plot, in the data settings rail.
///
///     [x] /committed/morning[:]              (o) align  ( ) stretch  [x]
///         this selects 4 × 3, and an entry is one line — hold every
///         dimension but one at a single index
///
/// The box keeps the contract every box in this application that acts on data
/// keeps, and it is PostprocessStepRow's almost line for line: every keystroke
/// is checked, nothing is applied until the reader commits, an uncommitted edit
/// says so in the ground rather than in a colour, and an edit that will not
/// read is applied anyway and left in the box with its reason underneath. That
/// last one is the part worth restating -- refusing to apply it would leave the
/// reader looking at a box saying one thing and a stroke on the plot saying
/// another, which is the one state this must never be in.
///
/// The tick on the left is the same question the legend's tick asks, and writes
/// the same property. A reader working through a list of eight slices should
/// not have to open the legend to take one out of the picture.
Item {
    id: row

    /// The CustomPlot this row belongs to, and which row of it this is.
    property var plot
    property int rowIndex: -1

    // The roles, handed down rather than required here: a required property
    // cannot also be one the component already declares, and the delegate that
    // wraps this is what takes them off the model.
    property string expression: ""
    property string error: ""
    property int scaling: CustomPlot.Align
    property bool scalable: false
    property bool drawn: true

    implicitHeight: layout.implicitHeight

    /// Why what is *in the box* will not read, as opposed to why the applied
    /// line did not. Two channels, as the pipeline has: one about the text and
    /// one about the last read.
    property string problem: ""
    readonly property bool pending: box.text.trim() !== row.expression
                                    && row.problem === ""
    readonly property bool troubled: row.problem !== "" || row.error !== ""

    function commit() {
        if (!row.plot || row.rowIndex < 0)
            return
        const wanted = box.text.trim()
        if (wanted === row.expression) {
            row.problem = ""
            return
        }
        row.plot.setExpression(row.rowIndex, wanted)
        // What the model made of it is what the row is now stating.
        box.text = row.expression
        row.problem = ""
    }

    ColumnLayout {
        id: layout

        width: row.width
        spacing: Theme.gapXS

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gapS

            AppCheckBox {
                checked: row.drawn
                onToggled: {
                    if (row.plot)
                        row.plot.setSeriesVisible(row.rowIndex, checked)
                }

                AppToolTip {
                    shown: parent.hovered
                    text: qsTr("draw this line")
                }
            }

            FilterInput {
                id: box

                objectName: "entryBox"

                Layout.fillWidth: true
                implicitHeight: Theme.smallControlHeight
                text: row.expression
                placeholderText: qsTr("/group/dataset[:, 0]")
                invalid: row.problem !== "" || row.error !== ""
                pending: row.pending

                onTextEdited: {
                    row.problem = row.plot
                        ? row.plot.entryError(row.rowIndex, box.text) : ""
                }
                onAccepted: row.commit()
                onActiveFocusChanged: if (!box.activeFocus) row.commit()
                Keys.onEscapePressed: {
                    box.text = row.expression
                    row.problem = ""
                    box.focus = false
                }
            }

            // Where this line goes when it is not the same length as the axis.
            // Disabled rather than hidden when it is: the row keeps its shape
            // down a list of eight, and the reader can see that the question
            // was asked and has no answer to give.
            Row {
                spacing: Theme.gapS
                opacity: row.scalable ? 1.0 : 0.4

                AppRadioButton {
                    text: qsTr("align")
                    // The bindings below are the only writers of `checked`;
                    // auto-exclusion would have the control write it too, and
                    // an imperative write over a binding silently replaces it.
                    autoExclusive: false
                    enabled: row.scalable
                    checked: row.scaling === CustomPlot.Align
                    onClicked: {
                        if (row.plot)
                            row.plot.setScaling(row.rowIndex, CustomPlot.Align)
                    }

                    AppToolTip {
                        shown: parent.hovered
                        text: qsTr("sample i at position i; the line stops " +
                                   "where the shorter of the two runs out")
                    }
                }

                AppRadioButton {
                    text: qsTr("stretch")
                    autoExclusive: false
                    enabled: row.scalable
                    checked: row.scaling === CustomPlot.Stretch
                    onClicked: {
                        if (row.plot)
                            row.plot.setScaling(row.rowIndex, CustomPlot.Stretch)
                    }

                    AppToolTip {
                        shown: parent.hovered
                        text: qsTr("spread the samples over the whole of the " +
                                   "x axis, however many there are")
                    }
                }
            }

            AppIconButton {
                objectName: "removeEntry"

                glyph: "close"
                ink: Theme.danger
                bare: true
                hint: qsTr("take this line out of the plot")
                onClicked: {
                    if (row.plot)
                        row.plot.removeEntry(row.rowIndex)
                }
            }
        }

        // The reason, under the line it is about. Wrapped rather than elided:
        // these sentences say what to write instead, and half of one is half
        // an instruction.
        Text {
            objectName: "entryNote"

            Layout.fillWidth: true
            Layout.leftMargin: Theme.indicatorSize + Theme.gapS
            visible: text !== ""
            text: row.problem !== "" ? row.problem
                : row.error !== "" ? row.error
                : row.pending ? qsTr("not applied yet — press Return") : ""
            font: Theme.caption
            color: row.troubled ? Theme.warning : Theme.accent
            wrapMode: Text.WordWrap
        }
    }

    // A row whose text was changed from anywhere else -- a restored view, a
    // dataset added from the tree -- is still this row, so the box follows it.
    onExpressionChanged: {
        if (!box.activeFocus) {
            box.text = row.expression
            row.problem = ""
        }
    }
}
