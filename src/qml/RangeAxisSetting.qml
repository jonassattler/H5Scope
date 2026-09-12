// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import QtQuick.Layouts

/// The controls for an x axis stated as start, step and stop: three boxes, a
/// lock apiece, the sentence that states the two-of-three rule, and the way
/// back to the default.
///
/// Lifted out of PlotSettingsPanel when the custom plot tabs started offering
/// the same axis. RangeAxis holds the arithmetic and this holds the controls
/// over it, which is the same split for the same reason: the rule that editing
/// a third releases the one edited longest ago is not obvious, and a second
/// copy of it would drift the first time one of them was corrected.
///
/// It takes a PlotSurface rather than a RangeAxis, because that is what the
/// panels have and because the surface is where `dataLength` -- the number the
/// caption states the default against -- actually lives.
Column {
    id: setting

    /// The PlotSurface whose axis this states.
    property var target

    spacing: Theme.gapXS

    Repeater {
        model: [
            { key: "start", label: qsTr("start") },
            { key: "step",  label: qsTr("step") },
            { key: "stop",  label: qsTr("stop") }
        ]

        delegate: RowLayout {
            id: axisRow

            required property var modelData

            readonly property bool pinned:
                setting.target ? setting.target.locked(modelData.key) : false
            /// What the axis is actually using for this one, whether it was
            /// typed or worked out. A computed box shows its result rather
            /// than sitting blank, so the reader can see what locking two
            /// of them did to the third.
            readonly property real shown: {
                if (!setting.target)
                    return 0
                const resolved = setting.target.resolved
                return resolved ? resolved[modelData.key] : 0
            }

            width: parent.width
            spacing: Theme.gapS

            Text {
                Layout.preferredWidth: Theme.s10
                text: axisRow.modelData.label
                font: Theme.microLabel
                color: axisRow.pinned ? Theme.textPrimary : Theme.textDisabled
                verticalAlignment: Text.AlignVCenter
            }

            RealField {
                Layout.fillWidth: true
                value: axisRow.shown
                // A value the reader typed is a value they meant, so
                // entering one is what pins it. Asking them to tick a box
                // first would be asking them to say the same thing twice.
                onCommitted: amount => {
                    if (!setting.target)
                        return
                    setting.target[axisRow.modelData.key === "start" ? "rangeStart"
                               : axisRow.modelData.key === "step"  ? "rangeStep"
                                                                   : "rangeStop"] = amount
                    setting.target.lock(axisRow.modelData.key)
                }
            }

            AppCheckBox {
                text: qsTr("lock")
                checked: axisRow.pinned
                onToggled: {
                    if (!setting.target)
                        return
                    // Pinning from the box takes the value on screen with
                    // it, or the axis would jump to whatever was last in
                    // the property behind an unpinned box.
                    if (checked) {
                        setting.target[axisRow.modelData.key === "start" ? "rangeStart"
                                   : axisRow.modelData.key === "step"  ? "rangeStep"
                                                                       : "rangeStop"] =
                            axisRow.shown
                    }
                    setting.target.setLocked(axisRow.modelData.key, checked)
                }
            }
        }
    }

    // Two is the whole of the rule, so it is stated once, here, rather
    // than left for the reader to infer from a box unticking itself. The
    // default is stated too, because "0 : 1 : len(data)" is the sentence
    // that says these numbers are the x values and not a viewport.
    Text {
        width: parent.width
        text: {
            if (!setting.target)
                return ""
            if (!setting.target.rangeValid)
                return qsTr("A step above zero and a stop above the start; showing 0 : 1 : %1 meanwhile.")
                       .arg(setting.target.dataLength)
            if (setting.target.locks.length === 0)
                return qsTr("x = start + i x step, over %1 elements. Edit two; the third follows.")
                       .arg(setting.target.dataLength)
            return qsTr("Editing a third releases the one edited longest ago.")
        }
        font: Theme.caption
        color: (setting.target && !setting.target.rangeValid)
               ? Theme.warning : Theme.textDisabled
        wrapMode: Text.WordWrap
    }

    AppToolButton {
        width: parent.width
        text: qsTr("back to 0 : 1 : len(data)")
        size: "sm"
        enabled: setting.target ? setting.target.locks.length > 0 : false
        onClicked: { if (setting.target) setting.target.locks = [] }
    }
}
