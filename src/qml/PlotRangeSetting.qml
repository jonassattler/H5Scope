// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import QtQuick.Layouts

/// Where the plot's window is, in four numbers:
///
///     X RANGE
///     [   0    ]  [  2048   ]
///     Y RANGE
///     [ -1.25   ]  [  1.25   ]
///
/// The wheel, the drag and the right-drag band all say the same thing with the
/// pointer; this says it by knowing. A reader comparing two traces against the
/// same band, or lining a picture up with one in a paper, has a number in mind
/// and no way to reach it by gesture -- zooming to exactly 0.4 … 0.6 is not
/// something a wheel can be asked for.
///
/// It is an input *and* a readout, and that is the point of it: the four boxes
/// are bound to the view, so a region selected with the right button, a wheel
/// zoom, a pan and a reset all report here without a second path to keep in
/// step. Committing one calls back into the surface, which clamps it the same
/// way a pan is clamped -- the window never leaves the data -- and the boxes
/// then show what is actually in force rather than what was typed, because
/// they are bound rather than held.
///
/// Inputs, not outputs, like every other control in this rail: the boxes
/// report what the reader did and the surface writes the result back.
Column {
    id: control

    /// The PlotSurface whose window this states.
    property var target

    readonly property bool usable: !!control.target && control.target.drawable

    width: parent ? parent.width : 0
    spacing: Theme.gapXS

    /// Ask for a window, leaving the three edges the caller is not moving
    /// where they are.
    function apply(x0, x1, y0, y1) {
        if (control.target)
            control.target.setViewRange(x0, x1, y0, y1)
    }

    Text {
        text: qsTr("x range")
        font: Theme.microLabel
        color: Theme.textDisabled
    }

    RowLayout {
        width: parent.width
        spacing: Theme.gapS

        RealField {
            objectName: "rangeViewMinX"

            Layout.fillWidth: true
            enabled: control.usable
            value: control.target ? control.target.viewMinX : 0
            onCommitted: amount => control.apply(amount,
                                                 control.target.viewMaxX,
                                                 control.target.viewMinY,
                                                 control.target.viewMaxY)
        }

        RealField {
            objectName: "rangeViewMaxX"

            Layout.fillWidth: true
            enabled: control.usable
            value: control.target ? control.target.viewMaxX : 1
            onCommitted: amount => control.apply(control.target.viewMinX,
                                                 amount,
                                                 control.target.viewMinY,
                                                 control.target.viewMaxY)
        }
    }

    Text {
        text: qsTr("y range")
        font: Theme.microLabel
        color: Theme.textDisabled
    }

    RowLayout {
        width: parent.width
        spacing: Theme.gapS

        RealField {
            objectName: "rangeViewMinY"

            Layout.fillWidth: true
            enabled: control.usable
            value: control.target ? control.target.viewMinY : 0
            onCommitted: amount => control.apply(control.target.viewMinX,
                                                 control.target.viewMaxX,
                                                 amount,
                                                 control.target.viewMaxY)
        }

        RealField {
            objectName: "rangeViewMaxY"

            Layout.fillWidth: true
            enabled: control.usable
            value: control.target ? control.target.viewMaxY : 1
            onCommitted: amount => control.apply(control.target.viewMinX,
                                                 control.target.viewMaxX,
                                                 control.target.viewMinY,
                                                 amount)
        }
    }

    // Said once, here, rather than left for the reader to discover by typing a
    // number and watching a different one come back. There is nothing outside
    // the data to look at -- the y axis *is* the extent of the values and the
    // x axis is where the elements are -- so a window past either end is
    // pulled back to the end.
    Text {
        width: parent.width
        text: qsTr("Clamped to the data: there is nothing outside it to show.")
        font: Theme.caption
        color: Theme.textDisabled
        wrapMode: Text.WordWrap
    }
}
