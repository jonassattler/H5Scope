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

    property bool showGrid: true
    /// Roughly how many ticks an axis carries.
    property int tickTarget: 8

    /// Punctuation on the lines: a dot at every sample, where the line is
    /// drawn sample for sample rather than summarised.
    property bool markers: false
    property real markerSize: Theme.plotMarkerSize

    /// What the pointer is over, snapped to the nearest drawn sample:
    /// `{ valid, line, x, y, px, py }`, or an invalid reading when the pointer
    /// is elsewhere. Read by whoever wants to say what it is a reading *of* --
    /// this file knows the numbers and not the names.
    readonly property var reading: (frame.showCursor && pointer.hovered)
        ? plotLines.nearestSample(pointer.point.position.x,
                                  pointer.point.position.y)
        : ({ valid: false })


    /// Whether the crosshair is offered at all.
    ///
    /// It used to be unconditional, on the grounds that a control for something
    /// that is only there while you are pointing at it is a control nobody
    /// needs. That is true of the crosshair and not true of the *readout* it
    /// produces, which is a line of numbers in the bar below the plot and is
    /// therefore something a reader may reasonably want to stop changing.
    property bool showCursor: true

    /// The item the lines are drawn on. Handed out so that whoever owns the
    /// data can fill it; this file knows nothing about what is in it.
    readonly property alias lines: plotLines

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
    readonly property int gutterLeft:
        Math.max(Theme.plotLabelMargin,
                 Math.ceil(yLabelMetrics.width) + Theme.gapM)

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
    readonly property int gutterTop: Theme.gapS
    /// Measured rather than stated, because it has to hold a line of type and
    /// the token that used to stand here does not know how tall one is. Qt
    /// Graphs drew its own labels inside its own margin and got away with
    /// plotMargin exactly; drawn here, the same number put the last two pixels
    /// of every x label past the bottom of the frame.
    readonly property int gutterBottom: Math.ceil(tickMetrics.height) + Theme.gapS

    TextMetrics {
        id: tickMetrics

        font: Theme.readout
        text: "0.0"
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

    // --- the drawing ------------------------------------------------------
    // A step stronger than a table's rules, for the same reason the table's own
    // went up: this plot's ground is the inset, which is true black, and a
    // hairline at line-1 against it is a line nobody can see. The axis rules go
    // a step further again, so the frame reads as the frame.
    Repeater {
        model: frame.showGrid ? frame.yTicks : []

        Rectangle {
            required property var modelData

            x: frame.area.x
            y: Math.round(frame.area.y + (1 - modelData.at) * frame.area.height)
            width: frame.area.width
            height: Theme.hairline
            color: Theme.borderStrong
        }
    }

    Repeater {
        model: frame.showGrid ? frame.xTicks : []

        Rectangle {
            required property var modelData

            x: Math.round(frame.area.x + modelData.at * frame.area.width)
            y: frame.area.y
            width: Theme.hairline
            height: frame.area.height
            color: Theme.borderStrong
        }
    }

    // The two rules the readings are read against, which are the frame itself.
    Rectangle {
        x: frame.area.x
        y: frame.area.y
        width: Theme.hairline
        height: frame.area.height
        color: Theme.borderGuide
    }

    Rectangle {
        x: frame.area.x
        y: frame.area.y + frame.area.height
        width: frame.area.width
        height: Theme.hairline
        color: Theme.borderGuide
    }

    /// The lines. Clipped, so a zoomed-in stroke stops at the frame rather
    /// than being drawn across the axis labels.
    PlotItem {
        id: plotLines

        objectName: "plotLines"

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
            color: Theme.borderGuide
        }

        Rectangle {
            x: Math.round(frame.reading.px)
            width: Theme.hairline
            height: parent.height
            color: Theme.borderGuide
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
            border.color: Theme.accent
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
            color: Theme.textSecondary
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
            color: Theme.textSecondary
            horizontalAlignment: Text.AlignHCenter
        }
    }
}
