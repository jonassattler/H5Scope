// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick

/// The lines named in a corner of the plot itself.
///
/// PlotLegend's opposite number, and the two answer different questions. That
/// one is a control: every line in the table, tickable, scrollable, with the
/// three buttons that widen and narrow the drawn set -- it is where a reader
/// decides *which* lines there are. This one decides nothing. It is a caption
/// on the picture, and it exists because a picture leaves this application:
/// "copy plot" puts the frame on the clipboard, and a plot of six traces
/// pasted into a document with nothing saying which is which is six traces
/// nobody can read.
///
/// So it is a child of PlotFrame rather than of the surface, which is what
/// makes it part of what is copied; and it is off by default, because on
/// screen the panel on the left already answers this and better.
///
/// Bounded at Theme.plotLegendRows. A legend that named all ten thousand lines
/// of a table would be the picture replaced by a list of what is in it, so
/// past the bound it says how many are left instead -- which is the honest
/// thing to put on a plot whose lines cannot all be named.
Rectangle {
    id: legend

    /// The PlotSurface being captioned. Its `seriesColor` is what ties a name
    /// to a stroke, and it is asked rather than reimplemented: a swatch drawn
    /// in a colour this file worked out for itself would be a second rule
    /// about which line is which, and the one thing a legend may never be is
    /// wrong.
    property var target
    /// Which corner: "topLeft", "topRight", "bottomLeft" or "bottomRight".
    property string corner: "topRight"
    /// The pane to sit in, in the parent's coordinates -- PlotFrame.area.
    property rect area: Qt.rect(0, 0, 0, 0)

    /// Whether this asks the plot object anything at all.
    ///
    /// Nothing here may touch that object until the reader has asked for the
    /// caption, and the rule is the one PlotLegend's ListView keeps by going
    /// unbound while it is closed: a question put to a plot is a reading of
    /// the file, and one of these exists on *every* plot in the window
    /// whether or not it is the one on screen.
    ///
    /// Asked for and drawable, not merely asked for. A custom tab is built
    /// while another tab is showing -- its lines are added, read and drawn
    /// before the reader ever selects it -- so a binding here that reached for
    /// `drawnSeries` during that window put a question to an inactive plot in
    /// the middle of its first read. What came back was a tab whose strokes
    /// were never filled: `active` turned true, the refill that follows it
    /// found the plot still answering and cleared the item instead, and
    /// nothing afterwards asked again. The picture was an empty pane with a
    /// correct axis and a footer saying 96 datapoints.
    readonly property bool asking: !!legend.target && legend.target.legendOnPlot
                                   && legend.target.drawable
    readonly property var plot: legend.asking ? legend.target.plot : null
    /// Which lines are drawn, in the order they were handed to the item. The
    /// position in this list is what a colour map spreads its shares over, so
    /// it is carried through to seriesColor rather than recomputed.
    readonly property var drawn: legend.plot ? legend.plot.drawnSeries : []
    readonly property int named: Math.min(legend.drawn.length,
                                          Theme.plotLegendRows)
    readonly property int unnamed: legend.drawn.length - legend.named

    /// The rows, as plain data: a delegate then depends on its own entry and
    /// on nothing else. Rebuilt whenever the drawn set changes, which is what
    /// makes seriesLabel() -- a call, not a property -- answer again.
    readonly property var rows: {
        const out = []
        for (let i = 0; i < legend.named; ++i) {
            out.push({ series: legend.drawn[i],
                       position: i,
                       label: legend.plot.seriesLabel(legend.drawn[i]) })
        }
        return out
    }

    /// The widest name, by characters.
    ///
    /// The same shortcut PlotFrame takes over its y tick labels, and it is
    /// sound for the same reason: these are set in Theme.monoSmall, so the
    /// longest string is the widest one. A proportional face would need every
    /// one of them measured.
    readonly property string widest: {
        let found = ""
        for (let i = 0; i < legend.rows.length; ++i) {
            if (legend.rows[i].label.length > found.length)
                found = legend.rows[i].label
        }
        return found
    }

    TextMetrics {
        id: nameMetrics

        font: Theme.monoSmall
        text: legend.widest
    }

    /// One line of type and a little air. Tighter than a list row in a panel,
    /// because this is a caption standing on the drawing rather than a run of
    /// controls.
    readonly property int rowHeight: Math.ceil(nameMetrics.height) + Theme.gapXS

    /// As wide as the names need, and never more than half the pane: a caption
    /// that covered the picture it captions would have swapped the two.
    readonly property real wanted: Theme.gapM * 2 + Theme.plotLegendSwatch
                                   + Theme.gapS
                                   + Math.ceil(nameMetrics.advanceWidth)

    readonly property bool atLeft: legend.corner === "topLeft"
                                   || legend.corner === "bottomLeft"
    readonly property bool atTop: legend.corner === "topLeft"
                                  || legend.corner === "topRight"

    visible: legend.asking && legend.rows.length > 0
    width: Math.min(legend.wanted, Math.max(0, legend.area.width * 0.5))
    height: rowsColumn.height + Theme.gapS * 2
    x: legend.atLeft ? legend.area.x + Theme.gapM
                     : legend.area.x + legend.area.width - width - Theme.gapM
    y: legend.atTop ? legend.area.y + Theme.gapM
                    : legend.area.y + legend.area.height - height - Theme.gapM
    radius: Theme.radiusS
    color: Theme.plotLegendGround
    border.width: Theme.borderWidth
    border.color: Theme.border

    Column {
        id: rowsColumn

        x: Theme.gapM
        y: Theme.gapS
        width: parent.width - Theme.gapM * 2
        spacing: 0

        Repeater {
            model: legend.rows

            delegate: Item {
                id: row

                required property var modelData

                width: rowsColumn.width
                height: legend.rowHeight

                HoverHandler { id: rowHover }

                // The stroke, drawn as a stroke. A block of colour would read
                // as a category chip; what this stands for is a line, so it is
                // a line -- the same swatch the panel on the left draws, in
                // the same colour, asked of the same function.
                Rectangle {
                    id: swatch

                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.plotLegendSwatch
                    height: Theme.borderWidthAccent
                    radius: Theme.radiusS
                    color: legend.target
                           ? legend.target.seriesColor(row.modelData.series,
                                                       row.modelData.position,
                                                       legend.drawn.length)
                           : Theme.accent
                }

                Text {
                    id: name

                    anchors.left: swatch.right
                    anchors.leftMargin: Theme.gapS
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    text: row.modelData.label
                    font: Theme.monoSmall
                    color: Theme.textPrimary
                    elide: Text.ElideMiddle
                    verticalAlignment: Text.AlignVCenter

                    AppToolTip {
                        shown: rowHover.hovered && name.truncated
                        verbatim: true
                        text: row.modelData.label
                    }
                }
            }
        }

        // What it could not name. A caption that silently stopped at twelve
        // would be a legend that is wrong about the picture rather than short
        // of room for it.
        Text {
            width: rowsColumn.width
            height: legend.rowHeight
            visible: legend.unnamed > 0
            text: qsTr("+%1 more").arg(legend.unnamed)
            font: Theme.micro
            color: Theme.textDisabled
            verticalAlignment: Text.AlignVCenter
        }
    }
}
