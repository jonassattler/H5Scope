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

    // --- which kind of cycle, then which one ------------------------------
    SettingRow {
        label: qsTr("colours")

        // The kind first, because it is the real decision and the two kinds
        // answer different questions: a palette says *which line* a stroke is,
        // a map says *how far along* it sits. They used to share one flat
        // dropdown, which offered no way to tell -- with the list closed,
        // nothing said that `safe` and `viridis` were different kinds of
        // thing, and the reader had to pick one to find out.
        //
        // "same" is a map here, filed with `range` and the ramps. One colour
        // for every line is the degenerate map -- `range` with both ends the
        // same -- and it is certainly not a palette: the one thing every
        // palette does is give each line a colour of its own.
        ButtonGroup { id: colorKinds }

        // Stacked, where the radio pair at the foot of this panel sits in a
        // row. Those two are "row" and "column"; these are "categorical" and
        // "continuous", which in the system's label face are 110px and 100px
        // of uppercase, and two of them side by side overrun a 212px rail and
        // cut the second word off. The rail is the fixed quantity here.
        Column {
            spacing: Theme.gapXS

            AppRadioButton {
                id: categoricalKind

                text: qsTr("categorical")
                ButtonGroup.group: colorKinds
                onClicked: panel.chooseKind(true)
            }

            AppRadioButton {
                id: continuousKind

                text: qsTr("continuous")
                ButtonGroup.group: colorKinds
                onClicked: panel.chooseKind(false)
            }
        }

        // The marks follow the mode rather than the click, for the reason
        // AppComboBox gives at `selectedIndex`: a ButtonGroup writes `checked`
        // imperatively, and an imperative write to a bound property discards
        // the binding for good. That is not hypothetical here -- `colorMode`
        // is per-dataset, so selecting another dataset restores its own cycle,
        // and a pair of radios with their bindings gone would go on naming the
        // kind belonging to the dataset before it.
        Binding {
            target: categoricalKind
            property: "checked"
            value: panel.categorical
            restoreMode: Binding.RestoreBindingOrValue
        }

        Binding {
            target: continuousKind
            property: "checked"
            value: !panel.categorical
            restoreMode: Binding.RestoreBindingOrValue
        }

        // Which cycle, within the kind the radios above settled. The model is
        // one kind's list, so nothing the reader can reach in here changes the
        // answer to the question those radios asked.
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

        // What the chosen cycle actually does. The radios above name the kind,
        // so this is left with the part they cannot say: for a palette, how
        // many lines it separates before it starts over, which is the number
        // the reader is really asking about.
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
                return qsTr("The lines take even shares of the map.")
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

    // --- the two kinds, and what each one offers --------------------------
    /// The cycles of each kind, as four lists that stay put.
    ///
    /// Stable rather than filtered on demand, for the reason the note on the
    /// dropdown gives: a model expression that builds a new array every time
    /// it runs makes the ComboBox reset its index. `colorModeLabels` below
    /// picks between two of these by reference, so the array the model sees
    /// changes only when the kind does.
    ///
    /// The palettes come from Theme rather than being listed again here, so
    /// adding one to the design system adds it to this panel.
    readonly property var paletteKeys: Theme.categoricalPaletteNames
    /// Palettes and ramps carry their own names; only the two the reader
    /// builds themselves have words that want translating.
    readonly property var paletteLabels: Theme.categoricalPaletteNames
    readonly property var mapKeys: ["same", "range"].concat(Theme.colorRampNames)
    readonly property var mapLabels:
        [qsTr("same"), qsTr("range")].concat(Theme.colorRampNames)

    /// Which kind the plot is drawing, which is read off the mode rather than
    /// stored beside it. One authority: `colorMode` is the setting, it is what
    /// DatasetMemory carries per dataset, and a second copy saying which kind
    /// it belongs to is a second thing that can be wrong.
    readonly property bool categorical:
        panel.target ? panel.isPalette(panel.target.colorMode) : true

    /// What the dropdown is showing: the chosen kind's list, and only it.
    readonly property var colorModeKeys: panel.categorical ? panel.paletteKeys
                                                           : panel.mapKeys
    readonly property var colorModeLabels: panel.categorical ? panel.paletteLabels
                                                             : panel.mapLabels

    /// The last cycle chosen in each kind, so that going to the other kind and
    /// back returns the reader to what they had rather than to the top of a
    /// list. Panel state rather than the plot's: it is about the reader's way
    /// round the control, not about how a dataset is drawn.
    property string lastPalette: Theme.categoricalPaletteNames[0]
    /// A ramp rather than "same", which is first in the list. A reader who has
    /// just asked for `continuous` and been handed one flat colour has been
    /// answered with the opposite of what they pressed; "same" is one line
    /// down the list for whoever wants it.
    property string lastMap: "viridis"

    /// Whether `mode` names a palette rather than a map.
    function isPalette(mode) {
        return Theme.categoricalPalettes[mode] !== undefined
    }

    /// Switch kinds, landing on whatever was last used in the one asked for.
    function chooseKind(wantPalette) {
        if (panel.target && wantPalette !== panel.categorical)
            panel.target.colorMode = wantPalette ? panel.lastPalette
                                                 : panel.lastMap
    }

    /// Note the current mode as its kind's most recent.
    ///
    /// Driven off the mode changing rather than off the dropdown being used,
    /// so that a cycle arriving from anywhere else -- DatasetMemory restoring
    /// one when the reader selects another dataset -- is remembered too.
    function rememberMode() {
        if (!panel.target)
            return
        const mode = panel.target.colorMode
        if (panel.isPalette(mode))
            panel.lastPalette = mode
        else if (panel.mapKeys.indexOf(mode) >= 0)
            panel.lastMap = mode
    }

    Connections {
        target: panel.target
        function onColorModeChanged() { panel.rememberMode() }
    }

    Component.onCompleted: panel.rememberMode()
}
