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

    /// The four the window has always had: one reading of whatever the tree
    /// has selected, apiece.
    readonly property var fixedTabs: [
        { id: "info",  label: qsTr("information") },
        { id: "table", label: qsTr("table") },
        { id: "plot",  label: qsTr("plot") },
        { id: "image", label: qsTr("image") }
    ]

    /// ...and the custom plots after them, which are about no selection at all.
    ///
    /// Ids are "custom:<position>" rather than the tab's name, because a name
    /// is the one thing about a custom tab the reader can change and an id
    /// that moved when they renamed it would not be an id. The position is
    /// stable enough for the same reason the strip is: CustomPlotSet keeps
    /// `activeIndex` on the same plot through a reorder, so the tab the reader
    /// was looking at is the tab they are still looking at.
    readonly property var tabs: {
        // Read so that the binding depends on it. `count` announces itself and
        // the rest of what is below does not: a rename and a tear-off are both
        // dataChanged on a row, which a function call cannot see.
        void window.tabRevision
        const all = window.fixedTabs.slice()
        for (let i = 0; i < customPlots.count; ++i) {
            // A tab showing in a window of its own has left the strip. One
            // plot lives in one place.
            if (customPlots.detached(i))
                continue
            const plot = customPlots.plotAt(i)
            all.push({ id: "custom:" + i,
                       label: plot ? plot.name : "",
                       custom: true,
                       index: i })
        }
        return all
    }

    /// Bumped by anything that changes the strip, because `tabs` above is
    /// built by a function and QML cannot see through one to the model
    /// underneath it.
    property int tabRevision: 0

    readonly property var customPlots: AppController.customPlots

    /// The torn-off windows, by position. Exposed because a Window is not in
    /// the item tree, so the QML suite -- which finds things by walking it --
    /// cannot reach one any other way.
    readonly property alias plotWindows: plotWindows

    /// Whether the information view is the one showing. Which of the other
    /// three is showing is DataView's own state and stays there: it is the
    /// thing that has to drop out of the plot when the selection turns out to
    /// be text, and a copy of that state up here could only disagree with it.
    ///
    /// Which *custom* tab is showing is CustomPlotSet's, and for a third
    /// reason: two things outside this window need it -- the tree's plus and
    /// the menu entry that names a time base -- and both would otherwise have
    /// to be handed the answer from here.
    property bool informationSelected: true

    /// Whether the tree draws the tags beside its names. View -> Tree Tags
    /// flips it.
    property bool treeTagsVisible: true

    /// Which tab is selected, by stable id rather than by position.
    readonly property string currentTabId:
        window.informationSelected ? "info"
        : window.customPlots.activeIndex >= 0
          ? "custom:" + window.customPlots.activeIndex
          : dataView.viewMode

    /// The plot and the image are for numbers; the table serves every datatype
    /// and the information view needs no dataset at all. An unavailable tab is
    /// shown greyed rather than removed, so the strip keeps its shape and the
    /// reader can see what this selection does not offer.
    function tabAvailable(id) {
        // A custom plot is about no dataset, so there is no selection that can
        // fail to offer it. That is the whole of why these tabs exist.
        if (id.startsWith("custom:"))
            return true
        return id === "info" || id === "table" || AppController.datasetIsNumeric
    }

    function selectTab(id) {
        if (!window.tabAvailable(id))
            return
        if (id.startsWith("custom:")) {
            window.informationSelected = false
            window.customPlots.activeIndex = parseInt(id.substring(7))
            return
        }
        window.customPlots.activeIndex = -1
        window.informationSelected = id === "info"
        if (id !== "info")
            dataView.show(id)
    }

    /// Make a tab, name it and show it. The "+" at the end of the strip, the
    /// View menu's entry and make-screenshots all come through here.
    function addCustomTab() {
        const index = window.customPlots.addPlot()
        window.selectTab("custom:" + index)
        return index
    }

    /// Put the strip back to its four.
    ///
    /// For tools/make-screenshots, which calls it between pictures for the
    /// same reason it collapses the tree between them: each picture should
    /// show what it names and nothing the picture before it left behind.
    function clearCustomTabs() {
        while (window.customPlots.count > 0)
            window.customPlots.removePlot(window.customPlots.count - 1)
    }

    /// Make a custom plot called `name` holding `expressions`, and show it.
    ///
    /// One function rather than four calls from outside, because the four have
    /// to happen in order and the caller that needs them -- the screenshot
    /// tool -- reaches this window through a JavaScript expression in its own
    /// context and can only make one call at a time.
    function buildCustomTab(name, expressions) {
        const index = window.addCustomTab()
        if (name !== "")
            window.customPlots.setName(index, name)
        const plot = window.customPlots.plotAt(index)
        for (let i = 0; i < expressions.length; ++i)
            plot.addExpression(expressions[i])
        return "custom:" + index
    }

    /// Make a tab out of a saved view.
    ///
    /// Checked before the tab is made rather than after, so a reader who
    /// changes their mind at the question is not left with an empty plot to
    /// close. The answer arrives at onViewChecked below.
    property string pendingView: ""

    function openViewInNewTab(name) {
        window.pendingView = name
        window.customPlots.checkView(name)
    }

    /// ...and the half of it that happens once the question is answered.
    function landView(name) {
        const index = window.addCustomTab()
        window.customPlots.restoreView(name, index)
    }

    /// Close the tab at `index`, and leave the reader somewhere sensible.
    function closeCustomTab(index) {
        const wasShowing = window.customPlots.activeIndex === index
        window.customPlots.removePlot(index)
        if (wasShowing && window.customPlots.activeIndex < 0)
            window.selectTab("table")
    }

    Connections {
        target: window.customPlots

        function onViewChecked(name, issues, reasons) {
            if (name !== window.pendingView)
                return
            window.pendingView = ""
            if (issues === 0) {
                window.landView(name)
                return
            }
            newTabWarning.viewName = name
            newTabWarning.issues = issues
            newTabWarning.reasons = reasons
            newTabWarning.open()
        }

        // A tab that comes back from its own window is the tab the reader was
        // just looking at, so the strip shows it rather than whatever it was
        // showing before they tore it off.
        function onActiveIndexChanged() {
            if (window.customPlots.activeIndex >= 0)
                window.informationSelected = false
        }

        function onNamesChanged() { window.tabRevision++ }
        function onCountChanged() { window.tabRevision++ }
        function onDataChanged() { window.tabRevision++ }
        function onModelReset() { window.tabRevision++ }
        // A reorder is rowsMoved and nothing else: no row was added, removed
        // or edited, and the strip still has to relabel itself.
        function onRowsMoved() { window.tabRevision++ }
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
        onNewCustomPlotRequested: window.addCustomTab()
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

                    objectName: "tabBar"

                    anchors.left: parent.left
                    anchors.leftMargin: Theme.gapS
                    height: parent.height

                    // Exposed for the QML tests, which assert tab visibility.
                    readonly property int count: window.tabs.length

                    /// Which custom tab a dragged one should land on, given
                    /// where its centre has got to along the strip.
                    ///
                    /// By the slot the centre is over rather than by an offset
                    /// in tab widths, because these tabs are not the same
                    /// width: they are named, and "Custom 1" and "pressure vs
                    /// time" are not the same number of pixels. A drop past
                    /// either end clamps to the tab at that end.
                    function dropTarget(centre) {
                        let first = -1
                        let last = -1
                        for (let i = 0; i < tabBar.children.length; ++i) {
                            const child = tabBar.children[i]
                            if (child.customIndex === undefined
                                || child.customIndex < 0)
                                continue
                            if (first < 0)
                                first = child.customIndex
                            last = child.customIndex
                            if (centre >= child.x && centre < child.x + child.width)
                                return child.customIndex
                        }
                        if (first < 0)
                            return -1
                        return centre < 0 ? first : last
                    }

                    Repeater {
                        model: window.tabs

                        delegate: AppTabButton {
                            id: tabButton

                            required property var modelData

                            /// Its place among the custom plots, or -1 for one
                            /// of the four the window has always had. Read by
                            /// dropTarget above, off the sibling list.
                            readonly property int customIndex:
                                modelData.custom === true ? modelData.index : -1

                            text: modelData.label
                            selected: window.currentTabId === modelData.id
                            enabled: window.tabAvailable(modelData.id)
                            closable: tabButton.customIndex >= 0
                            // The four fixed tabs are this program's own
                            // words; a custom one is the reader's, and the
                            // name box in its bar shows it back unchanged.
                            verbatimLabel: tabButton.customIndex >= 0
                            onClicked: window.selectTab(modelData.id)
                            onCloseRequested: window.closeCustomTab(tabButton.customIndex)

                            // Lifted while it is being carried, and put down
                            // between one frame and the next: this application
                            // animates nothing, so a dragged tab is where the
                            // pointer is and nowhere else.
                            transform: Translate { x: tabButton.carried }
                            z: dragger.active ? 1 : 0

                            /// How far the pointer has taken it from where it
                            /// sits. Zero unless it is being dragged.
                            property real carried: 0

                            DragHandler {
                                id: dragger

                                enabled: tabButton.customIndex >= 0
                                target: null
                                yAxis.enabled: false
                                cursorShape: active ? Qt.ClosedHandCursor
                                                    : Qt.ArrowCursor

                                // The travelled distance off the centroid
                                // rather than off activeTranslation, for the
                                // reason the pipeline's drag handle gives:
                                // activeTranslation is measured against a
                                // target this handler does not have.
                                onActiveChanged: {
                                    if (dragger.active)
                                        return
                                    const centre = tabButton.x + tabButton.carried
                                                 + tabButton.width / 2
                                    const to = tabBar.dropTarget(centre)
                                    tabButton.carried = 0
                                    if (to >= 0 && to !== tabButton.customIndex)
                                        window.customPlots.movePlot(tabButton.customIndex, to)
                                }
                                onCentroidChanged: {
                                    if (dragger.active) {
                                        tabButton.carried =
                                            dragger.centroid.scenePosition.x
                                            - dragger.centroid.scenePressPosition.x
                                    }
                                }
                            }
                        }
                    }

                    // The way to a tab that is about no dataset at all. At the
                    // end of the strip because that is where the tab it makes
                    // will appear, and because the four before it are fixed:
                    // a plus in front of them would read as adding one there.
                    AppTabButton {
                        objectName: "addCustomTab"

                        glyph: "plus"
                        // The accent, not the positive green. This one makes a
                        // *tab* -- it is navigation, like the four beside it,
                        // and the strip is drawn in one ink. Green is spent on
                        // the plus in the tree, which is the one that puts a
                        // dataset somewhere.
                        ink: Theme.accent
                        enabled: AppController.hasFile
                        onClicked: window.addCustomTab()
                    }

                    // ...and beside it, the same thing from an arrangement the
                    // reader has already built. A caret rather than a second
                    // glyph of its own: what it does is open a list, which is
                    // what a caret says everywhere else in this window.
                    AppTabButton {
                        id: viewsButton

                        objectName: "openSavedView"

                        glyph: "caret"
                        ink: Theme.accent
                        enabled: AppController.hasFile
                                 && AppController.customPlots.viewNames.length > 0
                        onClicked: savedViews.popup(viewsButton, 0,
                                                    viewsButton.height)

                        SavedViewsMenu {
                            id: savedViews

                            onPicked: (name) => window.openViewInNewTab(name)
                        }
                    }
                }
            }

            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: window.informationSelected ? 0
                            : window.customPlots.activeIndex >= 0 ? 2 : 1

                InfoView {}
                DataView {
                    id: dataView

                    // Named so the QML suite can reach the views' shared
                    // state, as the surfaces inside it are.
                    objectName: "dataView"
                }

                // A plain Item holding one CustomView per tab, `visible`-gated
                // rather than a second StackLayout: a StackLayout derives its
                // own size from every child it holds, and these are built and
                // torn down as the reader makes and closes tabs. DataView's
                // rail is arranged the same way for the same reason.
                Item {
                    id: customViews

                    objectName: "customViews"

                    Repeater {
                        model: window.customPlots

                        delegate: CustomView {
                            id: customView

                            required property int index
                            required property bool detached

                            anchors.fill: parent
                            objectName: "customView"
                            plot: window.customPlots.plotAt(customView.index)
                            plotIndex: customView.index
                            // Only the one on screen samples the file. A tab
                            // the reader is not looking at costs nothing, which
                            // is what lets there be a dozen of them.
                            active: !customView.detached
                                    && window.customPlots.activeIndex === customView.index
                            visible: customView.active
                        }
                    }
                }
            }
        }
    }

    // --- the torn-off plots ----------------------------------------------
    // An Instantiator rather than a Repeater, because a Repeater builds Items
    // and a Window is not one.
    //
    // One per plot, shown only while that plot is detached. A Window that is
    // never made visible is a QQuickWindow and no platform window at all, so
    // the ones the reader has not torn off cost an object apiece and nothing
    // on screen -- which is cheaper than tearing the view down and building it
    // again every time one is opened.
    Instantiator {
        id: plotWindows

        model: window.customPlots

        delegate: CustomPlotWindow {
            required property int index
            required property bool detached

            plot: window.customPlots.plotAt(index)
            plotIndex: index
            visible: detached
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

    // Asked here rather than in the tab, because what raises it is the tree --
    // a plus beside a row, or its menu -- and the tree is this window's.
    CrowdedPlotDialog {
        id: crowdedDialog
    }

    // The same question the data panel asks before restoring into an existing
    // tab, asked before making a new one out of a view.
    RestoreViewDialog {
        id: newTabWarning

        onAccepted: window.landView(newTabWarning.viewName)
    }

    /// Exposed for the QML suite, which cannot walk to a Popup.
    readonly property alias savedViewWarning: newTabWarning

    /// The question a whole dataset raises, exposed for the QML suite: a Popup
    /// is not in the item tree, so there is no walking to it.
    readonly property alias crowdedPlotDialog: crowdedDialog

    Connections {
        target: window.customPlots

        function onCrowdingWarned(index, path, lines) {
            crowdedDialog.plotIndex = index
            crowdedDialog.path = path
            crowdedDialog.lines = lines
            crowdedDialog.open()
        }
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
