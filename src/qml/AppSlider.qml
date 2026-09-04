// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import QtQuick.Controls.Basic

/// A single-handle slider over integer indices.
///
/// `knobValue` is an input, not an output: dragging reports through `movedTo`
/// and the owner writes the result back, so the model stays the single
/// authority on what is selected and a clamped value snaps the handle rather
/// than leaving it where the pointer was. The Binding below is what survives
/// the drag's imperative write.
///
/// The track is a rule, not a trough: this design system draws structure with
/// hairlines and fills nothing it does not have to.
Slider {
    id: control

    property int knobValue: 0

    /// An index the user actually dragged to.
    signal movedTo(int amount)

    from: 0
    stepSize: 1
    snapMode: Slider.SnapAlways
    onMoved: control.movedTo(Math.round(control.value))

    Binding {
        target: control
        property: "value"
        value: control.knobValue
        restoreMode: Binding.RestoreBindingOrValue
    }

    implicitHeight: Theme.gapL
    implicitWidth: Theme.s10
    opacity: control.enabled ? 1.0 : 0.4
    padding: 0

    background: Rectangle {
        x: control.leftPadding
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: control.availableWidth
        height: Theme.sliderTrackHeight
        radius: Theme.radiusS
        color: Theme.border

        Rectangle {
            width: control.visualPosition * parent.width
            height: parent.height
            radius: parent.radius
            color: Theme.textSecondary
        }
    }

    handle: Rectangle {
        x: control.leftPadding + control.visualPosition
           * (control.availableWidth - width)
        y: control.topPadding + control.availableHeight / 2 - height / 2
        implicitWidth: Theme.sliderHandleSize
        implicitHeight: Theme.sliderHandleSize
        radius: width / 2
        color: control.pressed ? Theme.accentHover : Theme.accent
        border.width: Theme.borderWidth
        border.color: Theme.border
    }
}
