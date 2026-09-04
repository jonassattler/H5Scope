// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import H5Scope.Backend

/// One line of the pipeline: the gutter that joins it to the steps above and
/// below, what the step is, what it was given, the shape it leaves, and -- on
/// the steps a reader added -- a way to take it out again.
///
/// Every control writes straight to the model, as TableSetupPanel's delegate
/// does. The model is the authority on what a shape is, on whether an argument
/// reads and on where a dragged row is allowed to land, so nothing in this file
/// has to be.
Item {
    id: row

    /// The pipeline this is a row of, and which row of it.
    property var pipeline
    property int rowIndex: 0

    /// The roles, bound by the panel's delegate.
    property int kind: PostprocessModel.Operation
    property string label
    property string argument
    property string argumentLabel
    property string placeholder
    property string shape
    property string error
    property bool removable: false
    property bool movable: false
    /// False for a row past the one the reader clicked: it did not run, so it
    /// is drawn as not having run.
    property bool computed: true
    /// Whether this is the row the pipeline is being computed up to.
    property bool current: false
    /// The last row draws the gutter's corner rather than its tee.
    property bool last: false
    /// The operations the add row offers, handed down so the list is built
    /// once by the panel rather than on every read of every row.
    property var choices: []
    /// The width of the column the argument's name stands in, measured once by
    /// the panel over every name any row can put there. Handed down rather
    /// than worked out here so that every row agrees, which is the only way
    /// the boxes after it can line up.
    property real argumentLabelWidth: Theme.s11

    implicitHeight: Math.max(Theme.settingRowHeight, contents.implicitHeight)

    /// Everything but the gutter fades when the step did not run. Opacity
    /// rather than a second palette, which is this system's rule for anything
    /// inactive: it stays legible against whatever it is drawn on.
    readonly property real ink: row.computed ? 1.0 : 0.4
    /// The ends of the chain are not steps, so there is nothing to compute up
    /// to and nothing to pick up.
    readonly property bool selectable: row.kind === PostprocessModel.Slice
                                       || row.kind === PostprocessModel.Operation
    /// What is in the argument box has been typed and not applied.
    ///
    /// A row is not doing what it says while this is true: the shape beside it
    /// and the numbers in the view are still the previous argument's. An
    /// argument that will not read is excluded -- the box is already amber
    /// with the reason under it, and "not applied yet" is not the useful half
    /// of that.
    readonly property bool pending: box.visible && internal.problem === ""
                                    && box.text !== row.argument
    /// Whether something is wrong, as against merely unfinished.
    readonly property bool troubled: internal.problem !== "" || row.error !== ""

    QtObject {
        id: internal

        /// Why what is in the argument box will not read.
        property string problem: ""
    }

    // The row lifts off the surface while it is being carried, which is the
    // only thing that says the drag is doing something before it lands.
    transform: Translate { y: dragHandler.active ? dragHandler.carried : 0 }
    z: dragHandler.active ? 1 : 0

    // --- the gutter -------------------------------------------------------
    // Two columns, in this order: the rule that joins the steps to each other,
    // and then the handle a step is picked up by. Beside one another rather
    // than stacked, because a grip drawn over the rule covers the very thing
    // it is a handle for; and the rule on the outside, because it is the
    // drawing of the chain and the chain is what the panel's left edge is.
    //
    // The rules are drawn rather than typed. The tree-drawing in the diagram
    // this panel comes from is box characters, and a font that has to supply
    // the corner is a font this application does not carry.
    Item {
        id: gutter

        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: Theme.dragHandleWidth + Theme.stepGutterWidth

        // The upright, stopping at the corner on the last row. Drawn at
        // chainGuide, which is text ink: this is the drawing of the pipeline
        // rather than a rule separating two things, and at a separator's
        // weight it was a smudge down the gutter. See the token.
        Rectangle {
            x: Theme.stepGutterWidth / 2
            width: Theme.borderWidth
            height: row.last ? parent.height / 2 : parent.height
            color: Theme.chainGuide
        }

        // The arm, reaching out from the upright to the handle beside it.
        Rectangle {
            x: Theme.stepGutterWidth / 2
            y: parent.height / 2
            width: Theme.stepGutterWidth / 2
            height: Theme.borderWidth
            color: Theme.chainGuide
        }

        Item {
            id: handle

            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: Theme.dragHandleWidth

            AppIcon {
                objectName: "stepGrip"

                anchors.centerIn: parent
                width: Theme.dragHandleWidth
                height: Theme.dragHandleWidth
                name: "grip"
                visible: row.movable
                color: Theme.textSecondary
                opacity: dragHandler.active ? 1.0 : (gripHover.hovered ? 0.9 : 0.45)
            }

            HoverHandler {
                id: gripHover

                enabled: row.movable
                cursorShape: Qt.OpenHandCursor
            }

            DragHandler {
                id: dragHandler

                enabled: row.movable
                target: null
                xAxis.enabled: false
                yAxis.enabled: true

                /// How far the pointer has come since it went down, which is
                /// what the row is carried by and what the drop is measured
                /// from.
                ///
                /// Not `activeTranslation`: a DragHandler does not become
                /// active until the pointer has passed the drag threshold, and
                /// that translation is counted from the moment it did -- so a
                /// drag of exactly one row reads as one row less the threshold,
                /// and a drag made of one long movement reads as nothing at
                /// all. The press position is on the centroid throughout,
                /// including at the moment the grab ends, which is the moment
                /// this has to be read.
                readonly property real carried:
                    dragHandler.centroid.scenePosition.y
                    - dragHandler.centroid.scenePressPosition.y

                onActiveChanged: {
                    if (dragHandler.active)
                        return
                    // Read straight off the handler rather than out of a copy
                    // kept as the drag went: a handler becomes active *during*
                    // the delivery of the move that passed the threshold, so a
                    // mirror written under an `active` guard misses that move --
                    // and on a short drag that is the only move there was.
                    //
                    // Rows are one height, so how far it was dropped from where
                    // it was picked up is how many rows it moved. The model
                    // clamps the destination, so nothing lands above the slice.
                    const moved = Math.round(dragHandler.carried
                                             / Math.max(1, row.height))
                    if (moved !== 0 && row.pipeline)
                        row.pipeline.moveStep(row.rowIndex, row.rowIndex + moved)
                }
            }
        }
    }

    // --- clicking the row runs the pipeline only this far -----------------
    Rectangle {
        anchors.left: gutter.right
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        radius: Theme.radiusS
        color: row.current ? Theme.surfaceRaised
             : (rowHover.hovered && row.selectable) ? Theme.surfaceHover
                                                    : Theme.clear(Theme.surfaceHover)
    }

    HoverHandler { id: rowHover }

    TapHandler {
        enabled: row.selectable
        // The property, not a setter: a WRITE function is not something
        // QML can call, and a row that silently did nothing when clicked
        // is the sort of thing only a test catches.
        onTapped: if (row.pipeline) row.pipeline.activeRow = row.rowIndex
    }

    // --- what the row says ------------------------------------------------
    ColumnLayout {
        id: contents

        anchors.left: gutter.right
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: Theme.gapS
        anchors.rightMargin: Theme.gapS
        spacing: Theme.gapXS
        opacity: row.ink

        // --- the row that puts another operation in the chain --------------
        // A button and a box, not a box alone: picking from a list is choosing
        // *which*, and the reader should get to change their mind before one
        // more step lands in a pipeline that is running.
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gapS
            visible: row.kind === PostprocessModel.Adder

            AppToolButton {
                objectName: "addStep"

                Layout.preferredWidth: Theme.s13
                Layout.alignment: Qt.AlignVCenter
                text: qsTr("add")
                size: "sm"
                variant: "secondary"
                onClicked: if (row.pipeline) row.pipeline.addChosenStep()
            }

            AppComboBox {
                objectName: "addOperation"

                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                // Held in a property rather than written into `model:`: an
                // expression that rebuilds its array every time it is read
                // resets the box's index under the reader, which is the trap
                // PlotSettingsPanel and TableSettingsPanel both carry a note
                // about.
                model: row.choices
                textRole: "name"
                selectedIndex: row.pipeline ? row.pipeline.chosenOperation : 0
                onActivated: (index) => {
                    if (row.pipeline)
                        row.pipeline.chosenOperation = index
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gapS
            visible: row.kind !== PostprocessModel.Adder

            Text {
                id: name

                objectName: "stepLabel"

                // The dataset's path is a path and elides from the left, the
                // way it does in the slice bar. Every other row holds a word,
                // and holds it in a column of fixed width so that what comes
                // after it -- the argument's name, then the box -- lines up
                // down the panel. Reading a column is the whole way this panel
                // is meant to be read.
                Layout.preferredWidth: row.kind === PostprocessModel.Input
                                       ? -1 : Theme.s13
                Layout.maximumWidth: row.kind === PostprocessModel.Input
                                     ? Theme.s13 : -1
                text: row.label
                font: row.kind === PostprocessModel.Input ? Theme.mono
                                                          : Theme.bodySmall
                color: row.kind === PostprocessModel.Operation ? Theme.textPrimary
                                                               : Theme.textSecondary
                elide: row.kind === PostprocessModel.Input ? Text.ElideLeft
                                                           : Text.ElideNone

                HoverHandler { id: nameHover }

                AppToolTip {
                    shown: name.truncated && nameHover.hovered
                    verbatim: true
                    text: row.label
                }
            }

            // What the box beside it wants: "axis", "shape", "subscripts".
            // A machine label rather than a sentence, so micro -- which
            // uppercases it, the same treatment the bar gives "slice" and the
            // foot of this panel gives "add".
            Text {
                objectName: "stepArgumentLabel"

                Layout.alignment: Qt.AlignVCenter
                // One column, the width of the longest word any row puts in
                // it, and the same on every row including the slice's. It used
                // to be a floor that the slice was exempt from, which put the
                // slice's box in a different place from every other row's --
                // and reading a column is the whole way this panel is meant to
                // be read.
                Layout.preferredWidth: row.argumentLabelWidth
                Layout.minimumWidth: row.argumentLabelWidth
                visible: row.argumentLabel !== ""
                text: row.argumentLabel
                font: Theme.micro
                color: Theme.textSecondary
            }

            FilterInput {
                id: box

                objectName: "stepArgument"

                // One width for every row. The slice's box used to fill the
                // rail while an operation's stayed at a third of that, so the
                // two rows that take an argument the same way looked like two
                // different kinds of control -- and the boxes started and
                // ended at four different x positions down a panel whose whole
                // point is a column. A long slice scrolls inside its box here;
                // the bar above the views is where there is room to write one.
                Layout.preferredWidth: Theme.s13
                Layout.alignment: Qt.AlignVCenter
                implicitHeight: Theme.smallControlHeight
                visible: row.argumentLabel !== ""
                placeholderText: row.placeholder
                invalid: internal.problem !== ""
                pending: row.pending

                // The contract the slice line already keeps, and for the same
                // reason: every keystroke is checked and nothing is applied
                // until the reader commits, so a half-typed argument never
                // becomes a read.
                onTextEdited: internal.problem =
                    row.pipeline ? row.pipeline.argumentError(row.rowIndex, box.text)
                                 : ""
                onAccepted: row.commit()
                onActiveFocusChanged: if (!box.activeFocus) row.commit()

                Keys.onEscapePressed: {
                    box.text = row.argument
                    internal.problem = ""
                    box.focus = false
                }
            }

            Item { Layout.fillWidth: true }

            // The shape after this row's operation, which read down the rows
            // is what the panel is for. Blank on a row that did not run: a
            // stale shape beside a greyed one would be a claim about data
            // nobody computed.
            Text {
                objectName: "stepShape"

                text: row.shape
                font: Theme.readout
                color: row.kind === PostprocessModel.Output ? Theme.textPrimary
                                                            : Theme.textSecondary
            }

            AppIconButton {
                objectName: "stepRemove"

                Layout.alignment: Qt.AlignVCenter
                glyph: "close"
                hint: qsTr("remove this step")
                ink: Theme.danger
                // No slab under it: this sits at the end of a line of text
                // rather than in a bar of buttons, and a rim around every row
                // would draw a column of boxes down the panel. The glyph is
                // what lights up when the pointer is over it.
                bare: true
                visible: row.removable
                onClicked: if (row.pipeline) row.pipeline.removeStep(row.rowIndex)
            }
        }

        // Why this row will not run, or -- when there is nothing wrong with it
        // -- that it is not running what is in its box yet. Caption rather
        // than micro: micro uppercases what it is given, which turns a
        // sentence into a machine label.
        //
        // The two never appear together, and the colour says which is which:
        // amber is this system's "look at this", and an edit nobody has
        // pressed Return on is not that. It gets signal white, the same ink
        // the box's own ground is a tenth of the way toward.
        Text {
            objectName: "stepNote"

            Layout.fillWidth: true
            visible: text !== ""
            text: internal.problem !== "" ? internal.problem
                : row.error !== ""        ? row.error
                : row.pending             ? qsTr("not applied yet — press Return")
                                          : ""
            font: Theme.caption
            color: row.troubled ? Theme.warning : Theme.accent
            wrapMode: Text.WordWrap
        }
    }

    function commit() {
        if (!row.pipeline || box.text === row.argument) {
            internal.problem = ""
            return
        }
        // Whether it reads was answered on the last keystroke. One that does
        // not is applied anyway and left in the box with its reason, which is
        // what the slice bar does and for the same reason; one that does is
        // read back, because what the model made of it is what the row is now
        // stating. The slice row is where that differs: it writes through to
        // the table, which resolves "0:4" over a four-long axis to ":", and a
        // box still holding "0:4" would go on saying Return had not been
        // pressed over a view already drawn from it.
        const readable = internal.problem === ""
        row.pipeline.setArgument(row.rowIndex, box.text)
        internal.problem = ""
        if (readable)
            box.text = row.argument
    }

    // Put the box back whenever the model's own text moves under it: a
    // reordered pipeline, a new dataset, or the slice being written in the bar
    // rather than here.
    onArgumentChanged: {
        if (!box.activeFocus)
            box.text = row.argument
        internal.problem = ""
    }

    Component.onCompleted: box.text = row.argument
}
