// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import QtQuick.Controls.Basic

/// Push button styled entirely from Theme; the Basic style contributes no
/// visuals of its own, which is exactly why it was chosen as the base.
///
/// This is the design system's Button, ported. Five variants:
///
///   primary    a filled slab in the signal colour. One per view.
///   secondary  a hairline outline on the bare surface. The default.
///   ghost      no chrome at all until hovered.
///   caution    filled amber. Something the reader may not want.
///   danger     filled red. Something they cannot undo.
///
/// A filled variant's ink is not named per variant: it is whichever of the
/// system's two inks reads on the ground, which is what Theme.inkOn decides.
///
/// The last two exist so that colour, when it is spent on a control, is spent
/// on what the control does rather than on making it noticeable. There is no
/// third state between them and `secondary`: a button that merely matters is
/// still an outline.
///
/// Three sizes, 22/26/32, matching the system's `--ctl-h-sm/md/lg`. The label
/// is the reading face at semibold, not the mono uppercase machine label --
/// see the note on Theme.button for why an instruction is not a name.
Button {
    id: control

    /// "primary" | "secondary" | "ghost" | "caution" | "danger"
    property string variant: "secondary"
    /// "sm" | "md" | "lg"
    property string size: "md"

    /// What pressing this does, beyond acting: `"dialog"` puts something modal
    /// in front of the reader, `"panel"` reveals or hides one. Empty -- the
    /// default -- means it simply happens, here, now.
    ///
    /// A button that opens something and one that does something look
    /// identical everywhere in this application, and the two are not the same
    /// promise. The ellipsis is the desktop convention for the first; the
    /// caret is the same idea for a panel, and it points where the panel is
    /// about to go.
    property string opens: ""
    /// Whether the panel this button reveals is currently showing. Only read
    /// when `opens` is "panel", where it turns the caret round.
    property bool open: false
    readonly property bool filled: variant === "primary" || variant === "caution"
                                   || variant === "danger"
    readonly property bool ghost: variant === "ghost"
    readonly property bool small: size === "sm"

    /// The mark after the label, if any. A panel's caret follows the panel:
    /// down while it is up and to be dismissed, up while it is waiting.
    readonly property string affordance: {
        if (control.opens === "dialog")
            return "…"
        if (control.opens === "panel")
            return control.open ? "▾" : "▸"
        return ""
    }

    implicitHeight: size === "sm" ? Theme.tinyControlHeight
                  : size === "lg" ? Theme.controlHeight
                                  : Theme.smallControlHeight
    // Measured rather than taken off the content item. The label has to elide
    // when a caller gives the button a width, which means its width depends on
    // the button's -- so deriving the button's width from the label's would
    // close a loop, and a loop here resolves to a button with no text in it at
    // all. TextMetrics answers the same question without being in the layout.
    // leftPadding + rightPadding, not padding * 2: the Basic style derives
    // `horizontalPadding` from `padding` rather than taking it, so the two are
    // not the same number and assuming they were left every label four pixels
    // short of itself -- which is an ellipsis on every button in the window.
    implicitWidth: control.labelWidth + control.markWidth
                   + control.leftPadding + control.rightPadding

    // Rounded up and then given a pixel: an advance width is fractional, and a
    // Text handed exactly its own advance elides the moment the rounding goes
    // the wrong way -- which costs a whole character and an ellipsis on top of
    // it. The same reasoning as the tree's readout, at the other end of the UI.
    readonly property real labelWidth: control.text === ""
                                       ? 0
                                       : Math.ceil(labelMetrics.advanceWidth) + Theme.s1
    readonly property real markWidth: control.affordance === ""
                                      ? 0
                                      : Math.ceil(markMetrics.advanceWidth) + Theme.s1
                                        + Theme.gapS

    TextMetrics {
        id: labelMetrics

        font: control.font
        text: control.text
    }

    TextMetrics {
        id: markMetrics

        font: control.font
        text: control.affordance
    }
    // 8 / 11 / 16 in the system; the grid carries 8, 12 and 18, and the middle
    // step is the nearest to the one the design system names.
    padding: size === "sm" ? Theme.gapM : size === "lg" ? Theme.gapXL : Theme.gapL
    font: size === "lg" ? Theme.button : Theme.buttonSmall
    hoverEnabled: true

    // Press translates one pixel down. The system permits no scale on press.
    transform: Translate { y: control.down ? 1 : 0 }

    // Disabled is opacity, not a separate palette, so it stays legible against
    // any surface the button is placed on.
    opacity: control.enabled ? 1.0 : 0.4

    readonly property color ground: {
        switch (control.variant) {
        case "primary": return Theme.accent
        case "caution": return Theme.warning
        case "danger":  return Theme.danger
        default:        return "transparent"
        }
    }

    /// A filled button's label is read against its own ground, so which of
    /// the system's two inks it takes is a question about that ground's
    /// luminance and nothing else -- which is exactly what Theme.inkOn
    /// answers. It had been fixed per variant, which was right in the dark
    /// theme by luck and wrong in the light one: `primary` inverts to a black
    /// slab there, and a black slab lettered in near-black is a button with no
    /// text on it.
    readonly property color ink: control.filled ? Theme.inkOn(control.ground)
                                                : Theme.textPrimary

    background: Rectangle {
        radius: Theme.radiusS
        color: {
            if (control.filled) {
                // A filled button lightens under the pointer rather than
                // darkening: every ground here is already the light end.
                return control.hovered ? Theme.mix(control.ground, Theme.n11, 0.2)
                                       : control.ground
            }
            // Theme.clear rather than "transparent": the same colour at zero
            // alpha, which is what "no ground here" means when the ground it
            // is standing in for is a light one.
            return control.down ? Theme.surfaceActive
                 : control.hovered ? Theme.surfaceHover
                                   : Theme.clear(Theme.surfaceHover)
        }
        // line-2, not line-1. An outline at line-1 is the same weight as the
        // seams between panes, so a button drawn with one reads as a region of
        // the layout rather than as something to press.
        border.width: (control.filled || control.ghost) ? 0 : Theme.borderWidth
        border.color: Theme.borderStrong
    }

    // Anchored rather than laid out in a Row: the label takes whatever the mark
    // does not, which is the behaviour a button given less width than it wants
    // needs -- the instruction survives and the punctuation after it stays put.
    contentItem: Item {
        Text {
            id: label

            anchors.left: parent.left
            anchors.right: mark.visible ? mark.left : parent.right
            anchors.rightMargin: mark.visible ? Theme.gapS : 0
            anchors.verticalCenter: parent.verticalCenter
            text: control.text
            font: control.font
            color: control.ink
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight

            AppToolTip {
                shown: label.truncated && control.hovered
                text: control.text
            }
        }

        Text {
            id: mark

            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            visible: control.affordance !== ""
            text: control.affordance
            font: control.font
            // The mark is punctuation on the label, not a second word: it
            // steps back so the instruction still reads first.
            color: control.filled ? control.ink : Theme.textSecondary
            verticalAlignment: Text.AlignVCenter
        }
    }
}
