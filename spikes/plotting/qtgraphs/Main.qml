// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

// The baseline scene, arranged the way src/qml/PlotSurface.qml arranges the
// application's: the graph is built rather than declared, and rebuilt rather
// than refilled.

import QtQuick
import QtGraphs

Window {
    id: root

    width: 1400
    height: 900
    visible: true
    title: "spike-qtgraphs"
    color: "#202226"

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

    Component {
        id: lineComponent
        LineSeries {}
    }

    Component {
        id: graphComponent

        GraphsView {
            id: graph

            anchors.fill: parent

            antialiasing: true

            // The application's own arrangement: the plot area is clipped so a
            // zoomed line stops at the frame rather than running across the
            // labels, and the margins leave room where the labels actually are.
            clipPlotArea: true
            marginTop: 6
            marginBottom: 14
            marginLeft: 32
            marginRight: 20

            // No animation anywhere, which is the application's rule and also
            // the only way a frame time means anything: a renderer still easing
            // towards its new range is a renderer being timed drawing something
            // nobody asked for.
            theme: GraphsTheme {
                colorScheme: GraphsTheme.ColorScheme.Dark
                backgroundColor: "#202226"
                plotAreaBackgroundColor: "#202226"
                // The same weights the other two spikes draw their chrome at,
                // so the pictures are comparable.
                //
                // And a live finding: src/qml/PlotSurface.qml:571-578 records
                // that "Qt Graphs 6.11 draws neither the grid nor the axis
                // rules whatever these are set to -- verified by setting them
                // to 3px red, which also does not appear". With the pinned
                // 6.11.1 and this scene it draws both. Setting them to 3px red
                // here produced 62 632 red pixels in 23 horizontal and 29
                // vertical rules, with clipPlotArea, the margins and
                // gridVisible all as the application has them. Whatever
                // suppresses the grid in H5Scope is in the application's own
                // configuration rather than in the library.
                grid.mainColor: "#2e3238"
                grid.mainWidth: 1
                axisX.mainColor: "#8b919b"
                axisY.mainColor: "#8b919b"
                axisX.mainWidth: 1
                axisY.mainWidth: 1
            }

            axisX: ValueAxis {
                id: axisX
                min: plot.xMin
                max: plot.xMax
                titleVisible: false
                labelDecimals: 3
                gridVisible: true
                subGridVisible: false
            }

            axisY: ValueAxis {
                id: axisY
                min: plot.yMin
                max: plot.yMax
                titleVisible: false
                labelDecimals: 3
                gridVisible: true
                subGridVisible: false
            }

            // Built here rather than declared, because the number of lines is
            // not known until the data arrives and a Repeater of LineSeries is
            // not something GraphsView accepts.
            Component.onCompleted: {
                const count = plot.seriesCount
                for (let i = 0; i < count; ++i) {
                    const line = lineComponent.createObject(graph)
                    line.color = plot.seriesColor(i)
                    line.width = 1.0
                    graph.addSeries(line)
                    plot.fill(line, i)
                }
            }
        }
    }

    // Qt Graphs holds on to what a series last drew. Reusing one for a new
    // selection leaves the old path on screen underneath the new one, in the
    // pixel coordinates of the axes it was drawn against, and neither emptying
    // the series nor taking it out of the graph clears it -- nor can the series
    // simply be destroyed, because removeSeries() keeps the raw pointer in a
    // cleanup list it reads on its next polish. Discarding the whole GraphsView
    // answers all of it, and it is what the application does.
    Loader {
        id: graphLoader
        anchors.fill: parent
        anchors.topMargin: 28
        asynchronous: false
        active: false
        sourceComponent: graphComponent
    }

    Connections {
        target: plot
        function onRebuilt() {
            graphLoader.active = false
            graphLoader.active = true
        }
    }
}
