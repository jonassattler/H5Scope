// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import H5Scope.Backend

/// The Data Viewer's "data settings" sidebar: one block per dimension of the
/// selected dataset, saying which of its indices appear and which axis they
/// appear on.
///
/// It answers which values the tab shows, which is the one question all three
/// of its presentations share -- the table, the plot and the image each draw
/// whatever this resolves to. Their own panels sit in the same rail and answer
/// only how they draw it.
///
/// Every control here writes to AppController.tableSetupModel and reads back
/// from it -- the model clamps, parses and enforces the one-axis-per-dimension
/// rule, so nothing in this file has to. That is also why the checkboxes are
/// bound to a single `onX` flag rather than to one boolean each: "both" and
/// "neither" are states the panel cannot express.
Rectangle {
    id: panel

    color: Theme.surface

    /// Width of the x/y column, so its heading lines up with its checkboxes.
    readonly property int axisColumnWidth: Theme.s10

    // --- heading ----------------------------------------------------------
    Rectangle {
        id: heading

        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: Theme.treeHeaderHeight
        color: Theme.surface

        Text {
            anchors.left: parent.left
            anchors.leftMargin: Theme.gapM
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("data settings")
            font: Theme.micro
            color: Theme.textSecondary
        }

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: Theme.borderWidth
            color: Theme.border
        }
    }

    // --- column headings, once rather than once per dimension -------------
    Item {
        id: columnHeadings

        anchors.top: heading.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: Theme.rowHeight

        Text {
            anchors.left: parent.left
            anchors.leftMargin: Theme.gapM
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("dimension")
            font: Theme.microLabel
            color: Theme.textDisabled
        }

        Row {
            anchors.right: parent.right
            anchors.rightMargin: Theme.gapM
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.gapS

            Repeater {
                model: [qsTr("x"), qsTr("y")]

                delegate: Text {
                    required property string modelData

                    width: (panel.axisColumnWidth - Theme.gapS) / 2
                    text: modelData
                    font: Theme.microLabel
                    color: Theme.textDisabled
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: Theme.borderWidth
            color: Theme.border
        }
    }

    ListView {
        id: dimensions

        anchors.top: columnHeadings.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        clip: true
        model: AppController.tableSetupModel
        boundsBehavior: Flickable.StopAtBounds

        ScrollBar.vertical: ScrollBar {}

        delegate: Item {
            id: dimension

            required property int index
            required property int extent
            required property int mode
            required property int indexValue
            required property int rangeFirst
            required property int rangeLast
            required property string expression
            required property string expressionError
            required property bool onX
            required property string summary

            /// The highest index this dimension has. An empty dimension has
            /// none, and every control below collapses to zero rather than to
            /// a negative bound. Not `top`: QQuickItem declares that one FINAL.
            readonly property int lastIndex: Math.max(dimension.extent - 1, 0)
            readonly property var setup: AppController.tableSetupModel

            width: dimensions.width
            implicitHeight: body.implicitHeight + Theme.gapM * 2

            ColumnLayout {
                id: body

                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.leftMargin: Theme.gapM
                anchors.rightMargin: Theme.gapM
                anchors.topMargin: Theme.gapM
                spacing: Theme.gapS

                // --- name, and the axis it sits on ------------------------
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.gapS

                    Text {
                        text: qsTr("dim %1").arg(dimension.index)
                        font: Theme.micro
                        color: Theme.textPrimary
                    }

                    Text {
                        id: summary

                        Layout.fillWidth: true
                        text: dimension.summary
                        font: Theme.monoSmall
                        color: Theme.textDisabled
                        elide: Text.ElideRight

                        HoverHandler { id: summaryHover }

                        // A Custom selection can list dozens of indices, and
                        // the rail is 212px wide.
                        AppToolTip {
                            shown: summary.truncated && summaryHover.hovered
                            verbatim: true
                            text: summary.text
                        }
                    }

                    Row {
                        objectName: "axisColumn"

                        spacing: Theme.gapS

                        // Which axis a dimension sits on is a statement about
                        // the *dataset*, and a running pipeline is drawing
                        // something else: the output array is a new array with
                        // its own rank, and it takes the ordinary default --
                        // last dimension along the columns, the rest down the
                        // rows. Greyed rather than hidden, so the row keeps its
                        // shape and says the choice is unavailable rather than
                        // quietly ignoring it. The indices beside it still mean
                        // everything: they are the pipeline's slice step.
                        enabled: !AppController.postprocessActive
                        opacity: enabled ? 1.0 : 0.4

                        Repeater {
                            model: [true, false]

                            delegate: CheckBox {
                                id: axisBox

                                required property bool modelData

                                width: (panel.axisColumnWidth - Theme.gapS) / 2
                                height: Theme.gapL
                                padding: 0
                                hoverEnabled: true
                                checked: dimension.onX === axisBox.modelData
                                // Not `checked = !checked`: the pair is one
                                // flag, so clicking the ticked box is a no-op
                                // and neither box can end up clear.
                                onClicked: dimension.setup.setAxis(dimension.index,
                                                                   axisBox.modelData)

                                indicator: Rectangle {
                                    anchors.centerIn: parent
                                    implicitWidth: Theme.gapL
                                    implicitHeight: Theme.gapL
                                    radius: Theme.radiusS
                                    color: Theme.surfaceInset
                                    border.width: Theme.borderWidth
                                    border.color: axisBox.hovered ? Theme.borderStrong
                                                                  : Theme.border

                                    Rectangle {
                                        anchors.centerIn: parent
                                        width: parent.width - Theme.gapXS * 2
                                        height: width
                                        radius: Theme.radiusS
                                        color: Theme.accent
                                        visible: axisBox.checked
                                    }
                                }

                                contentItem: Item {}
                            }
                        }
                    }
                }

                // --- how much of it to show -------------------------------
                ButtonGroup { id: modes }

                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: Theme.gapS
                    rowSpacing: Theme.gapXS

                    Repeater {
                        model: [
                            { label: qsTr("all"),    value: TableSetupModel.All },
                            { label: qsTr("index"),  value: TableSetupModel.Index },
                            { label: qsTr("range"),  value: TableSetupModel.Range },
                            { label: qsTr("custom"), value: TableSetupModel.Custom }
                        ]

                        delegate: AppRadioButton {
                            required property var modelData

                            Layout.fillWidth: true
                            ButtonGroup.group: modes
                            text: modelData.label
                            checked: dimension.mode === modelData.value
                            onClicked: dimension.setup.setMode(dimension.index,
                                                               modelData.value)
                        }
                    }
                }

                // --- all: the whole dimension, and nothing to decide -------
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.gapS
                    visible: dimension.mode === TableSetupModel.All

                    NumberField {
                        enabled: false
                        value: 0
                    }

                    AppRangeSlider {
                        Layout.fillWidth: true
                        enabled: false
                        to: dimension.lastIndex
                        firstValue: 0
                        secondValue: dimension.lastIndex
                    }

                    NumberField {
                        enabled: false
                        value: dimension.lastIndex
                    }
                }

                // --- index: one plane -------------------------------------
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.gapS
                    visible: dimension.mode === TableSetupModel.Index

                    // Live, not a readout: scrubbing through the planes of a
                    // dimension is the reason this mode exists, and typing
                    // each index in turn to do it is not scrubbing.
                    AppSlider {
                        objectName: "indexSlider" + dimension.index

                        Layout.fillWidth: true
                        to: dimension.lastIndex
                        knobValue: dimension.indexValue
                        onMovedTo: amount => dimension.setup.setIndex(
                                       dimension.index, amount)
                    }

                    NumberField {
                        to: dimension.lastIndex
                        value: dimension.indexValue
                        onCommitted: amount => dimension.setup.setIndex(dimension.index,
                                                                        amount)
                    }
                }

                // --- range: a span, from either end -----------------------
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.gapS
                    visible: dimension.mode === TableSetupModel.Range

                    NumberField {
                        to: dimension.lastIndex
                        value: dimension.rangeFirst
                        onCommitted: amount => dimension.setup.setRange(
                                         dimension.index, amount, dimension.rangeLast)
                    }

                    AppRangeSlider {
                        Layout.fillWidth: true
                        to: dimension.lastIndex
                        firstValue: dimension.rangeFirst
                        secondValue: dimension.rangeLast
                        onMovedTo: (first, second) => dimension.setup.setRange(
                                       dimension.index, first, second)
                    }

                    NumberField {
                        to: dimension.lastIndex
                        value: dimension.rangeLast
                        onCommitted: amount => dimension.setup.setRange(
                                         dimension.index, dimension.rangeFirst, amount)
                    }
                }

                // --- custom: whatever the reader can write down -----------
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.gapXS
                    visible: dimension.mode === TableSetupModel.Custom

                    FilterInput {
                        id: expressionBox

                        Layout.fillWidth: true
                        implicitHeight: Theme.smallControlHeight
                        text: dimension.expression
                        invalid: dimension.expressionError !== ""
                        placeholderText: qsTr("0,2,5:9")
                        onTextEdited: dimension.setup.setExpression(dimension.index,
                                                                    text)
                    }

                    // Caption, not micro: this is a sentence, and micro
                    // uppercases what it is given -- which turns a message
                    // about what went wrong into a machine label.
                    Text {
                        Layout.fillWidth: true
                        visible: dimension.expressionError !== ""
                        text: dimension.expressionError
                        font: Theme.caption
                        color: Theme.warning
                        wrapMode: Text.WordWrap
                    }
                }
            }

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: Theme.borderWidth
                color: Theme.border
            }
        }
    }
}
