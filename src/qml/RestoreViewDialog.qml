// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import QtQuick.Controls.Basic

/// What a saved view will not be able to draw, before it is restored.
///
/// A view names paths, and a path is only as good as the file under it: the
/// dataset may have gone, or moved, or changed shape so that the subscript no
/// longer selects a line. Restoring first and letting the reader discover it
/// row by row would be the same information delivered worse -- so the lines are
/// checked against the file first, and this is where the count goes.
///
/// The count is the prompt because the count is the decision. One stale line
/// out of nine is a view worth restoring and fixing; nine out of nine is the
/// wrong view. The reasons are under it for a reader who wants them, and the
/// restore goes ahead if they say so -- nothing here refuses, because a view
/// with half its lines broken is still the fastest way back to the half that
/// work.
///
/// Built as the system's Panel, like every other dialog in this application:
/// hairline border, 2px radius, mono uppercase header, and a scrim rather than
/// a blur behind it.
Dialog {
    id: dialog

    /// The view being restored, and what the check found.
    property string viewName: ""
    property int issues: 0
    property var reasons: []

    objectName: "restoreViewDialog"

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
            text: qsTr("this view no longer fits the file")
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
            text: dialog.issues === 1
                  ? qsTr("One line of \"%1\" will not draw.").arg(dialog.viewName)
                  : qsTr("%1 lines of \"%2\" will not draw.")
                    .arg(dialog.issues).arg(dialog.viewName)
            font: Theme.bodyStrong
            color: Theme.textPrimary
            wrapMode: Text.WordWrap
        }

        // The reasons themselves, verbatim: each is the sentence the entry row
        // would have printed, and a reader who is deciding whether to go ahead
        // is deciding on exactly these.
        Column {
            width: parent.width
            spacing: Theme.gapXS

            Repeater {
                model: dialog.reasons

                delegate: Text {
                    required property string modelData

                    width: parent.width
                    text: modelData
                    font: Theme.caption
                    color: Theme.warning
                    wrapMode: Text.WordWrap
                }
            }
        }

        Text {
            width: parent.width
            text: qsTr("Restoring anyway keeps them, with their reasons, so " +
                       "they can be written into something that does read.")
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
                objectName: "restoreAnyway"

                text: qsTr("restore anyway")
                variant: "primary"
                size: "lg"
                onClicked: dialog.accept()
            }
        }
    }
}
