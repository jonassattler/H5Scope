// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

.pragma library

// The plot as the figure drew it, read the way the suite asks about it.
//
// Until plotcore drew the plot, the suite asked the frame's own properties --
// yTicks, yGrid, yMinorTicks, the gutter -- and the plot item's -- lineCount(),
// seriesColor(), drawnRunCount. Both are plotcore::Figure now, behind
// gui::FigureItem, and these read the same answers off it: the axes as the
// scene says they were numbered and ruled (FigureItem.axis, in their own
// values rather than in rounded pixels), and the lines as the scene drew them
// (FigureItem.drawnLines). So the assertions below them are the ones the
// suite made of the QML, asked of the picture that replaced it.

/// The frame's questions, asked of `figure`.
///
/// Each one lays the frame out again first, so that a setting changed a line
/// before is in the answer -- as it was when the frame was QML and these were
/// bindings.
function frameOf(figure) {
    const axis = (which) => {
        figure.compose()
        return figure.axis(which)
    }
    return {
        get xTicks() { return axis("x").ticks },
        get yTicks() { return axis("y").ticks },
        get xGrid() { return axis("x").grid },
        get yGrid() { return axis("y").grid },
        get xMinorTicks() { return axis("x").minor },
        get yMinorTicks() { return axis("y").minor },
        get xLogNumbers() { return axis("x").logNumbers },
        get yLogNumbers() { return axis("y").logNumbers },
        get yLog() { return axis("y").logarithmic },
        get yLogBase() { return axis("y").base },
        get yMin() { return axis("y").low },
        get yMax() { return axis("y").high },
        get sideAxes() {
            figure.compose()
            return figure.sideAxes()
        },
        get area() {
            figure.compose()
            return figure.area
        },
        get gutterLeft() { return this.area.x },
        get reading() { return figure.reading }
    }
}

/// The plot item's questions, asked of `figure`: how many lines it was lent,
/// and what each drawn one was drawn in -- its colour without the opacity,
/// the opacity, the width.
function linesOf(figure) {
    const drawn = (i) => {
        figure.compose()
        const lines = figure.drawnLines()
        return i >= 0 && i < lines.length ? lines[i] : undefined
    }
    return {
        lineCount: () => figure.lineCount(),
        seriesColor: (i) => {
            const line = drawn(i)
            return line ? Qt.rgba(line.colour.r, line.colour.g, line.colour.b, 1) : undefined
        },
        seriesOpacity: (i) => {
            const line = drawn(i)
            return line ? line.colour.a : undefined
        },
        seriesWidth: (i) => {
            const line = drawn(i)
            return line ? line.width : undefined
        },
        get drawnRunCount() {
            figure.compose()
            return figure.drawnRunCount
        },
        get drawnPointCount() {
            figure.compose()
            return figure.drawnPointCount
        },
        get yLog() {
            figure.compose()
            return figure.axis("y").logarithmic
        }
    }
}

/// A point of the pane, `fx` across it and `fy` down it, in the figure's own
/// coordinates: where a pointer is put to read the plot.
function paneX(figure, fx) {
    return Math.round(figure.area.x + figure.area.width * fx)
}

function paneY(figure, fy) {
    return Math.round(figure.area.y + figure.area.height * fy)
}

/// The band's numbers keyed by what each one says: "start", "end",
/// "widthAbove" and the rest.
function bandLabelsByKey(figure) {
    const out = {}
    const labels = figure.bandLabels()
    for (let i = 0; i < labels.length; ++i)
        out[labels[i].key] = labels[i]
    return out
}

/// Whether `box` lies inside `area`, whole, and whether two boxes stand
/// `clearance` apart -- the band's own rule, one distance between a number
/// and whatever it is kept off.
function inside(box, area) {
    return box.x >= area.x - 1e-9 && box.y >= area.y - 1e-9
        && box.x + box.width <= area.x + area.width + 1e-9
        && box.y + box.height <= area.y + area.height + 1e-9
}

function clears(a, b, clearance) {
    return a.x + a.width + clearance <= b.x
        || b.x + b.width + clearance <= a.x
        || a.y + a.height + clearance <= b.y
        || b.y + b.height + clearance <= a.y
}

/// The numbers painted on one side of the pane, as text: `"left"`, up the
/// side, or `"foot"`, along the bottom -- whichever axis the plot put there.
function tickTexts(figure, side) {
    figure.compose()
    // TextRole.Tick, and Align.End across for the left, Middle for the foot.
    const across = side === "left" ? 2 : 1
    return figure.marks().filter((m) => m.kind === 3 && m.role === 0 && m.across === across)
                         .map((m) => m.text)
}

/// An axis's numbers inside its window, as text: the ones the frame paints.
function shownTicks(ticks) {
    return ticks.filter((t) => t.at >= -1e-9 && t.at <= 1 + 1e-9).map((t) => t.text)
}
