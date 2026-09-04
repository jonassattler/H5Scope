// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import QtQuick.Controls.Basic
import H5Scope.Backend

/// The 28px menu bar, and the top of the window.
///
/// design.txt asks for a proper menu rather than a strip of buttons, so this is
/// built on Qt Quick's own MenuBar/Menu/MenuItem rather than assembled from
/// AppToolButtons: click-outside-to-close, hovering to switch between open
/// drawers, keyboard navigation and working shortcuts are all desktop
/// conventions a hand-rolled bar would have to reimplement badly.
///
/// It replaces the 44px action bar, whose buttons are now menu rows and whose
/// filter box has moved into the tree (see ObjectTree.qml). The right-hand slot
/// the design system's MenuBar reserves for a status badge stays empty: this
/// application opens every file read-only and has no edit affordance anywhere
/// in it, so a badge saying so was a label for a state that has no alternative
/// -- one the About dialog already states once, where a reader goes to ask.
///
/// The type is a Rectangle wrapping a MenuBar rather than a MenuBar itself:
/// MenuBar's own content row holds nothing but menu titles, and the bar draws
/// its own hairline beneath them. Note also the App- prefix -- a file named MenuBar.qml
/// would be silently shadowed by QtQuick.Controls' own type, which is the exact
/// fault tst_qmlload.cpp exists to catch.
Rectangle {
    id: bar

    signal openRequested()
    /// A file the reader picked out of the recent list.
    signal recentRequested(string path)
    signal reloadRequested()
    signal closeRequested()
    signal expandRequested()
    signal collapseRequested()
    signal treeTagsRequested()
    signal aboutRequested()
    signal tabRequested(string id)

    /// Which tab the window is showing, so View can mark it.
    property string currentTabId: ""
    /// Whether the tree draws its tags, so View can mark that too.
    property bool treeTagsVisible: true

    /// Exposed for the QML suite, which asserts the bar's shape.
    readonly property alias menus: menuStrip
    readonly property alias menuCount: menuStrip.count

    implicitHeight: Theme.menuBarHeight
    color: Theme.background

    // --- actions ---------------------------------------------------------
    // Declared out here rather than inside the drawers so a shortcut works
    // from the first frame, whether or not its menu has ever been opened.
    // Each action is also the single source of truth for its own shortcut
    // text: AppMenuItem reads the string back off the action to draw it, so
    // the label and the binding cannot drift apart.
    Action {
        id: openAction
        text: qsTr("Open…")
        shortcut: "Ctrl+O"
        onTriggered: bar.openRequested()
    }

    Action {
        id: reloadAction
        text: qsTr("Reload")
        shortcut: "Ctrl+R"
        enabled: AppController.hasFile
        onTriggered: bar.reloadRequested()
    }

    Action {
        id: closeAction
        text: qsTr("Close")
        shortcut: "Ctrl+W"
        enabled: AppController.hasFile
        onTriggered: bar.closeRequested()
    }

    Action {
        id: quitAction
        text: qsTr("Quit")
        shortcut: "Ctrl+Q"
        onTriggered: Qt.quit()
    }

    Action {
        id: infoTabAction
        text: qsTr("Information")
        shortcut: "Ctrl+1"
        onTriggered: bar.tabRequested("info")
    }

    // One row per tab, in the strip's own order. The three data views are
    // peers of the information view there, so they are peers here too --
    // "Data Viewer" named a layer that no longer exists.
    Action {
        id: tableTabAction
        text: qsTr("Table")
        shortcut: "Ctrl+2"
        onTriggered: bar.tabRequested("table")
    }

    Action {
        id: plotTabAction
        text: qsTr("Plot")
        shortcut: "Ctrl+3"
        enabled: AppController.datasetIsNumeric
        onTriggered: bar.tabRequested("plot")
    }

    Action {
        id: imageTabAction
        text: qsTr("Image")
        shortcut: "Ctrl+4"
        enabled: AppController.datasetIsNumeric
        onTriggered: bar.tabRequested("image")
    }

    Action {
        id: expandAction
        text: qsTr("Expand All")
        shortcut: "Ctrl+Shift+E"
        enabled: AppController.hasFile
        onTriggered: bar.expandRequested()
    }

    Action {
        id: collapseAction
        text: qsTr("Collapse All")
        shortcut: "Ctrl+Shift+C"
        enabled: AppController.hasFile
        onTriggered: bar.collapseRequested()
    }

    // Not checkable, for the same reason darkAction below is not: the bullet
    // follows the window's state through AppMenuItem.marked rather than the
    // row's own `checked`, which triggering the row would overwrite.
    Action {
        id: treeTagsAction
        text: qsTr("Tree Tags")
        shortcut: "Ctrl+T"
        onTriggered: bar.treeTagsRequested()
    }

    // Deliberately not a checkable Action: `checked` would have to be bound to
    // Theme.dark, and triggering the row would overwrite that binding. The
    // bullet follows Theme.dark directly instead, through AppMenuItem.marked.
    Action {
        id: darkAction
        text: qsTr("Dark Theme")
        shortcut: "Ctrl+D"
        onTriggered: Theme.dark = !Theme.dark
    }

    Action {
        id: aboutAction
        text: qsTr("About H5Scope…")
        onTriggered: bar.aboutRequested()
    }

    // --- the bar ---------------------------------------------------------
    MenuBar {
        id: menuStrip

        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom

        padding: 0
        // The bar paints its own ground and its own hairline; the control adds
        // nothing of its own on top.
        background: null

        // Titles are machine labels -- mono, uppercase, tracked -- and the one
        // whose drawer is open inverts to solid signal white, exactly as its
        // rows do.
        delegate: MenuBarItem {
            id: barItem

            /// Inverted while this title's drawer is open or the title is
            /// under the pointer. MenuBar sets `highlighted` on whichever item
            /// it has made current, which covers both as the user drives it;
            /// the drawer's own state is checked as well so a menu opened any
            /// other way cannot leave its title looking closed.
            readonly property bool inverted:
                barItem.highlighted
                || (barItem.menu !== null && barItem.menu.opened)

            implicitHeight: Theme.menuBarHeight
            leftPadding: Theme.gapL
            rightPadding: Theme.gapL
            topPadding: 0
            bottomPadding: 0

            background: Rectangle {
                color: barItem.inverted ? Theme.accent : Theme.clear(Theme.accent)
            }

            contentItem: Text {
                text: barItem.text
                font: Theme.label
                color: barItem.inverted ? Theme.accentText : Theme.textPrimary
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }

        AppMenu {
            title: qsTr("File")

            AppMenuItem { action: openAction }

            // What was opened before. A submenu rather than rows in the File
            // drawer itself: ten paths would be most of that drawer, and what
            // a reader is looking for there most of the time is one of the
            // four verbs above and below it.
            //
            // Its own title row is drawn by AppMenu's delegate, which is what
            // puts it on the same gutter as the rows around it and gives it
            // the system's caret rather than the Basic style's arrow -- and
            // what carries `enabled` below out to the row, so a drawer with
            // files in it stops arriving greyed.
            AppMenu {
                id: recentMenu

                title: qsTr("Open Recent")
                enabled: AppController.recentFiles.length > 0

                Instantiator {
                    // Rebuilt rather than bound row by row: opening a file
                    // moves it to the head of the list, so the row a given
                    // index names changes and there is nothing stable to bind.
                    model: AppController.recentFiles
                    onObjectAdded: (index, object) => recentMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => recentMenu.removeItem(object)

                    delegate: AppMenuItem {
                        required property var modelData
                        required property int index

                        // Numbered, because a menu of ten similar file names is
                        // read by position as much as by name. The folder rides
                        // in the shortcut column, which is where a row's second
                        // fact goes in this menu -- bounded, because one deep
                        // path would otherwise set the width of the drawer.
                        text: (index + 1) + "  " + modelData.name
                        shortcutText: modelData.missing ? qsTr("missing")
                                                        : modelData.folder
                        shortcutMaxWidth: Theme.s13 * 2
                        // A file that has moved is shown and refused rather
                        // than quietly dropped: a reader looking for something
                        // they had last week wants to be told it is gone.
                        enabled: !modelData.missing
                        onTriggered: bar.recentRequested(modelData.path)
                    }
                }

                AppMenuSeparator {}

                AppMenuItem {
                    text: qsTr("Clear Recent")
                    enabled: AppController.recentFiles.length > 0
                    onTriggered: AppController.clearRecentFiles()
                }
            }

            AppMenuItem { action: reloadAction }
            AppMenuItem { action: closeAction }
            AppMenuSeparator {}
            AppMenuItem { action: quitAction }
        }

        AppMenu {
            title: qsTr("View")

            AppMenuItem {
                action: infoTabAction
                marked: bar.currentTabId === "info"
            }
            AppMenuItem {
                action: tableTabAction
                marked: bar.currentTabId === "table"
            }
            AppMenuItem {
                action: plotTabAction
                marked: bar.currentTabId === "plot"
            }
            AppMenuItem {
                action: imageTabAction
                marked: bar.currentTabId === "image"
            }
            AppMenuSeparator {}
            AppMenuItem { action: expandAction }
            AppMenuItem { action: collapseAction }
            AppMenuItem {
                action: treeTagsAction
                marked: bar.treeTagsVisible
            }
            AppMenuSeparator {}
            AppMenuItem {
                action: darkAction
                marked: Theme.dark
            }
        }

        AppMenu {
            title: qsTr("Help")

            AppMenuItem { action: aboutAction }
        }
    }

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: Theme.borderWidth
        color: Theme.border
    }
}
