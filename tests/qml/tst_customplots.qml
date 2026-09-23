// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import QtTest
import H5Scope
import H5Scope.Backend

/// The custom plot tabs, as chrome.
///
/// What a tab *holds* is asserted headless in tests/test_customplot.cpp, which
/// is where the slicing, the axis arithmetic and the costs belong. This is the
/// other half: that the strip grows a tab and loses one, that a tab is offered
/// whatever the tree has selected, that the name in the bar is the name in the
/// strip, and that the two rails put what the reader typed into the object
/// underneath.
TestCase {
    id: testCase
    name: "CustomPlots"
    when: windowShown
    width: 1280
    height: 820

    readonly property string fixture: TestFixture.path

    function openFixture() {
        verify(AppController.openFile(fixture), "the fixture must be accepted")
        tryVerify(() => AppController.hasFile && !AppController.busy, 10000,
                  "the fixture must finish opening")
        return AppController.hasFile
    }

    function select(path) {
        verify(AppController.selectPath(path))
        tryVerify(() => AppController.currentPath === path && !AppController.busy,
                  10000, "selecting " + path)
        wait(0)
        tryVerify(() => !AppController.busy, 10000, "settling after " + path)
        return true
    }

    /// Reads are coalesced onto a zero-timer and answered by the HDF5 thread,
    /// so anything that asserts on what a tab drew has to turn the loop as
    /// well as wait for the thread.
    function settleReads() {
        wait(0)
        tryVerify(() => !AppController.busy, 10000, "the tab must finish reading")
        wait(0)
        tryVerify(() => !AppController.busy, 10000, "and settle")
    }

    /// One controller serves the whole binary, so a suite that leaves state
    /// behind makes the next case depend on the last one.
    ///
    /// Re-opening the file empties the tabs, which is the behaviour under test
    /// in test_customplot and is used here as the reset. The saved views are
    /// *not* emptied by it -- they outlive the file on purpose -- so they are
    /// taken out by hand. Nothing reaches the reader's own settings doing it:
    /// the QML harness names no organisation, and CustomPlotSet reads and
    /// writes nothing until one is named.
    function init() {
        AppController.closeFile()
        // Copied first. A QStringList reaches QML as a sequence bound to the
        // property it came from, so reading an element re-reads the property
        // -- and removing while walking one skips every other entry.
        const views = AppController.customPlots.viewNames.slice()
        for (let i = 0; i < views.length; ++i)
            AppController.customPlots.removeView(views[i])
        verify(openFixture(), "the fixture must re-open")
    }

    function cleanupTestCase() {
        AppController.closeFile()
    }

    Component {
        id: windowComponent
        Main {}
    }

    /// The window, rendered once so its delegates exist.
    function openWindow() {
        const win = createTemporaryObject(windowComponent, testCase)
        verify(win, "the window must instantiate")
        waitForRendering(win.contentItem)
        return win
    }

    /// Every item under `root` with this objectName, in order.
    function findAllOf(root, name) {
        const found = []
        const visit = (item) => {
            if (item.objectName === name)
                found.push(item)
            for (let i = 0; i < item.children.length; ++i)
                visit(item.children[i])
        }
        visit(root)
        return found
    }

    /// Press the "restore" beside a named view, in this tab's data panel.
    ///
    /// Through the button rather than through CustomPlotSet.checkView, because
    /// arming the check is half of what the button does: the panel only acts on
    /// an answer about the view *it* asked about, so that two tabs with the
    /// panel open do not both restore when one of them was pressed.
    function restore(view, name) {
        const rows = findAllOf(view, "restoreView")
        for (let i = 0; i < rows.length; ++i) {
            if (rows[i].viewName === name) {
                mouseClick(rows[i])
                return true
            }
        }
        fail("no restore button for \"" + name + "\"")
        return false
    }

    /// The strip's tab buttons, in order. Found by what they are: a tab is
    /// the only thing in this window carrying both a selected flag and a
    /// verbatim-label one.
    function tabButtons(win) {
        const bar = findAllOf(win.contentItem, "tabBar")[0]
        const found = []
        for (let i = 0; i < bar.children.length; ++i) {
            const child = bar.children[i]
            if (child.selected !== undefined && child.verbatimLabel !== undefined)
                found.push(child)
        }
        return found
    }

    /// The CustomView that is actually on screen, or null.
    function shownView(win) {
        const views = findAllOf(win.contentItem, "customView")
        for (let i = 0; i < views.length; ++i) {
            if (views[i].visible)
                return views[i]
        }
        return null
    }

    // --- the strip ---------------------------------------------------------

    function test_the_strip_starts_at_four_and_grows_one_per_custom_plot() {
        const win = openWindow()

        compare(win.tabs.length, 4)
        compare(win.tabs.map((tab) => tab.id).join(","), "info,table,plot,image")

        const index = win.addCustomTab()
        compare(index, 0)
        waitForRendering(win.contentItem)

        compare(win.tabs.length, 5)
        compare(win.tabs[4].id, "custom:0")
        compare(win.tabs[4].label, "Custom 1")
        compare(win.currentTabId, "custom:0")
        compare(AppController.customPlots.activeIndex, 0)

        win.addCustomTab()
        waitForRendering(win.contentItem)
        compare(win.tabs.length, 6)
        compare(win.tabs[5].label, "Custom 2")
    }

    function test_the_plus_at_the_end_of_the_strip_makes_one() {
        const win = openWindow()
        const plus = findAllOf(win.contentItem, "addCustomTab")
        compare(plus.length, 1)
        verify(plus[0].enabled, "a file is open, so the plus is live")

        mouseClick(plus[0])
        waitForRendering(win.contentItem)

        compare(AppController.customPlots.count, 1)
        compare(win.currentTabId, "custom:0")
    }

    function test_a_custom_tab_is_offered_whatever_the_tree_has_selected() {
        const win = openWindow()
        win.addCustomTab()
        waitForRendering(win.contentItem)

        // A dataset of text: the plot and the image cannot show it and are
        // greyed, and the custom tab -- which is about no dataset at all --
        // is untouched.
        select("/str_vlen")
        waitForRendering(win.contentItem)

        verify(!win.tabAvailable("plot"), "a text dataset cannot be plotted")
        verify(!win.tabAvailable("image"))
        verify(win.tabAvailable("custom:0"), "a custom tab is about no dataset")

        win.selectTab("custom:0")
        compare(win.currentTabId, "custom:0")

        // ...and browsing on does not take the reader off it, where selecting
        // a text dataset does drop the data view out of the plot.
        select("/matrix")
        waitForRendering(win.contentItem)
        compare(win.currentTabId, "custom:0")
    }

    function test_closing_a_custom_tab_leaves_the_reader_somewhere() {
        const win = openWindow()
        win.addCustomTab()
        win.addCustomTab()
        waitForRendering(win.contentItem)
        compare(win.currentTabId, "custom:1")

        win.closeCustomTab(1)
        waitForRendering(win.contentItem)
        compare(AppController.customPlots.count, 1)
        compare(win.tabs.length, 5)

        win.closeCustomTab(0)
        waitForRendering(win.contentItem)
        compare(AppController.customPlots.count, 0)
        compare(win.tabs.length, 4)
        // The tab they were on is gone, so they are put on one that is there
        // rather than left looking at nothing.
        compare(win.currentTabId, "table")
    }

    function test_the_strip_relabels_itself_when_a_plot_is_renamed() {
        const win = openWindow()
        win.addCustomTab()
        waitForRendering(win.contentItem)

        compare(AppController.customPlots.setName(0, "pressure"), "")
        waitForRendering(win.contentItem)
        compare(win.tabs[4].label, "pressure")
    }

    function test_reordering_moves_the_tab_and_keeps_the_reader_on_it() {
        const win = openWindow()
        win.addCustomTab()
        win.addCustomTab()
        waitForRendering(win.contentItem)

        // The drop arithmetic is the strip's; the move itself is the set's,
        // and this asserts they agree about which tab the reader is on.
        AppController.customPlots.movePlot(1, 0)
        waitForRendering(win.contentItem)

        compare(win.tabs[4].label, "Custom 2")
        compare(win.tabs[5].label, "Custom 1")
        compare(win.currentTabId, "custom:0")
    }

    // --- the tab itself ----------------------------------------------------

    function test_the_name_in_the_bar_is_the_name_in_the_strip() {
        const win = openWindow()
        win.addCustomTab()
        waitForRendering(win.contentItem)

        const view = shownView(win)
        verify(view, "the tab must be showing")
        const field = findAllOf(view, "customNameField")[0]
        verify(field, "the bar must hold a name field")
        compare(field.text, "Custom 1")

        field.forceActiveFocus()
        field.text = "pressure vs time"
        field.textEdited()
        keyClick(Qt.Key_Return)
        waitForRendering(win.contentItem)

        compare(AppController.customPlots.plotAt(0).name, "pressure vs time")
        compare(win.tabs[4].label, "pressure vs time")
    }

    function test_a_name_already_taken_is_refused_and_said_so() {
        const win = openWindow()
        win.addCustomTab()
        win.addCustomTab()
        waitForRendering(win.contentItem)

        const field = findAllOf(shownView(win), "customNameField")[0]
        field.forceActiveFocus()
        field.text = "Custom 1"
        field.textEdited()
        waitForRendering(win.contentItem)

        // Checked as it is typed, before anything is applied.
        verify(field.invalid, "the box must say the name will not do")
        compare(AppController.customPlots.plotAt(1).name, "Custom 2")
    }

    function test_the_rails_open_one_at_a_time() {
        const win = openWindow()
        win.addCustomTab()
        waitForRendering(win.contentItem)

        const view = shownView(win)
        compare(view.rail, "")

        mouseClick(findAllOf(view, "customDataButton")[0])
        compare(view.rail, "data")

        mouseClick(findAllOf(view, "customPlotButton")[0])
        compare(view.rail, "plot")

        mouseClick(findAllOf(view, "customPlotButton")[0])
        compare(view.rail, "")
    }

    /// Put `text` in a box the way typing it would: the handler that checks it
    /// and asks what could come next runs off textEdited, which a plain
    /// assignment does not emit.
    function typeInto(box, text) {
        box.text = text
        box.textEdited()
    }

    function test_a_line_written_into_the_data_panel_is_drawn() {
        const win = openWindow()
        win.addCustomTab()
        waitForRendering(win.contentItem)

        const view = shownView(win)
        mouseClick(findAllOf(view, "customDataButton")[0])
        waitForRendering(win.contentItem)

        mouseClick(findAllOf(view, "addEntry")[0])
        waitForRendering(win.contentItem)

        const boxes = findAllOf(view, "entryBox")
        compare(boxes.length, 1)

        boxes[0].forceActiveFocus()
        boxes[0].text = "/series/a[:]"
        boxes[0].textEdited()
        keyClick(Qt.Key_Return)
        settleReads()
        waitForRendering(win.contentItem)

        const plot = AppController.customPlots.plotAt(0)
        compare(plot.sourceSeriesCount, 1)
        compare(plot.pointCount, 64)
        verify(plot.hasData, "the line must have been read")
        compare(findAllOf(view, "entryNote").filter((n) => n.visible).length, 0)
    }

    /// The names in a file are in the file and nowhere the reader can see
    /// them. Completion is what makes this box writable without the tree open
    /// beside it and a dimension count done by hand.
    function test_the_entry_box_completes_a_path_and_writes_its_subscript() {
        const win = openWindow()
        win.addCustomTab()
        waitForRendering(win.contentItem)

        const view = shownView(win)
        mouseClick(findAllOf(view, "customDataButton")[0])
        waitForRendering(win.contentItem)
        mouseClick(findAllOf(view, "addEntry")[0])
        waitForRendering(win.contentItem)

        const box = findAllOf(view, "entryBox")[0]
        box.forceActiveFocus()

        // The tree is lazy, so a group is asked for rather than walked and the
        // answer arrives a moment later. These waits are what the box does for
        // itself by re-asking on completionsChanged.
        typeInto(box, "/ser")
        tryVerify(() => AppController.completions("/ser").length === 1, 10000,
                  "the root's listing must arrive")

        // One match, and a group, so it completes with the separator: one Tab
        // and keep typing.
        keyClick(Qt.Key_Tab)
        compare(box.text, "/series/")

        typeInto(box, "/series/ha")
        tryVerify(() => AppController.completions("/series/ha")[0]
                        === "/series/half[:]", 10000,
                  "the group's listing and the dataset's rank must arrive")

        keyClick(Qt.Key_Tab)
        // A dataset completes with the subscript that selects the whole of it,
        // of the right rank. That is the half a reader would otherwise have to
        // count dimensions for.
        compare(box.text, "/series/half[:]")

        keyClick(Qt.Key_Return)
        settleReads()
        waitForRendering(win.contentItem)
        const plot = AppController.customPlots.plotAt(0)
        compare(plot.sourceSeriesCount, 1)
        verify(plot.hasData, "and what Tab wrote is a line that reads")
    }

    /// The other half of the same box: once the subscript is closed, what can
    /// follow it is the datatype's members.
    function test_the_entry_box_completes_a_member_after_the_bracket() {
        const win = openWindow()
        win.addCustomTab()
        waitForRendering(win.contentItem)

        const view = shownView(win)
        mouseClick(findAllOf(view, "customDataButton")[0])
        waitForRendering(win.contentItem)
        mouseClick(findAllOf(view, "addEntry")[0])
        waitForRendering(win.contentItem)

        const box = findAllOf(view, "entryBox")[0]
        box.forceActiveFocus()

        typeInto(box, "/series/trace_pairs[:]")
        tryVerify(() => AppController.completions("/series/trace_pairs[:]").length === 2,
                  10000, "the datatype must arrive")

        // Two members, so Tab writes the head they share and leaves the
        // choosing to the reader.
        keyClick(Qt.Key_Tab)
        compare(box.text, "/series/trace_pairs[:].")

        typeInto(box, "/series/trace_pairs[:].o")
        keyClick(Qt.Key_Tab)
        compare(box.text, "/series/trace_pairs[:].other")

        keyClick(Qt.Key_Return)
        settleReads()
        waitForRendering(win.contentItem)
        const plot = AppController.customPlots.plotAt(0)
        compare(plot.sourceSeriesCount, 1)
        verify(plot.hasData, "a member named by Tab is a line like any other")
    }

    function test_a_line_with_postprocessing_takes_a_pipeline_in_a_data_box() {
        const win = openWindow()
        win.addCustomTab()
        waitForRendering(win.contentItem)

        const view = shownView(win)
        mouseClick(findAllOf(view, "customDataButton")[0])
        waitForRendering(win.contentItem)
        mouseClick(findAllOf(view, "addEntry")[0])
        waitForRendering(win.contentItem)

        const box = findAllOf(view, "entryBox")[0]
        box.forceActiveFocus()
        box.text = "/matrix[:, 0]"
        box.textEdited()
        keyClick(Qt.Key_Return)
        settleReads()

        const label = findAllOf(view, "entryBoxLabel")[0]
        compare(label.text, "slice")
        const plot = AppController.customPlots.plotAt(0)
        compare(plot.pointCount, 4)

        // Ticked, the slice is rewritten as the script that says the same
        // thing, and the box that shows it is the one that takes several lines.
        mouseClick(findAllOf(view, "entryPostprocess")[0])
        settleReads()
        waitForRendering(win.contentItem)
        compare(label.text, "data", "it is not just a slice any more")
        verify(!box.visible)
        const script = findAllOf(view, "entryScript")[0]
        verify(script.visible)
        compare(script.text, "/matrix\n.slice(:, 0)")
        compare(plot.pointCount, 4, "and nothing drawn moved")

        // A pipeline that leaves more than a line is warned about as it is
        // typed, in the words of what it leaves.
        script.forceEditing()
        script.text = "/matrix\n.abs"
        waitForRendering(win.contentItem)
        verify(script.invalid, "the box must be marked")
        const notes = findAllOf(view, "entryNote").filter((n) => n.visible)
        compare(notes.length, 1)
        verify(notes[0].text.indexOf("one dimension") >= 0,
               "the reason must say what a line has to be: " + notes[0].text)

        // ...and one that reduces to a line is drawn.
        script.text = "/matrix\n.max(1)"
        verify(!script.invalid)
        keyClick(Qt.Key_Return)
        settleReads()
        compare(plot.maximum, 32)
        compare(script.text, "/matrix\n.max(1)")
    }

    function test_the_data_box_completes_a_path_and_writes_its_slice() {
        const win = openWindow()
        win.addCustomTab()
        waitForRendering(win.contentItem)

        const view = shownView(win)
        mouseClick(findAllOf(view, "customDataButton")[0])
        waitForRendering(win.contentItem)
        mouseClick(findAllOf(view, "addEntry")[0])
        waitForRendering(win.contentItem)
        mouseClick(findAllOf(view, "entryPostprocess")[0])
        waitForRendering(win.contentItem)

        const script = findAllOf(view, "entryScript")[0]
        verify(script.visible)
        script.forceEditing()
        script.text = "/series/ha"
        script.cursorPosition = script.text.length
        tryVerify(() => AppController.completions("/series/ha")[0] === "/series/half[:]",
                  10000, "the group's listing and the dataset's rank must arrive")

        // A dataset completes with the subscript that selects the whole of it,
        // and in a script that is the slice line under the path.
        keyClick(Qt.Key_Tab)
        compare(script.text, "/series/half\n.slice(:)")

        keyClick(Qt.Key_Return)
        settleReads()
        const plot = AppController.customPlots.plotAt(0)
        compare(plot.pointCount, 32, "and what Tab wrote is a line that reads")

        // A slice the reader has written already is theirs, and stays.
        script.forceEditing()
        script.text = "/series/ha\n.slice(0:4)"
        script.cursorPosition = "/series/ha".length
        keyClick(Qt.Key_Tab)
        compare(script.text, "/series/half\n.slice(0:4)")
    }

    function test_a_line_that_will_not_read_says_why_under_its_box() {
        const win = openWindow()
        win.addCustomTab()
        waitForRendering(win.contentItem)

        const view = shownView(win)
        mouseClick(findAllOf(view, "customDataButton")[0])
        waitForRendering(win.contentItem)
        mouseClick(findAllOf(view, "addEntry")[0])
        waitForRendering(win.contentItem)

        const box = findAllOf(view, "entryBox")[0]
        box.forceActiveFocus()
        box.text = "/matrix[:, :]"
        box.textEdited()
        keyClick(Qt.Key_Return)
        settleReads()
        waitForRendering(win.contentItem)

        verify(box.invalid, "the box must be marked")
        const notes = findAllOf(view, "entryNote").filter((n) => n.visible)
        compare(notes.length, 1)
        verify(notes[0].text.indexOf("one line") >= 0,
               "the reason must say what an entry has to be: " + notes[0].text)
    }

    function test_the_x_axis_offers_three_things_and_shows_the_one_chosen() {
        const win = openWindow()
        win.addCustomTab()
        waitForRendering(win.contentItem)

        const view = shownView(win)
        mouseClick(findAllOf(view, "customDataButton")[0])
        waitForRendering(win.contentItem)

        const plot = AppController.customPlots.plotAt(0)
        compare(plot.xMode, CustomPlot.Index)
        // The time-base box is only there when there is a time base to write.
        compare(findAllOf(view, "timeBaseBox").filter((b) => b.visible).length, 0)

        plot.xMode = CustomPlot.Dataset
        waitForRendering(win.contentItem)
        const boxes = findAllOf(view, "timeBaseBox").filter((b) => b.visible)
        compare(boxes.length, 1)

        boxes[0].forceActiveFocus()
        boxes[0].text = "/series/time[:]"
        boxes[0].textEdited()
        keyClick(Qt.Key_Return)
        settleReads()

        compare(plot.xExpression, "/series/time[:]")
        verify(plot.xReady, "the time base must have been read")
    }

    function test_a_view_saved_from_one_tab_goes_into_another() {
        const win = openWindow()
        win.addCustomTab()
        waitForRendering(win.contentItem)

        const first = AppController.customPlots.plotAt(0)
        first.addExpression("/series/a[:]")
        settleReads()

        const view = shownView(win)
        mouseClick(findAllOf(view, "customDataButton")[0])
        waitForRendering(win.contentItem)

        const nameBox = findAllOf(view, "viewName")[0]
        nameBox.forceActiveFocus()
        nameBox.text = "just a"
        nameBox.textEdited()
        keyClick(Qt.Key_Return)
        waitForRendering(win.contentItem)

        compare(AppController.customPlots.viewNames.length, 1)
        compare(AppController.customPlots.viewNames[0], "just a")

        // Into a fresh tab, through the same panel.
        win.addCustomTab()
        waitForRendering(win.contentItem)
        const second = shownView(win)
        mouseClick(findAllOf(second, "customDataButton")[0])
        waitForRendering(win.contentItem)

        restore(second, "just a")
        settleReads()
        waitForRendering(win.contentItem)

        const restored = AppController.customPlots.plotAt(1)
        compare(restored.sourceSeriesCount, 1)
        compare(restored.seriesLabel(0), "/series/a[:]")
    }

    function test_a_view_that_no_longer_fits_asks_before_it_lands() {
        const win = openWindow()
        win.addCustomTab()
        waitForRendering(win.contentItem)

        const plot = AppController.customPlots.plotAt(0)
        plot.addExpression("/series/a[:]")
        plot.addExpression("/gone_away[:]")
        settleReads()

        compare(AppController.customPlots.saveView("stale", 0, ({})), "")

        const view = shownView(win)
        mouseClick(findAllOf(view, "customDataButton")[0])
        waitForRendering(win.contentItem)

        restore(view, "stale")
        settleReads()
        waitForRendering(win.contentItem)

        const panel = findAllOf(view, "customDataPanel")[0]
        verify(panel, "the tab must have a data panel")
        const warning = panel.restoreDialog
        verify(warning.visible, "the warning must be in front of the reader")
        compare(warning.issues, 1)
        verify(warning.reasons[0].indexOf("/gone_away[:]") >= 0,
               "the reason must name the line: " + warning.reasons[0])

        // Nothing landed while the question was open.
        compare(AppController.customPlots.plotAt(0).sourceSeriesCount, 2)
    }

    // --- getting a dataset in from outside the tab -------------------------

    function test_the_tree_grows_a_plus_while_a_custom_tab_is_open() {
        const win = openWindow()

        // Nothing to add to, so nothing offering to add.
        compare(findAllOf(win.contentItem, "addToPlot")
                .filter((p) => p.visible).length, 0)

        win.addCustomTab()
        waitForRendering(win.contentItem)
        tryVerify(() => !AppController.busy, 10000, "the tree must settle")
        waitForRendering(win.contentItem)

        const pluses = findAllOf(win.contentItem, "addToPlot")
                       .filter((p) => p.visible)
        verify(pluses.length > 0, "every dataset row must offer the plus")

        // And it is green, which is the one thing in this pane drawn in a
        // colour that is not a warning.
        compare(String(pluses[0].ink), String(Theme.positive))
    }

    function test_the_plus_puts_the_dataset_into_the_tab_on_screen() {
        const win = openWindow()
        win.addCustomTab()
        waitForRendering(win.contentItem)

        AppController.customPlots.addDatasetTo(0, "/series/a")
        settleReads()

        const plot = AppController.customPlots.plotAt(0)
        compare(plot.sourceSeriesCount, 1)
        compare(plot.seriesLabel(0), "/series/a[:]")
        compare(plot.pointCount, 64)
    }

    function test_a_line_of_the_plot_tab_can_be_taken_to_a_custom_plot() {
        const win = openWindow()
        win.addCustomTab()
        select("/cube")
        win.selectTab("plot")
        waitForRendering(win.contentItem)

        // The plot tab draws six lines of a 2 x 3 x 4, and each of them is a
        // slice of one dimension -- which is what the legend's menu offers.
        const plot = AppController.datasetPlot
        compare(plot.sourceSeriesCount, 6)
        compare(plot.seriesExpression(0), "/cube[0, 0, :]")
        compare(plot.seriesExpression(5), "/cube[1, 2, :]")

        AppController.customPlots.plotAt(0).addExpression(plot.seriesExpression(3))
        settleReads()

        const custom = AppController.customPlots.plotAt(0)
        compare(custom.sourceSeriesCount, 1)
        compare(custom.pointCount, 4)
        verify(custom.hasData, "the line must have been read")
    }

    function test_a_post_processed_line_has_no_path_to_record() {
        const win = openWindow()
        win.addCustomTab()
        select("/hypercube")
        win.selectTab("plot")
        waitForRendering(win.contentItem)

        const legends = findAllOf(win.contentItem, "plotLegend")
                        .filter((l) => l.offersCustom)
        compare(legends.length, 1)
        const legend = legends[0]

        // With nothing done to it, a line of the plot tab is a slice of the
        // file and can be taken away.
        compare(legend.customRefusal(0), "")

        const pipeline = AppController.postprocessModel
        pipeline.enabled = true
        pipeline.addStep("max")
        pipeline.setArgument(2, "0, 1")
        settleReads()
        waitForRendering(win.contentItem)

        verify(AppController.postprocessActive,
               "the pipeline must be running for this to be the case under test")

        // What the legend is listing now is a computed array: it has no path,
        // and an entry is a path and nothing else. Refused with its reason
        // rather than quietly recording the raw slice underneath, which is
        // not the line that was clicked.
        const refusal = legend.customRefusal(0)
        verify(refusal.indexOf("post-processed") >= 0,
               "the reason must say why: " + refusal)

        pipeline.enabled = false
        settleReads()
        waitForRendering(win.contentItem)
        compare(legend.customRefusal(0), "")
    }

    function test_a_line_that_is_not_a_slice_of_one_dimension_is_not_offered() {
        const win = openWindow()
        win.addCustomTab()
        select("/hypercube")
        win.selectTab("plot")
        waitForRendering(win.contentItem)

        // A rank-4 table spreads three dimensions down its rows and one along
        // its columns, so every line is a slice of one dimension and each is
        // offered.
        const legend = findAllOf(win.contentItem, "plotLegend")
                       .filter((l) => l.offersCustom)[0]
        compare(legend.customRefusal(0), "")

        // Put a second dimension on the columns and a line stops being a
        // hyperslab of anything: its points run over the product of two.
        const setup = AppController.tableSetupModel
        setup.setAxis(2, true)
        settleReads()
        waitForRendering(win.contentItem)

        const refusal = legend.customRefusal(0)
        verify(refusal.indexOf("one dimension") >= 0,
               "the reason must say what it is not: " + refusal)
    }

    // --- a window of its own -----------------------------------------------

    function test_a_torn_off_plot_leaves_the_strip_and_comes_back() {
        const win = openWindow()
        win.addCustomTab()
        waitForRendering(win.contentItem)
        compare(win.tabs.length, 5)

        const plot = AppController.customPlots.plotAt(0)
        plot.addExpression("/series/a[:]")
        settleReads()

        AppController.customPlots.setDetached(0, true)
        waitForRendering(win.contentItem)

        // One plot lives in one place: while its own window is up the strip
        // does not list it, and the strip is no longer showing it.
        verify(AppController.customPlots.detached(0))
        compare(win.tabs.length, 4)
        compare(AppController.customPlots.activeIndex, -1)
        // The plot itself is untouched -- the window is where it is being
        // shown, not where it lives.
        compare(plot.sourceSeriesCount, 1)
        compare(plot.pointCount, 64)

        AppController.customPlots.setDetached(0, false)
        waitForRendering(win.contentItem)

        compare(win.tabs.length, 5)
        compare(win.tabs[4].label, "Custom 1")
        compare(plot.sourceSeriesCount, 1)
    }

    function test_the_torn_off_window_draws_the_same_plot() {
        const win = openWindow()
        win.addCustomTab()
        waitForRendering(win.contentItem)

        const plot = AppController.customPlots.plotAt(0)
        plot.addExpression("/series/a[:]")
        settleReads()

        AppController.customPlots.setDetached(0, true)
        waitForRendering(win.contentItem)
        wait(0)

        // A Window is not in the item tree, so it is reached through the
        // Instantiator that made it rather than by walking children.
        const torn = win.plotWindows.objectAt(0)
        verify(torn, "there must be a window for the plot")
        verify(torn.visible, "the torn-off window must be on screen")
        compare(torn.plotIndex, 0)
        verify(torn.title.indexOf("Custom 1") >= 0,
               "the window is named for the plot: " + torn.title)

        // ...and the view inside it is the same plot, drawn.
        const inside = findAllOf(torn.contentItem, "customWindowView")
        compare(inside.length, 1)
        compare(inside[0].plot, plot)
        verify(inside[0].detached, "the view knows where it is")

        // The control that took the plot out of the strip is the one a reader
        // looks for to put it back, so it is in the same place, reversed.
        const back = findAllOf(inside[0], "detachPlot")
                     .filter((b) => b.visible)
        compare(back.length, 1)
        compare(back[0].glyph, "attach")

        AppController.customPlots.setDetached(0, false)
        waitForRendering(win.contentItem)
        verify(!torn.visible, "and it goes away when the tab comes back")
    }

    function test_the_view_menu_lists_the_custom_plots_and_makes_one() {
        const win = openWindow()
        const bar = win.menuBar
        verify(bar, "the window must have a menu bar")

        bar.newCustomPlotRequested()
        waitForRendering(win.contentItem)
        compare(AppController.customPlots.count, 1)
        compare(win.currentTabId, "custom:0")

        compare(AppController.customPlots.setName(0, "pressure"), "")
        waitForRendering(win.contentItem)

        // The drawer lists it after the four fixed rows, and marks it while
        // it is the tab on screen.
        const row = bar.customTabRow(0)
        verify(row, "the View menu must list the custom plot")
        compare(row.text, "pressure")
        verify(row.marked, "and mark the one showing")

        win.selectTab("table")
        waitForRendering(win.contentItem)
        verify(!row.marked, "and stop marking it when it is not")
    }

    function test_a_named_tab_reads_the_way_the_name_box_shows_it() {
        const win = openWindow()
        win.addCustomTab()
        waitForRendering(win.contentItem)

        const field = findAllOf(shownView(win), "customNameField")[0]
        field.forceActiveFocus()
        field.text = "morning vs afternoon"
        field.textEdited()
        keyClick(Qt.Key_Return)
        waitForRendering(win.contentItem)

        const buttons = tabButtons(win)
        // Four fixed, then the plus, then this one -- and the plus carries no
        // label at all, so it is filtered out by having one.
        const named = buttons.filter((b) => b.text === "morning vs afternoon")
        verify(named.length === 1,
               "the strip holds [" + buttons.map((b) => b.text).join("|") + "]")

        // The contract: the box and the strip say the same string the same
        // way. The four this program named itself keep the uppercase, because
        // that is what says they are a fixed vocabulary rather than a phrase.
        verify(named[0].verbatimLabel, "a named tab is drawn as it was written")
        compare(named[0].font.capitalization, Font.MixedCase)
        compare(field.text, named[0].text)

        const information = buttons.filter((b) => b.text === "information")
        verify(information.length === 1,
               "the strip holds [" + buttons.map((b) => b.text).join("|") + "]")
        verify(!information[0].verbatimLabel)
        compare(information[0].font.capitalization, Font.AllUppercase)

        // Everything else about the two is the same, so a named tab still
        // sits in the strip as a tab.
        compare(named[0].font.pixelSize, information[0].font.pixelSize)
        compare(named[0].font.family, information[0].font.family)
        compare(named[0].font.letterSpacing, information[0].font.letterSpacing)
    }

    // --- how many lines is too many ---------------------------------------

    function test_a_whole_dataset_that_is_a_great_many_lines_asks_first() {
        const win = openWindow()
        win.addCustomTab()
        waitForRendering(win.contentItem)

        // /compressed is 100 x 100, so a hundred lines -- past the point where
        // strokes over one another stop separating.
        AppController.customPlots.addDatasetTo(0, "/compressed")
        settleReads()
        waitForRendering(win.contentItem)

        const asked = win.crowdedPlotDialog
        verify(asked.visible, "the question must be in front of the reader")
        compare(asked.lines, 100)
        compare(asked.path, "/compressed")
        compare(asked.plotIndex, 0)
        // Nothing landed while it was open.
        compare(AppController.customPlots.plotAt(0).sourceSeriesCount, 0)

        asked.accept()
        settleReads()
        waitForRendering(win.contentItem)

        // Every one of them, not sixty-four of them: a silent clip is a
        // picture that looks complete and is not.
        compare(AppController.customPlots.plotAt(0).sourceSeriesCount, 100)
    }

    function test_cancelling_leaves_the_plot_as_it_was() {
        const win = openWindow()
        win.addCustomTab()
        waitForRendering(win.contentItem)

        AppController.customPlots.addDatasetTo(0, "/series/a")
        settleReads()
        compare(AppController.customPlots.plotAt(0).sourceSeriesCount, 1)

        AppController.customPlots.addDatasetTo(0, "/compressed")
        settleReads()
        waitForRendering(win.contentItem)
        win.crowdedPlotDialog.reject()
        settleReads()

        compare(AppController.customPlots.plotAt(0).sourceSeriesCount, 1)
    }

    function test_a_hand_written_line_is_never_questioned() {
        const win = openWindow()
        win.addCustomTab()
        waitForRendering(win.contentItem)

        // One at a time is one decision at a time, however many of them there
        // are -- the question is about a dataset arriving whole.
        const plot = AppController.customPlots.plotAt(0)
        for (let i = 0; i < 70; ++i)
            plot.addExpression("/series/a[:]")
        settleReads()
        waitForRendering(win.contentItem)

        compare(plot.sourceSeriesCount, 70)
        verify(!win.crowdedPlotDialog.visible, "nothing to ask about")
    }

    function test_the_save_box_opens_on_the_name_of_the_tab() {
        const win = openWindow()
        win.addCustomTab()
        waitForRendering(win.contentItem)

        const view = shownView(win)
        mouseClick(findAllOf(view, "customDataButton")[0])
        waitForRendering(win.contentItem)

        const box = findAllOf(view, "viewName")[0]
        verify(box, "the panel must hold a save box")
        compare(box.text, "Custom 1")

        // ...and follows it, until the reader writes something else.
        compare(AppController.customPlots.setName(0, "morning vs afternoon"), "")
        waitForRendering(win.contentItem)
        compare(box.text, "morning vs afternoon")

        // Saving puts the tab's name back rather than emptying the box: what
        // it states is what saving now would be called.
        const plot = AppController.customPlots.plotAt(0)
        plot.addExpression("/series/a[:]")
        settleReads()
        box.forceActiveFocus()
        box.text = "just a"
        box.textEdited()
        keyClick(Qt.Key_Return)
        waitForRendering(win.contentItem)

        compare(AppController.customPlots.viewNames[0], "just a")
        compare(box.text, "morning vs afternoon")
    }

    function test_a_restored_view_brings_the_tab_title_with_it() {
        const win = openWindow()
        win.addCustomTab()
        waitForRendering(win.contentItem)
        compare(AppController.customPlots.setName(0, "morning"), "")

        const plot = AppController.customPlots.plotAt(0)
        plot.addExpression("/series/a[:]")
        settleReads()
        compare(AppController.customPlots.saveView("named", 0, ({})), "")

        win.addCustomTab()
        waitForRendering(win.contentItem)
        const second = shownView(win)
        mouseClick(findAllOf(second, "customDataButton")[0])
        waitForRendering(win.contentItem)

        restore(second, "named")
        settleReads()
        waitForRendering(win.contentItem)

        // The tab it was saved from still holds that name, so this one takes
        // the first free variant -- and the strip says so.
        compare(AppController.customPlots.plotAt(1).name, "morning 2")
        compare(win.tabs[5].label, "morning 2")
    }

    function test_the_tick_beside_a_line_takes_it_out_of_the_picture() {
        const win = openWindow()
        win.addCustomTab()
        waitForRendering(win.contentItem)

        const plot = AppController.customPlots.plotAt(0)
        plot.addExpression("/series/a[:]")
        plot.addExpression("/series/b[:]")
        settleReads()

        const view = shownView(win)
        mouseClick(findAllOf(view, "customDataButton")[0])
        waitForRendering(win.contentItem)

        // The same question the legend's tick asks, written on the row the
        // reader is already editing.
        const ticks = findAllOf(view, "entryDrawn")
        compare(ticks.length, 2)
        verify(ticks[0].checked && ticks[1].checked, "both start drawn")

        plot.setSeriesVisible(1, false)
        waitForRendering(win.contentItem)
        verify(ticks[0].checked)
        verify(!ticks[1].checked, "the row follows the plot")

        mouseClick(ticks[1])
        waitForRendering(win.contentItem)
        verify(plot.seriesVisible(1), "and the plot follows the row")
        compare(plot.seriesCount, 2)
    }

    /// Taking a line out of the picture leaves the others the colour they
    /// were.
    ///
    /// Reported from use, and a custom tab is where it bites: the lines are a
    /// handful the reader put together deliberately, and unticking one to look
    /// underneath it recoloured the rest. The palette was being asked where a
    /// line sat among the *drawn* ones rather than which line of the tab it
    /// was, so hiding the first of three handed the second the first one's
    /// colour. A palette exists to answer "which line is this" and nothing
    /// else; see the note on PlotSurface.seriesColor.
    function test_hiding_a_line_does_not_recolour_the_ones_left() {
        const win = openWindow()
        win.addCustomTab()
        waitForRendering(win.contentItem)

        const plot = AppController.customPlots.plotAt(0)
        plot.addExpression("/series/a[:]")
        plot.addExpression("/series/b[:]")
        plot.addExpression("/series/time[:]")
        settleReads()
        waitForRendering(win.contentItem)

        const view = shownView(win)
        const surface = findAllOf(view, "customPlotSurface")[0]
        const lines = findAllOf(view, "plotLines")[0]
        verify(lines, "the drawn lines must be reachable")
        compare(surface.colorMode, "okabe-ito")
        tryVerify(() => lines.lineCount() === 3, 5000, "three lines are drawn")

        const before = []
        for (let i = 0; i < 3; ++i)
            before.push(String(lines.seriesColor(i)))
        verify(before[0] !== before[1] && before[1] !== before[2],
               "a palette must give three lines three colours")

        // The middle one goes. The last one moves up a place in what the item
        // is handed, and must not take the colour of the line that left.
        plot.setSeriesVisible(1, false)
        settleReads()
        tryVerify(() => lines.lineCount() === 2, 5000, "one line goes")
        compare(String(lines.seriesColor(0)), before[0])
        compare(String(lines.seriesColor(1)), before[2],
                "the third line keeps its own colour")

        plot.setSeriesVisible(1, true)
        settleReads()
        tryVerify(() => lines.lineCount() === 3, 5000, "and comes back")
        for (let i = 0; i < 3; ++i) {
            compare(String(lines.seriesColor(i)), before[i],
                    "line " + i + " must be back where it started")
        }
    }

    /// A second line leaves the first as it was.
    ///
    /// The Plot tab draws a bundle under full strength so that where its rows
    /// pile up can be seen. A custom tab's lines were each put there on
    /// purpose, and drawing them that way meant adding a second line dimmed
    /// both -- to 0.55 on the dark theme.
    function test_a_second_line_does_not_dim_the_first() {
        const win = openWindow()
        win.addCustomTab()
        waitForRendering(win.contentItem)

        const plot = AppController.customPlots.plotAt(0)
        plot.addExpression("/series/a[:]")
        settleReads()
        waitForRendering(win.contentItem)

        const view = shownView(win)
        const lines = findAllOf(view, "plotLines")[0]
        verify(lines, "the drawn lines must be reachable")
        tryVerify(() => lines.lineCount() === 1, 5000, "one line is drawn")
        const alone = lines.seriesOpacity(0)
        compare(alone, 1.0, "a line on its own is drawn at full strength")

        plot.addExpression("/series/b[:]")
        settleReads()
        tryVerify(() => lines.lineCount() === 2, 5000, "two lines are drawn")
        compare(lines.seriesOpacity(0), alone, "the first line keeps its strength")
        compare(lines.seriesOpacity(1), alone, "and the second takes the same")
    }

    // --- a line the reader colours -----------------------------------------

    /// A colour the reader gives one line beats the cycle, and only for that
    /// line.
    ///
    /// The cycle answers "which line is this" for lines that are alike. A
    /// custom tab's are not: they were each put there on purpose and often
    /// mean different things, so a reader drawing temperature against pressure
    /// has a colour in mind for each that no cycle is going to guess. What has
    /// to hold is that saying so about one line says nothing about the others
    /// -- the same promise a palette makes about its own index, and for the
    /// same reason.
    function test_a_line_takes_the_colour_the_reader_gave_it() {
        const win = openWindow()
        win.addCustomTab()
        waitForRendering(win.contentItem)

        const plot = AppController.customPlots.plotAt(0)
        plot.addExpression("/series/a[:]")
        plot.addExpression("/series/b[:]")
        plot.addExpression("/series/time[:]")
        settleReads()
        waitForRendering(win.contentItem)

        const view = shownView(win)
        const surface = findAllOf(view, "customPlotSurface")[0]
        const lines = findAllOf(view, "plotLines")[0]
        verify(surface && lines, "the surface and its lines must be reachable")
        tryVerify(() => lines.lineCount() === 3, 5000, "three lines are drawn")

        const before = []
        for (let i = 0; i < 3; ++i)
            before.push(String(lines.seriesColor(i)))
        verify(before[0] !== before[1] && before[1] !== before[2])

        // Nothing given yet, which is a different answer from "transparent".
        compare(plot.seriesOverride(1), undefined)

        plot.setEntryColor(1, "#ff00ff")
        settleReads()
        tryVerify(() => String(lines.seriesColor(1)) === "#ff00ff", 5000,
                  "the line must take the colour it was given")
        compare(String(lines.seriesColor(0)), before[0],
                "the line above must be where it was")
        compare(String(lines.seriesColor(2)), before[2],
                "and the line below")

        // The legend and the card ask the surface rather than the item, so
        // that is asserted too: a swatch that disagreed with the stroke beside
        // it would be the one thing a legend may never be.
        compare(String(surface.seriesColor(1, 1, 3)), "#ff00ff")

        // A cycle changed under it leaves it alone -- it is an override and
        // not a seat in the cycle -- and moves the two that have none.
        plot.clearEntryColor(2)
        surface.colorMode = "safe"
        settleReads()
        tryVerify(() => String(lines.seriesColor(0)) !== before[0], 5000,
                  "the lines without a colour follow the cycle")
        compare(String(lines.seriesColor(1)), "#ff00ff")

        surface.colorMode = "okabe-ito"
        settleReads()

        // ...and clearing gives the line back to the cycle rather than
        // freezing whatever the cycle happened to say.
        plot.clearEntryColor(1)
        settleReads()
        tryVerify(() => String(lines.seriesColor(1)) === before[1], 5000,
                  "clearing must give the line back to the cycle")
        compare(plot.seriesOverride(1), undefined)
    }

    /// ...and the card in the data rail is one of the two places to say it.
    function test_the_entry_card_offers_the_line_a_colour() {
        const win = openWindow()
        win.addCustomTab()
        waitForRendering(win.contentItem)

        const plot = AppController.customPlots.plotAt(0)
        plot.addExpression("/series/a[:]")
        settleReads()

        const view = shownView(win)
        mouseClick(findAllOf(view, "customDataButton")[0])
        waitForRendering(win.contentItem)

        const swatch = findAllOf(view, "entryColour")[0]
        const clear = findAllOf(view, "entryColourClear")[0]
        verify(swatch && clear, "the card must offer a colour")

        // The swatch shows what the line is drawn in whether or not the reader
        // chose it, because a blank swatch would say the line has no colour
        // and that is never true.
        const surface = findAllOf(view, "customPlotSurface")[0]
        compare(String(swatch.value), String(surface.seriesColor(0, 0, 1)))
        verify(!clear.enabled, "there is nothing to clear yet")

        plot.setEntryColor(0, "#00ff88")
        settleReads()
        waitForRendering(win.contentItem)
        compare(String(swatch.value), "#00ff88")
        verify(clear.enabled, "and now there is")

        mouseClick(clear)
        settleReads()
        waitForRendering(win.contentItem)
        compare(plot.seriesOverride(0), undefined)
        verify(!clear.enabled)
    }

    // --- the legend's own menu ---------------------------------------------

    function test_the_line_menu_carries_no_blank_row() {
        const win = openWindow()
        win.addCustomTab()
        select("/cube")
        win.selectTab("plot")
        waitForRendering(win.contentItem)

        const legend = findAllOf(win.contentItem, "plotLegend")
                       .filter((l) => l.offersCustom)[0]
        const menu = legend.lineMenu
        menu.series = 0

        // Opened, because a Menu lays its rows out only when it is: the whole
        // question is what the drawer looks like in front of a reader.
        menu.popup()
        tryVerify(() => menu.opened, 5000, "the drawer must open")
        waitForRendering(win.contentItem)

        // Nothing to refuse, so the row that would have said why takes no
        // height at all. An invisible item is laid out as a full-height blank
        // line otherwise, which is what the drawer was showing.
        compare(legend.customRefusal(0), "")
        compare(menu.count, 4)
        verify(!menu.itemAt(1).visible, "there is nothing to explain")
        compare(menu.itemAt(1).height, 0)
        // The two colour rows are the other drawer this one menu carries, and
        // this legend is the plot tab's. The two offers are exclusive by
        // nature -- a line here is a row of one dataset, which the cycle
        // already tells apart -- so the drawer is never a list of one thing
        // that works and two that do not.
        verify(!menu.itemAt(2).visible, "a plot-tab line takes no colour")
        verify(!menu.itemAt(3).visible)
        compare(menu.itemAt(2).height, 0)
        compare(menu.itemAt(3).height, 0)
        // ...and the drawer is exactly as tall as its one real row.
        compare(menu.contentItem.contentHeight, menu.itemAt(0).height)
        menu.close()

        // ...and when there is, the row is there and says it, and the thing it
        // is about is greyed rather than hidden.
        AppController.postprocessModel.enabled = true
        AppController.postprocessModel.addStep("max")
        AppController.postprocessModel.setArgument(2, "0")
        settleReads()
        waitForRendering(win.contentItem)

        verify(AppController.postprocessActive)
        menu.popup()
        tryVerify(() => menu.opened, 5000, "the drawer must open again")
        waitForRendering(win.contentItem)

        verify(menu.itemAt(1).visible, "the reason must be there")
        verify(menu.itemAt(1).height > 0)
        verify(!menu.itemAt(0).enabled, "and the option greyed out")
        verify(menu.itemAt(1).text.indexOf("post-processed") >= 0)
        menu.close()

        AppController.postprocessModel.enabled = false
        settleReads()
    }

    function test_an_alias_is_what_the_legend_calls_the_line() {
        const win = openWindow()
        win.addCustomTab()
        waitForRendering(win.contentItem)

        const plot = AppController.customPlots.plotAt(0)
        plot.addExpression("/series/a[:]")
        settleReads()

        const view = shownView(win)
        mouseClick(findAllOf(view, "customDataButton")[0])
        waitForRendering(win.contentItem)

        const alias = findAllOf(view, "entryAlias")[0]
        verify(alias, "the card must offer a name")
        compare(alias.text, "")

        alias.forceActiveFocus()
        alias.text = "morning"
        keyClick(Qt.Key_Return)
        waitForRendering(win.contentItem)

        compare(plot.seriesLabel(0), "morning")
        // The slice is what is actually read, so the box above still holds it.
        compare(findAllOf(view, "entryBox")[0].text, "/series/a[:]")
        compare(plot.pointCount, 64)
    }

    function test_the_scaling_row_is_only_there_when_it_decides_something() {
        const win = openWindow()
        win.addCustomTab()
        waitForRendering(win.contentItem)

        const plot = AppController.customPlots.plotAt(0)
        plot.addExpression("/series/a[:]")
        settleReads()

        const view = shownView(win)
        mouseClick(findAllOf(view, "customDataButton")[0])
        waitForRendering(win.contentItem)

        // One line, so it is the axis's own length: align and stretch put the
        // points in the same places and there is nothing to choose.
        compare(findAllOf(view, "scalingAlign").filter((r) => r.visible).length, 0)

        // A second line half as long, and the question means something.
        plot.addExpression("/series/half[:]")
        settleReads()
        waitForRendering(win.contentItem)

        const shown = findAllOf(view, "scalingAlign").filter((r) => r.visible)
        compare(shown.length, 1)
        verify(shown[0].checked, "align is where a line starts")

        const stretch = findAllOf(view, "scalingStretch").filter((r) => r.visible)
        compare(stretch.length, 1)
        mouseClick(stretch[0])
        waitForRendering(win.contentItem)
        compare(plot.data(plot.index(1, 0), CustomPlot.ScalingRole),
                CustomPlot.Stretch)
    }

    function test_a_legend_name_loses_its_path_before_its_name() {
        const win = openWindow()
        win.addCustomTab()
        waitForRendering(win.contentItem)

        const plot = AppController.customPlots.plotAt(0)
        plot.addExpression("/group/nested/leaf[:]")
        settleReads()

        const view = shownView(win)
        const surface = findAllOf(view, "customPlotSurface")[0]
        surface.legendOpen = true
        waitForRendering(win.contentItem)

        const legend = findAllOf(view, "plotLegend")[0]
        compare(legend.width, Theme.railWidth)

        // The group a line sits in is usually the same for every line in the
        // list, so it is the part that carries no information and the part to
        // lose. What is left keeps the name and the subscript.
        compare(legend.withoutPath("/group/nested/leaf[:]"), "…/leaf[:]")
        // Nothing to drop when there is no path in front of the name.
        compare(legend.withoutPath("[4,_,_,2]"), "[4,_,_,2]")
        compare(legend.withoutPath("/leaf[:]"), "/leaf[:]")
    }

    function test_the_legend_can_be_made_wider() {
        const win = openWindow()
        win.addCustomTab()
        waitForRendering(win.contentItem)

        AppController.customPlots.plotAt(0).addExpression("/series/a[:]")
        settleReads()

        const view = shownView(win)
        const surface = findAllOf(view, "customPlotSurface")[0]
        surface.legendOpen = true
        waitForRendering(win.contentItem)

        const legend = findAllOf(view, "plotLegend")[0]
        const grip = findAllOf(legend, "legendGrip")[0]
        verify(grip, "the edge must be grabbable")
        compare(legend.width, Theme.railWidth)

        // A legend lists slices, and a slice is as long as the path naming it.
        legend.resizeBy(120)
        compare(legend.width, Theme.railWidth + 120)
        // The graph starts where the legend stops, so widening it moves the
        // picture rather than covering it.
        compare(surface.contentLeft, legend.width)

        // Dragged past either end it stops rather than running on.
        legend.resizeBy(-10000)
        compare(legend.width, legend.minimumWidth)
        legend.resizeBy(10000)
        compare(legend.width, legend.maximumWidth)
    }

    // --- the saved views, as a library -------------------------------------

    function test_the_strip_offers_the_saved_views_next_to_the_plus() {
        const win = openWindow()
        const caret = findAllOf(win.contentItem, "openSavedView")
        compare(caret.length, 1)
        verify(!caret[0].enabled, "nothing saved, so nothing to offer")

        win.addCustomTab()
        waitForRendering(win.contentItem)
        const plot = AppController.customPlots.plotAt(0)
        plot.addExpression("/series/a[:]")
        settleReads()
        compare(AppController.customPlots.saveView("just a", 0, ({})), "")
        waitForRendering(win.contentItem)

        verify(caret[0].enabled, "and now there is")

        // Picking one makes a tab of its own out of it rather than landing in
        // the tab that happens to be open.
        win.openViewInNewTab("just a")
        settleReads()
        waitForRendering(win.contentItem)

        compare(AppController.customPlots.count, 2)
        compare(win.currentTabId, "custom:1")
        compare(AppController.customPlots.plotAt(1).sourceSeriesCount, 1)
    }

    function test_a_view_that_does_not_fit_asks_before_a_tab_is_made() {
        const win = openWindow()
        win.addCustomTab()
        waitForRendering(win.contentItem)

        const plot = AppController.customPlots.plotAt(0)
        plot.addExpression("/series/a[:]")
        plot.addExpression("/gone_away[:]")
        settleReads()
        compare(AppController.customPlots.saveView("half here", 0, ({})), "")

        win.openViewInNewTab("half here")
        settleReads()
        waitForRendering(win.contentItem)

        const asked = win.savedViewWarning
        verify(asked.visible, "the question must come before the tab")
        compare(asked.issues, 1)
        // Nothing was made while it was open -- a reader who changes their
        // mind is not left with an empty plot to close.
        compare(AppController.customPlots.count, 1)

        asked.accept()
        settleReads()
        waitForRendering(win.contentItem)
        compare(AppController.customPlots.count, 2)
    }

    function test_a_view_carries_a_dot_saying_how_much_of_the_file_it_holds() {
        const win = openWindow()
        win.addCustomTab()
        waitForRendering(win.contentItem)

        const plot = AppController.customPlots.plotAt(0)
        plot.addExpression("/series/a[:]")
        settleReads()
        compare(AppController.customPlots.saveView("all here", 0, ({})), "")

        plot.addExpression("/gone_away[:]")
        settleReads()
        compare(AppController.customPlots.saveView("some here", 0, ({})), "")

        compare(AppController.customPlots.stateOf("all here"),
                CustomPlotSet.FullMatch)
        compare(AppController.customPlots.stateOf("some here"),
                CustomPlotSet.PartialMatch)

        // Best fit first, then the alphabet.
        compare(AppController.customPlots.viewNames,
                ["all here", "some here"])

        // The dot is the same colour wherever the view can be picked, which is
        // why the mapping is Theme's rather than each panel's.
        compare(String(Theme.matchColor(CustomPlotSet.FullMatch, false)),
                String(Theme.positive))
        compare(String(Theme.matchColor(CustomPlotSet.PartialMatch, false)),
                String(Theme.warning))
        compare(String(Theme.matchColor(CustomPlotSet.NoMatch, false)),
                String(Theme.danger))
    }

    function test_a_torn_off_plot_can_be_put_back_from_its_own_window() {
        const win = openWindow()
        win.addCustomTab()
        waitForRendering(win.contentItem)
        AppController.customPlots.setDetached(0, true)
        waitForRendering(win.contentItem)
        wait(0)

        const torn = win.plotWindows.objectAt(0)
        verify(torn.visible)
        compare(win.tabs.length, 4)

        const inside = findAllOf(torn.contentItem, "customWindowView")[0]
        const back = findAllOf(inside, "detachPlot").filter((b) => b.visible)[0]
        verify(back, "the way back must be where the way out was")
        mouseClick(back)
        waitForRendering(win.contentItem)

        verify(!torn.visible, "the window goes")
        compare(win.tabs.length, 5)
        // ...and the reader is looking at the tab they were just looking at.
        compare(win.currentTabId, "custom:0")
    }

    // --- logarithmic axes --------------------------------------------------

    /// A custom tab is drawn by the same surface, so it gets the same scales.
    ///
    /// That is the whole claim worth testing here: PlotSurface serves both
    /// plots and neither of them was forked to grow a logarithmic axis. What a
    /// custom tab does have of its own is where the x axis *starts* -- a time
    /// base is read out of the file rather than stated as three numbers -- so
    /// that is what the rest of this asserts.
    function test_a_custom_tab_draws_on_a_logarithmic_axis_too() {
        const win = openWindow()
        win.addCustomTab()
        waitForRendering(win.contentItem)

        const plot = AppController.customPlots.plotAt(0)
        plot.addExpression("/series/decades[:]")
        settleReads()
        waitForRendering(win.contentItem)

        const view = shownView(win)
        const surface = findAllOf(view, "customPlotSurface")[0]
        const lines = findAllOf(view, "plotLines")[0]
        verify(surface && lines, "the surface and its lines must be reachable")

        // The same two values the Data tab's plot has no place for, in a tab
        // that reached them by a different road.
        compare(plot.positiveMinimum, 1e-3)
        verify(plot.minimum < 0)

        surface.yLog = true
        waitForRendering(win.contentItem)
        compare(lines.yLog, true)
        // 0..9, 11..19, 21..48: the zero and the negative are gaps.
        compare(lines.drawnRunCount, 3)
        fuzzyCompare(Math.log10(surface.lowerBound), -3.3, 1e-9)
        fuzzyCompare(Math.log10(surface.upperBound), 3.3, 1e-9)
    }

    /// A logarithmic x axis starts at the first x that can be drawn.
    ///
    /// The axis a custom tab has that the Data tab does not: a time base whose
    /// values came out of the file. /series/time runs 0, 0.5, 1 ... 31.5, so
    /// its low end is a value the scale has no place for -- and the smallest
    /// one it does have a place for is the second element, which only the plot
    /// object can say because only it has read them.
    function test_a_logarithmic_x_axis_starts_at_the_first_time_it_can_draw() {
        const win = openWindow()
        win.addCustomTab()
        waitForRendering(win.contentItem)

        const plot = AppController.customPlots.plotAt(0)
        plot.addExpression("/series/a[:]")
        plot.xMode = CustomPlot.Dataset
        plot.xExpression = "/series/time[:]"
        settleReads()
        waitForRendering(win.contentItem)
        verify(plot.xReady, "the time base must have been read")

        compare(plot.xMinimum, 0)
        compare(plot.xMaximum, 31.5)
        compare(plot.xPositiveMinimum, 0.5)

        const view = shownView(win)
        const surface = findAllOf(view, "customPlotSurface")[0]
        compare(surface.axisLowX, 0)

        surface.xLog = true
        waitForRendering(win.contentItem)
        compare(surface.axisLowX, 0.5)
        compare(surface.viewMinX, 0.5)
        compare(surface.viewMaxX, 31.5)
        // The first sample is drawn nowhere, so the line is one run short of
        // its length rather than a stroke reaching down to an invented x.
        const lines = findAllOf(view, "plotLines")[0]
        compare(lines.drawnRunCount, 1)
        verify(lines.drawnPointCount < 64)
    }

    /// A tab drawn against a time base spanning decades draws across the whole
    /// of a logarithmic pane.
    ///
    /// The custom tab's half of what tst_views asserts for the Plot tab, and
    /// the half with a time base in it: /trace_time runs four decades, from a
    /// thousandth of a second to twenty, and its summary put one drawn point
    /// every ten milliseconds -- so the first two decades of the pane, where
    /// there are ten elements, held one bucket and the line began a fifth of
    /// the way across. Asked of the pixels, in the window's own grab, because
    /// what matters is what the reader sees.
    function test_a_time_base_across_decades_draws_across_a_logarithmic_pane() {
        const win = openWindow()
        win.addCustomTab()
        waitForRendering(win.contentItem)

        const plot = AppController.customPlots.plotAt(0)
        plot.addExpression("/trace[:]")
        plot.xMode = CustomPlot.Dataset
        plot.xExpression = "/trace_time[:]"
        settleReads()
        verify(plot.xReady, "the time base must have been read")

        const view = shownView(win)
        const surface = findAllOf(view, "customPlotSurface")[0]
        surface.colorMode = "spectrum"
        wait(600)
        settleReads()
        waitForRendering(win.contentItem)

        const inked = () => {
            const shot = grabImage(win.contentItem)
            const area = surface.plotRect
            const corner = surface.mapToItem(win.contentItem, area.x, area.y)
            const left = Math.ceil(corner.x) + 1
            const right = Math.floor(corner.x + area.width) - 1
            const top = Math.ceil(corner.y)
            const bottom = Math.floor(corner.y + area.height)
            let columns = 0
            for (let x = left; x < right; ++x) {
                for (let y = top; y < bottom; ++y) {
                    const pixel = shot.pixel(x, y)
                    if (Math.max(pixel.r, pixel.g, pixel.b)
                        - Math.min(pixel.r, pixel.g, pixel.b) > 0.06) {
                        ++columns
                        break
                    }
                }
            }
            return columns / Math.max(1, right - left)
        }
        verify(inked() > 0.95, "the linear plot is the baseline")

        surface.xLog = true
        wait(200)
        waitForRendering(win.contentItem)
        fuzzyCompare(surface.axisLowX, 0.001, 1e-12)
        const across = inked()
        verify(across > 0.95, "the line must reach across the pane: " + across)
    }

    /// An index axis starts at one, which is the first index there is a place
    /// for.
    ///
    /// The other half of the same question, and the half no read answers: the
    /// default x axis is the element's own index and starts at zero, so every
    /// plot in the application would go blank the moment the box was ticked if
    /// this were taken as given. It is arithmetic over the stated grid
    /// instead -- see PlotSurface.gridPositiveMinX -- so it costs nothing and
    /// is right before a single element has been read.
    function test_a_logarithmic_index_axis_starts_at_the_first_index() {
        const win = openWindow()
        win.addCustomTab()
        waitForRendering(win.contentItem)

        const plot = AppController.customPlots.plotAt(0)
        plot.addExpression("/series/a[:]")
        settleReads()
        waitForRendering(win.contentItem)

        const view = shownView(win)
        const surface = findAllOf(view, "customPlotSurface")[0]
        compare(surface.axisLowX, 0)

        surface.xLog = true
        waitForRendering(win.contentItem)
        compare(surface.axisLowX, 1)
        compare(surface.viewMinX, 1)
    }

    function test_the_footer_counts_entries_and_datapoints() {
        const win = openWindow()
        win.addCustomTab()
        waitForRendering(win.contentItem)

        const plot = AppController.customPlots.plotAt(0)
        plot.addExpression("/series/a[:]")
        plot.addExpression("/series/b[:]")
        settleReads()
        waitForRendering(win.contentItem)

        compare(plot.seriesCount, 2)
        compare(plot.pointCount, 128)
    }

    // --- a line on a y axis of its own -------------------------------------

    /// The side axes the frame on screen is drawing, left to right.
    function sideAxes(view) {
        return findAllOf(view, "plotSideAxis").sort((a, b) => a.x - b.x)
    }

    function test_a_line_can_be_given_a_y_axis_of_its_own() {
        const win = openWindow()
        win.addCustomTab()
        waitForRendering(win.contentItem)

        const plot = AppController.customPlots.plotAt(0)
        plot.addExpression("/series/a[:]")
        settleReads()

        const view = shownView(win)
        mouseClick(findAllOf(view, "customDataButton")[0])
        waitForRendering(win.contentItem)

        // One line has one axis: the box is there, and says so by being off.
        let boxes = findAllOf(view, "entrySeparateAxis")
        compare(boxes.length, 1)
        verify(!boxes[0].enabled, "a plot of one line has nothing to separate from")

        plot.addExpression("/series/b[:]")
        settleReads()
        waitForRendering(win.contentItem)
        boxes = findAllOf(view, "entrySeparateAxis")
        compare(boxes.length, 2)
        tryVerify(() => boxes[1].enabled, 2000, "a second line is something to separate from")

        const surface = findAllOf(view, "customPlotSurface")[0]
        const lines = findAllOf(view, "plotLines")[0]
        const frame = lines.parent
        const before = frame.gutterLeft
        compare(sideAxes(view).length, 0)
        verify(!findAllOf(view, "entryAxisFixed")[1].visible,
               "exclude from zooming is a question about a separate axis")

        mouseClick(boxes[1])
        settleReads()
        waitForRendering(win.contentItem)

        compare(plot.sharedSeriesCount, 1)
        verify(findAllOf(view, "entryAxisFixed")[1].visible)
        tryVerify(() => sideAxes(view).length === 1, 2000, "the line's axis must be drawn")
        const axis = sideAxes(view)[0]
        compare(axis.line, 1)
        // To the left of the common axis, and the pane moved over to make room
        // for it rather than the two being drawn over one another.
        verify(frame.gutterLeft > before, "the gutter must grow by the axis")
        verify(axis.x + axis.width < frame.area.x - frame.commonWidth + 1,
               "the side axis is left of the common one")
        // In the colour of the line it is the axis of.
        compare(String(axis.colour), String(lines.seriesColor(1)))
        verify(lines.seriesHasOwnY(1), "the line is drawn against its own axis")
        verify(!lines.seriesHasOwnY(0), "and the other one is not")

        // A tick is drawn where the curve is: the side axis's ticks and the
        // item's own map of that line agree.
        const tick = axis.ticks[1]
        const value = Number(tick.text)
        fuzzyCompare(tick.at, lines.seriesYFraction(1, value), 1e-9)

        // The common axis spans the line left on it, and the separate one is
        // the axis series_b would have if it were the only line: 37..100.
        compare(plot.maximum, 63)
        const own = surface.separateAxes[0]
        verify(own.low < 37 && own.high > 100, "the own axis spans its whole line")
        verify(own.low > 30 && own.high < 110, "...with a lone line's air and no more")

        // Unticked, it goes back on the common axis.
        mouseClick(boxes[1])
        settleReads()
        waitForRendering(win.contentItem)
        compare(sideAxes(view).length, 0)
        verify(!lines.seriesHasOwnY(1))
        compare(frame.gutterLeft, before)
    }

    function test_a_separate_axis_zooms_with_the_plot_unless_excluded() {
        const win = openWindow()
        win.addCustomTab()
        waitForRendering(win.contentItem)

        const plot = AppController.customPlots.plotAt(0)
        plot.addExpression("/series/a[:]")
        plot.addExpression("/series/b[:]")
        plot.addExpression("/series/a[:]")
        settleReads()
        plot.setSeparateAxis(1, true)
        plot.setSeparateAxis(2, true)
        plot.setAxisFixed(2, true)
        settleReads()
        waitForRendering(win.contentItem)

        const view = shownView(win)
        const surface = findAllOf(view, "customPlotSurface")[0]
        tryVerify(() => surface.separateAxes.length === 2, 2000)
        const zooming = surface.separateAxes[0]
        const fixed = surface.separateAxes[1]
        compare(zooming.series, 1)
        compare(fixed.series, 2)

        // Twice as close, about the top of the pane: the window's top on the
        // top of the data. Worked out rather than clamped into, because the
        // pan may now go past the data's end -- see PlotSurface.panKeep.
        surface.zoomY = 2
        surface.panY = (surface.upperBound - surface.lowerBound) / 4
        waitForRendering(win.contentItem)

        const after = surface.separateAxes
        // The zooming axis shows the top half of itself -- the same share of
        // its whole as the common axis is showing of its own.
        fuzzyCompare(after[0].high, zooming.high, 1e-9)
        fuzzyCompare(after[0].low, (zooming.low + zooming.high) / 2, 1e-9)
        // ...and the excluded one has not moved at all.
        compare(after[1].low, fixed.low)
        compare(after[1].high, fixed.high)

        // The renderer was told both.
        const lines = findAllOf(view, "plotLines")[0]
        fuzzyCompare(lines.seriesYFraction(1, after[0].low), 0, 1e-9)
        fuzzyCompare(lines.seriesYFraction(2, fixed.low), 0, 1e-9)
    }

    function test_with_every_line_on_its_own_axis_there_is_no_common_one() {
        const win = openWindow()
        win.addCustomTab()
        waitForRendering(win.contentItem)

        const plot = AppController.customPlots.plotAt(0)
        plot.addExpression("/series/a[:]")
        plot.addExpression("/series/b[:]")
        settleReads()
        plot.setSeparateAxis(0, true)
        plot.setSeparateAxis(1, true)
        settleReads()
        waitForRendering(win.contentItem)

        const view = shownView(win)
        const surface = findAllOf(view, "customPlotSurface")[0]
        const lines = findAllOf(view, "plotLines")[0]
        const frame = lines.parent

        compare(plot.sharedSeriesCount, 0)
        verify(!surface.sharedAxis)
        tryVerify(() => sideAxes(view).length === 2, 2000)
        // Nothing numbered, named or ruled in the common axis's place.
        compare(frame.yTicks.length, 0)
        compare(frame.yGrid.length, 0)
        compare(frame.commonWidth, Theme.gapS)
        // ...and still something to draw, and nothing to complain about.
        verify(surface.drawable)
        compare(surface.logReason, "")
        surface.yLog = true
        waitForRendering(win.contentItem)
        compare(surface.logReason, "",
                "a logarithmic common axis with no line on it is not a complaint")
        verify(lines.drawnPointCount > 0, "the lines on their own axes still draw")
    }

    /// The picture that leaves is a second frame (PlotPicture.qml) with copies
    /// of the lines, so both halves have to come across: which line is on an
    /// axis of its own, which travels in the line, and the axis numbered
    /// beside it, which is the frame's.
    function test_a_copied_picture_keeps_the_separate_axes() {
        const win = openWindow()
        win.addCustomTab()
        waitForRendering(win.contentItem)

        const plot = AppController.customPlots.plotAt(0)
        plot.addExpression("/series/a[:]")
        plot.addExpression("/series/b[:]")
        settleReads()
        plot.setSeparateAxis(1, true)
        settleReads()
        waitForRendering(win.contentItem)

        const view = shownView(win)
        const surface = findAllOf(view, "customPlotSurface")[0]
        tryVerify(() => surface.separateAxes.length === 1, 2000)

        verify(surface.copyImage(), "a drawn plot must accept the request")
        verify(surface.picture, "the picture is alive while the grab is in flight")
        const drawn = findChild(surface.picture, "pictureLines")
        verify(drawn, "the picture's own lines must be reachable")
        verify(drawn.seriesHasOwnY(1), "the line keeps its own axis in the picture")
        verify(!drawn.seriesHasOwnY(0))
        compare(drawn.parent.sideColumns.length, 1)
        compare(drawn.parent.commonAxis, true)
        fuzzyCompare(drawn.seriesYFraction(1, surface.separateAxes[0].low), 0, 1e-9)

        tryVerify(() => surface.picture === null, 10000, "the grab must answer")
    }
}
