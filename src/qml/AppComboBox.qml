// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Shapes

/// A dropdown, styled from Theme. Same material as NumberField -- inset well,
/// hairline rim, signal-white rim on focus -- because a choice from a list and
/// a typed number are the same kind of control with different contents.
///
/// The popup is `Popup.Item` for the reason AppMenu gives: a platform popup
/// arrives in the host's palette and typeface, and this application draws
/// every surface from Theme.
///
/// The caret is drawn here rather than set as a character, for the reason
/// AppIcon gives at greater length: the bundled faces are the faces this
/// program sets, and a glyph borrowed from the host is a dependency.
ComboBox {
    id: control

    /// The row the owner says is current. An input, not an output: picking
    /// from the list reports through `activated` and the owner writes the
    /// result back, so the model stays the single authority on what is
    /// selected. The Binding below is what survives the control's own
    /// imperative write, exactly as in AppSlider.
    property int selectedIndex: 0

    Binding {
        target: control
        property: "currentIndex"
        value: control.selectedIndex
        restoreMode: Binding.RestoreBindingOrValue
    }

    font: Theme.monoSmall
    implicitHeight: Theme.smallControlHeight
    leftPadding: Theme.gapS
    rightPadding: Theme.gapL + Theme.gapS
    topPadding: 0
    bottomPadding: 0
    opacity: control.enabled ? 1.0 : 0.4

    background: Rectangle {
        radius: Theme.radiusS
        color: Theme.surfaceInset
        border.width: control.activeFocus ? Theme.borderWidthAccent
                                          : Theme.borderWidth
        border.color: control.activeFocus ? Theme.accent
                    : control.hovered ? Theme.borderStrong : Theme.border
    }

    contentItem: Text {
        id: shown

        text: control.displayText
        font: control.font
        color: Theme.textPrimary
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter

        AppToolTip {
            shown: shown.truncated && control.hovered
            text: control.displayText
        }
    }

    // A Shape rather than a Canvas, for the reason AppIcon gives at greater
    // length: a filled path takes its colour from a binding, where a canvas
    // has to be told to repaint when the theme flips.
    indicator: Shape {
        id: caret

        x: control.width - width - Theme.gapS
        y: control.topPadding + (control.availableHeight - height) / 2
        width: Theme.gapM
        height: Theme.gapS
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            strokeWidth: 0
            fillColor: Theme.textSecondary

            startX: 0
            startY: 0
            PathLine { x: caret.width; y: 0 }
            PathLine { x: caret.width / 2; y: caret.height }
            PathLine { x: 0; y: 0 }
        }
    }

    delegate: ItemDelegate {
        id: option

        required property int index
        required property var modelData

        width: ListView.view.width
        height: Theme.smallControlHeight
        padding: 0
        leftPadding: Theme.gapS
        rightPadding: Theme.gapS
        highlighted: control.highlightedIndex === index

        background: Rectangle {
            color: option.highlighted ? Theme.surfaceHover : "transparent"
        }

        contentItem: Text {
            id: optionLabel

            text: control.textRole === ""
                  ? option.modelData
                  : option.modelData[control.textRole]
            font: control.font
            color: control.currentIndex === option.index ? Theme.textEmphasis
                                                         : Theme.textPrimary
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter

            AppToolTip {
                shown: optionLabel.truncated && option.hovered
                text: optionLabel.text
            }
        }
    }

    popup: Popup {
        y: control.height
        width: control.width
        implicitHeight: Math.min(contentItem.implicitHeight,
                                 Theme.smallControlHeight * 8)
        padding: Theme.borderWidth
        popupType: Popup.Item

        background: Rectangle {
            color: Theme.surface
            border.width: Theme.borderWidth
            border.color: Theme.borderGuide
        }

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.delegateModel
            currentIndex: control.highlightedIndex
            boundsBehavior: Flickable.StopAtBounds

            ScrollBar.vertical: ScrollBar {}
        }
    }
}
