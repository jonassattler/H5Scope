// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

/// One compound element, opened out.
///
/// A compound has no single value, so the one line a grid cell can give it is
/// the whole struct squeezed together and elided -- which for a struct is the
/// same as showing nothing. This is the other treatment, and it is the string
/// panes' treatment applied to a different shape of value: pick an element in
/// the grid, and read it whole underneath.
///
/// Twice, deliberately. The named rows are the readable form -- what the
/// members are called, what type each one is, and what it holds. The JSON is
/// the form that leaves the program: one block, selectable, that pastes into
/// anything that can parse it. Neither is a summary of the other.
Column {
    id: pane

    /// { label, text, json, fields: [{ name, type, value }] }, from
    /// DatasetTableModel::elementAt.
    property var element: ({})

    readonly property var fields:
        (element && element.fields !== undefined) ? element.fields : []
    readonly property string json:
        (element && element.json !== undefined) ? element.json : ""
    readonly property string label:
        (element && element.label !== undefined) ? element.label : ""

    /// Room for the widest member name this element has, so the values line up
    /// down the pane rather than stepping in and out with each name.
    ///
    /// FontMetrics rather than a TextMetrics whose text is reassigned in the
    /// loop: writing to a measuring object that the same expression reads back
    /// is a binding loop, and this application treats a QML warning as a build
    /// failure.
    readonly property real nameWidth: {
        let widest = 0
        for (let i = 0; i < pane.fields.length; ++i) {
            widest = Math.max(widest, nameFont.advanceWidth(pane.fields[i].name))
        }
        return Math.min(widest, width / 3)
    }

    /// The same, for the type column beside it. Both are capped at a share of
    /// the pane so one long name cannot push the values off the right edge.
    readonly property real typeWidth: {
        let widest = 0
        for (let i = 0; i < pane.fields.length; ++i) {
            widest = Math.max(widest, typeFont.advanceWidth(pane.fields[i].type))
        }
        return Math.min(widest, width / 4)
    }

    spacing: Theme.gapM

    FontMetrics {
        id: nameFont
        font: Theme.microLabel
    }

    FontMetrics {
        id: typeFont
        font: Theme.readout
    }

    // --- the members, named ---------------------------------------------
    Rectangle {
        width: parent.width
        height: fieldColumn.height + header.height + Theme.gapM
        color: Theme.surface
        radius: Theme.radiusS
        border.width: Theme.borderWidth
        border.color: Theme.border

        Item {
            id: header

            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: Theme.borderWidth
            height: Theme.treeHeaderHeight

            Text {
                anchors.left: parent.left
                anchors.leftMargin: Theme.gapM
                anchors.verticalCenter: parent.verticalCenter
                text: pane.label === "" ? qsTr("element")
                                        : qsTr("element %1").arg(pane.label)
                font: Theme.micro
                color: Theme.textSecondary
            }

            Text {
                anchors.right: parent.right
                anchors.rightMargin: Theme.gapM
                anchors.verticalCenter: parent.verticalCenter
                text: pane.fields.length === 1 ? qsTr("1 field")
                                               : qsTr("%1 fields").arg(pane.fields.length)
                font: Theme.readout
                color: Theme.textDisabled
            }

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: Theme.borderWidth
                color: Theme.border
            }
        }

        Column {
            id: fieldColumn

            anchors.top: header.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: Theme.borderWidth
            anchors.rightMargin: Theme.borderWidth

            Repeater {
                model: pane.fields

                delegate: Rectangle {
                    id: field

                    required property int index
                    required property var modelData

                    width: fieldColumn.width
                    height: Math.max(Theme.rowHeight,
                                     valueText.contentHeight + Theme.gapS * 2)
                    // The same barely-there banding the grid above uses, so a
                    // long list of members stays readable across the row.
                    color: index % 2 !== 0 ? Theme.rowStripe : "transparent"

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.gapM
                        anchors.rightMargin: Theme.gapM
                        anchors.topMargin: Theme.gapS
                        anchors.bottomMargin: Theme.gapS
                        spacing: Theme.gapM

                        Text {
                            id: memberName

                            Layout.preferredWidth: pane.nameWidth
                            Layout.alignment: Qt.AlignTop
                            text: field.modelData.name
                            font: Theme.microLabel
                            color: Theme.textSecondary
                            elide: Text.ElideRight

                            // A member name and a member type are both fixed
                            // columns here, and a compound is free to hold
                            // names and types longer than either.
                            HoverHandler { id: nameHover }

                            AppToolTip {
                                shown: memberName.truncated && nameHover.hovered
                                verbatim: true
                                text: memberName.text
                            }
                        }

                        Text {
                            id: memberType

                            Layout.preferredWidth: pane.typeWidth
                            Layout.alignment: Qt.AlignTop
                            text: field.modelData.type
                            font: Theme.readout
                            color: Theme.textDisabled
                            elide: Text.ElideRight

                            HoverHandler { id: typeHover }

                            AppToolTip {
                                shown: memberType.truncated && typeHover.hovered
                                verbatim: true
                                text: memberType.text
                            }
                        }

                        // Read-only but not inert: a value one cannot select
                        // is a value one cannot get out of the program.
                        TextEdit {
                            id: valueText

                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignTop
                            text: field.modelData.value
                            readOnly: true
                            selectByMouse: true
                            selectByKeyboard: true
                            textFormat: TextEdit.PlainText
                            wrapMode: TextEdit.Wrap
                            font: Theme.mono
                            color: Theme.textPrimary
                            selectionColor: Theme.accent
                            selectedTextColor: Theme.accentText
                        }
                    }
                }
            }
        }
    }

    // --- the same element, as JSON --------------------------------------
    TextPane {
        width: parent.width
        title: qsTr("json")
        meta: qsTr("%1 chars").arg(pane.json.length)
        text: pane.json
    }
}
