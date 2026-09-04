// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import QtQuick.Controls.Basic

/// A square button whose whole content is one AppIcon, with the name of what
/// it does on a tooltip.
///
/// A picture with no label needs somewhere to say what it is. The tip is
/// AppToolTip, which is the design system's Tooltip and the same one every
/// elided value in the application uses; this file used to draw its own.
Button {
    id: control

    /// Which glyph, by AppIcon's name. Not `icon`: AbstractButton already
    /// has one of those, grouped and FINAL, and shadowing it is refused.
    property string glyph
    /// What it does, in words, for the tooltip. `text` stays empty: a Button
    /// with text would size itself around it.
    property string hint
    /// Drawn as pressed while some state it toggles is on.
    property bool active: false
    /// The glyph's colour when the button is not active. Left at the reading
    /// ink for everything that acts on what is on screen; set to Theme.danger
    /// for the one control that takes something away, where the colour is
    /// half of what says so.
    property color ink: Theme.textPrimary
    /// The glyph alone, with no slab under it.
    ///
    /// For a control that sits at the end of a line of text rather than in a
    /// bar of buttons: one per row of a list would draw a column of rims down
    /// the side of it, which is a border where the design system asks for
    /// none. With nothing to fill on hover the glyph itself is what answers
    /// the pointer, so it rests dimmed and comes up to full colour -- the same
    /// treatment the pipeline's drag handle gets, for the same reason.
    property bool bare: false

    implicitWidth: Theme.smallControlHeight
    implicitHeight: Theme.smallControlHeight
    // Whatever is left over after the icon is the rim around it. Button's own
    // default padding would leave the glyph two-thirds of this size again.
    padding: (Theme.smallControlHeight - Theme.iconSize) / 2
    hoverEnabled: true
    opacity: control.enabled ? 1.0 : 0.4

    transform: Translate { y: control.down ? 1 : 0 }

    background: Rectangle {
        visible: !control.bare
        radius: Theme.radiusS
        color: control.active ? Theme.accent
             : control.down ? Theme.surfaceActive
             : control.hovered ? Theme.surfaceHover
                               : Theme.clear(Theme.surfaceHover)
        border.width: Theme.borderWidth
        border.color: control.active ? Theme.accent : Theme.border
    }

    contentItem: AppIcon {
        name: control.glyph
        color: control.active ? Theme.accentText : control.ink
        opacity: (!control.bare || control.hovered || control.down) ? 1.0 : 0.55
    }

    AppToolTip {
        shown: control.hovered
        text: control.hint
    }
}
