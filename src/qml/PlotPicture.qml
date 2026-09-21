// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import H5Scope.Backend

/// The plot drawn again, off screen, to be taken away.
///
/// Until 0.6.3 a copied plot was the pane itself, grabbed where it stood. That
/// is a screenshot: the reader's theme, the reader's window size, and whatever
/// the crosshair happened to be doing. Three things under Settings > Plot
/// Settings ask for something else -- publication colours, a chosen size, the
/// crosshair in or out -- and none of them can be had by re-styling the frame
/// on screen, because a grab is rendered from the scene as it stands and the
/// picture would be bought with a frame of the application in the wrong
/// colours. So the picture is a *different frame*, drawn where nobody is
/// looking and thrown away when the grab lands.
///
/// It is laid out at the size that was asked for rather than scaled to it, and
/// that is the whole reason this is a frame and not a bigger grab of the other
/// one. PlotFrame decides its gutters from the type it is about to set and its
/// ticks from the span and the room, so a picture built this way has type at
/// its proper size and as many numbered ticks as it has room for -- where a
/// scaled grab has the pane's own type magnified and the pane's own ticks, and
/// stretches outright if the shape asked for is not the shape on screen.
///
/// Positioned outside the window rather than hidden: an invisible item builds
/// no scene graph nodes and grabs as nothing at all. Off the edge it is
/// rendered like anything else and simply never seen.
///
/// **In publication mode the colours are the light scope's, not one ink.** The
/// chrome is the light theme's chrome and the strokes are the light theme's
/// palette -- the picture a reader would see if they flipped the theme, drawn
/// without flipping it. See Theme's paper block and `paperPalettes`.
///
/// **In publication mode it holds the picture twice**, on white above and on
/// black below, and is grabbed as the pair. A grab carries no alpha channel to
/// rely on -- under the software renderer it comes back as opaque RGB -- so a
/// frame that simply drew no ground would land in the clipboard as a black
/// slab. The two together say exactly what any ground was covering; see
/// ImageClipboard::copyItem, which does that arithmetic.
Item {
    id: picture

    /// The PlotSurface this is a picture of. Everything about the view comes
    /// from it, so the picture cannot disagree with the pane about what it is
    /// showing.
    property var surface: null
    /// The live item whose drawn lines are copied. The copy is made in C++
    /// (PlotItem::adopt) and is a copy rather than a second borrow; see the
    /// note there, which is about a lifetime rather than about a cost.
    property Item sourceLines: null
    /// Print rather than screen: the light scope's colours -- its chrome and
    /// its line palette both -- and no ground at all.
    property bool publication: false
    /// Whether the crosshair and the sample it snapped to are in the picture.
    property bool includeCursor: false

    /// The size of the picture itself. `height` is twice this when the pair is
    /// being drawn, and the caller asks for `pageHeight` back.
    property int pageWidth: 0
    property int pageHeight: 0

    /// How many grounds the picture is drawn on: one, or the pair.
    readonly property int passes: picture.publication ? 2 : 1

    width: picture.pageWidth
    height: picture.pageHeight * picture.passes

    // Far enough out that no window is going to reach it, at full opacity and
    // visibility, which is what keeps its nodes built.
    x: -32768
    y: -32768

    /// Copy what the live item is drawing, and dress it for the picture.
    ///
    /// Called once, by whoever built this. The colours are *assigned* rather
    /// than bound for the reason PlotSurface.restyle gives: a line belongs to a
    /// C++ item and not to the QML object tree.
    function take() {
        if (!picture.sourceLines || !picture.surface)
            return false

        // The same two indices restyle() keeps apart: `i` is the line's place
        // in what the item was handed, `drawn[i]` is which line of the legend
        // it is, and a palette answers to the second.
        const drawn = picture.surface.plot ? picture.surface.plot.drawnSeries : []
        for (let pass = 0; pass < picture.passes; ++pass) {
            const frame = pages.itemAt(pass)
            if (!frame)
                return false
            frame.lines.adopt(picture.sourceLines)
            for (let i = 0; i < frame.lines.lineCount(); ++i) {
                const series = i < drawn.length ? drawn[i] : i
                // The same question in both cases, asked of the other scope
                // for the picture: `seriesColor` answers with the light
                // scope's palette, the light scope's accent and the reader's
                // own per-line colours, which is what "the light theme's
                // picture" means. Until 0.6.4 this branch drew every stroke
                // in one black ink instead, and that cost the picture the one
                // thing colour was buying -- which line is which -- for six
                // traces that a caption then had to name in colours the
                // picture did not contain.
                frame.lines.setSeriesColor(
                    i, picture.surface.seriesColor(series, i, drawn.length,
                                                   picture.publication))
                // The opacity does not follow it, and that is the one place
                // the picture is deliberately not the light theme. On screen
                // a bundle separates by piling up; on a page a line drawn at
                // part strength is a line somebody will complain about.
                frame.lines.setSeriesOpacity(
                    i, picture.publication
                        ? Theme.paperSeriesOpacity
                        : picture.surface.seriesOpacity(series, drawn.length))
                frame.lines.setSeriesWidth(i, picture.surface.seriesWidth(series))
            }
        }
        return true
    }

    Repeater {
        id: pages

        model: picture.passes

        PlotFrame {
            required property int index

            // Not "plotLines": the frame on screen answers to that, and a
            // findChild walking this window must not find three.
            linesObjectName: "pictureLines"

            y: index * picture.pageHeight
            width: picture.pageWidth
            height: picture.pageHeight

            viewMinX: picture.surface ? picture.surface.viewMinX : 0
            viewMaxX: picture.surface ? picture.surface.viewMaxX : 1
            viewMinY: picture.surface ? picture.surface.viewMinY : 0
            viewMaxY: picture.surface ? picture.surface.viewMaxY : 1
            gridMode: picture.surface ? picture.surface.gridMode : "loose"
            gridStepX: picture.surface ? picture.surface.gridStepX : 0
            gridStepY: picture.surface ? picture.surface.gridStepY : 0
            tickTarget: picture.surface ? picture.surface.tickTarget : 8

            title: picture.surface ? picture.surface.plotTitle : ""
            xLabel: picture.surface ? picture.surface.xLabel : ""
            yLabel: picture.surface ? picture.surface.yLabel : ""

            markers: picture.surface ? picture.surface.showMarkers : false
            markerSize: Theme.plotMarkerSize

            // Nobody is pointing at this frame, so it is handed the reading the
            // pane took rather than taking one of its own.
            showCursor: picture.includeCursor
            givenReading: picture.surface ? picture.surface.reading : null

            // The one thing that differs between the two passes, and it has to
            // be the only one: what is recovered from them is the difference,
            // so a picture that drew anything else differently would recover
            // that difference as coverage.
            ground: picture.publication ? (index === 0 ? Theme.n11 : Theme.n0)
                                        : Theme.surfaceInset
            ink: picture.publication ? Theme.paperInk : Theme.plotInk
            ruleMinor: picture.publication ? Theme.paperRuleMinor : Theme.border
            ruleMajor: picture.publication ? Theme.paperRuleMajor : Theme.borderStrong
            axisRule: picture.publication ? Theme.paperAxisRule : Theme.borderGuide
            cursorInk: picture.publication ? Theme.paperInk : Theme.accent

            // Inside the frame, exactly as it is on screen, which is what makes
            // it part of the picture rather than a caption on everything but
            // the copy.
            PlotOverlayLegend {
                objectName: "pictureOverlayLegend"

                target: picture.surface
                corner: picture.surface ? picture.surface.legendCorner : "topRight"
                area: parent.area

                ink: picture.publication ? Theme.paperInk : Theme.textPrimary
                // No ground under the caption either: a white slab on a
                // picture that stands on nothing is the one part of it still
                // carrying this application's own design into somebody's page.
                ground: picture.publication ? "transparent" : Theme.plotLegendGround
                rule: picture.publication ? Theme.paperRuleMajor : Theme.border
                faint: picture.publication ? Theme.paperAxisRule : Theme.textDisabled
                // The caption is asked the same question the strokes were,
                // so it names the colours that are actually in the picture.
                paper: picture.publication
            }
        }
    }
}
