// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import QtQuick.Controls.Basic

/// The design system's Tooltip: the ground inverted, no border and no shadow.
/// It is the only thing in this application that stands on `--surface-invert`,
/// which is what makes it read as an overlay rather than as another pane.
///
/// Drawn here rather than through the attached `ToolTip` property, for the
/// reason AppIconButton gave first: the attached property shares one instance
/// which the Controls style owns, and this application styles nothing from the
/// Controls style. `popupType: Popup.Item` for the reason AppMenu gives.
///
/// Two voices, and the difference matters:
///
///   prose     what a person reads as language -- what a control does, why a
///             tag is on a row, the rest of a name an ellipsis took. Set in
///             the reading face, sentence case, and wrapped.
///   verbatim  mono, and the text exactly as it was given. For a *value*.
///             `/Data` is not `/data` and "nan" is not "NaN", and a tooltip
///             that alters what it is quoting is not a way to read it.
///
/// The prose voice used to be the system's machine label -- mono, tracked,
/// UPPERCASED -- which was the same mistake Theme.button records having made
/// with buttons, and records the correction to: a machine label is the name of
/// a thing, and the system sets a *sentence* in sentence case in the reading
/// face. "ROTATE 90° LEFT" is not a readout, it is an instruction. It also
/// could not wrap, so the moment a tooltip had a sentence to say rather than
/// two words it ran off the side of the window.
///
/// The second voice is why this component exists at all. Everywhere a value
/// can be elided -- a grid cell, a tree row, a readout, a path -- the pointer
/// has to be able to reach the whole of it, or the UI has made data
/// inaccessible by being too small, which is the one thing a viewer must never
/// do.
ToolTip {
    id: tip

    /// Whether the pointer is somewhere that should show this. Named rather
    /// than using `visible` directly so a caller writes the condition once.
    property bool shown: false
    /// Quote the text rather than say it. See the note above.
    property bool verbatim: false

    visible: tip.shown && tip.text !== ""
    // A dwell, not an animation: the tip does not fade in, it waits and then
    // is there. It takes the longest step on the ladder because the thing
    // being avoided is a tooltip that fires while the pointer is merely
    // crossing a control on its way somewhere else, and that is a fifth of a
    // second of travel.
    delay: Theme.dur5
    popupType: Popup.Item
    // Reading text wants more room at its sides than above and below it; a
    // quoted value is the same chip so the two never look like two components.
    topPadding: Theme.gapS
    bottomPadding: Theme.gapS
    leftPadding: Theme.gapM
    rightPadding: Theme.gapM
    // Wide enough for a path or a sentence without becoming a paragraph; past
    // this it wraps rather than running off the window.
    property int maximumWidth: Theme.s14 * 4

    // Nothing on the way in and nothing on the way out. Stated rather than
    // left off: Popup takes its transitions from the style, and `null` is the
    // only way to say that this one has none.
    enter: null
    exit: null

    background: Rectangle {
        color: Theme.surfaceInvert
        radius: Theme.radiusS
    }

    contentItem: Text {
        text: tip.text
        font: tip.verbatim ? Theme.monoSmall : Theme.caption
        color: Theme.textInvert
        // Both voices wrap. A quoted value breaks anywhere, because a path has
        // no word boundaries to break at; a sentence breaks at its words and
        // only splits one when a single word is wider than the whole tip.
        wrapMode: tip.verbatim ? Text.WrapAnywhere : Text.Wrap
        width: Math.min(implicitWidth,
                        tip.maximumWidth - tip.leftPadding - tip.rightPadding)
    }
}
