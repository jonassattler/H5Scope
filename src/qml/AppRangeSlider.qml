// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import QtQuick.Controls.Basic

/// A two-handle slider over an inclusive span of integer indices.
///
/// `firstValue` and `secondValue` are inputs, not outputs: dragging reports
/// through `movedTo` and the owner writes the result back, so the model stays
/// the single authority on what is selected and a clamped or swapped span
/// snaps the handles rather than leaving them where the pointer was. The
/// Bindings below are what survive the drag's imperative writes to the nodes.
RangeSlider {
    id: control

    property int firstValue: 0
    property int secondValue: 0

    /// A span the user actually dragged, in that order.
    signal movedTo(int first, int second)

    from: 0
    stepSize: 1
    snapMode: RangeSlider.SnapAlways
    implicitHeight: Theme.gapL
    implicitWidth: Theme.s10
    opacity: control.enabled ? 1.0 : 0.4
    padding: 0

    first.onMoved: control.movedTo(Math.round(control.first.value),
                                   Math.round(control.second.value))
    second.onMoved: control.movedTo(Math.round(control.first.value),
                                    Math.round(control.second.value))

    Binding {
        target: control.first
        property: "value"
        value: control.firstValue
        restoreMode: Binding.RestoreBindingOrValue
    }

    Binding {
        target: control.second
        property: "value"
        value: control.secondValue
        restoreMode: Binding.RestoreBindingOrValue
    }

    background: Rectangle {
        x: control.leftPadding
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: control.availableWidth
        height: Theme.sliderTrackHeight
        radius: Theme.radiusS
        color: Theme.border

        Rectangle {
            x: control.first.visualPosition * parent.width
            width: (control.second.visualPosition - control.first.visualPosition)
                   * parent.width
            height: parent.height
            radius: parent.radius
            color: Theme.textSecondary
        }
    }

    first.handle: Rectangle {
        x: control.leftPadding + control.first.visualPosition
           * (control.availableWidth - width)
        y: control.topPadding + control.availableHeight / 2 - height / 2
        implicitWidth: Theme.sliderHandleSize
        implicitHeight: Theme.sliderHandleSize
        radius: width / 2
        color: control.first.pressed ? Theme.accentHover : Theme.accent
        border.width: Theme.borderWidth
        border.color: Theme.border
    }

    second.handle: Rectangle {
        x: control.leftPadding + control.second.visualPosition
           * (control.availableWidth - width)
        y: control.topPadding + control.availableHeight / 2 - height / 2
        implicitWidth: Theme.sliderHandleSize
        implicitHeight: Theme.sliderHandleSize
        radius: width / 2
        color: control.second.pressed ? Theme.accentHover : Theme.accent
        border.width: Theme.borderWidth
        border.color: Theme.border
    }
}
