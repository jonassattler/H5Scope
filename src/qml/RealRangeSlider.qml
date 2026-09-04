// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import QtQuick.Controls.Basic

/// A two-handle slider over a span of real values.
///
/// AppRangeSlider's sibling rather than a mode of it, for the reason RealField
/// gives about NumberField: a table index and a data value are different
/// things, and a control that snapped a measurement to whole numbers would be
/// wrong about it. Everything else -- the inputs-not-outputs rule, the Bindings
/// that survive the drag's imperative writes -- is that file's.
///
/// Give it `rampStops` and the track becomes the colour ramp the band is being
/// set on, with the handles standing on it. That is the whole of the value
/// range control: a reader setting a black point is looking for where black
/// should fall, and the answer is a colour, not a number -- so the colours are
/// what the handles move over. The bar is drawn in full at every position and
/// never rescales; what the band changes is which values reach it, so the ends
/// it has cut off are veiled rather than redrawn. See Theme.rampVeil.
RangeSlider {
    id: control

    property real firstValue: 0.0
    property real secondValue: 1.0

    /// The ramp under the handles, as Theme.colorRamps gives it. `null` -- the
    /// default -- is the plain hairline track; an *empty* list is the
    /// black-to-white ramp, the same convention DatasetImage.ramp uses.
    property var rampStops: null
    property bool rampReversed: false

    readonly property bool onRamp: control.rampStops !== null
    /// How many cells the ramp is drawn in. Enough that the steps read as a
    /// gradient at rail width, few enough to stay a handful of rectangles.
    readonly property int rampCells: 48

    /// A span the user actually dragged, in that order.
    signal movedTo(real first, real second)

    /// `value` clamped to the span the slider currently runs over.
    ///
    /// The clamping is RangeSlider's own -- a node cannot hold a value outside
    /// [from, to] -- and doing it here as well would be pointless if it were
    /// only about the arithmetic. It is about the *dependency*: a binding that
    /// reads `from` and `to` re-runs when they change, and one that reads only
    /// the wanted value does not. Without that, a slider told to sit at 60
    /// while it still spanned 0..1 clamps to 1 and stays there when the extent
    /// it was waiting for arrives -- which is exactly what a table's colour
    /// band does, because the extent costs a read of the file and is not taken
    /// until the reader asks for the fill.
    function inRange(value) {
        return Math.max(control.from, Math.min(control.to, value))
    }

    implicitHeight: control.onRamp
                    ? Theme.rampBarHeight + Theme.rampHandleOverhang * 2
                    : Theme.gapL
    implicitWidth: Theme.s10
    opacity: control.enabled ? 1.0 : 0.4
    padding: 0

    first.onMoved: control.movedTo(control.first.value, control.second.value)
    second.onMoved: control.movedTo(control.first.value, control.second.value)

    Binding {
        target: control.first
        property: "value"
        value: control.inRange(control.firstValue)
        restoreMode: Binding.RestoreBindingOrValue
    }

    Binding {
        target: control.second
        property: "value"
        value: control.inRange(control.secondValue)
        restoreMode: Binding.RestoreBindingOrValue
    }

    background: Item {
        x: control.leftPadding
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: control.availableWidth
        height: control.onRamp ? Theme.rampBarHeight : Theme.sliderTrackHeight

        // The plain track: a hairline pair, with the band between the handles
        // drawn over it. What every other slider in the application looks like.
        Rectangle {
            anchors.fill: parent
            visible: !control.onRamp
            radius: Theme.radiusS
            color: Theme.border

            Rectangle {
                x: control.first.visualPosition * parent.width
                width: (control.second.visualPosition
                        - control.first.visualPosition) * parent.width
                height: parent.height
                radius: parent.radius
                color: Theme.textSecondary
            }
        }

        // The ramp as the track. Drawn in cells rather than as a Gradient
        // because the stops arrive as a list whose length is not known here,
        // and Gradient takes declared children; Theme.rampColor is the same
        // interpolation the image and the plot use, so all three agree.
        Item {
            anchors.fill: parent
            visible: control.onRamp
            clip: true

            Row {
                anchors.fill: parent
                spacing: 0

                Repeater {
                    model: control.onRamp ? control.rampCells : 0

                    delegate: Rectangle {
                        required property int index

                        width: parent.width / control.rampCells
                        height: parent.height
                        color: {
                            const last = control.rampCells - 1
                            const at = control.rampReversed
                                       ? 1 - index / last : index / last
                            const stops = control.rampStops
                            return stops && stops.length > 0
                                   ? Theme.rampColor(stops, at)
                                   : Qt.rgba(at, at, at, 1)
                        }
                    }
                }
            }

            // The two ends the band has cut off. Every value below the first
            // handle is drawn in the ramp's first colour and every value above
            // the second in its last, so those stretches of the bar are no
            // longer a scale -- the veil says so without moving them.
            Rectangle {
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                width: control.first.visualPosition * parent.width
                color: Theme.rampVeil
            }

            Rectangle {
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.right: parent.right
                width: (1 - control.second.visualPosition) * parent.width
                color: Theme.rampVeil
            }

            Rectangle {
                anchors.fill: parent
                color: "transparent"
                border.width: Theme.borderWidth
                border.color: Theme.border
            }
        }
    }

    // A handle is a rule when it stands on the ramp and a knob when it stands
    // on the hairline: on the bar it is marking an edge, and the system draws
    // an edge with a rule. It overhangs the bar top and bottom so there is
    // something to take hold of that is not over the colour being read.
    first.handle: Rectangle {
        x: control.leftPadding + control.first.visualPosition
           * (control.availableWidth - width)
        y: control.topPadding + control.availableHeight / 2 - height / 2
        implicitWidth: control.onRamp ? Theme.rampHandleWidth
                                      : Theme.sliderHandleSize
        implicitHeight: control.onRamp
                        ? Theme.rampBarHeight + Theme.rampHandleOverhang * 2
                        : Theme.sliderHandleSize
        radius: control.onRamp ? Theme.radiusS : width / 2
        color: control.first.pressed ? Theme.accentHover : Theme.accent
        border.width: Theme.borderWidth
        border.color: Theme.border
    }

    second.handle: Rectangle {
        x: control.leftPadding + control.second.visualPosition
           * (control.availableWidth - width)
        y: control.topPadding + control.availableHeight / 2 - height / 2
        implicitWidth: control.onRamp ? Theme.rampHandleWidth
                                      : Theme.sliderHandleSize
        implicitHeight: control.onRamp
                        ? Theme.rampBarHeight + Theme.rampHandleOverhang * 2
                        : Theme.sliderHandleSize
        radius: control.onRamp ? Theme.radiusS : width / 2
        color: control.second.pressed ? Theme.accentHover : Theme.accent
        border.width: Theme.borderWidth
        border.color: Theme.border
    }
}
