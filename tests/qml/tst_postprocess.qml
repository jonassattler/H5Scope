// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import QtQuick.Window
import QtTest
import H5Scope
import H5Scope.Backend

/// The postprocessing panel against the real model: the rows it draws, the
/// controls on them, and the two places in the bar above the views that say a
/// pipeline is running.
///
/// What the operations *compute* is settled in test_postprocess, against numpy.
/// This suite is about the panel: that the rows are the pipeline, that the
/// controls reach the model, and that a switch left off changes nothing.
TestCase {
    id: testCase
    name: "Postprocessing"
    when: windowShown
    width: 800
    height: 600

    readonly property string fixture: TestFixture.path
    readonly property var pipeline: AppController.postprocessModel

    function initTestCase() {
        verify(AppController.openFile(fixture), "fixture must open")
    }

    /// Every test starts on a file nobody has been at yet, so a pipeline left
    /// running by one of them is not inherited by the next.
    function init() {
        AppController.closeFile()
        verify(AppController.openFile(fixture), "fixture must re-open")
    }

    function cleanupTestCase() {
        AppController.closeFile()
    }

    /// A panel in a window of its own: an item parented into the test case is
    /// never effectively visible, so its delegates are never built and nothing
    /// measured off them means anything.
    Component {
        id: panelWindowComponent

        Window {
            property alias panel: livePanel

            width: 520
            height: 700
            visible: true
            color: Theme.background

            PostprocessPanel {
                id: livePanel
                anchors.fill: parent
            }
        }
    }

    Component {
        id: windowComponent
        Main {}
    }

    function openPanel() {
        const win = createTemporaryObject(panelWindowComponent, testCase)
        verify(win, "the panel must instantiate")
        waitForRendering(win.contentItem)
        return win
    }

    /// The rows the list has actually built, in order.
    function rowsOf(win) {
        const list = findChild(win.contentItem, "pipelineRows")
        verify(list, "the panel must have its list")
        const out = []
        for (let i = 0; i < list.contentItem.children.length; ++i) {
            const slot = list.contentItem.children[i]
            if (slot.objectName !== "" || slot.children.length === 0)
                continue
            out.push(slot)
        }
        return out
    }

    function test_the_panel_opens_on_the_two_ends_and_nothing_between() {
        verify(AppController.selectPath("/cube"))
        const win = openPanel()

        // The input, the slice and the output: in the beginning they are the
        // only things there.
        compare(pipeline.rowCount(), 4)

        const list = findChild(win.contentItem, "pipelineRows")
        compare(list.count, 4)
    }

    function test_the_switch_greys_everything_below_it() {
        verify(AppController.selectPath("/cube"))
        const win = openPanel()

        const box = findChild(win.contentItem, "enablePostprocessing")
        verify(box, "the panel must have its switch")
        verify(!box.checked, "a dataset nobody has been at opens switched off")

        const list = findChild(win.contentItem, "pipelineRows")
        verify(!list.enabled, "the chain is dead while the switch is off")

        // Greyed rather than hidden: the chain is still what it was.
        verify(list.visible, "...and still on screen")

        pipeline.enabled = true
        waitForRendering(win.contentItem)
        verify(list.enabled, "the chain wakes up with the switch")
        verify(box.checked, "and the box follows the model")
    }

    function test_a_row_is_added_by_the_button_and_removed_by_the_x() {
        verify(AppController.selectPath("/cube"))
        pipeline.enabled = true
        const win = openPanel()

        const chooser = findChild(win.contentItem, "addOperation")
        const button = findChild(win.contentItem, "addStep")
        verify(chooser, "the add row must have its dropdown")
        verify(button, "...and its button")
        verify(chooser.count > 0, "...with the operations in the list")

        // Choosing is not adding. A dropdown that acted the moment it was
        // picked from would put a step into a running pipeline before the
        // reader had finished deciding which one they wanted.
        const before = pipeline.rowCount()
        pipeline.chosenOperation = 3 // min
        waitForRendering(win.contentItem)
        compare(pipeline.rowCount(), before, "picking one adds nothing")

        button.clicked()
        waitForRendering(win.contentItem)
        compare(pipeline.rowCount(), before + 1)
        compare(pipeline.data(pipeline.index(2, 0), PostprocessModel.LabelRole),
                "min", "and the button adds the one that was picked")
        // ...and the box has not jumped back to the first operation, so a
        // second min is one more click rather than another trip through a list.
        compare(pipeline.chosenOperation, 3)

        const list = findChild(win.contentItem, "pipelineRows")
        compare(list.count, before + 1)

        // The red X is on the added row and on nothing else.
        const removes = []
        findAllRemoves(win.contentItem, removes)
        compare(removes.length, 1, "only an added step can be removed")

        removes[0].clicked()
        waitForRendering(win.contentItem)
        compare(pipeline.rowCount(), before)
    }

    function test_the_add_row_sits_in_the_chain_above_the_output() {
        // Where postprocessing.md draws it: a row of the chain, with the same
        // gutter as the steps, rather than a strip below the panel.
        verify(AppController.selectPath("/cube"))
        pipeline.enabled = true
        const win = openPanel()

        compare(pipeline.rowCount(), 4)
        compare(pipeline.data(pipeline.index(2, 0), PostprocessModel.KindRole),
                PostprocessModel.Adder)
        compare(pipeline.data(pipeline.index(3, 0), PostprocessModel.KindRole),
                PostprocessModel.Output)

        const list = findChild(win.contentItem, "pipelineRows")
        compare(list.count, 4, "the add row is one of the list's own")
        // It states no shape, because it leaves nothing behind.
        compare(pipeline.data(pipeline.index(2, 0), PostprocessModel.ShapeRole), "")
    }

    /// Every visible remove button under `item`, appended to `out`.
    function findAllRemoves(item, out) {
        for (let i = 0; i < item.children.length; ++i) {
            const child = item.children[i]
            if (child.objectName === "stepRemove" && child.visible)
                out.push(child)
            findAllRemoves(child, out)
        }
    }

    function test_the_shape_column_states_what_each_step_leaves() {
        verify(AppController.selectPath("/cube")) // 2 x 3 x 4
        pipeline.enabled = true
        pipeline.addStep("max")
        pipeline.setArgument(2, "0")
        const win = openPanel()

        const shapes = []
        findAllShapes(win.contentItem, shapes)
        // The input, the slice, the Max, the add row and the output.
        compare(shapes.length, 5)
        compare(shapes[0], "2 × 3 × 4")
        compare(shapes[1], "2 × 3 × 4")
        compare(shapes[2], "3 × 4")
        compare(shapes[3], "", "the add row leaves nothing behind")
        compare(shapes[4], "3 × 4")
    }

    /// The shape text of every row, in order.
    function findAllShapes(item, out) {
        for (let i = 0; i < item.children.length; ++i) {
            const child = item.children[i]
            if (child.objectName === "stepShape")
                out.push(child.text)
            findAllShapes(child, out)
        }
    }

    function test_a_step_that_cannot_run_says_so_where_it_happened() {
        verify(AppController.selectPath("/cube"))
        pipeline.enabled = true
        pipeline.addStep("transpose")
        pipeline.setArgument(2, "7, 7")
        const win = openPanel()

        verify(pipeline.error !== "", "the pipeline must report the refusal")
        const shapes = []
        findAllShapes(win.contentItem, shapes)
        // The row that refused has no shape: a stale one beside it would be a
        // claim about data nobody computed.
        compare(shapes[2], "")
        // ...and the output is what the last row that worked produced.
        compare(shapes[4], "2 × 3 × 4")
    }

    function test_clicking_a_row_greys_the_ones_after_it() {
        verify(AppController.selectPath("/cube"))
        pipeline.enabled = true
        pipeline.addStep("max")
        pipeline.setArgument(2, "0")
        pipeline.addStep("min")
        pipeline.setArgument(3, "0")
        const win = openPanel()

        compare(pipeline.activeRow, 3)
        pipeline.activeRow = 2
        waitForRendering(win.contentItem)

        compare(pipeline.data(pipeline.index(2, 0), PostprocessModel.ComputedRole),
                true)
        compare(pipeline.data(pipeline.index(3, 0), PostprocessModel.ComputedRole),
                false)
        // The output is never greyed: it is the end of whatever is actually
        // being computed, which is exactly what clicking a row changes.
        compare(pipeline.data(pipeline.index(4, 0), PostprocessModel.ComputedRole),
                true)
        compare(pipeline.data(pipeline.index(5, 0), PostprocessModel.ComputedRole),
                true)
    }

    function test_the_bar_says_when_a_pipeline_is_running() {
        verify(AppController.selectPath("/cube"))
        const win = createTemporaryObject(windowComponent, testCase)
        waitForRendering(win.contentItem)
        win.selectTab("table")
        waitForRendering(win.contentItem)

        const badge = findChild(win.contentItem, "postprocessBadge")
        verify(badge, "the bar must carry the label")
        verify(!badge.visible, "...and not show it until there is something to say")

        pipeline.enabled = true
        waitForRendering(win.contentItem)
        verify(badge.visible, "every number below the bar is now a computed one")
        compare(badge.tone, "warn")

        pipeline.enabled = false
        waitForRendering(win.contentItem)
        verify(!badge.visible, "and it goes away with the switch")
    }

    function test_the_button_opens_the_panel_and_widens_the_rail() {
        verify(AppController.selectPath("/cube"))
        const win = createTemporaryObject(windowComponent, testCase)
        waitForRendering(win.contentItem)
        win.selectTab("table")
        waitForRendering(win.contentItem)

        const button = findChild(win.contentItem, "postprocessButton")
        verify(button, "the bar must have the button")

        const view = findChild(win.contentItem, "dataView")
        verify(view, "the window must have its data view")
        compare(view.rail, "")

        button.clicked()
        waitForRendering(win.contentItem)
        compare(view.rail, "post")
        // The one panel the rail widens for: a pipeline row is a line, not a
        // column of settings.
        compare(view.railWidth, Theme.railWidthWide)

        button.clicked()
        waitForRendering(win.contentItem)
        compare(view.rail, "")
    }

    function test_the_slice_row_is_the_slice_above_the_table() {
        verify(AppController.selectPath("/cube"))
        pipeline.enabled = true
        openPanel()

        // Not a copy of it: writing in the panel writes the slice everything
        // else is reading.
        pipeline.setArgument(1, "1, :, :")
        compare(AppController.sliceText, "1, :, :")

        // ...and writing it in the bar comes back here.
        compare(AppController.applySlice(":, 2, :"), "")
        compare(pipeline.data(pipeline.index(1, 0), PostprocessModel.ArgumentRole),
                ":, 2, :")
    }

    function test_each_row_says_what_its_argument_is() {
        // "axis" and "shape" are not interchangeable and neither is guessable
        // from the operation's name, so the box says which one it wants.
        verify(AppController.selectPath("/cube"))
        pipeline.enabled = true
        pipeline.addStep("max")
        pipeline.addStep("abs")
        pipeline.addStep("reshape")
        const win = openPanel()

        const labels = []
        findAllOf(win.contentItem, "stepArgumentLabel", labels)
        // The input, the slice, Max, Abs, Reshape, the add row and the output.
        compare(labels.length, 7, "one per row, shown or not")

        verify(!labels[0].visible, "the dataset takes no argument")
        compare(labels[1].text, "subscripts")
        compare(labels[2].text, "axis")
        verify(!labels[3].visible, "an operation that takes no argument says so "
                                   + "by having no box and no label")
        compare(labels[4].text, "shape")
        verify(!labels[5].visible, "the add row draws its own controls")
        verify(!labels[6].visible, "and neither the output nor the input takes one")
    }

    function test_every_row_holds_its_argument_the_same_way() {
        // The slice is a step like the others and is written like the others.
        // Its box used to fill the rail while an operation's stayed at a third
        // of that, and its label was exempt from the column the rest shared --
        // so the two rows that take an argument in the same way looked like
        // two different kinds of control, and no two boxes in the panel began
        // at the same x.
        verify(AppController.selectPath("/cube"))
        pipeline.enabled = true
        pipeline.addStep("max")
        pipeline.addStep("reshape")
        const win = openPanel()

        const boxes = []
        findAllOf(win.contentItem, "stepArgument", boxes)
        const shown = boxes.filter((box) => box.visible)
        compare(shown.length, 3, "the slice and the two steps take arguments")

        const leftOf = (item) => item.mapToItem(win.contentItem, 0, 0).x
        for (let i = 1; i < shown.length; ++i) {
            compare(shown[i].width, shown[0].width, "one width for all of them")
            compare(leftOf(shown[i]), leftOf(shown[0]), "...and one column")
        }
    }

    function test_an_argument_typed_and_not_applied_says_so() {
        // The row is not doing what its box says until Return is pressed, and
        // nothing on screen used to say which of the two states it was in --
        // the shape beside it and the numbers in the view were still the
        // previous argument's.
        verify(AppController.selectPath("/cube")) // 2 x 3 x 4
        pipeline.enabled = true
        pipeline.addStep("max")
        pipeline.setArgument(2, "0")
        const win = openPanel()

        const boxes = []
        findAllOf(win.contentItem, "stepArgument", boxes)
        const box = boxes[2]
        verify(box.visible, "the step must have its box")
        verify(!box.pending, "nothing has been typed into it")
        compare(box.background.color, Theme.surfaceInset)

        const notes = []
        findAllOf(win.contentItem, "stepNote", notes)
        const note = notes[2]
        verify(!note.visible, "and there is nothing to say about it")

        box.forceActiveFocus()
        box.text = "1"
        box.textEdited()
        waitForRendering(win.contentItem)

        verify(box.pending, "typed, and the row is still running the old one")
        verify(note.visible, "which the row says under itself")
        compare(note.color, Theme.accent, "an unfinished edit is not a mistake")
        compare(box.background.color, Theme.surfacePending,
                "and the box says it too")
        compare(pipeline.data(pipeline.index(2, 0), PostprocessModel.ShapeRole),
                "3 × 4", "the shape is still the applied argument's")

        keyClick(Qt.Key_Return)
        waitForRendering(win.contentItem)

        verify(!box.pending, "Return applies it")
        verify(!note.visible)
        compare(box.background.color, Theme.surfaceInset)
        compare(pipeline.data(pipeline.index(2, 0), PostprocessModel.ShapeRole),
                "2 × 4")
    }

    function test_a_step_is_dragged_by_its_gutter_to_reorder_it() {
        // The one interaction here that is not a click, and the only one this
        // codebase had no precedent for. Driven with real presses rather than
        // by calling moveStep, because what is being tested is the arithmetic
        // that turns a drop position into a destination row.
        verify(AppController.selectPath("/hypercube")) // 2 x 3 x 4 x 5
        pipeline.enabled = true
        pipeline.addStep("max")
        pipeline.setArgument(2, "0")
        pipeline.addStep("reshape")
        pipeline.setArgument(3, "-1")
        const win = openPanel()

        compare(labelAt(win, 2), "max")
        compare(labelAt(win, 3), "reshape")

        // Drawn on every row and shown on the two that can move: the ends of
        // the chain keep the gutter so the rule down it stays unbroken, and
        // lose the grip because there is nothing to pick up.
        const grips = []
        findAllOf(win.contentItem, "stepGrip", grips)
        compare(grips.length, 6, "every row draws a gutter")
        const movable = grips.filter((grip) => grip.visible)
        compare(movable.length, 2, "only the added steps can be picked up")

        // Carry the second one up over the first. The gutter is what holds the
        // DragHandler, and the grip is drawn inside it.
        const handle = movable[1]
        const height = handle.parent.height
        const x = handle.width / 2
        const y = handle.height / 2
        mousePress(handle, x, y)
        // The button has to travel with the moves: mouseMove defaults to
        // Qt.NoButton, and a DragHandler is not looking at those.
        mouseMove(handle, x, y - height / 4, -1, Qt.LeftButton)
        mouseMove(handle, x, y - height, -1, Qt.LeftButton)
        mouseRelease(handle, x, y - height)
        waitForRendering(win.contentItem)

        compare(labelAt(win, 2), "reshape")
        compare(labelAt(win, 3), "max")
        // ...and the shapes are recomputed for the order they are now in:
        // flattened to 120 first, so a maximum over axis 0 is a scalar.
        compare(pipeline.data(pipeline.index(2, 0), PostprocessModel.ShapeRole),
                "120")
        compare(pipeline.data(pipeline.index(3, 0), PostprocessModel.ShapeRole),
                "scalar")
    }

    function labelAt(win, row) {
        return pipeline.data(pipeline.index(row, 0), PostprocessModel.LabelRole)
    }

    /// Every item under `item` with that objectName, appended to `out`.
    function findAllOf(item, name, out) {
        for (let i = 0; i < item.children.length; ++i) {
            const child = item.children[i]
            if (child.objectName === name)
                out.push(child)
            findAllOf(child, name, out)
        }
    }

    function test_the_axis_column_greys_out_while_a_pipeline_runs() {
        // The output array is a new array with its own rank, so an axis
        // assignment made about the dataset cannot be carried onto it. The
        // control says so instead of accepting clicks that change nothing.
        verify(AppController.selectPath("/cube"))
        const win = createTemporaryObject(windowComponent, testCase)
        waitForRendering(win.contentItem)
        win.selectTab("table")
        const view = findChild(win.contentItem, "dataView")
        view.rail = "data"
        waitForRendering(win.contentItem)

        const boxes = []
        findAllOf(win.contentItem, "axisColumn", boxes)
        verify(boxes.length > 0, "the data settings panel must have its x/y column")
        verify(boxes[0].enabled, "which is live while nothing is postprocessing")

        pipeline.enabled = true
        waitForRendering(win.contentItem)
        verify(!boxes[0].enabled, "and greys out once something is")

        pipeline.enabled = false
        waitForRendering(win.contentItem)
        verify(boxes[0].enabled, "and comes back with the switch")
    }

    function test_a_dataset_with_no_numbers_says_why_it_cannot() {
        verify(AppController.selectPath("/str_vlen"))
        const win = openPanel()

        // The switch is still there to set; what changes is that setting it
        // does nothing, and the panel says so rather than leaving it to be
        // inferred from a chain that never moves.
        pipeline.enabled = true
        waitForRendering(win.contentItem)
        verify(!AppController.postprocessActive)

        const chooser = findChild(win.contentItem, "addOperation")
        verify(!chooser.enabled, "there is no operation to add that could run")
    }
}
