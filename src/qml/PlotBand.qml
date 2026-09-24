// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick

/// The region a right-drag is drawing, and the numbers that say what it is.
///
/// The band on its own says *where* the reader is about to look and nothing
/// about *what* they are about to see. Somebody dragging one over the second
/// half of a pulse is choosing an interval, and the interval is the thing they
/// want written down: where the drag started, where it has reached, and how
/// wide and how tall it has become by now. Those are the same four numbers the
/// axes are about to be set to, so they are the four drawn here.
///
/// They are *asked of the surface* rather than worked out again -- `dataXAt`
/// and `dataYAt` are the two functions zoomToRegion resolves the band itself
/// with. A second copy of that mapping would be a readout that can disagree
/// with the window it names, and being wrong about where the plot is going is
/// the one thing this may not be.
///
/// The crosshair is off while a band is being drawn -- see
/// PlotSurface.selecting. Two readouts of two different things, one snapped to
/// the nearest sample and one following the pointer, drawn over each other in
/// one pane is a picture the reader has to take apart before they can read
/// either half of it.
///
/// Where the numbers go is the rest of this file, and one rule carries it:
/// **nothing is drawn over the band, over the pane's edge, over another
/// number, or under the pointer.** A coordinate is drawn at the corner it
/// names, pushed away from the band -- and past the cursor where that push
/// would go under it, because the corner that follows the pointer is the one
/// the reader is covering with their own hand. A length is drawn against its
/// own edge only where there is room for one, and the two that measure the
/// sides read along them. A length that does not fit is left out rather than
/// squeezed in: the two corners are the reading, the lengths are the
/// subtraction between them, and the subtraction is what a crowded pane can
/// afford to lose.
Item {
    id: readout

    /// The plot surface this band is being drawn over. Asked what a pixel
    /// stands for and how a number is written; told nothing.
    property var target
    /// The pane, in the parent's coordinates -- PlotSurface.plotRect.
    property rect area: Qt.rect(0, 0, 0, 0)
    /// Whether a band is being drawn at all.
    property bool active: false
    /// Where the press was, and where the pointer has reached, both in the
    /// parent's coordinates.
    property point from: Qt.point(0, 0)
    property point to: Qt.point(0, 0)

    x: readout.area.x
    y: readout.area.y
    width: readout.area.width
    height: readout.area.height
    visible: readout.active
    // A drag that runs off the pane says "to the edge" rather than drawing
    // over the axis labels. Everything below is clamped to the pane by the
    // same arithmetic the zoom clamps with, so this clips nothing that was
    // meant to be seen -- which is what keeps it from ever making the one
    // thing clipping would make of a readout: half a number.
    clip: true

    // --- the band, in this item's own pixels ------------------------------
    // Clamped rather than merely clipped, because these two corners are what
    // the numbers are read out of as well as what the rectangle is drawn from.
    // The band the reader sees, the numbers beside it and the window they get
    // on release are then one statement about one pair of points.
    function paneX(px) {
        return Math.max(0, Math.min(readout.area.width, px - readout.area.x))
    }

    function paneY(py) {
        return Math.max(0, Math.min(readout.area.height, py - readout.area.y))
    }

    readonly property real startX: readout.paneX(readout.from.x)
    readonly property real startY: readout.paneY(readout.from.y)
    readonly property real endX: readout.paneX(readout.to.x)
    readonly property real endY: readout.paneY(readout.to.y)

    readonly property real bandLeft: Math.min(readout.startX, readout.endX)
    readonly property real bandRight: Math.max(readout.startX, readout.endX)
    readonly property real bandTop: Math.min(readout.startY, readout.endY)
    readonly property real bandBottom: Math.max(readout.startY, readout.endY)

    /// The band as a box, for the layout below to keep its distance from.
    readonly property rect bandRect: Qt.rect(readout.bandLeft, readout.bandTop,
                                             readout.bandRight - readout.bandLeft,
                                             readout.bandBottom - readout.bandTop)

    // --- the band, in the axes' own numbers -------------------------------
    /// The two corners, in data coordinates. Guarded by the target and not by
    /// `active`: a band nobody is drawing is two stale points, and reading a
    /// stale point costs a subtraction. A surface that has not been handed one
    /// has no axes to ask, which is the case worth a branch.
    ///
    /// Both coordinates of each corner go to both questions, because on a
    /// flipped pane x is read off the height and y off the width.
    readonly property real startValueX:
        readout.target ? readout.target.dataXAt(readout.from.x, readout.from.y) : 0
    readonly property real startValueY:
        readout.target ? readout.target.dataYAt(readout.from.y, readout.from.x) : 0
    readonly property real endValueX:
        readout.target ? readout.target.dataXAt(readout.to.x, readout.to.y) : 0
    readonly property real endValueY:
        readout.target ? readout.target.dataYAt(readout.to.y, readout.to.x) : 0

    /// A corner, written. Two numbers and a comma, which is how a coordinate
    /// is written everywhere else -- which axis is which is said by where the
    /// number is rather than by a letter in front of it, and a pair is already
    /// as much as a corner of the pane can hold.
    ///
    /// Each number is written by the axis it is on (`xNumber`, `yNumber`),
    /// which takes its decimals off the span on screen. So these say what the
    /// ticks beneath them say, to the same precision, and a reader zooming in
    /// is given digits as the ticks close up rather than six of them whatever
    /// they are looking at.
    ///
    /// Only the x when there is no common y axis: every line is then on an
    /// axis of its own, the y the band spans is a different number on each of
    /// them, and the one the gestures run on is a nominal axis nobody can see.
    function coordinateText(x, y) {
        if (!readout.target)
            return ""
        if (!readout.target.sharedAxis)
            return readout.target.xNumber(x)
        return qsTr("%1, %2").arg(readout.target.xNumber(x))
                             .arg(readout.target.yNumber(y))
    }

    /// A length, written. With a sign in front of it, because a lone number
    /// drawn against an edge of the band would read as one more coordinate.
    ///
    /// U+2206 INCREMENT and deliberately not U+0394 GREEK CAPITAL LETTER
    /// DELTA: they draw the same triangle and the mono face this application
    /// ships carries only the first of them, so the Greek one would come out a
    /// box on the one machine whose fonts are this program's own.
    function lengthText(span, written) {
        if (!readout.target)
            return ""
        return qsTr("∆%1").arg(written(span))
    }

    readonly property string startText:
        readout.coordinateText(readout.startValueX, readout.startValueY)
    readonly property string endText:
        readout.coordinateText(readout.endValueX, readout.endValueY)
    /// How far the band runs in x and in y, each written by its own axis --
    /// and none for a y with no common axis, for coordinateText's reason.
    readonly property string xSpanText:
        readout.target ? readout.lengthText(
                             Math.abs(readout.endValueX - readout.startValueX),
                             readout.target.xNumber)
                       : ""
    readonly property string ySpanText:
        readout.target && readout.target.sharedAxis ? readout.lengthText(
                             Math.abs(readout.endValueY - readout.startValueY),
                             readout.target.yNumber)
                       : ""
    /// ...and which of them is written along which edge: the band's width is
    /// a length of whichever axis runs across the pane, which on a flipped one
    /// is y.
    readonly property bool flipped: readout.target ? !!readout.target.flipped : false
    readonly property string widthText: readout.flipped ? readout.ySpanText
                                                        : readout.xSpanText
    readonly property string heightText: readout.flipped ? readout.xSpanText
                                                         : readout.ySpanText

    // The four of them measured before any of them is placed: where a number
    // goes depends on how big it is, and a box laid out first and measured
    // afterwards is a box that was drawn in the wrong place for a frame.
    TextMetrics {
        id: startMetrics

        font: Theme.readout
        text: readout.startText
    }

    TextMetrics {
        id: endMetrics

        font: Theme.readout
        text: readout.endText
    }

    TextMetrics {
        id: widthMetrics

        font: Theme.readout
        text: readout.widthText
    }

    TextMetrics {
        id: heightMetrics

        font: Theme.readout
        text: readout.heightText
    }

    /// The air between a number and whatever it is being kept off: the band,
    /// the pane's edge, another number. One distance for all three, because
    /// they are one rule.
    readonly property int clearance: Theme.gapS

    /// A number's box: the type, and a little ground around it. See
    /// Theme.plotReadoutGround for why there is ground under it at all.
    function boxFor(metrics) {
        return { width: Math.ceil(metrics.width) + Theme.gapXS * 2,
                 height: Math.ceil(metrics.height) + Theme.gapXS * 2 }
    }

    /// Whether one box stands clear of another, with the clearance between
    /// them. A little more than "does not overlap": two numbers a pixel apart
    /// read as one number twice as long.
    ///
    /// Satisfied exactly at the clearance, which is where everything placed
    /// below sits -- a number a gap off the band's edge is clear of the band.
    function clears(a, b) {
        return a.x + a.width + readout.clearance <= b.x
            || b.x + b.width + readout.clearance <= a.x
            || a.y + a.height + readout.clearance <= b.y
            || b.y + b.height + readout.clearance <= a.y
    }

    /// Whether a box is inside the pane, whole.
    function inside(box) {
        return box.x >= 0 && box.y >= 0
            && box.x + box.width <= readout.width
            && box.y + box.height <= readout.height
    }

    /// The numbers and where each of them goes: `{ key, text, x, y, width,
    /// height, turn }`, in this item's own pixels -- `turn` being the quarter
    /// a length against a vertical side is read through, and zero for the rest.
    ///
    /// One list rather than six separately placed items, for two reasons. A
    /// delegate then depends on its own entry and on nothing else, which is
    /// how everything else drawn over this plot is built. And the whole layout
    /// becomes one value the QML suite can read: that nothing overlaps the
    /// band, that nothing overlaps another number, that a length with no room
    /// is absent rather than misplaced -- asserted rather than eyeballed on a
    /// screenshot.
    readonly property var labels: {
        const out = []
        if (!readout.active || !readout.target
                || readout.width <= 0 || readout.height <= 0)
            return out

        const gap = readout.clearance
        const paneW = readout.width
        const paneH = readout.height
        const clamp = (v, size, extent) => Math.max(0, Math.min(extent - size, v))

        // What a corner readout has to stay off. The band, always -- and for
        // the corner the pointer is on, the pointer.
        //
        // A cursor is drawn *from* its hotspot down and to the right, which is
        // what an arrow is, so a number placed in that quadrant is a number the
        // reader is covering with the thing they are pointing with. It is the
        // end corner every time, because that corner is where the pointer is,
        // and it showed up the moment a band was dragged downwards: the reading
        // that changes as you drag was the one under your own cursor.
        const pointerBox = { x: readout.endX, y: readout.endY,
                             width: Theme.pointerSize, height: Theme.pointerSize }

        const stands = (box, avoid) => {
            if (!readout.inside(box)) {
                return false
            }
            for (let i = 0; i < avoid.length; ++i) {
                if (!readout.clears(box, avoid[i])) {
                    return false
                }
            }
            return true
        }

        // --- the two corners, which are always drawn ---------------------
        // Each is pushed away from the band on both axes, so away from the
        // edges of the pane it stands clear of the rectangle diagonally, at
        // the corner it is a reading of. Where that push is into the quadrant
        // the pointer is drawn in, it is a pointer's width rather than a gap.
        //
        // Four placements are tried in order, and the first that stands clear
        // of everything is taken: the preferred one, the same sliding the other
        // way, and the two that fold back over the corner -- which are worth
        // trying because at the pane's edge one of the two pushes has nowhere
        // to go. A number that slid is still a number beside the corner it
        // names.
        const place = (cx, cy, awayX, awayY, size, avoid) => {
            // A pointer's width *and* the gap: the clearance a number keeps
            // from the cursor is the clearance it keeps from everything else,
            // measured from the far side of it.
            const outX = awayX > 0 && awayY > 0 ? Theme.pointerSize + gap : gap
            const wantX = awayX < 0 ? cx - gap - size.width : cx + outX
            const wantY = awayY < 0 ? cy - gap - size.height : cy + gap
            const backX = awayX < 0 ? cx + gap : cx - gap - size.width
            const backY = awayY < 0 ? cy + gap : cy - gap - size.height
            const at = (x, y) => ({ x: x, y: y,
                                    width: size.width, height: size.height })
            const tries = [at(clamp(wantX, size.width, paneW), wantY),
                           at(wantX, clamp(wantY, size.height, paneH)),
                           at(clamp(wantX, size.width, paneW), backY),
                           at(backX, clamp(wantY, size.height, paneH))]
            for (let i = 0; i < tries.length; ++i) {
                if (stands(tries[i], avoid)) {
                    return tries[i]
                }
            }
            // A band drawn into the corner of the pane, which leaves its own
            // corner nowhere outside it to stand. It stands on the band
            // instead: that fill is a veil and not a cover, and a corner with
            // no number at all would be the readout failing exactly where the
            // reader is looking.
            return at(clamp(wantX, size.width, paneW),
                      clamp(wantY, size.height, paneH))
        }

        // Diagonally opposite corners, so the two pushes are exact opposites
        // -- which is what holds the two coordinates apart when the band is
        // small enough that they would otherwise meet.
        const runsRight = readout.endX >= readout.startX ? 1 : -1
        const runsDown = readout.endY >= readout.startY ? 1 : -1
        const startSize = readout.boxFor(startMetrics)
        const endSize = readout.boxFor(endMetrics)
        const startAt = place(readout.startX, readout.startY,
                              -runsRight, -runsDown, startSize,
                              [readout.bandRect, pointerBox])
        const endAt = place(readout.endX, readout.endY,
                            runsRight, runsDown, endSize,
                            [readout.bandRect, pointerBox])
        // `turn: 0` and not nothing at all. A coordinate is read straight
        // and only the two lengths against the vertical sides are turned, so
        // it is tempting to leave the key off the pair that does not use it
        // -- and that is what was done, and it put two warnings a frame on
        // the console for as long as a band was being drawn: the delegate
        // binds `rotation` to this, and an absent key is `undefined`, which
        // is not a double. Every entry in this list carries every key the
        // delegate reads.
        const startBox = { key: "start", text: readout.startText,
                           x: startAt.x, y: startAt.y,
                           width: startSize.width, height: startSize.height,
                           turn: 0 }
        const endBox = { key: "end", text: readout.endText,
                         x: endAt.x, y: endAt.y,
                         width: endSize.width, height: endSize.height,
                         turn: 0 }
        out.push(startBox)
        out.push(endBox)

        // --- the two lengths, drawn wherever they fit --------------------
        // On both of the band's x lines and both of its y lines, because a
        // reader measuring an edge is already looking at that edge -- and a
        // gap outside each of them, which is what keeps them off the band.
        //
        // A length is offered only where it fits *between the two corners it
        // measures*: wider than the band it spans, it would reach past the
        // rectangle and into the coordinates at the ends of it, and a band
        // narrower than its own width is one whose width the corners already
        // say. That test is most of what keeps these clear of everything else;
        // the two below are the rest of it -- the pane's edge, and the
        // coordinates, which are placed first and never give way.
        const candidates = []
        const widthSize = readout.boxFor(widthMetrics)
        const written = readout.boxFor(heightMetrics)
        // The height reads *along* the side it measures, turned the same
        // quarter the y axis's own name is turned by -- and for the same
        // reason: a number lying across the side of a tall band is a number the
        // reader has to hold at an angle to the thing it is about.
        //
        // Turning it swaps what it costs in each direction, which is the other
        // half of why it is worth doing: what it now needs beside the band is a
        // line's height rather than a number's length, so a tall narrow band
        // near the edge of the pane can carry its height where it could not
        // before -- and what it needs *along* the band is the length, which a
        // band tall enough to be worth measuring has.
        //
        // The two are turned opposite ways, not the same way. They are a pair
        // on either side of one rectangle, and a pair reads as one when each is
        // the other reflected: every letter's top faces away from the band it
        // is measuring, so the left-hand one runs up the page and the
        // right-hand one runs down it. Turned alike, the right-hand number
        // reads with its back to the band and the two stop looking like the
        // same measurement said twice.
        const heightSize = { width: written.height, height: written.width }
        const midX = (readout.bandLeft + readout.bandRight) / 2
        const midY = (readout.bandTop + readout.bandBottom) / 2
        if (readout.widthText !== ""
                && widthSize.width <= readout.bandRect.width) {
            candidates.push({ key: "widthAbove", text: readout.widthText,
                              x: midX - widthSize.width / 2,
                              y: readout.bandTop - gap - widthSize.height,
                              width: widthSize.width, height: widthSize.height,
                              turn: 0 })
            candidates.push({ key: "widthBelow", text: readout.widthText,
                              x: midX - widthSize.width / 2,
                              y: readout.bandBottom + gap,
                              width: widthSize.width, height: widthSize.height,
                              turn: 0 })
        }
        if (readout.heightText !== ""
                && heightSize.height <= readout.bandRect.height) {
            candidates.push({ key: "heightLeft", text: readout.heightText,
                              x: readout.bandLeft - gap - heightSize.width,
                              y: midY - heightSize.height / 2,
                              width: heightSize.width, height: heightSize.height,
                              turn: -90 })
            candidates.push({ key: "heightRight", text: readout.heightText,
                              x: readout.bandRight + gap,
                              y: midY - heightSize.height / 2,
                              width: heightSize.width, height: heightSize.height,
                              turn: 90 })
        }
        for (let i = 0; i < candidates.length; ++i) {
            const box = candidates[i]
            if (readout.inside(box) && readout.clears(box, startBox)
                    && readout.clears(box, endBox))
                out.push(box)
        }
        return out
    }

    // What the reader is about to ask for, drawn while they are deciding.
    Rectangle {
        x: readout.bandRect.x
        y: readout.bandRect.y
        width: readout.bandRect.width
        height: readout.bandRect.height
        color: Theme.plotBandFill
        border.width: Theme.borderWidthAccent
        border.color: Theme.accent
    }

    // ...and what it would be, in numbers. Whole pixels, because a line of
    // 10px type laid out on a half pixel is a line of blurred type.
    Repeater {
        model: readout.labels

        delegate: Rectangle {
            id: labelGround

            required property var modelData

            x: Math.round(labelGround.modelData.x)
            y: Math.round(labelGround.modelData.y)
            width: labelGround.modelData.width
            height: labelGround.modelData.height
            radius: Theme.radiusS
            color: Theme.plotReadoutGround

            // Turned about its own centre inside a box whose sides were
            // swapped for it, which is how PlotFrame hangs the y axis's name
            // as well: a rotation is a painter's transform and leaves the
            // item's own width and height alone, so the box has to be told.
            Text {
                anchors.centerIn: parent
                rotation: labelGround.modelData.turn
                text: labelGround.modelData.text
                font: Theme.readout
                color: Theme.textPrimary
            }
        }
    }
}
