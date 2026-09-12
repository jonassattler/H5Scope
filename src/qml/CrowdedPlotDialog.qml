// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import QtQuick.Controls.Basic
import H5Scope.Backend

/// What a whole dataset is about to cost the plot it is going into.
///
/// Adding `/large/table_500000x4` puts half a million strokes on one pair of
/// axes. That is a thing a reader may genuinely want -- the shape of a great
/// many runs at once is a real question -- and it is also a thing nobody means
/// to do by pressing a plus beside a row. So it is asked about rather than
/// refused, and rather than clipped: a silent sixty-four lines out of half a
/// million is the worst of the three, because it is a picture that looks
/// complete and is not.
///
/// The same stance the legend's `all` takes on a table of ten thousand rows,
/// and the same words: state the cost, then do as you are told.
Dialog {
    id: dialog

    /// The tab it would go into, what would go into it, and how many lines
    /// that is.
    property int plotIndex: -1
    property string path: ""
    property int lines: 0

    objectName: "crowdedPlotDialog"

    anchors.centerIn: Overlay.overlay
    modal: true
    padding: Theme.gapXL

    Overlay.modal: Rectangle {
        color: Theme.scrim
    }

    background: Rectangle {
        color: Theme.surfaceRaised
        radius: Theme.radiusS
        border.width: Theme.borderWidth
        border.color: Theme.borderStrong
    }

    header: Item {
        implicitHeight: Theme.treeHeaderHeight

        Text {
            anchors.left: parent.left
            anchors.leftMargin: Theme.gapXL
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("that is a great many lines")
            font: Theme.micro
            color: Theme.textSecondary
        }

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: Theme.borderWidth
            color: Theme.border
        }
    }

    Column {
        width: Theme.dialogTextWidth
        spacing: Theme.gapM

        Text {
            width: parent.width
            text: {
                const plot = AppController.customPlots.plotAt(dialog.plotIndex)
                return qsTr("%1 would put %2 lines into %3.")
                       .arg(dialog.path)
                       .arg(dialog.lines)
                       .arg(plot ? plot.name : qsTr("this plot"))
            }
            font: Theme.bodyStrong
            color: Theme.textPrimary
            wrapMode: Text.WordWrap
        }

        Text {
            width: parent.width
            text: qsTr("Past a few dozen, strokes over one another stop " +
                       "separating and the plot takes a while to draw. " +
                       "Nothing is stopping you — the legend takes any of " +
                       "them back out again.")
            font: Theme.caption
            color: Theme.textDisabled
            wrapMode: Text.WordWrap
        }
    }

    footer: Item {
        implicitHeight: Theme.controlHeight + Theme.gapXL

        Row {
            anchors.right: parent.right
            anchors.rightMargin: Theme.gapXL
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.gapM

            AppToolButton {
                text: qsTr("cancel")
                size: "lg"
                onClicked: dialog.reject()
            }

            AppToolButton {
                objectName: "addAnyway"

                text: qsTr("add all %1").arg(dialog.lines)
                variant: "caution"
                size: "lg"
                onClicked: dialog.accept()
            }
        }
    }

    onAccepted: AppController.customPlots.addDatasetTo(dialog.plotIndex,
                                                       dialog.path, true)
}
