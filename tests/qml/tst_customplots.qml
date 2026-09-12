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

    /// One controller serves the whole binary, so a suite that leaves tabs
    /// behind makes the next case depend on the last one. Re-opening the file
    /// empties them, which is the behaviour under test in test_customplot and
    /// is used here as the reset.
    function init() {
        AppController.closeFile()
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
        verify(inside[0].detached, "it offers no second tear-off")
        compare(findAllOf(inside[0], "detachPlot")
                .filter((b) => b.visible).length, 0)

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
}
