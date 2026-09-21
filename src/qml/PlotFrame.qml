// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import H5Scope.Backend

/// The plot's frame: the gutters, the rules, the ticks and their labels, with
/// the drawing surface inside them.
///
/// This is the half of a plotting library that is not the plotting. Qt Graphs
/// drew it, and drawing it here is what the move off Qt Graphs costs -- about
/// two hundred lines, against a library that redrew a series to change its
/// colour and segfaulted at ten million points. The tick arithmetic was never
/// Qt Graphs' anyway: niceStep() has been in PlotSurface since the day it
/// turned out the library spaced its ticks from the range an axis *declares*
/// rather than the range it is showing, so a zoomed-in axis kept the spacing of
/// the whole dataset and printed one lonely tick.
///
/// One rule holds this file together: **a tick is drawn where the curve was
/// drawn, or it is a lie**. Both axes are linear and the arithmetic is a
/// subtraction, so both halves of it are a fraction of a span and the renderer
/// computes the same fraction of the same span; tst_views asserts that this
/// file's yFraction() and the item's agree, because that is the one thing that
/// could quietly drift.
Item {
    id: frame

    /// The window being shown, in data coordinates. The surface's zoom and pan
    /// arithmetic resolves to exactly these four numbers.
    property real viewMinX: 0.0
    property real viewMaxX: 1.0
    property real viewMinY: 0.0
    property real viewMaxY: 1.0

    /// How closely the grid is ruled: "none", "loose", "dense" or "custom".
    ///
    /// This was a bool, and a bool is the wrong control for it. A grid is
    /// reading aid rather than decoration -- a reader counting a spike's width
    /// off the rules wants more of them than a reader looking at the shape of
    /// a trace does -- and "on" picked one density for both of them.
    ///
    /// The *numbered* ticks do not move with it. They are what the axis says,
    /// and an axis that prints sixteen labels down a narrow pane because
    /// somebody wanted a finer grid has answered the wrong question. So
    /// `loose` rules at the labels, `dense` adds minor rules between them, and
    /// `custom` rules wherever the reader says -- and all three print the same
    /// numbers.
    property string gridMode: "loose"
    /// Under "custom", how far apart the rules are, in the data's own units.
    /// Zero or less draws nothing on that axis, which is what a half-typed
    /// number in the box resolves to.
    property real gridStepX: 0.0
    property real gridStepY: 0.0
    /// Roughly how many ticks an axis carries.
    property int tickTarget: 8

    /// A title over the pane, and a name for each axis. Empty by default and
    /// empty for most plots: what a dataset is called is already in the slice
    /// bar above it, so these are for the reader who is making a picture to
    /// show somebody else rather than one to read now.
    ///
    /// Each costs room only when it says something -- see gutterTop below.
    property string title: ""
    property string xLabel: ""
    property string yLabel: ""

    /// Punctuation on the lines: a dot at every sample, where the line is
    /// drawn sample for sample rather than summarised.
    property bool markers: false
    property real markerSize: Theme.plotMarkerSize

    /// What the pointer is over, snapped to the nearest drawn sample:
    /// `{ valid, line, x, y, px, py }`, or an invalid reading when the pointer
    /// is elsewhere. Read by whoever wants to say what it is a reading *of* --
    /// this file knows the numbers and not the names.
    readonly property var reading: {
        if (!frame.showCursor)
            return ({ valid: false })
        if (frame.givenReading !== null)
            return frame.givenReading.valid ? frame.placeReading(frame.givenReading)
                                            : ({ valid: false })
        return pointer.hovered
            ? plotLines.nearestSample(pointer.point.position.x,
                                      pointer.point.position.y)
            : ({ valid: false })
    }

    /// A reading handed to this frame rather than taken from its own pointer.
    ///
    /// Null for the frame on screen, which has a pointer over it. The picture
    /// that leaves this application is drawn by a second frame, off screen and
    /// very possibly of another size (PlotPicture.qml), and nobody is pointing
    /// at that one -- so a crosshair asked for under Settings would never be
    /// drawn at all. It is given the reading instead.
    ///
    /// The *sample* is what crosses over, not the place it was drawn: where
    /// that sample lands in pixels is this frame's own arithmetic, and a
    /// picture at twice the width puts it somewhere else.
    property var givenReading: null

    /// That reading with `px` and `py` worked out against this pane.
    ///
    /// Through the item's own xFraction/yFraction, which is the same rule the
    /// ticks go through -- a crosshair drawn where the curve is not would be
    /// the tick lie in another form.
    function placeReading(taken) {
        return ({
            valid: true,
            line: taken.line,
            x: taken.x,
            y: taken.y,
            px: plotLines.xFraction(taken.x) * plotLines.width,
            py: plotLines.height - plotLines.yFraction(taken.y) * plotLines.height
        })
    }


    /// Whether the crosshair is offered at all.
    ///
    /// It used to be unconditional, on the grounds that a control for something
    /// that is only there while you are pointing at it is a control nobody
    /// needs. That is true of the crosshair and not true of the *readout* it
    /// produces, which is a line of numbers in the bar below the plot and is
    /// therefore something a reader may reasonably want to stop changing.
    property bool showCursor: true

    // --- the colours this frame draws in ---------------------------------
    // Properties with Theme defaults rather than direct reads of Theme, and
    // the defaults below are exactly what was written in place before -- so
    // nothing on screen changes by their existing.
    //
    // What they buy is the *other* frame. A plot exported for publication is
    // drawn on paper: a white or transparent ground, black ink, the light
    // scope's rules -- while the reader who asked for it is very likely
    // sitting in the dark theme. `Theme.dark` is a singleton the whole window
    // is bound to and cannot be flipped for the length of a grab without
    // buying the picture with a frame of the application in the wrong colours,
    // so the picture is drawn by a second frame, off screen, with these set
    // (see PlotPicture.qml). A frame that read Theme directly could not be
    // told to do that.
    /// The ground the whole picture stands on. `"transparent"` is a legal
    /// answer and is what a publication export gives, so that the page's own
    /// colour shows through.
    property color ground: Theme.surfaceInset
    /// Everything written on that ground: the tick numbers, the axis names and
    /// the title. One property for all three because they are one thing --
    /// what this picture says about itself -- and a picture whose title and
    /// numbers are different colours is a picture with a design in it.
    property color ink: Theme.plotInk
    /// The grid: a rule between two numbered ticks, and a rule at one.
    property color ruleMinor: Theme.border
    property color ruleMajor: Theme.borderStrong
    /// The two axes themselves, and the crosshair drawn against them.
    property color axisRule: Theme.borderGuide
    /// The ring around the sample the crosshair has snapped to. The accent,
    /// which is what this application says "this is the one" in -- except on
    /// paper, where there is no rest of the application for it to mean it
    /// against and it is simply ink.
    property color cursorInk: Theme.accent

    /// Whether the pointer is over the pane -- the lines themselves, not the
    /// gutters the labels live in.
    ///
    /// The same HoverHandler the crosshair reads, handed out because Ctrl+C
    /// copies the plot "while the mouse is inside it" and this is where that
    /// is known. Not guarded by showCursor: turning the readout off says
    /// nothing about where the pointer is.
    readonly property alias hovered: pointer.hovered

    /// The item the lines are drawn on. Handed out so that whoever owns the
    /// data can fill it; this file knows nothing about what is in it.
    readonly property alias lines: plotLines

    /// What that item is called, so that a second frame built for the picture
    /// does not answer to the name the one on screen is found by.
    property string linesObjectName: "plotLines"

    /// The plot area, in this item's coordinates: the frame minus the gutters
    /// the labels live in. Every gesture is measured against it, because a
    /// fraction of the whole item is not a fraction of the axis.
    readonly property rect area: Qt.rect(
        gutterLeft, gutterTop,
        Math.max(1, frame.width - gutterLeft - gutterRight),
        Math.max(1, frame.height - gutterTop - gutterBottom))

    // Room where the labels actually are. These were once the other way round
    // -- air on the top and right, nothing on the left and bottom -- which left
    // the y axis with no width to print a number in and so with no numbers at
    // all, and cut the x axis's first tick off at the frame. The right-hand
    // gutter is room for half of the last x label, which sits centred on the
    // axis's right end: at a gap's worth of margin half of it fell off.
    /// Wide enough for the numbers it has to hold, and never narrower than the
    /// token that used to stand here alone.
    ///
    /// A fixed margin was right for as long as the axis printed four
    /// characters. Point it at a trace of sixteen-bit samples and the ticks
    /// read -32000, which is six -- and what a reader saw was "0000" down the
    /// side of the plot, four times, with the sign and the leading digits cut
    /// off at the frame. A label that is clipped is worse than no label: it is
    /// a number, and it is the wrong one.
    ///
    /// Measured off the widest tick rather than off all of them because
    /// Theme.readout is monospaced, so the longest string is the widest one.
    /// No cycle: a tick's *text* comes from the view, and only its position
    /// comes from the pane this decides the size of.
    ///
    /// The y axis's *name*, when there is one, is added on the outside of
    /// that: it is drawn rotated, so what it costs sideways is a line's height
    /// and not its length. Nothing is reserved for it when the box is empty,
    /// which is the whole of "no wasted space" -- an unnamed axis's pane
    /// starts exactly where it started before this existed.
    readonly property int gutterLeft:
        Math.max(Theme.plotLabelMargin,
                 Math.ceil(yLabelMetrics.width) + Theme.gapM)
        + (frame.yLabel === ""
           ? 0 : Math.ceil(axisNameMetrics.height) + Theme.gapXS)

    readonly property string widestYLabel: {
        let widest = ""
        for (let i = 0; i < frame.yTicks.length; ++i) {
            if (frame.yTicks[i].text.length > widest.length)
                widest = frame.yTicks[i].text
        }
        return widest
    }

    TextMetrics {
        id: yLabelMetrics

        font: Theme.readout
        text: frame.widestYLabel
    }

    readonly property int gutterRight: Theme.s9
    /// Air, plus a line for the title when there is one.
    readonly property int gutterTop: Theme.gapS
        + (frame.title === ""
           ? 0 : Math.ceil(titleMetrics.height) + Theme.gapS)
    /// Measured rather than stated, because it has to hold a line of type and
    /// the token that used to stand here does not know how tall one is. Qt
    /// Graphs drew its own labels inside its own margin and got away with
    /// plotMargin exactly; drawn here, the same number put the last two pixels
    /// of every x label past the bottom of the frame.
    ///
    /// The axis's name goes under the numbers, and only when it has one.
    readonly property int gutterBottom: Math.ceil(tickMetrics.height) + Theme.gapS
        + (frame.xLabel === ""
           ? 0 : Math.ceil(axisNameMetrics.height) + Theme.gapXS)

    TextMetrics {
        id: tickMetrics

        font: Theme.readout
        text: "0.0"
    }

    // The two faces the title and the axis names are set in, measured off a
    // line of type rather than off the reader's own words: what decides the
    // gutter is how tall a line is, and every line in one face is as tall as
    // every other. Measuring the strings themselves would make the pane's
    // geometry depend on whether the title happens to carry a descender.
    TextMetrics {
        id: titleMetrics

        font: Theme.bodyStrong
        text: "Ag"
    }

    TextMetrics {
        id: axisNameMetrics

        font: Theme.bodySmall
        text: "Ag"
    }

    // --- where a value sits ----------------------------------------------
    /// Where `value` sits up the pane, as a fraction from the bottom.
    ///
    /// The bottom and the top of the axis are the window the surface resolved,
    /// with nothing between them and the data: this is the same subtraction the
    /// renderer does, over the same two numbers, which is what makes a tick land
    /// where the curve did.
    function yFraction(value) {
        const span = frame.viewMaxY - frame.viewMinY
        return span > 0 ? (value - frame.viewMinY) / span : 0
    }

    /// ...and along it.
    function xFraction(value) {
        const span = frame.viewMaxX - frame.viewMinX
        return span > 0 ? (value - frame.viewMinX) / span : 0
    }

    // --- which ticks ------------------------------------------------------
    /// A round tick spacing giving roughly `target` ticks across `span`: 1, 2
    /// or 5 times a power of ten, which is what every axis in every plotting
    /// library settles on and what a reader can add up in their head.
    function niceStep(span, target) {
        if (!(span > 0))
            return 0
        const raw = span / Math.max(1, target)
        const magnitude = Math.pow(10, Math.floor(Math.log(raw) / Math.LN10))
        const scaled = raw / magnitude
        return magnitude * (scaled <= 1 ? 1 : scaled <= 2 ? 2 : scaled <= 5 ? 5 : 10)
    }

    /// Enough decimals to tell two ticks apart, and no more: a span of 100
    /// wants none, a span of a thousandth wants five. A fixed count prints
    /// either noise or "0.0" all the way up the axis. Taken off the visible
    /// span, so zooming in adds digits as the ticks close up.
    function decimalsFor(span) {
        const width = Math.abs(span)
        if (!(width > 0))
            return 0
        return Math.max(0, Math.min(6,
            Math.ceil(-Math.log(width) / Math.LN10) + 2))
    }

    /// One tick, written.
    ///
    /// The *span* decides whether to use an exponent, not the value. That is
    /// the distinction that matters on an axis with a large offset and a small
    /// range -- a time base of seconds since 1970 spanning five minutes -- where
    /// every tick rounds to 1.7e+9 and an axis of eight identical labels says
    /// nothing at all. Judged by the span, that axis prints whole seconds and
    /// the ticks differ; an axis that really does cross decades gets the
    /// exponent, and its ticks differ too.
    function labelFor(value, span) {
        const width = Math.abs(span)
        if (width >= 1e6 || (width > 0 && width < 1e-4)) {
            // Zero written as an exponent is "0.0e+0", which is six characters
            // that mean nothing the first one did not.
            return value === 0 ? "0" : value.toExponential(1)
        }
        return value.toFixed(frame.decimalsFor(span))
    }

    /// The round values in `low`..`high`, stepping by `step`.
    ///
    /// Counted from a first tick rather than accumulated, because adding a
    /// step to itself a hundred times is a hundred roundings and the last tick
    /// comes out at 99.99999999999999. The ceiling on the count is not a
    /// design choice -- it is what stops a span that has gone to zero or NaN
    /// under a bad binding from asking for an unbounded Repeater.
    function ticksBetween(low, high, step) {
        const found = []
        if (!(step > 0) || !(high > low))
            return found
        const first = Math.ceil(low / step)
        for (let i = 0; i < 512; ++i) {
            const value = (first + i) * step
            if (value > high)
                break
            found.push(value)
        }
        return found
    }

    /// One entry per tick, carrying where it goes as well as what it says.
    ///
    /// The position is computed here rather than in the delegate's binding so
    /// that a tick depends on nothing but its own entry and the pane: a
    /// delegate that reached back out for the view would be re-evaluated in
    /// whatever order the bindings happened to settle in, and half a frame of
    /// ticks drawn against the previous window is exactly the lie this file is
    /// arranged to prevent.
    readonly property var xTicks: {
        const step = frame.niceStep(frame.viewMaxX - frame.viewMinX, frame.tickTarget)
        const values = frame.ticksBetween(frame.viewMinX, frame.viewMaxX, step)
        const span = frame.viewMaxX - frame.viewMinX
        const out = []
        for (let i = 0; i < values.length; ++i) {
            out.push({ at: frame.xFraction(values[i]),
                       text: frame.labelFor(values[i], span) })
        }
        return out
    }

    readonly property var yTicks: {
        const out = []
        const span = frame.viewMaxY - frame.viewMinY
        const step = frame.niceStep(span, frame.tickTarget)
        const values = frame.ticksBetween(frame.viewMinY, frame.viewMaxY, step)
        for (let i = 0; i < values.length; ++i) {
            out.push({ at: frame.yFraction(values[i]),
                       text: frame.labelFor(values[i], span) })
        }
        return out
    }

    // --- which rules ------------------------------------------------------
    /// How far apart the rules go on an axis spanning `low`..`high`, or 0 for
    /// an axis that is not ruled. `custom` is the reader's own number and is
    /// taken as given, including the nonsense ones: a step of zero or less
    /// draws nothing, which is what a box holding "-" or "1e" resolves to
    /// while it is being typed.
    function gridStep(low, high, custom) {
        const span = high - low
        if (frame.gridMode === "none")
            return 0
        if (frame.gridMode === "custom")
            return custom > 0 ? custom : 0
        if (frame.gridMode === "dense")
            return frame.niceStep(span, frame.tickTarget * Theme.plotGridDenseFactor)
        return frame.niceStep(span, frame.tickTarget)
    }

    /// One entry per rule: where it goes, and whether it is a major.
    ///
    /// A major is a rule that lands on a numbered tick, and it is drawn a step
    /// stronger than the rest. That distinction only exists under `dense`,
    /// where the rules outnumber the labels -- under `loose` every rule is on
    /// a label and under `custom` the reader asked for exactly these, so
    /// neither has anything to demote.
    ///
    /// The multiple is tested with a tolerance because the two steps are
    /// computed separately: a rule at 0.30000000000000004 and a label at 0.3
    /// are the same rule, and comparing them exactly would draw the one line
    /// that ought to be strongest as the faintest thing on the axis.
    function gridAt(low, high, step, labelStep, vertical) {
        const out = []
        if (!(step > 0))
            return out
        const values = frame.ticksBetween(low, high, step)
        const minor = frame.gridMode === "dense" && labelStep > 0
        for (let i = 0; i < values.length; ++i) {
            const on = minor
                ? Math.abs(values[i] / labelStep
                           - Math.round(values[i] / labelStep)) < 1e-6
                : true
            out.push({ at: vertical ? frame.yFraction(values[i])
                                    : frame.xFraction(values[i]),
                       major: on })
        }
        return out
    }

    readonly property var xGrid: frame.gridAt(
        frame.viewMinX, frame.viewMaxX,
        frame.gridStep(frame.viewMinX, frame.viewMaxX, frame.gridStepX),
        frame.niceStep(frame.viewMaxX - frame.viewMinX, frame.tickTarget), false)

    readonly property var yGrid: frame.gridAt(
        frame.viewMinY, frame.viewMaxY,
        frame.gridStep(frame.viewMinY, frame.viewMaxY, frame.gridStepY),
        frame.niceStep(frame.viewMaxY - frame.viewMinY, frame.tickTarget), true)

    // --- the drawing ------------------------------------------------------
    // The ground, drawn here rather than only behind the whole surface.
    //
    // It is the same colour the surface already stands on, so on screen this
    // changes nothing at all. What it buys is that the frame is a complete
    // picture by itself: "copy plot" grabs this item, and an item whose ground
    // is a sibling behind it grabs as strokes and labels on transparency --
    // which is what lands in the clipboard and what a reader pastes.
    Rectangle {
        anchors.fill: parent
        color: frame.ground
    }

    // A step stronger than a table's rules, for the same reason the table's own
    // went up: this plot's ground is the inset, which is true black, and a
    // hairline at line-1 against it is a line nobody can see. The axis rules go
    // a step further again, so the frame reads as the frame.
    //
    // A minor rule -- one the dense grid puts between two numbered ticks --
    // goes back down a step. It is subdivision rather than structure, and at
    // the majors' weight a dense grid reads as a hatch with a curve somewhere
    // in it.
    Repeater {
        model: frame.yGrid

        Rectangle {
            required property var modelData

            x: frame.area.x
            y: Math.round(frame.area.y + (1 - modelData.at) * frame.area.height)
            width: frame.area.width
            height: Theme.hairline
            color: modelData.major ? frame.ruleMajor : frame.ruleMinor
        }
    }

    Repeater {
        model: frame.xGrid

        Rectangle {
            required property var modelData

            x: Math.round(frame.area.x + modelData.at * frame.area.width)
            y: frame.area.y
            width: Theme.hairline
            height: frame.area.height
            color: modelData.major ? frame.ruleMajor : frame.ruleMinor
        }
    }

    // The rules the readings are read against, which are the frame itself.
    //
    // All four sides, and that is the whole of what this is. Two of them --
    // the left and the bottom -- are the axes the numbers are printed beside,
    // and for a long time they were the only ones drawn. What the other two
    // buy is that the pane has an *edge*: a trace that runs out of the window
    // at the top now stops at a rule instead of fading into the gutter, and a
    // reader can see where the window ends without reading a label to find
    // out. It is what every plotting library draws by default and what a
    // figure on a page looks like.
    //
    // One rectangle with a border rather than four hairlines, because four
    // items is four chances for one of them to be a pixel off the others. Its
    // border is drawn *inside* its bounds, so it is a hairline wider and
    // taller than the pane: that puts the left rule on the pane's first
    // column and the bottom rule on the row just below it, which is exactly
    // where each of them was drawn when they were two separate items.
    Rectangle {
        x: frame.area.x
        y: frame.area.y
        width: frame.area.width + Theme.hairline
        height: frame.area.height + Theme.hairline
        color: "transparent"
        border.width: Theme.hairline
        border.color: frame.axisRule
    }

    /// The lines. Clipped, so a zoomed-in stroke stops at the frame rather
    /// than being drawn across the axis labels.
    PlotItem {
        id: plotLines

        objectName: frame.linesObjectName

        x: frame.area.x
        y: frame.area.y
        width: frame.area.width
        height: frame.area.height
        clip: true

        xMin: frame.viewMinX
        xMax: frame.viewMaxX
        yMin: frame.viewMinY
        yMax: frame.viewMaxY
        markers: frame.markers
        markerSize: frame.markerSize

        // Inside the item the lines are in, so a pointer position is already
        // in the coordinates nearestSample answers about.
        HoverHandler {
            id: pointer
        }
    }

    // --- the reading under the pointer ------------------------------------
    // A crosshair that snaps to a sample rather than following the pointer
    // freely. A plot is a picture of measurements that were taken, and a
    // readout of the space between two of them is a reading of something
    // nobody measured -- the same argument the time base makes about
    // interpolating an x, and the gap makes about a value that would not read.
    //
    // Clipped to the pane, and that is not tidiness. The sample nearest the
    // pointer need not be on screen: point at a zoomed-in trace just below a
    // spike and the nearest sample is the top of it, a thousand pixels above
    // the frame -- and the rule through it was drawn there, across the tab bar.
    // Clipping answers every case of that at once and answers it correctly,
    // because each part of the crosshair then appears exactly when what it
    // marks is visible. A sample above the pane keeps its vertical rule, which
    // marks an x that *is* on screen, and loses the horizontal one and the
    // ring, which mark a y and a point that are not.
    Item {
        objectName: "plotCursor"

        x: frame.area.x
        y: frame.area.y
        width: frame.area.width
        height: frame.area.height
        clip: true
        visible: frame.reading.valid

        Rectangle {
            y: Math.round(frame.reading.py)
            width: parent.width
            height: Theme.hairline
            color: frame.axisRule
        }

        Rectangle {
            x: Math.round(frame.reading.px)
            width: Theme.hairline
            height: parent.height
            color: frame.axisRule
        }

        /// The sample itself, so the reader can see which one was taken.
        Rectangle {
            width: Theme.plotMarkerSize + Theme.borderWidthAccent
            height: width
            radius: width / 2
            x: frame.reading.px - width / 2
            y: frame.reading.py - height / 2
            color: "transparent"
            border.width: Theme.borderWidthAccent
            border.color: frame.cursorInk
        }
    }

    // --- the labels -------------------------------------------------------
    // Drawn last, so that nothing is drawn over a number. They sit in the
    // gutters and never inside the pane, which is what the gutters are for.
    Repeater {
        model: frame.yTicks

        Text {
            required property var modelData

            anchors.right: undefined
            x: frame.area.x - width - Theme.gapS
            y: Math.round(frame.area.y + (1 - modelData.at) * frame.area.height)
               - height / 2
            text: modelData.text
            font: Theme.readout
            color: frame.ink
            horizontalAlignment: Text.AlignRight
        }
    }

    Repeater {
        model: frame.xTicks

        Text {
            required property var modelData

            x: Math.round(frame.area.x + modelData.at * frame.area.width) - width / 2
            y: frame.area.y + frame.area.height + Theme.s3
            text: modelData.text
            font: Theme.readout
            color: frame.ink
            horizontalAlignment: Text.AlignHCenter
        }
    }

    // --- what the reader called it ----------------------------------------
    // The title and the two axis names. Each sits in the room its own gutter
    // grew for it, so none of them can be drawn over a number -- and when the
    // box is empty the gutter never grew and there is nothing here at all.
    //
    // All three elide over the pane's width rather than wrapping. A plot is a
    // fixed rectangle and a title that took three lines would take them out of
    // the picture; the pointer reaches the whole of it, which is the contract
    // every elided thing in this application keeps.
    Text {
        id: titleText

        x: frame.area.x
        y: Theme.gapS
        width: frame.area.width
        visible: frame.title !== ""
        text: frame.title
        font: Theme.bodyStrong
        color: frame.ink
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideRight

        HoverHandler { id: titleHover }

        AppToolTip {
            shown: titleHover.hovered && titleText.truncated
            verbatim: true
            text: frame.title
        }
    }

    Text {
        id: xNameText

        x: frame.area.x
        y: frame.area.y + frame.area.height + Theme.s3
           + Math.ceil(tickMetrics.height) + Theme.gapXS
        width: frame.area.width
        visible: frame.xLabel !== ""
        text: frame.xLabel
        font: Theme.bodySmall
        color: frame.ink
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideRight

        HoverHandler { id: xNameHover }

        AppToolTip {
            shown: xNameHover.hovered && xNameText.truncated
            verbatim: true
            text: frame.xLabel
        }
    }

    // Turned a quarter, reading upwards, which is where every plot in every
    // field puts the name of a y axis. Rotation about the item's own centre,
    // so the arithmetic is "put the centre where the middle of the pane's left
    // edge is": the box is laid out as wide as the pane is tall and then
    // turned, which is also what gives `elide` the right width to measure
    // against.
    Text {
        id: yNameText

        width: frame.area.height
        x: Math.ceil(axisNameMetrics.height) / 2 - width / 2
        y: frame.area.y + frame.area.height / 2 - height / 2
        rotation: -90
        visible: frame.yLabel !== ""
        text: frame.yLabel
        font: Theme.bodySmall
        color: frame.ink
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideRight

        HoverHandler { id: yNameHover }

        AppToolTip {
            shown: yNameHover.hovered && yNameText.truncated
            verbatim: true
            text: frame.yLabel
        }
    }
}
