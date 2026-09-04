// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Qt.labs.folderlistmodel
import H5Scope.Backend

/// The application's own file picker.
///
/// QtQuick.Dialogs.FileDialog hands the job to the host, which returns a window
/// in the host's palette, typeface and metrics -- the one window in this program
/// that would not look like this program. Since every other surface here is
/// drawn from Theme, this one is too: same hairlines, same mono machine labels,
/// same 2px radius, same single primary button per view.
///
/// The listing itself comes from Qt.labs.folderlistmodel, so no directory
/// walking happens in this file; FileSystem supplies the well-known locations
/// and the formatting QML has no answer for.
Dialog {
    id: picker

    /// Emitted with a plain filesystem path once the user commits to a file.
    signal fileChosen(string path)

    /// Where the browser is pointing. Reopening returns here.
    property url folder: FileSystem.home
    /// Highlighted row's absolute path, or "" when the highlight is on a folder.
    property string selectedPath: ""
    /// Narrow the listing to the extensions this program can actually open.
    property bool onlyHdf5: true

    readonly property var hdf5Filters: ["*.h5", "*.hdf5", "*.he5", "*.hdf", "*.nc"]

    anchors.centerIn: parent
    modal: true
    padding: 0
    width: Math.min(Theme.pickerWidth, parent ? parent.width - Theme.gapXL * 2
                                              : Theme.pickerWidth)
    height: Math.min(Theme.pickerHeight, parent ? parent.height - Theme.gapXL * 2
                                                : Theme.pickerHeight)

    // Open where the last file came from, not wherever the browser was left.
    onAboutToShow: {
        folder = FileSystem.folderOf(AppController.filePath)
        selectedPath = ""
        list.currentIndex = -1
        locationField.text = FileSystem.toLocalPath(folder)
    }

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
            anchors.leftMargin: Theme.gapM
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("open hdf5 file")
            font: Theme.micro
            color: Theme.textSecondary
        }

        Text {
            anchors.right: parent.right
            anchors.rightMargin: Theme.gapM
            anchors.verticalCenter: parent.verticalCenter
            text: folderModel.count === 1
                  ? qsTr("1 entry") : qsTr("%1 entries").arg(folderModel.count)
            font: Theme.readout
            color: Theme.textDisabled
        }

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: Theme.borderWidth
            color: Theme.border
        }
    }

    FolderListModel {
        id: folderModel

        folder: picker.folder
        showDirs: true
        showDotAndDotDot: false
        showHidden: false
        sortField: FolderListModel.Name
        sortCaseSensitive: false
        // Folders first, so descending the tree never means hunting through
        // the files for the one directory in the middle of them.
        showDirsFirst: true
        nameFilters: picker.onlyHdf5 ? picker.hdf5Filters : ["*"]
    }

    contentItem: ColumnLayout {
        spacing: 0

        // --- where we are, and how to leave ------------------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.toolbarHeight
            color: Theme.surface

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.gapM
                anchors.rightMargin: Theme.gapM
                spacing: Theme.gapS

                AppToolButton {
                    text: qsTr("up")
                    size: "md"
                    variant: "ghost"
                    onClicked: picker.navigate(FileSystem.parentOf(picker.folder))
                }

                AppToolButton {
                    text: qsTr("home")
                    size: "md"
                    variant: "ghost"
                    onClicked: picker.navigate(FileSystem.home)
                }

                // Typing a path is the fastest way to somewhere deep, and the
                // one thing every styled picker tends to leave out.
                FilterInput {
                    id: locationField

                    Layout.fillWidth: true
                    placeholderText: qsTr("path")
                    text: FileSystem.toLocalPath(picker.folder)
                    onAccepted: {
                        if (FileSystem.isFolder(text)) {
                            picker.navigate(FileSystem.fromLocalPath(text))
                        } else if (FileSystem.exists(text)) {
                            picker.commit(text)
                        }
                    }
                }
            }

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: Theme.borderWidth
                color: Theme.border
            }
        }

        // --- places rail | listing ----------------------------------------
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Rectangle {
                Layout.preferredWidth: Theme.railWidth / 2
                Layout.fillHeight: true
                color: Theme.surface

                Column {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.topMargin: Theme.gapS

                    Repeater {
                        model: FileSystem.places

                        delegate: Rectangle {
                            required property var modelData

                            width: parent.width
                            height: Theme.treeRowHeight
                            color: placeHover.hovered ? Theme.surfaceHover
                                                      : "transparent"

                            Text {
                                anchors.left: parent.left
                                anchors.leftMargin: Theme.gapM
                                anchors.right: parent.right
                                anchors.rightMargin: Theme.gapS
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.label
                                font: Theme.micro
                                color: Theme.textSecondary
                                elide: Text.ElideRight

                                // Where it actually goes. A place is named by
                                // its last segment, and two of them can share
                                // one.
                                AppToolTip {
                                    shown: placeHover.hovered
                                    verbatim: true
                                    text: FileSystem.toLocalPath(modelData.url)
                                }
                            }

                            HoverHandler { id: placeHover }
                            TapHandler {
                                onTapped: picker.navigate(modelData.url)
                            }
                        }
                    }
                }

                Rectangle {
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: Theme.borderWidth
                    color: Theme.border
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                // Column heads, in the system's machine-label treatment.
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.rowHeight
                    color: Theme.surface

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.gapM
                        anchors.rightMargin: Theme.gapM
                        spacing: Theme.gapM

                        Text {
                            Layout.fillWidth: true
                            text: qsTr("name")
                            font: Theme.micro
                            color: Theme.textSecondary
                        }

                        Text {
                            Layout.preferredWidth: Theme.railWidth / 3
                            text: qsTr("size")
                            font: Theme.micro
                            color: Theme.textSecondary
                            horizontalAlignment: Text.AlignRight
                        }

                        Text {
                            Layout.preferredWidth: Theme.railWidth / 2
                            text: qsTr("modified")
                            font: Theme.micro
                            color: Theme.textSecondary
                            horizontalAlignment: Text.AlignRight
                        }
                    }

                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: Theme.borderWidth
                        color: Theme.borderStrong
                    }
                }

                ListView {
                    id: list

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    focus: true
                    currentIndex: -1
                    model: folderModel
                    boundsBehavior: Flickable.StopAtBounds

                    ScrollBar.vertical: ScrollBar {}

                    Rectangle {
                        anchors.fill: parent
                        color: Theme.surfaceInset
                        z: -1
                    }

                    // Enter opens whatever is highlighted; Escape backs out.
                    // A picker one cannot drive from the keyboard is only half
                    // a picker.
                    Keys.onReturnPressed: picker.activate(list.currentIndex)
                    Keys.onEnterPressed: picker.activate(list.currentIndex)
                    Keys.onEscapePressed: picker.reject()

                    delegate: Rectangle {
                        id: row

                        required property int index
                        required property string fileName
                        required property string filePath
                        required property bool fileIsDir
                        required property real fileSize
                        required property date fileModified

                        width: ListView.view.width
                        height: Theme.rowHeight
                        color: ListView.isCurrentItem ? Theme.surfaceActive
                             : rowHover.hovered ? Theme.surfaceHover
                             : index % 2 === 0 ? "transparent" : Theme.rowStripe

                        // The selected row carries the accent rule, exactly as
                        // the object tree's does.
                        Rectangle {
                            anchors.left: parent.left
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            width: Theme.borderWidthAccent
                            color: Theme.accent
                            visible: row.ListView.isCurrentItem
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.gapM
                            anchors.rightMargin: Theme.gapM
                            spacing: Theme.gapM

                            Text {
                                id: entryName

                                Layout.fillWidth: true
                                text: row.fileIsDir ? row.fileName + "/" : row.fileName
                                font: Theme.mono
                                color: row.fileIsDir ? Theme.textEmphasis
                                                     : Theme.textPrimary
                                elide: Text.ElideMiddle

                                AppToolTip {
                                    shown: entryName.truncated && rowHover.hovered
                                    verbatim: true
                                    text: entryName.text
                                }
                            }

                            Text {
                                Layout.preferredWidth: Theme.railWidth / 3
                                text: row.fileIsDir
                                      ? "" : FileSystem.formatSize(row.fileSize)
                                font: Theme.readout
                                color: Theme.textDisabled
                                horizontalAlignment: Text.AlignRight
                            }

                            Text {
                                Layout.preferredWidth: Theme.railWidth / 2
                                text: FileSystem.formatTime(row.fileModified)
                                font: Theme.readout
                                color: Theme.textDisabled
                                horizontalAlignment: Text.AlignRight
                            }
                        }

                        HoverHandler { id: rowHover }

                        TapHandler {
                            onSingleTapped: {
                                list.currentIndex = row.index
                                picker.selectedPath = row.fileIsDir
                                                      ? "" : row.filePath
                            }
                            onDoubleTapped: picker.activate(row.index)
                        }

                        Rectangle {
                            anchors.bottom: parent.bottom
                            width: parent.width
                            height: Theme.borderWidth
                            color: Theme.surface
                        }
                    }
                }

                // An empty directory has to say so; a blank rectangle reads as
                // a bug rather than as an answer.
                Text {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: folderModel.count === 0
                    text: picker.onlyHdf5
                          ? qsTr("no HDF5 files here")
                          : qsTr("empty")
                    font: Theme.micro
                    color: Theme.textSecondary
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }

    footer: Rectangle {
        implicitHeight: Theme.toolbarHeight + Theme.gapS
        color: Theme.surface
        radius: Theme.radiusS

        Rectangle {
            anchors.top: parent.top
            width: parent.width
            height: Theme.borderWidth
            color: Theme.border
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.gapM
            anchors.rightMargin: Theme.gapM
            spacing: Theme.gapS

            CheckBox {
                id: allFiles

                text: qsTr("all files")
                font: Theme.label
                checked: !picker.onlyHdf5
                onToggled: picker.onlyHdf5 = !checked

                indicator: Rectangle {
                    implicitWidth: Theme.gapL
                    implicitHeight: Theme.gapL
                    anchors.verticalCenter: parent.verticalCenter
                    radius: Theme.radiusS
                    color: Theme.surfaceInset
                    border.width: Theme.borderWidth
                    border.color: allFiles.hovered ? Theme.borderStrong
                                                   : Theme.border

                    Rectangle {
                        anchors.centerIn: parent
                        width: parent.width - Theme.gapXS * 2
                        height: width
                        radius: Theme.radiusS
                        color: Theme.accent
                        visible: allFiles.checked
                    }
                }

                contentItem: Text {
                    text: allFiles.text
                    font: allFiles.font
                    color: Theme.textSecondary
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: allFiles.indicator.width + Theme.gapS
                }
            }

            Item { Layout.fillWidth: true }

            AppToolButton {
                text: qsTr("cancel")
                size: "md"
                variant: "ghost"
                onClicked: picker.reject()
            }

            AppToolButton {
                text: qsTr("open")
                size: "md"
                variant: "primary"
                enabled: picker.selectedPath !== ""
                onClicked: picker.commit(picker.selectedPath)
            }
        }
    }

    /// Move the browser to `target`, dropping any selection made in the old
    /// directory -- it no longer refers to anything on screen.
    function navigate(target) {
        picker.folder = target
        picker.selectedPath = ""
        list.currentIndex = -1
        locationField.text = FileSystem.toLocalPath(target)
    }

    /// Descend into row `index` if it is a directory, open it if it is a file.
    function activate(index) {
        if (index < 0 || index >= folderModel.count)
            return
        const path = folderModel.get(index, "filePath")
        if (folderModel.isFolder(index)) {
            picker.navigate(FileSystem.fromLocalPath(path))
        } else {
            picker.commit(path)
        }
    }

    function commit(path) {
        picker.close()
        picker.fileChosen(path)
    }
}
