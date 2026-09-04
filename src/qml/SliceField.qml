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

    /// Why what is in the box cannot be read as a slice, or "" when it can.
    /// The bar prints this beside the field; nothing here has room for it.
    readonly property alias error: internal.error
    /// A scalar is one cell: there are no subscripts, so there is nothing to
    /// type and the line is the path by itself.
    readonly property bool editable: AppController.datasetRank > 0
    /// What is in the box reads as a slice, and is not the slice on screen.
    ///
    /// The whole tab below this line is a picture of some elements, and while
    /// this is true they are not the elements written here. The bar prints a
    /// note beside the well and the well lifts off its ground -- the same pair
    /// the pipeline panel's argument boxes use, because it is the same
    /// contract: nothing is applied until the reader commits.
    readonly property bool pending: field.editable && internal.error === ""
                                    && body.text !== AppController.sliceText

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
                 + (field.editable ? field.typingRoom + field.bracketsWidth
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
    readonly property real pathWanted: pathMetrics.width
    /// Both brackets: chrome that is never squeezed, whatever else is.
    readonly property real bracketsWidth: field.editable
        ? openBracket.implicitWidth + closeBracket.implicitWidth : 0
    /// What the line wants: the path, the subscripts, and both brackets. The
    /// slack after the closing bracket is not part of it -- that is room to
    /// grow into rather than something being drawn.
    readonly property real contentWanted: field.pathWanted
        + (field.editable ? field.bodyWanted + field.bracketsWidth : 0)
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
    /// What is left for the subscripts and the path, the brackets being
    /// chrome that is drawn whatever else is not.
    readonly property real bodyRoom:
        Math.max(0, field.lineWidth - field.bracketsWidth)
    /// The subscripts take exactly what they need, so the bracket sits against
    /// the last character of them, and the whole of what is left when there is
    /// less than that. There is no floor here: the room to type in is held
    /// open by minimumUsefulWidth, which is what the bar squeezes the well
    /// down to, and a floor applied again in here would only take the space
    /// off the path.
    readonly property real bodyWidth: field.editable
        ? Math.min(field.bodyWanted, field.bodyRoom)
        : 0
    /// ...and the path takes what is left, which on a bar with room to spare
    /// is the whole of it.
    readonly property real pathWidth:
        Math.max(0, field.lineWidth - field.bodyWidth - field.bracketsWidth)

    implicitHeight: Theme.smallControlHeight
    implicitWidth: Theme.gapM * 2 + field.growingRoom + field.contentWanted
    radius: Theme.radiusS
    color: field.pending ? Theme.surfacePending : Theme.surfaceInset
    border.width: (body.activeFocus || internal.error !== "")
                  ? Theme.borderWidthAccent : Theme.borderWidth
    border.color: internal.error !== "" ? Theme.warning
                : body.activeFocus ? Theme.accent : Theme.borderStrong

    QtObject {
        id: internal

        property string error: ""
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
        internal.error = ""
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
        enabled: field.editable
        cursorShape: Qt.IBeamCursor
        onClicked: (mouse) => {
            body.forceActiveFocus()
            // Where the caret lands says which end was aimed at: before the
            // subscripts, the start of them; after, the end.
            body.cursorPosition = field.mapToItem(body, mouse.x, 0).x <= 0
                                  ? 0 : body.length
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
            visible: field.editable
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
            visible: field.editable
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
            visible: field.editable
            text: "]"
            font: Theme.mono
            color: Theme.textDisabled
            verticalAlignment: Text.AlignVCenter
        }
    }
}
