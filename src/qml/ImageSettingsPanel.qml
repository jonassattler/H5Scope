// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import H5Scope.Backend

/// How the raster is mapped and drawn. The range and the inversion write to
/// AppController.datasetImage, because they change the pixels; the fit and the
/// interpolation are the view's own and change only how those pixels land on
/// screen.
SettingsPanel {
    id: panel

    /// Which ramp a single-channel picture is drawn on. Read off the image
    /// rather than held here: the table fills its cells from the same ramp,
    /// and a panel that owned the answer would be a second copy of it. See
    /// DatasetImage::rampName.
    readonly property string rampName: panel.image.rampName

    /// The stops that name resolves to. Theme is the only file allowed to hold
    /// a colour, so the lookup happens here and the image is handed the result.
    readonly property var rampStops: {
        const stops = Theme.colorRamps[panel.rampName]
        return stops !== undefined ? stops : []
    }

    /// Hand the name and the stops to the image, which is where a pixel is
    /// actually coloured. An empty list is the black-to-white ramp.
    function setRamp(name) {
        panel.image.rampName = name
        panel.image.ramp = Theme.colorRamps[name] !== undefined
                           ? Theme.colorRamps[name] : []
    }

    /// The ImageSurface these settings apply to.
    property var target
    readonly property var image: AppController.datasetImage

    title: qsTr("image settings")

    /// Which colour modes there are, and what they are called. These are the
    /// whole of DatasetImage::ColorMode; a fourth would be added here and
    /// there. Each needs as many channels as it has planes, and `channelsFor`
    /// below is what the dropdown greys the unreachable ones out by.
    readonly property var colorModes: [
        { label: qsTr("grayscale"), value: DatasetImage.Grayscale, channels: 1 },
        { label: qsTr("RGB"),       value: DatasetImage.Rgb,       channels: 3 },
        { label: qsTr("RGBA"),      value: DatasetImage.Rgba,      channels: 4 }
    ]

    /// The data's own extent, which is what the value range's handles run
    /// between. Guarded on visibility: reading the extremes samples the file,
    /// and this panel exists in the rail whether or not it is showing.
    readonly property real dataLow: panel.visible ? panel.image.minimum : 0
    readonly property real dataHigh: panel.visible ? panel.image.maximum : 1

    readonly property bool hasChannels: panel.image.channelDimension >= 0
    /// Whether the picture is made of one plane per primary rather than of one
    /// channel on a ramp. RGB and RGBA differ only in whether a fourth plane
    /// carries the coverage.
    readonly property bool isColour: panel.image.colorMode === DatasetImage.Rgb
                                     || panel.image.colorMode === DatasetImage.Rgba
    readonly property bool isRgba: panel.image.colorMode === DatasetImage.Rgba
    /// Highest index the sliders may reach. A colour axis of three has indices
    /// 0, 1 and 2; with no axis at all there is nothing to slide.
    readonly property int lastChannel: Math.max(panel.image.channelCount - 1, 0)

    /// Where a mode sits in the dropdown's list.
    function rowForMode(mode) {
        for (let i = 0; i < panel.colorModes.length; ++i) {
            if (panel.colorModes[i].value === mode)
                return i
        }
        return 0
    }

    /// Where a dimension sits in the dropdown's list. The list carries "none"
    /// first and then one entry per dimension, so the two do not line up.
    function rowForDimension(dimension) {
        const choices = panel.image.channelChoices
        for (let i = 0; i < choices.length; ++i) {
            if (choices[i].dimension === dimension)
                return i
        }
        return 0
    }

    // --- what the values are a picture of --------------------------------
    // A raster and a stack of bands are the same numbers; what separates them
    // is a reader saying which dimension holds colour and which of its indices
    // is red. The file answers both when it carries the Image spec's
    // attributes, and these controls are then showing what it said.
    SettingRow {
        label: qsTr("colour channel")

        AppComboBox {
            width: parent.width
            // Rank 2 is exactly the two dimensions the picture is made of.
            // There is no third one left to hold channels, so there is nothing
            // to choose and the mode below is fixed to grayscale.
            enabled: panel.image.channelSelectable
            model: panel.image.channelChoices
            textRole: "label"
            selectedIndex: panel.rowForDimension(panel.image.channelDimension)
            onActivated: (row) => {
                panel.image.channelDimension = panel.image.channelChoices[row].dimension
            }
        }
    }

    SettingRow {
        label: qsTr("colour mode")

        AppComboBox {
            width: parent.width
            // Every mode past grayscale needs somewhere to take its planes
            // from, and a colour axis three deep has no coverage in it.
            enabled: panel.hasChannels && panel.image.channelCount >= 3
            model: panel.colorModes
            textRole: "label"
            selectedIndex: panel.rowForMode(panel.image.colorMode)
            onActivated: (row) => {
                panel.image.colorMode = panel.colorModes[row].value
            }
        }

        // A dataset whose colour axis is three deep can be read as RGB and not
        // as RGBA, and the panel says which rather than letting the reader
        // choose a mode the picture would quietly demote.
        Text {
            width: parent.width
            visible: panel.hasChannels && panel.image.channelCount === 3
            text: qsTr("A colour axis of three has no fourth plane to read as coverage.")
            font: Theme.caption
            color: Theme.textDisabled
            wrapMode: Text.WordWrap
        }
    }

    // One slider in grayscale, three in RGB, each choosing an index along the
    // colour axis. They are absent rather than disabled when there is no such
    // axis: a slider over nothing is a control with no meaning, not one that
    // is temporarily unavailable.
    SettingRow {
        label: qsTr("grayscale index")
        visible: panel.hasChannels && !panel.isColour

        RowLayout {
            width: parent.width
            spacing: Theme.gapS

            NumberField {
                to: panel.lastChannel
                value: panel.image.grayIndex
                onCommitted: amount => panel.image.grayIndex = amount
            }

            AppSlider {
                Layout.fillWidth: true
                to: panel.lastChannel
                knobValue: panel.image.grayIndex
                onMovedTo: amount => panel.image.grayIndex = amount
            }
        }
    }

    Repeater {
        model: [
            { label: qsTr("red index"),   name: "redIndex",   alpha: false },
            { label: qsTr("green index"), name: "greenIndex", alpha: false },
            { label: qsTr("blue index"),  name: "blueIndex",  alpha: false },
            { label: qsTr("alpha index"), name: "alphaIndex", alpha: true }
        ]

        delegate: SettingRow {
            required property var modelData

            label: modelData.label
            visible: panel.hasChannels && panel.isColour
                     && (!modelData.alpha || panel.isRgba)

            RowLayout {
                width: parent.width
                spacing: Theme.gapS

                NumberField {
                    to: panel.lastChannel
                    value: panel.image[modelData.name]
                    onCommitted: amount => panel.image[modelData.name] = amount
                }

                AppSlider {
                    Layout.fillWidth: true
                    to: panel.lastChannel
                    knobValue: panel.image[modelData.name]
                    onMovedTo: amount => panel.image[modelData.name] = amount
                }
            }
        }
    }

    // --- what the one channel is painted in -------------------------------
    // A grayscale picture is one number per pixel put on a ramp, and black to
    // white is only the most obvious ramp to put it on. The others are the
    // plot's, from Theme, so a picture and a plot of the same values cannot
    // disagree about what viridis means.
    //
    // Only for a single channel: three planes is one channel per primary, which
    // is the picture's *own* colour, and a ramp over that would be a reading
    // imposed on a picture that already has one.
    SettingRow {
        label: qsTr("colours")
        visible: !panel.isColour

        AppComboBox {
            width: parent.width
            model: Theme.valueRampLabels
            selectedIndex: {
                const at = Theme.valueRampKeys.indexOf(panel.rampName)
                return at < 0 ? 0 : at
            }
            onActivated: index => panel.setRamp(Theme.valueRampKeys[index])
        }

        AppCheckBox {
            text: qsTr("reverse")
            // For grayscale this is the file's own "white is zero" inverted --
            // the same operation, which is why it is the same flag: reversing a
            // ramp and inverting a gray one are both t -> 1 - t.
            checked: panel.image.invert
            onToggled: panel.image.invert = checked
        }
    }

    // Three planes have no ramp to choose, but they still have a direction.
    SettingRow {
        label: qsTr("mapping")
        visible: panel.isColour

        AppCheckBox {
            text: qsTr("invert")
            checked: panel.image.invert
            onToggled: panel.image.invert = checked
        }
    }

    // Which stretch of the ramp the picture is drawn in, and which values
    // reach it -- the black and the white point.
    //
    // Below the ramp it is set on, because the ramp is what it is set *with*:
    // the bar is the control's scale, and the row above chooses which colours
    // that scale is made of.
    SettingRow {
        label: qsTr("colour range")

        ValueRangeSetting {
            // Three planes are their own colour; there is no ramp under the
            // handles because there is no ramp, and so no stretch of one to
            // keep. The value range still applies -- it is the black and white
            // point of all three at once.
            rampStops: panel.isColour ? null : panel.rampStops
            rampReversed: panel.image.invert
            rampBegin: panel.image.rampBegin
            rampEnd: panel.image.rampEnd
            integer: !AppController.datasetIsFloat
            // The range in force. Until one is typed it is the data's own, and
            // the boxes have to show what is actually being read against.
            lower: panel.image.autoRange
                   ? panel.dataLow
                   : Math.min(panel.image.rangeMinimum, panel.image.rangeMaximum)
            upper: panel.image.autoRange
                   ? panel.dataHigh
                   : Math.max(panel.image.rangeMinimum, panel.image.rangeMaximum)
            onRampRequested: (begin, end) => {
                panel.image.rampBegin = Math.min(begin, end)
                panel.image.rampEnd = Math.max(begin, end)
            }
            onBoundsRequested: (low, high) => {
                panel.image.autoRange = false
                panel.image.rangeMinimum = Math.min(low, high)
                panel.image.rangeMaximum = Math.max(low, high)
            }
        }

        Text {
            id: extremes

            width: parent.width
            // Guarded on visibility: reading the extremes samples the file,
            // and this panel exists in the rail whether or not it is showing.
            text: panel.visible
                  ? qsTr("data %1 … %2").arg(panel.image.minimum.toPrecision(4))
                                        .arg(panel.image.maximum.toPrecision(4))
                  : ""
            font: Theme.readout
            color: Theme.textDisabled
            elide: Text.ElideRight

            HoverHandler { id: extremesHover }

            // Four significant figures fit the rail; the values behind them do
            // not always, and the range is what the reader is setting against.
            AppToolTip {
                shown: extremes.truncated && extremesHover.hovered
                verbatim: true
                text: panel.visible
                      ? qsTr("data %1 … %2").arg(panel.image.minimum)
                                            .arg(panel.image.maximum)
                      : ""
            }
        }
    }

    // --- orientation -----------------------------------------------------
    // Icons rather than words, because this is the one row in the application
    // whose four actions are all shapes -- "rotate 90 left" takes longer to
    // read than the arrow does, and every viewer a reader has used before
    // draws them the same way. What each one is stays a hover away; see
    // AppIconButton.
    SettingRow {
        label: qsTr("orientation")

        Row {
            spacing: Theme.gapXS

            AppIconButton {
                glyph: "rotateLeft"
                hint: qsTr("Rotate 90° left")
                onClicked: { if (panel.target) panel.target.turnBy(-90) }
            }

            AppIconButton {
                glyph: "rotateRight"
                hint: qsTr("Rotate 90° right")
                onClicked: { if (panel.target) panel.target.turnBy(90) }
            }

            AppIconButton {
                glyph: "flipHorizontal"
                hint: qsTr("Flip horizontal")
                active: panel.target ? panel.target.flipHorizontal : false
                onClicked: {
                    if (panel.target)
                        panel.target.flipHorizontal = !panel.target.flipHorizontal
                }
            }

            AppIconButton {
                glyph: "flipVertical"
                hint: qsTr("Flip vertical")
                active: panel.target ? panel.target.flipVertical : false
                onClicked: {
                    if (panel.target)
                        panel.target.flipVertical = !panel.target.flipVertical
                }
            }
        }

        // Any angle, not only the quarter turns: a picture taken off a
        // horizon is straightened by a degree or two, and the buttons cannot
        // say that. Reset is a word rather than a fifth glyph -- it undoes the
        // flips as well as the angle, and no arrow means "all of that".
        RowLayout {
            width: parent.width
            spacing: Theme.gapS

            RealField {
                Layout.fillWidth: true
                value: panel.target ? panel.target.rotationAngle : 0
                onCommitted: amount => {
                    if (panel.target)
                        panel.target.rotationAngle = ((amount % 360) + 360) % 360
                }
            }

            AppToolButton {
                text: qsTr("reset")
                size: "md"
                enabled: panel.target ? panel.target.turned : false
                onClicked: { if (panel.target) panel.target.resetOrientation() }
            }
        }
    }

    SettingRow {
        label: qsTr("view")

        // The wheel zooms about the pointer and a drag pans, once there is
        // more picture than frame; this is the way back for a reader who
        // would rather press something than know that a double-click does it.
        RowLayout {
            width: parent.width
            spacing: Theme.gapS

            Text {
                Layout.fillWidth: true
                text: panel.visible && panel.target
                      ? qsTr("%1×").arg(panel.target.zoom.toFixed(1)) : ""
                font: Theme.readout
                color: Theme.textDisabled
                verticalAlignment: Text.AlignVCenter
            }

            AppToolButton {
                text: qsTr("reset zoom")
                size: "md"
                enabled: panel.target ? panel.target.zoomed : false
                onClicked: { if (panel.target) panel.target.resetView() }
            }
        }
    }

    // --- what the picture stands on --------------------------------------
    // A raster is judged against its ground, and with a coverage channel the
    // ground is half of what the reader is looking at: which pixels are absent
    // can only be seen through it.
    SettingRow {
        label: qsTr("background")

        RowLayout {
            width: parent.width
            spacing: Theme.gapS

            ColorSwatchButton {
                label: qsTr("colour")
                enabled: panel.target ? !panel.target.checkerboard : false
                value: panel.target ? panel.target.ground : Theme.imageGround
                onPicked: chosen => {
                    if (!panel.target)
                        return
                    panel.target.backgroundColor = chosen
                    // Choosing one is what pins it. Until then the ground
                    // follows the theme, which is the eye the reader is
                    // already adapted to; after it, it is a statement about
                    // this dataset and the theme stops overruling it.
                    panel.target.backgroundCustom = true
                }
            }

            Item { Layout.fillWidth: true }
        }

        AppCheckBox {
            text: qsTr("checkerboard")
            // Overrides the colour rather than mixing with it: the pattern is
            // there to be unmistakably not a value, and a tinted one would
            // start looking like one.
            checked: panel.target ? panel.target.checkerboard : false
            onToggled: { if (panel.target) panel.target.checkerboard = checked }
        }

        AppToolButton {
            width: parent.width
            text: qsTr("follow the theme")
            size: "sm"
            enabled: panel.target ? panel.target.backgroundCustom : false
            onClicked: { if (panel.target) panel.target.backgroundCustom = false }
        }
    }

    SettingRow {
        label: qsTr("drawing")

        AppCheckBox {
            text: qsTr("stretch to fit")
            checked: panel.target ? panel.target.stretch : false
            onToggled: { if (panel.target) panel.target.stretch = checked }
        }

        AppCheckBox {
            text: qsTr("interpolate")
            checked: panel.target ? panel.target.interpolate : false
            onToggled: { if (panel.target) panel.target.interpolate = checked }
        }
    }
}
