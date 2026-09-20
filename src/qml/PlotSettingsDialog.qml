// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import QtQuick.Controls.Basic
import H5Scope.Backend

/// Settings -> Plot Settings. Built as the system's Panel, like the About and
/// error dialogs it sits beside in Main.qml: hairline border, 2px radius, mono
/// uppercase header bar, and a scrim rather than a blur behind it.
///
/// A dialog rather than more rows under the Settings drawer, and the reason is
/// that a drawer can only offer rows and bullets. Two of these settings are a
/// number the reader types, and a menu with a text field in it is a menu
/// pretending to be a form. It is modal for the ordinary reason: it asks about
/// nothing on screen, so there is nothing behind it to consult while answering.
///
/// **What a setting means is a tooltip and not a paragraph under it.** This is
/// a form and it is going to grow; a sentence of explanation under every
/// control turns a list of six settings into a page of prose with controls in
/// it, and the reader who already knows what publication mode is has to read
/// past all of it to reach the next tick. The rail panels are built the same
/// way for the same reason -- see PlotSettingsPanel, where every control that
/// needs explaining carries an AppToolTip and the panel stays a list.
///
/// Rows are SettingRow, which is the shape the rail's panels established, so a
/// setting added here lands in the same grid as the ones already in it.
///
/// Everything in it is a property of AppController and not of any plot. There
/// is one Plot tab and any number of custom ones, and what a picture out of
/// this program looks like is not a property of the tab the reader happened to
/// press the button on.
Dialog {
    id: control

    anchors.centerIn: parent
    modal: true
    padding: Theme.gapXL

    Overlay.modal: Rectangle {
        color: Theme.scrim
    }

    background: Rectangle {
        color: Theme.surfaceRaised
        radius: Theme.radiusS
        border.width: Theme.borderWidth
        border.color: Theme.borderStrong
    }

    header: Item {
        implicitHeight: Theme.treeHeaderHeight

        Text {
            anchors.left: parent.left
            anchors.leftMargin: Theme.gapXL
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("plot settings")
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

    contentItem: Column {
        width: Theme.panelMinWidth
        spacing: Theme.gapL

        SettingRow {
            label: qsTr("export")

            AppCheckBox {
                objectName: "publicationModeBox"

                text: qsTr("publication mode")
                checked: AppController.plotExportPublication
                onToggled: AppController.plotExportPublication = checked

                AppToolTip {
                    shown: parent.hovered
                    text: qsTr("Draw the copy for print: black strokes, the "
                               + "light theme's chrome, and no ground at all, "
                               + "so it stands on the page it is pasted into. "
                               + "What is on screen does not change.")
                }
            }

            AppCheckBox {
                objectName: "includeCursorBox"

                text: qsTr("include cursor")
                checked: AppController.plotExportCursor
                onToggled: AppController.plotExportCursor = checked

                AppToolTip {
                    shown: parent.hovered
                    text: qsTr("Put the crosshair, and the sample it has "
                               + "snapped to, in the picture — for one whose "
                               + "point is that reading.")
                }
            }
        }

        SettingRow {
            label: qsTr("size")

            // Two radios and not a checkbox, because the reader is choosing
            // between two sizes rather than turning one on: there is no state
            // in which the picture has no size at all.
            //
            // The marks follow the setting rather than the click, for the
            // reason AppComboBox gives at `selectedIndex`: a ButtonGroup
            // writes `checked` imperatively, and an imperative write to a
            // bound property discards the binding for good.
            ButtonGroup { id: sizes }

            AppRadioButton {
                id: windowSize
                objectName: "windowSizeRadio"

                ButtonGroup.group: sizes
                text: qsTr("same as window")
                onToggled: { if (checked) AppController.plotExportCustomSize = false }

                AppToolTip {
                    shown: parent.hovered
                    text: qsTr("The pane as it stands, at this display's own "
                               + "resolution.")
                }
            }

            AppRadioButton {
                id: customSize
                objectName: "customSizeRadio"

                ButtonGroup.group: sizes
                text: qsTr("custom")
                onToggled: { if (checked) AppController.plotExportCustomSize = true }

                AppToolTip {
                    shown: parent.hovered
                    text: qsTr("Draw the picture again at a size in pixels — "
                               + "its own ticks, its own labels — over exactly "
                               + "the x and y range on screen.")
                }
            }

            Binding {
                target: windowSize
                property: "checked"
                value: !AppController.plotExportCustomSize
                restoreMode: Binding.RestoreBindingOrValue
            }

            Binding {
                target: customSize
                property: "checked"
                value: AppController.plotExportCustomSize
                restoreMode: Binding.RestoreBindingOrValue
            }

            Row {
                spacing: Theme.gapS
                // Absent rather than disabled: a pair of boxes greyed out
                // beside the radio that would enable them is two controls
                // saying the same thing, and this application says it once.
                // Same rule as the table panel's width slider and the image
                // panel's channel indices.
                visible: AppController.plotExportCustomSize

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("x")
                    font: Theme.micro
                    color: Theme.textSecondary
                }

                NumberField {
                    objectName: "exportWidthField"

                    value: AppController.plotExportWidth
                    from: AppController.minExportPixels
                    to: AppController.maxExportPixels
                    onCommitted: amount => AppController.plotExportWidth = amount

                    AppToolTip {
                        shown: parent.hovered
                        text: qsTr("Width of the picture, in pixels.")
                    }
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("y")
                    font: Theme.micro
                    color: Theme.textSecondary
                }

                NumberField {
                    objectName: "exportHeightField"

                    value: AppController.plotExportHeight
                    from: AppController.minExportPixels
                    to: AppController.maxExportPixels
                    onCommitted: amount => AppController.plotExportHeight = amount

                    AppToolTip {
                        shown: parent.hovered
                        text: qsTr("Height of the picture, in pixels.")
                    }
                }
            }
        }
    }

    footer: Item {
        implicitHeight: Theme.controlHeight + Theme.gapXL

        AppToolButton {
            anchors.right: parent.right
            anchors.rightMargin: Theme.gapXL
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("done")
            variant: "primary"
            size: "lg"
            onClicked: control.close()
        }
    }
}
