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
/// that a drawer can only offer rows and bullets. Three of these settings are a
/// number the reader types, and a menu with a text field in it is a menu
/// pretending to be a form. It is modal for the ordinary reason: it asks about
/// nothing on screen, so there is nothing behind it to consult while answering.
///
/// Size and density are two rows because they are two questions, and both
/// change what the picture looks like. The size is how many pixels come back.
/// The density is how big those pixels are -- and since the type, the rules
/// and the gutters are all a fixed number of logical units, that is what
/// decides how large they are *against* the figure. A 1920 by 1080 export at
/// 300 dpi is a 6.4 by 3.6 inch figure with type at its true point size; the
/// same 1920 by 1080 at 96 dpi is twenty inches across with the same type a
/// third the size on it.
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
                    text: qsTr("Draw the copy for print: the light theme's "
                               + "colours, chrome and line palette both, and "
                               + "no ground at all, so it stands on the page "
                               + "it is pasted into. What is on screen does "
                               + "not change.")
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
                    text: qsTr("The pane as it stands, composed exactly as "
                               + "it is on screen.")
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
                    text: qsTr("Draw the picture again at a size of your own "
                               + "— its own ticks, its own labels — over "
                               + "exactly the x and y range on screen. In "
                               + "pixels, and exactly those pixels whatever "
                               + "the density below says.")
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

        // A second pair of radios rather than one more tick, and for the
        // reason the pair above is a pair: the reader is choosing between two
        // densities, not turning one on. A picture always has one.
        SettingRow {
            label: qsTr("dots per inch")

            ButtonGroup { id: resolutions }

            AppRadioButton {
                id: displayScale
                objectName: "displayScaleRadio"

                ButtonGroup.group: resolutions
                text: qsTr("this display")
                onToggled: { if (checked) AppController.plotExportCustomDpi = false }

                AppToolTip {
                    shown: parent.hovered
                    text: qsTr("Pixels the size this screen draws them, so "
                               + "the type on the picture is the size it is "
                               + "on screen. What the plot has always been "
                               + "copied at.")
                }
            }

            AppRadioButton {
                id: chosenDpi
                objectName: "customDpiRadio"

                ButtonGroup.group: resolutions
                text: qsTr("dots per inch")
                onToggled: { if (checked) AppController.plotExportCustomDpi = true }

                AppToolTip {
                    shown: parent.hovered
                    text: qsTr("Say how big the pixels are. The type, the "
                               + "rules and the ticks are set in points, so a "
                               + "higher figure makes them larger against the "
                               + "picture — the same number of pixels, over "
                               + "fewer inches. The image is tagged with it "
                               + "too, so it lands on the page at that size.")
                }
            }

            Binding {
                target: displayScale
                property: "checked"
                value: !AppController.plotExportCustomDpi
                restoreMode: Binding.RestoreBindingOrValue
            }

            Binding {
                target: chosenDpi
                property: "checked"
                value: AppController.plotExportCustomDpi
                restoreMode: Binding.RestoreBindingOrValue
            }

            Row {
                spacing: Theme.gapS
                visible: AppController.plotExportCustomDpi

                NumberField {
                    objectName: "exportDpiField"

                    value: AppController.plotExportDpi
                    from: AppController.minExportDpi
                    to: AppController.maxExportDpi
                    onCommitted: amount => AppController.plotExportDpi = amount

                    AppToolTip {
                        shown: parent.hovered
                        text: qsTr("300 is what most journals ask for; 600 "
                                   + "is line art. Below 96 the type reads "
                                   + "smaller than it does on screen.")
                    }
                }

                /// What the two rows come to: the figure's size on paper.
                ///
                /// The inches and not the pixels, because the pixels are what
                /// the reader typed one row up and printing them back would
                /// say nothing. This is the number the density actually buys
                /// -- and it is the one that says whether the type will read,
                /// since the type is a fixed share of it.
                ///
                /// Shown only where this dialog knows the pixel count. The
                /// pane's own size is whatever the window is, and that is not
                /// this dialog's to report.
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: AppController.plotExportCustomSize
                    text: {
                        // Named so that this binding depends on them, the way
                        // CustomEntryRow's swatch names its cycle: a call
                        // creates no dependency on what it reads, so a reader
                        // typing a new density would go on reading the size
                        // the one before it gave.
                        const wide = AppController.plotExportWidth
                        const tall = AppController.plotExportHeight
                        const dpi = AppController.plotExportDpi
                        if (!AppController.plotExportCustomDpi || dpi <= 0)
                            return ""
                        const across = (wide / dpi).toFixed(2)
                        const down = (tall / dpi).toFixed(2)
                        return qsTr("→ %1 × %2 in").arg(across).arg(down)
                    }
                    font: Theme.readout
                    color: Theme.textSecondary
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
