// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import H5Scope.Backend

/// The slice line, and the one place in the window where a reader can write
/// one instead of assembling it out of radio buttons and sliders.
///
///     ┌──────────────────────────────────┐
///     │ /cube[:, 2, [0,3]]               │
///     └──────────────────────────────────┘
///       fixed  editable   fixed   room to grow
///
/// Only what stands between the brackets is a text box. The path and the
/// brackets are printed chrome, because they are not the reader's to change
/// here: which object is on screen is the tree's answer, and a line missing
/// one of its brackets is not a slice of anything. What is left is exactly
/// what AppController.sliceText prints, so the box always opens holding the
/// selection the table is already showing.
///
/// **A compound is written as one line instead**, and everything after the
/// path is the box:
///
///     ┌──────────────────────────────────┐
///     │ /events[:, 2].samples            │
///     └──────────────────────────────────┘
///       fixed  editable            room to grow
///
/// It began the other way, with a second box for the chain after the closing
/// bracket, and that shape was wrong for the job in a way only a reader meets.
/// A chain and the subscript it appends axes to are one statement --
/// `.samples[2]` belongs on the slice and `[:, 2].samples` is the same
/// selection written the other way round -- so rearranging one is usually
/// rearranging both, and two boxes made that two commits with a shape nobody
/// asked for in between. Two boxes also meant two targets, and the second was
/// a few characters wide with a grey `.member` standing in it, which reads as
/// a value that has been chosen rather than as a box that is empty.
///
/// One line has the properties the split did not: it is what
/// `sliceExpression` prints less the path, so it pastes; it is checked whole
/// on every keystroke and applied whole on Return, so there is no half-applied
/// state to be in; and the brackets are the reader's to write, which is what
/// "freely edited" has to mean. The bracketed form above stays for everything
/// that is not a compound, where there is no chain and so nothing to be one
/// statement with.
///
/// The two brackets are two items, not one item and a character glued to the
/// end of the path, because they outlive the path: a bar too narrow for the
/// whole line gives the path away first, and `[0,1]]` -- an opening bracket
/// eaten along with the path it was stuck to -- is not a slice either.
///
/// The parts are packed to the left and the well's spare width falls after
/// the closing bracket, so the line always reads as one line. The box is
/// exactly as wide as what it is holding and the bracket follows it out as the
/// reader types -- which is a value growing, and is what the design draws. The
/// alternative, a box reserving its room with the bracket pinned to the right
/// of the well, puts a hand's width of nothing between the subscripts and the
/// `]` that closes them, and reads as trailing space inside the slice.
///
/// **How the width is shared, which is the whole of this file's arithmetic.**
/// The well asks for exactly what it is holding: the path, the subscripts, the
/// brackets, and one gap step of room to grow into. It used to ask for that
/// plus a fixed 112 pixels, which is where the dead space at the end of the bar
/// came from -- a hand's width of nothing, on every slice, however long.
///
/// The box itself was held open at sixty pixels for the same reason, which put
/// the dead space *inside* the brackets instead: on `/cube[:, :, :]` the `]`
/// stood thirty-five pixels clear of the subscripts it closes, which is a
/// slice with five spaces typed after it. Sixty pixels is a floor for a well
/// the bar is squeezing, not a width to pad a short slice out to.
///
/// When the bar cannot give it that much, the well gives width back in this
/// order: the room to grow into first, because nothing is drawn in it; then
/// the path, because the subscripts are the only part of this line anyone can
/// write and the path is named in three other places in the window; and only
/// then do the subscripts start scrolling inside their box. The brackets do
/// not yield at all.
///
/// That the well can give width back *at all* is the bar's doing rather than
/// this file's, and was the bug behind most of the above: a RowLayout does not
/// resize an item that does not fill, so the well asked for the width of its
/// line and took it whatever the bar could afford, walking the two settings
/// buttons off the right-hand end of the window on a long slice. It fills now,
/// between this file's own floor and a ceiling of the line it is holding, so
/// it grows to the line and no further and yields to the bar and no less.
///
/// The path used to be capped at a fixed width as well as squeezed, so it was
/// cut short
/// on a bar with room to spare -- which looked like the well refusing to show
/// the object it was a slice of. There is no cap now: the path is elided only
/// when the subscripts have taken the room first.
///
/// Because the box is only as wide as its text, a click that lands anywhere
/// else in the well starts editing too: on a long path the box is a couple of
/// characters across, and a well holding a text box is one target.
///
/// Reading and writing are the same grammar -- the line pastes back into
/// itself -- which is why the box seeds from the readout rather than from a
/// placeholder, and why a scattered selection is bracketed when printed.
///
/// The contract is the one the data settings panel's expression box already
/// keeps: every keystroke is checked and nothing is applied until the reader
/// commits, a line that does not read stays on screen in hazard amber with the
/// reason beside it, and the table holds the last selection that did read.
/// Return applies; Escape puts back what the table is showing.
///
/// A line that *does* read and has not been applied says so as well -- see
/// `pending`. It is the same contract's other half, and the half that used to
/// be silent: a slice typed and not committed leaves a bar describing one set
/// of elements above a table drawn from another, and nothing on screen said
/// which was which.
Rectangle {
    id: field

    /// Why what is in either box cannot be read, or "" when both can. The bar
    /// prints this beside the field; nothing here has room for it.
    ///
    /// One line for two boxes because there is one well and one note beside
    /// it. The member is reported first when both are wrong: a chain that does
    /// not resolve is the reason the shape the slice is against is not the one
    /// the reader thinks it is.
    readonly property string error: field.oneLine ? internal.selectionError
                                                 : internal.error
    /// A scalar is one cell: there are no subscripts, so there is nothing to
    /// type and the line is the path by itself.
    ///
    /// Not a question for a compound, which is written as one line and gets a
    /// box whatever its rank: `.energy` on a scalar struct is a selection.
    readonly property bool editable: AppController.datasetRank > 0
    /// Whether the whole of what follows the path is one box.
    ///
    /// A compound, and only a compound: it is the one thing with a member to
    /// name, and naming one is the same statement as subscripting the axes it
    /// appends. Everything else has a subscript and nothing else, and gets the
    /// bracketed form above.
    ///
    /// The *dataset's* class, so the line stays writable after a chain has
    /// resolved to a float: the reader has to be able to get back at what they
    /// typed.
    readonly property bool oneLine: AppController.datasetIsCompound

    /// What is in the box reads as a slice, and is not the slice on screen.
    ///
    /// The whole tab below this line is a picture of some elements, and while
    /// this is true they are not the elements written here. The bar prints a
    /// note beside the well and the well lifts off its ground -- the same pair
    /// the pipeline panel's argument boxes use, because it is the same
    /// contract: nothing is applied until the reader commits.
    readonly property bool pending: field.oneLine
        ? (internal.selectionError === ""
           && selection.text !== AppController.selectionText)
        : (field.editable && internal.error === ""
           && body.text !== AppController.sliceText)

    /// The slack asked for after the closing bracket. Enough that the well
    /// reads as a well and is worth clicking into while the slice in it is
    /// short, and not a pixel more: this is space the bar is spending on
    /// nothing. What is actually left after the bracket is slackWidth, which
    /// is this or less.
    readonly property int growingRoom: Theme.s9
    /// What the well keeps for the subscripts when the bar squeezes it: see
    /// minimumUsefulWidth, which is where this is spent. It is a floor for the
    /// *well*, not a width the box is padded out to -- the box itself is only
    /// ever as wide as what it holds.
    readonly property int typingRoom: Theme.s12

    /// The narrowest this is worth drawing at: room to type and the two
    /// brackets. Everything above it goes to the path. Read by the bar, so
    /// the arithmetic stays in the file that knows the parts.
    ///
    /// Never more than the well is asking for, so a short slice on a short
    /// path is not held open wider than the line in it: the surplus could only
    /// fall after the `]`, which is the dead space this file exists to avoid.
    /// The room to grow into is not in it either -- a well being squeezed this
    /// far has already spent that on the subscripts.
    readonly property real minimumUsefulWidth:
        Math.min(field.implicitWidth,
                 Theme.gapM * 2
                 + (field.oneLine ? field.typingRoom
                    : field.editable ? field.typingRoom + field.bracketsWidth
                                     : Theme.s12))

    // --- how the three parts share the well -------------------------------
    /// What the subscripts would like: their own text, and room for the caret
    /// to sit after the last character of it.
    readonly property real bodyWanted: body.implicitWidth + Theme.gapXS
    /// What the path would like, measured off the font rather than off the
    /// label -- because a Text that elides reports the *elided* line as its
    /// implicit width, and a well sized from the label is therefore a latch: a
    /// hair too little room elides the path, the shorter path shrinks the
    /// well, the smaller well elides further, and it settles with room for an
    /// ellipsis and nothing else. The well used to be sized that way and stood
    /// one rounding of its own width away from it. Metrics cannot elide.
    ///
    /// The advance rather than `width`, and the difference is the whole of
    /// what a label needs to not elide. `width` is the tight bounding box of
    /// the glyphs; what a Text lays out to -- its contentWidth -- is the sum
    /// of their advances, and the two are not the same number. On this
    /// project's Linux build "/cube" measures 32 against an advance of
    /// 31.765625, so the bounding box was the larger and the label fitted by
    /// luck. Under Windows' rasteriser the inequality goes the other way: the
    /// box is narrower than the advance, the label is built a fraction too
    /// small for its own text, and it elides on a bar with room to spare.
    ///
    /// Ceiling because a Text's width is whole pixels and an advance is not:
    /// 31.765625 pixels of text does not fit in 31 of label. This is the same
    /// number as before on Linux, and a defensible one everywhere.
    readonly property real pathWanted: Math.ceil(pathMetrics.advanceWidth)
    /// Both brackets: chrome that is never squeezed, whatever else is. None in
    /// one-line mode, where the brackets are the reader's to write and are
    /// therefore inside the box like everything else after the path.
    readonly property real bracketsWidth: (field.editable && !field.oneLine)
        ? openBracket.implicitWidth + closeBracket.implicitWidth : 0
    /// What the one-line box would like: whichever is wider of what is in it
    /// and the hint standing in for it, so an empty box is still a box.
    readonly property real selectionWanted: field.oneLine
        ? Math.max(selection.implicitWidth, selectionHint.implicitWidth) + Theme.gapXS
        : 0
    /// What the line wants: the path, the subscripts, and both brackets. The
    /// slack after the closing bracket is not part of it -- that is room to
    /// grow into rather than something being drawn.
    readonly property real contentWanted: field.pathWanted + field.selectionWanted
        + (field.editable && !field.oneLine
           ? field.bodyWanted + field.bracketsWidth : 0)
    /// The well, less its margins.
    readonly property real innerWidth:
        Math.max(0, field.width - Theme.gapM * 2)
    /// The slack actually left after the closing bracket: the whole step while
    /// the well has room for its line, and nothing at all once the bar has cut
    /// the well below that. Room to grow into is the first thing a well under
    /// pressure can do without, and the last thing worth keeping while the
    /// subscripts are scrolling inside their box for want of it.
    readonly property real slackWidth:
        Math.max(0, Math.min(field.growingRoom,
                             field.innerWidth - field.contentWanted))
    /// The well, less its margins and whatever slack survived.
    readonly property real lineWidth: field.innerWidth - field.slackWidth
    /// The one-line box takes what it needs, and takes it before the path
    /// does: it is the only part of this well anybody can write, and the path
    /// is named in three other places in the window.
    readonly property real selectionWidth: field.oneLine
        ? Math.min(field.selectionWanted, field.lineWidth)
        : 0
    /// What is left for the subscripts and the path, the brackets being
    /// chrome that is drawn whatever else is not.
    readonly property real bodyRoom:
        Math.max(0, field.lineWidth - field.bracketsWidth - field.selectionWidth)
    /// The subscripts take exactly what they need, so the bracket sits against
    /// the last character of them, and the whole of what is left when there is
    /// less than that. There is no floor here: the room to type in is held
    /// open by minimumUsefulWidth, which is what the bar squeezes the well
    /// down to, and a floor applied again in here would only take the space
    /// off the path.
    readonly property real bodyWidth: (field.editable && !field.oneLine)
        ? Math.min(field.bodyWanted, field.bodyRoom)
        : 0
    /// ...and the path takes what is left, which on a bar with room to spare
    /// is the whole of it.
    readonly property real pathWidth:
        Math.max(0, field.lineWidth - field.bodyWidth - field.bracketsWidth
                 - field.selectionWidth)

    implicitHeight: Theme.smallControlHeight
    implicitWidth: Theme.gapM * 2 + field.growingRoom + field.contentWanted
    radius: Theme.radiusS
    color: field.pending ? Theme.surfacePending : Theme.surfaceInset
    border.width: (body.activeFocus || selection.activeFocus || field.error !== "")
                  ? Theme.borderWidthAccent : Theme.borderWidth
    border.color: field.error !== "" ? Theme.warning
                : (body.activeFocus || selection.activeFocus) ? Theme.accent
                : Theme.borderStrong

    QtObject {
        id: internal

        property string error: ""
        property string selectionError: ""
        /// What could be written next in the one-line box, and whether the
        /// reader has waved it away for this moment.
        property var selectionOptions: []
        property bool selectionDismissed: false
    }

    /// The path at its full length, whatever the label is drawing.
    TextMetrics {
        id: pathMetrics

        font: Theme.mono
        text: AppController.currentPath
    }

    /// Put back the line the table is showing, and drop any complaint about
    /// what was in the box. Called whenever the selection or the layout moves
    /// under the box -- including by the box's own commit, which is what
    /// normalises "0:4" to what the table resolved it to.
    function revert() {
        body.text = AppController.sliceText
        selection.text = AppController.selectionText
        internal.error = ""
        internal.selectionError = ""
        internal.selectionOptions = []
    }

    /// The lines that could go in the one-line box, given what is in it.
    /// Arithmetic over the datatype already described, so this costs no read
    /// and can run on every keystroke.
    function refreshSelectionOptions() {
        internal.selectionOptions =
            selection.activeFocus ? AppController.selectionCompletions(selection.text) : []
    }

    /// Apply what has been typed. A line that does not read is left where it
    /// is, in amber, with the reason: throwing away what someone wrote is a
    /// worse answer than telling them why it will not do.
    ///
    /// A line that does read is read back afterwards, because what the table
    /// made of it is the line the bar is now stating: "0:4" over a four-long
    /// axis resolves to ":". The layout change normally puts that back on its
    /// own, but a slice that resolves to the one already showing reports no
    /// change at all -- and without this the well would sit in its unapplied
    /// ground, saying Return had not been pressed, over a table drawn from
    /// exactly what is in it.
    function commit() {
        if (!field.editable || body.text === AppController.sliceText) {
            internal.error = ""
            return
        }
        internal.error = AppController.applySlice(body.text)
        if (internal.error === "")
            body.text = AppController.sliceText
    }

    /// Apply an edited selection line: the subscript and the chain at once.
    ///
    /// Read back afterwards for the slice box's own reason and a louder one: a
    /// subscript written on the chain moves onto the slice in front of it, so
    /// `[:].samples[2]` comes back as `[:, 2].samples`. What was typed has
    /// moved rather than gone, which is the difference between this and a box
    /// that argues.
    function commitSelection() {
        if (!field.oneLine || selection.text === AppController.selectionText) {
            internal.selectionError = ""
            return
        }
        internal.selectionError = AppController.applySelection(selection.text)
        if (internal.selectionError === "")
            selection.text = AppController.selectionText
    }

    Component.onCompleted: field.revert()

    Connections {
        target: AppController
        // Covers a new selection as well as a rearrangement: selecting an
        // object reshapes the setup model, which reports the layout change.
        function onTableLayoutChanged() { field.revert() }
    }

    // Declared before the parts, so it sits beneath them: the box's own press,
    // drag and selection reach the box first, and this catches only what falls
    // past the path, the bracket and the room after it -- none of which
    // handles a click of its own.
    MouseArea {
        anchors.fill: parent
        enabled: field.editable || field.oneLine
        cursorShape: Qt.IBeamCursor
        onClicked: (mouse) => {
            const box = field.oneLine ? selection : body
            box.forceActiveFocus()
            // Where the caret lands says which end was aimed at: before the
            // box, the start of it; after, the end.
            box.cursorPosition = field.mapToItem(box, mouse.x, 0).x <= 0
                                 ? 0 : box.length
        }
    }

    // Anchored rather than laid out: a RowLayout distributes what is left over
    // by its own rules, and which of these parts gives way when the bar is
    // narrow is the one thing this file has an opinion about.
    Item {
        id: line

        anchors.fill: parent
        anchors.leftMargin: Theme.gapM
        anchors.rightMargin: Theme.gapM

        // The object the slice is of. Elided from the left, because the end of
        // a path is the name of the thing and the start of it is the way there.
        Text {
            id: pathLabel

            objectName: "slicePath"

            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: Math.min(field.pathWanted, field.pathWidth)
            text: pathMetrics.text
            font: Theme.mono
            color: Theme.textDisabled
            elide: Text.ElideLeft
            verticalAlignment: Text.AlignVCenter

            HoverHandler { id: pathHover }

            AppToolTip {
                shown: pathLabel.truncated && pathHover.hovered
                verbatim: true
                text: AppController.sliceExpression
            }
        }

        Text {
            id: openBracket

            objectName: "sliceOpenBracket"

            anchors.left: pathLabel.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            visible: field.editable && !field.oneLine
            text: "["
            font: Theme.mono
            color: Theme.textDisabled
            verticalAlignment: Text.AlignVCenter
        }

        TextInput {
            id: body

            objectName: "sliceInput"

            anchors.left: openBracket.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: field.bodyWidth
            visible: field.editable && !field.oneLine
            font: Theme.mono
            // The one thing in this bar that is a value rather than a label,
            // and the only one the reader can change: it gets the reading ink.
            color: Theme.textEmphasis
            selectionColor: Theme.accent
            selectedTextColor: Theme.accentText
            selectByMouse: true
            verticalAlignment: TextInput.AlignVCenter
            clip: true

            onTextEdited: internal.error = AppController.sliceError(body.text)
            onAccepted: field.commit()
            onActiveFocusChanged: if (!body.activeFocus) field.commit()

            Keys.onEscapePressed: {
                field.revert()
                body.focus = false
            }
        }

        Text {
            id: closeBracket

            objectName: "sliceCloseBracket"

            anchors.left: body.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            visible: field.editable && !field.oneLine
            text: "]"
            font: Theme.mono
            color: Theme.textDisabled
            verticalAlignment: Text.AlignVCenter
        }

        // --- the whole selection, for a compound ---------------------------
        //
        // One box holding everything after the path: the brackets, the
        // subscript and the chain. See the note at the top of this file for
        // why a compound is written this way and everything else is not --
        // briefly, a chain and the subscript over the axes it appends are one
        // statement, and two boxes made editing one of them two commits with a
        // shape nobody asked for in between.
        //
        // What stands in it is still a complete slice of the object named
        // beside it, which is the property the pipeline's slice row depends on:
        // that row *is* the slice line, and applying from here goes through
        // AppController.applySelection, which writes the chain's own subscripts
        // onto it exactly as the member box used to.
        Item {
            id: selectionWell

            anchors.left: closeBracket.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: field.selectionWidth
            visible: field.oneLine
            clip: true

            /// What the box would hold if the reader wrote something. Laid out
            /// whatever is in the box, because the well is sized off it: a box
            /// that collapsed to nothing when empty would be a box nobody
            /// could find to type into.
            ///
            /// It names both halves, because both are the reader's to write
            /// here and the chain is the half they have no other way of
            /// learning about.
            Text {
                id: selectionHint

                objectName: "sliceSelectionHint"

                anchors.fill: parent
                visible: selection.text === ""
                text: "[:].member"
                font: Theme.mono
                color: Theme.textDisabled
                verticalAlignment: Text.AlignVCenter
            }

            TextInput {
                id: selection

                objectName: "sliceSelectionInput"

                anchors.fill: parent
                font: Theme.mono
                color: Theme.textEmphasis
                selectionColor: Theme.accent
                selectedTextColor: Theme.accentText
                selectByMouse: true
                verticalAlignment: TextInput.AlignVCenter
                clip: true

                // The contract every box in this window keeps: every keystroke
                // is checked -- against the datatype and the shape already
                // described, which costs no read -- and nothing is applied
                // until the reader commits, so a half-typed line never becomes
                // a selection.
                onTextEdited: {
                    internal.selectionError = AppController.selectionError(selection.text)
                    internal.selectionDismissed = false
                    field.refreshSelectionOptions()
                }
                onAccepted: field.commitSelection()
                onActiveFocusChanged: {
                    if (selection.activeFocus) {
                        // Offered on the way in rather than on the first
                        // keystroke: a compound is exactly the moment a reader
                        // does not know what to write, and the list is the only
                        // place the member names appear.
                        internal.selectionDismissed = false
                        field.refreshSelectionOptions()
                        return
                    }
                    internal.selectionOptions = []
                    field.commitSelection()
                }

                Keys.onEscapePressed: {
                    // The list first, as in the custom plots' entry box: while
                    // it is up, dismissing it is what Escape means.
                    if (selectionCompletion.visible) {
                        internal.selectionDismissed = true
                        return
                    }
                    field.revert()
                    selection.focus = false
                }

                // The members are in the file and nowhere the reader can see
                // them, which is the whole case for completing this box: a
                // nested compound's `.position.x` is otherwise something you
                // have to already know.
                Keys.onTabPressed: (event) => {
                    internal.selectionDismissed = false
                    field.refreshSelectionOptions()
                    event.accepted = selectionCompletion.take()
                }
                // A row the reader moved onto is taken rather than the line
                // applied; with none, Return falls through to onAccepted.
                Keys.onReturnPressed: (event) => {
                    event.accepted = selectionCompletion.takeChosen()
                }
                Keys.onEnterPressed: (event) => {
                    event.accepted = selectionCompletion.takeChosen()
                }
                Keys.onUpPressed: (event) => {
                    event.accepted = selectionCompletion.visible
                    if (event.accepted)
                        selectionCompletion.move(-1)
                }
                Keys.onDownPressed: (event) => {
                    event.accepted = selectionCompletion.visible
                    if (event.accepted)
                        selectionCompletion.move(1)
                }
            }
        }

        // Outside the well rather than inside it, and that is not tidiness:
        // the well clips -- a line longer than the bar can afford has to be cut
        // rather than painted over the note beside it -- and a list declared
        // inside a clipping item is a list nobody can read. `parent` keeps it
        // laid out under the well all the same, which is where it belongs.
        CompletionPopup {
            id: selectionCompletion

            objectName: "sliceSelectionCompletion"

            parent: selectionWell
            options: internal.selectionOptions
            written: selection.text
            visible: internal.selectionOptions.length > 0
                     && selection.activeFocus && !internal.selectionDismissed

            onTaken: (option) => {
                selection.text = option
                selection.cursorPosition = option.length
                internal.selectionError = AppController.selectionError(option)
                field.refreshSelectionOptions()
            }
        }
    }
}
