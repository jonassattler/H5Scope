// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import QtQuick.Layouts
import H5Scope.Backend

/// The data views -- table, plot and image -- and the bar of controls they
/// share.
///
///     [legend] SLICE /cube[ :, :, 3, : ] ..... [data settings] [plot settings]
///     +---------------------------------------------+-----------+
///     |  the table, the plot, or the image          |  the one  |
///     |                                             |   panel   |
///     +---------------------------------------------+           |
///     |  what is on screen, in numbers              |           |
///     +---------------------------------------------+-----------+
///
/// The three are peers of the information view in the window's one tab strip
/// (see Main.qml), so this file no longer carries a mode bar of its own: the
/// window says which view is showing and this arranges it.
///
/// They differ in how they draw a slice, never in which slice they draw. That
/// is the data settings panel's business, through TableSetupModel and
/// DatasetTableModel, and the slice line at the head of the bar both prints
/// what it resolves to and takes an edited one back -- so a rank-4 dataset is
/// plottable and viewable as an image on exactly the terms it is already
/// browsable, and a reader who knows the slice they want can write it rather
/// than build it.
///
/// Everything that acts on what is drawn sits in that one bar, along the top:
/// the slice, the panel that decides it, the panel that decides how this view
/// draws it, and -- for the plot -- the legend, at the left end because that
/// is the side the legend comes in from. The bar under the view is a readout
/// and nothing else.
///
/// The rail holds one panel at a time. Two of them would take a third of the
/// window between them, and the reader is answering one question at a time
/// anyway. Data settings survive a view change because they still apply after
/// it -- they are the one thing all three views share -- and a view's own
/// settings do not, and close.
Rectangle {
    id: root

    color: Theme.background

    /// "table" | "plot" | "image"
    property string viewMode: "table"
    /// Which panel the rail is showing:
    /// "" | "data" | "post" | "table" | "plot" | "image"
    property string rail: ""

    /// Whether the rail is shown. `rail` is the intent; a scalar has no
    /// dimensions to set up, so data settings never open for one whatever was
    /// asked for.
    readonly property bool railVisible:
        rail === "data" ? AppController.datasetRank > 0 : rail !== ""

    /// The rail is 212 for every panel but one. The pipeline states an
    /// operation, an argument and a shape on one line per step, and the shape
    /// column read downwards is the whole of what it is for; stacked into 212
    /// those become three lines a row and stop lining up with each other. It
    /// hands the width back on close.
    readonly property int railWidth: rail === "post" ? Theme.railWidthWide
                                                     : Theme.railWidth

    /// Kept for the QML suite and for anything else that asks whether the data
    /// settings sidebar is up, which is what this tab's only toggle used to be.
    readonly property bool setupVisible: rail === "data"
                                         && AppController.datasetRank > 0
    /// The table's own presentation, which still varies with the datatype.
    readonly property string mode: tableSurface.mode

    /// The narrowest this view can be drawn at, which is the narrowest the bar
    /// can hold its own contents. Main.qml builds the window's minimum width
    /// out of it.
    ///
    /// Measured rather than declared, because every part of it is text and the
    /// host decides how wide text is. The same three buttons come out 375
    /// pixels wide here and wider on a machine whose fontconfig hints
    /// differently -- same faces, they are compiled in, but not the same
    /// advance widths. A minimum typed as one number is therefore a number
    /// that is right on the machine it was typed on: this window's was 1000,
    /// which left the bar thirty pixels of slack locally and none at all on
    /// CI, where the slice path silently elided at the minimum size the window
    /// would open at.
    ///
    /// Every button is counted whether or not it is showing. The legend button
    /// appears only for the plot and the badge only while a pipeline runs, and
    /// a minimum width that grew when they did would be a window that has to
    /// widen itself to change tabs.
    readonly property real barMinimumWidth:
        Theme.gapM * 2                    // the row's margins
        + Theme.gapM * 5                  // and the gaps between six parts
        + legendButton.implicitWidth
        + sliceLabel.implicitWidth
        + Theme.sliceWellMinimum
        + dataSettingsButton.implicitWidth
        + postprocessButton.implicitWidth
        + viewSettingsButton.implicitWidth

    function show(mode) {
        if (root.viewMode === mode)
            return
        // A panel of settings for the view being left no longer applies to
        // anything on screen.
        if (root.rail === root.viewMode)
            root.rail = ""
        root.viewMode = mode
    }

    function toggleRail(which) {
        root.rail = (root.rail === which) ? "" : which
    }

    // A dataset of text can be read, but not plotted or drawn: the modes that
    // cannot show it must not stay selected when the selection moves to one.
    Connections {
        target: AppController
        function onSelectionChanged() {
            if (!AppController.datasetIsNumeric && root.viewMode !== "table")
                root.show("table")
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // --- the bar every view acts from -------------------------------
        // Hidden along with everything it acts on: with no dataset selected
        // the view below says so across the whole tab, and a strip of chrome
        // over the top of that sentence would be controls for nothing.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.sliceBarHeight
            visible: AppController.datasetTabVisible
            // A step up from the ground, as the design gives it: this bar is
            // the one surface in the tab that acts rather than displays.
            color: Theme.surfaceRaised

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.gapM
                anchors.rightMargin: Theme.gapM
                spacing: Theme.gapM

                // Which line is which. At the head of the bar because the
                // legend comes in over the left of the plot, and a button that
                // opens something should be on the side it opens from -- which
                // is also where it stood when it was under the view.
                AppToolButton {
                    id: legendButton

                    objectName: "legendButton"

                    text: qsTr("legend")
                    size: "md"
                    variant: plotSurface.legendOpen ? "secondary" : "ghost"
                    opens: "panel"
                    open: plotSurface.legendOpen
                    visible: root.viewMode === "plot" && plotSurface.drawable
                    onClicked: plotSurface.legendOpen = !plotSurface.legendOpen
                }

                Text {
                    id: sliceLabel

                    text: qsTr("slice")
                    font: Theme.micro
                    color: Theme.textSecondary
                }

                // Which elements are on screen is the single most consequential
                // fact in this tab -- every number below it is a number *of*
                // this slice -- and it is also the fastest way to change them.
                SliceField {
                    id: sliceField

                    // Grows with the line it holds, to the width of that line
                    // and no further -- the ceiling -- and hands the width
                    // back when the bar cannot afford it, down to the floor.
                    // Both are the field's own, because it is the file that
                    // knows what the parts cost.
                    //
                    // fillWidth is what makes the other two mean anything: a
                    // RowLayout never resizes an item that does not fill, so
                    // the well stood at its implicit width whatever the bar
                    // could afford and simply overflowed -- a slice of ninety
                    // characters put the two settings buttons a hundred and
                    // twenty pixels past the right-hand end of the bar, off
                    // the window. Filling does not mean stretching here: the
                    // ceiling keeps the well the width of its own line, and
                    // the spacer below takes everything left over.
                    Layout.fillWidth: true
                    Layout.minimumWidth: sliceField.minimumUsefulWidth
                    Layout.maximumWidth: sliceField.implicitWidth
                }

                // Every number below this bar is then a number this
                // application worked out rather than one the file holds, and
                // that is worth saying in the one colour this system uses to
                // mean "look at this". Right after the slice, because the
                // slice is what it is qualifying: the elements named there go
                // through the pipeline before anything draws them.
                Badge {
                    objectName: "postprocessBadge"

                    Layout.alignment: Qt.AlignVCenter
                    visible: AppController.postprocessActive
                    tone: "warn"
                    text: qsTr("post processing active")
                }

                // Why the line will not do, or -- when there is nothing wrong
                // with it -- that the tab below is not drawn from it yet.
                // Printed where it happened rather than hidden behind a hover.
                // Caption, not micro: micro uppercases what it is given, which
                // turns a sentence into a machine label.
                //
                // The two never appear together, and the colour separates
                // them: amber is this system's "look at this" and belongs to
                // the line that will not read. An edit nobody has pressed
                // Return on is not a mistake, so it takes signal white -- the
                // same ink the well's own ground has stepped a tenth toward.
                Text {
                    id: sliceNote

                    objectName: "sliceNote"

                    // A reason asks for a readable phrase rather than for the
                    // whole sentence, and grows into whatever the well does
                    // not want. Reasons are often longer than the bar, and
                    // asking for all of one would squeeze the line it is about
                    // -- hiding the path and scrolling the subscripts at the
                    // one moment they are being read. It keeps the tooltip and
                    // the amber border to speak through.
                    //
                    // The pending note asks for all of itself. It is one short
                    // sentence this file wrote, the same one every time, and
                    // half of it is the instruction -- a note reading "not
                    // applied yet ..." has elided away the only part worth
                    // printing. It asks for no floor either, so a bar too
                    // narrow for both still gives the well its room first.
                    Layout.fillWidth: true
                    Layout.preferredWidth: sliceField.error !== ""
                        ? Math.min(sliceNote.implicitWidth, Theme.s14)
                        : sliceNote.implicitWidth
                    Layout.minimumWidth: 0
                    Layout.alignment: Qt.AlignVCenter
                    visible: text !== ""
                    text: sliceField.error !== ""
                          ? sliceField.error
                          : sliceField.pending
                            ? qsTr("not applied yet — press Return")
                            : ""
                    font: Theme.caption
                    color: sliceField.error !== "" ? Theme.warning : Theme.accent
                    elide: Text.ElideRight

                    HoverHandler { id: noteHover }

                    AppToolTip {
                        shown: sliceNote.truncated && noteHover.hovered
                        text: sliceNote.text
                    }
                }

                // Keeps the buttons at the right end while there is nothing
                // to say about the line.
                Item {
                    Layout.fillWidth: true
                    visible: !sliceNote.visible
                }

                // Rank 0 is one cell: there is nothing to choose about it.
                AppToolButton {
                    id: dataSettingsButton

                    text: qsTr("data settings")
                    size: "md"
                    variant: root.rail === "data" ? "secondary" : "ghost"
                    opens: "panel"
                    open: root.rail === "data"
                    visible: AppController.datasetRank > 0
                    onClicked: root.toggleRail("data")
                }

                // Beside the data settings because it is the second half of
                // the same question: that panel says which elements, this one
                // says what is done to them before they are drawn.
                AppToolButton {
                    id: postprocessButton

                    objectName: "postprocessButton"

                    text: qsTr("postprocessing")
                    size: "md"
                    variant: root.rail === "post" ? "secondary" : "ghost"
                    opens: "panel"
                    open: root.rail === "post"
                    visible: AppController.datasetTabVisible
                    onClicked: root.toggleRail("post")
                }

                // Named for the view it belongs to rather than "view
                // settings": the reader is looking at one of three things and
                // the button says which one it is about to describe.
                AppToolButton {
                    id: viewSettingsButton

                    text: root.viewMode === "plot" ? qsTr("plot settings")
                        : root.viewMode === "image" ? qsTr("image settings")
                                                    : qsTr("table settings")
                    size: "md"
                    variant: root.rail === root.viewMode ? "secondary" : "ghost"
                    opens: "panel"
                    open: root.rail === root.viewMode
                    visible: root.viewMode === "plot" ? plotSurface.drawable
                           : root.viewMode === "image" ? imageSurface.drawable
                           : tableSurface.mode !== "empty"
                             && tableSurface.mode !== "message"
                    onClicked: root.toggleRail(root.viewMode)
                }
            }

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: Theme.borderWidth
                color: Theme.border
            }
        }

        // --- the data, and the panel that decides which of it ------------
        // The rail takes width from the content rather than height, so the
        // footers stay put and the grid keeps its full run of rows while the
        // reader rearranges it.
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                StackLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: root.viewMode === "plot" ? 1
                                : root.viewMode === "image" ? 2 : 0

                    TableSurface {
                        id: tableSurface
                        objectName: "tableSurface"
                    }
                    // The plot and the image sample the file to answer what
                    // they would draw, so each is told whether it is the one
                    // on screen rather than working it out from a size.
                    PlotSurface {
                        id: plotSurface
                        objectName: "plotSurface"
                        active: root.viewMode === "plot"
                    }
                    ImageSurface {
                        id: imageSurface
                        objectName: "imageSurface"
                        active: root.viewMode === "image"
                    }
                }

                // --- the current view's own footer ----------------------
                ViewFooter {
                    id: tableFooter

                    Layout.fillWidth: true
                    visible: root.viewMode === "table"
                             && tableSurface.mode !== "empty"
                             && tableSurface.mode !== "message"
                    facts: {
                        if (tableSurface.mode === "text")
                            return [qsTr("1 string")]
                        const shape = [counted(tableSurface.rows, qsTr("row"), qsTr("rows")),
                                       counted(tableSurface.columns, qsTr("col"), qsTr("cols"))]
                        if (tableSurface.mode === "strings") {
                            return [counted(AppController.datasetElementCount,
                                            qsTr("string"), qsTr("strings"))].concat(shape)
                        }
                        return shape
                    }
                }

                ViewFooter {
                    id: plotFooter

                    readonly property var plot: AppController.datasetPlot

                    Layout.fillWidth: true
                    visible: root.viewMode === "plot" && plotSurface.drawable
                    facts: {
                        if (!plotSurface.drawable)
                            return []
                        const shown = [counted(plot.seriesCount, qsTr("line"), qsTr("lines"))]
                        if (plot.seriesCount < plot.sourceSeriesCount)
                            shown[0] += qsTr(" of %1").arg(plot.sourceSeriesCount)
                        shown.push(counted(plot.pointCount, qsTr("point"), qsTr("points"))
                                   + (plot.thinned ? qsTr(" thinned") : ""))
                        // Once the view has been zoomed or panned the range of
                        // the data is no longer the range on screen, and only
                        // one of the two is worth printing.
                        if (plotSurface.zoomed) {
                            shown.push(qsTr("x %1 … %2")
                                       .arg(plotSurface.viewMinX.toPrecision(4))
                                       .arg(plotSurface.viewMaxX.toPrecision(4)))
                            shown.push(qsTr("y %1 … %2")
                                       .arg(plotSurface.viewMinY.toPrecision(4))
                                       .arg(plotSurface.viewMaxY.toPrecision(4)))
                            shown.push(qsTr("zoom %1×")
                                       .arg(Math.max(plotSurface.zoomX,
                                                     plotSurface.zoomY).toFixed(1)))
                        } else {
                            shown.push(qsTr("y %1 … %2").arg(plot.minimum.toPrecision(4))
                                                        .arg(plot.maximum.toPrecision(4)))
                        }
                        return shown
                    }
                }

                ViewFooter {
                    id: imageFooter

                    readonly property var image: AppController.datasetImage

                    Layout.fillWidth: true
                    visible: root.viewMode === "image" && imageSurface.drawable
                    facts: {
                        if (!imageSurface.drawable)
                            return []
                        const size = image.width + " × " + image.height
                        const shown = [image.thinned
                                ? qsTr("%1 of %2 × %3").arg(size)
                                                       .arg(image.sourceWidth)
                                                       .arg(image.sourceHeight)
                                : size,
                                qsTr("%1 … %2").arg(image.minimum.toPrecision(4))
                                               .arg(image.maximum.toPrecision(4))]
                        // Which channels the picture is made of. The slice
                        // line above describes the *table*, which holds one
                        // index of the colour axis; an RGB picture is reading
                        // three, and this is where it says which.
                        if (image.channelDimension >= 0) {
                            shown.push(image.colorMode === DatasetImage.Rgb
                                ? qsTr("rgb %1·%2·%3").arg(image.redIndex)
                                                      .arg(image.greenIndex)
                                                      .arg(image.blueIndex)
                                : qsTr("channel %1").arg(image.grayIndex))
                        }
                        // A picture that has been turned or magnified is not
                        // the picture the file holds, and a reader coming back
                        // to it has to be told so somewhere.
                        if (imageSurface.turned) {
                            const turn = []
                            if (imageSurface.rotationAngle !== 0)
                                turn.push(imageSurface.rotationAngle + "°")
                            if (imageSurface.flipHorizontal)
                                turn.push(qsTr("flip h"))
                            if (imageSurface.flipVertical)
                                turn.push(qsTr("flip v"))
                            shown.push(turn.join(" "))
                        }
                        if (imageSurface.zoomed)
                            shown.push(qsTr("zoom %1×").arg(imageSurface.zoom.toFixed(1)))
                        return shown
                    }
                }
            }

            Rectangle {
                Layout.fillHeight: true
                Layout.preferredWidth: Theme.borderWidth
                visible: root.railVisible
                color: Theme.borderStrong
            }

            // --- the rail: one panel, whichever was asked for -------------
            // A plain Item rather than a StackLayout: a StackLayout derives its
            // own width from its children and takes the whole tab with it,
            // where the rail has to be exactly as wide as the design says and
            // leave the rest to the data.
            Item {
                Layout.fillHeight: true
                Layout.preferredWidth: root.railWidth
                visible: root.railVisible

                TableSetupPanel {
                    anchors.fill: parent
                    visible: root.rail === "data"
                }

                PostprocessPanel {
                    anchors.fill: parent
                    visible: root.rail === "post"
                }

                TableSettingsPanel {
                    anchors.fill: parent
                    visible: root.rail === "table"
                    target: tableSurface
                }

                PlotSettingsPanel {
                    anchors.fill: parent
                    visible: root.rail === "plot"
                    target: plotSurface
                }

                ImageSettingsPanel {
                    anchors.fill: parent
                    visible: root.rail === "image"
                    target: imageSurface
                }
            }
        }
    }
}
