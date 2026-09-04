// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import QtQuick.Layouts
import H5Scope.Backend

/// How the grid is drawn. Not what it shows -- that is the data settings
/// panel, which is the same rail and a different question.
SettingsPanel {
    id: panel

    /// The TableSurface these settings apply to.
    property var target

    /// The model is where a cell is actually made, so the notation belongs to
    /// it rather than to the surface drawing it.
    readonly property var model: AppController.datasetModel

    title: qsTr("table settings")

    /// Held rather than written inline: a model expression that builds a new
    /// array every time it runs makes the ComboBox reset its index.
    readonly property var floatFormats: [qsTr("shortest"), qsTr("fixed"),
                                         qsTr("scientific")]

    /// Where the fill's ramp, direction and band actually live. The picture's
    /// object, because a cell filled by its value is the picture put back over
    /// the numbers -- one question about one dataset, and two panels that must
    /// not be able to answer it differently. See DatasetImage::rampName.
    readonly property var image: AppController.datasetImage

    /// The stops the ramp's name resolves to, for the bar the band is set on.
    readonly property var rampStops: {
        const stops = Theme.colorRamps[panel.image.rampName]
        return stops !== undefined ? stops : []
    }

    SettingRow {
        label: qsTr("column width")

        AppCheckBox {
            text: qsTr("fit to contents")
            checked: panel.target ? panel.target.autoWidth : true
            onToggled: { if (panel.target) panel.target.autoWidth = checked }
        }

        AppSlider {
            width: parent.width
            from: Theme.s11
            to: Theme.s13 * 2
            // A fixed width is what the reader falls back to, so reaching for
            // it is how they say they want one: moving the slider turns the
            // fitting off rather than moving a control that does nothing.
            knobValue: panel.target ? panel.target.columnWidth : Theme.s11
            onMovedTo: amount => {
                if (!panel.target)
                    return
                panel.target.autoWidth = false
                panel.target.columnWidth = amount
            }
        }
    }

    // Only floats have a notation to choose. An integer is written one way and
    // a string is not a number at all, so for those this row is not disabled --
    // it is not there, because there is nothing it could do.
    SettingRow {
        label: qsTr("float format")
        visible: AppController.datasetIsFloat

        AppComboBox {
            width: parent.width
            model: panel.floatFormats
            selectedIndex: panel.model ? panel.model.floatFormat : 0
            onActivated: index => { if (panel.model) panel.model.floatFormat = index }
        }

        RowLayout {
            width: parent.width
            spacing: Theme.gapS
            // Shortest is the exact one: it prints the fewest digits that read
            // back as the same double, and a digit count is not a thing that
            // can be asked of it.
            enabled: panel.model ? panel.model.floatFormat !== 0 : false

            Text {
                Layout.fillWidth: true
                text: qsTr("decimals")
                font: Theme.microLabel
                color: Theme.textDisabled
                verticalAlignment: Text.AlignVCenter
            }

            NumberField {
                from: 0
                to: 17
                value: panel.model ? panel.model.floatDecimals : 6
                onCommitted: amount => {
                    if (panel.model) panel.model.floatDecimals = amount
                }
            }
        }

        Text {
            width: parent.width
            text: qsTr("Hover a cell for the value in full.")
            font: Theme.caption
            color: Theme.textDisabled
            wrapMode: Text.WordWrap
        }
    }

    SettingRow {
        label: qsTr("rows")

        AppCheckBox {
            text: qsTr("striped")
            // Two textures over one grid is one too many: a stripe under a
            // fill is either invisible or a second thing the eye has to
            // discount. The box says so by going unavailable rather than by
            // staying on and doing nothing.
            enabled: panel.target ? !panel.target.colorCells : true
            checked: panel.target ? panel.target.striped
                                    && !panel.target.colorCells : false
            onToggled: { if (panel.target) panel.target.striped = checked }
        }

        AppCheckBox {
            text: qsTr("grid lines")
            checked: panel.target ? panel.target.gridLines : false
            onToggled: { if (panel.target) panel.target.gridLines = checked }
        }
    }

    // --- the cells, filled by what is in them -----------------------------
    // The image's reading of a table put back over the table itself, and the
    // image settings panel's controls with it: the same ramps under the same
    // names, the same reverse, the same auto-or-manual band. A grid of numbers
    // is worst at exactly one question -- where in this dataset is it large --
    // and a fill answers it without the reader having to leave the numbers.
    //
    // Only for numbers. A string or a struct has no value to place on a ramp,
    // so the row is absent rather than disabled: there is nothing it could do.
    SettingRow {
        label: qsTr("cell colours")
        visible: AppController.datasetIsNumeric

        AppCheckBox {
            text: qsTr("fill from value")
            checked: panel.target ? panel.target.colorCells : false
            onToggled: { if (panel.target) panel.target.colorCells = checked }
        }

        AppComboBox {
            width: parent.width
            enabled: panel.target ? panel.target.colorCells : false
            model: Theme.valueRampLabels
            selectedIndex: {
                const at = Theme.valueRampKeys.indexOf(panel.image.rampName)
                return at < 0 ? 0 : at
            }
            onActivated: index => {
                const name = Theme.valueRampKeys[index]
                panel.image.rampName = name
                // The stops as well as the name: the picture colours its
                // pixels from the stops, and Theme is the only file allowed to
                // hold a colour, so the lookup happens on this side.
                panel.image.ramp = Theme.colorRamps[name] !== undefined
                                   ? Theme.colorRamps[name] : []
            }
        }

        AppCheckBox {
            text: qsTr("reverse")
            // Which end of a ramp is the dark one is a property of the ramp
            // and not of the data, and the reader is the one who knows which
            // way round they want to read it. The same flag the picture calls
            // `invert`, because reversing a ramp and inverting a gray one are
            // both t -> 1 - t.
            enabled: panel.target ? panel.target.colorCells : false
            checked: panel.image.invert
            onToggled: panel.image.invert = checked
        }
    }

    // Which colours the fill is made of, and which values reach them. Below
    // the ramp it is read on, because the row above chooses which ramp.
    SettingRow {
        label: qsTr("colour range")
        visible: AppController.datasetIsNumeric
        enabled: panel.target ? panel.target.colorCells : false

        ValueRangeSetting {
            rampStops: panel.rampStops
            rampReversed: panel.image.invert
            rampBegin: panel.image.rampBegin
            rampEnd: panel.image.rampEnd
            // Whole numbers for a dataset of them: a value range of 0.5 is not
            // a range a grid of integers can be read against.
            integer: !AppController.datasetIsFloat
            lower: panel.target ? panel.target.colorLow : 0
            upper: panel.target ? panel.target.colorHigh : 1
            onRampRequested: (begin, end) => {
                panel.image.rampBegin = Math.min(begin, end)
                panel.image.rampEnd = Math.max(begin, end)
            }
            onBoundsRequested: (low, high) => {
                // Typing a range pins it. Until then it is the data's own,
                // which is what a float array with nothing to say about itself
                // is read against -- and what the boxes have been showing.
                panel.image.autoRange = false
                panel.image.rangeMinimum = Math.min(low, high)
                panel.image.rangeMaximum = Math.max(low, high)
            }
        }

        Text {
            id: extremes

            width: parent.width
            text: panel.target && panel.target.colorCells
                  ? qsTr("data %1 … %2").arg(panel.target.dataLow.toPrecision(4))
                                        .arg(panel.target.dataHigh.toPrecision(4))
                  : ""
            font: Theme.readout
            color: Theme.textDisabled
            elide: Text.ElideRight

            HoverHandler { id: extremesHover }

            // Four significant figures fit the rail; the values behind them do
            // not always, and the band is what the reader is setting against.
            AppToolTip {
                shown: extremes.truncated && extremesHover.hovered
                verbatim: true
                text: panel.target && panel.target.colorCells
                      ? qsTr("data %1 … %2").arg(panel.target.dataLow)
                                            .arg(panel.target.dataHigh)
                      : ""
            }
        }
    }
}
