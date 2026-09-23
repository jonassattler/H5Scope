// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import H5Scope.Backend

/// The Data Viewer's "postprocessing" sidebar: an ordered chain of numpy-shaped
/// operations between the dataset and the three views.
///
/// The rows are the chain read downwards -- the dataset, the slice, an
/// operation apiece, the output -- and the shape column beside them is the
/// point of the whole panel: it says what each step leaves behind, so a reader
/// can see a rank fall from four to two without running anything. The shapes
/// are arithmetic over the shape above, so they stay live on a dataset far too
/// large to compute.
///
/// The second row is not a copy of the slice above the table; it *is* it.
/// Writing in either place writes to TableSetupModel, which is why the panel,
/// the bar and the data settings can never disagree about which elements are
/// being read.
///
/// With visual editing off, the rows give way to the same chain written out as
/// text -- the path, `.select`, `.slice`, a step to a line -- which is the one
/// form of a pipeline that can be read at a glance, pasted and typed. The two
/// are views of one model and not two copies: see postproc::Script.
Rectangle {
    id: panel

    color: Theme.surface

    readonly property var pipeline: AppController.postprocessModel

    /// Rows, or text. A way of looking at the pipeline rather than a setting
    /// about it, so it is neither filed per dataset nor remembered between
    /// runs: the list of what this program remembers is short on purpose.
    property bool visualEditing: true
    /// Why what is in the text box will not read, as opposed to why the
    /// applied pipeline stopped. Two channels, as the rows have.
    property string scriptProblem: ""
    /// Put the model's script back into the box even though it has focus.
    /// Set by a commit, whose answer may arrive a moment later -- a script
    /// naming another dataset is applied once that dataset is open -- and
    /// cleared by the next keystroke, which is the reader taking the box back.
    property bool followScript: false
    /// Built once here rather than per row: a model expression that rebuilds
    /// its array on every read resets the dropdown's index under the reader.
    readonly property var choices: panel.pipeline ? panel.pipeline.operations : []

    /// The column the argument names stand in, as wide as the longest of them.
    ///
    /// Measured rather than guessed at a spacing step: they are set in micro,
    /// which uppercases and tracks what it is given, so how wide "subscripts"
    /// comes out is the host font's answer and not this file's. Measured once
    /// here rather than per row, because a column every row agrees on is the
    /// only thing that puts their boxes at the same x -- and the boxes lining
    /// up is what makes the panel a table of a pipeline rather than six
    /// settings that happen to be stacked.
    ///
    /// A Column reports the widest of its children as its implicit width,
    /// which is exactly the arithmetic wanted; it is never shown.
    readonly property real argumentLabelWidth: argumentLabels.implicitWidth

    /// Whether the chain can be worked on: the switch set, and arithmetic to
    /// do -- or a compound, whose member is the question still open.
    readonly property bool live: (panel.pipeline ? panel.pipeline.enabled : false)
                                 && (AppController.datasetIsNumeric
                                     || AppController.datasetIsCompound)

    Column {
        id: argumentLabels

        visible: false

        Repeater {
            model: panel.choices

            delegate: Text {
                required property var modelData

                text: modelData.argumentLabel
                font: Theme.micro
            }
        }
    }

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
            text: qsTr("postprocessing")
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

    // --- the switch the whole thing hangs off -----------------------------
    // Everything below is `enabled` off this, which fades the subtree at the
    // 0.4 every disabled control in this application uses. Greyed rather than
    // hidden: the chain is still what it was, and a panel that emptied itself
    // when the switch went off would look like it had forgotten.
    Item {
        id: switchRow

        anchors.top: heading.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.gapM
        height: Theme.settingRowHeight

        AppCheckBox {
            id: enableBox

            objectName: "enablePostprocessing"

            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("enable postprocessing")
            checked: panel.pipeline ? panel.pipeline.enabled : false
            onToggled: if (panel.pipeline) panel.pipeline.enabled = enableBox.checked
        }
    }

    // Rows or text. Live whether or not the pipeline is on, because choosing
    // how to look at a chain is not running it.
    Item {
        id: visualRow

        anchors.top: switchRow.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: Theme.gapM
        anchors.rightMargin: Theme.gapM
        height: Theme.settingRowHeight

        AppCheckBox {
            id: visualBox

            objectName: "visualEditing"

            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("enable visual editing")
            checked: panel.visualEditing
            onToggled: {
                panel.visualEditing = visualBox.checked
                // The text is the whole chain, so what the views draw has to
                // be the whole chain too: a row clicked to look at a stage is
                // a thing only the rows can show.
                if (!panel.visualEditing && panel.pipeline)
                    panel.pipeline.activeRow = panel.pipeline.rowCount()
            }
        }
    }

    // Why the switch will not do anything, on the datatypes it cannot. Said
    // here rather than left to be inferred from a panel that does nothing.
    Text {
        id: notNumeric

        anchors.top: visualRow.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: Theme.gapM
        anchors.rightMargin: Theme.gapM
        visible: AppController.datasetTabVisible && !AppController.datasetIsNumeric
        height: visible ? implicitHeight : 0
        // A compound is not a dataset with nothing to work on, it is one with
        // a question still open: which member. The chain below stays live for
        // it, because the row that answers that question is in the chain.
        text: AppController.datasetIsCompound
              ? qsTr("name a member below to work on its numbers")
              : qsTr("this dataset holds no numbers to work on")
        font: Theme.caption
        color: Theme.textDisabled
        wrapMode: Text.WordWrap
    }

    // --- the chain --------------------------------------------------------
    ListView {
        id: steps

        objectName: "pipelineRows"

        anchors.top: notNumeric.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: foot.top
        anchors.margins: Theme.gapM
        clip: true
        spacing: 0
        model: panel.pipeline
        visible: panel.visualEditing
        // Dead unless the switch is set *and* there is arithmetic to do. On a
        // dataset of strings the chain is drawn and greyed with the reason
        // above it, rather than offering an add button for operations that
        // could not run.
        enabled: panel.live
        opacity: steps.enabled ? 1.0 : 0.4
        boundsBehavior: Flickable.StopAtBounds

        ScrollBar.vertical: ScrollBar {}

        // The roles are taken on a plain Item and handed down, rather than
        // onto the row itself: a required property cannot also be one the
        // component already declares, and the row's properties are its
        // interface rather than this list's.
        delegate: Item {
            id: slot

            required property int index
            required property int kind
            required property string label
            required property string argument
            required property string argumentLabel
            required property string placeholder
            required property string shape
            required property string error
            required property bool removable
            required property bool movable
            required property bool computed
            required property var memberChoices

            width: steps.width
            height: line.implicitHeight

            PostprocessStepRow {
                id: line

                width: parent.width
                height: implicitHeight

                pipeline: panel.pipeline
                choices: panel.choices
                argumentLabelWidth: panel.argumentLabelWidth
                rowIndex: slot.index
                kind: slot.kind
                label: slot.label
                argument: slot.argument
                argumentLabel: slot.argumentLabel
                placeholder: slot.placeholder
                shape: slot.shape
                error: slot.error
                removable: slot.removable
                movable: slot.movable
                computed: slot.computed
                memberChoices: slot.memberChoices
                current: panel.pipeline !== null
                         && panel.pipeline.activeRow === slot.index
                         && slot.kind !== PostprocessModel.Output
                last: slot.index === steps.count - 1
            }
        }
    }

    // --- the chain, as text ------------------------------------------------
    Column {
        id: scriptEditor

        objectName: "pipelineScriptEditor"

        anchors.top: notNumeric.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.gapM
        spacing: Theme.gapXS
        visible: !panel.visualEditing
        // Typing is live even with the switch off: a script that is applied
        // turns the pipeline on, which is what writing one asks for. Greyed
        // only where there is nothing to run it on.
        enabled: AppController.datasetTabVisible
        opacity: (panel.live || scriptBox.editing) ? 1.0 : 0.4

        ScriptField {
            id: scriptBox

            objectName: "pipelineScript"

            width: parent.width
            placeholderText: qsTr("/group/dataset\n.slice(:, 0)\n.max(0)")
            invalid: panel.scriptProblem !== ""
            pending: panel.scriptProblem === "" && panel.pipeline !== null
                     && scriptBox.text !== panel.pipeline.script

            onTextEdited: {
                panel.followScript = false
                panel.scriptProblem = panel.pipeline
                    ? panel.pipeline.scriptError(scriptBox.text) : ""
            }
            onAccepted: panel.commitScript()
            onEditingChanged: if (!scriptBox.editing) panel.commitScript()
            onCancelled: {
                scriptBox.text = panel.pipeline ? panel.pipeline.script : ""
                panel.scriptProblem = ""
            }
        }

        // Why the text will not read, why the pipeline it names stopped, or
        // that it has not been applied -- the three sentences the rows say,
        // in the same order and the same colours.
        Text {
            objectName: "pipelineScriptNote"

            width: parent.width
            visible: text !== ""
            text: panel.scriptProblem !== "" ? panel.scriptProblem
                : scriptBox.pending ? qsTr("not applied yet — press Return, or "
                                           + "Shift+Return for a new line")
                : (panel.pipeline && panel.pipeline.error !== "") ? panel.pipeline.error
                : ""
            font: Theme.caption
            color: (panel.scriptProblem !== ""
                    || (!scriptBox.pending && panel.pipeline
                        && panel.pipeline.error !== ""))
                   ? Theme.warning : Theme.accent
            wrapMode: Text.WordWrap
        }

        // What the chain leaves, which the rows state in their shape column
        // and a script has no column for. Set as the output row sets it.
        Row {
            spacing: Theme.gapS
            visible: panel.pipeline !== null && panel.pipeline.outputText !== ""

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("output")
                font: Theme.micro
                color: Theme.textSecondary
            }

            Text {
                objectName: "pipelineScriptOutput"

                anchors.verticalCenter: parent.verticalCenter
                text: panel.pipeline ? panel.pipeline.outputText : ""
                font: Theme.readout
                color: Theme.textPrimary
            }
        }
    }

    function commitScript() {
        if (!panel.pipeline || scriptBox.text === panel.pipeline.script) {
            panel.scriptProblem = ""
            return
        }
        const refused = panel.pipeline.applyScript(scriptBox.text)
        if (refused !== "") {
            // Nothing was applied; the text stays, with the reason under it.
            panel.scriptProblem = refused
            return
        }
        // What the model made of it is what the box now says -- formatted, a
        // step to a line -- and, for a script naming another dataset, what it
        // will make of it once that dataset is open.
        panel.scriptProblem = ""
        panel.followScript = true
        scriptBox.text = panel.pipeline.script
    }

    Connections {
        target: panel.pipeline
        function onChanged() {
            if (!scriptBox.editing || panel.followScript)
                scriptBox.text = panel.pipeline.script
        }
    }

    Component.onCompleted: scriptBox.text = panel.pipeline ? panel.pipeline.script : ""

    // --- the foot: the output, into a custom plot -------------------------
    // Below the chain in both views, because it is about the end of the chain:
    // what the views are drawing, and whether that is a line. Refused with its
    // reason rather than hidden, so a reader who has a 3 × 4 output learns
    // what the button wants from them.
    Item {
        id: foot

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Theme.gapM
        height: Theme.settingRowHeight

        // The hover is on a wrapper because a disabled button takes none, and
        // a disabled button is exactly the one whose reason is wanted.
        Item {
            id: addWrapper

            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: addToCustom.implicitWidth
            height: addToCustom.implicitHeight

            AppToolButton {
                id: addToCustom

                objectName: "addToCustomPlot"

                anchors.fill: parent
                text: qsTr("add to custom plot")
                size: "sm"
                variant: "secondary"
                enabled: panel.pipeline ? panel.pipeline.canAddToCustom : false
                onClicked: customMenu.popup(addToCustom, 0, addToCustom.height)
            }

            HoverHandler { id: addHover }

            AppToolTip {
                shown: addHover.hovered && !addToCustom.enabled
                text: panel.pipeline ? panel.pipeline.customRefusal : ""
            }
        }

        AppMenu {
            id: customMenu

            AppMenuItem {
                objectName: "addToNewCustomPlot"

                text: qsTr("New Custom Plot")
                onTriggered: {
                    const plots = AppController.customPlots
                    plots.addScriptTo(plots.addPlot(), panel.pipeline.customScript())
                }
            }

            AddToCustomMenu {
                title: qsTr("Add to")
                onPicked: (index) => AppController.customPlots.addScriptTo(
                              index, panel.pipeline.customScript())
            }
        }
    }

    // The chain belongs to the dataset it was made about, like every other
    // setting in this application: a Max over axis 0 says nothing about the
    // next dataset, and on one of a different rank it does not even exist.
    DatasetMemory {
        subject: panel.pipeline
        group: "postprocess"
        // Not activeRow: which step a reader clicked to look at is a moment
        // of inspection rather than a setting, and restoring it would also
        // have to be ordered after the steps it indexes into.
        names: ["enabled", "steps"]
    }
}
