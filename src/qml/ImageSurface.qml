// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import H5Scope.Backend

/// The Data Viewer's image presentation: the table as a raster, one pixel per
/// cell, under a view the reader can move and turn.
///
/// The pixels come from DatasetImageProvider rather than from a property,
/// because an image is not a value to bind. `revision` is: it changes whenever
/// the raster would, and carrying it in the URL is what makes Qt ask the
/// provider again instead of serving the cached pixmap.
///
/// `asynchronous: false` is load-bearing. A provider is otherwise served on a
/// loader thread, and the HDF5 reads behind this one are not thread-safe.
///
/// Zoom, pan, rotation and flip are all view state and none of them touches
/// the raster: the provider hands over the same pixels whatever is done here,
/// and what changes is the transform they are drawn under. That is what keeps
/// a turn or a magnification free of a re-read, and it is why rotating by an
/// arbitrary angle costs no more than rotating by ninety degrees.
Item {
    id: surface

    // --- settings, written by ImageSettingsPanel ------------------------
    /// Fill the view, or keep the data's own aspect ratio.
    property bool stretch: false
    /// Interpolate between cells when magnified. Off shows each cell as a
    /// square of one value, which is what it is.
    property bool interpolate: false

    // --- what the picture stands on --------------------------------------
    // A raster is judged against its ground, and with an alpha channel the
    // ground is part of what the reader is looking at: which pixels are absent
    // can only be seen through it.
    //
    // The default follows the theme -- black in the dark one, white in the
    // light -- and keeps following it until the reader picks a colour for this
    // particular dataset. That is what `backgroundCustom` is for: a chosen
    // ground is a statement about this data, and it must not be undone by
    // flipping the theme, while a ground nobody has chosen should follow the
    // eye that is adapted to it.
    property bool backgroundCustom: false
    property color backgroundColor: Theme.imageGround
    /// Draw the checkerboard instead, as every image editor does. It overrides
    /// the colour: neither of its two greys is a colour a channel could have
    /// produced, so what shows through it is unmistakably nothing rather than
    /// a dark or a pale pixel.
    property bool checkerboard: false

    /// The ground actually drawn, when it is a colour at all.
    readonly property color ground:
        surface.backgroundCustom ? surface.backgroundColor : Theme.imageGround

    // --- orientation ----------------------------------------------------
    /// Degrees clockwise. The buttons step it by ninety; the box takes any
    /// angle, which is what an image straightened against a horizon needs.
    property real rotationAngle: 0.0
    property bool flipHorizontal: false
    property bool flipVertical: false

    // --- the view on the raster -----------------------------------------
    /// 1 is the whole picture fitted to the frame, which is also the floor:
    /// there is nothing outside the raster to look at, and it makes the pan
    /// clamp below exact rather than arbitrary.
    property real zoom: 1.0
    property real panX: 0.0
    property real panY: 0.0

    /// Ceiling on magnification. The raster is sampled at no more than
    /// 1024 x 1024, so past this a cell is already a slab of screen; reading
    /// more of the file than that is the data settings panel's job.
    readonly property real maxZoom: 64.0

    readonly property bool zoomed: zoom !== 1.0 || panX !== 0.0 || panY !== 0.0
    readonly property bool turned: rotationAngle !== 0.0
                                   || flipHorizontal || flipVertical

    /// Whether this is the presentation on screen. Reading `image.hasData`
    /// samples the file, so every path into the raster is guarded by this: a
    /// reader browsing a large dataset as a table must not pay for a picture
    /// of it that nobody asked to see.
    property bool active: true

    readonly property var image: AppController.datasetImage
    readonly property bool drawable: active && AppController.datasetIsNumeric
                                     && image.hasData

    // --- how big the picture lands --------------------------------------
    // A rotated rectangle occupies the bounding box
    //     |w cos a| + |h sin a|  by  |w sin a| + |h cos a|
    // and that box, not the raster, is what has to fit the frame -- otherwise
    // turning a wide image ninety degrees would push it out of the top and
    // bottom of the view.
    readonly property real sourceWidth: drawable ? Math.max(image.width, 1) : 1
    readonly property real sourceHeight: drawable ? Math.max(image.height, 1) : 1
    readonly property real radians: rotationAngle * Math.PI / 180.0
    readonly property real boxWidth:
        Math.abs(sourceWidth * Math.cos(radians))
        + Math.abs(sourceHeight * Math.sin(radians))
    readonly property real boxHeight:
        Math.abs(sourceWidth * Math.sin(radians))
        + Math.abs(sourceHeight * Math.cos(radians))

    readonly property real fitX: viewport.width / Math.max(boxWidth, 1)
    readonly property real fitY: viewport.height / Math.max(boxHeight, 1)
    readonly property real fit: Math.min(fitX, fitY)
    // Stretching is a deliberate break of the aspect ratio, so the two axes
    // part company; every other mode keeps them equal.
    readonly property real scaleX: (stretch ? fitX : fit) * zoom
    readonly property real scaleY: (stretch ? fitY : fit) * zoom

    /// The bounding box as drawn, which is what the pan is clamped against.
    readonly property real drawnWidth: boxWidth * scaleX
    readonly property real drawnHeight: boxHeight * scaleY

    /// Screen pixels per cell of the table, for the footer to report.
    readonly property real magnification: Math.min(scaleX, scaleY)

    function resetView() {
        zoom = 1.0
        panX = 0.0
        panY = 0.0
    }

    function resetOrientation() {
        rotationAngle = 0.0
        flipHorizontal = false
        flipVertical = false
    }

    /// Turn by ninety degrees, kept in [0, 360) so the box below reads back a
    /// number a person would have typed.
    function turnBy(degrees) {
        surface.rotationAngle = ((surface.rotationAngle + degrees) % 360 + 360) % 360
        surface.clampPan()
    }

    /// Nothing may be dragged so far that the picture leaves the frame. When
    /// it is smaller than the frame there is nowhere to drag it at all, and it
    /// stays centred.
    function clampPan() {
        const roomX = Math.max(0, (surface.drawnWidth - viewport.width) / 2)
        const roomY = Math.max(0, (surface.drawnHeight - viewport.height) / 2)
        surface.panX = Math.max(-roomX, Math.min(roomX, surface.panX))
        surface.panY = Math.max(-roomY, Math.min(roomY, surface.panY))
    }

    /// Magnify about a point, holding whatever is under it still. That is what
    /// separates a zoom a reader can aim from one that always pulls towards
    /// the middle of the frame.
    function zoomAt(px, py, factor) {
        const next = Math.max(1.0, Math.min(surface.maxZoom, surface.zoom * factor))
        const step = next / surface.zoom
        const centreX = viewport.width / 2 + surface.panX
        const centreY = viewport.height / 2 + surface.panY
        surface.zoom = next
        surface.panX = px + step * (centreX - px) - viewport.width / 2
        surface.panY = py + step * (centreY - py) - viewport.height / 2
        surface.clampPan()
    }

    function panBy(dx, dy) {
        surface.panX += dx
        surface.panY += dy
        surface.clampPan()
    }

    // The one colour this view does not draw from a value. Set here rather
    // than in C++ so Theme stays the only place a colour is named.
    Binding {
        target: AppController.datasetImage
        property: "missingColor"
        value: Theme.warning
    }

    Rectangle {
        anchors.fill: parent
        color: surface.checkerboard ? Theme.checkerLight : surface.ground
    }

    // The checkerboard, drawn once per resize rather than assembled out of a
    // few thousand Rectangles. A Canvas is the cheapest thing in Qt Quick that
    // can fill a pattern across an arbitrary area, and this one repaints only
    // when the frame changes size, when it is switched on, or when the theme
    // moves under it.
    Canvas {
        id: checker

        anchors.fill: parent
        visible: surface.checkerboard

        onPaint: {
            const ctx = getContext("2d")
            const step = Theme.checkerSize
            ctx.fillStyle = Theme.checkerLight
            ctx.fillRect(0, 0, width, height)
            ctx.fillStyle = Theme.checkerDark
            for (let row = 0; row * step < height; ++row) {
                for (let column = row % 2; column * step < width; column += 2)
                    ctx.fillRect(column * step, row * step, step, step)
            }
        }

        onWidthChanged: checker.requestPaint()
        onHeightChanged: checker.requestPaint()
        onVisibleChanged: if (visible) checker.requestPaint()

        Connections {
            target: Theme
            function onDarkChanged() { checker.requestPaint() }
        }
    }

    Item {
        id: viewport

        anchors.fill: parent
        anchors.margins: Theme.gapM
        clip: true

        Image {
            id: raster

            width: surface.sourceWidth * surface.scaleX
            height: surface.sourceHeight * surface.scaleY
            x: (viewport.width - width) / 2 + surface.panX
            y: (viewport.height - height) / 2 + surface.panY
            visible: surface.drawable
            // Unset when there is nothing to draw: an Image with a source still
            // asks the provider, and a provider with no raster to give answers
            // with a warning rather than with a picture.
            source: surface.drawable
                    ? "image://dataset/" + surface.image.revision : ""
            // Sampled once per revision at a fixed cap, so resizing the window
            // scales what is already in memory and never re-reads the file.
            asynchronous: false
            cache: false
            smooth: surface.interpolate
            mipmap: false
            fillMode: Image.Stretch

            // Rotation first, mirror second, so a flip is always the flip the
            // reader sees rather than one in the raster's own axes -- on a
            // picture already turned ninety degrees those are not the same
            // thing, and the button says "horizontal".
            transform: [
                Rotation {
                    origin.x: raster.width / 2
                    origin.y: raster.height / 2
                    angle: surface.rotationAngle
                },
                Scale {
                    origin.x: raster.width / 2
                    origin.y: raster.height / 2
                    xScale: surface.flipHorizontal ? -1 : 1
                    yScale: surface.flipVertical ? -1 : 1
                }
            ]
        }

        // --- zoom and pan -----------------------------------------------
        Item {
            anchors.fill: parent
            enabled: surface.drawable

            WheelHandler {
                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                onWheel: (event) => {
                    surface.zoomAt(event.x, event.y,
                                   Math.pow(1.25, event.angleDelta.y / 120))
                }
            }

            DragHandler {
                id: panner

                target: null
                enabled: surface.drawnWidth > viewport.width
                         || surface.drawnHeight > viewport.height
                cursorShape: active ? Qt.ClosedHandCursor : Qt.OpenHandCursor

                /// The translation already applied. DragHandler reports the
                /// whole movement since the press, and the view wants the step.
                property point applied: Qt.point(0, 0)

                onActiveChanged: applied = Qt.point(0, 0)
                onActiveTranslationChanged: {
                    surface.panBy(activeTranslation.x - applied.x,
                                  activeTranslation.y - applied.y)
                    applied = activeTranslation
                }
            }

            TapHandler {
                onDoubleTapped: surface.resetView()
            }
        }
    }

    // A window onto one dataset says nothing about the next one, and the turn
    // goes with it: an angle chosen to straighten one picture is not an angle
    // for another. Both are in the list below, so a fresh dataset puts them
    // back to nothing and one the reader has been at before puts back what
    // they left. See DatasetMemory.
    DatasetMemory {
        subject: surface
        group: "imageView"
        names: ["stretch", "interpolate", "rotationAngle", "flipHorizontal",
                "flipVertical", "zoom", "panX", "panY",
                "backgroundCustom", "backgroundColor", "checkerboard"]
    }

    // ...and the picture's own reading of the values, which lives on the
    // object that does the reading.
    DatasetMemory {
        subject: AppController.datasetImage
        group: "image"
        // The image works out its own defaults from what the file says about
        // itself -- a declared colour axis, a stated range -- so a dataset with
        // nothing remembered is left exactly as those defaults left it.
        restoresDefaults: false
        names: ["invert", "rampName", "rampBegin", "rampEnd", "autoRange",
                "rangeMinimum", "rangeMaximum", "colorMode", "channelDimension",
                "grayIndex", "redIndex", "greenIndex", "blueIndex", "alphaIndex"]
    }

    // Resizing the frame changes what there is to pan within, and a picture
    // that was dragged to its edge would otherwise be left hanging outside it.
    onWidthChanged: surface.clampPan()
    onHeightChanged: surface.clampPan()

    ViewMessage {
        anchors.fill: parent
        visible: !surface.drawable
        title: qsTr("nothing to show")
        warning: surface.active && surface.image.error !== ""
        text: {
            // Reading `error` samples, so nothing is asked of a raster that is
            // not the presentation on screen.
            if (!surface.active)
                return ""
            if (surface.image.error !== "")
                return surface.image.error
            if (!AppController.datasetTabVisible)
                return qsTr("Select a dataset in the tree to see its values as an image.")
            if (!AppController.datasetIsNumeric)
                return qsTr("%1 holds no numbers. Only a numeric dataset can be drawn as an image.")
                       .arg(AppController.currentPath)
            return qsTr("The selected slice has no finite values in it.")
        }
    }
}
