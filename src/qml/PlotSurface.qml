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
    property bool showGrid: true
    property bool showMarkers: false
    /// A logarithmic y axis: the first of the two things this plot could not do
    /// while it drew through Qt Graphs. 2-D Qt Graphs ships a value axis, a bar
    /// category axis and a date-time axis, and nothing logarithmic at any
    /// price -- the only log axis in the module is a formatter for the 3-D
    /// surfaces.
    property bool logY: false

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
    /// The default is the spectrum palette. This plot used to open on "same",
    /// one accent for every line, and separate them by overlap alone; that
    /// stops separating anything at about a dozen lines, and the legend that
    /// names them is no use when they all look alike. A map was the first
    /// answer to that and is the wrong shape for the question: it puts its
    /// neighbours next to each other by construction, so the lines it has to
    /// tell apart are the ones it draws most alike. A palette is built to do
    /// exactly this, so it is what a new plot opens on.
    property string colorMode: "spectrum"
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

    /// The colour line `position` of `count` takes.
    ///
    /// `count` is what the maps need and what a palette ignores: a share of a
    /// continuum only exists once you know how many shares there are, where a
    /// palette entry is the position itself. The rest of this note is about
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
    function seriesColor(position, count) {
        if (surface.colorMode === "same")
            return surface.colorSingle

        // A palette is asked which line this is rather than how far along it
        // sits, so none of the arithmetic below applies to one: not the shares
        // -- there is no continuum to take shares of -- and not the reader's
        // band. The position is the answer, counted along a cycle that
        // repeats, and it does not move when a line is added or taken away.
        // That is the second thing a palette buys over a map: on a map every
        // line changes colour when one of them is unticked.
        const palette = Theme.categoricalPalettes[surface.colorMode]
        if (palette) {
            return Theme.categoricalColor(
                palette,
                surface.colorsReversed ? palette.length - 1 - position
                                       : position)
        }

        let at = count > 0 ? (position + 1) / (count + 1) : 0.5
        if (surface.colorsReversed)
            at = 1 - at
        // Across the reader's own slice of the map rather than across the
        // whole of it. Untouched that slice is the whole of it, so the
        // arithmetic is a no-op until they say otherwise.
        at = surface.colorFrom + at * (surface.colorTo - surface.colorFrom)
        if (surface.colorMode === "range")
            return Theme.mix(surface.colorRangeFrom, surface.colorRangeTo, at)
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

    /// Ceiling on magnification. The sample behind the plot holds at most a
    /// couple of thousand points per line, so beyond this there is nothing
    /// further to resolve -- reading between two samples is the data settings
    /// panel's job, which subsets the dataset and re-reads it.
    readonly property real maxZoom: 256.0

    readonly property bool zoomed: zoomX !== 1.0 || zoomY !== 1.0
                                   || panX !== 0.0 || panY !== 0.0

    /// The plot area, in this item's coordinates: the frame minus the margins
    /// the axis labels live in. Every gesture below is measured against it,
    /// because a fraction of the whole item is not a fraction of the axis.
    readonly property rect plotRect: frame.area

    function resetView() {
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

    function zoomAt(px, py, factor) {
        const area = surface.plotRect
        if (area.width <= 0 || area.height <= 0)
            return
        const fx = Math.max(0, Math.min(1, (px - area.x) / area.width))
        // y grows downward on screen and upward on the axis.
        const fy = 1.0 - Math.max(0, Math.min(1, (py - area.y) / area.height))

        const x = surface.zoomedAxis(surface.zoomX, surface.panX,
                                     surface.axisMinX, surface.axisMaxX,
                                     fx, factor)
        const y = surface.zoomedAxis(surface.zoomY, surface.panY,
                                     surface.lowerBound, surface.upperBound,
                                     fy, factor)
        surface.zoomX = x.zoom
        surface.panX = x.pan
        surface.zoomY = y.zoom
        surface.panY = y.pan
    }

    /// Drag the view by a pointer movement. The content follows the pointer,
    /// so the axis moves the other way.
    function panBy(dx, dy) {
        const area = surface.plotRect
        if (area.width <= 0 || area.height <= 0)
            return
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
    /// Set explicitly because Qt Graphs computes its automatic spacing from
    /// the axis's *declared* range and not from the range it is showing, so a
    /// zoomed-in axis would keep the spacing of the whole dataset and print
    /// one lonely tick.
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
        logY: surface.logY
        showGrid: surface.showGrid
        tickTarget: surface.tickTarget

        markers: surface.showMarkers
        markerSize: Theme.plotMarkerSize
    }

    /// Hand the lines over and dress them.
    ///
    /// One crossing into C++ for the whole plot rather than one per line, and
    /// no points built on the way: see DatasetPlot::fill. The lines are
    /// borrowed, so the model empties the item before it frees them -- which is
    /// why there is nothing here to tear down.
    function refill() {
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
            frame.lines.setSeriesColor(i, surface.seriesColor(i, drawn.length))
            frame.lines.setSeriesOpacity(i, surface.seriesOpacity(drawn[i],
                                                                   drawn.length))
            frame.lines.setSeriesWidth(i, surface.seriesWidth(drawn[i]))
        }
    }

    Component.onCompleted: surface.refill()
    onActiveChanged: Qt.callLater(surface.refill)

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
        function onChanged() { Qt.callLater(surface.refill) }
        // The same lines, moved along x. Nothing has to be re-read and no line
        // has appeared or gone away -- but where a point sits along x is the
        // axis the item was handed, so it has to be handed the new one.
        function onXAxisChanged() { Qt.callLater(surface.refill) }
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
        "showGrid", "showMarkers", "logY"
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
                "showGrid", "showMarkers", "logY", "highlighted",
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
    // Over the graph rather than inside it: GraphsView carries handlers of its
    // own for the zoom and pan styles it implements, and those zoom about the
    // centre of the frame. The pointer is what a reader is aiming with, so the
    // wheel is taken here instead and turned into the axis arithmetic above.
    Item {
        anchors.fill: parent
        // The same inset as the graph, so a pointer position in this item is a
        // pointer position in the graph's own coordinates -- which is what
        // plotRect and every gesture below are measured against.
        anchors.leftMargin: surface.contentLeft
        enabled: surface.drawable

        WheelHandler {
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            // One notch is 120 eighths of a degree; a trackpad sends fractions
            // of that, and the exponential keeps both feeling the same.
            onWheel: (event) => {
                surface.zoomAt(event.x, event.y,
                               Math.pow(1.25, event.angleDelta.y / 120))
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
        // map and image viewer uses.
        TapHandler {
            onDoubleTapped: surface.resetView()
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
