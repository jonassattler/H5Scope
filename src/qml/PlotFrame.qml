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
/// drawn, or it is a lie**. Where a value sits is a fraction of the axis it is
/// on, and the renderer computes the same fraction of the same axis;
/// tst_views asserts that this file's yFraction() and the item's agree,
/// because that is the one thing that could quietly drift.
///
/// That rule is also the whole of what a logarithmic axis costs here.
/// fractionOn() is the one function either half of the plot has ever asked,
/// so putting log10 inside it moved the ticks, the rules, the crosshair and
/// the curve at once and left everything between them alone. What a
/// logarithmic axis really does need is the other half of this file: which
/// numbers to print. A round number on a linear axis is a multiple of a round
/// step and on a logarithmic one it is a small multiple of a power of ten,
/// and no amount of arithmetic over spans turns the first into the second --
/// see tickValues() and the block above it.
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

    /// Whether each axis places a value by its logarithm.
    ///
    /// The four bounds above stay in the data's own units either way: what is
    /// being asked for is a different *axis*, not a different window, and the
    /// numbers the reader reads are the numbers the data has. Everything below
    /// that turns a value into a place goes through fractionOn(), which is the
    /// only line in this file that knows which scale it is on -- and the
    /// renderer's own mapping is the same rule in C++ (gui::AxisMapping), which
    /// is what tst_views holds the two to.
    ///
    /// A logarithmic axis whose bounds are not both above zero has no answer
    /// for anything and draws nothing at all; keeping them positive belongs to
    /// whoever chose them, which is PlotSurface.
    property bool xLog: false
    property bool yLog: false

    /// What base each of those logarithms is taken to.
    ///
    /// Ten unless the reader says otherwise. Nothing below assumes it: a
    /// "decade" is the interval from one power of the base to the next, so on
    /// a base-2 axis it is an octave, and the marks in it are the whole
    /// multiples of the power below -- which for base 2 is none, because there
    /// is no whole number strictly between 2^k and 2^(k+1) that is a multiple
    /// of 2^k. That is what every plotting library draws for a base-2 axis and
    /// it falls out of the same rule rather than out of a branch.
    ///
    /// Never at or below one: the logarithm is undefined at one and runs
    /// backwards below it. PlotSurface resolves the reader's choice down to a
    /// base that is above one before it ever reaches here, and the renderer's
    /// own mapping refuses anything else in case something else writes it.
    property real xLogBase: 10.0
    property real yLogBase: 10.0

    /// Whether the subdivisions between two decades carry their digit.
    ///
    /// Off by default, and a setting rather than a rule because it is a trade:
    /// what it buys is that a reader can put a number to a point between two
    /// powers of ten without counting rules, and what it costs is eight more
    /// numbers per decade down an axis that already has one. Which of those
    /// matters depends on what is being read, which is the same argument the
    /// grid's four densities are made of.
    ///
    /// Only where the grid rules more finely than the axis is numbered -- the
    /// numbers go under rules that are drawn, and are read off the list the
    /// rules themselves were drawn from. A number under a rule that is not
    /// there would be a reading of nothing.
    property bool minorNumbers: false

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
        frame.areaHeight)

    /// The pane's height, apart from the rect it is part of. The y axis's
    /// numbers depend on it -- a logarithmic axis numbers as many powers as it
    /// has room for -- and the left gutter depends on those numbers, so a y
    /// axis reading `area.height` would be reading the gutter it sizes. The
    /// height is the top and bottom gutters' business only.
    readonly property real areaHeight: Math.max(1, frame.height - gutterTop - gutterBottom)

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
    /// Measured off the widest tick, where each tick has already measured
    /// itself (labelWidth): a power is written in two faces, `10` in the
    /// axis's and its exponent smaller, so the longest string is no longer
    /// certain to be the widest one. No cycle: a tick's *text* comes from the
    /// view and the height of the pane, and only the x ticks' positions come
    /// from the width this decides.
    ///
    /// The y axis's *name*, when there is one, is added on the outside of
    /// that: it is drawn rotated, so what it costs sideways is a line's height
    /// and not its length. Nothing is reserved for it when the box is empty,
    /// which is the whole of "no wasted space" -- an unnamed axis's pane
    /// starts exactly where it started before this existed.
    readonly property int gutterLeft:
        Math.max(Theme.plotLabelMargin,
                 Math.ceil(frame.widestYLabel) + Theme.gapM)
        + (frame.yLabel === ""
           ? 0 : Math.ceil(axisNameMetrics.height) + Theme.gapXS)

    readonly property real widestYLabel: {
        let widest = 0
        for (let i = 0; i < frame.yTicks.length; ++i)
            widest = Math.max(widest, frame.yTicks[i].width)
        return widest
    }

    /// The two faces a tick is written in, for labelWidth.
    FontMetrics {
        id: tickFontMetrics

        font: Theme.readout
    }

    FontMetrics {
        id: minorFontMetrics

        font: Theme.readoutMinor
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

    /// One subdivision digit, which is the whole of what that face ever sets.
    TextMetrics {
        id: minorMetrics

        font: Theme.readoutMinor
        text: "0"
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
    /// Where `value` sits on an axis running `low`..`high`, as a fraction from
    /// `low`.
    ///
    /// The one piece of arithmetic this whole file is arranged around, and the
    /// only place in it that knows a logarithmic axis exists. The ends of the
    /// axis are the window the surface resolved, with nothing between them and
    /// the data: this is the same subtraction the renderer does, over the same
    /// two numbers and through the same transform, which is what makes a tick
    /// land where the curve did.
    ///
    /// Zero for an axis with no span to divide by, and for a logarithmic one
    /// whose ends are not both above zero -- the same two refusals
    /// gui::mappingOver() makes, written out here because a chrome that
    /// answered a degenerate view differently from the renderer would be the
    /// tick lie in its quietest form.
    function fractionOn(value, low, high, logarithmic, base) {
        if (logarithmic && (!(low > 0) || !(high > 0) || !frame.usableBase(base)))
            return 0
        const from = logarithmic ? frame.logOf(low, base) : low
        const span = (logarithmic ? frame.logOf(high, base) : high) - from
        if (!isFinite(span) || !(span > 0))
            return 0
        return ((logarithmic ? frame.logOf(value, base) : value) - from) / span
    }

    /// Whether `base` is a base at all. The same test gui::mappingOver makes,
    /// and the reason it is a test rather than a clamp is written there.
    function usableBase(base) {
        return isFinite(base) && base > 1
    }

    /// The logarithm of `value` to `base`.
    ///
    /// The two bases a reader is most likely to pick have exact functions of
    /// their own, and they are not the same answer as the division:
    /// `Math.log(1000) / Math.log(10)` is 2.9999999999999996, so a mark that
    /// ought to *be* a power of the base comes out a shade beside one. The
    /// whole structure of this grid is that its majors land exactly on its
    /// labels, so the shade matters.
    function logOf(value, base) {
        if (base === 10)
            return Math.log10(value)
        if (base === 2)
            return Math.log2(value)
        return Math.log(value) / Math.log(base)
    }

    /// Where `value` sits up the pane, as a fraction from the bottom.
    function yFraction(value) {
        return frame.fractionOn(value, frame.viewMinY, frame.viewMaxY,
                                frame.yLog, frame.yLogBase)
    }

    /// ...and along it.
    function xFraction(value) {
        return frame.fractionOn(value, frame.viewMinX, frame.viewMaxX,
                                frame.xLog, frame.xLogBase)
    }

    // --- how a number is spaced and written -------------------------------
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
            if (value === 0)
                return "0"
            // As many figures as it takes to tell this tick from the next one,
            // which is how far the value sits above the span -- the argument
            // decimalsFor makes, in significant figures rather than places.
            // One figure was right only while the value and the span were of a
            // size: between 1.0001 and 1.0002, which is where a logarithmic axis
            // zoomed inside a decade ends up, every tick read "1.0e+0".
            const figures = Math.ceil(Math.log10(Math.abs(value) / width)) + 2
            return value.toExponential(Math.max(1, Math.min(15, figures)))
        }
        return value.toFixed(frame.decimalsFor(span))
    }

    /// A decimal with its trailing zeros taken off, and the point with them.
    ///
    /// An exponent keeps its own part, so "1.00e+7" comes back as "1e+7".
    function trimmed(text) {
        const at = text.indexOf("e")
        const mantissa = at < 0 ? text : text.substring(0, at)
        if (mantissa.indexOf(".") < 0)
            return text
        return mantissa.replace(/\.?0+$/, "") + (at < 0 ? "" : text.substring(at))
    }

    /// One tick on a logarithmic axis, written.
    ///
    /// A span decides nothing here, and that is the difference. On a linear
    /// axis every tick is the same distance from the next and they differ in
    /// their last digits, so what has to be written is exactly as much as tells
    /// two of them apart. On a logarithmic one each tick is a *multiple* of the
    /// one before, so they differ in their magnitudes and never in a trailing
    /// digit -- 0.001 and 100000 are ticks of one axis and each is written the
    /// way it would be written alone.
    ///
    /// Three significant figures, with the zeros a round number leaves taken
    /// off again: every tick this is asked about is a small multiple of a power
    /// of ten and comes back exact, and a band's readout asking about a value
    /// between two of them gets the figures it deserves.
    function logLabelFor(value) {
        if (!(value > 0) || !isFinite(value))
            return String(value)
        const exponent = Math.floor(Math.log10(value))
        // Past either end of what a reader takes in at a glance, a number is an
        // exponent or it is a row of zeros.
        if (exponent >= 6 || exponent < -4)
            return frame.trimmed(value.toExponential(2))
        return frame.trimmed(value.toFixed(Math.max(0, Math.min(8, 2 - exponent))))
    }

    /// A value on an axis running `low`..`high`, written the way that axis
    /// writes its own ticks. The one entry point for anything outside this
    /// file -- see PlotSurface.xNumber, which the band's readout goes through.
    function axisLabel(value, low, high, logarithmic) {
        return logarithmic ? frame.logLabelFor(value)
                           : frame.labelFor(value, high - low)
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
        // Both ends with a billionth of a step to spare, which is less than
        // any tick could be drawn apart by and more than a multiplication can
        // round by: 1 + 5 x 0.0002 is 1.0010000000000001, and without it an
        // axis running 1 to 1.001 lost the tick at its own top.
        const first = Math.ceil(low / step - 1e-9)
        for (let i = 0; i < 512; ++i) {
            const value = (first + i) * step
            if (value > high + step * 1e-9)
                break
            found.push(value)
        }
        return found
    }

    // --- a logarithmic axis: matplotlib's locator ------------------------
    // Which values a logarithmic axis carries, and which of them are numbered,
    // are **matplotlib's answers, ported rather than reasoned out again**:
    // `LogLocator` for the first and `LogFormatterSciNotation` for the second,
    // as they stand in matplotlib 3.11. A reader who puts a figure from here
    // beside one they drew in Python should find the same numbers at the same
    // places, and every time this file reasoned its own way to an answer the
    // answer came out a little different -- a stride of five where matplotlib
    // takes four, a linear axis where matplotlib keeps its multiples, a rule
    // at three halves of an octave that matplotlib never draws.
    //
    // The shape of it:
    //
    // - **Majors are powers of the base**, strided when there are more of them
    //   than the axis has room for. The stride is not a rounding of the count
    //   but a search: the largest number of ticks, no more than was asked
    //   for, that the powers can actually be split into -- which is why
    //   thirty decades are numbered every fourth (eight numbers) and not
    //   every fifth (seven).
    // - **Minors are whole multiples of the power below**, `arange(2, base)` of
    //   them -- 2 through 9 on base ten, nothing on base two, where an octave
    //   doubles straight to the next -- and only while nothing is strided and
    //   fewer than ten powers are crossed. They bunch towards the top of each
    //   power, and that bunching is the shape of the scale.
    // - **Zoomed until at most one of either is in view**, the minors are
    //   replaced by the round values a linear axis would carry. The curve
    //   across such a window is very nearly straight, and an axis with one
    //   number on it says nothing about its scale.
    // - **A power is always numbered; a multiple only on an axis crossing at
    //   most one power** -- a subset of them up to 0.4 of a power across, and
    //   all of them below that (`minor_thresholds=(1, 0.4)`). Past one power
    //   the multiples are the grid's business and not the numbers'.
    //
    // Two departures, both at the edges, and both because matplotlib's answer
    // there is an axis with nothing on it. The linear fallback is taken on any
    // base, where matplotlib takes it only where a power holds two multiples or
    // more -- so base two or e, zoomed inside a power, still has numbers. And
    // the powers are found with a tolerance of 1e-10 on their exponents, so a
    // custom base whose logarithm of its own square comes back as
    // 1.9999999999999998 still marks the square.

    /// matplotlib's `get_tick_space`: how many labels an axis has room for,
    /// from its length.
    ///
    /// Two lines of type per label up the side, and three characters' worth
    /// along the bottom, where a label is as wide as it is long -- matplotlib's
    /// own two constants over its own font size. Clipped to 2..9 the way
    /// `LogLocator` clips it, which is the whole range it ever asks for: never
    /// fewer than two (one number says nothing about a scale) and never more
    /// than nine (a power of ten per label past that is a column of them).
    function logTickRoom(extent, vertical) {
        const per = (vertical ? 2 : 3) * Theme.readout.pixelSize
        const room = Math.floor(Math.max(0, extent) / per)
        return Math.max(2, Math.min(9, room))
    }

    /// Whether `x` is a whole number, to within what a logarithm can be off
    /// by. matplotlib's `_is_close_to_int`, and its tolerance.
    function nearInteger(x) {
        return Math.abs(x - Math.round(x)) < 1e-10
    }

    /// What a power holds: `arange(2, base)`. Empty below three, which is
    /// matplotlib's rule and not an accident of it -- a power of two doubles
    /// straight to the next with no whole multiple of itself between, and e,
    /// which would hold a 2, is under three too.
    function logSubs(base) {
        const subs = []
        if (base < 3)
            return subs
        for (let m = 2; m < base && subs.length < 64; ++m)
            subs.push(m)
        return subs
    }

    /// `LogLocator.tick_values`, over `low`..`high`, asking for about
    /// `request` numbers.
    ///
    /// Returns the majors and the minors *in view*, the stride the majors were
    /// taken at, and whether the minors are the linear fallback. The majors
    /// are built as `base ** exponent` rather than by repeated multiplication,
    /// so that a power of ten is that power of ten exactly and the label it
    /// carries sits on its own rule.
    function logLocate(low, high, base, request) {
        const none = ({ majors: [], minors: [], stride: 1, linear: false })
        if (!(low > 0) || !(high > low) || !frame.usableBase(base))
            return none
        const efmin = frame.logOf(low, base)
        const efmax = frame.logOf(high, base)
        const emin = Math.ceil(efmin - 1e-10)
        const emax = Math.floor(efmax + 1e-10)
        const avail = emax - emin + 1

        // *The number of ticks*: the smallest stride that brings them under
        // what was asked for, and then as many as that stride really gives.
        let wanted = Math.max(2, request)
        let stride = Math.floor(avail / (wanted + 1)) + 1
        const got = Math.ceil(avail / stride)
        if (got <= wanted)
            wanted = got
        let decades = []
        if (wanted <= 0) {
            decades = [emin - 1, emax + 1]
            stride = decades[1] - decades[0]
        } else if (wanted === 1) {
            const mid = Math.round((efmin + efmax) / 2)
            stride = Math.max(mid - (emin - 1), (emax + 1) - mid)
            decades = [mid - stride, mid, mid + stride]
        } else {
            // *The stride*: the largest one that still gives this many, so
            // the unticked space at the ends is as small as it can be...
            stride = Math.floor((avail - 1) / (wanted - 1))
            if (stride < avail / wanted)
                stride = Math.floor(avail / wanted)
            // ...and *the offset*: at a multiple of the stride if that gives
            // the same count, which is what puts 10^0 on the axis whenever it
            // can be; else the smallest offset that does.
            const olo = Math.max(avail - stride * wanted, 0)
            const ohi = Math.min(avail - stride * (wanted - 1), stride)
            let offset = ((-emin) % stride + stride) % stride
            if (!(olo <= offset && offset < ohi))
                offset = olo
            for (let e = emin + offset - stride; e <= emax + stride && decades.length < 1024;
                 e += stride)
                decades.push(e)
        }

        const inView = (v) => v >= low * (1 - 1e-12) && v <= high * (1 + 1e-12)
        const majors = []
        for (let i = 0; i < decades.length; ++i) {
            const v = Math.pow(base, decades[i])
            if (inView(v))
                majors.push(v)
        }

        const subs = avail >= 10 ? [] : frame.logSubs(base)
        const minors = []
        if (subs.length > 0 && (stride === 1 || avail <= 1)) {
            for (let e = emin - 1; e <= emax && minors.length < 4096; ++e) {
                const power = Math.pow(base, e)
                for (let j = 0; j < subs.length; ++j) {
                    const v = subs[j] * power
                    if (inView(v))
                        minors.push(v)
                }
            }
        }

        // One tick or none in view says nothing about the scale, so the
        // minors become the round values a linear axis would have carried.
        //
        // A power still in view stays, and the round values within half a
        // step of it give way to it. On base ten that is only ever the one
        // they coincide with, since a power of ten is a multiple of every
        // linear step inside it; on base two, whose powers are not round
        // decimals, it is what keeps 1.4, 1.49 and 1.5 from being printed on
        // top of one another.
        if (stride === 1 && majors.length + minors.length <= 1) {
            const step = frame.niceStep(high - low, frame.tickTarget)
            const linear = frame.ticksBetween(low, high, step)
            const kept = linear.filter(
                (v) => !majors.some((m) => Math.abs(m - v) < step / 2))
            return ({ majors: majors, minors: kept, stride: stride, linear: true })
        }
        return ({ majors: majors, minors: minors, stride: stride, linear: false })
    }

    /// `LogFormatter.set_locs`: which multiples of a power are numbered on an
    /// axis running `low`..`high`, as the set of whole coefficients allowed.
    ///
    /// One coefficient -- the power itself -- on an axis crossing more than one
    /// power. Under that, a subset spaced evenly in ratio if the axis is more
    /// than 0.4 of a power across (1, 2, 3, 4 and 6 on base ten), and every
    /// whole multiple if it is less.
    function logSublabels(low, high, base) {
        if (!(low > 0) || !(high > low) || !frame.usableBase(base))
            return [1]
        // `math.log(x, b)`, which is a division even for base ten, and not
        // logOf. The difference is an ulp and it is a visible one: log(0.001)
        // over log(10) is -2.9999999999999996, so matplotlib counts 0.001 to
        // 0.01 as crossing one power and numbers its multiples, where 1 to 10,
        // whose logarithms are exact, crosses two and is numbered at its ends.
        // A reader comparing the two figures sees the ulp, so it is kept.
        const lmin = Math.log(low) / Math.log(base)
        const lmax = Math.log(high) / Math.log(base)
        // `floor(nextafter(lmin, -inf))`: a view starting exactly on a power
        // has crossed it.
        const below = Number.isInteger(lmin) ? lmin - 1 : Math.floor(lmin)
        const crossed = Math.floor(lmax) - below
        if (crossed > 1)
            return [1]
        const found = []
        if (lmax - lmin > 0.4) {
            const count = Math.floor(Math.floor(base) / 2) + 1
            for (let i = 0; i < count; ++i) {
                const c = Math.round(Math.pow(base, count > 1 ? i / (count - 1) : 0))
                if (found.indexOf(c) < 0)
                    found.push(c)
            }
            return found
        }
        for (let c = 1; c <= base; ++c)
            found.push(c)
        return found
    }

    /// How a base is written: as a whole number where it is one, `e` where it
    /// is e, and otherwise as the number. matplotlib writes e out to sixteen
    /// digits, which is the one place this does not copy it.
    function baseText(base) {
        if (Math.abs(base - Math.E) < 1e-12)
            return "e"
        return String(base)
    }

    /// A tick on a logarithmic axis, written the way `LogFormatterSciNotation`
    /// writes it: `10³` at a power and `2×10³` at a multiple of one -- or
    /// nothing, where `sublabels` says the multiple is not numbered.
    ///
    /// Returned in two parts, because the exponent is set raised and in the
    /// smaller face (Theme.readoutMinor), which is how mathtext sets it and
    /// what a reader of matplotlib's axes is used to reading. `text` is the
    /// whole of it in one string with the exponent in superscript digits, for
    /// anything that wants a single string: a test, a width, a tooltip.
    ///
    /// `precision` is the significant figures of the coefficient: six, which
    /// is `%g`'s, unless the ticks beside it would not be told apart by six --
    /// see logLabels.
    function logLabel(value, base, sublabels, precision) {
        const fx = Math.log(value) / Math.log(base)
        const decade = frame.nearInteger(fx)
        const exponent = decade ? Math.round(fx) : Math.floor(fx)
        const coeff = Math.round(Math.pow(base, fx - exponent))
        if (sublabels.indexOf(coeff) < 0)
            return null
        const power = frame.baseText(base)
        const sign = exponent < 0 ? "−" : ""
        const digits = String(Math.abs(exponent))
        let lead = ""
        if (!decade) {
            let c = value / Math.pow(base, exponent)
            if (frame.nearInteger(c))
                c = Math.round(c)
            lead = String(Number(c.toPrecision(precision))) + "×"
        }
        const superscript = "⁰¹²³⁴⁵⁶⁷⁸⁹"
        let raised = exponent < 0 ? "⁻" : ""
        for (let i = 0; i < digits.length; ++i)
            raised += superscript[Number(digits[i])]
        return ({ base: lead + power, exponent: sign + digits,
                  text: lead + power + raised })
    }

    /// Every tick in `values` written, with the coefficient given as many
    /// figures as it takes for no two labels to read alike.
    ///
    /// `%g`'s six are what matplotlib uses and are nearly always enough; the
    /// case they are not is a window zoomed so far in that the linear fallback
    /// is stepping in the seventh figure, where matplotlib prints a column of
    /// identical labels. An axis whose numbers do not differ is not numbered.
    function logLabels(values, base, sublabels) {
        let out = []
        for (let precision = 6; precision <= 15; ++precision) {
            out = values.map((v) => frame.logLabel(v, base, sublabels, precision))
            const seen = {}
            let clash = false
            for (let i = 0; i < out.length && !clash; ++i) {
                if (out[i] === null)
                    continue
                clash = seen[out[i].text] === true
                seen[out[i].text] = true
            }
            if (!clash)
                break
        }
        return out
    }

    /// The powers of `factor` between `low` and `high`, anchored at one.
    ///
    /// What a custom step means on a logarithmic axis -- see gridValues. A
    /// factor of one or less is the nonsense a half-typed box resolves to and
    /// draws nothing, exactly as a step of zero does on a linear axis.
    ///
    /// The axis's own base does not enter into it: a factor *is* a base, and
    /// this is the reader stating one for the grid alone. Ten gives the
    /// decades on any axis, and two the doublings.
    function factorsBetween(low, high, factor) {
        const found = []
        if (!(low > 0) || !(high > low) || !(factor > 1))
            return found
        const step = Math.log10(factor)
        const to = Math.log10(high)
        const first = Math.ceil(Math.log10(low) / step - 1e-9)
        for (let i = 0; i < 512; ++i) {
            const exponent = (first + i) * step
            if (exponent > to + 1e-9)
                break
            found.push(Math.pow(10, exponent))
        }
        return found
    }

    // --- which ticks ------------------------------------------------------
    /// The values an axis is numbered at, what each one says, and every value
    /// it carries a tick at whether numbered or not.
    ///
    /// On a linear axis those are one list and the numbers are a round step
    /// apart. On a logarithmic one they are matplotlib's (see logLocate): the
    /// ticks are its majors and its minors, and the numbered ones are the
    /// majors and whichever minors logSublabels allows.
    ///
    /// `logarithmic` in the answer is whether the locator placed the ticks at
    /// powers and their multiples -- false on a linear axis, and false on a
    /// logarithmic one zoomed until the linear fallback took over. The rules
    /// and the band's readout both follow it, so that a pane numbered in round
    /// linear steps is ruled and read in them too.
    ///
    /// `extent` is the length of the axis on the pane, which is what decides
    /// how many numbers it has room for; see logTickRoom.
    function tickValues(low, high, logarithmic, base, extent, vertical) {
        if (logarithmic) {
            const located = frame.logLocate(low, high, base,
                                            frame.logTickRoom(extent, vertical))
            const all = located.majors.concat(located.minors).sort((a, b) => a - b)
            const labels = frame.logLabels(all, base,
                                           frame.logSublabels(low, high, base))
            const values = []
            const texts = []
            for (let i = 0; i < all.length; ++i) {
                if (labels[i] !== null) {
                    values.push(all[i])
                    texts.push(labels[i])
                }
            }
            return ({ values: values, texts: texts, ticks: all,
                      logarithmic: !located.linear, fallback: located.linear })
        }
        const values = frame.ticksBetween(
            low, high, frame.niceStep(high - low, frame.tickTarget))
        return ({ values: values,
                  texts: values.map((v) => ({ base: frame.labelFor(v, high - low),
                                              exponent: "",
                                              text: frame.labelFor(v, high - low) })),
                  ticks: values, logarithmic: false, fallback: false })
    }

    readonly property var xTickValues:
        frame.tickValues(frame.viewMinX, frame.viewMaxX, frame.xLog, frame.xLogBase,
                         frame.area.width, false)
    readonly property var yTickValues:
        frame.tickValues(frame.viewMinY, frame.viewMaxY, frame.yLog, frame.yLogBase,
                         frame.areaHeight, true)

    /// Whether each axis's ticks came out the logarithmic way. Read by the
    /// band's readout, which writes its own numbers the way the ticks beside
    /// it are written -- see PlotSurface.xNumber.
    readonly property bool xLogNumbers: frame.xTickValues.logarithmic
    readonly property bool yLogNumbers: frame.yTickValues.logarithmic

    /// How wide a written tick is: the base in the axis's own face and the
    /// exponent in the smaller one. Measured rather than counted, because the
    /// two parts are set in two sizes and the left gutter has to hold both.
    function labelWidth(written) {
        return tickFontMetrics.advanceWidth(written.base)
               + (written.exponent === ""
                  ? 0 : minorFontMetrics.advanceWidth(written.exponent))
    }

    /// One entry per tick, carrying where it goes as well as what it says.
    ///
    /// The position is computed here rather than in the delegate's binding so
    /// that the whole list is recomputed at once when the window moves: a
    /// delegate reading the frame's view properties one at a time can paint
    /// ticks drawn against the previous window is exactly the lie this file is
    /// arranged to prevent.
    function ticksOf(found, vertical) {
        const out = []
        for (let i = 0; i < found.values.length; ++i) {
            const written = found.texts[i]
            out.push({ at: vertical ? frame.yFraction(found.values[i])
                                    : frame.xFraction(found.values[i]),
                       text: written.text, base: written.base,
                       exponent: written.exponent,
                       width: frame.labelWidth(written) })
        }
        return out
    }

    readonly property var xTicks: frame.ticksOf(frame.xTickValues, false)
    readonly property var yTicks: frame.ticksOf(frame.yTickValues, true)

    // --- which rules ------------------------------------------------------
    /// The values an axis is ruled at, given the reader's own step.
    ///
    /// The four densities say the same thing on either scale: `none` rules
    /// nowhere, `loose` rules where the numbers are, `dense` cuts the same
    /// axis finer, and `custom` rules where the reader says. What differs is
    /// only what a step is made of.
    ///
    /// On a logarithmic axis `dense` is every tick matplotlib's locator
    /// carries -- its majors and its minors, which is `grid(which="both")` --
    /// and `loose` is the numbered ones, which is `grid(which="major")` with
    /// the numbered multiples of an axis inside one power added, since those
    /// are numbers too. See logLocate for when a power has multiples at all:
    /// past ten powers, or once they are strided, it has none, and dense is
    /// the powers.
    ///
    /// `custom` is the reader's own number and is taken as given, including
    /// the nonsense ones: a step of zero or less draws nothing, which is what
    /// a box holding "-" or "1e" resolves to while it is being typed. On a
    /// logarithmic axis that number is read as a **factor** rather than as an
    /// increment, because an increment is the one thing it cannot be there: a
    /// rule every 1 up an axis running to a million is a million rules, and
    /// what a reader means by "every decade" or "every doubling" is a
    /// multiplication. So the rules go at its powers, anchored at one -- and a
    /// factor of ten is the decades, which is the answer that makes the
    /// control's own reading obvious.
    ///
    /// `found` is the axis's own tickValues: the grid is chosen on the scale
    /// the numbers were, so a pane zoomed until the locator fell back to round
    /// linear steps is ruled in linear steps with it.
    function gridValues(low, high, custom, found) {
        if (frame.gridMode === "none")
            return []
        if (frame.gridMode === "custom") {
            return found.logarithmic ? frame.factorsBetween(low, high, custom)
                                     : frame.ticksBetween(low, high, custom > 0 ? custom : 0)
        }
        if (!found.logarithmic) {
            const linear = frame.ticksBetween(
                low, high, frame.niceStep(high - low, frame.gridTarget))
            if (!found.fallback)
                return linear
            // A logarithmic axis in its linear fallback may still have one
            // power in view, numbered, and off the linear step. It is ruled.
            return linear.concat(found.ticks.filter((v) => !frame.holds(linear, v)))
                         .sort((a, b) => a - b)
        }
        return frame.gridMode === "dense" ? found.ticks : found.values
    }

    /// How many rules the grid is aiming for on a linear axis, or on a
    /// logarithmic one zoomed into its linear fallback.
    readonly property int gridTarget: frame.gridMode === "dense"
        ? frame.tickTarget * Theme.plotGridDenseFactor : frame.tickTarget

    // --- the numbers between two of those ---------------------------------
    /// What a subdivision says: how many times the power below it the mark is
    /// -- 2 through 9 on base ten, the single digits log paper prints.
    ///
    /// Cleaned through toPrecision before it is written, because the value was
    /// built by multiplying: 3 x 0.001 over 0.001 is not 3 in doubles, and a
    /// grid line reading "2.9999999999999996" would be this arithmetic showing
    /// through the axis.
    function minorLabel(value, reference) {
        if (!(reference > 0))
            return ""
        const mantissa = Number((value / reference).toPrecision(6))
        return Number.isInteger(mantissa)
            ? String(mantissa) : String(Number(mantissa.toFixed(3)))
    }

    /// The power of `base` at or below `value`.
    function powerBelow(value, base) {
        return Math.pow(base, Math.floor(frame.logOf(value, base) + 1e-9))
    }

    /// Which of the grid's rules carry a digit, and what each one says.
    ///
    /// This application's own addition to matplotlib's axis, and off unless
    /// asked for. matplotlib numbers a power's multiples only on an axis
    /// crossing at most one power, and then in full (`2×10⁰`); past that the
    /// multiples are rules with nothing on them, and this puts their digit
    /// beside them in the smaller face. So it does nothing where matplotlib is
    /// already numbering them: two numbers under one rule would be saying the
    /// same thing twice.
    ///
    /// Taken from what is actually ruled rather than worked out a second time,
    /// so a digit can never appear where there is no line under it. There are
    /// multiples to rule only while every power is numbered (see logLocate:
    /// a strided axis has none), so what each one is measured against is the
    /// power below it, and that power is always one the reader can see.
    ///
    /// As many as there is room for, kept in order from the bottom. That order
    /// is what makes it the *most* there is room for rather than merely a lot:
    /// the candidates are fixed positions, so keeping every one that clears
    /// the last one kept is the largest set that can be kept at all.
    ///
    /// Each has to clear the numbered tick above it as well as whatever was
    /// last drawn below it. One that clears its neighbour and then collides
    /// with the number over it has not fitted, and the mark just under a
    /// numbered tick is where this happens: on base ten the 9 sits a twentieth
    /// of a decade below the next number where the 2 sits three tenths above
    /// the last.
    ///
    /// `gapPixels` is the room two labels need whatever they say, and
    /// `perChar` is what each character of them adds. Down the side of the
    /// pane a label is a line of type tall however long it is, so the first
    /// carries it and the second is nothing; along the bottom a label is as
    /// wide as it is long, and two of them clear each other by half of each.
    function minorNumbersOf(values, found, low, high, base, extent, gapPixels, perChar) {
        const out = []
        if (!frame.minorNumbers || !found.logarithmic || !(extent > 0))
            return out
        // A custom grid rules at the reader's own factor. Those marks are not
        // subdivisions of a power and have nothing to say about one: a factor
        // of two over a decade puts rules at 2, 4 and 8 times it and then at
        // 1.6 times the next, which is arithmetic rather than a reading.
        if (frame.gridMode === "custom")
            return out
        // Every rule is numbered already, so there is nothing here to say --
        // which is also what keeps `loose` out, and `none`, which rules
        // nowhere at all.
        const labels = found.values
        if (labels.length === 0 || values.length <= labels.length)
            return out
        // matplotlib is numbering the multiples itself.
        if (labels.some((v) => !frame.nearInteger(frame.logOf(v, base))))
            return out

        // Where each rule is and what it would say, with the numbered ones
        // carrying their own label so that a digit beside one clears the width
        // of what is actually printed there.
        const spots = []
        const texts = []
        const numbered = []
        for (let i = 0; i < values.length; ++i) {
            let at = -1
            for (let j = 0; j < labels.length && at < 0; ++j) {
                if (frame.holds([labels[j]], values[i]))
                    at = j
            }
            spots.push(frame.fractionOn(values[i], low, high, true, base) * extent)
            texts.push(at >= 0 ? found.texts[at].text
                               : frame.minorLabel(values[i],
                                                  frame.powerBelow(values[i], base)))
            numbered.push(at >= 0)
        }

        // A run that can offer a single number offers none. It would be the
        // same number in every run -- base three has one multiple to a power
        // and would print "2" beside every one of them -- and what it says,
        // that this rule is the one between, the rule says by being there.
        let mostMarks = 0
        let markRun = 0
        for (let i = 0; i < values.length; ++i) {
            markRun = numbered[i] ? 0 : markRun + 1
            mostMarks = Math.max(mostMarks, markRun)
        }
        if (mostMarks < 2)
            return out

        // The next numbered rule above each one, taken in a pass backwards so
        // that the walk forwards can look ahead without searching.
        const above = new Array(values.length).fill(Infinity)
        const aboveText = new Array(values.length).fill("")
        for (let i = values.length - 1; i >= 0; --i) {
            if (numbered[i]) {
                above[i] = spots[i]
                aboveText[i] = texts[i]
            }
            else if (i + 1 < values.length) {
                above[i] = above[i + 1]
                aboveText[i] = aboveText[i + 1]
            }
        }

        const roomFor = (one, other) =>
            gapPixels + perChar * (one.length + other.length) / 2.0

        let lastAt = -Infinity
        let lastText = ""
        for (let i = 0; i < values.length; ++i) {
            if (numbered[i]) {
                lastAt = spots[i]
                lastText = texts[i]
                continue
            }
            if (texts[i] === "")
                continue
            if (spots[i] - lastAt < roomFor(lastText, texts[i]))
                continue
            if (above[i] - spots[i] < roomFor(aboveText[i], texts[i]))
                continue
            out.push({ at: spots[i] / extent, text: texts[i] })
            lastAt = spots[i]
            lastText = texts[i]
        }
        return out
    }

    readonly property var xMinorTicks: frame.minorNumbersOf(
        frame.xGridValues, frame.xTickValues,
        frame.viewMinX, frame.viewMaxX, frame.xLogBase,
        frame.area.width, Theme.gapS, minorMetrics.width)

    readonly property var yMinorTicks: frame.minorNumbersOf(
        frame.yGridValues, frame.yTickValues,
        frame.viewMinY, frame.viewMaxY, frame.yLogBase,
        frame.area.height, Math.ceil(minorMetrics.height), 0)

    /// Whether `value` is one of `values`, to within what two ways of
    /// computing one number can differ by.
    function holds(values, value) {
        for (let i = 0; i < values.length; ++i) {
            const scale = Math.max(Math.abs(values[i]), Math.abs(value))
            if (Math.abs(values[i] - value) <= scale * 1e-9)
                return true
        }
        return false
    }

    /// One entry per rule: where it goes, and whether it is a major.
    ///
    /// A major is a rule that lands on a numbered tick, and it is drawn a step
    /// stronger than the rest. The distinction only exists where the rules
    /// outnumber the labels: under `custom` the reader asked for exactly these
    /// so there is nothing to demote, and on a linear axis `loose` rules at
    /// the labels and has nothing either.
    ///
    /// Tested by counting rather than by naming `dense`. On a logarithmic axis
    /// inside one power the numbered multiples are majors too, and a dense
    /// grid there has more numbers than one per power to be strong at.
    ///
    /// Tested against the numbered values themselves rather than by dividing
    /// by their spacing, which is what this used to do and what a logarithmic
    /// axis has no answer for: its ticks have no one spacing to be divided by.
    /// The comparison is a relative one for the reason the division needed a
    /// tolerance -- a rule at 0.30000000000000004 and a label at 0.3 are the
    /// same rule, and comparing them exactly would draw the one line that
    /// ought to be strongest as the faintest thing on the axis.
    function gridAt(values, labels, vertical) {
        const out = []
        const demote = frame.gridMode !== "custom" && labels.length > 0
                       && values.length > labels.length
        for (let i = 0; i < values.length; ++i) {
            out.push({ at: vertical ? frame.yFraction(values[i])
                                    : frame.xFraction(values[i]),
                       major: demote ? frame.holds(labels, values[i]) : true })
        }
        return out
    }

    /// The values the grid rules at, before they are turned into places.
    ///
    /// Held apart from the rules themselves because the numbered subdivisions
    /// read the same list: a number goes under a rule that is drawn, or it
    /// goes under nothing.
    readonly property var xGridValues: frame.gridValues(
        frame.viewMinX, frame.viewMaxX, frame.gridStepX, frame.xTickValues)

    readonly property var yGridValues: frame.gridValues(
        frame.viewMinY, frame.viewMaxY, frame.gridStepY, frame.yTickValues)

    readonly property var xGrid:
        frame.gridAt(frame.xGridValues, frame.xTickValues.values, false)

    readonly property var yGrid:
        frame.gridAt(frame.yGridValues, frame.yTickValues.values, true)

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
        xLog: frame.xLog
        yLog: frame.yLog
        xLogBase: frame.xLogBase
        yLogBase: frame.yLogBase
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
    //
    // A tick is a number in the axis's own face, and on a logarithmic axis an
    // exponent after it: set in the smaller face and raised, which is how
    // matplotlib's mathtext writes `10³` and what a reader of its axes reads.
    // The two are laid out as one label, so a y label is right-aligned on the
    // whole of it and an x label is centred on the whole of it. On a linear
    // axis the exponent is empty and takes no room.
    component TickLabel: Item {
        required property var modelData

        implicitWidth: power.implicitWidth + raised.implicitWidth
        implicitHeight: power.implicitHeight
        width: implicitWidth
        height: implicitHeight

        Text {
            id: power

            text: parent.modelData.base
            font: Theme.readout
            color: frame.ink
        }

        Text {
            id: raised

            x: power.implicitWidth
            y: power.baselineOffset - baselineOffset - Theme.plotExponentRaise
            text: parent.modelData.exponent
            font: Theme.readoutMinor
            color: frame.ink
        }
    }

    Repeater {
        model: frame.yTicks

        TickLabel {
            x: frame.area.x - width - Theme.gapS
            y: Math.round(frame.area.y + (1 - modelData.at) * frame.area.height)
               - height / 2
        }
    }

    Repeater {
        model: frame.xTicks

        TickLabel {
            x: Math.round(frame.area.x + modelData.at * frame.area.width) - width / 2
            y: frame.area.y + frame.area.height + Theme.s3
        }
    }

    // The digits between two decades, where the reader has asked for them and
    // there is room. Same ink as the numbers above -- this frame has one ink,
    // and a picture whose numbers are two colours is a picture with a design
    // in it -- but two pixels smaller (Theme.readoutMinor), because what these
    // are is not a second set of numbers down the axis. The axis is numbered
    // at its powers of ten; these are marks on the way between two of them,
    // and set at the axis's own size, eight per decade argue with it about
    // which is the scale. Log paper draws the same distinction the same way.
    //
    // Drawn after the numbered ticks and in the same gutters, so a digit is
    // never the thing a power's own label is drawn over -- minorNumbersOf
    // keeps them apart by measuring, and this keeps the order right if it ever
    // fails to.
    Repeater {
        model: frame.yMinorTicks

        Text {
            required property var modelData

            x: frame.area.x - width - Theme.gapS
            y: Math.round(frame.area.y + (1 - modelData.at) * frame.area.height)
               - height / 2
            text: modelData.text
            font: Theme.readoutMinor
            color: frame.ink
            horizontalAlignment: Text.AlignRight
        }
    }

    Repeater {
        model: frame.xMinorTicks

        Text {
            required property var modelData

            x: Math.round(frame.area.x + modelData.at * frame.area.width) - width / 2
            y: frame.area.y + frame.area.height + Theme.s3
            text: modelData.text
            font: Theme.readoutMinor
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
