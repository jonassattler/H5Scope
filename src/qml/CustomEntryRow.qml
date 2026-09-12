// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import QtQuick.Layouts
import H5Scope.Backend

/// One line of a custom plot, in the data settings rail.
///
///     +--------------------------------------------------+
///     | [x] SLICE   [ /series/half[:]              ]  [x] |
///     |     ALIAS   [ morning                      ]     |
///     |     SCALING ( ) align   (o) stretch              |
///     +--------------------------------------------------+
///
/// A card apiece, like the saved views above them, and for the same reason:
/// three of these stacked in a rail read as one paragraph with controls
/// scattered through it unless something says where one stops and the next
/// begins. An entry is a thing -- a line on the plot, with a name and a place
/// to sit -- and a bordered ground says that.
///
/// The slice gets a line to itself because it is the long thing: a path, a
/// subscript, and nothing else on the row to squeeze it. What used to sit
/// beside it -- an alias box and two scaling columns -- is on the line below,
/// where the labels can be read.
///
/// The scaling row is only there when the choice means something. Align and
/// stretch put the points in the same places when a line is exactly as long as
/// the axis, which is nearly every line nearly always, and two reserved columns
/// of dimmed ticks down the whole list is a hundred and twenty pixels spent on
/// a question with no answer. Absent, the words can appear beside the controls
/// on the rows that do have one, which is where a reader wants to read them.
///
/// The two boxes keep the contract every box in this application that acts on
/// data keeps, and the first is PostprocessStepRow's almost line for line:
/// every keystroke is checked, nothing is applied until the reader commits, an
/// uncommitted edit says so in the ground rather than in a colour, and an edit
/// that will not read is applied anyway and left in the box with its reason
/// underneath. That last one is the part worth restating -- refusing to apply
/// it would leave the reader looking at a box saying one thing and a stroke on
/// the plot saying another, which is the one state this must never be in.
Rectangle {
    id: row

    /// The CustomPlot this row belongs to, and which row of it this is.
    property var plot
    property int rowIndex: -1

    // The roles, handed down rather than required here: a required property
    // cannot also be one the component already declares, and the delegate that
    // wraps this is what takes them off the model.
    property string expression: ""
    property string alias: ""
    property string error: ""
    property int scaling: CustomPlot.Align
    property bool scalable: false
    property bool drawn: true

    /// Why what is *in the box* will not read, as opposed to why the applied
    /// line did not. Two channels, as the pipeline has: one about the text and
    /// one about the last read.
    property string problem: ""
    readonly property bool pending: box.text.trim() !== row.expression
                                    && row.problem === ""
    readonly property bool troubled: row.problem !== "" || row.error !== ""

    implicitHeight: layout.implicitHeight + Theme.gapS * 2
    radius: Theme.radiusS
    color: Theme.surfaceRaised
    border.width: Theme.borderWidth
    // A line that will not draw says so in its own edge as well as in the
    // sentence under it: a reader scrolling a list of eight wants to find the
    // broken one without reading all of them.
    border.color: row.troubled ? Theme.warning : Theme.border

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

    function commitAlias() {
        if (!row.plot || row.rowIndex < 0)
            return
        row.plot.setAlias(row.rowIndex, aliasBox.text)
        aliasBox.text = row.alias
    }

    ColumnLayout {
        id: layout

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: Theme.gapS
        anchors.topMargin: Theme.gapS
        // Flush right, where every other rightmost control in this panel is.
        anchors.rightMargin: 0
        spacing: Theme.gapXS

        // --- the slice, with the whole width to itself -------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gapS

            AppCheckBox {
                objectName: "entryDrawn"

                Layout.alignment: Qt.AlignVCenter
                // No label, so no gap for one: the control's width is its
                // indicator plus the room a label would have taken, and the
                // rows below reserve the indicator alone. Eight pixels of
                // difference is all it takes to stop three labels being a
                // column.
                spacing: 0
                checked: row.drawn
                onToggled: {
                    if (row.plot)
                        row.plot.setSeriesVisible(row.rowIndex, checked)
                }
            }

            Text {
                Layout.preferredWidth: labels.width
                text: qsTr("slice")
                font: Theme.microLabel
                color: Theme.textDisabled
                verticalAlignment: Text.AlignVCenter
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

            AppIconButton {
                objectName: "removeEntry"

                Layout.alignment: Qt.AlignVCenter
                glyph: "close"
                ink: Theme.danger
                bare: true
                onClicked: {
                    if (row.plot)
                        row.plot.removeEntry(row.rowIndex)
                }
            }
        }

        // --- what the legend should call it instead -----------------------
        // A slice says exactly what a line is and nothing about what it means,
        // which is the right default and the wrong label on a plot of six of
        // them. Sans rather than mono, which is the one place this card parts
        // company with the box above: what is in there is a slice and what is
        // in here is a phrase. Nothing is checked, because there is nothing it
        // could be wrong about.
        RowLayout {
            Layout.fillWidth: true
            Layout.rightMargin: Theme.smallControlHeight + Theme.gapS
            spacing: Theme.gapS

            Item { Layout.preferredWidth: Theme.indicatorSize }

            Text {
                Layout.preferredWidth: labels.width
                text: qsTr("alias")
                font: Theme.microLabel
                color: Theme.textDisabled
                verticalAlignment: Text.AlignVCenter
            }

            FilterInput {
                id: aliasBox

                objectName: "entryAlias"

                Layout.fillWidth: true
                implicitHeight: Theme.smallControlHeight
                font: Theme.body
                text: row.alias
                        placeholderText: qsTr("a name for the legend")

                onAccepted: row.commitAlias()
                onActiveFocusChanged: if (!aliasBox.activeFocus) row.commitAlias()
                Keys.onEscapePressed: {
                    aliasBox.text = row.alias
                    aliasBox.focus = false
                }
            }
        }

        // --- where it goes when it is not the axis's length ---------------
        RowLayout {
            Layout.fillWidth: true
            Layout.rightMargin: Theme.smallControlHeight + Theme.gapS
            spacing: Theme.gapS
            visible: row.scalable

            Item { Layout.preferredWidth: Theme.indicatorSize }

            Text {
                Layout.preferredWidth: labels.width
                text: qsTr("scaling")
                font: Theme.microLabel
                color: Theme.textDisabled
                verticalAlignment: Text.AlignVCenter
            }

            AppRadioButton {
                objectName: "scalingAlign"

                text: qsTr("align")
                // The bindings here are the only writers of `checked`;
                // auto-exclusion would have the control write it too, and an
                // imperative write over a binding silently replaces it.
                autoExclusive: false
                checked: row.scaling === CustomPlot.Align
                onClicked: {
                    if (row.plot)
                        row.plot.setScaling(row.rowIndex, CustomPlot.Align)
                }
            }

            AppRadioButton {
                objectName: "scalingStretch"

                text: qsTr("stretch")
                autoExclusive: false
                checked: row.scaling === CustomPlot.Stretch
                onClicked: {
                    if (row.plot)
                        row.plot.setScaling(row.rowIndex, CustomPlot.Stretch)
                }
            }

            Item { Layout.fillWidth: true }
        }

        // The reason, under the line it is about. Wrapped rather than elided:
        // these sentences say what to write instead, and half of one is half
        // an instruction.
        Text {
            objectName: "entryNote"

            Layout.fillWidth: true
            Layout.leftMargin: Theme.indicatorSize + Theme.gapS * 2 + labels.width
            Layout.rightMargin: Theme.smallControlHeight + Theme.gapS
            Layout.bottomMargin: Theme.gapXS
            visible: text !== ""
            text: row.problem !== "" ? row.problem
                : row.error !== "" ? row.error
                : row.pending ? qsTr("not applied yet — press Return") : ""
            font: Theme.caption
            color: row.troubled ? Theme.warning : Theme.accent
            wrapMode: Text.WordWrap
        }
    }

    /// The width the three leading labels share, so every box in the card
    /// starts in one column. Measured off the longest of them rather than
    /// typed, because how wide text comes out is the host's decision.
    TextMetrics {
        id: labels

        font: Theme.microLabel
        text: qsTr("scaling")
    }

    // A row whose text was changed from anywhere else -- a restored view, a
    // dataset added from the tree -- is still this row, so the boxes follow it.
    onExpressionChanged: {
        if (!box.activeFocus) {
            box.text = row.expression
            row.problem = ""
        }
    }

    onAliasChanged: {
        if (!aliasBox.activeFocus)
            aliasBox.text = row.alias
    }
}
