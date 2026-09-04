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
Rectangle {
    id: panel

    color: Theme.surface

    readonly property var pipeline: AppController.postprocessModel
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

    // Why the switch will not do anything, on the datatypes it cannot. Said
    // here rather than left to be inferred from a panel that does nothing.
    Text {
        id: notNumeric

        anchors.top: switchRow.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: Theme.gapM
        anchors.rightMargin: Theme.gapM
        visible: AppController.datasetTabVisible && !AppController.datasetIsNumeric
        height: visible ? implicitHeight : 0
        text: qsTr("this dataset holds no numbers to work on")
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
        anchors.bottom: parent.bottom
        anchors.margins: Theme.gapM
        clip: true
        spacing: 0
        model: panel.pipeline
        // Dead unless the switch is set *and* there is arithmetic to do. On a
        // dataset of strings the chain is drawn and greyed with the reason
        // above it, rather than offering an add button for operations that
        // could not run.
        enabled: (panel.pipeline ? panel.pipeline.enabled : false)
                 && AppController.datasetIsNumeric
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
                current: panel.pipeline !== null
                         && panel.pipeline.activeRow === slot.index
                         && slot.kind !== PostprocessModel.Output
                last: slot.index === steps.count - 1
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
