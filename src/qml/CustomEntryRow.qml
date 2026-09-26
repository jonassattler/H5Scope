// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import QtQuick.Layouts
import H5Scope.Backend

/// One line of a custom plot, in the data settings rail.
///
///     +--------------------------------------------------+
///     | [x] SLICE   [ /series/half[:]              ]  [x] |
///     |     PIPELINE [ ] enable postprocessing           |
///     |     ALIAS   [ morning                      ]     |
///     |     COLOUR  [#]  clear                           |
///     |     AXIS    [x] separate y-axis  [ ] exclude ... |
///     |     SCALING ( ) align   (o) stretch              |
///     +--------------------------------------------------+
///
/// A card apiece, like the saved views above them, and for the same reason:
/// three of these stacked in a rail read as one paragraph with controls
/// scattered through it unless something says where one stops and the next
/// begins. An entry is a thing -- a line on the plot, with a name and a place
/// to sit -- and a bordered ground says that.
///
/// With postprocessing on, SLICE reads DATA and its box takes a whole
/// pipeline -- a postproc::Script, a step to a line -- in the same box the
/// postprocessing panel's text mode uses, checked by the same function. It is
/// not just a slice any more, and the label says so.
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
    /// The surface it is drawn on, so that the swatch below can show what the
    /// line actually looks like and not only what the reader has said about it.
    property var surface

    // The roles, handed down rather than required here: a required property
    // cannot also be one the component already declares, and the delegate that
    // wraps this is what takes them off the model.
    property string expression: ""
    property string alias: ""
    property string error: ""
    property int scaling: CustomPlot.Align
    property bool scalable: false
    property bool drawn: true
    /// The colour the reader gave this line, or `undefined` where they have
    /// not. Undefined and not a colour, because "none" has to be a different
    /// answer from "transparent"; see CustomPlot::seriesOverride.
    property var colour: undefined
    /// Whether the reader asked for this line to have a y axis of its own,
    /// and for that axis to stay put while the plot zooms. Requests: see
    /// CustomPlot::seriesAxis for when the first is in force.
    property bool separateAxis: false
    property bool axisFixed: false
    /// Whether the line is a pipeline rather than a slice. See
    /// CustomPlot::setPostprocess.
    property bool postprocess: false
    /// Whether there is a second line for this one to be separate from. A
    /// plot of one line has one axis, whatever this card's box says.
    readonly property bool separable: row.plot ? row.plot.seriesCount > 1 : false

    /// What this line is drawn in: the colour the reader gave it, or the
    /// cycle's answer for it where they have given none.
    ///
    /// Asked of the surface rather than worked out here, for the reason the
    /// legend's swatch is: a colour this file decided for itself would be a
    /// second rule about which line is which, and the swatch would be wrong
    /// the moment the two disagreed.
    readonly property color drawnColour: {
        if (row.colour !== undefined && row.colour !== null)
            return row.colour
        if (!row.surface || !row.plot)
            return Theme.accent
        // Named so that this binding depends on them. seriesColor() is a
        // function, and a call creates no dependency on what it reads, so a
        // reader changing the cycle would leave the swatch showing the colour
        // the line used to be.
        const cycle = row.surface.colorMode
        const reversed = row.surface.colorsReversed
        const drawn = row.plot.drawnSeries
        const at = drawn.indexOf(row.rowIndex)
        return row.surface.seriesColor(row.rowIndex, Math.max(at, 0),
                                       drawn.length)
    }

    /// Why what is *in the box* will not read, as opposed to why the applied
    /// line did not. Two channels, as the pipeline has: one about the text and
    /// one about the last read.
    property string problem: ""
    /// What is in whichever box is showing.
    readonly property string written: row.postprocess ? scriptBox.text : box.text
    /// Whether the reader is in whichever box is showing.
    readonly property bool editing: row.postprocess ? scriptBox.editing : box.activeFocus
    readonly property bool pending: row.written.trim() !== row.expression
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

    /// What could be written next, for the box below. Refreshed on every
    /// keystroke and again whenever the file answers something the completer
    /// was waiting on -- the tree is lazy, so a group nobody has opened is
    /// asked for rather than walked and the list arrives a moment later.
    property var options: []
    /// Whether the reader has pressed Escape over the list. Cleared by the next
    /// keystroke, so dismissing it is for this moment rather than for the
    /// session.
    property bool dismissed: false

    function refreshOptions() {
        if (row.postprocess) {
            row.refreshScriptOptions()
            return
        }
        row.options = box.activeFocus ? AppController.completions(box.text) : []
    }

    /// For the DATA box, the line each offered path came from. `options`
    /// holds the paths alone, because a script's first line is a path and
    /// nothing else; the subscript that came with a dataset is kept here, for
    /// takeIntoScript to write as the script's slice.
    property var scriptOptions: []

    /// The first line of the DATA box, which is where its path is.
    function pathLine() {
        const text = scriptBox.text
        const end = text.indexOf("\n")
        return end < 0 ? text : text.slice(0, end)
    }

    /// What could be written on the path line, while the caret is on it. The
    /// same completer the slice box asks, handed the path line alone; nothing
    /// is offered once that line has a step on it, because then it is not a
    /// path that is being typed.
    function refreshScriptOptions() {
        const line = row.pathLine()
        if (!scriptBox.editing || scriptBox.cursorPosition > line.length
                || /\.[A-Za-z_]\w*\s*\(/.test(line)) {
            row.options = []
            row.scriptOptions = []
            return
        }
        const offered = AppController.completions(line)
        const paths = []
        const full = []
        for (let i = 0; i < offered.length; ++i) {
            const whole = /^(.*)\[([^\[\]]*)\]$/.exec(offered[i])
            const path = whole ? whole[1] : offered[i]
            // The path already written is not something to write next.
            if (path === line || paths.indexOf(path) >= 0)
                continue
            paths.push(path)
            full.push(offered[i])
        }
        row.scriptOptions = full
        row.options = paths
    }

    /// Write a completed path onto the first line of the DATA box. A dataset
    /// brings the subscript that selects the whole of it, as it does in the
    /// slice box, and here that is a `.slice(...)` line under the path --
    /// unless the script already begins with a slice or a select of its own,
    /// which is the reader's and is left alone.
    function takeIntoScript(option) {
        const at = row.options.indexOf(option)
        const full = at >= 0 ? row.scriptOptions[at] : option
        const whole = /^(.*)\[([^\[\]]*)\]$/.exec(full)
        const path = whole ? whole[1] : full
        const text = scriptBox.text
        const end = text.indexOf("\n")
        let rest = end < 0 ? "" : text.slice(end)
        if (whole && !/^\n\s*\.(slice|select)\s*\(/.test(rest))
            rest = "\n.slice(" + whole[2] + ")" + rest
        scriptBox.text = path + rest
        scriptBox.cursorPosition = path.length
        row.problem = row.plot ? row.plot.entryError(row.rowIndex, scriptBox.text) : ""
        row.refreshOptions()
    }

    Connections {
        target: AppController
        function onCompletionsChanged() { row.refreshOptions() }
    }

    function commit() {
        if (!row.plot || row.rowIndex < 0)
            return
        // A commit is the end of an edit, so the list of what could have been
        // written next goes with it. The next keystroke brings it back.
        row.options = []
        const wanted = row.written.trim()
        if (wanted === row.expression) {
            row.problem = ""
            return
        }
        row.plot.setExpression(row.rowIndex, wanted)
        // What the model made of it is what the row is now stating -- for a
        // pipeline, formatted a step to a line.
        row.showExpression()
        row.problem = ""
    }

    function showExpression() {
        box.text = row.expression
        scriptBox.text = row.expression
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

                Layout.alignment: Qt.AlignTop
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
                objectName: "entryBoxLabel"

                Layout.preferredWidth: labels.width
                Layout.preferredHeight: Theme.smallControlHeight
                Layout.alignment: Qt.AlignTop
                text: row.postprocess ? qsTr("data") : qsTr("slice")
                font: Theme.microLabel
                color: Theme.textDisabled
                verticalAlignment: Text.AlignVCenter
            }

            // The pipeline's box. Its own control rather than the slice's box
            // made taller, because it keeps the whole of ScriptField's
            // contract -- Return commits and Shift+Return breaks the line --
            // and the two are shown one at a time, never both.
            ScriptField {
                id: scriptBox

                objectName: "entryScript"

                Layout.fillWidth: true
                visible: row.postprocess
                text: row.expression
                placeholderText: qsTr("/group/dataset\n.slice(:, 0)\n.max(0)")
                invalid: row.problem !== "" || row.error !== ""
                pending: row.pending

                completion: scriptCompletion

                onTextEdited: {
                    row.problem = row.plot
                        ? row.plot.entryError(row.rowIndex, scriptBox.text) : ""
                    row.dismissed = false
                    row.refreshOptions()
                }
                onCompleting: {
                    row.dismissed = false
                    row.refreshOptions()
                }
                onAccepted: row.commit()
                onEditingChanged: {
                    if (scriptBox.editing) {
                        row.dismissed = false
                        row.refreshOptions()
                        return
                    }
                    row.options = []
                    row.commit()
                }
                onCancelled: {
                    // The list first, as in the slice box: it is the thing that
                    // just appeared.
                    if (scriptCompletion.visible) {
                        row.dismissed = true
                        return
                    }
                    scriptBox.text = row.expression
                    row.problem = ""
                }
            }

            // Not a child of the box: a ScrollView puts what is declared in it
            // into what it scrolls. Parented to it instead, so it opens under
            // the box and as wide as it, as the slice box's list does.
            CompletionPopup {
                id: scriptCompletion

                objectName: "entryScriptCompletion"

                parent: scriptBox
                options: row.postprocess ? row.options : []
                written: row.pathLine()
                visible: row.postprocess && row.options.length > 0 && scriptBox.editing
                         && !row.dismissed

                onTaken: (option) => row.takeIntoScript(option)
            }

            FilterInput {
                id: box

                objectName: "entryBox"

                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                visible: !row.postprocess
                implicitHeight: Theme.smallControlHeight
                text: row.expression
                placeholderText: qsTr("/group/dataset[:, 0]")
                invalid: row.problem !== "" || row.error !== ""
                pending: row.pending

                onTextEdited: {
                    row.problem = row.plot
                        ? row.plot.entryError(row.rowIndex, box.text) : ""
                    row.dismissed = false
                    row.refreshOptions()
                }
                onAccepted: row.commit()
                onActiveFocusChanged: {
                    if (box.activeFocus) {
                        // Offered on the way in: an empty box is exactly the
                        // moment a reader does not know what a file holds, and
                        // the root's own children are a fair answer to that.
                        row.dismissed = false
                        row.refreshOptions()
                        return
                    }
                    row.options = []
                    row.commit()
                }
                Keys.onEscapePressed: {
                    // The list first: it is the thing that just appeared, and
                    // dismissing it is what Escape means while it is up.
                    if (completion.visible) {
                        row.dismissed = true
                        return
                    }
                    box.text = row.expression
                    row.problem = ""
                    box.focus = false
                }
                // Tab writes as much as every candidate shares and only then
                // chooses. Letting it through when there is nothing to write
                // is what keeps Tab moving between the boxes, which is what it
                // does everywhere else in this window.
                Keys.onTabPressed: (event) => {
                    row.dismissed = false
                    row.refreshOptions()
                    event.accepted = completion.take()
                }
                // A row the reader moved onto is taken rather than the line
                // committed; with none, Return falls through to onAccepted.
                Keys.onReturnPressed: (event) => {
                    event.accepted = completion.takeChosen()
                }
                Keys.onEnterPressed: (event) => {
                    event.accepted = completion.takeChosen()
                }
                Keys.onUpPressed: (event) => {
                    event.accepted = completion.visible
                    if (event.accepted)
                        completion.move(-1)
                }
                Keys.onDownPressed: (event) => {
                    event.accepted = completion.visible
                    if (event.accepted)
                        completion.move(1)
                }

                CompletionPopup {
                    id: completion

                    objectName: "entryCompletion"

                    options: row.options
                    written: box.text
                    // Nothing to choose from is nothing to draw. Bound rather
                    // than opened and closed by hand, so a list that empties
                    // as the reader types past the last match goes away on its
                    // own.
                    visible: row.options.length > 0 && box.activeFocus
                             && !row.dismissed

                    onTaken: (option) => {
                        box.text = option
                        box.cursorPosition = option.length
                        row.problem = row.plot
                            ? row.plot.entryError(row.rowIndex, option) : ""
                        row.refreshOptions()
                    }
                }
            }

            AppIconButton {
                objectName: "removeEntry"

                Layout.alignment: Qt.AlignTop
                glyph: "close"
                ink: Theme.danger
                bare: true
                onClicked: {
                    if (row.plot)
                        row.plot.removeEntry(row.rowIndex)
                }
            }
        }

        // --- whether the line is a pipeline --------------------------------
        // Under the box it changes, because what it changes is what that box
        // takes: a slice, or a whole pipeline written a step to a line.
        RowLayout {
            Layout.fillWidth: true
            Layout.rightMargin: Theme.smallControlHeight + Theme.gapS
            spacing: Theme.gapS

            Item { Layout.preferredWidth: Theme.indicatorSize }

            Text {
                Layout.preferredWidth: labels.width
                text: qsTr("pipeline")
                font: Theme.microLabel
                color: Theme.textDisabled
                verticalAlignment: Text.AlignVCenter
            }

            AppCheckBox {
                objectName: "entryPostprocess"

                text: qsTr("enable postprocessing")
                checked: row.postprocess
                onToggled: {
                    if (row.plot)
                        row.plot.setPostprocess(row.rowIndex, checked)
                }
            }

            Item { Layout.fillWidth: true }
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

        // --- the colour it is drawn in ------------------------------------
        // Beside the alias, because it is the same kind of thing: something
        // the reader says about this one line that costs no read and changes
        // nothing about what is drawn, only how. The legend's own right-click
        // offers the same two things, and this is the half that is *found*
        // rather than the half that is reached for -- a reader setting a line
        // up is already here.
        RowLayout {
            Layout.fillWidth: true
            Layout.rightMargin: Theme.smallControlHeight + Theme.gapS
            spacing: Theme.gapS

            Item { Layout.preferredWidth: Theme.indicatorSize }

            Text {
                Layout.preferredWidth: labels.width
                text: qsTr("colour")
                font: Theme.microLabel
                color: Theme.textDisabled
                verticalAlignment: Text.AlignVCenter
            }

            // The swatch shows what the line is actually drawn in, whether or
            // not that is a colour the reader chose. A swatch that went blank
            // for a line taking the cycle would be a control saying the line
            // has no colour, which is the one thing that is never true.
            ColorSwatchButton {
                objectName: "entryColour"

                label: qsTr("colour for this line")
                value: row.drawnColour
                onPicked: chosen => {
                    if (row.plot)
                        row.plot.setEntryColor(row.rowIndex, chosen)
                }
            }

            AppToolButton {
                objectName: "entryColourClear"

                text: qsTr("clear")
                size: "sm"
                // Absent rather than disabled would move the swatch's
                // neighbours about every time a colour was set or cleared;
                // this is one control whose presence is not news.
                enabled: row.colour !== undefined && row.colour !== null
                onClicked: {
                    if (row.plot)
                        row.plot.clearEntryColor(row.rowIndex)
                }

                AppToolTip {
                    shown: parent.hovered
                    text: qsTr("Give this line back to the colour cycle.")
                }
            }

            Item { Layout.fillWidth: true }
        }

        // --- which y axis it is read against -------------------------------
        // A line of pressures beside a line of temperatures is a flat stroke
        // along the bottom of whichever pane the larger one sets. Its own axis
        // is the line as it would be drawn alone, numbered to the left of the
        // common one in the line's own colour. Beside the colour because it is
        // the same kind of thing: how this one line is drawn, at no read.
        //
        // Disabled rather than hidden while there is only one line, for the
        // reason the colour's "clear" is: a row that came and went as lines
        // were ticked would move everything under it. The hover is the row's
        // own, because a disabled control takes no pointer and so could never
        // say why it is disabled.
        RowLayout {
            id: axisRow

            Layout.fillWidth: true
            Layout.rightMargin: Theme.smallControlHeight + Theme.gapS
            spacing: Theme.gapS

            HoverHandler { id: axisHover }

            AppToolTip {
                shown: axisHover.hovered && !row.separable
                text: qsTr("A plot of one line has one y axis. Add or tick a " +
                           "second line to give either its own.")
            }

            Item { Layout.preferredWidth: Theme.indicatorSize }

            Text {
                Layout.preferredWidth: labels.width
                text: qsTr("axis")
                font: Theme.microLabel
                color: Theme.textDisabled
                verticalAlignment: Text.AlignVCenter
            }

            AppCheckBox {
                objectName: "entrySeparateAxis"

                text: qsTr("separate y-axis")
                enabled: row.separable
                checked: row.separateAxis
                onToggled: {
                    if (row.plot)
                        row.plot.setSeparateAxis(row.rowIndex, checked)
                }
            }

            // Only beside a separate axis, because it is a question about
            // one: the common axis is the one the reader zooms.
            AppCheckBox {
                objectName: "entryAxisFixed"

                text: qsTr("exclude from zooming")
                visible: row.separateAxis
                enabled: row.separable
                checked: row.axisFixed
                onToggled: {
                    if (row.plot)
                        row.plot.setAxisFixed(row.rowIndex, checked)
                }

                AppToolTip {
                    shown: parent.hovered
                    text: qsTr("Keep this axis at the whole of its line " +
                               "while the plot zooms and pans.")
                }
            }

            Item { Layout.fillWidth: true }
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
        text: qsTr("pipeline")
    }

    // A row whose text was changed from anywhere else -- a restored view, a
    // dataset added from the tree -- is still this row, so the boxes follow it.
    onExpressionChanged: {
        if (!row.editing) {
            row.showExpression()
            row.problem = ""
        }
    }
    // Ticking the box rewrites the line into the other grammar, and the box
    // that shows it changes with it.
    onPostprocessChanged: {
        row.showExpression()
        row.problem = ""
    }

    onAliasChanged: {
        if (!aliasBox.activeFocus)
            aliasBox.text = row.alias
    }
}
