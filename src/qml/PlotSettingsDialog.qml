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

    /// A centimetre to the hundredth of one, which is as fine as a figure
    /// for a page is ever stated and as fine as a whole dpi can answer.
    function round2(amount) {
        return Math.round(amount * 100) / 100
    }

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
        width: Theme.formDialogWidth
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

            /// The density and the figure it comes to, as three boxes over
            /// one number.
            ///
            /// The pixel count is settled in the row above and no density may
            /// move it, so at a stated size a density *is* a physical size:
            /// one degree of freedom, three ways of saying it. Write any one
            /// of the three and the other two follow. That replaced a readout
            /// of the inches it came to, which said the same thing and could
            /// not be typed in -- and a reader preparing a figure for a page
            /// has the size, not the density, in front of them: journals ask
            /// for 8.5 cm or 17.8, and working back to the dpi that gives it
            /// was arithmetic this dialog was leaving to them.
            ///
            /// A Flow and not a Row, because three labelled boxes are wider
            /// than this dialog at some type sizes and a line that has to
            /// wrap should wrap between two of them rather than clip the
            /// third. Each label is glued to its own box by a Row inside it.
            Flow {
                width: parent.width
                spacing: Theme.gapS
                visible: AppController.plotExportCustomDpi

                Row {
                    spacing: Theme.gapS

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("dpi")
                        font: Theme.micro
                        color: Theme.textSecondary
                    }

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
                }

                /// The figure's two sides on paper, in centimetres.
                ///
                /// Shown only where this dialog knows the pixel count: the
                /// pane's own size is whatever the window is, and that is not
                /// this dialog's to report -- nor, therefore, to let anybody
                /// type a density into the back of.
                ///
                /// Both boxes write the *density* and neither touches the
                /// pixels. So the two sides cannot be set independently: they
                /// are one figure at one density, and its shape was settled
                /// by the pixel count above. Typing a width sets the density
                /// that gives it and the height follows, which is the whole
                /// of what "changing one changes the other two" means here.
                Row {
                    spacing: Theme.gapS
                    visible: AppController.plotExportCustomSize

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("width (cm)")
                        font: Theme.micro
                        color: Theme.textSecondary
                    }

                    RealField {
                        objectName: "exportWidthCmField"

                        // Both properties named, so the binding depends on
                        // both: a call creates no dependency on what it
                        // reads, and a box that did not name the density
                        // would go on showing the size the one before it
                        // gave. Rounded to the hundredth it is written at --
                        // the field prints six significant figures, and
                        // 15.9934 cm is not a number anybody typed.
                        value: control.round2(
                            AppController.plotExportCentimetres(
                                AppController.plotExportWidth,
                                AppController.plotExportDpi))
                        onCommitted: amount => AppController.plotExportDpi =
                            AppController.plotExportDpiFor(
                                AppController.plotExportWidth, amount)

                        AppToolTip {
                            shown: parent.hovered
                            text: qsTr("How wide the figure lands on the "
                                       + "page. It sets the density rather "
                                       + "than the pixels — those are the "
                                       + "ones asked for above, at every "
                                       + "size.")
                        }
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("height (cm)")
                        font: Theme.micro
                        color: Theme.textSecondary
                    }

                    RealField {
                        objectName: "exportHeightCmField"

                        value: control.round2(
                            AppController.plotExportCentimetres(
                                AppController.plotExportHeight,
                                AppController.plotExportDpi))
                        onCommitted: amount => AppController.plotExportDpi =
                            AppController.plotExportDpiFor(
                                AppController.plotExportHeight, amount)

                        AppToolTip {
                            shown: parent.hovered
                            text: qsTr("How tall it lands. The same density "
                                       + "as the width, because the shape of "
                                       + "the figure is the pixel count "
                                       + "asked for above.")
                        }
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
