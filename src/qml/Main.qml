// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Window
import H5Scope.Backend

/// The window frame:
///
///     menu bar         28
///     tree | tabs + content
///     status strip     22
///
/// Every boundary between the fixed chrome and the scrolling content is a
/// hairline, per the design system: borders do the structural work and nothing
/// casts a shadow unless it floats. Chrome never scrolls; content always does.
///
/// mockup.md put a 48px breadcrumb bar above a 44px action bar. Both are gone.
/// The breadcrumb carried the file name and the selected path, and both already
/// appear elsewhere -- the file name in the window title, and the whole
/// breadcrumb in the status strip's left segment, which is where ambient state
/// belongs. The action bar's buttons are menu rows now (design.txt asks for a
/// proper menu rather than a strip of buttons) and its filter box has moved to
/// the bottom of the tree. One 28px menu bar is the top of the window.
///
/// The strip below it is likewise one layer, not two. It had been two tabs --
/// information and data viewer -- with a second row of table / plot / image
/// inside the second of them, which spent two bars of chrome and a nested
/// selection on what is one question: which of four ways of looking at the
/// selection is on screen. The four are peers now and sit in one strip.
/// DataView still owns which of its three is showing, because it is the thing
/// that knows a text dataset cannot be plotted; this window asks it.
///
/// The attribute table is folded into the information view as its last panel.
/// AppController still reports metadataTabVisible; it now drives that panel
/// rather than a tab of its own.
ApplicationWindow {
    id: window

    width: 1280
    height: 820
    // Wide enough for the bar above the data views to hold everything in it
    // at once: the slice line with room to type in, and the three buttons that
    // act on it. A minimum that cannot draw the chrome is not a minimum -- it
    // is a window that overflows quietly.
    //
    // Asked of the bar rather than typed here, because the bar is the only
    // thing that knows: its parts are text, and how wide text comes out is the
    // host's decision, not this file's. Typed as 1000 it was right on the
    // machine it was typed on and thirty pixels short of right on CI, where
    // the slice path elided at the size the window opens at.
    //
    // The tree stands at its preferred width while the pane beside it takes
    // the change, so it is that width, not the tree's own minimum, that the
    // bar has to be added to.
    minimumWidth: Theme.treeWidth + Theme.splitHandleWidth
                  + dataView.barMinimumWidth
    minimumHeight: 520
    visible: true
    title: AppController.hasFile
           ? qsTr("H5Scope - %1").arg(AppController.fileName)
           : qsTr("H5Scope")

    color: Theme.background

    readonly property var tabs: [
        { id: "info",  label: qsTr("information") },
        { id: "table", label: qsTr("table") },
        { id: "plot",  label: qsTr("plot") },
        { id: "image", label: qsTr("image") }
    ]

    /// Whether the information view is the one showing. Which of the other
    /// three is showing is DataView's own state and stays there: it is the
    /// thing that has to drop out of the plot when the selection turns out to
    /// be text, and a copy of that state up here could only disagree with it.
    property bool informationSelected: true

    /// Whether the tree draws the tags beside its names. View -> Tree Tags
    /// flips it.
    property bool treeTagsVisible: true

    /// Which tab is selected, by stable id rather than by position.
    readonly property string currentTabId:
        informationSelected ? "info" : dataView.viewMode

    /// The plot and the image are for numbers; the table serves every datatype
    /// and the information view needs no dataset at all. An unavailable tab is
    /// shown greyed rather than removed, so the strip keeps its shape and the
    /// reader can see what this selection does not offer.
    function tabAvailable(id) {
        return id === "info" || id === "table" || AppController.datasetIsNumeric
    }

    function selectTab(id) {
        if (!window.tabAvailable(id))
            return
        window.informationSelected = id === "info"
        if (id !== "info")
            dataView.show(id)
    }

    // --- the device pixel grid -------------------------------------------
    // Theme measures everything in logical pixels, and at a fractional display
    // scale a whole number of those is not a whole number of physical ones.
    // Handing it the ratio is what lets the table snap its columns so that
    // every rule in the grid comes out the same width. Bound rather than read
    // once: a window dragged to a second monitor is on a different scale.
    Binding {
        target: Theme
        property: "pixelRatio"
        value: Screen.devicePixelRatio
    }

    // --- pressing somewhere else puts the caret down ----------------------
    // Qt Quick does not take focus off a text field when the pointer lands
    // outside it, and it cannot be done from QML either: a pointer handler is
    // offered a press only until some item accepts it, and every item a reader
    // presses to leave a text box accepts presses. FocusRelease watches the
    // window itself, which is the one place that sees them all. See its header.
    FocusRelease {
        window: window
    }

    // --- fixed chrome above ----------------------------------------------
    menuBar: AppMenuBar {
        currentTabId: window.currentTabId
        treeTagsVisible: window.treeTagsVisible

        onOpenRequested: filePicker.open()
        onRecentRequested: (path) => AppController.openFile(path)
        onReloadRequested: AppController.openFile(AppController.filePath)
        onCloseRequested: AppController.closeFile()
        onExpandRequested: objectTree.expandToDepth(2)
        onCollapseRequested: objectTree.collapseAll()
        onTreeTagsRequested: window.treeTagsVisible = !window.treeTagsVisible
        onAboutRequested: aboutDialog.open()
        onTabRequested: (id) => window.selectTab(id)
    }

    // --- fixed chrome below ----------------------------------------------
    footer: StatusStrip {}

    // --- tree | tabs -----------------------------------------------------
    SplitView {
        anchors.fill: parent
        orientation: Qt.Horizontal

        // The handle is a grab target four pixels wide painted as a hairline,
        // so the seam reads the same as every other boundary until touched.
        handle: Rectangle {
            implicitWidth: Theme.splitHandleWidth
            color: Theme.background

            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: Theme.borderWidth
                color: SplitHandle.pressed ? Theme.accent
                     : SplitHandle.hovered ? Theme.borderStrong : Theme.border
            }
        }

        ObjectTree {
            id: objectTree

            SplitView.preferredWidth: Theme.treeWidth
            SplitView.minimumWidth: 220

            tagsVisible: window.treeTagsVisible

            onObjectSelected: (path) => AppController.selectPath(path)
        }

        ColumnLayout {
            SplitView.fillWidth: true
            spacing: 0

            // Tab strip. The design system's Tabs carries no ground of its
            // own -- the row is transparent and only the hairline beneath it
            // and the accent rule under the active tab draw anything.
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.tabBarHeight
                color: "transparent"

                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: Theme.borderWidth
                    color: Theme.border
                }

                Row {
                    id: tabBar

                    anchors.left: parent.left
                    anchors.leftMargin: Theme.gapS
                    height: parent.height

                    // Exposed for the QML tests, which assert tab visibility.
                    readonly property int count: window.tabs.length

                    Repeater {
                        model: window.tabs

                        delegate: AppTabButton {
                            required property var modelData

                            text: modelData.label
                            selected: window.currentTabId === modelData.id
                            enabled: window.tabAvailable(modelData.id)
                            onClicked: window.selectTab(modelData.id)
                        }
                    }
                }
            }

            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: window.informationSelected ? 0 : 1

                InfoView {}
                DataView {
                    id: dataView

                    // Named so the QML suite can reach the views' shared
                    // state, as the surfaces inside it are.
                    objectName: "dataView"
                }
            }
        }
    }

    // --- transient status line -------------------------------------------
    // A toast floats, so this is one of the two places the system allows an
    // elevation cue -- and even here it is a border, not a drop shadow.
    Rectangle {
        id: statusToast

        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: Theme.gapXL
        width: statusText.implicitWidth + Theme.gapL * 2
        height: Theme.controlHeight
        radius: Theme.radiusS
        color: Theme.surfaceRaised
        border.width: Theme.borderWidth
        border.color: Theme.borderStrong
        visible: false

        Text {
            id: statusText
            anchors.centerIn: parent
            font: Theme.body
            color: Theme.textPrimary
        }

        // How long it stays, which is the only thing about a toast that is
        // about time. It used to be the middle leg of a fade-pause-fade, and
        // is a timer now that nothing in this application fades: the message
        // is there the moment it is said and gone four seconds later.
        Timer {
            id: statusDwell

            interval: 4000
            onTriggered: statusToast.visible = false
        }

        Connections {
            target: AppController
            function onStatusMessage(message) {
                statusText.text = message
                statusToast.visible = true
                statusDwell.restart()
            }
        }
    }

    // --- error dialog, only ever raised by an explicit user action --------
    // Built as the system's Panel: hairline border, 2px radius, mono uppercase
    // header bar, and a scrim rather than a blur behind it.
    Dialog {
        id: errorDialog

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
                text: qsTr("cannot open file")
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

        // An error names what failed and states the consequence; it never
        // apologises. The text comes from the backend verbatim.
        Text {
            text: AppController.errorText
            font: Theme.body
            color: Theme.textPrimary
            wrapMode: Text.WordWrap
            width: 460
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
                onClicked: errorDialog.close()
            }
        }
    }

    AboutDialog {
        id: aboutDialog
    }

    // Whether a file opened is no longer something openFile() can return.
    //
    // It is opened on the thread that owns HDF5 and answered a moment later --
    // which is the whole point, because a large file on a network share takes
    // long enough that finding out here would freeze the window on the click
    // that asked. The three places that open one therefore just ask, and this
    // is where the answer arrives. `errorText` is already bound to the dialog's
    // body, so all that is left is to put it in front of the reader.
    Connections {
        target: AppController

        function onFileOpened(ok, path) {
            if (!ok)
                errorDialog.open()
        }
    }

    // The file picker is the application's own, not the platform's: a native
    // dialog would arrive in the host's palette and typeface, which is the one
    // window in this program that would not look like the program. See
    // FilePicker.qml.
    FilePicker {
        id: filePicker

        onFileChosen: (path) => AppController.openFile(path)
    }
}
