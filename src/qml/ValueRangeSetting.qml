// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import QtQuick.Layouts

/// The two halves of a value-to-colour reading, one above the other:
///
///     ╭─────────────────────────────────────╮
///     │▓▓▓│███████████████████████████│▓▓▓▓▓│  which colours
///     ╰─────────────────────────────────────╯
///     VALUE RANGE
///     [    0    ]  [   255   ]                 which values reach them
///
/// They are two questions and they used to be asked as one, which is what was
/// wrong with this control. A single pair of handles running over the *data*
/// answered "which values reach the ends of the ramp" and nothing answered
/// "which part of the ramp am I reading in" at all -- so a reader who wanted
/// the dark half of viridis had no way to say so, and one who wanted to type a
/// range found the boxes and the handles were the same number twice.
///
/// Above: the ramp, with a handle at each end of the stretch being kept. Only
/// the colours between them are painted; the two ends the band has cut off are
/// veiled rather than moved, because the bar is the scale and a scale that
/// redraws itself as you drag it gives you nothing to drag against. Setting a
/// beginning and an end colour is a thing done by eye, and what tells the
/// reader they have found it is the colour.
///
/// Below: the value range, in two boxes. That is a thing done by knowing --
/// 0 to 255, or the extent of the data -- so it is typed, in the notation the
/// data is in: whole numbers for a dataset of integers, and a fractional part
/// only where the values can have one.
///
/// One component rather than one per view. The image and the table put the
/// same question to the reader about the same numbers, and an answer given in
/// one has to look like an answer given in the other -- which is a thing that
/// stays true by there being one control, and stops being true the first time
/// somebody edits a copy.
///
/// Inputs, not outputs, like every other control here: the signals report what
/// the reader did and the owner writes the result back, so a band that gets
/// clamped or turned round snaps the handles rather than leaving them where
/// the pointer was.
Column {
    id: control

    /// The stretch of the ramp in use, 0 to 1 along it.
    property real rampBegin: 0.0
    property real rampEnd: 1.0
    /// The ramp those two run over, as Theme.colorRamps gives it: an empty
    /// list is the black-to-white one. `null` takes the slider away, for a
    /// caller whose picture has no ramp to narrow -- three planes are their
    /// own colour, and there is nothing there to keep a stretch of.
    property var rampStops: []
    property bool rampReversed: false

    /// The values the ramp is spread between.
    property real lower: 0.0
    property real upper: 1.0
    /// Whether those values are whole numbers, which the dataset decides.
    property bool integer: false

    signal rampRequested(real begin, real end)
    signal boundsRequested(real low, real high)

    width: parent ? parent.width : 0
    spacing: Theme.gapXS

    RealRangeSlider {
        width: parent.width
        visible: control.rampStops !== null
        rampStops: control.rampStops
        rampReversed: control.rampReversed
        // The whole ramp, always: these handles say where in it to read, and
        // the ends of it are the only ends there are.
        from: 0.0
        to: 1.0
        firstValue: control.rampBegin
        secondValue: control.rampEnd
        onMovedTo: (begin, end) => control.rampRequested(begin, end)
    }

    Text {
        text: qsTr("value range")
        font: Theme.microLabel
        color: Theme.textDisabled
    }

    RowLayout {
        width: parent.width
        spacing: Theme.gapS

        RealField {
            Layout.fillWidth: true
            integer: control.integer
            value: control.lower
            onCommitted: amount => control.boundsRequested(amount, control.upper)
        }

        RealField {
            Layout.fillWidth: true
            integer: control.integer
            value: control.upper
            onCommitted: amount => control.boundsRequested(control.lower, amount)
        }
    }
}
