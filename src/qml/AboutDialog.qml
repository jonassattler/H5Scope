// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import H5Scope.Backend

/// Help -> About. Built as the system's Panel, like the error dialog it sits
/// beside in Main.qml: hairline border, 2px radius, mono uppercase header bar,
/// and a scrim rather than a blur behind it.
///
/// The design system has no logo, so the name is set as a wordmark -- mono,
/// uppercase, wide-tracked -- which is what that system does wherever a mark
/// would otherwise go.
Dialog {
    id: control

    anchors.centerIn: parent
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
            text: qsTr("about")
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

    contentItem: ColumnLayout {
        spacing: Theme.gapL

        Text {
            text: qsTr("H5Scope")
            font: Theme.label
            color: Theme.textEmphasis
        }

        Text {
            Layout.fillWidth: true
            text: qsTr("A viewer for HDF5 files. Files are opened read-only.")
            font: Theme.body
            color: Theme.textPrimary
            wrapMode: Text.WordWrap
        }

        // What this binary is, which commit it was built from, what it is
        // called on disk, and which HDF5 it statically links -- the last being
        // the whole point of shipping a self-contained executable.
        //
        // The file name is here because it carries the version: a reader with
        // several builds on disk can see from this dialog which of them
        // answered, without going back to the shell to find out.
        GridLayout {
            columns: 2
            columnSpacing: Theme.gapXL
            rowSpacing: Theme.gapS

            Text {
                text: qsTr("version")
                font: Theme.micro
                color: Theme.textSecondary
            }
            Text {
                // The compiled-in constant rather than Qt.application.version:
                // the latter is only whatever main() set, so it is empty in any
                // host that did not -- the QML test harness, for one -- and a
                // dialog whose job is to say what this build is should not
                // depend on someone else having said it first.
                text: AppController.appVersion
                font: Theme.monoSmall
                color: Theme.textPrimary
            }

            Text {
                text: qsTr("commit")
                font: Theme.micro
                color: Theme.textSecondary
            }
            Text {
                text: AppController.appCommit
                font: Theme.monoSmall
                color: Theme.textPrimary
            }

            Text {
                text: qsTr("binary")
                font: Theme.micro
                color: Theme.textSecondary
            }
            Text {
                id: binaryLabel

                Layout.fillWidth: true
                text: AppController.binaryName
                font: Theme.monoSmall
                color: Theme.textPrimary
                elide: Text.ElideMiddle

                HoverHandler { id: binaryHover }

                AppToolTip {
                    shown: binaryLabel.truncated && binaryHover.hovered
                    verbatim: true
                    text: binaryLabel.text
                }
            }

            Text {
                text: qsTr("hdf5")
                font: Theme.micro
                color: Theme.textSecondary
            }
            Text {
                text: AppController.hdf5Version
                font: Theme.monoSmall
                color: Theme.textPrimary
            }

            Text {
                text: qsTr("licence")
                font: Theme.micro
                color: Theme.textSecondary
            }
            Text {
                text: qsTr("GPL-3.0-only")
                font: Theme.monoSmall
                color: Theme.textPrimary
            }
        }

        // The GPL's Appropriate Legal Notices, and the OFL's requirement that
        // its terms accompany the font software. Both are obligations of
        // shipping a static binary rather than decoration: everything named
        // here is *inside* the executable, so there is nowhere else for a
        // reader to look.
        //
        // Which is also why the last paragraph names the two options rather
        // than only the repository. The full texts are compiled in as well --
        // the GPL and the notices at :/licenses, the OFL beside the faces it
        // covers at :/fonts/LICENSE.txt -- and a summary that can only point
        // at a URL would leave an offline reader with a binary whose licence
        // it is impossible to read.
        Text {
            Layout.fillWidth: true
            Layout.maximumWidth: Theme.dialogTextWidth
            text: qsTr("Copyright © 2026 Jonas Sattler. This program comes with "
                       + "ABSOLUTELY NO WARRANTY. It is free software, and you "
                       + "are welcome to redistribute it under the terms of the "
                       + "GNU General Public License version 3.\n\n"
                       + "Links Qt 6.11.1 (Qt Graphs and Qt Quick 3D under "
                       + "GPL-3.0-only, the remaining modules under LGPL-3.0, "
                       + "conveyed here under the GPL), HDF5 2.2.0 and about "
                       + "twenty further libraries. Sets IBM Plex, © 2017 IBM "
                       + "Corp., under the SIL Open Font License 1.1.\n\n"
                       + "The full licence and every third-party notice are "
                       + "inside this binary: run it with --license or "
                       + "--notices to print them. Source: "
                       + "github.com/jonassattler/H5Scope")
            font: Theme.caption
            color: Theme.textSecondary
            wrapMode: Text.WordWrap
        }
    }

    footer: Item {
        implicitHeight: Theme.controlHeight + Theme.gapXL

        AppToolButton {
            anchors.right: parent.right
            anchors.rightMargin: Theme.gapXL
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("dismiss")
            variant: "primary"
            size: "lg"
            onClicked: control.close()
        }
    }
}
