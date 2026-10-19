// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import H5Scope.Backend

/// The Data Viewer's plot presentation: every row of the table as one line,
/// with x running along the columns -- and a custom tab's lines, drawn by the
/// same surface.
///
/// Which values those are is the data settings panel's business, not this
/// file's -- this is one more reading of exactly the table the grid shows, so
/// a rank-4 dataset plots on the same terms it browses.
///
/// The plot itself is plotcore's: its Figure decides the axes, their numbers
/// and rules, the gestures, the colours, the crosshair, the legend on the
/// plot and the band with what it writes, and FigureItem draws it on the
/// scene graph. That is the plot every other host of plotcore draws, so the
/// viewer and the library are one plot. Until 0.8 this file and PlotFrame.qml
/// were a second copy of it in QML, and a feature written into one of them --
/// the band's readout -- was missing from the other; the reasons written over
/// that QML went to plotcore with the arithmetic they were about.
///
/// What is left here is the reader's side of it: the settings, under the
/// names the settings panel writes, the dataset's memory files and a saved
/// view keeps; the x axis as a range of three numbers; the legend beside the
/// plot; the picture that leaves; and what to say when there is nothing to
/// draw. Each setting is handed to the figure as it is, and the figure is what
/// answers for what it does.
Item {
    id: surface

    // --- settings, written by PlotSettingsPanel -------------------------
    /// How closely the grid is ruled: "none", "loose", "dense" or "custom",
    /// and under "custom" how far apart the rules go on each axis. See the
    /// argument over plotcore's gridValues, which is where the four are drawn.
    property string gridMode: "loose"
    property real gridStepX: 0.0
    property real gridStepY: 0.0
    /// Whether each axis places a value by its logarithm.
    ///
    /// A property of the *axis* and not of the window: the four bounds stay in
    /// the data's own units, the ticks print the numbers the data has, and what
    /// changes is how far apart two of them are drawn. See plotcore's fractionOn
    /// (Ticks.hpp), which is the rule wherever the plot places a value.
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

    /// Whether x runs up the pane and y across it: the same plot with its axes
    /// swapped, as `plot(y, x)` would draw it. Offered on a custom tab, where
    /// a line may be a depth or an altitude profile that is read downwards.
    ///
    /// Only where things are *drawn* changes. The window is still a window
    /// onto x and onto y -- zoomX and panX are still about x, the models are
    /// still asked for a range of x -- so flipping moves nothing the reader
    /// has arranged and costs no read; it is the figure (plotcore's Figure, its
    /// frame and its strokes alike) that lays the same numbers out the other
    /// way, and its Camera that reads every gesture back through it.
    property bool flipped: false

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

    /// Whether `base` is a base at all, and the base a mode and its number
    /// resolve to -- ten for anything this build does not recognise. plotcore's
    /// usableBase (Ticks.cpp) and baseFor (Camera.cpp), where the argument for
    /// each is; asked here because the panel validates against them.
    function usableBase(base) {
        return figure.usableBase(base)
    }

    function baseFor(mode, custom) {
        return figure.baseFor(mode, custom)
    }

    /// Whether the subdivisions between two powers carry their digit. See
    /// plotcore's minorNumbersOf (Ticks.hpp) for where they appear.
    property bool minorNumbers: false
    property bool showMarkers: false
    /// Whether pointing at the plot reads the sample under the pointer: a
    /// crosshair on the plot and a line of numbers in the bar below it.
    property bool showCursor: true

    /// Whether every drawn line is drawn in a band of the pane of its own,
    /// the first at the top, each numbered beside the pane on its own axis:
    /// lines that would sit over one another on one axis laid out one under
    /// the next, as a strip chart lays them out (jonassattler/H5Scope#30).
    property bool stacked: false

    /// Whether a drawn line has been moved or scaled on its axis, and the way
    /// to put every line back where it was read. A line is moved by a drag
    /// with the middle button or with Alt, scaled by the wheel with Alt and
    /// Control, and put back alone by Control and a click on it; the plot
    /// keeps what was done to each line (LineModel::seriesMove).
    readonly property bool linesMoved: figure.linesMoved
    function putLinesBack() {
        figure.putLinesBack()
    }

    /// A title over the pane, and a name for each axis. Empty draws nothing
    /// and costs no room -- see plotcore's Figure::layout, whose gutters grow
    /// for these only when there is something in them.
    ///
    /// They are for the picture that leaves this application rather than for
    /// the one on screen: what is being drawn is already named in the slice
    /// bar above the plot and in the legend beside it, and neither of those
    /// goes to the clipboard.
    property string plotTitle: ""
    property string xLabel: ""
    property string yLabel: ""

    /// Whether the lines are named on the plot as well as beside it, and in
    /// which corner. See the legend on the plot in plotcore's Figure::compose
    /// for why a second legend exists at all: this one is a caption on the
    /// picture and is part of what "copy plot" copies.
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
    /// Whether the lines are a bundle: alike, many, and read by where they
    /// pile up, so drawn under full strength (see seriesOpacity). True for
    /// the Plot tab, whose lines are the rows or columns of one dataset. A
    /// custom tab says false -- its lines were each put there on purpose, and
    /// a second one must not wash out the first.
    property bool bundle: true
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

    /// The x axis's ends as it is actually drawn between them: its own, or one
    /// empty power of the base where a logarithmic x has nothing above zero
    /// to run between (plotcore's xBounds). The figure's answer.
    readonly property real axisLowX: figure.xBoundLow
    readonly property real axisHighX: figure.xBoundHigh

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
    /// The whole of the y axis, the padded extent of the lines drawn on the
    /// common axis -- or a nominal one when every line is on an axis of its
    /// own. The figure's answer (plotcore's padded); there is no manual band:
    /// the reader has the wheel and the drag for looking closer at part of it.
    readonly property real lowerBound: figure.yBoundLow
    readonly property real upperBound: figure.yBoundHigh

    /// Whether there is a common y axis at all. There is not when every drawn
    /// line has been given one of its own. Asked of the plot object, which
    /// counts it: DatasetPlot answers with every drawn line, because its lines
    /// are never separate.
    readonly property bool sharedAxis: !surface.drawable || !surface.plot
                                       || surface.plot.sharedSeriesCount > 0

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
    /// Whether the three colours above were chosen, which is what decides
    /// whether they are drawn -- ImageSurface.backgroundCustom's rule, for its
    /// reason: a colour somebody picked is a statement about this plot and a
    /// theme must not undo it, and one nobody picked should follow the theme.
    ///
    /// The bindings to Theme.accent and Theme.info above were meant to be
    /// that rule and could not be: DatasetMemory writes every name it
    /// remembers back as a value on the first selection, and a saved view
    /// writes them as values too, so the binding was gone before the reader
    /// saw a line. A plot opened on the dark theme then held signal white as
    /// a plain colour, and after View > theme a line in "same" was drawn
    /// white on the light theme's white ground -- no line at all.
    ///
    /// Set by the swatches in PlotSettingsPanel, which pin all three as they
    /// are drawn before changing one, so choosing the last line's colour does
    /// not move the first; cleared by its "follow the theme".
    property bool colorsCustom: false
    /// The three as they are drawn.
    readonly property color drawnColorSingle:
        surface.colorsCustom ? surface.colorSingle : Theme.accent
    readonly property color drawnColorRangeFrom:
        surface.colorsCustom ? surface.colorRangeFrom : Theme.accent
    readonly property color drawnColorRangeTo:
        surface.colorsCustom ? surface.colorRangeTo : Theme.info
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
    /// The colour of line `series`, drawn `position` of `count`, on the screen
    /// or, with `paper`, in the light scope a publication picture is drawn in.
    /// plotcore's seriesColor, which is where the argument for it is written:
    /// a palette by series so a line keeps its colour when another is hidden,
    /// a map by place at the middles of its shares. The legend beside the
    /// plot asks it, so that its swatches are the strokes' colours.
    function seriesColor(series, position, count, paper) {
        return figure.seriesColor(series, position, count, !!paper)
    }

    // --- the view ---------------------------------------------------------
    // The window as a zoom and a pan on each axis, which is what a dataset's
    // memory files and puts back; the figure holds it and moves it. See
    // plotcore's Camera for every rule a gesture keeps.
    property alias zoomX: figure.zoomX
    property alias panX: figure.panX
    property alias zoomY: figure.zoomY
    property alias panY: figure.panY
    readonly property bool zoomed: figure.zoomed
    /// How far x may be magnified, the narrowest x window that makes, and how
    /// much of the window a drag keeps over the data: plotcore's Camera.
    readonly property real maxZoom: figure.maxZoom
    readonly property real minimumSpanX: figure.minimumSpanX
    readonly property real panKeep: figure.panKeep
    readonly property real viewMinX: figure.viewMinX
    readonly property real viewMaxX: figure.viewMaxX
    readonly property real viewMinY: figure.viewMinY
    readonly property real viewMaxY: figure.viewMaxY

    /// The plot area, in the figure's coordinates -- inset from this item by
    /// contentLeft, the legend beside it -- minus the gutters the labels live
    /// in. Every gesture below is measured in the same coordinates.
    readonly property rect plotRect: figure.area

    function resetView() {
        figure.resetView()
    }

    /// Put the window at exactly these four numbers: the four boxes under
    /// Plot Settings > View.
    function setViewRange(x0, x1, y0, y1) {
        figure.setViewRange(x0, x1, y0, y1)
    }

    // The gestures, named, in the figure's coordinates -- what a test drives
    // the plot with. Each goes the way the pointer's would.
    function zoomAt(px, py, factor, axes) {
        figure.zoomAt(px, py, factor, axes)
    }

    function panBy(dx, dy) {
        figure.panBy(dx, dy)
    }

    function zoomToRegion(px0, py0, px1, py1) {
        return figure.zoomToRegion(px0, py0, px1, py1)
    }

    /// What a pixel stands for on each axis, as the band reads it.
    function dataXAt(px, py) {
        return figure.dataXAt(px, py === undefined ? 0 : py)
    }

    function dataYAt(py, px) {
        return figure.dataYAt(py, px === undefined ? 0 : px)
    }

    /// Where `value` sits along an axis, and the value `at` a fraction of a
    /// window: plotcore's axisPosition and valueAlong, the one place that
    /// knows what a logarithmic axis does to a position.
    function axisPosition(value, logarithmic, base) {
        return figure.axisPosition(value, logarithmic, base)
    }

    function valueAlong(low, high, at, logarithmic, base) {
        return figure.valueAlong(low, high, at, logarithmic, base)
    }

    /// Which axes a wheel event with these modifiers zooms: Shift alone is x,
    /// Ctrl alone is y, and everything else is both.
    function zoomAxesFor(modifiers) {
        const shift = (modifiers & Qt.ShiftModifier) !== 0
        const control = (modifiers & Qt.ControlModifier) !== 0
        if (shift && !control)
            return "x"
        if (control && !shift)
            return "y"
        return "both"
    }

    /// Whether a region is being drawn right now, which is when the crosshair
    /// is put away for it.
    readonly property bool selecting: figure.selecting

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

    // The plot. Everything that decides it is the figure's; this hands it the
    // settings, the faces and the x axis, and it hands the plot object the
    // window and asks it for its lines.
    FigureItem {
        id: figure

        objectName: "plotFigure"

        anchors.fill: parent
        anchors.leftMargin: surface.contentLeft
        // Nothing to look at is nothing to zoom. The same condition the
        // message answers to: an axis with no value it can place has no window
        // either, and a drag over one would be moving a view whose ends are
        // not numbers. Disabled, the figure takes no pointer at all, and so
        // reads nothing under it either.
        enabled: surface.drawable && surface.logReason === ""

        plot: surface.plot ? surface.plot : null
        active: surface.active && surface.sourceUsable
        xMinimum: surface.axisMinX
        xMaximum: surface.axisMaxX
        xPositiveMinimum: surface.positiveMinX
        dataLength: surface.dataLength

        grid: surface.gridMode
        gridStepX: surface.gridStepX
        gridStepY: surface.gridStepY
        xLog: surface.xLog
        yLog: surface.yLog
        xLogBase: surface.xLogBase
        yLogBase: surface.yLogBase
        flipped: surface.flipped
        minorNumbers: surface.minorNumbers
        markers: surface.showMarkers
        crosshair: surface.showCursor
        title: surface.plotTitle
        xLabel: surface.xLabel
        yLabel: surface.yLabel
        legend: surface.legendOnPlot
        legendCorner: surface.legendCorner
        colourMode: surface.colorMode
        colourSingle: surface.colorSingle
        colourRangeFrom: surface.colorRangeFrom
        colourRangeTo: surface.colorRangeTo
        coloursReversed: surface.colorsReversed
        colourFrom: surface.colorFrom
        colourTo: surface.colorTo
        coloursCustom: surface.colorsCustom
        highlighted: surface.highlighted
        bundle: surface.bundle
        stacked: surface.stacked
        dark: Theme.dark

        tickFont: Theme.readout
        tickMinorFont: Theme.readoutMinor
        titleFont: Theme.bodyStrong
        axisNameFont: Theme.bodySmall
        legendFont: Theme.monoSmall
        noteFont: Theme.micro
    }

    /// What the pointer is over, snapped to the nearest drawn sample:
    /// `{ valid, line, series, x, y, px, py, xText, yText }`, `line` being its
    /// place among the drawn lines. Invalid when the pointer is elsewhere, the
    /// reader has turned the crosshair off, or a band is being drawn.
    readonly property var reading: figure.reading

    /// The same thing written out, for the bar below the plot to print.
    ///
    /// It used to be a box floating in the corner of the pane. That is where a
    /// plotting library puts it and it is the wrong place here, because this
    /// application already has a strip along the foot of every view whose whole
    /// job is to say what is on screen in numbers -- and a second readout in a
    /// second style, over the top of the picture, is a second convention. So
    /// the figure is told not to write it (FigureItem.readout), and this
    /// writes it in the bar.
    ///
    /// The figure knows the numbers -- written as a reading is, to the digits
    /// the sample was taken with (plotcore's readingText) -- and only the
    /// thing being drawn knows the names.
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
        facts.push(qsTr("x %1").arg(surface.reading.xText))
        facts.push(qsTr("y %1").arg(surface.reading.yText))
        return facts
    }

    /// A value on an axis, written the way that axis writes its own ticks --
    /// what the band's corners are written in. The figure's answer.
    function xNumber(value) {
        return figure.xNumber(value)
    }

    function yNumber(value) {
        return figure.yNumber(value)
    }

    // --- taking the picture away ------------------------------------------
    /// Put what is drawn on the clipboard, as an image.
    ///
    /// The plot and not this item: the ground, the rules, the numbers, the
    /// title, the axis names, the strokes and the legend on the plot. What it
    /// leaves out is the legend panel on the left, which is a control the
    /// reader opens to decide which lines there are rather than a part of the
    /// drawing; that is what the legend on the plot is for.
    ///
    /// The figure lays the picture out again at the size asked for, in the
    /// page's colours when it is for publication, with the crosshair where it
    /// is now when Settings asks for it, and paints it -- a picture of the
    /// figure rather than a grab of the window, so it is not bought with a
    /// frame of the application in the wrong colours, and its ground can be
    /// left clear without the two grabs a clear ground used to need.
    ///
    /// Two sizes and not one, which is the whole of what Settings > Plot
    /// Settings offers here: `page` is what the picture is *composed* at, in
    /// logical units, and `asked` is the pixels it is *rendered* into. The dpi
    /// is the ratio between them, so a figure stated at 1920 by 1080 comes
    /// back at 1920 by 1080 whatever the density -- composed in fewer units as
    /// the density rises, which is what makes the type on it grow to its true
    /// point size. Both are AppController's answers, so the dialog's readout
    /// and this cannot be two.
    function copyImage() {
        if (!surface.drawable)
            return false
        const page = AppController.plotExportLayout(figure.width, figure.height,
                                                    surface.pixelRatio)
        const asked = AppController.plotExportPixels(figure.width, figure.height,
                                                     surface.pixelRatio)
        if (page.width <= 0 || page.height <= 0)
            return false
        const image = figure.picture(page.width, page.height,
                                     asked.width / page.width,
                                     AppController.plotExportPublication,
                                     AppController.plotExportCursor)
        return ImageClipboard.copyImage(image, AppController.plotExportTaggedDpi())
    }

    /// Device pixels per logical one, which is what a picture taken at the
    /// display's scale is drawn at.
    readonly property real pixelRatio: Math.max(1, Screen.devicePixelRatio)

    // Ctrl+C with the pointer over the pane, which is the other half of what
    // was asked for -- a reader looking at a plot should not have to open a
    // panel to take it away.
    //
    // A Shortcut rather than a Keys handler because neither the plot nor the
    // figure holds the keyboard: the focus is wherever the reader last typed,
    // usually the slice bar or the tree's filter, and a plot that stole it to
    // offer a copy would take the caret out of a line somebody was writing.
    //
    // `figure.hovered` is what makes it unambiguous with more than one plot in
    // the window: only one pane can be under the pointer, so only one of these
    // is ever enabled. Nothing else in this application binds Ctrl+C.
    Shortcut {
        sequences: [StandardKey.Copy]
        enabled: surface.active && surface.drawable && figure.hovered
        onActivated: surface.copyImage()
    }

    /// The properties a saved view keeps.
    ///
    /// How the lines are drawn and where the x axis runs -- not the zoom, the
    /// pan or the highlighted line, which are where the reader happens to be
    /// looking rather than what they arranged. Restoring a view should put the
    /// picture back, not the scroll position.
    readonly property var drawingSettingNames: [
        "rangeStart", "rangeStep", "rangeStop", "locks",
        "colorMode", "colorSingle", "colorRangeFrom", "colorRangeTo", "colorsCustom",
        "colorsReversed", "colorFrom", "colorTo",
        "gridMode", "gridStepX", "gridStepY",
        "xLog", "yLog", "minorNumbers",
        "xLogBaseMode", "yLogBaseMode", "xLogBaseCustom", "yLogBaseCustom",
        "showMarkers", "showCursor",
        "plotTitle", "xLabel", "yLabel",
        "legendOnPlot", "legendCorner",
        "flipped", "stacked"
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
        // ...and one saved before a colour could be left to the theme carries
        // the three colours and no `colorsCustom`. They are its reader's
        // choice wherever one of them is a colour no theme would have put
        // there; otherwise they are the defaults of whichever theme it was
        // saved under, and follow the one on screen now.
        if (!values.hasOwnProperty("colorsCustom")) {
            const chosen = (name, ofDark, ofLight) =>
                values.hasOwnProperty(name)
                && !Qt.colorEqual(values[name], ofDark)
                && !Qt.colorEqual(values[name], ofLight)
            surface.colorsCustom = chosen("colorSingle", Theme.sig500, Theme.n0)
                || chosen("colorRangeFrom", Theme.sig500, Theme.n0)
                || chosen("colorRangeTo", Theme.cyan500, Theme.cyan900)
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
                "colorMode", "colorSingle", "colorRangeFrom", "colorRangeTo", "colorsCustom",
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
                "legendOnPlot", "legendCorner", "stacked",
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

    // What the reader's gestures did to the lines, filed with the dataset they
    // were done on -- and, unlike the way round, put back to none for a dataset
    // with nothing filed. After the memory above, whose restoring the way
    // round forgets every move: these are moves of the places that way gives.
    DatasetMemory {
        subject: surface.plot
        group: surface.plotMemoryGroup === "" ? "" : surface.plotMemoryGroup + "Lines"
        names: ["lineMoves"]
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
    /// The figure's answer, and its title is the figure's too: it asks what
    /// the axes were *offered* rather than what they settled on, which is the
    /// half that is easy to get backwards, since both settle on a nominal
    /// decade when there is nothing to draw between.
    readonly property string logReason: surface.drawable ? figure.message : ""

    ViewMessage {
        objectName: "plotIdleMessage"

        anchors.fill: parent
        anchors.leftMargin: surface.contentLeft
        visible: !surface.drawable || surface.logReason !== ""
        title: surface.logReason !== "" ? figure.messageTitle
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
