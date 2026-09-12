// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

// The QCustomPlot spike's scene. Almost nothing in it, because the library
// draws its own axes, grid, ticks and labels -- which is most of what
// PlotSurface.qml and the scene-graph spike's Main.qml have to write out by
// hand, and is the strongest argument on this side of the comparison.

import QtQuick
import SpikeQCustomPlot

Window {
    id: root

    width: 1400
    height: 900
    visible: true
    title: "spike-qcustomplot"
    color: "#202226"

    Text {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 8
        color: "#c8ccd4"
        font.pixelSize: 12
        font.family: "monospace"
        text: plot.caption
    }

    QCustomPlotItem {
        id: plotItem
        objectName: "plotItem"
        anchors.fill: parent
        anchors.topMargin: 28
    }
}
