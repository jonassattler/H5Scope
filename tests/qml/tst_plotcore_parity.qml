// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import QtTest
import H5Scope

// plotcore's port of the viewer's axes, held to the viewer's axes.
//
// PlotFrame.qml and PlotSurface.qml are where the application decides how an
// axis is numbered and how a gesture moves it, and plotcore is the same
// arithmetic written out in C++ so that a library can draw what the
// application draws. Two ways of numbering one axis are two answers to "where
// is this", so they are asked the same questions here and must give the same
// answers -- not close ones: every value compared exactly, every label
// compared character for character. `PlotCore` is tests/PlotCoreProbe, which
// is plotcore and nothing else.
TestCase {
    id: suite

    name: "PlotCoreParity"
    width: 800
    height: 600

    PlotFrame {
        id: frame

        width: 600
        height: 400
    }

    // A surface with nothing to draw: its arithmetic is all that is asked of
    // it, and that arithmetic reads no plot.
    PlotSurface {
        id: surface

        width: 800
        height: 500
        plot: null
    }

    /// `a` and `b` are the same list, element for element and exactly.
    function sameValues(a, b, what) {
        compare(b.length, a.length, what + ": how many")
        for (let i = 0; i < Math.min(a.length, b.length); ++i)
            verify(a[i] === b[i], what + " [" + i + "]: QML " + a[i] + ", plotcore " + b[i])
    }

    function sameTicks(js, cpp, what) {
        sameValues(js.values, cpp.values, what + " values")
        sameValues(js.ticks, cpp.ticks, what + " ticks")
        compare(cpp.texts.length, js.texts.length, what + " labels")
        for (let i = 0; i < Math.min(js.texts.length, cpp.texts.length); ++i) {
            compare(cpp.texts[i].text, js.texts[i].text, what + " label " + i)
            compare(cpp.texts[i].base, js.texts[i].base, what + " base " + i)
            compare(cpp.texts[i].exponent, js.texts[i].exponent, what + " exponent " + i)
        }
        compare(cpp.logarithmic, js.logarithmic, what + " logarithmic")
        compare(cpp.fallback, js.fallback, what + " fallback")
    }

    // --- how a number is written -----------------------------------------
    // JavaScript's formatting is not printf's: a tie rounds up, an exponent is
    // "e+7", and the shortest form switches to an exponent at 1e21. Asked of
    // the engine itself, because that is what the labels are written by.
    readonly property var numbers: [0, -0, 0.5, 1.5, 2.5, -2.5, 1.005, 1.25, 0.125, 2.675,
        9.995, 123.456, 1e21, 1.5e21, 1e-7, 0.000001234, 123456789, 1700000000.5,
        0.1 + 0.2, 1 / 3, 5e-324, 1.7976931348623157e308, 99.99999999, 0.0005, 65536.25]

    function test_numbers_are_written_as_javascript_writes_them() {
        for (let i = 0; i < suite.numbers.length; ++i) {
            const v = suite.numbers[i]
            compare(PlotCore.numberText(v), String(v), "String(" + v + ")")
            for (let d = 0; d <= 8; ++d) {
                if (Math.abs(v) < 1e21)
                    compare(PlotCore.toFixed(v, d), v.toFixed(d), v + ".toFixed(" + d + ")")
                compare(PlotCore.toExponential(v, d), v.toExponential(d),
                        v + ".toExponential(" + d + ")")
                compare(PlotCore.toPrecision(v, d + 1), v.toPrecision(d + 1),
                        v + ".toPrecision(" + (d + 1) + ")")
            }
        }
        for (const v of [0.5, 1.5, 2.5, -0.5, -1.5, -2.5, 0.49999999999999994, 1e16 + 1])
            verify(PlotCore.jsRound(v) === Math.round(v), "Math.round(" + v + ")")
    }

    // --- which ticks ------------------------------------------------------
    readonly property var views: [
        // linear: a unit, an offset, an epoch, a wide span, a tiny one
        { low: 0, high: 1, log: false, base: 10 },
        { low: -3.2, high: 7.5, log: false, base: 10 },
        { low: 1.7e9, high: 1.7e9 + 300, log: false, base: 10 },
        { low: 0, high: 1e7, log: false, base: 10 },
        { low: -0.0005, high: 0.0003, log: false, base: 10 },
        { low: 1, high: 1.001, log: false, base: 10 },
        { low: 99.99, high: 100.01, log: false, base: 10 },
        { low: -32768, high: 32767, log: false, base: 10 },
        // decades: one, two, under one, thirty, inside one, across many
        { low: 1, high: 10, log: true, base: 10 },
        { low: 1, high: 100, log: true, base: 10 },
        { low: 0.001, high: 0.01, log: true, base: 10 },
        { low: 1, high: 1e30, log: true, base: 10 },
        { low: 3, high: 7, log: true, base: 10 },
        { low: 1.0001, high: 1.0002, log: true, base: 10 },
        { low: 0.5, high: 2e6, log: true, base: 10 },
        { low: 5, high: 5000, log: true, base: 10 },
        { low: 2e-9, high: 3e-3, log: true, base: 10 },
        // octaves, e, and two custom bases
        { low: 1, high: 2, log: true, base: 2 },
        { low: 1, high: 1024, log: true, base: 2 },
        { low: 1.4, high: 1.6, log: true, base: 2 },
        { low: 0.25, high: 3000, log: true, base: 2 },
        { low: 1, high: 20, log: true, base: Math.E },
        { low: 0.1, high: 3, log: true, base: Math.E },
        { low: 1, high: 100, log: true, base: 3 },
        { low: 1, high: 10, log: true, base: 1.5 }
    ]

    function viewName(view, extent, vertical) {
        return (view.log ? "log" + view.base : "linear") + " " + view.low + ".." + view.high
               + (vertical ? " up " : " across ") + extent
    }

    function test_an_axis_is_numbered_as_the_frame_numbers_it() {
        for (let i = 0; i < suite.views.length; ++i) {
            const view = suite.views[i]
            for (const extent of [600, 400, 120]) {
                for (const vertical of [false, true]) {
                    const js = frame.tickValues(view.low, view.high, view.log, view.base,
                                                extent, vertical)
                    const cpp = PlotCore.tickValues(view.low, view.high, view.log, view.base,
                                                    extent, vertical, frame.tickTarget,
                                                    Theme.readout.pixelSize)
                    suite.sameTicks(js, cpp, suite.viewName(view, extent, vertical))
                }
            }
        }
    }

    function test_an_axis_is_ruled_as_the_frame_rules_it() {
        const modes = ["none", "loose", "dense", "custom"]
        for (let i = 0; i < suite.views.length; ++i) {
            const view = suite.views[i]
            const custom = view.log ? (view.base === 2 ? 2 : 10) : (view.high - view.low) / 7
            for (let m = 0; m < modes.length; ++m) {
                frame.gridMode = modes[m]
                const found = frame.tickValues(view.low, view.high, view.log, view.base, 600,
                                               false)
                const js = frame.gridValues(view.low, view.high, custom, found)
                const cpp = PlotCore.gridValues(modes[m], view.low, view.high, custom, found,
                                                frame.tickTarget, Theme.plotGridDenseFactor)
                suite.sameValues(js, cpp, suite.viewName(view, 600, false) + " " + modes[m])
            }
        }
        frame.gridMode = "loose"
    }

    function test_the_digits_between_two_powers_are_the_frames() {
        for (let i = 0; i < suite.views.length; ++i) {
            const view = suite.views[i]
            if (!view.log)
                continue
            for (const mode of ["dense", "loose", "custom"]) {
                frame.gridMode = mode
                for (const minor of [true, false]) {
                    frame.minorNumbers = minor
                    for (const spacing of [{ extent: 600, gap: 4, perChar: 6 },
                                           { extent: 400, gap: 12, perChar: 0 }]) {
                        const found = frame.tickValues(view.low, view.high, true, view.base,
                                                       spacing.extent, false)
                        const values = frame.gridValues(view.low, view.high, 10, found)
                        const js = frame.minorNumbersOf(values, found, view.low, view.high,
                                                        view.base, spacing.extent,
                                                        spacing.gap, spacing.perChar)
                        const cpp = PlotCore.minorNumbersOf(minor, mode, values, found,
                                                            view.low, view.high, view.base,
                                                            spacing.extent, spacing.gap,
                                                            spacing.perChar)
                        const what = suite.viewName(view, spacing.extent, false) + " " + mode
                                     + (minor ? " numbered" : "")
                        compare(cpp.length, js.length, what + ": how many")
                        for (let k = 0; k < Math.min(js.length, cpp.length); ++k) {
                            verify(cpp[k].at === js[k].at, what + " at " + k)
                            compare(cpp[k].text, js[k].text, what + " text " + k)
                        }
                    }
                }
            }
        }
        frame.gridMode = "loose"
        frame.minorNumbers = false
    }

    function test_a_value_sits_where_the_frame_puts_it() {
        for (let i = 0; i < suite.views.length; ++i) {
            const view = suite.views[i]
            for (const at of [0, 0.25, 0.5, 0.999, 1]) {
                const value = view.log
                    ? Math.pow(view.base, frame.logOf(view.low, view.base)
                               + at * (frame.logOf(view.high, view.base)
                                       - frame.logOf(view.low, view.base)))
                    : view.low + at * (view.high - view.low)
                const js = frame.fractionOn(value, view.low, view.high, view.log, view.base)
                const cpp = PlotCore.fractionOn(value, view.low, view.high, view.log, view.base)
                verify(js === cpp, suite.viewName(view, 0, false) + " at " + at
                       + ": QML " + js + ", plotcore " + cpp)
            }
        }
    }

    function test_a_label_is_written_as_the_frame_writes_it() {
        for (const span of [1, 1e-5, 2e6, 0.37, 300, 1e-4, 1e6])
            for (const v of [0, 1.5, -2.25, 1700000123.4567, 0.000123, 12345.678])
                compare(PlotCore.labelFor(v, span), frame.labelFor(v, span),
                        "labelFor(" + v + ", " + span + ")")
        for (const v of [1, 2, 0.001, 0.0002, 3e7, 123456, 1.23456, 5e-5, 7])
            compare(PlotCore.logLabelFor(v), frame.logLabelFor(v), "logLabelFor(" + v + ")")
    }

    // --- how a gesture moves an axis ---------------------------------------
    readonly property var axes: [
        { low: 0, high: 1, log: false, base: 10 },
        { low: -32768, high: 32767, log: false, base: 10 },
        { low: 1.7e9, high: 1.7e9 + 3600, log: false, base: 10 },
        { low: 1, high: 1e6, log: true, base: 10 },
        { low: 0.001, high: 2, log: true, base: 10 },
        { low: 1, high: 4096, log: true, base: 2 },
        { low: 0.5, high: 80, log: true, base: Math.E }
    ]

    function axisName(axis) {
        return (axis.log ? "log" + axis.base : "linear") + " " + axis.low + ".." + axis.high
    }

    function test_an_axis_is_padded_as_the_surface_pads_it() {
        const ranges = [[0, 1], [-5, 5], [3, 3], [5, 5], [10, 10], [0, 0], [-4, -1],
                        [0.001, 1000], [1e-12, 1e12], [7, 7.0000001]]
        for (const base of [10, 2, Math.E, 3.5]) {
            for (const log of [false, true]) {
                for (const range of ranges) {
                    const js = surface.padded(range[0], range[1], log, base)
                    const cpp = PlotCore.padded(range[0], range[1], log, base)
                    const what = range + (log ? " log" + base : " linear")
                    verify(js.low === cpp.low, what + " low: QML " + js.low + ", plotcore " + cpp.low)
                    verify(js.high === cpp.high, what + " high: QML " + js.high
                           + ", plotcore " + cpp.high)
                }
            }
        }
    }

    function test_a_wheel_moves_an_axis_as_the_surface_moves_it() {
        for (const axis of suite.axes) {
            const minimum = axis.log ? (axis.high - axis.low) / 4096 : 0
            for (const start of [{ zoom: 1, pan: 0 }, { zoom: 7.5, pan: 0.2 },
                                 { zoom: 300, pan: -0.4 }]) {
                for (const fraction of [0, 0.3, 1]) {
                    for (const factor of [1.25, 0.8, 1e6, 1e-6]) {
                        const js = surface.zoomedAxis(start.zoom, start.pan, axis.low, axis.high,
                                                      fraction, factor, axis.log, axis.base,
                                                      minimum)
                        const cpp = PlotCore.zoomedAxis(start.zoom, start.pan, axis.low,
                                                        axis.high, fraction, factor, axis.log,
                                                        axis.base, minimum, surface.maxZoom,
                                                        surface.panKeep)
                        const what = suite.axisName(axis) + " from " + start.zoom + "/"
                                     + start.pan + " at " + fraction + " by " + factor
                        verify(js.zoom === cpp.zoom, what + " zoom: QML " + js.zoom
                               + ", plotcore " + cpp.zoom)
                        verify(js.pan === cpp.pan, what + " pan: QML " + js.pan
                               + ", plotcore " + cpp.pan)
                    }
                }
            }
        }
    }

    function test_a_window_is_found_as_the_surface_finds_it() {
        for (const axis of suite.axes) {
            const minimum = axis.log ? (axis.high - axis.low) / 4096 : 0
            for (const at of [[0.1, 0.2], [0.9, 0.3], [0, 1], [-0.5, 1.5], [0.5, 0.5],
                              [0.4999, 0.5001]]) {
                const from = surface.valueAlong(axis.low, axis.high, at[0], axis.log, axis.base)
                const to = surface.valueAlong(axis.low, axis.high, at[1], axis.log, axis.base)
                verify(from === PlotCore.valueAlong(axis.low, axis.high, at[0], axis.log,
                                                    axis.base), suite.axisName(axis) + " along")
                const js = surface.viewedAxis(axis.low, axis.high, from, to, axis.log,
                                              axis.base, minimum)
                const cpp = PlotCore.viewedAxis(axis.low, axis.high, from, to, axis.log,
                                                axis.base, minimum, surface.maxZoom,
                                                surface.panKeep)
                const what = suite.axisName(axis) + " " + at
                verify(js.zoom === cpp.zoom, what + " zoom: QML " + js.zoom + ", plotcore "
                       + cpp.zoom)
                verify(js.pan === cpp.pan, what + " pan: QML " + js.pan + ", plotcore " + cpp.pan)
                for (const view of [js, { zoom: 3, pan: 9 }, { zoom: 1, pan: -1e9 }]) {
                    verify(surface.visibleLow(axis.low, axis.high, view.zoom, view.pan, axis.log,
                                              axis.base)
                           === PlotCore.visibleLow(axis.low, axis.high, view.zoom, view.pan,
                                                   axis.log, axis.base), what + " low")
                    verify(surface.visibleHigh(axis.low, axis.high, view.zoom, view.pan, axis.log,
                                               axis.base)
                           === PlotCore.visibleHigh(axis.low, axis.high, view.zoom, view.pan,
                                                    axis.log, axis.base), what + " high")
                    verify(surface.clampPan(view.pan, view.zoom, axis.low, axis.high, axis.log,
                                            axis.base)
                           === PlotCore.clampPan(view.pan, view.zoom, axis.low, axis.high,
                                                 axis.log, axis.base, surface.panKeep),
                           what + " clamp")
                }
                const span = surface.positiveSpan(from - 5, to, axis.low, axis.high, axis.log)
                const spanCpp = PlotCore.positiveSpan(from - 5, to, axis.low, axis.high, axis.log)
                verify(span.from === spanCpp.from && span.to === spanCpp.to,
                       what + " positive span")
            }
        }
    }

    function test_the_zoom_ceiling_is_the_surfaces() {
        for (const base of [10, 2, Math.E]) {
            for (const held of [-3, 0, 2.5, 6]) {
                for (const fraction of [0, 0.5, 1]) {
                    for (const minimum of [1e-9, 0.01, 1, 1e6, 0]) {
                        const js = surface.logZoomCeiling(6, held, fraction, minimum, base)
                        const cpp = PlotCore.logZoomCeiling(6, held, fraction, minimum, base,
                                                            surface.maxZoom)
                        verify(js === cpp, "ceiling " + base + "/" + held + "/" + fraction
                               + "/" + minimum + ": QML " + js + ", plotcore " + cpp)
                    }
                }
            }
        }
    }

    // --- colours ------------------------------------------------------------

    /// Two colours that are one colour. Within a sixteen-bit step rather than
    /// exactly, because a QColor holds sixteen bits a channel and a mix in
    /// QML arrives through floats where plotcore's stays in doubles: the same
    /// arithmetic can land one step apart, and a different one cannot.
    function sameColour(js, cpp, what) {
        const a = Qt.color(js)
        const b = Qt.color(cpp)
        const step = 1.5 / 65535
        verify(Math.abs(a.r - b.r) < step && Math.abs(a.g - b.g) < step
               && Math.abs(a.b - b.b) < step && Math.abs(a.a - b.a) < step,
               what + ": QML " + a + ", plotcore " + b)
    }

    function surfaceColours() {
        return {
            mode: surface.colorMode,
            single: surface.colorSingle,
            rangeFrom: surface.colorRangeFrom,
            rangeTo: surface.colorRangeTo,
            reversed: surface.colorsReversed,
            from: surface.colorFrom,
            to: surface.colorTo
        }
    }

    function test_every_stop_of_every_palette_is_the_themes() {
        sameValues(Theme.categoricalPaletteNames, PlotCore.paletteNames(), "palette names")
        sameValues(Theme.colorRampNames, PlotCore.rampNames(), "ramp names")
        for (const onDark of [true, false]) {
            const palettes = Theme.palettesFor(onDark)
            for (const name of Theme.categoricalPaletteNames) {
                const js = palettes[name]
                const cpp = PlotCore.palette(name, onDark)
                compare(cpp.length, js.length, name + " stops")
                for (let i = 0; i < js.length; ++i)
                    sameColour(js[i], cpp[i], name + (onDark ? " dark " : " light ") + i)
            }
        }
        for (const name of Theme.colorRampNames) {
            const js = Theme.colorRamps[name]
            const cpp = PlotCore.ramp(name)
            compare(cpp.length, js.length, name + " stops")
            for (let i = 0; i < js.length; ++i)
                sameColour(js[i], cpp[i], name + " " + i)
            for (const at of [-0.5, 0, 0.1, 1 / 3, 0.5, 0.77, 1, 1.5])
                sameColour(Theme.rampColor(js, at), PlotCore.rampColor(name, at),
                           name + " at " + at)
        }
        for (const colour of [Theme.sig500, Theme.cyan500, Theme.n0, "#123456"])
            sameColour(Theme.paperColor(Qt.color(colour)), PlotCore.paperColor(colour),
                       "paper " + colour)
    }

    function test_a_line_is_coloured_as_the_surface_colours_it() {
        const modes = Theme.categoricalPaletteNames.concat(Theme.colorRampNames)
                                                   .concat(["same", "range"])
        const wasDark = Theme.dark
        try {
            for (const onDark of [true, false]) {
                Theme.dark = onDark
                // The defaults are what the surface's bindings name, read in
                // this scope -- asked of the tokens rather than of the surface,
                // whose own copies DatasetMemory has already written over.
                const defaults = PlotCore.defaultColours(onDark)
                sameColour(Theme.accent, defaults.single, "default single")
                sameColour(Theme.accent, defaults.rangeFrom, "default range from")
                sameColour(Theme.info, defaults.rangeTo, "default range to")
                compare(defaults.mode, "okabe-ito", "default mode")
                for (const [mode, chosen] of modes.map(m => [m, false])
                                                   .concat([["same", true], ["range", true]])) {
                    surface.colorMode = mode
                    // The defaults, and colours a token could not have put
                    // there, which paperColor() has to leave alone.
                    surface.colorSingle = chosen ? "#3366cc" : Theme.accent
                    surface.colorRangeFrom = chosen ? "#33cc66" : Theme.accent
                    surface.colorRangeTo = chosen ? "#cc6633" : Theme.info
                    for (const band of [[0, 1], [0.2, 0.7]]) {
                        surface.colorFrom = band[0]
                        surface.colorTo = band[1]
                        for (const reversed of [false, true]) {
                            surface.colorsReversed = reversed
                            for (const paper of [false, true]) {
                                for (const count of [1, 3, 25]) {
                                    for (let position = 0; position < count; ++position) {
                                        const series = 2 * position + 1
                                        sameColour(surface.seriesColor(series, position, count,
                                                                       paper),
                                                   PlotCore.seriesColor(surfaceColours(), series,
                                                                        position, count, paper,
                                                                        onDark, null),
                                                   mode + " " + band + (reversed ? " reversed" : "")
                                                   + (paper ? " paper " : " ") + position + "/"
                                                   + count)
                                    }
                                }
                            }
                        }
                    }
                }
                for (const highlighted of [-1, 0, 2]) {
                    surface.highlighted = highlighted
                    for (const bundle of [false, true]) {
                        surface.bundle = bundle
                        for (const count of [1, 4]) {
                            for (let series = 0; series < count; ++series) {
                                compare(PlotCore.seriesOpacity(series, count, highlighted,
                                                               bundle, onDark),
                                        surface.seriesOpacity(series, count),
                                        "opacity " + series + "/" + count + " highlighted "
                                        + highlighted + (bundle ? " bundled" : ""))
                            }
                        }
                    }
                }
            }
        } finally {
            Theme.dark = wasDark
            surface.colorMode = "okabe-ito"
            surface.colorSingle = Theme.accent
            surface.colorRangeFrom = Theme.accent
            surface.colorRangeTo = Theme.info
            surface.colorFrom = 0
            surface.colorTo = 1
            surface.colorsReversed = false
            surface.highlighted = -1
            surface.bundle = true
        }
    }

    function test_the_picture_around_the_lines_is_the_frames() {
        const wasDark = Theme.dark
        try {
            for (const onDark of [true, false]) {
                Theme.dark = onDark
                const cpp = PlotCore.chrome(onDark, false)
                sameColour(frame.ground, cpp.ground, "ground")
                sameColour(frame.ink, cpp.ink, "ink")
                sameColour(frame.ruleMinor, cpp.ruleMinor, "minor rule")
                sameColour(frame.ruleMajor, cpp.ruleMajor, "major rule")
                sameColour(frame.axisRule, cpp.axisRule, "axis rule")
                sameColour(frame.cursorInk, cpp.cursorInk, "cursor")
                // PlotPicture's publication colours, whatever is on screen.
                const paper = PlotCore.chrome(onDark, true)
                sameColour(Theme.n11, paper.ground, "paper ground")
                sameColour(Theme.paperInk, paper.ink, "paper ink")
                sameColour(Theme.paperRuleMinor, paper.ruleMinor, "paper minor rule")
                sameColour(Theme.paperRuleMajor, paper.ruleMajor, "paper major rule")
                sameColour(Theme.paperAxisRule, paper.axisRule, "paper axis rule")
                sameColour(Theme.paperInk, paper.cursorInk, "paper cursor")
            }
        } finally {
            Theme.dark = wasDark
        }
    }
}
