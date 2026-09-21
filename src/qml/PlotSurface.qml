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

    // --- the y axis: the values, and nothing to set about them -----------
    /// A little air above and below, so a line at the extreme is a line and
    /// not part of the frame. A flat series has no span to take a share of,
    /// and gets a unit of room instead.
    ///
    /// Guarded like every other reader of the sample: `drawable` tests `active`
    /// first, so a hidden plot's bindings never reach the file.
    readonly property real padding: {
        if (!drawable)
            return 1.0
        const span = plot.maximum - plot.minimum
        return span > 0 ? span * 0.05 : 1.0
    }

    /// The extent of the values being drawn, which is the whole of the y axis.
    /// There is no manual band: the reader has the wheel and the drag for
    /// looking closer at part of it, and a second way to say the same thing --
    /// two boxes that also had to be kept from crossing, and that went stale
    /// the moment the selection moved -- is a control that earns nothing.
    readonly property real lowerBound: !surface.drawable
        ? 0.0 : surface.plot.minimum - surface.padding
    readonly property real upperBound: !surface.drawable
        ? 1.0 : surface.plot.maximum + surface.padding

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

    /// The window the axes are actually showing, worked out with the same
    /// arithmetic ValueAxis uses. The footer prints these, because once the
    /// view has been moved the range of the data is no longer the range on
    /// screen and only one of the two is worth reading.
    function visibleLow(low, high, zoom, pan) {
        return (low + high) / 2.0 + pan - (high - low) / zoom / 2.0
    }

    function visibleHigh(low, high, zoom, pan) {
        return (low + high) / 2.0 + pan + (high - low) / zoom / 2.0
    }

    readonly property real viewMinX:
        visibleLow(axisMinX, axisMaxX, zoomX, panX)
    readonly property real viewMaxX:
        visibleHigh(axisMinX, axisMaxX, zoomX, panX)
    readonly property real viewMinY:
        visibleLow(lowerBound, upperBound, zoomY, panY)
    readonly property real viewMaxY:
        visibleHigh(lowerBound, upperBound, zoomY, panY)

    /// Pan clamped so the visible window stays inside the data. With zoom at
    /// 1 the window *is* the data and the only legal pan is none.
    function clampPan(pan, zoom, low, high) {
        const room = (high - low) * (1.0 - 1.0 / zoom) / 2.0
        return Math.max(-room, Math.min(room, pan))
    }

    /// Zoom one axis by `factor` while holding the value at `fraction` of the
    /// visible span still -- which is what makes the wheel zoom into whatever
    /// the pointer is over rather than into the middle of the frame.
    ///
    /// Returns the new { zoom, pan } for the caller to assign, because QML has
    /// no out-parameters and two of these run per wheel tick.
    function zoomedAxis(zoom, pan, low, high, fraction, factor) {
        const next = Math.max(1.0, Math.min(surface.maxZoom, zoom * factor))
        const full = high - low
        const span = full / zoom
        const held = (low + high) / 2.0 + pan - span / 2.0 + fraction * span
        const nextSpan = full / next
        const centre = held - fraction * nextSpan + nextSpan / 2.0
        return { zoom: next,
                 pan: surface.clampPan(centre - (low + high) / 2.0, next, low, high) }
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
            surface.pushZoomFocus(surface.viewMinX
                                  + fx * (surface.viewMaxX - surface.viewMinX),
                                  factor)

            const x = surface.zoomedAxis(surface.zoomX, surface.panX,
                                         surface.axisMinX, surface.axisMaxX,
                                         fx, factor)
            surface.zoomX = x.zoom
            surface.panX = x.pan
        }
        if (axes !== "x") {
            const y = surface.zoomedAxis(surface.zoomY, surface.panY,
                                         surface.lowerBound, surface.upperBound,
                                         fy, factor)
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
    function viewedAxis(low, high, from, to) {
        const full = high - low
        const span = Math.abs(to - from)
        if (!(full > 0) || !(span > 0))
            return { zoom: 1.0, pan: 0.0 }
        const zoom = Math.max(1.0, Math.min(surface.maxZoom, full / span))
        const centre = (from + to) / 2.0
        return { zoom: zoom,
                 pan: surface.clampPan(centre - (low + high) / 2.0,
                                       zoom, low, high) }
    }

    /// Put the window at exactly these four numbers, in data coordinates.
    ///
    /// The one way in for everything that states a window rather than nudging
    /// one: the region band, and the four boxes under Plot Settings > View.
    /// Both of those are the reader saying where to look, which is the same
    /// thing said twice -- so it is arithmetic in one place and the boxes
    /// report the band's answer without either knowing about the other.
    function setViewRange(x0, x1, y0, y1) {
        const x = surface.viewedAxis(surface.axisMinX, surface.axisMaxX,
                                     Math.min(x0, x1), Math.max(x0, x1))
        const y = surface.viewedAxis(surface.lowerBound, surface.upperBound,
                                     Math.min(y0, y1), Math.max(y0, y1))
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
        return surface.viewMinX + at * (surface.viewMaxX - surface.viewMinX)
    }

    /// The same, down the other axis. y grows downward on screen and upward on
    /// the axis, so the top of the pane is the larger value.
    function dataYAt(py) {
        const area = surface.plotRect
        if (area.height <= 0)
            return surface.viewMinY
        const at = Math.max(0, Math.min(1, (py - area.y) / area.height))
        return surface.viewMinY
               + (1.0 - at) * (surface.viewMaxY - surface.viewMinY)
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

        const span = surface.viewMaxX - surface.viewMinX
        if (span > 0 && x1 > x0)
            surface.pushZoomFocus((x0 + x1) / 2.0, span / (x1 - x0))
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
        const spanX = (surface.axisMaxX - surface.axisMinX) / surface.zoomX
        const spanY = (surface.upperBound - surface.lowerBound) / surface.zoomY
        surface.panX = surface.clampPan(surface.panX - dx * spanX / area.width,
                                        surface.zoomX, surface.axisMinX,
                                        surface.axisMaxX)
        surface.panY = surface.clampPan(surface.panY + dy * spanY / area.height,
                                        surface.zoomY, surface.lowerBound,
                                        surface.upperBound)
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
        tickTarget: surface.tickTarget

        title: surface.plotTitle
        xLabel: surface.xLabel
        yLabel: surface.yLabel

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
    function xNumber(value) {
        return frame.labelFor(value, surface.viewMaxX - surface.viewMinX)
    }

    function yNumber(value) {
        return frame.labelFor(value, surface.viewMaxY - surface.viewMinY)
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
    /// publication colours, a size of the reader's choosing, a resolution of
    /// their choosing, the crosshair in or out -- and none of the first three
    /// can be had by re-styling the frame on screen: a grab renders the scene
    /// as it stands, so the picture would be bought with a frame of the
    /// application in the wrong colours and at the wrong size.
    function copyImage() {
        if (!surface.drawable)
            return false

        // A second press cancels the first, which is what ImageClipboard does
        // with the grab itself; the picture the abandoned grab was of goes
        // with it rather than waiting for a `copied` that will never come.
        surface.dropPicture()

        // Rounded once, here, because these two are used twice: they are the
        // size the picture is laid out at and they are what the resolution is
        // applied to. A frame whose width is a fraction of a logical pixel
        // would otherwise be truncated into the layout and rounded into the
        // grab, and the picture would be asked for at a size it was not.
        const custom = AppController.plotExportCustomSize
        const wide = Math.round(custom ? AppController.plotExportWidth
                                       : frame.width)
        const tall = Math.round(custom ? AppController.plotExportHeight
                                       : frame.height)

        const publication = AppController.plotExportPublication
        picture = pictureComponent.createObject(surface, {
            pageWidth: wide,
            pageHeight: tall,
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

        // The picture is *composed* at `wide` by `tall` and *rendered* at
        // however many pixels the resolution asks for; AppController is where
        // those two meet, and it is asked rather than reimplemented here so
        // that the number the dialog shows the reader and the number the grab
        // is asked for cannot differ. At the default resolution a point is a
        // pixel and a chosen size is given exactly, which is the picture this
        // application has always copied.
        const asked = AppController.plotExportPixels(wide, tall,
                                                     surface.pixelRatio)
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
        enabled: surface.drawable

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

    ViewMessage {
        anchors.fill: parent
        anchors.leftMargin: surface.contentLeft
        visible: !surface.drawable
        title: qsTr("nothing to plot")
        warning: surface.active && surface.plot
                 && surface.plot.error !== ""
        text: {
            // Reading `error` samples, so nothing is asked of a plot that is
            // not the presentation on screen.
            if (!surface.active || !surface.plot)
                return ""
            if (surface.plot.error !== "")
                return surface.plot.error
            return surface.idleReason
        }
    }


}
