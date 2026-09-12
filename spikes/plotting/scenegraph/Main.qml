// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

// The scene-graph spike's scene.
//
// The curves are the PlotItem's; the chrome -- grid, ticks, labels -- is QML,
// which is the arrangement this spike is arguing for. It is also what makes the
// comparison fair: Qt Graphs draws its own axis labels inside the frame it is
// being timed on, so a spike that drew no labels at all would be winning by
// leaving out work rather than by doing it faster.
//
// niceStep is lifted from src/qml/PlotSurface.qml:407-422, where it exists
// because Qt Graphs computes its automatic tick spacing from the axis's
// declared range rather than the range it is showing. Here it exists because
// nothing computes it for us -- which is the honest form of the same cost.

import QtQuick
import SpikeSceneGraph

Window {
    id: root

    width: 1400
    height: 900
    visible: true
    title: "spike-scenegraph"
    color: "#202226"

    // A round tick spacing giving roughly `target` ticks across `span`:
    // 1, 2 or 5 times a power of ten.
    function niceStep(span, target) {
        if (!(span > 0) || target <= 0)
            return 1
        const raw = span / target
        const magnitude = Math.pow(10, Math.floor(Math.log(raw) / Math.LN10))
        const normalised = raw / magnitude
        if (normalised < 1.5)
            return magnitude
        if (normalised < 3)
            return 2 * magnitude
        if (normalised < 7)
            return 5 * magnitude
        return 10 * magnitude
    }

    function ticksIn(low, high, target) {
        const step = niceStep(high - low, target)
        const first = Math.ceil(low / step) * step
        const out = []
        for (let value = first; value <= high && out.length < 64; value += step)
            out.push(value)
        return out
    }

    // Decade ticks, for the axis the renderer can draw logarithmically and
    // nothing else here can. It is a small function and it is the point: the
    // renderer's log axis is three lines of arithmetic, and everything that
    // *labels* one is work the application writes for itself. A library that
    // has a log axis has this too.
    function logTicksIn(low, high) {
        const from = Math.ceil(Math.log(Math.max(low, Number.MIN_VALUE)) / Math.LN10)
        const to = Math.floor(Math.log(Math.max(high, Number.MIN_VALUE)) / Math.LN10)
        const every = Math.max(1, Math.ceil((to - from) / 8))
        const out = []
        for (let decade = from; decade <= to && out.length < 64; decade += every)
            out.push(Math.pow(10, decade))
        return out
    }

    function yTicks() {
        return plot.logY ? logTicksIn(plot.yMin, plot.yMax)
                         : ticksIn(plot.yMin, plot.yMax, 6)
    }

    /// Where a y value sits in the pane, 0 at the bottom and 1 at the top.
    /// The renderer does this same arithmetic on every vertex; the chrome has
    /// to agree with it or the grid lies about where the curve is.
    function yFraction(value) {
        if (plot.logY) {
            const low = Math.log(Math.max(plot.yMin, Number.MIN_VALUE)) / Math.LN10
            const high = Math.log(Math.max(plot.yMax, Number.MIN_VALUE)) / Math.LN10
            return (Math.log(Math.max(value, Number.MIN_VALUE)) / Math.LN10 - low)
                    / (high - low)
        }
        return (value - plot.yMin) / (plot.yMax - plot.yMin)
    }

    function labelFor(value, span) {
        return plot.logY ? value.toExponential(0) : value.toFixed(decimalsFor(span))
    }

    // Decimals enough to tell two neighbouring ticks apart, and no more. The
    // application derives the same number from the *visible* span for the same
    // reason: a zoomed-in axis whose labels all read the same is not an axis.
    function decimalsFor(span) {
        if (!(span > 0))
            return 3
        return Math.max(0, Math.min(9, 2 - Math.floor(Math.log(span) / Math.LN10)))
    }

    Text {
        id: caption
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 8
        color: "#c8ccd4"
        font.pixelSize: 12
        font.family: "monospace"
        text: plot.caption
    }

    Item {
        id: pane

        anchors.fill: parent
        anchors.topMargin: 28

        // The grid, under the curves. Rectangles rather than a painted layer:
        // eight of them cost nothing and the scene graph batches them.
        Repeater {
            model: root.ticksIn(plot.xMin, plot.xMax, 8)
            delegate: Rectangle {
                required property real modelData
                width: 1
                height: pane.height
                color: "#2e3238"
                x: Math.round((modelData - plot.xMin) / (plot.xMax - plot.xMin) * pane.width)
            }
        }

        Repeater {
            model: root.yTicks()
            delegate: Rectangle {
                required property real modelData
                width: pane.width
                height: 1
                color: "#2e3238"
                y: Math.round(pane.height - root.yFraction(modelData) * pane.height)
            }
        }

        PlotItem {
            id: plotItem
            objectName: "plotItem"
            anchors.fill: parent
        }

        Repeater {
            model: root.ticksIn(plot.xMin, plot.xMax, 8)
            delegate: Text {
                required property real modelData
                color: "#8b919b"
                font.pixelSize: 10
                font.family: "monospace"
                text: modelData.toFixed(root.decimalsFor(plot.xMax - plot.xMin))
                x: Math.round((modelData - plot.xMin) / (plot.xMax - plot.xMin) * pane.width) + 3
                y: pane.height - height - 2
            }
        }

        Repeater {
            model: root.yTicks()
            delegate: Text {
                required property real modelData
                color: "#8b919b"
                font.pixelSize: 10
                font.family: "monospace"
                text: root.labelFor(modelData, plot.yMax - plot.yMin)
                x: 3
                y: Math.round(pane.height - root.yFraction(modelData) * pane.height) + 2
            }
        }
    }
}
