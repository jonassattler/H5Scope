// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import H5Scope.Backend

/// How the lines are drawn, and which of them. The series settings write
/// straight to the plot object rather than to the surface, because drawing
/// fewer lines means reading less of the file -- a question about the data
/// rather than about the frame around it.
///
/// Two of the rows are about the *source* rather than about the drawing, and a
/// custom plot tab switches both off. The x axis is one: a custom plot states
/// its x in its own data panel, because there it is one of three things and
/// not always three numbers. "One line per" is the other: a custom plot's
/// entries are already single lines, so there is no table left to read either
/// way round. Everything else -- the colours, the view, the drawing -- is the
/// same question about either plot and is shared rather than copied.
SettingsPanel {
    id: panel

    /// The PlotSurface these settings apply to.
    property var target
    readonly property var plot: panel.target ? panel.target.plot : null

    /// Whether the two source rows are shown. See the note above.
    property bool showXAxis: true
    property bool showOrientation: true

    title: qsTr("plot settings")

    // --- what to call it --------------------------------------------------
    // A title and two axis names, all three empty to begin with and all three
    // drawing nothing while they are. They are not for the plot on screen --
    // the slice bar above it already says what is being drawn and the legend
    // beside it says which line is which -- they are for the plot that leaves
    // here through "copy plot", where neither of those goes.
    //
    // Nothing is checked, because there is nothing a phrase can be wrong
    // about. FilterInput is the same box CustomEntryRow gives an alias, and
    // for the same reason: what is being typed is language rather than a value.
    SettingRow {
        label: qsTr("labels")

        FilterInput {
            id: titleField

            objectName: "plotTitleField"

            width: parent.width
            implicitHeight: Theme.smallControlHeight
            font: Theme.body
            text: panel.target ? panel.target.plotTitle : ""
            placeholderText: qsTr("plot title")
            enabled: !!panel.target
            onAccepted: panel.commitLabel(titleField, "plotTitle")
            onActiveFocusChanged: {
                if (!titleField.activeFocus)
                    panel.commitLabel(titleField, "plotTitle")
            }
        }

        FilterInput {
            id: xLabelField

            objectName: "plotXLabelField"

            width: parent.width
            implicitHeight: Theme.smallControlHeight
            font: Theme.body
            text: panel.target ? panel.target.xLabel : ""
            placeholderText: qsTr("x label")
            enabled: !!panel.target
            onAccepted: panel.commitLabel(xLabelField, "xLabel")
            onActiveFocusChanged: {
                if (!xLabelField.activeFocus)
                    panel.commitLabel(xLabelField, "xLabel")
            }
        }

        FilterInput {
            id: yLabelField

            objectName: "plotYLabelField"

            width: parent.width
            implicitHeight: Theme.smallControlHeight
            font: Theme.body
            text: panel.target ? panel.target.yLabel : ""
            placeholderText: qsTr("y label")
            enabled: !!panel.target
            onAccepted: panel.commitLabel(yLabelField, "yLabel")
            onActiveFocusChanged: {
                if (!yLabelField.activeFocus)
                    panel.commitLabel(yLabelField, "yLabel")
            }
        }
    }

    /// Write a label box back to the surface, and put the box's binding back.
    ///
    /// The binding matters more here than the write does: these three are
    /// per-dataset, so selecting another dataset restores its own title, and a
    /// box whose binding a typed character had discarded would go on showing
    /// the previous dataset's words over the new one's plot.
    function commitLabel(box, name) {
        if (!panel.target)
            return
        panel.target[name] = box.text
        box.text = Qt.binding(() => panel.target ? panel.target[name] : "")
    }

    // --- the x axis, as three numbers ------------------------------------
    // Start, step and stop, of which any two determine the third. They are the
    // x values of the data, not a window onto them: element i is drawn at
    // start + i * step. Locking is by use -- typing a value states it, a third
    // pushes out the oldest -- and whatever is left is the default, which is
    // 0 : 1 : len(data), the element's own index. A reader who never touches
    // this row never has to know the rule.
    SettingRow {
        label: qsTr("x axis")
        visible: panel.showXAxis

        RangeAxisSetting {
            width: parent.width
            target: panel.target
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
                    // The cell *is* the line here -- the strip is a picture of
                    // the cycle and not of a plot -- so which line it is and
                    // where it sits are the same number.
                    color: panel.target
                           ? panel.target.seriesColor(index, index, 24)
                           : Theme.accent
                }
            }
        }
    }

    SettingRow {
        label: qsTr("view")

        // Where the window is, in numbers. The wheel, the drag and the
        // right-button band all say this with the pointer; a reader who has a
        // range in mind -- the same band over two traces, or the one a paper
        // prints -- cannot ask a wheel for it.
        //
        // Also the readout for the band: the four boxes are bound to the view,
        // so a region just selected is reported here without a second path
        // between the two.
        PlotRangeSetting {
            objectName: "plottingRange"

            width: parent.width
            target: panel.target
        }

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

        // --- the picture, out of here -------------------------------------
        // What a reader does with a plot they have arranged is put it in
        // something else, and until this the only way out of this window was a
        // screenshot of it -- which takes the chrome, the rail and whatever
        // else is on screen along with the plot.
        //
        // The same thing Ctrl+C does with the pointer over the pane; this is
        // the discoverable half of the pair.
        AppToolButton {
            objectName: "copyPlotButton"

            width: parent.width
            text: qsTr("copy plot")
            size: "sm"
            enabled: panel.target ? panel.target.drawable : false
            onClicked: { if (panel.target) panel.target.copyImage() }

            AppToolTip {
                shown: parent.hovered
                text: qsTr("Put a picture of the plot on the clipboard. Ctrl+C "
                           + "does the same with the pointer over it.")
            }
        }

        // How it went. This application has no toast and no animation, so a
        // line that changes and then stays is the way it reports something
        // that happened a moment ago -- and a copy needs reporting, because it
        // succeeds by putting something somewhere the reader cannot see.
        Text {
            objectName: "copyPlotResult"

            width: parent.width
            visible: panel.copyResult !== ""
            text: panel.copyResult
            font: Theme.caption
            color: panel.copyFailed ? Theme.warning : Theme.textDisabled
            wrapMode: Text.WordWrap
        }
    }

    /// What the last copy did, and whether it was a complaint. Cleared when
    /// the plot changes underneath it, because "copied" is a fact about a
    /// picture and the picture has moved on.
    property string copyResult: ""
    property bool copyFailed: false

    Connections {
        target: ImageClipboard

        function onCopied() {
            panel.copyResult = qsTr("Copied to the clipboard.")
            panel.copyFailed = false
        }

        function onFailed(reason) {
            panel.copyResult = reason
            panel.copyFailed = true
        }
    }

    Connections {
        target: panel.plot

        function onChanged() { panel.copyResult = "" }
    }

    SettingRow {
        label: qsTr("drawing")

        // Four densities rather than on and off. A grid is a reading aid, and
        // how much of it a reader wants depends on what they are reading: the
        // shape of a trace wants the rules out of the way, counting a spike's
        // width off them wants more of them, and lining a picture up with
        // something else wants the steps stated. "On" answered only the first.
        //
        // The numbered ticks do not move with this -- see PlotFrame.gridMode.
        AppComboBox {
            objectName: "gridModeBox"

            width: parent.width
            model: panel.gridModeLabels
            selectedIndex: {
                const at = panel.gridModeKeys.indexOf(
                    panel.target ? panel.target.gridMode : "loose")
                return at < 0 ? 0 : at
            }
            onActivated: index => {
                if (panel.target)
                    panel.target.gridMode = panel.gridModeKeys[index]
            }
        }

        // Absent rather than disabled under the other three, which is the
        // stance this panel already takes on the map-range slider: a control
        // that cannot do anything is a control the reader has to work out the
        // rule for.
        Text {
            width: parent.width
            visible: panel.customGrid
            text: qsTr("a rule every, along x and along y, in the data's own units")
            font: Theme.caption
            color: Theme.textDisabled
            wrapMode: Text.WordWrap
        }

        RowLayout {
            width: parent.width
            spacing: Theme.gapS
            visible: panel.customGrid

            RealField {
                objectName: "gridStepXField"

                Layout.fillWidth: true
                value: panel.target ? panel.target.gridStepX : 0
                // Negative is not a narrower grid, it is a grid drawn
                // backwards; zero is "none" said in the wrong control. Both
                // come back as nothing ruled on that axis, which is what
                // PlotFrame does with a step it cannot use, so the box and the
                // picture agree.
                onCommitted: amount => {
                    if (panel.target)
                        panel.target.gridStepX = Math.max(0, amount)
                }
            }

            RealField {
                objectName: "gridStepYField"

                Layout.fillWidth: true
                value: panel.target ? panel.target.gridStepY : 0
                onCommitted: amount => {
                    if (panel.target)
                        panel.target.gridStepY = Math.max(0, amount)
                }
            }
        }

        AppCheckBox {
            text: qsTr("point markers")
            checked: panel.target ? panel.target.showMarkers : false
            onToggled: { if (panel.target) panel.target.showMarkers = checked }
        }

        /// Pointing at the plot reads the sample under the pointer, and says so
        /// in the bar below it. Off for a reader who wants the footer to hold
        /// still while they look at it.
        AppCheckBox {
            text: qsTr("cursor")
            checked: panel.target ? panel.target.showCursor : false
            onToggled: { if (panel.target) panel.target.showCursor = checked }
        }
    }

    // --- naming the lines inside the picture ------------------------------
    // The panel on the left of the plot already names them, and better: it
    // lists every line in the table, ticks them on and off, and scrolls. This
    // is not that. It is a caption drawn *on* the plot, and it is here because
    // a copied plot takes its children with it and takes nothing beside it --
    // six unnamed traces pasted into a document are six traces nobody can read.
    //
    // Off by default for that reason: on screen it is a second answer to a
    // question already answered, and it stands over part of the drawing.
    SettingRow {
        label: qsTr("legend")

        AppCheckBox {
            objectName: "legendOnPlotBox"

            text: qsTr("show legend on plot")
            checked: panel.target ? panel.target.legendOnPlot : false
            onToggled: { if (panel.target) panel.target.legendOnPlot = checked }
        }

        // The four corners, in reading order rather than in the order the
        // property's strings happen to sort in. Absent while the legend is
        // off, for the reason the custom grid steps are.
        ButtonGroup { id: corners }

        Column {
            spacing: Theme.gapXS
            visible: !!panel.target && panel.target.legendOnPlot

            Repeater {
                model: panel.cornerKeys

                delegate: AppRadioButton {
                    id: cornerButton

                    required property int index

                    text: panel.cornerLabels[cornerButton.index]
                    ButtonGroup.group: corners
                    onClicked: {
                        if (panel.target) {
                            panel.target.legendCorner =
                                panel.cornerKeys[cornerButton.index]
                        }
                    }

                    // The mark follows the setting rather than the click, for
                    // the reason the two colour-kind radios give above: a
                    // ButtonGroup writes `checked` imperatively, and an
                    // imperative write to a bound property discards the
                    // binding for good -- which here would leave the mark on
                    // the corner belonging to the dataset before this one.
                    Binding {
                        target: cornerButton
                        property: "checked"
                        value: !!panel.target
                               && panel.target.legendCorner
                                  === panel.cornerKeys[cornerButton.index]
                        restoreMode: Binding.RestoreBindingOrValue
                    }
                }
            }
        }
    }

    SettingRow {
        label: qsTr("one line per")
        visible: panel.showOrientation

        ButtonGroup { id: orientations }

        Row {
            spacing: Theme.gapM

            AppRadioButton {
                text: qsTr("row")
                ButtonGroup.group: orientations
                checked: (panel.showOrientation && panel.plot)
                         ? panel.plot.seriesFromRows : true
                onClicked: panel.plot.seriesFromRows = true
            }

            AppRadioButton {
                text: qsTr("column")
                ButtonGroup.group: orientations
                checked: (panel.showOrientation && panel.plot)
                         ? !panel.plot.seriesFromRows : false
                onClicked: panel.plot.seriesFromRows = false
            }
        }
    }

    // --- the grid's four densities ----------------------------------------
    /// Stable lists rather than expressions that build an array each time they
    /// run, for the reason the colour dropdown's note gives at length: a fresh
    /// array makes the ComboBox reset its index, which clears the binding
    /// under it and leaves the box naming a density the plot is not drawing.
    readonly property var gridModeKeys: ["none", "loose", "dense", "custom"]
    readonly property var gridModeLabels:
        [qsTr("none"), qsTr("loose"), qsTr("dense"), qsTr("custom")]

    readonly property bool customGrid:
        !!panel.target && panel.target.gridMode === "custom"

    /// The four corners a legend on the plot can take, in reading order.
    readonly property var cornerKeys:
        ["topLeft", "topRight", "bottomLeft", "bottomRight"]
    readonly property var cornerLabels: [qsTr("top left"), qsTr("top right"),
                                         qsTr("bottom left"), qsTr("bottom right")]

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
