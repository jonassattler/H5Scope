// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import H5Scope.Backend

/// How the lines are drawn, and which of them. The series settings write
/// straight to AppController.datasetPlot, because drawing fewer lines means
/// reading less of the file -- a question about the data rather than about the
/// frame around it.
SettingsPanel {
    id: panel

    /// The PlotSurface these settings apply to.
    property var target
    readonly property var plot: AppController.datasetPlot

    title: qsTr("plot settings")

    // --- the x axis, as three numbers ------------------------------------
    // Start, step and stop, of which any two determine the third. They are the
    // x values of the data, not a window onto them: element i is drawn at
    // start + i * step. Locking is by use -- typing a value states it, a third
    // pushes out the oldest -- and whatever is left is the default, which is
    // 0 : 1 : len(data), the element's own index. A reader who never touches
    // this row never has to know the rule.
    SettingRow {
        label: qsTr("x axis")

        Repeater {
            model: [
                { key: "start", label: qsTr("start") },
                { key: "step",  label: qsTr("step") },
                { key: "stop",  label: qsTr("stop") }
            ]

            delegate: RowLayout {
                id: axisRow

                required property var modelData

                readonly property bool pinned:
                    panel.target ? panel.target.locked(modelData.key) : false
                /// What the axis is actually using for this one, whether it was
                /// typed or worked out. A computed box shows its result rather
                /// than sitting blank, so the reader can see what locking two
                /// of them did to the third.
                readonly property real shown: {
                    if (!panel.target)
                        return 0
                    const resolved = panel.target.resolved
                    return resolved ? resolved[modelData.key] : 0
                }

                width: parent.width
                spacing: Theme.gapS

                Text {
                    Layout.preferredWidth: Theme.s10
                    text: axisRow.modelData.label
                    font: Theme.microLabel
                    color: axisRow.pinned ? Theme.textPrimary : Theme.textDisabled
                    verticalAlignment: Text.AlignVCenter
                }

                RealField {
                    Layout.fillWidth: true
                    value: axisRow.shown
                    // A value the reader typed is a value they meant, so
                    // entering one is what pins it. Asking them to tick a box
                    // first would be asking them to say the same thing twice.
                    onCommitted: amount => {
                        if (!panel.target)
                            return
                        panel.target[axisRow.modelData.key === "start" ? "rangeStart"
                                   : axisRow.modelData.key === "step"  ? "rangeStep"
                                                                       : "rangeStop"] = amount
                        panel.target.lock(axisRow.modelData.key)
                    }
                }

                AppCheckBox {
                    text: qsTr("lock")
                    checked: axisRow.pinned
                    onToggled: {
                        if (!panel.target)
                            return
                        // Pinning from the box takes the value on screen with
                        // it, or the axis would jump to whatever was last in
                        // the property behind an unpinned box.
                        if (checked) {
                            panel.target[axisRow.modelData.key === "start" ? "rangeStart"
                                       : axisRow.modelData.key === "step"  ? "rangeStep"
                                                                           : "rangeStop"] =
                                axisRow.shown
                        }
                        panel.target.setLocked(axisRow.modelData.key, checked)
                    }
                }
            }
        }

        // Two is the whole of the rule, so it is stated once, here, rather
        // than left for the reader to infer from a box unticking itself. The
        // default is stated too, because "0 : 1 : len(data)" is the sentence
        // that says these numbers are the x values and not a viewport.
        Text {
            width: parent.width
            text: {
                if (!panel.target)
                    return ""
                if (!panel.target.rangeValid)
                    return qsTr("A step above zero and a stop above the start; showing 0 : 1 : %1 meanwhile.")
                           .arg(panel.target.dataLength)
                if (panel.target.locks.length === 0)
                    return qsTr("x = start + i x step, over %1 elements. Edit two; the third follows.")
                           .arg(panel.target.dataLength)
                return qsTr("Editing a third releases the one edited longest ago.")
            }
            font: Theme.caption
            color: (panel.target && !panel.target.rangeValid) ? Theme.warning
                                                              : Theme.textDisabled
            wrapMode: Text.WordWrap
        }

        AppToolButton {
            width: parent.width
            text: qsTr("back to 0 : 1 : len(data)")
            size: "sm"
            enabled: panel.target ? panel.target.locks.length > 0 : false
            onClicked: { if (panel.target) panel.target.locks = [] }
        }
    }

    // --- one colour, two, or a named ramp ---------------------------------
    SettingRow {
        label: qsTr("colours")

        AppComboBox {
            width: parent.width
            // A stable list, not `colorModes.map(...)`: a model expression that
            // builds a new array every time it runs makes the ComboBox reset
            // its index, which quietly clears the binding below and leaves the
            // box naming a mode the plot is not drawing.
            model: panel.colorModeLabels
            selectedIndex: {
                const at = panel.colorModeKeys.indexOf(
                    panel.target ? panel.target.colorMode : "same")
                return at < 0 ? 0 : at
            }
            onActivated: index => {
                if (panel.target)
                    panel.target.colorMode = panel.colorModeKeys[index]
            }
        }

        // Which of the two kinds the choice is, stated rather than left to be
        // inferred from a name. The two behave differently in ways the reader
        // can see -- a palette holds a line's colour still when its neighbours
        // are unticked, a map does not -- and the count is the number they
        // actually want: how many lines they can tell apart before two of them
        // are drawn alike.
        Text {
            width: parent.width
            text: {
                if (!panel.target)
                    return ""
                const palette = Theme.categoricalPalettes[panel.target.colorMode]
                if (palette)
                    return qsTr("%1 colours, each its own; past that the cycle starts over.")
                           .arg(palette.length)
                if (panel.target.colorMode === "same")
                    return qsTr("One colour for every line.")
                return qsTr("A continuous map; the lines take even shares of it.")
            }
            font: Theme.caption
            color: Theme.textDisabled
            wrapMode: Text.WordWrap
        }

        // The two cycles the reader builds themselves: "same" is one colour
        // for every line, "range" fades between two. A palette and a named
        // ramp are both given, so neither swatch is shown for them.
        RowLayout {
            width: parent.width
            spacing: Theme.gapS
            visible: !!panel.target && (panel.target.colorMode === "same"
                                        || panel.target.colorMode === "range")

            ColorSwatchButton {
                label: panel.target && panel.target.colorMode === "range"
                       ? qsTr("first line") : qsTr("every line")
                value: panel.target
                       ? (panel.target.colorMode === "range"
                          ? panel.target.colorRangeFrom : panel.target.colorSingle)
                       : Theme.accent
                onPicked: chosen => {
                    if (!panel.target)
                        return
                    if (panel.target.colorMode === "range")
                        panel.target.colorRangeFrom = chosen
                    else
                        panel.target.colorSingle = chosen
                }
            }

            ColorSwatchButton {
                label: qsTr("last line")
                visible: !!panel.target && panel.target.colorMode === "range"
                value: panel.target ? panel.target.colorRangeTo : Theme.info
                onPicked: chosen => {
                    if (panel.target) panel.target.colorRangeTo = chosen
                }
            }

            Item { Layout.fillWidth: true }
        }

        AppCheckBox {
            text: qsTr("reverse")
            // Which end of a ramp is the dark one is a property of the ramp,
            // not of the data; the reader is the one who knows which way round
            // they want to read it. On a palette it turns the order of the
            // entries around, so a plot of four lines can be given the other
            // end of the cycle when the near end clashes with something.
            checked: panel.target ? panel.target.colorsReversed : false
            onToggled: { if (panel.target) panel.target.colorsReversed = checked }
        }

        // The map's own range, which is the same control the image and the
        // table put over their values -- except that a plot colours by *which
        // line* a stroke is rather than by how big its numbers are, so the
        // band runs over the map itself and not over the data.
        //
        // It earns its place on a perceptual ramp. Those run dark to light,
        // and this plot's ground is true black, so the first lines of a
        // viridis start out very nearly invisible; pulling the near handle up
        // takes that end of the map back. One colour has no map to slice and a
        // palette has no continuum to take a slice out of, so for both of
        // those the row is absent rather than disabled.
        Text {
            width: parent.width
            visible: !!panel.target && panel.target.colorMode !== "same"
                     && !panel.isPalette(panel.target.colorMode)
            text: qsTr("map range %1 … %2")
                  .arg(panel.target ? panel.target.colorFrom.toFixed(2) : 0)
                  .arg(panel.target ? panel.target.colorTo.toFixed(2) : 1)
            font: Theme.readout
            color: Theme.textDisabled
        }

        RealRangeSlider {
            width: parent.width
            visible: !!panel.target && panel.target.colorMode !== "same"
                     && !panel.isPalette(panel.target.colorMode)
            from: 0.0
            to: 1.0
            firstValue: panel.target ? panel.target.colorFrom : 0.0
            secondValue: panel.target ? panel.target.colorTo : 1.0
            onMovedTo: (low, high) => {
                if (!panel.target)
                    return
                panel.target.colorFrom = low
                panel.target.colorTo = high
            }
        }

        // What the cycle looks like across the lines actually being drawn.
        // A ramp is a hard thing to imagine from its name, and so is the
        // length of a palette -- twenty-four cells is enough that every
        // palette here visibly starts over inside the strip, which is the
        // sentence above drawn rather than written.
        Row {
            width: parent.width
            height: Theme.gapL
            spacing: 0

            Repeater {
                model: 24

                delegate: Rectangle {
                    required property int index

                    width: parent.width / 24
                    height: parent.height
                    color: panel.target ? panel.target.seriesColor(index, 24)
                                        : Theme.accent
                }
            }
        }
    }

    SettingRow {
        label: qsTr("view")

        // The wheel zooms about the pointer and a drag pans; this is the way
        // back for a reader who would rather press something than remember
        // that a double-click does the same.
        AppToolButton {
            width: parent.width
            text: qsTr("reset zoom")
            size: "sm"
            enabled: panel.target ? panel.target.zoomed : false
            onClicked: { if (panel.target) panel.target.resetView() }
        }

        AppToolButton {
            width: parent.width
            text: qsTr("clear highlight")
            size: "sm"
            enabled: panel.target ? panel.target.highlighted >= 0 : false
            onClicked: { if (panel.target) panel.target.highlighted = -1 }
        }
    }

    SettingRow {
        label: qsTr("drawing")

        AppCheckBox {
            text: qsTr("grid lines")
            checked: panel.target ? panel.target.showGrid : false
            onToggled: { if (panel.target) panel.target.showGrid = checked }
        }

        AppCheckBox {
            text: qsTr("point markers")
            checked: panel.target ? panel.target.showMarkers : false
            onToggled: { if (panel.target) panel.target.showMarkers = checked }
        }
    }

    SettingRow {
        label: qsTr("one line per")

        ButtonGroup { id: orientations }

        Row {
            spacing: Theme.gapM

            AppRadioButton {
                text: qsTr("row")
                ButtonGroup.group: orientations
                checked: panel.plot.seriesFromRows
                onClicked: panel.plot.seriesFromRows = true
            }

            AppRadioButton {
                text: qsTr("column")
                ButtonGroup.group: orientations
                checked: !panel.plot.seriesFromRows
                onClicked: panel.plot.seriesFromRows = false
            }
        }
    }

    /// The cycles on offer, as two lists that stay put.
    ///
    /// Theme's categorical palettes lead, because one of them is what a plot
    /// opens on and because they are the answer to the question the reader is
    /// usually asking -- which line is which. Then the two the reader builds
    /// themselves: "same" is one colour for every line, "range" fades between
    /// two. Then Theme's named ramps, which carry their own names.
    readonly property var colorModeKeys:
        Theme.categoricalPaletteNames.concat(["same", "range"],
                                             Theme.colorRampNames)
    readonly property var colorModeLabels:
        Theme.categoricalPaletteNames.concat([qsTr("same"), qsTr("range")],
                                             Theme.colorRampNames)

    /// Whether `mode` names a palette rather than a map. Asked of Theme rather
    /// than kept as a list here, so adding a palette to the design system adds
    /// it to this panel and nothing has to be kept in step by hand.
    function isPalette(mode) {
        return Theme.categoricalPalettes[mode] !== undefined
    }
}
