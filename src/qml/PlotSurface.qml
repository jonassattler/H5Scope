// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import QtGraphs
import H5Scope.Backend

/// The Data Viewer's plot presentation: every row of the table as one line,
/// with x running along the columns.
///
/// Which values those are is the data settings panel's business, not this
/// file's -- this is one more reading of exactly the table the grid shows, so
/// a rank-4 dataset plots on the same terms it browses.
///
/// The points never cross into QML. AppController.datasetPlot samples the file
/// and loads each series through fill(), which is one bulk replace; a graph
/// filled a point at a time from JavaScript redraws itself on every one of
/// them, and a dataset has thousands.
///
/// Qt Graphs rather than Qt Charts: Charts draws through the Qt Widgets
/// graphics scene and asserts without a QApplication, which is the whole
/// widgets stack pulled into a Qt Quick application to draw polylines.
Item {
    id: surface

    // --- settings, written by PlotSettingsPanel -------------------------
    property bool showGrid: true
    property bool showMarkers: false

    /// Whether this is the presentation on screen. Reading `plot.hasData`
    /// samples the file, so every path into the plot is guarded by this: a
    /// reader browsing a large dataset as a table must not pay for a plot of
    /// it that nobody asked to see.
    property bool active: true

    readonly property var plot: AppController.datasetPlot
    readonly property bool drawable: active && AppController.datasetIsNumeric
                                     && plot.hasData

    // --- the x axis, as start / step / stop ------------------------------
    // The x values themselves, not a window onto them. Element i of the data
    // is drawn at `start + i * step`, and `stop` is where the axis ends -- so
    // these three numbers say what the columns of the table *are*, which is
    // the question a dataset that is a measurement against something asks.
    //
    // Any two of them determine the third, and the third is computed rather
    // than typed, so they can never contradict each other. The default is what
    // a reader would write for data with no x of its own: 0 : 1 : len(data),
    // the element's own index, which is exactly what the grid's column headers
    // count.
    //
    // The upper bound is exclusive, as it is everywhere else in this program
    // -- the data settings panel's index expressions are numpy's and h5py's --
    // so `stop = start + step * len`, and 0 : 1 : len(data) is len(data)
    // points in both places.
    property real rangeStart: 0.0
    property real rangeStep: 1.0
    property real rangeStop: 1.0

    /// Which of "start", "step", "stop" the reader has stated, oldest first.
    /// At most two: a third pushes out the oldest, so the two most recent
    /// edits are always the ones in force and nothing has to be released
    /// before it can be typed over.
    property var locks: []

    /// Kept for readers of this surface that only want to know whether the x
    /// axis is the data's own.
    readonly property bool autoAxis: locks.length === 0

    /// Roughly how many ticks an axis carries.
    readonly property int tickTarget: 8

    /// How long the data is, in elements. `len(data)` in the default above.
    ///
    /// Off the table's own geometry rather than off the drawn points, and
    /// deliberately not guarded by `drawable`: it is a column count, not a
    /// sample, so nothing here reaches the file. One keeps the axis drawable
    /// while there is no dataset.
    readonly property int dataLength: Math.max(surface.plot.sourcePointCount, 1)

    /// Whether the three numbers describe an axis at all. A step of zero has
    /// no ticks and a stop below its start has no extent; either is something
    /// the reader typed on the way to something else, so it is reported rather
    /// than drawn.
    readonly property bool rangeValid: resolved.step > 0
                                       && resolved.stop > resolved.start
                                       && isFinite(resolved.start)
                                       && isFinite(resolved.step)
                                       && isFinite(resolved.stop)

    function locked(which) {
        return surface.locks.indexOf(which) !== -1
    }

    /// State `which` at the value the reader just entered, dropping the oldest
    /// of the two already stated if that would make three.
    function lock(which) {
        if (surface.locked(which))
            return
        const next = surface.locks.concat([which])
        surface.locks = next.length > 2 ? next.slice(next.length - 2) : next
    }

    function unlock(which) {
        surface.locks = surface.locks.filter(entry => entry !== which)
    }

    function setLocked(which, on) {
        if (on)
            surface.lock(which)
        else
            surface.unlock(which)
    }

    /// The three numbers the axis is actually drawn with.
    ///
    /// With two stated the third follows from `stop = start + step * len`.
    /// With one, the default supplies the next one along -- start before step
    /// before stop -- and the third still follows, so there is exactly one
    /// answer whatever the reader has said and nothing is ever derived from a
    /// number they cannot see.
    readonly property var resolved: {
        const n = surface.dataLength

        const hasStart = surface.locked("start")
        const hasStep = surface.locked("step")
        const hasStop = surface.locked("stop")

        if (hasStart && hasStep) {
            return { start: surface.rangeStart,
                     step: surface.rangeStep,
                     stop: surface.rangeStart + surface.rangeStep * n }
        }
        if (hasStart && hasStop) {
            return { start: surface.rangeStart,
                     step: (surface.rangeStop - surface.rangeStart) / n,
                     stop: surface.rangeStop }
        }
        if (hasStep && hasStop) {
            return { start: surface.rangeStop - surface.rangeStep * n,
                     step: surface.rangeStep,
                     stop: surface.rangeStop }
        }
        if (hasStart) {
            return { start: surface.rangeStart,
                     step: 1.0,
                     stop: surface.rangeStart + n }
        }
        if (hasStep) {
            return { start: 0.0,
                     step: surface.rangeStep,
                     stop: surface.rangeStep * n }
        }
        if (hasStop) {
            return { start: 0.0,
                     step: surface.rangeStop / n,
                     stop: surface.rangeStop }
        }
        return { start: 0.0, step: 1.0, stop: n }
    }

    /// The x bounds the axis takes. A trio that does not describe an axis
    /// falls back to the default one, so a half-typed number leaves the plot
    /// standing rather than blanking it.
    readonly property real axisMinX:
        surface.rangeValid ? surface.resolved.start : 0.0
    readonly property real axisMaxX:
        surface.rangeValid ? surface.resolved.stop : surface.dataLength

    // Where the points sit is the plot's own business -- they are built in
    // fill() and never cross into QML -- so the resolved start and step are
    // pushed down to it. Moving them moves the same points, which is why
    // DatasetPlot signals it separately and the surface answers by re-filling
    // rather than by building another graph.
    Binding {
        target: surface.plot
        property: "xStart"
        value: surface.rangeValid ? surface.resolved.start : 0.0
    }

    Binding {
        target: surface.plot
        property: "xStep"
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
    /// How the lines are coloured: "same", "range", or the name of one of
    /// Theme's ramps.
    ///
    /// The default is "same" at the one accent, which is what this plot has
    /// always drawn -- a bundle separated by overlap. Colour is offered because
    /// overlap stops separating anything at a dozen lines, and a legend that
    /// names them is no use if they all look alike.
    property string colorMode: "same"
    property color colorSingle: Theme.accent
    property color colorRangeFrom: Theme.accent
    property color colorRangeTo: Theme.info
    /// Run the cycle the other way. Which end of a ramp is the dark one is a
    /// property of the ramp and not of the data, and the reader is the one who
    /// knows which way round they want to read it.
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
    property real colorFrom: 0.0
    property real colorTo: 1.0

    /// The line the reader has picked out in the legend, by its index in the
    /// table, or -1. It is drawn at full strength and full width and everything
    /// else steps back, which is the only way to follow one line through a
    /// bundle of them.
    property int highlighted: -1

    /// The colour line `position` of `count` takes.
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
    readonly property rect plotRect: graphLoader.item
        ? graphLoader.item.plotArea
        : Qt.rect(0, 0, Math.max(width, 1), Math.max(height, 1))

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

    // The graph is built rather than declared, and rebuilt rather than
    // refilled.
    //
    // Qt Graphs holds on to what a series last drew: reusing one for a new
    // selection leaves the old path on screen underneath the new one, in the
    // pixel coordinates of the axes it was drawn against, and neither
    // emptying the series nor taking it out of the graph clears it. Nor can
    // the series simply be destroyed -- removeSeries() keeps the raw pointer
    // in a cleanup list it reads on its next polish, so a series freed before
    // then takes the application down.
    //
    // Discarding the whole GraphsView answers both: the series go with their
    // graph in one teardown, so nothing outlives the list that refers to it,
    // and every selection draws onto a surface with no history. A graph is
    // rebuilt when the reader picks a dataset, which is not a rate that needs
    // optimising.
    Loader {
        id: graphLoader

        anchors.fill: parent
        anchors.leftMargin: surface.contentLeft
        active: false
        sourceComponent: graphComponent
    }

    Component {
        id: graphComponent

        GraphsView {
            id: graph

            antialiasing: true
            // Room where the labels actually are. These were the other way
            // round -- air on the top and right, nothing on the left and
            // bottom -- which left the y axis with no width to print a number
            // in and so with no numbers at all, and cut the x axis's first
            // tick off at the frame.
            marginTop: Theme.gapS
            marginBottom: Theme.plotMargin
            marginLeft: Theme.plotLabelMargin
            // Room for half of the last x label, which sits centred on the
            // axis's right end. The axis now runs to `stop` rather than to the
            // last element, so that label is always drawn -- and at a gap's
            // worth of margin half of it fell off the frame.
            marginRight: Theme.s9

            // The plot area is clipped so a zoomed-in line stops at the
            // frame rather than being drawn across the axis labels.
            clipPlotArea: true

            axisX: ValueAxis {
                id: xAxis

                min: surface.axisMinX
                max: surface.axisMaxX
                zoom: surface.zoomX
                pan: surface.panX
                // A round spacing over what is on screen, always -- and
                // pointedly *not* the reader's `step`. It used to be that,
                // back when step was the distance between two ticks; it is now
                // the distance between two elements, and a dataset of a
                // thousand points at 0.25 apart would put a thousand labels
                // along the axis on top of one another.
                //
                // Set explicitly rather than left to Qt Graphs because that
                // computes its automatic spacing from the axis's *declared*
                // range and not from the range it is showing, so a zoomed-in
                // axis would keep the spacing of the whole dataset and print
                // one lonely tick.
                tickInterval: surface.niceStep(visualMax - visualMin,
                                               surface.tickTarget)
                // Enough decimals to tell two ticks apart, and no more -- the
                // same rule the y axis below applies, and for the same reason.
                // This axis used to print none at all, which was right only
                // while it counted whole columns; a step the reader states can
                // be a thousandth, and eight ticks all reading "0" is an axis
                // that has stopped saying anything.
                labelDecimals: {
                    const span = Math.abs(visualMax - visualMin)
                    if (!(span > 0))
                        return 0
                    return Math.max(0, Math.min(6,
                        Math.ceil(-Math.log(span) / Math.LN10) + 2))
                }
                gridVisible: surface.showGrid
                subGridVisible: false
                titleVisible: false
            }

            axisY: ValueAxis {
                id: yAxis

                min: surface.lowerBound
                max: surface.upperBound
                zoom: surface.zoomY
                pan: surface.panY
                tickInterval: surface.niceStep(visualMax - visualMin,
                                               surface.tickTarget)
                // Enough decimals to tell two ticks apart, and no more: a span
                // of 100 wants none, a span of a thousandth wants five. A fixed
                // count prints either noise or "0.0" all the way up the axis.
                // Taken off the *visible* span, so zooming in adds digits as
                // the ticks close up.
                labelDecimals: {
                    const span = Math.abs(visualMax - visualMin)
                    if (!(span > 0))
                        return 2
                    return Math.max(0, Math.min(6,
                        Math.ceil(-Math.log(span) / Math.LN10) + 2))
                }
                gridVisible: surface.showGrid
                subGridVisible: false
                titleVisible: false
            }

            // Every colour and font the graph draws comes from Theme, like
            // every other surface here; nothing is Qt's own palette.
            theme: GraphsTheme {
                colorScheme: Theme.dark ? GraphsTheme.ColorScheme.Dark
                                        : GraphsTheme.ColorScheme.Light
                backgroundColor: Theme.surfaceInset
                plotAreaBackgroundColor: Theme.surfaceInset
                labelTextColor: Theme.textSecondary
                axisXLabelFont: Theme.readout
                axisYLabelFont: Theme.readout
                // A step stronger than a table's rules, for the same reason
                // the table's own went up: this plot's ground is the inset,
                // which is true black, and a hairline at line-1 against it is
                // a line nobody can see. The axis rules go a step further
                // again, so the frame reads as the frame.
                //
                // Qt Graphs 6.11 draws neither the grid nor the axis rules
                // whatever these are set to -- verified by setting them to
                // 3px red, which also does not appear -- so what "grid lines"
                // in the settings panel turns on is, for now, nothing. The
                // weights are stated here so that the day the library draws
                // them, it draws them in the system's own ink. The axis
                // *labels* are unaffected and do render; those are what the
                // margins above make room for.
                grid.mainColor: Theme.borderStrong
                grid.subColor: Theme.border
                grid.mainWidth: Theme.borderWidth
                axisX.mainColor: Theme.borderGuide
                axisY.mainColor: Theme.borderGuide
                axisX.mainWidth: Theme.borderWidth
                axisY.mainWidth: Theme.borderWidth
                axisX.labelTextColor: Theme.textSecondary
                axisY.labelTextColor: Theme.textSecondary
                // The fallback only. Every line is given its colour explicitly
                // when the graph is built, from the cycle the reader picked;
                // this is what a series would take if one ever were not.
                seriesColors: [Theme.accent]
            }

            /// The table lines this graph was built for, in drawing order, so
            /// restyle() can ask what each of its series is a line *of*.
            property var drawn: []

            /// Re-colour what is already drawn.
            ///
            /// Changing a colour or picking a line out does not change which
            /// series exist, so it must not go through rebuild(): tearing the
            /// GraphsView down and building another one blanks the view for a
            /// frame, and a plot that flashes every time the reader clicks a
            /// name in the legend is unusable for the one thing the legend is
            /// for -- following a line through a bundle.
            function restyle() {
                for (let i = 0; i < graph.drawn.length; ++i) {
                    const line = graph.seriesList[i]
                    if (!line)
                        continue
                    line.color = surface.seriesColor(i, graph.drawn.length)
                    line.opacity = surface.seriesOpacity(graph.drawn[i],
                                                         graph.drawn.length)
                    line.width = surface.seriesWidth(graph.drawn[i])
                    // Qt Graphs redraws a series when its *points* change and
                    // not when its colour does, so a recoloured line keeps its
                    // old stroke on screen until something marks it dirty.
                    // Re-filling is what marks it: the values come from
                    // DatasetPlot's own cache, so this touches no file and
                    // nothing is torn down -- which is the whole point of
                    // restyling rather than rebuilding.
                    surface.plot.fill(line, graph.drawn[i])
                }
            }

            Component.onCompleted: {
                // The lines the plot is drawing, by their index in the table.
                // Not 0..seriesCount -- with a line unticked in the middle of
                // the set, position and index are not the same number, and
                // filling by position would draw the wrong rows under the
                // right names.
                graph.drawn = surface.plot.drawnSeries
                for (let i = 0; i < graph.drawn.length; ++i) {
                    const line = lineComponent.createObject(graph)
                    line.pointDelegate = surface.showMarkers ? line.marker : null
                    line.color = surface.seriesColor(i, graph.drawn.length)
                    line.opacity = surface.seriesOpacity(graph.drawn[i],
                                                         graph.drawn.length)
                    line.width = surface.seriesWidth(graph.drawn[i])
                    graph.addSeries(line)
                    surface.plot.fill(line, graph.drawn[i])
                }
            }
        }
    }

    Component {
        id: lineComponent

        LineSeries {
            id: line

            color: Theme.accent
            width: Theme.plotLineWidth

            /// A marker is punctuation on the line, not a second series, so it
            /// takes the line's colour and the smallest size that still reads
            /// as a dot.
            ///
            /// Declared *inside* the series rather than beside the graph,
            /// which is the whole trick: a Component's instances resolve names
            /// in the scope the Component was declared in, so `line` here is
            /// this series and nothing else. One shared delegate outside had no
            /// way to know which line it was drawing on -- Qt Graphs passes a
            /// point's value and its selected state to the delegate, not its
            /// series -- so every marker on every line came out the same
            /// colour. Bound rather than assigned, so a marker follows its line
            /// through a restyle for free.
            property Component marker: Component {
                Rectangle {
                    width: Theme.plotMarkerSize
                    height: Theme.plotMarkerSize
                    radius: width / 2
                    color: line.color
                }
            }
        }
    }

    /// Discard the graph and build the next one. Toggling `active` is what
    /// does it: the Loader destroys the item, and its series with it.
    function rebuild() {
        graphLoader.active = false
        graphLoader.active = surface.drawable
    }

    /// Re-colour what is drawn, without discarding it. The two are separate
    /// because they cost separate things: rebuild() re-reads the file and
    /// blanks the view for a frame, restyle() assigns three properties per
    /// line.
    function restyle() {
        if (graphLoader.item)
            graphLoader.item.restyle()
    }

    Component.onCompleted: surface.rebuild()
    onActiveChanged: Qt.callLater(surface.rebuild)
    // A marker delegate is set on the series when it is created, so this one
    // does have to go the long way round.
    onShowMarkersChanged: Qt.callLater(surface.rebuild)

    // Colour and emphasis are assigned to the series rather than bound, because
    // a series is a Qt Graphs object and not an Item; but which series exist
    // has not changed, so these restyle rather than rebuild.
    onColorModeChanged: surface.restyle()
    onColorSingleChanged: surface.restyle()
    onColorRangeFromChanged: surface.restyle()
    onColorRangeToChanged: surface.restyle()
    onColorsReversedChanged: surface.restyle()
    onColorFromChanged: surface.restyle()
    onColorToChanged: surface.restyle()
    onHighlightedChanged: surface.restyle()

    Connections {
        target: AppController.datasetPlot
        enabled: surface.active
        function onChanged() { Qt.callLater(surface.rebuild) }
        // The same lines, moved along x. Nothing has to be re-read and no
        // series has appeared or gone away, so this re-fills what is already
        // drawn rather than tearing the graph down and building another.
        function onXAxisChanged() { Qt.callLater(surface.restyle) }
    }

    // A window onto one dataset says nothing about the next one, and neither
    // does an x axis, a colour cycle or a line picked out of the bundle. All of
    // them are in the list below, so a fresh dataset opens on the defaults and
    // one the reader has been at before opens where they left it. Rearranging
    // the same dataset is not a new selection and disturbs none of it.
    DatasetMemory {
        subject: surface
        group: "plotView"
        names: ["rangeStart", "rangeStep", "rangeStop", "locks",
                "colorMode", "colorSingle", "colorRangeFrom", "colorRangeTo",
                "colorsReversed", "colorFrom", "colorTo",
                "showGrid", "showMarkers", "highlighted",
                "zoomX", "panX", "zoomY", "panY"]
    }

    // Which way round the lines are read is the plot object's own, and it
    // chooses for itself on every new selection -- a vector has to draw as one
    // line rather than as a thousand. A choice the reader made outranks that,
    // so it is remembered; a dataset they have not been to keeps the object's.
    DatasetMemory {
        subject: AppController.datasetPlot
        group: "plot"
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

        anchors.top: parent.top
        anchors.bottom: parent.bottom
        target: surface
    }

    ViewMessage {
        anchors.fill: parent
        anchors.leftMargin: surface.contentLeft
        visible: !surface.drawable
        title: qsTr("nothing to plot")
        warning: surface.active && surface.plot.error !== ""
        text: {
            // Reading `error` samples, so nothing is asked of a plot that is
            // not the presentation on screen.
            if (!surface.active)
                return ""
            if (surface.plot.error !== "")
                return surface.plot.error
            if (!AppController.datasetTabVisible)
                return qsTr("Select a dataset in the tree to plot its values.")
            if (!AppController.datasetIsNumeric)
                return qsTr("%1 holds no numbers. Only a numeric dataset can be plotted.")
                       .arg(AppController.currentPath)
            return qsTr("The selected slice has no finite values in it.")
        }
    }


}
