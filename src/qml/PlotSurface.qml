// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import H5Scope.Backend

/// The Data Viewer's plot presentation: every row of the table as one line,
/// with x running along the columns.
///
/// Which values those are is the data settings panel's business, not this
/// file's -- this is one more reading of exactly the table the grid shows, so
/// a rank-4 dataset plots on the same terms it browses.
///
/// The points never cross into QML. AppController.datasetPlot samples the file
/// and hands every drawn line to the PlotFrame's item in one call -- a pointer
/// into the cache it already holds, plus the arithmetic that puts a sample at
/// an x. Nothing is built on the way and nothing is copied.
///
/// This surface used to draw through Qt Graphs, and most of what is below is
/// older than that decision: the zoom and pan arithmetic, the two-of-three x
/// axis, the colour cycles, the legend and the settings are all
/// library-agnostic and none of them moved. What went is the graph itself, the
/// Loader that existed only to defeat the library holding on to what a series
/// last drew, and the re-fill that existed only to make a recoloured line
/// redraw. See docs on PlotFrame.qml for what replaced the chrome.
Item {
    id: surface

    // --- settings, written by PlotSettingsPanel -------------------------
    /// How closely the grid is ruled: "none", "loose", "dense" or "custom",
    /// and under "custom" how far apart the rules go on each axis. See the
    /// note on PlotFrame.gridMode, which is where the four are drawn.
    property string gridMode: "loose"
    property real gridStepX: 0.0
    property real gridStepY: 0.0
    /// Whether each axis places a value by its logarithm.
    ///
    /// A property of the *axis* and not of the window: the four bounds stay in
    /// the data's own units, the ticks print the numbers the data has, and what
    /// changes is how far apart two of them are drawn. See PlotFrame.fractionOn
    /// and gui::AxisMapping, which are the one rule in its two implementations.
    ///
    /// Two things follow from it here and nowhere else. The gestures are
    /// arithmetic over an axis's span, so on a logarithmic axis they are
    /// arithmetic over decades -- see axisPosition. And a logarithmic axis has
    /// to start above zero, so its low end is the smallest positive value there
    /// is to show rather than the one the linear axis would have used; the
    /// default x axis is the element's own index and starts at zero, so without
    /// that every plot in the application would go blank the moment the box was
    /// ticked.
    property bool xLog: false
    property bool yLog: false

    /// Which base each of those logarithms is taken to: "10", "2", "e", or
    /// "custom" and then the number beside it.
    ///
    /// The shape the grid's own density already has (`gridMode` and
    /// `gridStepX`), and for the same reason: a list of the answers worth
    /// having, and a box for the one nobody could have listed. It has to be
    /// two properties rather than one number, because "custom" is a state and
    /// not a value -- a single settable base would spring the dropdown back to
    /// "base 10" the moment the reader picked custom over a base of ten, and
    /// the box they were about to type in would never appear.
    ///
    /// Ten by default, which is what every axis in the application was before
    /// the choice existed.
    property string xLogBaseMode: "10"
    property string yLogBaseMode: "10"
    /// The base under "custom". Read only while that is what is chosen, and
    /// only ever when it is a base at all -- see baseFor.
    property real xLogBaseCustom: 10.0
    property real yLogBaseCustom: 10.0

    /// What each axis's logarithm is actually taken to.
    ///
    /// The one number everything downstream reads, and it is derived rather
    /// than settable so that there is no way to be looking at an axis drawn to
    /// a base the controls do not name.
    readonly property real xLogBase:
        surface.baseFor(surface.xLogBaseMode, surface.xLogBaseCustom)
    readonly property real yLogBase:
        surface.baseFor(surface.yLogBaseMode, surface.yLogBaseCustom)

    /// Whether `base` is a base at all.
    ///
    /// Only a number above one is. At exactly one the logarithm is a division
    /// by zero and every value on the axis would sit in the same place; below
    /// it the axis runs backwards, which is a different request from the one
    /// this answers. The same test gui::mappingOver makes, so that the chrome
    /// and the renderer cannot disagree about whether an axis exists.
    ///
    /// A test and not a clamp, and that is the honest shape for it: the legal
    /// bases are open at one, so there is no nearest legal value to correct a
    /// bad one to. What the panel does with a refusal is put the box back to
    /// the base in force, which says no rather than inventing a yes.
    function usableBase(base) {
        return isFinite(base) && base > 1
    }

    /// The base a mode and its number resolve to.
    ///
    /// Ten for anything this build does not recognise, which is the stance the
    /// rest of the surface takes on a remembered setting it cannot read: a
    /// saved view naming a base from some later version costs the reader that
    /// base and not the whole of their view. It is also the last guard on the
    /// number itself -- everything downstream may assume this answered.
    function baseFor(mode, custom) {
        if (mode === "2")
            return 2
        if (mode === "e")
            return Math.E
        if (mode === "custom")
            return surface.usableBase(custom) ? custom : 10
        return 10
    }

    /// Whether the subdivisions between two powers carry their digit. See
    /// PlotFrame.minorNumbers, and minorNumbersOf for where they appear.
    property bool minorNumbers: false
    property bool showMarkers: false
    /// Whether pointing at the plot reads the sample under the pointer: a
    /// crosshair on the plot and a line of numbers in the bar below it.
    property bool showCursor: true

    /// A title over the pane, and a name for each axis. Empty draws nothing
    /// and costs no room -- see PlotFrame's gutters, which grow for these only
    /// when there is something in them.
    ///
    /// They are for the picture that leaves this application rather than for
    /// the one on screen: what is being drawn is already named in the slice
    /// bar above the plot and in the legend beside it, and neither of those
    /// goes to the clipboard.
    property string plotTitle: ""
    property string xLabel: ""
    property string yLabel: ""

    /// Whether the lines are named on the plot as well as beside it, and in
    /// which corner. See PlotOverlayLegend for why a second legend exists at
    /// all: this one is a caption on the picture and is part of what "copy
    /// plot" copies.
    property bool legendOnPlot: false
    property string legendCorner: "topRight"

    /// Whether this is the presentation on screen. Reading `plot.hasData`
    /// samples the file, so every path into the plot is guarded by this: a
    /// reader browsing a large dataset as a table must not pay for a plot of
    /// it that nobody asked to see.
    property bool active: true

    /// The object being drawn. AppController.datasetPlot by default, which is
    /// the selected dataset read as lines; a custom plot tab hands this its
    /// own object instead. Everything below asks that object the same
    /// questions -- which lines, how many points, what the extent is, fill
    /// this series -- and none of them are questions about a selection, which
    /// is what makes one surface serve both.
    property var plot: AppController.datasetPlot
    /// Whether what `plot` is drawing is something this can draw at all. For
    /// the selected dataset that is a question about its datatype; a custom
    /// plot answers it for itself.
    property bool sourceUsable: AppController.datasetIsNumeric
    readonly property bool drawable: active && surface.sourceUsable
                                     && surface.plot !== null
                                     && surface.plot !== undefined
                                     && surface.plot.hasData

    /// Which group the reader's settings are filed under, per dataset. Empty
    /// disables the memory entirely -- see DatasetMemory, which does nothing
    /// without a group -- which is what a plot that is not about the selection
    /// wants: its settings belong to the tab, not to whatever the tree has
    /// highlighted.
    property string memoryGroup: "plotView"
    property string plotMemoryGroup: "plot"

    // --- the x axis, as start / step / stop ------------------------------
    // The three numbers and the two-of-three solver live in RangeAxis, which
    // is where they went when a second plot started asking for them. The four
    // aliases below are the surface's own public names, kept because the
    // settings panel writes them, DatasetMemory files them by name, and the
    // QML suite reads them.
    readonly property RangeAxis xAxis: RangeAxis {
        id: rangeAxis

        length: surface.dataLength
    }

    property alias rangeStart: rangeAxis.start
    property alias rangeStep: rangeAxis.step
    property alias rangeStop: rangeAxis.stop
    property alias locks: rangeAxis.locks

    /// Kept for readers of this surface that only want to know whether the x
    /// axis is the data's own.
    readonly property bool autoAxis: xAxis.automatic
    readonly property var resolved: xAxis.resolved
    readonly property bool rangeValid: xAxis.valid

    /// Roughly how many ticks an axis carries.
    readonly property int tickTarget: 8

    /// How long the data is, in elements. `len(data)` in the default above.
    ///
    /// Off the table's own geometry rather than off the drawn points, and
    /// deliberately not guarded by `drawable`: it is a column count, not a
    /// sample, so nothing here reaches the file. One keeps the axis drawable
    /// while there is no dataset.
    ///
    /// Overridable, because a custom plot's axis is as long as its longest
    /// entry -- or as long as its time base -- rather than as long as one
    /// table's rows.
    property int dataLength:
        surface.plot ? Math.max(surface.plot.sourcePointCount, 1) : 1

    function locked(which) { return xAxis.locked(which) }
    function lock(which) { xAxis.lock(which) }
    function unlock(which) { xAxis.unlock(which) }
    function setLocked(which, on) { xAxis.setLocked(which, on) }

    /// Whether the trio above is what puts the points along x.
    ///
    /// It is for this plot and for a custom tab whose x axis is a stated
    /// range, and it is not for one drawing against another dataset read as a
    /// time series: there the plot object works out every x itself and two
    /// bindings pushing a start and a step over the top of it would be the
    /// surface overruling the data.
    property bool rangeDrivesX: true

    /// The x bounds the axis takes. Overridable, because a plot whose x comes
    /// from a dataset knows its own extent and this arithmetic does not.
    property real axisMinX: xAxis.minimum
    property real axisMaxX: xAxis.maximum

    /// The smallest x above zero on a *stated* x axis, or 0 when it has none.
    ///
    /// The x values there are a uniform grid -- `start + i x step` -- so the
    /// first one above zero is arithmetic over three numbers rather than a pass
    /// over anything. That matters: the whole of a stated axis's extent is
    /// known without reading a single element, and a logarithmic axis must not
    /// be the one thing about it that changes.
    readonly property real gridPositiveMinX: {
        const low = surface.axisMinX
        const high = surface.axisMaxX
        if (!(high > 0))
            return 0
        if (low > 0)
            return low
        const step = Math.abs(surface.rangeDrivesX && surface.rangeValid
                              ? surface.resolved.step : 1)
        if (!(step > 0))
            return high
        // The grid's members are low + j x step, so the first one strictly
        // above zero is the one after the last at or below it -- which is what
        // the +1 is, and why this is not a ceiling: at low = 0 exactly, the
        // member at zero is the one being stepped past.
        const at = low + (Math.floor(-low / step) + 1) * step
        return at > 0 && at <= high ? at : high
    }

    /// ...and the one this plot actually uses.
    ///
    /// Overridable for exactly the reason axisMinX is: a plot drawing against
    /// another dataset knows its own smallest positive x -- it has read every
    /// one of them -- and the grid arithmetic above has nothing to say about a
    /// time base. A separate property from the one it defaults to, rather than
    /// an override that falls back to itself, which is a binding loop.
    property real positiveMinX: surface.gridPositiveMinX

    /// The x axis's ends as it is actually drawn between them.
    ///
    /// The same two numbers on a linear axis, and every gesture below is
    /// measured against these rather than against the pair above so that there
    /// is one answer to "where does this axis run".
    ///
    /// An axis with no positive part gets one empty decade, which is what the
    /// y axis's `padded` does with the same situation and for the same reason:
    /// a frame with legible numbers on it beats a frame with none. It also
    /// keeps a *number* in every one of these, which matters more than it
    /// looks. Left at the bounds it cannot use, this reached log10 of a
    /// negative -- so the window was NaN, and NaN travelled out through
    /// `setVisibleRange` into the model and out through the footer, which
    /// printed "x NaN … NaN" under a pane that was explaining itself perfectly
    /// well. Every reader of a degenerate axis guards for it, but a number
    /// nothing has to guard for is better than a guard in every reader.
    readonly property var xBounds: {
        if (!surface.xLog)
            return ({ low: surface.axisMinX, high: surface.axisMaxX })
        const low = surface.positiveMinX
        if (!(low > 0) || !(surface.axisMaxX > low))
            return ({ low: 1.0, high: surface.xLogBase })
        return ({ low: low, high: surface.axisMaxX })
    }

    readonly property real axisLowX: surface.xBounds.low
    readonly property real axisHighX: surface.xBounds.high

    // Where the points sit is the plot's own business -- they are built in
    // fill() and never cross into QML -- so the resolved start and step are
    // pushed down to it. Moving them moves the same points, which is why
    // DatasetPlot signals it separately and the surface answers by re-filling
    // rather than by building another graph.
    Binding {
        target: surface.plot
        property: "xStart"
        when: surface.rangeDrivesX
        value: surface.rangeValid ? surface.resolved.start : 0.0
    }

    Binding {
        target: surface.plot
        property: "xStep"
        when: surface.rangeDrivesX
        value: surface.rangeValid ? surface.resolved.step : 1.0
    }

    // The one fact about the scale the plot object has to know. On a
    // logarithmic x axis a pixel column at the left holds a handful of elements
    // and one at the right holds thousands, so a line folded into buckets of
    // equal element width is folded wrong for nearly all of the pane -- see
    // LogColumns in PlotLevels.hpp. Unconditional, unlike the two above: every
    // x axis has a scale, a time base included.
    Binding {
        target: surface.plot
        property: "xLog"
        value: surface.xLog
    }

    // --- the y axis: the values, and nothing to set about them -----------
    /// `low`..`high` with a little air at each end, so a line at the extreme is
    /// a line and not part of the frame. A flat series has no span to take a
    /// share of, and gets a unit of room instead.
    ///
    /// In the axis's own scale, which is what makes it the same air on either
    /// one: a twentieth of a *decade* below the smallest value rather than a
    /// twentieth of the value itself, which on a trace running from 1e-9 to 1
    /// would be nine decades of margin over the top and none at all underneath.
    ///
    /// A flat line on a logarithmic axis runs between the powers either side
    /// of it -- see the branch below.
    ///
    /// A logarithmic axis with nothing above zero to draw between gets one
    /// empty decade. It draws nothing either way -- there is no value it could
    /// place -- and an axis with legible numbers on it is a better answer than
    /// one with none, which is what a span of zero would give.
    function padded(low, high, logarithmic, base) {
        if (logarithmic) {
            if (!(low > 0) || !(high > 0) || !surface.usableBase(base))
                return ({ low: 1.0, high: base > 1 ? base : 10.0 })
            let from = surface.logOf(low, base)
            let to = surface.logOf(high, base)
            if (!(to > from)) {
                // A flat line is widened to the powers either side of it,
                // which is matplotlib's `LogLocator.nonsingular`: the power
                // strictly below and the power strictly above, so a line at 5
                // runs 1 to 10 and one at 10 runs 1 to 100. Then the air, as
                // for any other line.
                const at = Math.abs(from - Math.round(from)) < 1e-10
                         ? Math.round(from) : from
                from = Number.isInteger(at) ? at - 1 : Math.floor(at)
                to = Number.isInteger(at) ? at + 1 : Math.ceil(at)
            }
            const air = (to - from) * 0.05
            return ({ low: Math.pow(base, from - air),
                      high: Math.pow(base, to + air) })
        }
        const air = high > low ? (high - low) * 0.05 : 1.0
        return ({ low: low - air, high: high + air })
    }

    /// The extent of the values being drawn, which is the whole of the y axis.
    /// There is no manual band: the reader has the wheel and the drag for
    /// looking closer at part of it, and a second way to say the same thing --
    /// two boxes that also had to be kept from crossing, and that went stale
    /// the moment the selection moved -- is a control that earns nothing.
    ///
    /// The bottom is the smallest value under a linear axis and the smallest
    /// *positive* one under a logarithmic axis, which is why the plot objects
    /// report both. Taking the ordinary minimum and clamping it would put a
    /// trace whose one zero sample sits among a thousand readings of about a
    /// hundred into a pane running from some invented floor up to a hundred --
    /// every decade of it empty but the last.
    ///
    /// Guarded like every other reader of the sample: `drawable` tests `active`
    /// first, so a hidden plot's bindings never reach the file.
    ///
    /// A plot with no common axis -- every line on one of its own -- has a
    /// nominal one instead, 0..1 or one power of the base, so that the zoom,
    /// the pan and the band go on working in fractions of it; see
    /// separateAxes, which is what reads those fractions.
    readonly property var valueBounds: {
        if (!surface.drawable)
            return ({ low: 0.0, high: 1.0 })
        if (!surface.sharedAxis)
            return surface.yLog ? ({ low: 1.0, high: surface.yLogBase })
                                : ({ low: 0.0, high: 1.0 })
        return surface.padded(surface.yLog ? surface.plot.positiveMinimum
                                           : surface.plot.minimum,
                              surface.plot.maximum, surface.yLog,
                              surface.yLogBase)
    }

    readonly property real lowerBound: surface.valueBounds.low
    readonly property real upperBound: surface.valueBounds.high

    // --- the lines on axes of their own ----------------------------------
    /// Whether there is a common y axis at all. There is not when every drawn
    /// line has been given one of its own, and then the frame numbers nothing
    /// in the common axis's place and rules nothing across the pane: the
    /// rules are the common axis's, and a grid ruled at a nominal axis would
    /// be a grid of nothing.
    ///
    /// Asked of the plot object, which counts it: DatasetPlot answers with
    /// every drawn line, because its lines are never separate.
    readonly property bool sharedAxis: !surface.drawable || !surface.plot
                                       || surface.plot.sharedSeriesCount > 0

    /// Where the window on the common axis sits within the whole of it, as
    /// a fraction from the bottom -- in the axis's own positions, so on a
    /// logarithmic common axis a zoom into the top decade is a zoom into the
    /// top of every separate axis too.
    function commonFraction(value) {
        const from = surface.axisPosition(surface.lowerBound, surface.yLog,
                                          surface.yLogBase)
        const to = surface.axisPosition(surface.upperBound, surface.yLog,
                                        surface.yLogBase)
        if (!(to > from))
            return 0.0
        return (surface.axisPosition(value, surface.yLog, surface.yLogBase)
                - from) / (to - from)
    }

    /// Every drawn line that is on an axis of its own, in drawing order, as
    /// `{ series, line, fixed, low, high, colour, paperColour }` -- `line`
    /// being its place in what the item was handed and `series` its row.
    ///
    /// **A separate axis zooms by the common axis's fractions.** Its whole
    /// is the line's own extent, with the air a lone line would get, so it is
    /// the axis that line would have if it were the only one on the plot; and
    /// the window on it is the same share of that whole the common axis is
    /// showing of its own. So one gesture zooms every axis at once, about the
    /// same place on the pane, and no axis needs a zoom or a pan of its own to
    /// be remembered, reset or saved. A *fixed* axis ignores the fractions and
    /// shows its whole, which is what "exclude from zooming" asks.
    ///
    /// Always linear: the plot settings are the common axis's, logarithm
    /// included, and a separate axis is the line as it would be drawn alone.
    readonly property var separateAxes: {
        if (!surface.drawable || !surface.plot)
            return []
        // Named so that this binding depends on them: seriesColor() and
        // seriesAxis() are calls, and a call creates no dependency on what it
        // reads. `drawnSeries` is announced with every change a separate axis
        // can follow -- a line ticked, a box ticked, a re-read.
        const drawn = surface.plot.drawnSeries
        // Nothing separate is nothing to ask about, and this binding runs on
        // every frame of a zoom: the Plot tab can be ten thousand lines, none
        // of them ever separate, and asking each of them per frame would be a
        // cost paid for a feature it does not have.
        if (surface.plot.sharedSeriesCount >= drawn.length)
            return []
        const cycle = surface.colorMode
        const reversed = surface.colorsReversed
        const single = surface.colorSingle
        const from = surface.colorRangeFrom
        const to = surface.colorRangeTo
        const band = [surface.colorFrom, surface.colorTo]
        const dark = Theme.dark
        const low = surface.commonFraction(surface.viewMinY)
        const high = surface.commonFraction(surface.viewMaxY)
        const found = []
        for (let i = 0; i < drawn.length; ++i) {
            const axis = surface.plot.seriesAxis(drawn[i])
            if (!axis.separate)
                continue
            const whole = axis.finite ? surface.padded(axis.low, axis.high, false, 10)
                                      : ({ low: 0.0, high: 1.0 })
            const bottom = axis.fixed ? 0.0 : low
            const top = axis.fixed ? 1.0 : high
            const span = whole.high - whole.low
            found.push({
                series: drawn[i],
                line: i,
                fixed: axis.fixed,
                low: whole.low + bottom * span,
                high: whole.low + top * span,
                colour: surface.seriesColor(drawn[i], i, drawn.length, false),
                paperColour: surface.seriesColor(drawn[i], i, drawn.length, true)
            })
        }
        return found
    }

    /// Put each line on the axis it belongs to. Part of restyling, because a
    /// fill hands the item lines on the common axis and this is what moves
    /// them off it -- and on its own whenever the window moves, because that
    /// moves every separate axis with it.
    function pushSeriesAxes() {
        const count = frame.lines.lineCount()
        const own = {}
        for (let i = 0; i < surface.separateAxes.length; ++i)
            own[surface.separateAxes[i].line] = surface.separateAxes[i]
        for (let line = 0; line < count; ++line) {
            const axis = own[line]
            if (axis)
                frame.lines.setSeriesYRange(line, axis.low, axis.high)
            else
                frame.lines.clearSeriesYRange(line)
        }
    }

    onSeparateAxesChanged: surface.pushSeriesAxes()

    // --- which line is which ---------------------------------------------
    /// How the lines are coloured: the name of one of Theme's categorical
    /// palettes, or "same", or "range", or the name of one of its ramps.
    ///
    /// Two kinds of answer share the one property because the reader is making
    /// one choice. A palette gives each line a colour of its own and starts
    /// over when it runs out; a map -- "range", or a named ramp -- spreads the
    /// lines along a continuum and gives each an even share of it.
    ///
    /// The default is a palette. This plot used to open on "same", one accent
    /// for every line, and separate them by overlap alone; that stops
    /// separating anything at about a dozen lines, and the legend that names
    /// them is no use when they all look alike. A map was the first answer to
    /// that and is the wrong shape for the question: it puts its neighbours
    /// next to each other by construction, so the lines it has to tell apart
    /// are the ones it draws most alike. A palette is built to do exactly
    /// this, so it is what a new plot opens on.
    ///
    /// Which palette is Okabe-Ito rather than this application's own
    /// `spectrum`, and the reason is about where the picture ends up: a figure
    /// drawn here goes into a document beside figures drawn by other people in
    /// other tools, and Okabe-Ito is the cycle those tools already agree on.
    /// See the note over Theme.categoricalPalettes, which also states what
    /// that costs on the light theme.
    property string colorMode: "okabe-ito"
    property color colorSingle: Theme.accent
    property color colorRangeFrom: Theme.accent
    property color colorRangeTo: Theme.info
    /// Run the cycle the other way. Which end of a ramp is the dark one is a
    /// property of the ramp and not of the data, and the reader is the one who
    /// knows which way round they want to read it. On a palette it is the
    /// order of the entries that turns around, so the first line takes the
    /// colour the last one would have.
    property bool colorsReversed: false

    /// Which part of the cycle the lines actually span, as two positions
    /// along it. The colour map's own range, the same control the image and
    /// the table put over their values -- here it runs over the map rather
    /// than over the data, because what a plot colours by is which line a
    /// stroke is and not how big its numbers are.
    ///
    /// It earns its place on a perceptual ramp: those run dark to light, and
    /// this plot's ground is true black, so the first few lines of a viridis
    /// start out nearly invisible. Pulling the near handle up takes that end
    /// of the map back.
    ///
    /// A palette has no continuum to take a slice out of -- its colours are a
    /// set and not a range -- so this does not apply to one, and the settings
    /// panel drops the control rather than showing one that does nothing.
    property real colorFrom: 0.0
    property real colorTo: 1.0

    /// The line the reader has picked out in the legend, by its index in the
    /// table, or -1. It is drawn at full strength and full width and everything
    /// else steps back, which is the only way to follow one line through a
    /// bundle of them.
    property int highlighted: -1

    /// The colour of line `series`, drawn `position` of `count`.
    ///
    /// Three numbers for one line, because the two kinds of cycle ask
    /// different questions of it. `series` is *which* line it is -- its own
    /// index in the legend, the number that names it whether it is drawn or
    /// not -- and it is the whole of a palette's answer. `position` and
    /// `count` place it among the lines currently drawn, which is what a map
    /// needs and what a palette ignores: a share of a continuum only exists
    /// once you know how many shares there are. The rest of this note is about
    /// the maps.
    ///
    /// The lines sit at the *middles* of `count` equal shares of the map
    /// rather than at its ends: line i of n is drawn at (i + 1) / (n + 1), so
    /// one line takes the middle colour, two take the thirds, three the
    /// quarters, and so on. The ends of the map are never reached.
    ///
    /// That is the point of it. Every perceptual ramp runs from something very
    /// dark to something very pale, and both of those are a line nobody can
    /// see: on this plot's true-black ground the first line of a viridis was
    /// very nearly invisible, and on a white one the last line of a hot is.
    /// Spreading the lines end to end spent the two worst colours in the map
    /// on two of the lines every time, and did it worst in the case that
    /// matters most -- a single line, which took the very first colour and
    /// nothing else.
    ///
    /// The reader's own band (colorFrom .. colorTo) still applies on top, so
    /// narrowing the map narrows what these shares are taken out of.
    ///
    /// `paper` is the fourth number and the only one that is not about which
    /// line this is: it asks for the colour the *light* scope would draw,
    /// whatever scope is on screen. That is what a publication export is --
    /// see the paper block in Theme -- and it is asked here rather than
    /// substituted afterwards because "the same picture in the other scope"
    /// is a property of every branch below and not of the answer. Until 0.6.4
    /// the picture that left was every stroke in one black ink, which is the
    /// one thing a colour cycle exists to not be: six traces pasted into a
    /// document were six identical strokes with a caption naming colours that
    /// were not in the picture.
    function seriesColor(series, position, count, paper) {
        // A colour the reader gave this line beats every cycle, and beats it
        // first: a cycle answers "which line is this" for lines that are alike,
        // and the lines of a custom tab were each put there on purpose. The
        // override is per line and nothing else moves, which is the same
        // promise a palette makes about its own index and for the same reason
        // -- a reader watching one stroke must not see it change under them
        // because they said something about another.
        //
        // Asked of the plot rather than held here, because both plots are
        // drawn by this file and only one of them has any: DatasetPlot answers
        // "no" for every line. Plot tab lines are rows or columns of one
        // dataset and their place in the table is what identifies them.
        if (surface.plot) {
            const own = surface.plot.seriesOverride(series)
            if (own !== undefined && own !== null)
                return paper ? Theme.paperColor(own) : own
        }

        if (surface.colorMode === "same")
            return paper ? Theme.paperColor(surface.colorSingle)
                         : surface.colorSingle

        // A palette is asked which line this is rather than how far along it
        // sits, so none of the arithmetic below applies to one: not the shares
        // -- there is no continuum to take shares of -- and not the reader's
        // band. The line's own index is the answer, counted along a cycle that
        // repeats, and it does not move when a line is added or taken away.
        // That is the second thing a palette buys over a map: on a map every
        // line changes colour when one of them is unticked.
        //
        // `series` and not `position`, which is the whole of that promise.
        // Asked where the line sat among the drawn ones, a palette kept the
        // promise only until the reader unticked something: hiding the first
        // of five lines handed the second line the first one's colour, and the
        // reader watching one stroke saw it change under them -- the exact
        // failure a palette is here to not have.
        const palette = paper ? Theme.paperPalettes[surface.colorMode]
                              : Theme.categoricalPalettes[surface.colorMode]
        if (palette) {
            return Theme.categoricalColor(
                palette,
                surface.colorsReversed ? palette.length - 1 - series : series)
        }

        let at = count > 0 ? (position + 1) / (count + 1) : 0.5
        if (surface.colorsReversed)
            at = 1 - at
        // Across the reader's own slice of the map rather than across the
        // whole of it. Untouched that slice is the whole of it, so the
        // arithmetic is a no-op until they say otherwise.
        at = surface.colorFrom + at * (surface.colorTo - surface.colorFrom)
        if (surface.colorMode === "range") {
            return Theme.mix(paper ? Theme.paperColor(surface.colorRangeFrom)
                                   : surface.colorRangeFrom,
                             paper ? Theme.paperColor(surface.colorRangeTo)
                                   : surface.colorRangeTo,
                             at)
        }
        // The ramps are the one part of this that has no second scope: they
        // are named colour maps kept as they are defined elsewhere, so a
        // viridis on paper is the viridis a reader in the light theme sees and
        // the viridis matplotlib draws. See Theme.colorRamps.
        return Theme.rampColor(Theme.colorRamps[surface.colorMode], at)
    }

    /// How strongly a line is drawn. One line has nothing to separate from and
    /// takes full strength; a bundle separates by overlap and is drawn under
    /// it. A highlighted line comes forward and the rest go further back, so
    /// the one being followed is the one that reads.
    function seriesOpacity(series, count) {
        if (surface.highlighted >= 0)
            return series === surface.highlighted ? 1.0
                                                  : Theme.plotSeriesOpacity / 2
        return count > 1 ? Theme.plotSeriesOpacity : 1.0
    }

    /// How heavily. A line being followed is drawn thicker as well as brighter:
    /// at a hairline, opacity alone is not enough to pick one out of fifty.
    function seriesWidth(series) {
        return series === surface.highlighted ? Theme.plotLineWidth * 2
                                              : Theme.plotLineWidth
    }

    // --- the view on those bounds ---------------------------------------
    // Zoom and pan are the axes' own, not a transform over the drawn image:
    // ValueAxis carries `zoom` and `pan`, and its visible range works out as
    //
    //     centre = (min + max) / 2 + pan
    //     span   = (max - min) / zoom
    //
    // so the labels, the grid and the line all follow one another and stay
    // exact. Scaling the rendered picture instead would blur the line and
    // leave the axis printing numbers that are no longer under their ticks.
    //
    // A zoom below 1 is not offered: 1 is the whole of the data, and there is
    // nothing outside it to look at. Which is also what makes the pan clamp
    // below simple -- the window is never larger than what it moves within.
    property real zoomX: 1.0
    property real panX: 0.0
    property real zoomY: 1.0
    property real panY: 0.0

    /// Ceiling on magnification.
    ///
    /// This was 256 flat, and the note beside it said why: the sample behind
    /// the plot held at most a couple of thousand points of the *whole* line,
    /// so past that there was nothing further to resolve and zooming only
    /// stretched what was already drawn.
    ///
    /// That is no longer true. The plot object reads the run the reader is
    /// looking at again, at a finer bucket, whenever they stop moving -- see
    /// DatasetPlot::setVisibleRange -- so each octave in is an octave of real
    /// detail until the line is drawn sample for sample. The ceiling is
    /// therefore about the data rather than about the cache: enough
    /// magnification to put a handful of elements across the pane, and no more,
    /// because a pane showing fewer than that is showing a gap between two
    /// samples rather than a line.
    readonly property real maxZoom: Math.max(256.0, surface.dataLength / 16)

    /// ...and what that ceiling is *for*, in the x the axis prints: the
    /// narrowest window the x axis may show.
    ///
    /// On a linear axis the two are one statement, and maxZoom goes on being
    /// the one the arithmetic uses. On a logarithmic one they come apart,
    /// because a zoom there is a share of the *decades* and the same share is
    /// a different number of elements at every place along the axis. The
    /// linear ceiling read that way was wrong at both ends at once: at the low
    /// end of six decades it let the reader zoom into the gap between the
    /// first two samples and on past it, and at the high end it stopped them
    /// with a hundred and seventy elements still across the pane. The window
    /// is what the ceiling was about, so on a logarithmic axis it is the window
    /// that is held to it -- see logZoomCeiling.
    readonly property real minimumSpanX:
        Math.abs(surface.axisMaxX - surface.axisMinX) / surface.maxZoom

    /// The largest zoom that keeps a logarithmic window at least `minimumSpan`
    /// wide in the data's own units, when the value at `held` -- a position,
    /// so a logarithm -- stays at `fraction` of the pane.
    ///
    /// The window's width grows with its span in decades, so the answer is a
    /// bisection of that span rather than a formula: the width is a difference
    /// of two powers, and solving it in closed form is a quadratic in one of
    /// them for no gain over fifty halvings of a number already in hand. A
    /// window that cannot be made that wide at all is the whole axis, which is
    /// a zoom of one.
    function logZoomCeiling(full, held, fraction, minimumSpan, base) {
        if (!(full > 0) || !(minimumSpan > 0))
            return surface.maxZoom
        const width = (span) => Math.pow(base, held + (1 - fraction) * span)
                                - Math.pow(base, held - fraction * span)
        if (!(width(full) > minimumSpan))
            return 1.0
        let narrow = 0.0
        let wide = full
        for (let i = 0; i < 60; ++i) {
            const mid = (narrow + wide) / 2
            if (width(mid) >= minimumSpan)
                wide = mid
            else
                narrow = mid
        }
        return Math.max(1.0, full / wide)
    }

    readonly property bool zoomed: zoomX !== 1.0 || zoomY !== 1.0
                                   || panX !== 0.0 || panY !== 0.0

    /// The plot area, in this item's coordinates: the frame minus the margins
    /// the axis labels live in. Every gesture below is measured against it,
    /// because a fraction of the whole item is not a fraction of the axis.
    readonly property rect plotRect: frame.area

    function resetView() {
        surface.clearZoomFocus()
        zoomX = 1.0
        panX = 0.0
        zoomY = 1.0
        panY = 0.0
    }

    /// Where a value sits along an axis, in the units the zoom and the pan are
    /// measured in -- and back again.
    ///
    /// The whole of what a logarithmic axis changes about the gestures, and
    /// the two are the only lines below that know which scale they are on.
    /// Zoom and pan are arithmetic over an axis's *span*, and on a logarithmic
    /// axis the only span that means anything is a count of decades: a wheel
    /// notch has to take the same bite out of a pane showing 1 to 10 as out of
    /// one showing 1e6 to 1e7, and a drag has to move by so many decades rather
    /// than by so many units. Measured in the data's own units instead, one
    /// notch at the low end of a six-decade axis would zoom past every float
    /// there is while the high end had not visibly moved at all.
    ///
    /// So `panX` and `panY` are in decades while their axis is logarithmic,
    /// and that is why switching a scale resets the view: a pan of 40 is four
    /// decades or it is forty units, and there is no reading of it that is both.
    function axisPosition(value, logarithmic, base) {
        return logarithmic ? surface.logOf(value, base) : value
    }

    function axisValue(position, logarithmic, base) {
        return logarithmic ? Math.pow(base, position) : position
    }

    /// The logarithm of `value` to `base`. PlotFrame.logOf, and the reason the
    /// two exact cases are worth branching for is written there.
    function logOf(value, base) {
        if (base === 10)
            return Math.log10(value)
        if (base === 2)
            return Math.log2(value)
        return Math.log(value) / Math.log(base)
    }

    /// The value `at` of the way from `low` to `high`. The inverse of
    /// PlotFrame.fractionOn, and the same arithmetic gui::AxisMapping::valueAt
    /// does -- which is what makes a pointer position resolve to the value the
    /// tick under it prints.
    function valueAlong(low, high, at, logarithmic, base) {
        const from = surface.axisPosition(low, logarithmic, base)
        const to = surface.axisPosition(high, logarithmic, base)
        return surface.axisValue(from + at * (to - from), logarithmic, base)
    }

    /// The window the axes are actually showing, worked out with the same
    /// arithmetic ValueAxis uses. The footer prints these, because once the
    /// view has been moved the range of the data is no longer the range on
    /// screen and only one of the two is worth reading.
    function visibleLow(low, high, zoom, pan, logarithmic, base) {
        const from = surface.axisPosition(low, logarithmic, base)
        const to = surface.axisPosition(high, logarithmic, base)
        return surface.axisValue((from + to) / 2.0 + pan - (to - from) / zoom / 2.0,
                                 logarithmic, base)
    }

    function visibleHigh(low, high, zoom, pan, logarithmic, base) {
        const from = surface.axisPosition(low, logarithmic, base)
        const to = surface.axisPosition(high, logarithmic, base)
        return surface.axisValue((from + to) / 2.0 + pan + (to - from) / zoom / 2.0,
                                 logarithmic, base)
    }

    readonly property real viewMinX:
        visibleLow(axisLowX, axisHighX, zoomX, panX, xLog, xLogBase)
    readonly property real viewMaxX:
        visibleHigh(axisLowX, axisHighX, zoomX, panX, xLog, xLogBase)
    readonly property real viewMinY:
        visibleLow(lowerBound, upperBound, zoomY, panY, yLog, yLogBase)
    readonly property real viewMaxY:
        visibleHigh(lowerBound, upperBound, zoomY, panY, yLog, yLogBase)

    /// Pan clamped so the visible window stays inside the data. With zoom at
    /// 1 the window *is* the data and the only legal pan is none.
    function clampPan(pan, zoom, low, high, logarithmic, base) {
        const room = (surface.axisPosition(high, logarithmic, base)
                      - surface.axisPosition(low, logarithmic, base))
                     * (1.0 - 1.0 / zoom) / 2.0
        return Math.max(-room, Math.min(room, pan))
    }

    /// Zoom one axis by `factor` while holding the value at `fraction` of the
    /// visible span still -- which is what makes the wheel zoom into whatever
    /// the pointer is over rather than into the middle of the frame.
    ///
    /// Returns the new { zoom, pan } for the caller to assign, because QML has
    /// no out-parameters and two of these run per wheel tick.
    function zoomedAxis(zoom, pan, low, high, fraction, factor, logarithmic, base,
                        minimumSpan) {
        // In positions rather than in values, which is the whole of what makes
        // this work on either scale: "hold the value under the pointer still"
        // is "hold its position still", and a position is a decade count on a
        // logarithmic axis.
        const from = surface.axisPosition(low, logarithmic, base)
        const to = surface.axisPosition(high, logarithmic, base)
        const full = to - from
        const span = full / zoom
        const held = (from + to) / 2.0 + pan - span / 2.0 + fraction * span
        // The ceiling where the pointer is, on a logarithmic axis that has
        // one -- see minimumSpanX -- and the flat one everywhere else.
        const ceiling = logarithmic && minimumSpan > 0
                      ? surface.logZoomCeiling(full, held, fraction, minimumSpan, base)
                      : surface.maxZoom
        // Never *out* past where the reader already is, though: a ceiling
        // that fell below the zoom in hand -- the pointer moved to where the
        // elements are sparser -- is not a reason to throw them back out.
        const next = Math.max(1.0, Math.min(Math.max(ceiling, zoom), zoom * factor))
        const nextSpan = full / next
        const centre = held - fraction * nextSpan + nextSpan / 2.0
        return { zoom: next,
                 pan: surface.clampPan(centre - (from + to) / 2.0, next, low, high,
                                       logarithmic, base) }
    }

    /// Zoom about (px, py) by `factor`, on the axes `axes` names: "x", "y" or
    /// "both".
    ///
    /// One axis at a time is what the modifiers ask for, and it is not a
    /// convenience. A plot of a long trace is read by stretching time without
    /// changing what an amplitude is worth, and a plot of a narrow band is read
    /// the other way round; a zoom that always takes both makes either of those
    /// a zoom followed by a correcting pan, done by eye. Shift for x and Ctrl
    /// for y, which is the pair every other plot in the field uses.
    function zoomAt(px, py, factor, axes) {
        const area = surface.plotRect
        if (area.width <= 0 || area.height <= 0)
            return
        const fx = Math.max(0, Math.min(1, (px - area.x) / area.width))
        // y grows downward on screen and upward on the axis.
        const fy = 1.0 - Math.max(0, Math.min(1, (py - area.y) / area.height))

        if (axes !== "y") {
            // Where the pointer is, in the x the axis prints, before the zoom
            // moves anything -- which is the value zoomedAxis() is about to
            // hold still under it.
            //
            // This is the one thing the surface has always known and never
            // said. Only the *resulting* range crossed into the plot object, so
            // the runs it read ahead of a zoom were centred on the middle of
            // the frame, and a reader zooming into one corner walked off them
            // after a step or two and waited for the file each time. Told where
            // the zoom is going, it reads that way instead -- and reads at once
            // rather than after the gesture stops, because an inward run costs
            // half the span of the one above it.
            //
            // Through valueAlong rather than by interpolating the two bounds,
            // which is the same distinction: on a logarithmic axis the value
            // halfway across the pane is the geometric mean of its ends and
            // not the arithmetic one, and a focus taken the other way would
            // send the read towards a place the pointer is nowhere near.
            surface.pushZoomFocus(
                surface.valueAlong(surface.viewMinX, surface.viewMaxX, fx,
                                   surface.xLog, surface.xLogBase),
                factor)

            const x = surface.zoomedAxis(surface.zoomX, surface.panX,
                                         surface.axisLowX, surface.axisHighX,
                                         fx, factor, surface.xLog,
                                         surface.xLogBase, surface.minimumSpanX)
            surface.zoomX = x.zoom
            surface.panX = x.pan
        }
        if (axes !== "x") {
            const y = surface.zoomedAxis(surface.zoomY, surface.panY,
                                         surface.lowerBound, surface.upperBound,
                                         fy, factor, surface.yLog,
                                         surface.yLogBase)
            surface.zoomY = y.zoom
            surface.panY = y.pan
        }
    }

    /// The zoom and pan that put `from`..`to` on an axis running `low`..`high`.
    ///
    /// The inverse of visibleLow/visibleHigh, which is what makes it exact:
    /// asked for the window those two report, it gives back the zoom and pan
    /// they were computed from. Clamped the way every other path here is --
    /// never below 1, never past maxZoom, never outside the data -- so a
    /// window nobody can be shown comes back as the nearest one that can be.
    function viewedAxis(low, high, from, to, logarithmic, base, minimumSpan) {
        const axisFrom = surface.axisPosition(low, logarithmic, base)
        const axisTo = surface.axisPosition(high, logarithmic, base)
        const wantFrom = surface.axisPosition(from, logarithmic, base)
        const wantTo = surface.axisPosition(to, logarithmic, base)
        const full = axisTo - axisFrom
        const span = Math.abs(wantTo - wantFrom)
        if (!(full > 0) || !(span > 0))
            return { zoom: 1.0, pan: 0.0 }
        const centre = (wantFrom + wantTo) / 2.0
        // Held about the middle of what was asked for, which is where a stated
        // window is centred. See zoomedAxis for the ceiling itself.
        const ceiling = logarithmic && minimumSpan > 0
                      ? surface.logZoomCeiling(full, centre, 0.5, minimumSpan, base)
                      : surface.maxZoom
        const zoom = Math.max(1.0, Math.min(ceiling, full / span))
        return { zoom: zoom,
                 pan: surface.clampPan(centre - (axisFrom + axisTo) / 2.0,
                                       zoom, low, high, logarithmic, base) }
    }

    /// `from`..`to` cut down to the part of a logarithmic axis that exists.
    ///
    /// A reader can type a zero into the four boxes under Plot Settings, and a
    /// saved view can carry a window from before the scale was turned on;
    /// neither is a window on this axis, because there is no place at or below
    /// zero for it to reach.
    ///
    /// Clipped to the axis's own low end rather than refused, which is what
    /// matplotlib does with a limit it cannot use: what was asked for is
    /// mostly reachable, and the part that is not is not there to be shown.
    /// Without it such a window resolved to a logarithm of minus infinity, an
    /// infinite span, and a zoom of one -- so asking to look at 0 to 100 on an
    /// axis running to a million showed the whole million, which is the
    /// opposite of what was asked.
    ///
    /// A window with nothing above zero in it at all is the whole axis, which
    /// is what a request to look at nothing resolves to everywhere else here.
    function positiveSpan(from, to, low, high, logarithmic) {
        if (!logarithmic)
            return ({ from: from, to: to })
        if (!(to > 0))
            return ({ from: low, to: high })
        return ({ from: Math.max(from, low), to: to })
    }

    /// Put the window at exactly these four numbers, in data coordinates.
    ///
    /// The one way in for everything that states a window rather than nudging
    /// one: the region band, and the four boxes under Plot Settings > View.
    /// Both of those are the reader saying where to look, which is the same
    /// thing said twice -- so it is arithmetic in one place and the boxes
    /// report the band's answer without either knowing about the other.
    function setViewRange(x0, x1, y0, y1) {
        const wantX = surface.positiveSpan(Math.min(x0, x1), Math.max(x0, x1),
                                           surface.axisLowX, surface.axisHighX,
                                           surface.xLog)
        const wantY = surface.positiveSpan(Math.min(y0, y1), Math.max(y0, y1),
                                           surface.lowerBound, surface.upperBound,
                                           surface.yLog)
        const x = surface.viewedAxis(surface.axisLowX, surface.axisHighX,
                                     wantX.from, wantX.to, surface.xLog,
                                     surface.xLogBase, surface.minimumSpanX)
        const y = surface.viewedAxis(surface.lowerBound, surface.upperBound,
                                     wantY.from, wantY.to, surface.yLog,
                                     surface.yLogBase)
        surface.zoomX = x.zoom
        surface.panX = x.pan
        surface.zoomY = y.zoom
        surface.panY = y.pan
    }

    /// What a pixel on the pane stands for, on each axis.
    ///
    /// Out here rather than inside zoomToRegion because the band's own readout
    /// needs exactly these two answers: a number written beside a rectangle
    /// and the window that rectangle resolves to must be one reading of one
    /// pixel, or the numbers are a promise the zoom does not keep. See
    /// PlotBand, which asks rather than reimplements.
    ///
    /// Clamped to the pane, because a drag that runs off the edge says "to the
    /// edge" -- which is what the band draws and what the zoom then takes.
    function dataXAt(px) {
        const area = surface.plotRect
        if (area.width <= 0)
            return surface.viewMinX
        const at = Math.max(0, Math.min(1, (px - area.x) / area.width))
        return surface.valueAlong(surface.viewMinX, surface.viewMaxX, at,
                                  surface.xLog, surface.xLogBase)
    }

    /// The same, down the other axis. y grows downward on screen and upward on
    /// the axis, so the top of the pane is the larger value.
    function dataYAt(py) {
        const area = surface.plotRect
        if (area.height <= 0)
            return surface.viewMinY
        const at = Math.max(0, Math.min(1, (py - area.y) / area.height))
        return surface.valueAlong(surface.viewMinY, surface.viewMaxY, 1.0 - at,
                                  surface.yLog, surface.yLogBase)
    }

    /// Go to the region a right-drag has just drawn, in this item's pixels.
    ///
    /// Two things happen here and the order of them is the whole feature. The
    /// focus is pushed *first*, with the magnification the move amounts to, so
    /// that the closer look the model reads is read towards the region and
    /// goes out in the same turn rather than after the settle -- a stated
    /// region is not a gesture that might carry on, it is a reader who has
    /// already said where they are going. Then the window moves, and the
    /// bindings under it push the range.
    ///
    /// A band under a few pixels in either direction is a slip rather than a
    /// request: a two-pixel-tall window is a magnification of several hundred
    /// on an axis the reader never meant to touch, and the way out of it would
    /// be a reset. Refused whole rather than clamped to one axis, because
    /// which axis was meant is not something this can know.
    function zoomToRegion(px0, py0, px1, py1) {
        const area = surface.plotRect
        if (area.width <= 0 || area.height <= 0)
            return false
        const left = Math.min(px0, px1)
        const right = Math.max(px0, px1)
        const top = Math.min(py0, py1)
        const bottom = Math.max(py0, py1)
        if (right - left < surface.minimumBand
                || bottom - top < surface.minimumBand)
            return false

        // The band's top edge is the axis's larger value -- see dataYAt.
        const x0 = surface.dataXAt(left)
        const x1 = surface.dataXAt(right)
        const y0 = surface.dataYAt(bottom)
        const y1 = surface.dataYAt(top)

        // The middle of the band and the magnification it amounts to, both
        // measured the way the axis measures: on a logarithmic one the middle
        // of the band is the geometric mean of its ends, and a band a decade
        // wide on a pane of six is six times in however many units that
        // decade holds. Taken the linear way, a band over the first decades of
        // a wide axis sent the read to the far end of it.
        const span = surface.axisPosition(surface.viewMaxX, surface.xLog, surface.xLogBase)
                   - surface.axisPosition(surface.viewMinX, surface.xLog, surface.xLogBase)
        const band = surface.axisPosition(x1, surface.xLog, surface.xLogBase)
                   - surface.axisPosition(x0, surface.xLog, surface.xLogBase)
        if (span > 0 && band > 0)
            surface.pushZoomFocus(surface.valueAlong(x0, x1, 0.5, surface.xLog,
                                                     surface.xLogBase),
                                  span / band)
        surface.setViewRange(x0, x1, y0, y1)
        return true
    }

    /// The smallest band that counts as one. See zoomToRegion.
    readonly property int minimumBand: Theme.gapL

    /// Whether a region is being drawn right now.
    ///
    /// The crosshair goes off while it is. What the reader is doing during a
    /// band is choosing an interval; the band writes that interval out in the
    /// axes' own numbers, and a second readout snapped to whichever sample
    /// happens to be nearest the pointer is a second answer to a question
    /// nobody asked twice -- drawn over the first one, in the same pane, in
    /// the same face. It comes back the moment the button does.
    readonly property bool selecting: band.active

    /// Which axes a wheel event with these modifiers zooms.
    ///
    /// Shift alone is x, Ctrl alone is y, and everything else -- neither, both,
    /// or one of them with a third key held -- is both. Both is the default
    /// rather than nothing, because a modifier this file does not know about is
    /// a modifier some window manager put there and not an instruction.
    function zoomAxesFor(modifiers) {
        const shift = (modifiers & Qt.ShiftModifier) !== 0
        const control = (modifiers & Qt.ControlModifier) !== 0
        if (shift && !control)
            return "x"
        if (control && !shift)
            return "y"
        return "both"
    }

    /// Drag the view by a pointer movement. The content follows the pointer,
    /// so the axis moves the other way.
    function panBy(dx, dy) {
        const area = surface.plotRect
        if (area.width <= 0 || area.height <= 0)
            return
        // A drag is not a zoom and has nowhere it is heading, so what is read
        // ahead goes back to being measured from the run on screen. It also
        // stops the read going out on every frame of the drag: only a focus
        // skips the settle.
        surface.clearZoomFocus()
        // In positions, so that a drag of so many pixels moves the view by the
        // share of the axis those pixels are -- which is the same sentence on
        // either scale and is a count of decades on one of them.
        const spanX = (surface.axisPosition(surface.axisHighX, surface.xLog,
                                           surface.xLogBase)
                       - surface.axisPosition(surface.axisLowX, surface.xLog,
                                              surface.xLogBase)) / surface.zoomX
        const spanY = (surface.axisPosition(surface.upperBound, surface.yLog,
                                           surface.yLogBase)
                       - surface.axisPosition(surface.lowerBound, surface.yLog,
                                              surface.yLogBase)) / surface.zoomY
        surface.panX = surface.clampPan(surface.panX - dx * spanX / area.width,
                                        surface.zoomX, surface.axisLowX,
                                        surface.axisHighX, surface.xLog,
                                        surface.xLogBase)
        surface.panY = surface.clampPan(surface.panY + dy * spanY / area.height,
                                        surface.zoomY, surface.lowerBound,
                                        surface.upperBound, surface.yLog,
                                        surface.yLogBase)
    }

    /// A round tick spacing giving roughly `target` ticks across `span`:
    /// 1, 2 or 5 times a power of ten, which is what every axis in every
    /// plotting library settles on and what a reader can add up in their head.
    ///
    /// Written here in the first place because Qt Graphs computed its
    /// automatic spacing from the axis's *declared* range and not from the
    /// range it was showing, so a zoomed-in axis kept the spacing of the whole
    /// dataset and printed one lonely tick. PlotFrame has its own copy, which
    /// is the one the ticks are drawn from; this one is what the QML suite
    /// reads and what the footer's readouts round against.
    function niceStep(span, target) {
        if (!(span > 0))
            return 0
        const raw = span / Math.max(1, target)
        const magnitude = Math.pow(10, Math.floor(Math.log(raw) / Math.LN10))
        const scaled = raw / magnitude
        return magnitude * (scaled <= 1 ? 1 : scaled <= 2 ? 2 : scaled <= 5 ? 5 : 10)
    }

    /// Where the graph starts, which is the legend's right edge while it is
    /// open. Read off the panel's own x rather than off `legendOpen`, so the
    /// two can never disagree about where the margin is.
    ///
    /// The legend pushes rather than covers. It has to: the y axis is drawn in
    /// the graph's left margin, so a panel lying over that margin takes the
    /// axis labels with it -- and an axis the reader has just been given three
    /// controls over is the last thing that may become unreadable.
    readonly property real contentLeft:
        Math.max(0, legendPanel.x + legendPanel.width)

    Rectangle {
        anchors.fill: parent
        color: Theme.surfaceInset
    }

    // The frame and the lines. PlotFrame draws the gutters, the rules, the
    // ticks and their labels; the item inside it draws the strokes.
    //
    // What this replaced was a Loader holding a GraphsView, discarded and
    // rebuilt on every change of selection. That was not a design -- it was
    // Qt Graphs tax. A series there held on to what it last drew, so reusing
    // one left the old path on screen underneath the new one in the pixel
    // coordinates of the axes it had been drawn against, and neither emptying
    // it nor taking it out of the graph cleared it; nor could it simply be
    // destroyed, because removeSeries() kept the raw pointer in a cleanup list
    // it read on its next polish. Throwing the whole view away answered both
    // and cost a blank frame every time the reader picked a dataset.
    //
    // Nothing here retains anything. fill() hands the item a pointer to the
    // lines and the item projects them; a new selection is a new set of
    // pointers and the frame after it is the new picture.
    PlotFrame {
        id: frame

        anchors.fill: parent
        anchors.leftMargin: surface.contentLeft

        viewMinX: surface.viewMinX
        viewMaxX: surface.viewMaxX
        viewMinY: surface.viewMinY
        viewMaxY: surface.viewMaxY
        gridMode: surface.gridMode
        gridStepX: surface.gridStepX
        gridStepY: surface.gridStepY
        xLog: surface.xLog
        yLog: surface.yLog
        xLogBase: surface.xLogBase
        yLogBase: surface.yLogBase
        minorNumbers: surface.minorNumbers
        tickTarget: surface.tickTarget

        title: surface.plotTitle
        xLabel: surface.xLabel
        yLabel: surface.yLabel

        commonAxis: surface.sharedAxis
        sideAxes: surface.separateAxes

        markers: surface.showMarkers
        markerSize: Theme.plotMarkerSize
        showCursor: surface.showCursor && !surface.selecting

        // Declared in here rather than beside the frame, which is what makes
        // it part of the picture: copyImage() grabs this item, and a caption
        // that was a sibling of the frame would be a caption on everything but
        // the copy. PlotFrame stays ignorant of it -- that file knows the
        // numbers and not the names, and this is handed the surface instead.
        PlotOverlayLegend {
            objectName: "plotOverlayLegend"

            target: surface
            corner: surface.legendCorner
            area: frame.area
        }
    }

    /// What the pointer is over, snapped to the nearest drawn sample:
    /// `{ valid, line, x, y, px, py }`. Invalid when the pointer is elsewhere
    /// or the reader has turned the cursor off.
    readonly property var reading: frame.reading

    /// The same thing written out, for the bar below the plot to print.
    ///
    /// It used to be a box floating in the corner of the pane. That is where a
    /// plotting library puts it and it is the wrong place here, because this
    /// application already has a strip along the foot of every view whose whole
    /// job is to say what is on screen in numbers -- and a second readout in a
    /// second style, over the top of the picture, is a second convention.
    ///
    /// The frame knows the numbers; only the thing being drawn knows the names,
    /// and the index it hands back is a position in the drawn set rather than a
    /// row of any table.
    readonly property var readingFacts: {
        if (!surface.reading.valid)
            return []
        const facts = []
        const drawn = surface.plot ? surface.plot.drawnSeries : []
        // Which line, and only when there is more than one -- "which" has no
        // answer worth printing about a plot of a single line, and a line's
        // name can be a bare row index, which read as a stray number between
        // two facts that were labelled.
        if (drawn.length > 1) {
            const position = surface.reading.line
            if (position >= 0 && position < drawn.length) {
                facts.push(qsTr("line %1")
                           .arg(surface.plot.seriesLabel(drawn[position])))
            }
        }
        facts.push(qsTr("x %1").arg(surface.readingNumber(surface.reading.x)))
        facts.push(qsTr("y %1").arg(surface.readingNumber(surface.reading.y)))
        return facts
    }

    /// A value on an axis, written the way that axis writes its own ticks.
    ///
    /// Asked of the frame rather than worked out again: `labelFor` takes its
    /// decimals off the span on screen -- enough to tell two ticks apart and no
    /// more -- so a number drawn beside the band and the numbers printed under
    /// the axis it is drawn over are one rule, and they agree as the reader
    /// zooms. Six significant figures everywhere was the other thing it could
    /// be, and on an axis running 0 to 100 that is "30.0119" against ticks
    /// reading 20, 40, 60: four digits of noise about a position nobody can
    /// point at that precisely.
    ///
    /// `xLogNumbers` rather than `xLog`, for the same reason the ticks
    /// themselves are written from it: a logarithmic axis zoomed inside a
    /// decade is numbered the linear way, and a band's readout that did not
    /// follow it there would print a different number from the tick beside it.
    function xNumber(value) {
        return frame.axisLabel(value, surface.viewMinX, surface.viewMaxX,
                               frame.xLogNumbers)
    }

    function yNumber(value) {
        return frame.axisLabel(value, surface.viewMinY, surface.viewMaxY,
                               frame.yLogNumbers)
    }

    /// One reading, written. Six significant figures, except for a whole
    /// number: the default x axis is the element's own index, and "12.0000" is
    /// four digits of decoration on a count.
    ///
    /// Not the same question as xNumber above, and deliberately not answered
    /// the same way: the crosshair reads a *sample*, whose value is a
    /// measurement and is worth all the digits it was taken with, while a band
    /// reads positions in the view, where a digit finer than the pane can
    /// resolve is a digit nobody asked for.
    function readingNumber(value) {
        if (!isFinite(value))
            return String(value)
        return Number.isInteger(value) ? String(value) : value.toPrecision(6)
    }

    // --- taking the picture away ------------------------------------------
    /// Put what is drawn on the clipboard, as an image.
    ///
    /// The frame and not this item: the frame is the plot -- the ground, the
    /// rules, the ticks, their labels, the title, the axis names, the strokes
    /// and the caption in the corner. What it leaves out is the legend panel
    /// on the left, which is a control the reader opens to decide which lines
    /// there are rather than a part of the drawing. That is what the legend on
    /// the plot is for, and it is why that one lives inside the frame.
    ///
    /// The grab is asynchronous and nothing here waits for it; ImageClipboard
    /// says how it went, and the settings panel prints that.
    ///
    /// What is grabbed is no longer this window's own frame but a second one
    /// built for the purpose (PlotPicture.qml), because four things under
    /// Settings > Plot Settings ask the picture to differ from the pane --
    /// publication colours, a size of the reader's choosing, a density of
    /// their choosing, the crosshair in or out -- and none of the first three
    /// can be had by re-styling the frame on screen: a grab renders the scene
    /// as it stands, so the picture would be bought with a frame of the
    /// application in the wrong colours, at the wrong size and with its type
    /// at the wrong one.
    function copyImage() {
        if (!surface.drawable)
            return false

        // A second press cancels the first, which is what ImageClipboard does
        // with the grab itself; the picture the abandoned grab was of goes
        // with it rather than waiting for a `copied` that will never come.
        surface.dropPicture()

        // Two sizes and not one, which is the whole of what Settings > Plot
        // Settings offers here: `page` is what the picture is *composed* at,
        // in logical units, and `asked` is the pixels it is *rendered* into.
        // The dpi is the ratio between them, so a figure stated at 1920 by
        // 1080 comes back at 1920 by 1080 whatever the density -- composed in
        // fewer units as the density rises, which is what makes the type on
        // it grow to its true point size.
        //
        // Both answered by AppController rather than worked out here: which
        // of the two the reader stated is its question, and a second reading
        // of it in QML is a second answer waiting to disagree with the one
        // the dialog prints.
        const page = AppController.plotExportLayout(frame.width, frame.height,
                                                    surface.pixelRatio)
        const asked = AppController.plotExportPixels(frame.width, frame.height,
                                                     surface.pixelRatio)
        if (page.width <= 0 || page.height <= 0)
            return false

        const publication = AppController.plotExportPublication
        picture = pictureComponent.createObject(surface, {
            pageWidth: page.width,
            pageHeight: page.height,
            surface: surface,
            sourceLines: frame.lines,
            publication: publication,
            includeCursor: AppController.plotExportCursor
        })
        if (!picture)
            return false
        if (!picture.take()) {
            surface.dropPicture()
            return false
        }

        // The grab is told the pixels, and the item it is given was laid out
        // in units -- so this is where the two meet and the density is
        // applied. Rounding the composition to whole units leaves the ratio a
        // fraction of a percent off the density asked for, which is a
        // sub-pixel stretch nobody can see; the alternative is handing the
        // reader a pixel count that is not the one they typed.
        const started = ImageClipboard.copyItem(
            picture, asked, publication, AppController.plotExportTaggedDpi())
        if (!started)
            surface.dropPicture()
        return started
    }

    /// The picture waiting for its grab, and nothing the rest of the time.
    ///
    /// It has to outlive the call that made it -- a grab is one more frame
    /// rendered, and there is no frame to render from inside a QML call -- and
    /// it must not outlive the answer, because it is a second copy of every
    /// drawn line.
    property var picture: null

    function dropPicture() {
        if (picture) {
            picture.destroy()
            picture = null
        }
    }

    // Whichever way the grab went, the picture has been read from and is done.
    // Every surface in the window hears this and drops its own, which is
    // right: a copy started while another was in flight cancels that one, and
    // the cancelled grab never answers.
    Connections {
        target: ImageClipboard

        function onCopied() { surface.dropPicture() }
        function onFailed(reason) { surface.dropPicture() }
    }

    Component {
        id: pictureComponent

        PlotPicture {}
    }

    // Ctrl+C with the pointer over the pane, which is the other half of what
    // was asked for -- a reader looking at a plot should not have to open a
    // panel to take it away.
    //
    // A Shortcut rather than a Keys handler because neither the plot nor the
    // frame holds the keyboard: the focus is wherever the reader last typed,
    // usually the slice bar or the tree's filter, and a plot that stole it to
    // offer a copy would take the caret out of a line somebody was writing.
    //
    // `frame.hovered` is what makes it unambiguous with more than one plot in
    // the window: only one pane can be under the pointer, so only one of these
    // is ever enabled. Nothing else in this application binds Ctrl+C.
    Shortcut {
        sequences: [StandardKey.Copy]
        enabled: surface.active && surface.drawable && frame.hovered
        onActivated: surface.copyImage()
    }

    /// Hand the lines over and dress them.
    ///
    /// One crossing into C++ for the whole plot rather than one per line, and
    /// no points built on the way: see DatasetPlot::fill. The lines are
    /// borrowed, so the model empties the item before it frees them -- which is
    /// why there is nothing here to tear down.
    function refill() {
        // Here as well as on every change of the view, because a plot object
        // that has just been reset -- a new dataset, a rearranged table -- has
        // forgotten what was on screen, and this is the first moment it is
        // being spoken to again.
        surface.pushColumns()
        surface.pushRange()
        if (!surface.drawable) {
            frame.lines.clear()
            return
        }
        surface.plot.fill(frame.lines)
        surface.restyle()
    }

    /// Re-colour what is drawn, without re-reading or re-filling it.
    ///
    /// The line this replaced said: "Qt Graphs redraws a series when its
    /// points change and not when its colour does, so a recoloured line keeps
    /// its old stroke on screen until something marks it dirty. Re-filling is
    /// what marks it." Sixty-four crossings into C++, each building a couple of
    /// thousand QPointF, to change a hue. A colour is now a property of the
    /// line and the item redraws from the values it already has.
    function restyle() {
        const drawn = surface.plot ? surface.plot.drawnSeries : []
        for (let i = 0; i < drawn.length; ++i) {
            // Two indices, and the difference between them is what a palette
            // holds still by: `i` is the line's place in what was handed to the
            // item, `drawn[i]` is which line of the legend it is. The colour
            // answers to the second, the way the opacity and the width already
            // did.
            frame.lines.setSeriesColor(
                i, surface.seriesColor(drawn[i], i, drawn.length))
            frame.lines.setSeriesOpacity(i, surface.seriesOpacity(drawn[i],
                                                                   drawn.length))
            frame.lines.setSeriesWidth(i, surface.seriesWidth(drawn[i]))
        }
        surface.pushSeriesAxes()
    }

    /// Tell the plot object what is on screen.
    ///
    /// Not a setting and nothing is drawn from it: it is what lets the object
    /// decide whether the lines are worth reading again at a finer bucket, and
    /// the answer is usually no -- a range that resolves to the run already in
    /// hand costs nothing at all on the other side. The read, when there is
    /// one, waits for the gesture to stop and then goes out asynchronously, so
    /// nothing here waits for it and no frame is missed.
    ///
    /// Guarded by `active` like every other path into the plot object: a reader
    /// browsing a large dataset as a table must not pay for a closer look at a
    /// plot nobody has asked to see.
    function pushRange() {
        if (!surface.active || !surface.plot)
            return
        surface.plot.setVisibleRange(surface.viewMinX, surface.viewMaxX)
    }

    /// Tell it where the reader is zooming, and which way.
    ///
    /// Pushed before the axis moves, so that the range arriving a moment later
    /// -- through the bindings, in the same turn -- is read towards the pointer
    /// rather than towards the middle of the frame. Guarded by `active` like
    /// every other path into the plot object.
    function pushZoomFocus(x, factor) {
        if (!surface.active || !surface.plot)
            return
        surface.plot.setZoomFocus(x, factor)
    }

    function clearZoomFocus() {
        if (!surface.active || !surface.plot)
            return
        surface.plot.clearZoomFocus()
    }

    /// Tell it how wide the pane is, in columns.
    ///
    /// A bucket is a column: what a line is thinned to is a property of the
    /// pane it is drawn in and not a constant, and the number used to be a
    /// constant in both directions -- fewer buckets than pixels on a wide
    /// screen, which draws an envelope as a hatch of separated teeth instead of
    /// a band, and more points than anyone can tell apart on a narrow one.
    ///
    /// The frame rather than the plot area inside it, which is not a rounding:
    /// the area's left gutter is measured off the widest y tick label, that
    /// label is measured off the extent of the data, and the extent moves when
    /// the resolution does -- a finer bucket can find a more extreme value. So
    /// a resolution taken from the area is a resolution that decides the gutter
    /// that decides the resolution, which is exactly the binding loop Qt
    /// reported the first time this was written that way. The frame's own width
    /// depends on nothing the plot draws.
    ///
    /// It overestimates by the gutters, which is what the renderer's tolerance
    /// for a few points per column is for -- see kSamplesPerColumn. Quantised
    /// on the other side, so dragging the window's edge does not re-read the
    /// file once a pixel.
    ///
    /// In *device* pixels, which is the other half of "a bucket is a column".
    /// A pane is laid out in logical pixels and drawn into a framebuffer with
    /// devicePixelRatio of them for each one, so thinning to the logical width
    /// on a HiDPI screen hands the renderer one bucket per two or three
    /// physical columns -- a band drawn at half or a third of the resolution
    /// the display has, which is the whole of what oversampling would have
    /// bought and is free to ask for correctly instead.
    function pushColumns() {
        if (!surface.active || !surface.plot)
            return
        surface.plot.setPaneColumns(
            Math.round((frame.width - frame.gutterRight) * surface.pixelRatio))
    }

    /// Device pixels per logical one, which is the other factor in the pane's
    /// width. A property rather than a call so that dragging the window onto a
    /// display with a different scaling re-thins the lines for it -- a resize
    /// would otherwise be the only thing that ever noticed.
    ///
    /// Unqualified `Screen`, which is how an attached property is reached: it
    /// attaches to the object whose scope names it, and that is this item.
    readonly property real pixelRatio: Math.max(1, Screen.devicePixelRatio)

    onPixelRatioChanged: surface.pushColumns()

    onViewMinXChanged: surface.pushRange()
    onViewMaxXChanged: surface.pushRange()

    // A window does not survive a change of scale, and cannot: the pan is a
    // distance along the axis, and the axis has just changed what a distance
    // along it is. A pan of forty is four decades or it is forty units, and
    // there is no reading of the number that is both -- so the reader who
    // ticks the box gets the whole of the data on the new scale, which is the
    // picture they asked to see.
    onXLogChanged: surface.resetView()
    onYLogChanged: surface.resetView()
    // ...and so does a change of base, for exactly the reason: the pan is a
    // count of powers, and the base is what says how big a power is. Four
    // decades and four octaves are not the same distance along the same axis.
    onXLogBaseChanged: { if (surface.xLog) surface.resetView() }
    onYLogBaseChanged: { if (surface.yLog) surface.resetView() }

    // A resized window is a different number of columns, and what a line is
    // thinned to follows it. Quantised on the other side, so a drag of the
    // frame's edge re-reads every sixty-four pixels rather than every one.
    Connections {
        target: frame
        function onWidthChanged() { surface.pushColumns() }
    }

    Component.onCompleted: surface.refill()
    onActiveChanged: refillSoon.restart()

    /// Refill at the end of the turn, once, however many things have asked.
    ///
    /// `Qt.callLater(surface.refill)` did this, and it is the wrong tool here:
    /// the delayed queue holds the *function*, not the object it came from, so
    /// a refill armed in the turn a custom tab is closed runs after the surface
    /// has been destroyed. Every run of the QML suite printed two of them --
    /// "QQmlVMEMetaObject: Internal error - attempted to evaluate a function in
    /// an invalid context", followed by a TypeError on the first property
    /// refill() touches. Closing a tab is the case, and there are two of them
    /// because two surfaces are torn down: the plot object says `changed` as it
    /// is dismantled and the tab's `active` goes false, and each of those arms
    /// a call whose object is gone before it runs.
    ///
    /// What kept that from being a crash rather than a warning is that *every*
    /// property of a destroyed surface fails to resolve, so the function threw
    /// on its first line instead of reaching `plot.fill(frame.lines)` with an
    /// item that no longer exists. That is luck, not a design, and it is the
    /// kind of luck that reads as noise in the log until the day it does not
    /// hold.
    ///
    /// A Timer is a child of this item: it is destroyed with the surface and
    /// its pending fire goes with it. Interval zero and `restart()` coalesce
    /// exactly as callLater did -- the same idiom ObjectTree's `reveal` uses,
    /// for the same reason.
    Timer {
        id: refillSoon

        interval: 0
        onTriggered: surface.refill()
    }

    // Colour and emphasis are assigned to the lines rather than bound, because
    // a line belongs to a C++ item and not to the QML object tree; but which
    // lines exist has not changed, so these restyle rather than re-fill.
    onColorModeChanged: surface.restyle()
    onColorSingleChanged: surface.restyle()
    onColorRangeFromChanged: surface.restyle()
    onColorRangeToChanged: surface.restyle()
    onColorsReversedChanged: surface.restyle()
    onColorFromChanged: surface.restyle()
    onColorToChanged: surface.restyle()
    onHighlightedChanged: surface.restyle()

    // The palettes are the theme's own -- the same hues solved for whichever
    // ground the plot is standing on -- so flipping the theme changes what
    // every line is drawn in, and by how much (plotSeriesOpacity is the
    // theme's too). None of that reaches the series on its own: the line above
    // says why these are assignments and not bindings, and an assignment does
    // not re-run when what it was computed from changes. Without this, View ->
    // Dark Theme repainted the whole application and left the plot drawn in
    // the palette of the theme the reader had just left.
    Connections {
        target: Theme
        function onDarkChanged() { surface.restyle() }
    }

    Connections {
        target: surface.plot
        enabled: surface.active
        function onChanged() { refillSoon.restart() }
        // The same lines, moved along x. Nothing has to be re-read and no line
        // has appeared or gone away -- but where a point sits along x is the
        // axis the item was handed, so it has to be handed the new one.
        function onXAxisChanged() { refillSoon.restart() }
    }

    /// The properties a saved view keeps.
    ///
    /// How the lines are drawn and where the x axis runs -- not the zoom, the
    /// pan or the highlighted line, which are where the reader happens to be
    /// looking rather than what they arranged. Restoring a view should put the
    /// picture back, not the scroll position.
    readonly property var drawingSettingNames: [
        "rangeStart", "rangeStep", "rangeStop", "locks",
        "colorMode", "colorSingle", "colorRangeFrom", "colorRangeTo",
        "colorsReversed", "colorFrom", "colorTo",
        "gridMode", "gridStepX", "gridStepY",
        "xLog", "yLog", "minorNumbers",
        "xLogBaseMode", "yLogBaseMode", "xLogBaseCustom", "yLogBaseCustom",
        "showMarkers", "showCursor",
        "plotTitle", "xLabel", "yLabel",
        "legendOnPlot", "legendCorner"
    ]

    /// Those properties as plain data, for something to write down.
    function drawingSettings() {
        const values = {}
        for (let i = 0; i < surface.drawingSettingNames.length; ++i) {
            const name = surface.drawingSettingNames[i]
            values[name] = surface[name]
        }
        return values
    }

    /// ...and back again. Names this build does not know are passed over
    /// rather than refused, which is the stance the pipeline takes on a
    /// remembered step it cannot read: a setting that has gone away costs the
    /// reader that setting, not the whole of what they saved.
    function applyDrawingSettings(values) {
        if (!values)
            return
        for (let i = 0; i < surface.drawingSettingNames.length; ++i) {
            const name = surface.drawingSettingNames[i]
            if (values.hasOwnProperty(name))
                surface[name] = values[name]
        }
        // A view saved before the grid had four densities carries `showGrid`
        // and no `gridMode`. Passing it over would have been within the rule
        // above -- a setting that has gone away costs the reader that setting
        // -- except that this one has not gone away, it has grown: "on" is
        // exactly what `loose` now means, and a view saved with the grid
        // turned off would otherwise come back with it on.
        //
        // Read only when the new name is absent, so a view written by this
        // build is never second-guessed by one written by an older one.
        if (!values.hasOwnProperty("gridMode")
                && values.hasOwnProperty("showGrid")) {
            surface.gridMode = values["showGrid"] ? "loose" : "none"
        }
    }

    // A window onto one dataset says nothing about the next one, and neither
    // does an x axis, a colour cycle or a line picked out of the bundle. All of
    // them are in the list below, so a fresh dataset opens on the defaults and
    // one the reader has been at before opens where they left it. Rearranging
    // the same dataset is not a new selection and disturbs none of it.
    DatasetMemory {
        subject: surface
        group: surface.memoryGroup
        names: ["rangeStart", "rangeStep", "rangeStop", "locks",
                "colorMode", "colorSingle", "colorRangeFrom", "colorRangeTo",
                "colorsReversed", "colorFrom", "colorTo",
                "gridMode", "gridStepX", "gridStepY",
                // Before the zoom and the pan, and that order is load-bearing:
                // DatasetMemory puts the names back in the order they are
                // written here, and changing a scale resets the view. Restored
                // after them, a remembered logarithmic axis would throw away
                // the remembered window in the same breath.
                "xLog", "yLog", "minorNumbers",
                "xLogBaseMode", "yLogBaseMode",
                "xLogBaseCustom", "yLogBaseCustom",
                "showMarkers", "showCursor", "highlighted",
                "plotTitle", "xLabel", "yLabel",
                "legendOnPlot", "legendCorner",
                "zoomX", "panX", "zoomY", "panY"]
    }

    // Which way round the lines are read is the plot object's own, and it
    // chooses for itself on every new selection -- a vector has to draw as one
    // line rather than as a thousand. A choice the reader made outranks that,
    // so it is remembered; a dataset they have not been to keeps the object's.
    DatasetMemory {
        subject: surface.plot
        group: surface.plotMemoryGroup
        restoresDefaults: false
        names: ["seriesFromRows"]
    }

    // --- zoom and pan ----------------------------------------------------
    // Over the frame rather than inside it. This began as a way around
    // GraphsView's own wheel and drag handlers, which zoomed about the centre
    // of the frame rather than about the pointer; it stays because zoom and pan
    // are properties of the *view* and not of the drawing, so they belong to
    // the object that owns the window onto the data.
    Item {
        objectName: "plotGestures"

        anchors.fill: parent
        // The same inset as the graph, so a pointer position in this item is a
        // pointer position in the graph's own coordinates -- which is what
        // plotRect and every gesture below are measured against.
        anchors.leftMargin: surface.contentLeft
        // Nothing to look at is nothing to zoom. The same condition the
        // message above answers to: an axis with no value it can place has no
        // window either, and a drag over one would be moving a view whose ends
        // are not numbers.
        enabled: surface.drawable && surface.logReason === ""

        WheelHandler {
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            // acceptedModifiers is deliberately left alone. Its default is
            // Qt.KeyboardModifierMask, which means "whatever is held"; naming
            // Shift and Control there would require *both* of them at once,
            // which is the opposite of what this reads.
            //
            // One notch is 120 eighths of a degree; a trackpad sends fractions
            // of that, and the exponential keeps both feeling the same. Held
            // down, Shift turns a wheel's vertical delta into a horizontal one
            // on several platforms, so the horizontal one is read when there is
            // no vertical one -- the notch the reader turned, wherever the
            // window system filed it.
            onWheel: (event) => {
                const turned = event.angleDelta.y !== 0 ? event.angleDelta.y
                                                        : event.angleDelta.x
                surface.zoomAt(event.x, event.y, Math.pow(1.25, turned / 120),
                               surface.zoomAxesFor(event.modifiers))
            }
        }

        DragHandler {
            id: panner

            target: null
            cursorShape: active ? Qt.ClosedHandCursor : Qt.OpenHandCursor

            /// The translation already applied. DragHandler reports the whole
            /// movement since the press, and the axes want the step.
            property point applied: Qt.point(0, 0)

            onActiveChanged: applied = Qt.point(0, 0)
            onActiveTranslationChanged: {
                surface.panBy(activeTranslation.x - applied.x,
                              activeTranslation.y - applied.y)
                applied = activeTranslation
            }
        }

        // The way back, without hunting for a button: the same gesture every
        // map and image viewer uses, and the reason it is worth a gesture at
        // all is that it costs nothing -- the whole-line summary is never
        // thrown away, so the most zoomed-out picture is always already in hand
        // and going to it is a draw rather than a read.
        //
        // A MouseArea rather than a TapHandler, for exactly the reason
        // ObjectTree gives at length: TapHandler counts its own taps against
        // the platform's double-click interval, and anything that takes the
        // grab in between resets the count -- here the drag handler above, which
        // takes a passive grab on every press. A MouseArea counts nothing. It
        // answers the QEvent::MouseButtonDblClick the window system itself
        // sends, which is a double click by the reader's own settings rather
        // than by this program's arithmetic.
        //
        // Panning still works, and by the same arrangement a Flickable uses: the
        // drag handler holds a passive grab through the press and takes the
        // exclusive one the moment the pointer moves past the drag threshold.
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
            onDoubleClicked: surface.resetView()
        }

        // --- the region band ---------------------------------------------
        // The right button draws a rectangle and the view goes to it. It is
        // the gesture every plot in every field has for "look at this part",
        // and the one this plot did not: the wheel zooms about a point and a
        // reader who can see the region they want had to arrive at it by
        // turning the wheel and correcting with a drag, by eye, several times.
        //
        // The right button rather than a modifier on the left, because the
        // left is already the pan and the two would have to be told apart by a
        // key held down before the press. Nothing else on this pane uses it --
        // the legend's rows do, but those are in the panel and not here.
        DragHandler {
            id: band

            objectName: "plotRegionDrag"

            target: null
            acceptedButtons: Qt.RightButton

            /// Where the press was, and where the pointer is now. Held rather
            /// than read off the centroid at draw time so that the rectangle
            /// below depends on two plain points and stops existing the moment
            /// the gesture does.
            property point from: Qt.point(0, 0)
            property point to: Qt.point(0, 0)

            onActiveChanged: {
                if (band.active) {
                    band.from = centroid.pressPosition
                    // Where the pointer is now, and not where it was pressed.
                    // A drag handler takes the gesture once the pointer has
                    // travelled the drag threshold, and the move that carried
                    // it there is the same event this is answering -- the
                    // centroid had already moved when `active` turned true, so
                    // reading the press position here opened every band as a
                    // rectangle of nothing that the next move corrected.
                    // Nobody saw that while a band was only a shape; a band
                    // that writes its own width said "0" for a frame.
                    band.to = centroid.position
                    return
                }
                // Released. A band too small to have been meant is dropped
                // without moving anything -- see zoomToRegion.
                surface.zoomToRegion(band.from.x, band.from.y,
                                     band.to.x, band.to.y)
            }

            onCentroidChanged: {
                if (band.active)
                    band.to = centroid.position
            }
        }

        // What the reader is about to ask for, drawn while they are deciding,
        // with the numbers that say what it is. In this layer rather than in
        // the frame: a band exists only during a gesture, so it is never part
        // of the picture that leaves this application -- see copyImage, which
        // grabs the frame.
        PlotBand {
            objectName: "plotBand"

            target: surface
            area: surface.plotRect
            active: band.active
            from: band.from
            to: band.to
        }
    }

    /// Which line is which. Over the plot rather than beside it: it belongs to
    /// the picture, and the reader asks for it with the pointer already there.
    property alias legendOpen: legendPanel.open

    PlotLegend {
        id: legendPanel

        objectName: "plotLegend"

        anchors.top: parent.top
        anchors.bottom: parent.bottom
        target: surface
    }

    /// What to say when there is nothing drawn and the plot object itself has
    /// no complaint to make. This is the only part of the message that is
    /// about *what* is being plotted rather than about plotting, so it is the
    /// only part a custom tab replaces; the error branch above it is the plot
    /// object's own words either way.
    property string idleReason: {
        if (!AppController.datasetTabVisible)
            return qsTr("Select a dataset in the tree to plot its values.")
        if (!AppController.datasetIsNumeric)
            return qsTr("%1 holds no numbers. Only a numeric dataset can be plotted.")
                   .arg(AppController.currentPath)
        return qsTr("The selected slice has no finite values in it.")
    }

    /// Why a logarithmic axis has nothing to draw, or "".
    ///
    /// A logarithmic axis cannot place a value at or below zero, so a reader
    /// who ticks the box over data that has nothing above zero gets an empty
    /// pane. That is the correct picture -- there is no place on such an axis
    /// that would be a true reading of any of those values -- and an empty
    /// pane on its own says none of it: the plot was there a moment ago and
    /// the only thing that changed was a checkbox.
    ///
    /// Asked of what the axes were *offered* rather than of what they settled
    /// on, which is the half that is easy to get backwards. Both of them fall
    /// back to a nominal decade when there is nothing to draw between -- see
    /// xBounds and `padded` -- so the bounds themselves look perfectly healthy
    /// in exactly the case this has to catch.
    readonly property string logReason: {
        if (!surface.drawable)
            return ""
        if (surface.yLog && surface.sharedAxis && !(surface.plot.maximum > 0))
            return qsTr("Nothing here is above zero, and a logarithmic y axis "
                        + "has no place to draw a value that is not.")
        if (surface.xLog && !(surface.positiveMinX > 0
                              && surface.axisMaxX > surface.positiveMinX))
            return qsTr("No x here is above zero, and a logarithmic x axis "
                        + "has no place to draw a value that is not.")
        return ""
    }

    ViewMessage {
        objectName: "plotIdleMessage"

        anchors.fill: parent
        anchors.leftMargin: surface.contentLeft
        visible: !surface.drawable || surface.logReason !== ""
        title: surface.logReason !== "" ? qsTr("nothing on this scale")
                                        : qsTr("nothing to plot")
        warning: surface.active && surface.plot
                 && surface.plot.error !== ""
        text: {
            // Reading `error` samples, so nothing is asked of a plot that is
            // not the presentation on screen.
            if (!surface.active || !surface.plot)
                return ""
            if (surface.logReason !== "")
                return surface.logReason
            if (surface.plot.error !== "")
                return surface.plot.error
            return surface.idleReason
        }
    }


}
