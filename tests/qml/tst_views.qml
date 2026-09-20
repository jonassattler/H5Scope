// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import QtQuick.Window
import QtTest
import H5Scope
import H5Scope.Backend

/// Instantiates the real view components against the real models, so a broken
/// delegate or a bad role name fails the build rather than shipping.
TestCase {
    id: testCase
    name: "Views"
    when: windowShown
    width: 800
    height: 600

    readonly property string fixture: TestFixture.path

    /// Wait for the tree to have finished describing what it is showing.
    ///
    /// Rows appear before their readouts do: the model answers with the link
    /// table straight away and asks the HDF5 thread what each name actually is,
    /// so a shape or a tag is one round trip behind the row it belongs to. Two
    /// passes, because the first settles the listing and the second the rows
    /// that listing produced.
    function settleTree(win) {
        for (let pass = 0; pass < 2; ++pass) {
            tryVerify(() => !AppController.busy, 10000, "the tree must settle")
            waitForRendering(win.tree)
        }
    }

    /// Open the fixture and wait for it.
    ///
    /// Opening is asked of the HDF5 thread and answered a moment later, so a
    /// test that asserts on what is in the file has to say when it wants the
    /// answer. `tryVerify` is Qt Quick Test's way of doing that: it runs the
    /// event loop until the condition holds or the deadline passes, which is
    /// exactly what the window does while it waits.
    function openFixture() {
        verify(AppController.openFile(fixture), "the fixture must be accepted")
        tryVerify(() => AppController.hasFile && !AppController.busy, 10000,
                  "the fixture must finish opening")
        return AppController.hasFile
    }

    /// Select an object and wait for everything the selection rebuilds.
    /// Describing the object is one round trip; installing what the views draw
    /// is the next, so this settles twice.
    function select(path) {
        verify(AppController.selectPath(path))
        tryVerify(() => AppController.currentPath === path && !AppController.busy,
                  10000, "selecting " + path)
        wait(0)
        tryVerify(() => !AppController.busy, 10000, "settling after " + path)
        return true
    }

    /// Views are created at a real size: a zero-sized ListView builds no
    /// delegates, and a delegate that never runs cannot fail a test.
    readonly property var viewSize: ({ width: 800, height: 600 })

    function initTestCase() {
        openFixture()
    }

    /// Every test starts on a file nobody has been at yet.
    ///
    /// Settings and slices are remembered per dataset for as long as a file is
    /// open -- see DatasetMemory and AppController::rememberSettings -- which
    /// is what a reader flicking between two datasets wants, and which makes a
    /// suite that shares one controller order-dependent. Re-opening the file is
    /// what "a fresh look at this" means to the application, so it is what a
    /// test that assumes one should do.
    function init() {
        AppController.closeFile()
        verify(openFixture(), "fixture must re-open")
    }

    function cleanupTestCase() {
        AppController.closeFile()
    }

    Component {
        id: infoComponent
        InfoView {}
    }

    Component {
        id: dataComponent
        DataView {}
    }

    Component {
        id: pickerComponent
        FilePicker {}
    }

    Component {
        id: treeComponent
        ObjectTree {}
    }

    Component {
        id: setupComponent
        TableSetupPanel {}
    }

    Component {
        id: menuBarComponent
        AppMenuBar {}
    }

    /// The bar in a window of its own, for the drawer tests: an item parented
    /// into the test case is never effectively visible, and a Menu popped up
    /// inside one never lays its rows out -- so every width read off it is
    /// zero and every assertion about them passes for the wrong reason.
    Component {
        id: menuWindowComponent

        Window {
            property alias bar: liveBar

            width: 900
            height: 420
            visible: true
            color: Theme.background

            AppMenuBar {
                id: liveBar

                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
            }
        }
    }

    Component {
        id: tableSettingsComponent
        TableSettingsPanel {}
    }

    Component {
        id: imageSettingsComponent
        ImageSettingsPanel {}
    }

    Component {
        id: plotSettingsComponent
        PlotSettingsPanel {}
    }

    /// The tree in a window of its own, for the filter test: an item parented
    /// into the test case is never effectively visible, and an invisible item
    /// takes no focus and is delivered no key events.
    Component {
        id: treeWindowComponent

        Window {
            property alias tree: liveTree

            width: 360
            height: 600
            visible: true
            color: Theme.background

            ObjectTree {
                id: liveTree
                anchors.fill: parent
            }
        }
    }

    /// The Information tab on a window of its own, for the assertions that
    /// read effective visibility: an Item parented to the TestCase is never
    /// shown, so everything under it reports invisible whatever its own
    /// binding says.
    Component {
        id: infoWindowComponent

        Window {
            property alias info: liveInfo

            width: 900
            height: 700
            visible: true
            color: Theme.background

            InfoView {
                id: liveInfo
                anchors.fill: parent
            }
        }
    }

    SignalSpy {
        id: menuSpy
    }

    // Copying is asynchronous -- a grab is one more frame rendered -- so the
    // test that asserts it has to wait for the answer rather than read a
    // return value.
    SignalSpy {
        id: copySpy

        target: ImageClipboard
        signalName: "copied"
    }

    SignalSpy {
        id: copyFailedSpy

        target: ImageClipboard
        signalName: "failed"
    }

    /// The whole tab in a window of its own, for the tests that have to look
    /// at pixels: an item parented into the test case is never effectively
    /// visible, and an invisible item is never rendered.
    Component {
        id: viewWindowComponent

        Window {
            property alias view: liveView

            width: 900
            height: 600
            visible: true
            color: Theme.background

            DataView {
                id: liveView
                anchors.fill: parent
            }
        }
    }

    /// The whole application window. The tab strip and the slice bar are the
    /// window's own furniture now, so the tests for them cannot get at either
    /// through a view on its own.
    Component {
        id: windowComponent
        Main {}
    }

    /// A panel in a window of its own, for the one test that needs to click
    /// something: an item parented into the test case is never effectively
    /// visible, and an invisible item is delivered no mouse events.
    Component {
        id: panelWindowComponent

        Window {
            property alias panel: livePanel

            width: 320
            height: 700
            visible: true
            color: Theme.background

            TableSetupPanel {
                id: livePanel
                anchors.fill: parent
            }
        }
    }

    Component {
        id: realFieldComponent
        RealField {}
    }

    Component {
        id: numberFieldComponent
        NumberField {}
    }

    /// A number box in a window of its own, for the tests that press its
    /// arrows: an item parented into the test case is never effectively
    /// visible, and an invisible item is delivered no mouse events.
    Component {
        id: fieldWindowComponent

        Window {
            property alias real: liveReal
            property alias whole: liveWhole

            width: 320
            height: 120
            visible: true
            color: Theme.background

            RealField {
                id: liveReal
                x: Theme.gapM
                y: Theme.gapM
            }

            NumberField {
                id: liveWhole
                x: Theme.gapM
                y: Theme.gapM + Theme.controlHeight * 2
                from: 0
                to: 10
            }
        }
    }

    /// What a number box writes for a value it was handed.
    ///
    /// The bug this pins down: `toPrecision(6)` writes 500000 as "500000" --
    /// six significant figures and no decimal point -- and the rule that took
    /// the padding zeros back off was stripping the trailing zeros of *that*,
    /// so half a million came out as "5". The plot's `stop` box computes
    /// itself from the length of the data, so on a dataset of that many
    /// elements the reader watched it report 5.
    function test_a_real_box_writes_the_number_it_was_given() {
        const field = createTemporaryObject(realFieldComponent, testCase)
        verify(field, "the field must instantiate")

        // The regression, and its neighbours: every whole number is written
        // out whole, however many zeros it ends in.
        const whole = [0, 1, 5, 10, 100, 1000, 2048, 40000, 50000, 100000,
                       250000, 500000, 1000000, 16777215, -50000, -500000]
        for (let i = 0; i < whole.length; ++i)
            compare(field.formatted(whole[i]), String(whole[i]))

        // Fractions keep six significant figures and lose only the zeros that
        // padding added.
        compare(field.formatted(1.5), "1.5")
        compare(field.formatted(0.1), "0.1")
        compare(field.formatted(0.25), "0.25")
        compare(field.formatted(-0.1), "-0.1")
        compare(field.formatted(0.000123456), "0.000123456")
        compare(field.formatted(1 / 3), "0.333333")

        // Past six figures it goes exponential, and the mantissa is trimmed
        // rather than left padded with zeros.
        compare(field.formatted(1e-7), "1e-7")
        compare(field.formatted(1.5e-7), "1.5e-7")
        compare(field.formatted(1.23456789e21), "1.23457e+21")

        // Nothing at all is nothing, not "NaN".
        compare(field.formatted(NaN), "")
        compare(field.formatted(Infinity), "")

        // An integer box rounds and never writes a point.
        field.integer = true
        compare(field.formatted(500000), "500000")
        compare(field.formatted(2.6), "3")
    }

    /// ...and reads back every notation a reader might write it in.
    ///
    /// The bug this pins down: the boxes validated against DoubleValidator and
    /// IntValidator, which ask the reader's *locale* what a number looks like.
    /// On a German desktop that refused the "." key outright -- "0.2", "-3.5",
    /// "0.0002" and "1.2e3" could not be typed into a manual range at all --
    /// while "0,2" passed the validator and went to parseFloat, which reads up
    /// to the comma and returns zero. Both notations are taken now and both
    /// mean the same number, and the test runs under whatever locale the
    /// machine is set to, because the point of the change is that the locale no
    /// longer decides.
    function test_a_number_box_takes_either_decimal_separator() {
        const field = createTemporaryObject(realFieldComponent, testCase)
        verify(field, "the field must instantiate")

        const same = [["0.2", "0,2", 0.2],
                      ["-3.5", "-3,5", -3.5],
                      ["0.0002", "0,0002", 0.0002],
                      ["1.2e3", "1,2e3", 1200],
                      ["2.5e-7", "2,5e-7", 2.5e-7]]
        for (let i = 0; i < same.length; ++i) {
            const wanted = same[i][2]
            for (const written of [same[i][0], same[i][1]]) {
                field.text = written
                verify(field.acceptableInput, "the box must accept " + written)
                fuzzyCompare(field.number(written), wanted,
                             Math.abs(wanted) * 1e-9 + 1e-12)
            }
        }

        // A number with no fractional part is unchanged by any of it, and a
        // leading separator is a number a reader really does type.
        field.text = "12"
        verify(field.acceptableInput)
        compare(field.number("12"), 12)
        for (const written of [".5", ",5"]) {
            field.text = written
            verify(field.acceptableInput, "the box must accept " + written)
            compare(field.number(written), 0.5)
        }

        // Half-way through being typed into, a box holds something that is not
        // a number yet. commit() asks for one, gets NaN and leaves the value
        // alone -- which is why nothing here has to be refused at the keystroke.
        verify(isNaN(field.number("")))
        verify(isNaN(field.number("-")))
        compare(field.number("1."), 1)
        compare(field.number("1,"), 1)

        // Two separators is not a number in either notation, and neither is a
        // group separator, which is what the locale's own validator used to let
        // through.
        for (const refused of ["1.2.3", "1,2,3", "1.2,3", "1 234", "e5", "x"]) {
            field.text = refused
            verify(!field.acceptableInput, "the box must refuse " + refused)
        }

        // The whole-number box has the same defect and the same fix: on a
        // locale whose group separator is a point, IntValidator accepted
        // "1.234" and parseInt answered one.
        const whole = createTemporaryObject(numberFieldComponent, testCase)
        whole.from = 0
        whole.to = 100000
        whole.text = "1234"
        verify(whole.acceptableInput)
        for (const refused of ["1.234", "1,234", "12.5"]) {
            whole.text = refused
            verify(!whole.acceptableInput, "the index box must refuse " + refused)
        }
    }

    /// One press of an arrow moves a whole-number box by one, and a box of
    /// values by about five per cent of what it is holding.
    function test_a_number_box_steps_by_something_sensible() {
        const field = createTemporaryObject(realFieldComponent, testCase)

        // A whole-number box counts.
        field.integer = true
        compare(field.stepSize, 1)
        field.integer = false

        // Otherwise the step is a proportion, rounded to 1, 2 or 5 times a
        // power of ten so that pressing the arrow walks round numbers.
        field.value = 100
        compare(field.stepSize, 5)
        field.value = 1
        compare(field.stepSize, 0.05)
        field.value = 0.1
        compare(field.stepSize, 0.005)
        field.value = 50000
        compare(field.stepSize, 2000)
        // A box holding nothing has no magnitude to take a share of.
        field.value = 0
        compare(field.stepSize, 1)
        // ...and a caller who knows better says so.
        field.step = 0.25
        compare(field.stepSize, 0.25)
    }

    /// The arrows are the point of issue 6: a number box that can only be
    /// typed into is one the reader cannot nudge.
    function test_the_arrows_move_the_number() {
        const win = createTemporaryObject(fieldWindowComponent, testCase)
        verify(win, "the field window must instantiate")
        waitForRendering(win.real)

        // The box is an input: it reports what was asked for and the owner
        // writes it back, exactly as the sliders do.
        win.whole.committed.connect(amount => win.whole.value = amount)
        win.real.committed.connect(amount => win.real.value = amount)

        // Upper half of the stepper is up, lower half is down.
        const stepper = findChild(win.whole, "") // fall through to geometry
        compare(win.whole.value, 0)
        mouseClick(win.whole, win.whole.width - Theme.gapS,
                   win.whole.height / 4)
        compare(win.whole.value, 1, "the upper arrow must add one")
        mouseClick(win.whole, win.whole.width - Theme.gapS,
                   win.whole.height / 4)
        compare(win.whole.value, 2)
        mouseClick(win.whole, win.whole.width - Theme.gapS,
                   win.whole.height * 3 / 4)
        compare(win.whole.value, 1, "the lower arrow must take one away")

        // ...and it stops at the ends of the range it was given.
        win.whole.value = 0
        mouseClick(win.whole, win.whole.width - Theme.gapS,
                   win.whole.height * 3 / 4)
        compare(win.whole.value, 0, "a box at the bottom of its range stays")

        // The keyboard says the same thing.
        win.whole.forceActiveFocus()
        keyClick(Qt.Key_Up)
        compare(win.whole.value, 1)
        keyClick(Qt.Key_Down)
        compare(win.whole.value, 0)

        // A value box steps by its proportion, and lands on round numbers.
        win.real.value = 100
        mouseClick(win.real, win.real.width - Theme.gapS, win.real.height / 4)
        compare(win.real.value, 105)
        win.real.value = 103
        mouseClick(win.real, win.real.width - Theme.gapS, win.real.height / 4)
        compare(win.real.value, 105, "a step lands on the round number above")
    }

    /// Pressing somewhere else puts the caret down. Qt Quick does not do this
    /// on its own: a TextInput keeps focus until something asks for it, and
    /// most of this window never asks.
    function test_pressing_outside_a_text_box_takes_the_keyboard_off_it() {
        const win = createTemporaryObject(windowComponent, testCase)
        verify(win, "the application window must instantiate")
        waitForRendering(win.contentItem)

        const filter = findChild(win, "treeFilter")
        verify(filter, "the tree's filter box must be reachable")
        filter.forceActiveFocus()
        verify(filter.activeFocus, "the box must take focus when asked")

        // A press inside it keeps the caret where the reader put it.
        mouseClick(filter, filter.width / 2, filter.height / 2)
        verify(filter.activeFocus, "a press inside the box must not end the edit")

        // A press anywhere else ends the edit.
        mouseClick(win.contentItem, win.width / 2, win.height / 2)
        verify(!filter.activeFocus,
               "a press outside the box must take the keyboard off it")
    }

    /// Issue 1's own example, end to end: an x axis stated for one dataset is
    /// not an x axis for the next one, and is still there when the reader comes
    /// back to the one it was stated for.
    function test_a_view_s_settings_belong_to_the_dataset_they_were_set_on() {
        verify(select("/matrix"))
        const view = createTemporaryObject(dataComponent, testCase, viewSize)
        waitForRendering(view)
        view.show("plot")
        const plot = findChild(view, "plotSurface")
        verify(plot, "the plot surface must be reachable")

        compare(plot.locks.length, 0)
        plot.rangeStart = 10
        plot.lock("start")
        plot.rangeStep = 0.25
        plot.lock("step")
        plot.gridMode = "none"
        plot.colorMode = "viridis"
        compare(plot.resolved.start, 10)
        compare(plot.resolved.step, 0.25)

        // Another dataset opens on the defaults rather than on the last
        // dataset's answers.
        verify(select("/cube"))
        waitForRendering(view)
        compare(plot.locks.length, 0)
        compare(plot.resolved.start, 0)
        compare(plot.resolved.step, 1)
        compare(plot.gridMode, "loose")
        compare(plot.colorMode, "spectrum")

        // ...and coming back finds what was left there.
        verify(select("/matrix"))
        waitForRendering(view)
        compare(plot.locks.length, 2)
        compare(plot.resolved.start, 10)
        compare(plot.resolved.step, 0.25)
        compare(plot.gridMode, "none")
        compare(plot.colorMode, "viridis")
    }

    /// The same, for the table: a column width fitted to one dataset is a guess
    /// about the next.
    function test_the_grid_s_own_settings_are_per_dataset_as_well() {
        verify(select("/matrix"))
        const view = createTemporaryObject(dataComponent, testCase, viewSize)
        waitForRendering(view)
        const table = findChild(view, "tableSurface")

        compare(table.autoWidth, true)
        table.autoWidth = false
        table.columnWidth = Theme.s13
        table.gridLines = false

        verify(select("/cube"))
        waitForRendering(view)
        compare(table.autoWidth, true)
        compare(table.gridLines, true)

        verify(select("/matrix"))
        waitForRendering(view)
        compare(table.autoWidth, false)
        compare(table.columnWidth, Theme.s13)
        compare(table.gridLines, false)
    }

    /// The picture's ground follows the theme until somebody chooses one for a
    /// particular dataset, and then it is that dataset's and no other's.
    function test_the_image_ground_follows_the_theme_until_it_is_chosen() {
        verify(select("/matrix"))
        const view = createTemporaryObject(dataComponent, testCase, viewSize)
        waitForRendering(view)
        view.show("image")
        const image = findChild(view, "imageSurface")
        verify(image, "the image surface must be reachable")

        verify(!image.backgroundCustom)
        compare(String(image.ground), String(Theme.imageGround))
        // Black in the dark theme, white in the light one.
        Theme.dark = !Theme.dark
        compare(String(image.ground), String(Theme.imageGround))
        Theme.dark = !Theme.dark

        image.backgroundColor = "#336699"
        image.backgroundCustom = true
        compare(String(image.ground), String(Qt.color("#336699")))
        // ...and now the theme does not overrule it.
        Theme.dark = !Theme.dark
        compare(String(image.ground), String(Qt.color("#336699")))
        Theme.dark = !Theme.dark

        // A chosen ground is a statement about this dataset and no other.
        verify(select("/cube"))
        waitForRendering(view)
        verify(!image.backgroundCustom)
        compare(String(image.ground), String(Theme.imageGround))

        verify(select("/matrix"))
        waitForRendering(view)
        verify(image.backgroundCustom)
        compare(String(image.ground), String(Qt.color("#336699")))

        // The checkerboard is what a picture stands on until the reader says
        // otherwise: a flat ground leaves them deciding whether a pale corner
        // is a pale pixel or no pixel at all.
        verify(image.checkerboard, "the checkerboard is the default ground")

        // It overrides the colour outright, which is why the swatch beside it
        // goes dead while it is on -- and the chosen colour is still there
        // underneath, for when it is switched off again.
        image.checkerboard = false
        compare(String(image.ground), String(Qt.color("#336699")))
        image.checkerboard = true
        compare(image.checkerboard, true)
    }

    /// RGBA is offered where there is a fourth plane to read as coverage, and
    /// the three colours open on the first three channels rather than all on
    /// the first one.
    function test_the_image_panel_offers_rgba_where_there_is_a_fourth_plane() {
        verify(select("/hypercube")) // 2 x 3 x 4 x 5
        const panel = createTemporaryObject(imageSettingsComponent, testCase,
                                            { width: Theme.railWidth, height: 700 })
        verify(panel, "the image settings panel must instantiate")
        const image = AppController.datasetImage

        compare(panel.colorModes.length, 3)
        compare(panel.colorModes[2].value, DatasetImage.Rgba)

        image.channelDimension = 2 // four deep
        compare(image.channelCount, 4)
        image.colorMode = DatasetImage.Rgb
        // The defaults are the first three channels in order. They used to be
        // clamped to zero while there was no colour axis, so a truecolour
        // picture came out grey.
        compare(image.redIndex, 0)
        compare(image.greenIndex, 1)
        compare(image.blueIndex, 2)

        image.colorMode = DatasetImage.Rgba
        compare(image.colorMode, DatasetImage.Rgba)
        compare(image.alphaIndex, 3)
        verify(panel.isRgba)
        verify(panel.isColour)

        // A colour axis three deep cannot supply a coverage, so the mode
        // reports what can actually be drawn.
        image.channelDimension = 1 // three deep
        compare(image.channelCount, 3)
        compare(image.colorMode, DatasetImage.Rgb)
        verify(!panel.isRgba)
    }

    /// WCAG relative luminance, which is what "reads as strongly as" is
    /// measured in throughout Theme.
    function luminance(colour) {
        const c = Qt.color(colour)
        const linear = v => v <= 0.03928 ? v / 12.92
                                         : Math.pow((v + 0.055) / 1.055, 2.4)
        return 0.2126 * linear(c.r) + 0.7152 * linear(c.g) + 0.0722 * linear(c.b)
    }

    function contrast(a, b) {
        const first = luminance(a)
        const second = luminance(b)
        const light = Math.max(first, second)
        const dark = Math.min(first, second)
        return (light + 0.05) / (dark + 0.05)
    }

    /// Every role reads against the light theme's ground about as strongly as
    /// it reads against the dark theme's.
    ///
    /// This is issue 9 stated as an assertion. Upstream's light scope is one
    /// line of CSS labelled "for print/marketing blocks": a raised card was
    /// pure white on a pure white ground, a hover carried a sixth of the
    /// separation its dark counterpart did, and the strongest rule in the UI
    /// was drawn as the faintest one.
    function test_the_light_theme_reads_as_strongly_as_the_dark_one() {
        const roles = ["surface", "surfaceRaised", "surfaceHover", "surfaceActive",
                       "border", "borderStrong", "borderGuide",
                       "textPrimary", "textSecondary", "textDisabled",
                       "warning", "danger", "info"]

        const was = Theme.dark
        Theme.dark = true
        const dark = {}
        for (let i = 0; i < roles.length; ++i)
            dark[roles[i]] = contrast(Theme[roles[i]], Theme.background)

        Theme.dark = false
        for (let i = 0; i < roles.length; ++i) {
            const role = roles[i]
            const light = contrast(Theme[role], Theme.background)
            verify(light >= dark[role] * 0.55,
                   role + " reads at " + light.toFixed(2)
                   + ":1 in the light theme against " + dark[role].toFixed(2)
                   + ":1 in the dark one")
        }

        // ...and the elevation order runs the same way in both: further from
        // the ground is further from it, never back towards it.
        verify(contrast(Theme.surfaceRaised, Theme.background)
               > contrast(Theme.surface, Theme.background),
               "a card must lift further off the ground than the surface under it")
        Theme.dark = was
    }

    /// A ground that fades keeps its hue and moves only its alpha.
    ///
    /// "transparent" is rgba(0, 0, 0, 0) -- black with no alpha -- and Qt
    /// interpolates a colour animation component by component, so a ground
    /// crossfading to it passes through half-alpha black. Over the dark theme's
    /// true-black ground that is invisible; over the light theme's white one it
    /// is a grey flash on the way in and another on the way out, and a pointer
    /// crossing a tree of names sets off one per row. That was the flicker.
    function test_a_ground_that_fades_keeps_its_hue() {
        const clear = Theme.clear(Theme.surfaceHover)
        compare(clear.a, 0)
        compare(clear.r, Theme.surfaceHover.r)
        compare(clear.g, Theme.surfaceHover.g)
        compare(clear.b, Theme.surfaceHover.b)
        // And it is not what "transparent" means, which is the whole point.
        verify(String(clear) !== String(Qt.color("transparent"))
               || Theme.surfaceHover.r === 0,
               "a cleared hover colour must not collapse to black")

        // ...and it is what a tree row actually stands on when the pointer is
        // somewhere else.
        const win = createTemporaryObject(treeWindowComponent, testCase)
        waitForRendering(win.tree)
        settleTree(win)
        const row = findTreeRow(win.tree, "cube")
        verify(row, "the tree must draw a row for the dataset")
        verify(!row.current, "...and this test needs one that is not selected")
        compare(String(row.children[0].color), String(clear))
    }

    /// The table's cells land on whole physical pixels.
    ///
    /// At a fractional display scale -- 125%, 150% -- a whole number of logical
    /// pixels is not a whole number of physical ones, so the seams of a table
    /// of them fall alternately on and between the pixels of the screen and the
    /// rules drawn at those seams come out alternately one pixel wide and two.
    /// That is the grid that "looks weird on my monitor".
    function test_the_grid_lands_on_whole_device_pixels() {
        const was = Theme.pixelRatio

        // At a whole ratio the snapping is the identity and costs nothing.
        Theme.pixelRatio = 1
        compare(Theme.snap(61), 61)
        compare(Theme.hairline, Theme.borderWidth)
        Theme.pixelRatio = 2
        compare(Theme.snap(61), 61)

        Theme.pixelRatio = 1.5
        const whole = (value) => Math.abs(value * Theme.pixelRatio
                                          - Math.round(value * Theme.pixelRatio)) < 1e-6
        verify(whole(Theme.snap(61)), "a snapped width must be whole in physical pixels")
        verify(whole(Theme.snap(28)))
        verify(whole(Theme.hairline), "and so must a rule")
        verify(Theme.snap(61) >= 61, "snapping never loses a pixel")

        verify(select("/matrix"))
        const view = createTemporaryObject(dataComponent, testCase, viewSize)
        waitForRendering(view)
        const grid = findChild(view, "valueGrid")
        verify(grid, "the value grid must be reachable")

        // Every measurement the table is ruled by. The seams of the grid are
        // multiples of these, so whole cells mean whole seams all the way
        // across.
        verify(whole(grid.rowHeight), "row height: " + grid.rowHeight)
        verify(whole(grid.columnWidth), "column width: " + grid.columnWidth)
        verify(whole(grid.indexWidth), "index column: " + grid.indexWidth)
        verify(whole(grid.ruleWidth), "rule: " + grid.ruleWidth)

        Theme.pixelRatio = was
    }

    /// Auto width fits the columns of whatever is selected, not of whichever
    /// dataset happened to be first.
    ///
    /// `measure()` walks the cells the model has already read, and those are
    /// read on the HDF5 thread: when the selection changes there are none,
    /// because setSource() has just emptied the cache. The one remeasure the
    /// grid did ran there, against an empty model, and took 0. Nothing then
    /// looked again once the blocks landed, so the columns kept the floor
    /// width. It looked intermittent rather than broken because `measure()` is
    /// also driven off the TableView's visible range -- a switch between two
    /// datasets of *different* shape moves that range after the data arrives
    /// and fits the columns by accident, which is why the bug reads as "auto
    /// sizing breaks when switching between datasets".
    function test_auto_width_fits_every_dataset_and_not_only_the_first() {
        const view = createTemporaryObject(dataComponent, testCase, viewSize)
        const grid = findChild(view, "valueGrid")
        verify(grid, "the value grid must be reachable")

        /// Wait for the blocks to land and the width to follow them.
        const fitted = (path) => {
            verify(select(path))
            tryVerify(() => grid.widestCell > 0, 10000,
                      "nothing was ever measured for " + path
                      + ", so its columns are at the floor width")
            waitForRendering(view)
        }

        verify(grid.autoWidth, "auto width is the default")

        // The first selection this grid ever sees.
        fitted("/matrix")

        // ...and the next, which is the half that was broken. Wider values
        // than /matrix's, so the fit is visible in the width and not only in
        // the measurement: past the floor the slider would give it.
        fitted("/compressed")
        verify(grid.columnWidth > Theme.s11,
               "a column of four-digit values must be wider than the floor: "
               + grid.columnWidth)

        // ...and back, where the shape returns to what it was two selections
        // ago and the visible range therefore does not move at all.
        fitted("/matrix")
    }

    function test_file_system_helpers_answer_the_picker() {
        verify(String(FileSystem.home).indexOf("file://") === 0)
        verify(FileSystem.places.length > 0)
        verify(FileSystem.isFolder(FileSystem.toLocalPath(FileSystem.home)))
        verify(!FileSystem.isFolder(fixture))
        verify(FileSystem.exists(fixture))
        compare(FileSystem.toLocalPath(FileSystem.folderOf(fixture)),
                fixture.substring(0, fixture.lastIndexOf("/")))
    }

    function test_info_view_instantiates_and_lists_rows() {
        verify(select("/matrix"))
        const view = createTemporaryObject(infoComponent, testCase)
        verify(view, "InfoView must instantiate")
        // Path, Name, Kind, Type, Shape, Elements, Layout, Storage, Attributes
        verify(AppController.infoModel.rowCount() >= 8)
    }

    /// The datatype panel opens a compound out until nothing is left but base
    /// types. What says which member a row belongs to is the indent, so the
    /// indent is what this asserts -- the depth reaching the view rather than
    /// merely reaching the model.
    function test_a_compound_datatype_is_drawn_as_an_indented_tree() {
        verify(select("/compound"))
        const view = createTemporaryObject(infoComponent, testCase, viewSize)
        waitForRendering(view)

        let members = null
        let member = null
        for (const item of selectableTexts(view)) {
            if (item.text === "Members")
                members = item
            if (item.text === "id")
                member = item
        }
        verify(members, "the one-line list of names stays where it was")
        verify(member, "and the tree names each member on a row of its own")
        verify(member.x > members.x,
               "a tree row is drawn further in than the rows above it")

        // And no heading at all where there is nothing to open out: float64
        // resolves to float64, which the Type row above has already said.
        verify(select("/matrix"))
        waitForRendering(view)
        for (const item of selectableTexts(view))
            verify(item.text !== "Resolves to", "a plain type gets no tree")
    }

    /// A viewer shows facts about a file that a reader has some other program
    /// to paste them into. A Text is a picture of a string: it can be read and
    /// not taken, which for a path or a filter name is the difference between
    /// using the answer and typing it back in by hand.
    function test_every_string_on_the_information_tab_can_be_copied() {
        verify(select("/matrix"))
        const view = createTemporaryObject(infoComponent, testCase, viewSize)
        waitForRendering(view)

        const strings = selectableTexts(view)
        verify(strings.length >= 8,
               "the Information tab draws " + strings.length
               + " strings; every row has a label and a value")

        // Not merely present: actually selectable, and giving back exactly
        // what it is showing.
        let path = null
        for (const item of strings) {
            if (item.text === "/matrix")
                path = item
        }
        verify(path, "the object panel must show the path")
        verify(path.selectByMouse)
        verify(path.readOnly, "an information panel is not an editor")
        path.selectAll()
        compare(path.selectedText, "/matrix")
        path.deselect()
    }

    /// A rule under the last row of a panel is the panel's own border drawn a
    /// second time, one hairline above itself.
    function test_a_panel_rules_between_its_rows_and_not_under_them() {
        verify(select("/matrix"))
        const win = createTemporaryObject(infoWindowComponent, testCase)
        verify(win, "the information window must instantiate")
        const view = win.info
        waitForRendering(view)

        // The rows of one panel, found by the property that says which of them
        // is the last.
        const rows = []
        const visit = (item) => {
            if (item.last !== undefined && item.modelData !== undefined)
                rows.push(item)
            for (let i = 0; i < item.children.length; ++i)
                visit(item.children[i])
        }
        visit(view)
        verify(rows.length > 0, "the tab must draw rows")

        let lastRows = 0
        for (const row of rows) {
            const rules = []
            for (let i = 0; i < row.children.length; ++i) {
                if (row.children[i].color !== undefined
                        && row.children[i].height === Theme.hairline)
                    rules.push(row.children[i])
            }
            compare(rules.length, 1, "every row declares its separator")
            compare(rules[0].visible, !row.last,
                    "the rule under \"" + row.modelData.label + "\"")
            if (row.last)
                ++lastRows
        }
        verify(lastRows > 0, "some row has to be the last one")
    }

    /// Every row of a panel is a whole number of physical pixels tall.
    ///
    /// This is the table's rule -- see Theme.snap -- applied where it was
    /// missing. A row is as tall as its text and text measures in fractions of
    /// a logical pixel, so at a fractional display scale every row boundary in
    /// the panel lands at a different fraction of a physical one and the
    /// hairline drawn there is smeared over two rows of the screen at a
    /// different share of each. Left unsnapped at 150% every row here came out
    /// 27 logical pixels, which is 40.5 physical: every separator in the tab
    /// on a half-pixel, which is what "these lines are too thin and render
    /// inconsistently" looks like from the other side of the screen.
    ///
    /// 1.5 rather than the ratio the test machine happens to have: headless is
    /// 1.0, where snapping is the identity and this would assert nothing.
    function test_panel_rows_land_on_the_device_pixel_grid() {
        const was = Theme.pixelRatio
        Theme.pixelRatio = 1.5
        try {
            verify(select("/compressed"))
            const win = createTemporaryObject(infoWindowComponent, testCase)
            verify(win, "the information window must instantiate")
            waitForRendering(win.info)

            const rows = []
            const visit = (item) => {
                if (item.last !== undefined && item.modelData !== undefined)
                    rows.push(item)
                for (let i = 0; i < item.children.length; ++i)
                    visit(item.children[i])
            }
            visit(win.info)
            verify(rows.length > 0, "the tab must draw rows")

            for (const row of rows) {
                const physical = row.height * 1.5
                fuzzyCompare(physical, Math.round(physical), 1e-6,
                             "the row for \"" + row.modelData.label
                             + "\" is " + row.height + " logical pixels, which"
                             + " is " + physical + " physical ones")
            }

            // And the rule itself, for the same reason: one logical pixel is
            // one and a half physical at this scale, and half a pixel of a
            // line is half its colour.
            const rulePhysical = Theme.hairline * 1.5
            fuzzyCompare(rulePhysical, Math.round(rulePhysical), 1e-6,
                         "a hairline must be a whole number of pixels")
        } finally {
            Theme.pixelRatio = was
        }
    }

    /// Every card is on screen, and a card that will not fit scrolls rather
    /// than the page under it.
    ///
    /// The tab was one long scrolling page until this, which answered "what is
    /// this object?" with a column to be travelled down: the panel a reader
    /// wanted was as likely to be below the fold as not, and nothing at the
    /// top said whether there was anything under it. Both halves are asserted
    /// here because either alone is a different layout -- a page that fits
    /// everything by hiding the ends of it, or one that shows every card whole
    /// by putting half of them past the bottom of the window.
    ///
    /// Two window heights rather than one, and the same selection in both: the
    /// tall one is where nothing may scroll, because a card that scrolls with
    /// room to spare is a card drawn short of what it was given.
    function test_the_cards_scroll_and_the_information_tab_does_not() {
        verify(select("/compound"))

        const roomy = createTemporaryObject(infoWindowComponent, testCase)
        verify(roomy, "the information window must instantiate")
        waitForRendering(roomy.info)

        const whole = infoCards(roomy.info)
        verify(whole.length >= 4, "the tab must draw its cards")
        for (const card of whole) {
            compare(card.scrolls, false,
                    "\"" + card.title + "\" has room and must not scroll")
            fuzzyCompare(card.height, card.implicitHeight, 1,
                         "\"" + card.title + "\" is drawn at its full height")
        }

        // The same cards in a window too short for them. Nothing leaves the
        // screen; what does not fit goes behind a scrollbar instead.
        const cramped = createTemporaryObject(infoWindowComponent, testCase,
                                              { height: 500 })
        verify(cramped, "the short information window must instantiate")
        waitForRendering(cramped.info)

        const cards = infoCards(cramped.info)
        compare(cards.length, whole.length, "every card is still drawn")

        let scrolling = 0
        for (const card of cards) {
            const bottom = card.mapToItem(cramped.info, 0, card.height).y
            verify(bottom <= cramped.info.height,
                   "\"" + card.title + "\" ends " + Math.round(bottom)
                   + " into a tab " + Math.round(cramped.info.height) + " tall")
            verify(card.height >= Math.min(card.implicitHeight,
                                           card.minimumHeight) - 1,
                   "\"" + card.title + "\" keeps its floor")
            if (card.scrolls)
                ++scrolling
        }
        verify(scrolling > 0,
               "a window this short has to be holding something back")

        // And what is held back is reachable: the body moves, and it moves far
        // enough to put the last row of the card on screen.
        for (const card of cards) {
            if (!card.scrolls)
                continue
            const viewport = flickableIn(card)
            verify(viewport, "\"" + card.title + "\" must scroll something")
            verify(viewport.contentHeight > viewport.height,
                   "\"" + card.title + "\" has more content than viewport")
            viewport.contentY = viewport.contentHeight - viewport.height
            fuzzyCompare(viewport.contentY,
                         viewport.contentHeight - viewport.height, 1,
                         "\"" + card.title + "\" reaches its last row")
        }
    }

    /// The Panels on the Information tab, found by the two properties a card
    /// declares about its own height.
    function infoCards(root) {
        const found = []
        const visit = (item) => {
            if (item.scrolls !== undefined && item.minimumHeight !== undefined)
                found.push(item)
            for (let i = 0; i < item.children.length; ++i)
                visit(item.children[i])
        }
        visit(root)
        return found
    }

    /// The viewport inside one, found by a property only a Flickable has.
    function flickableIn(item) {
        for (let i = 0; i < item.children.length; ++i) {
            const child = item.children[i]
            if (child.maximumFlickVelocity !== undefined)
                return child
            const deeper = flickableIn(child)
            if (deeper)
                return deeper
        }
        return null
    }

    /// Every TextEdit under `root` -- which on the Information tab is every
    /// string it draws.
    function selectableTexts(root) {
        const found = []
        const visit = (item) => {
            if (item.selectByMouse !== undefined && item.readOnly !== undefined)
                found.push(item)
            for (let i = 0; i < item.children.length; ++i)
                visit(item.children[i])
        }
        visit(root)
        return found
    }

    /// The two tab layers are one: information sits beside table, plot and
    /// image rather than above a second strip holding them.
    function test_one_tab_strip_holds_the_information_view_and_the_three_others() {
        const win = createTemporaryObject(windowComponent, testCase)
        verify(win, "the window must instantiate")
        waitForRendering(win.contentItem)

        compare(win.tabs.length, 4)
        compare(win.tabs.map((tab) => tab.id).join(","), "info,table,plot,image")
        compare(win.currentTabId, "info")

        verify(select("/matrix"))
        for (const id of ["table", "plot", "image", "info"]) {
            win.selectTab(id)
            waitForRendering(win.contentItem)
            compare(win.currentTabId, id)
        }
    }

    /// The plot and the image are for numbers. The strip keeps its shape on a
    /// dataset they cannot draw and refuses them instead.
    function test_the_strip_refuses_the_tabs_this_selection_cannot_offer() {
        const win = createTemporaryObject(windowComponent, testCase)
        waitForRendering(win.contentItem)

        verify(select("/matrix"))
        for (const id of ["info", "table", "plot", "image"])
            verify(win.tabAvailable(id), id + " must be offered for a matrix")

        // Standing on the plot when the selection turns to text: the view
        // falls back to the table, and the strip follows it rather than
        // marking a tab that is showing nothing.
        win.selectTab("plot")
        compare(win.currentTabId, "plot")
        verify(select("/str_vlen"))
        waitForRendering(win.contentItem)
        compare(win.currentTabId, "table")
        verify(win.tabAvailable("table"))
        verify(!win.tabAvailable("plot"))
        verify(!win.tabAvailable("image"))
        // And asking for one anyway does nothing.
        win.selectTab("image")
        compare(win.currentTabId, "table")
    }

    /// The slice line is the fastest way to say which elements to show, and
    /// the only one that does not go through the panel.
    function test_the_slice_line_can_be_typed_into() {
        verify(select("/hypercube")) // 2 x 3 x 4 x 5
        const win = createTemporaryObject(windowComponent, testCase)
        waitForRendering(win.contentItem)
        win.selectTab("table")
        waitForRendering(win.contentItem)

        const box = findChild(win.contentItem, "sliceInput")
        verify(box, "the slice box must be reachable")
        // It opens holding what the table is already showing.
        compare(box.text, AppController.sliceText)
        compare(box.text, ":, :, :, :")

        box.forceActiveFocus()
        box.text = "1, [0,2], 1:3, :"
        box.textEdited()
        // Typing checks but does not apply: the table is still whole.
        compare(AppController.datasetModel.rowCount(), 24)
        keyClick(Qt.Key_Return)
        waitForRendering(win.contentItem)

        compare(AppController.sliceExpression, "/hypercube[1, [0,2], 1:3, :]")
        compare(AppController.datasetModel.rowCount(), 4)   // 1 * 2 * 2
        compare(AppController.datasetModel.columnCount(), 5)

        // And the panel now shows the same selection in its own terms.
        const setup = AppController.tableSetupModel
        compare(setup.data(setup.index(0, 0), TableSetupModel.ModeRole),
                TableSetupModel.Index)
        compare(setup.data(setup.index(1, 0), TableSetupModel.ModeRole),
                TableSetupModel.Custom)
        compare(setup.data(setup.index(2, 0), TableSetupModel.ModeRole),
                TableSetupModel.Range)
        compare(setup.data(setup.index(3, 0), TableSetupModel.ModeRole),
                TableSetupModel.All)
    }

    /// The one-line selection box: how a compound is written.
    ///
    /// It replaces the bracketed form for a compound and for nothing else,
    /// which is also how a reader finds out the notation exists -- nothing in
    /// the bar mentions members until there is one to name.
    function test_a_compound_is_written_as_one_line_and_nothing_else_is() {
        verify(select("/matrix"))
        const win = createTemporaryObject(windowComponent, testCase)
        waitForRendering(win.contentItem)
        win.selectTab("table")
        waitForRendering(win.contentItem)

        const box = findChild(win.contentItem, "sliceSelectionInput")
        const body = findChild(win.contentItem, "sliceInput")
        verify(box, "the one-line box is built whether or not it is drawn")
        verify(body)
        compare(box.parent.visible, false, "a matrix of numbers has no members")
        compare(body.visible, true, "so it is written between brackets")

        verify(select("/compound"))
        waitForRendering(win.contentItem)
        compare(box.parent.visible, true, "a compound is written as one line")
        compare(body.visible, false, "and the brackets are the reader's to write")
        compare(box.text, "[:]", "which opens holding the slice and no member")

        // The hint stands in for what is not there, so the box is findable and
        // names both halves.
        const hint = findChild(win.contentItem, "sliceSelectionHint")
        verify(hint)
        compare(hint.visible, false, "the box is not empty: it holds the slice")
        compare(hint.text, "[:].member")
        verify(box.parent.width > 0, "and the well is a well")
    }

    /// Naming a member is what turns a grid of structs into a column of
    /// numbers, and the plot and the image exist for it afterwards.
    function test_naming_a_member_draws_it() {
        verify(select("/compound")) // {id: int32, value: float64} x 2
        const win = createTemporaryObject(windowComponent, testCase)
        waitForRendering(win.contentItem)
        win.selectTab("table")
        waitForRendering(win.contentItem)

        compare(AppController.datasetIsCompound, true)
        compare(AppController.datasetIsNumeric, false)

        const box = findChild(win.contentItem, "sliceSelectionInput")
        box.forceActiveFocus()
        box.text = "[:].value"
        box.textEdited()
        // Typing checks and does not apply, as the bracketed box does.
        compare(AppController.datasetIsNumeric, false)
        keyClick(Qt.Key_Return)
        waitForRendering(win.contentItem)

        compare(AppController.memberText, ".value")
        compare(AppController.selectionText, "[:].value")
        compare(AppController.datasetIsNumeric, true)
        // Still a compound, so the line the reader typed into is still one box.
        compare(AppController.datasetIsCompound, true)
        compare(box.parent.visible, true)

        // Put it back: a chain is remembered per dataset, which is the point of
        // it, so a test that leaves one leaves it for every test after.
        AppController.applyMember("")
    }

    /// The subscript and the chain are one statement, and one commit.
    ///
    /// Two boxes made this two: the chain, then the shape it produced, then the
    /// subscript over that shape -- with a selection nobody asked for in
    /// between. Written as one line it is read whole and applied whole.
    function test_the_subscript_and_the_member_are_applied_together() {
        verify(select("/compound")) // {id: int32, value: float64} x 2
        const win = createTemporaryObject(windowComponent, testCase)
        waitForRendering(win.contentItem)
        win.selectTab("table")
        waitForRendering(win.contentItem)

        const box = findChild(win.contentItem, "sliceSelectionInput")
        box.forceActiveFocus()
        box.text = "[1].value"
        keyClick(Qt.Key_Return)
        waitForRendering(win.contentItem)

        compare(AppController.memberText, ".value")
        compare(AppController.sliceText, "1")
        compare(AppController.selectionText, "[1].value")

        AppController.applyMember("")
    }

    /// The box prints back the canonical line, whatever was typed into it.
    ///
    /// A subscript left off is the whole of the dataset, which the grammar
    /// already means by an empty one -- so `.value` reads, and comes back
    /// spelled out. That the line the reader ends up holding is the line the
    /// views are showing is the same contract the bracketed box keeps: "0:4"
    /// over a four-long axis comes back as ":".
    ///
    /// The other rewriting this box does -- a subscript written on the chain
    /// moving onto the slice in front of it -- needs an array member, which
    /// this fixture has none of. It is asserted as arithmetic in
    /// tests/test_member.cpp, where the identity itself lives.
    function test_the_box_prints_back_the_line_the_views_are_showing() {
        verify(select("/compound"))
        const win = createTemporaryObject(windowComponent, testCase)
        waitForRendering(win.contentItem)
        win.selectTab("table")
        waitForRendering(win.contentItem)

        const box = findChild(win.contentItem, "sliceSelectionInput")
        box.forceActiveFocus()
        box.text = ".value"
        box.textEdited()
        compare(AppController.selectionError(box.text), "",
                "a line with no subscript is the whole of the dataset")
        keyClick(Qt.Key_Return)
        waitForRendering(win.contentItem)

        compare(AppController.memberText, ".value")
        compare(AppController.sliceText, ":")
        compare(box.text, "[:].value", "and the box holds what the views show")

        AppController.applyMember("")
    }

    /// The members of a compound are in the file and nowhere the reader can
    /// see them. Offering them here is what makes the box writable at all --
    /// `.position.x` is otherwise something you have to already know.
    function test_the_selection_box_offers_what_could_go_in_it() {
        verify(select("/compound")) // {id: int32, value: float64}
        const win = createTemporaryObject(windowComponent, testCase)
        waitForRendering(win.contentItem)
        win.selectTab("table")
        waitForRendering(win.contentItem)

        const box = findChild(win.contentItem, "sliceSelectionInput")
        box.forceActiveFocus()
        box.text = "[:]"
        box.textEdited()
        waitForRendering(win.contentItem)

        // A line with no chain on it yet asks for everything that could go
        // there. No file is touched for it: a chain resolves against the
        // datatype already described, which is why this answers on the
        // keystroke. Whole lines, so the subscript already written comes back
        // with each of them.
        compare(AppController.selectionCompletions("[:]").length, 2)
        compare(AppController.selectionCompletions("[:]")[0], "[:].id")
        const list = findChild(win, "sliceSelectionCompletion")
        verify(list, "the box must have its list")
        verify(list.visible, "and it is up while the box is being written in")
        // Drawn, not merely flagged: a popup whose content was never built
        // would have exactly this `visible` and nothing on screen.
        const rows = findChild(win, "completionList")
        verify(rows, "the list must have built its rows")
        compare(rows.count, 2)

        // The two share only the dot, so Tab writes that and leaves the
        // choosing to the reader -- a shell's behaviour, which is the one a
        // reader already has.
        keyClick(Qt.Key_Tab)
        compare(box.text, "[:].")

        box.text = "[:].v"
        box.textEdited()
        keyClick(Qt.Key_Tab)
        compare(box.text, "[:].value")

        keyClick(Qt.Key_Return)
        waitForRendering(win.contentItem)
        compare(AppController.memberText, ".value")
        compare(AppController.datasetIsNumeric, true)

        AppController.applyMember("")
    }

    /// Escape means the list while the list is up, and the box after that.
    /// Two meanings for one key, in the order the reader put the things there.
    function test_escape_dismisses_the_list_before_it_reverts_the_box() {
        verify(select("/compound"))
        const win = createTemporaryObject(windowComponent, testCase)
        waitForRendering(win.contentItem)
        win.selectTab("table")
        waitForRendering(win.contentItem)

        const box = findChild(win.contentItem, "sliceSelectionInput")
        const list = findChild(win, "sliceSelectionCompletion")
        box.forceActiveFocus()
        box.text = "[:].v"
        box.textEdited()
        waitForRendering(win.contentItem)
        verify(list.visible)

        keyClick(Qt.Key_Escape)
        waitForRendering(win.contentItem)
        verify(!list.visible, "the list goes")
        compare(box.text, "[:].v", "and what was typed stays")

        keyClick(Qt.Key_Escape)
        waitForRendering(win.contentItem)
        compare(box.text, "[:]", "the second one puts back what the table shows")
    }

    /// A line that does not read is left in the box, in amber, with the
    /// reason -- the bracketed box's contract, because it is the same
    /// contract. Neither half of it is applied: a selection is one statement.
    function test_a_selection_that_does_not_read_says_so_and_changes_nothing() {
        verify(select("/compound"))
        const win = createTemporaryObject(windowComponent, testCase)
        waitForRendering(win.contentItem)
        win.selectTab("table")
        waitForRendering(win.contentItem)

        const box = findChild(win.contentItem, "sliceSelectionInput")
        const note = findChild(win.contentItem, "sliceNote")
        verify(note)

        box.forceActiveFocus()
        box.text = "[:].nonesuch"
        box.textEdited()
        verify(note.text.indexOf("nonesuch") >= 0,
               "the note names the member that is not there: " + note.text)
        // And the list of the ones that are, which is the useful half.
        verify(note.text.indexOf("value") >= 0, note.text)

        keyClick(Qt.Key_Return)
        waitForRendering(win.contentItem)
        compare(AppController.memberText, "", "nothing was applied")
        compare(box.text, "[:].nonesuch", "and what was typed is still there")

        // A subscript the shape cannot take is refused the same way, and the
        // member beside it is not applied either.
        box.text = "[9].value"
        box.textEdited()
        verify(note.text !== "", "a subscript past the end is refused: " + note.text)
        keyClick(Qt.Key_Return)
        waitForRendering(win.contentItem)
        compare(AppController.memberText, "", "the member went nowhere with it")
        compare(AppController.sliceText, ":")

        // A bracket nobody closed is a line, not a slice, and says so.
        box.text = "[0.value"
        box.textEdited()
        verify(note.text.indexOf("closing bracket") >= 0, note.text)

        // Escape puts back what the table is showing.
        keyClick(Qt.Key_Escape)
        waitForRendering(win.contentItem)
        compare(box.text, "[:]")
    }

    /// The well holds the line *and* the room to grow it, the whole well is one
    /// target, and none of the three parts is cut short while the bar has room.
    ///
    /// It used to be sized from the path at its full length while drawing the
    /// path capped, so the box got the difference between the two: nothing at
    /// all on a path shorter than the cap, and a couple of characters just past
    /// it. It also added a fixed 112 pixels of slack to its own width, which is
    /// the dead space that used to sit at the end of the bar on every slice.
    function test_the_slice_box_keeps_room_to_type_and_takes_the_whole_well() {
        verify(select("/hypercube"))
        const win = createTemporaryObject(windowComponent, testCase)
        waitForRendering(win.contentItem)
        win.selectTab("table")
        waitForRendering(win.contentItem)

        const box = findChild(win.contentItem, "sliceInput")
        const line = box.parent
        const field = line.parent
        const pathLabel = findChild(win.contentItem, "slicePath")
        const bracket = findChild(win.contentItem, "sliceCloseBracket")

        // The slack after the closing bracket: enough to click into, and not
        // the hand's width it used to be.
        const room = field.width - Theme.gapM * 2 - (bracket.x + bracket.width)
        verify(room >= field.growingRoom - 1,
               "the well must keep " + field.growingRoom
               + " to grow into, not " + room)
        verify(room <= field.growingRoom + 1,
               "...and no more than that: " + room + " is dead space")

        // The box is as wide as what it holds, so the whole slice is on
        // screen rather than scrolled inside a sliver...
        verify(box.width + 1 >= box.contentWidth,
               "the subscripts must be drawn in full")
        // ...and no wider, so the bracket closes the subscripts rather than
        // standing clear of them. The box used to be held open at a fixed
        // sixty pixels whatever it held, which left twenty-four pixels of
        // nothing before the `]` on this slice and thirty-five on
        // `/cube[:, :, :]`: trailing space inside the brackets, which is not
        // what the line says.
        const trailing = bracket.x - (box.x + box.contentWidth)
        verify(trailing <= Theme.gapXS + 1,
               "the bracket must follow the subscripts, not stand "
               + trailing + " clear of them")

        // The path is drawn in full too. It used to be capped at a fixed width
        // whether or not the bar had room, so a path of about thirty
        // characters was cut short on a window with three hundred spare pixels
        // beside it.
        verify(!pathLabel.truncated,
               "the path must not be elided while the bar has room for it")

        // A click on that room starts editing at the end of the line, and one
        // on the path starts at the beginning of it: on a long path the box is
        // a couple of characters across, and a well with a box in it is one
        // target.
        mouseClick(field, field.width - Theme.gapM, field.height / 2)
        verify(box.activeFocus, "the room after the bracket must start editing")
        compare(box.cursorPosition, box.length)

        keyClick(Qt.Key_Escape)
        mouseClick(field, Theme.gapM, field.height / 2)
        verify(box.activeFocus, "the path must start editing too")
        compare(box.cursorPosition, 0)
        keyClick(Qt.Key_Escape)
    }

    /// The same, on the two lines where a box sized from anything but its own
    /// text shows worst: the shortest slice in the fixture, and the longest
    /// path. Both are drawn whole, with the bracket against the subscripts and
    /// the slack after it.
    ///
    /// The path is the latch to watch here. The well used to take its width
    /// from the label, and a Text that elides reports the *elided* line as its
    /// implicit width -- so a hair too little room elides the path, the
    /// shorter path shrinks the well, and it settles with room for an ellipsis
    /// and nothing else. The width is measured off the font now, which cannot
    /// elide; this test is what says so.
    function test_every_part_of_the_line_is_drawn_whole() {
        const lines = ["/cube", "/vec_int", "/group/nested/leaf", "/hypercube"]
        for (const path of lines) {
            verify(select(path))
            const win = createTemporaryObject(windowComponent, testCase)
            waitForRendering(win.contentItem)
            win.selectTab("table")
            waitForRendering(win.contentItem)

            const box = findChild(win.contentItem, "sliceInput")
            const line = box.parent
            const field = line.parent
            const pathLabel = findChild(win.contentItem, "slicePath")
            const bracket = findChild(win.contentItem, "sliceCloseBracket")

            verify(!pathLabel.truncated,
                   path + ": the path must be drawn in full on a bar with "
                   + "room for it, not cut to " + pathLabel.width)
            verify(pathLabel.width + 1 >= pathLabel.contentWidth,
                   path + ": ...at its own width")
            verify(box.width + 1 >= box.contentWidth,
                   path + ": the subscripts must be drawn in full")
            verify(bracket.x - (box.x + box.contentWidth) <= Theme.gapXS + 1,
                   path + ": the bracket must follow the subscripts")

            const room = line.width - (bracket.x + bracket.width)
            verify(room >= field.growingRoom - 1 && room <= field.growingRoom + 1,
                   path + ": the slack after the bracket must be "
                   + field.growingRoom + ", not " + room)
            win.destroy()
        }
    }

    /// A long slice makes the well longer, rather than scrolling inside a well
    /// that has stopped growing while the bar still has room.
    function test_the_slice_well_grows_with_the_line_in_it() {
        verify(select("/hypercube")) // 2 x 3 x 4 x 5
        const win = createTemporaryObject(windowComponent, testCase)
        waitForRendering(win.contentItem)
        win.selectTab("table")
        waitForRendering(win.contentItem)

        const box = findChild(win.contentItem, "sliceInput")
        const field = box.parent.parent
        const narrow = field.width

        // A scattered selection on every dimension, which is about as long as
        // a rank-4 line gets.
        compare(AppController.applySlice("[0,1], [0,1,2], [0,1,2,3], [0,1,2,3,4]"), "")
        waitForRendering(win.contentItem)

        verify(field.width > narrow,
               "the well must widen for a longer slice: " + field.width
               + " vs " + narrow)
        verify(box.width + 1 >= box.contentWidth,
               "...far enough to hold the whole of it")
        // ...and it got everything it asked for, rather than being clamped by a
        // ceiling of the bar's choosing. That ceiling was half the picker's
        // width, and a line past it scrolled inside a well with three hundred
        // spare pixels beside it.
        //
        // Unless the bar had nothing to give, which is a different thing and
        // has to be told apart from it rather than assumed away. How much a
        // bar of a given width has spare is a question about how wide the host
        // draws text: this window opens at a width with pixels to spare on
        // Linux and thirty short of the well's ask under Windows' rasteriser,
        // where the well is right to take what it can get.
        //
        // The spacer is what the difference is read from. It is the item that
        // holds whatever the bar did not spend, so a spacer with nothing in it
        // is a bar with nothing left to have given the well -- and a spacer
        // with room in it beside a well below its ask is the ceiling this
        // assertion exists to catch, on any platform and at any width.
        const spacer = findChild(win.contentItem, "sliceSpacer")
        verify(spacer, "the bar must have its spacer")
        verify(Math.abs(field.width - field.implicitWidth) < 1 || spacer.width < 1,
               "the well must take the width it asks for unless the bar has "
               + "nothing spare: " + field.width + " vs " + field.implicitWidth
               + ", spare " + spacer.width)

        AppController.applySlice(":, :, :, :")
    }

    /// The bar is not always wide enough for the whole line, and the order in
    /// which the parts give way is the point of this one: the slack after the
    /// bracket goes first, then the path, and only then do the subscripts
    /// start scrolling inside their box. The brackets never go, because a line
    /// missing one of them is not a slice of anything.
    ///
    /// What must not give way is the bar. The well used to stand at its
    /// implicit width whatever the bar could afford -- a RowLayout does not
    /// resize an item that does not fill, so Layout.minimumWidth on it meant
    /// nothing -- and a slice of ninety characters walked the two settings
    /// buttons a hundred and twenty pixels past the right-hand end of the
    /// window.
    function test_a_bar_too_narrow_for_the_line_gives_the_path_away_first() {
        verify(select("/hypercube")) // 2 x 3 x 4 x 5
        const win = createTemporaryObject(windowComponent, testCase)
        win.width = win.minimumWidth
        waitForRendering(win.contentItem)
        win.selectTab("table")
        waitForRendering(win.contentItem)

        const box = findChild(win.contentItem, "sliceInput")
        const line = box.parent
        const field = line.parent
        const bar = field.parent
        const pathLabel = findChild(win.contentItem, "slicePath")
        const openBracket = findChild(win.contentItem, "sliceOpenBracket")
        const closeBracket = findChild(win.contentItem, "sliceCloseBracket")

        // Nothing in the bar reaches past its right-hand edge.
        function overflow() {
            let worst = 0
            for (let i = 0; i < bar.children.length; ++i) {
                const child = bar.children[i]
                if (child.visible) {
                    worst = Math.max(worst, child.x + child.width - bar.width)
                }
            }
            return worst
        }
        function slack() {
            return line.width - (closeBracket.x + closeBracket.width)
        }

        // A line the bar can hold: everything whole, the slack after the
        // bracket, nothing scrolling.
        compare(AppController.applySlice(":, :, :, :"), "")
        waitForRendering(win.contentItem)
        verify(overflow() <= 1, "the bar must hold its own contents")
        verify(!pathLabel.truncated, "a short line leaves the path whole")
        verify(box.width + 1 >= box.contentWidth, "...and the subscripts too")
        verify(Math.abs(slack() - field.growingRoom) <= 1,
               "the slack after the bracket must be " + field.growingRoom
               + ", not " + slack())
        const narrow = box.width

        // ...and one it cannot.
        compare(AppController.applySlice(
                    "[0,1,0,1,0,1,0,1,0,1,0,1], [0,1,2], [0,1,2,3], [0,1,2,3,4]"),
                "")
        waitForRendering(win.contentItem)

        verify(overflow() <= 1,
               "a long slice must not push the settings buttons "
               + overflow() + " past the end of the bar")
        verify(field.width < field.implicitWidth,
               "the well must give width back when the bar runs out")
        // The slack goes first...
        verify(slack() <= 1, "the room to grow into is spent before the "
               + "subscripts scroll, not kept while they do: " + slack())
        // ...then the path...
        verify(pathLabel.width < field.pathWanted,
               "the path must yield to the subscripts")
        // ...and the box has everything the two of them gave up.
        verify(box.width > narrow,
               "the subscripts must be wider for it: " + box.width
               + " vs " + narrow)
        // The brackets are not the path's to take with it. `[0,1]]` is not a
        // slice, and an opening bracket glued to the end of the path label is
        // how it would become one.
        verify(openBracket.visible && openBracket.width > 0,
               "the opening bracket must outlive the path")
        verify(closeBracket.visible && closeBracket.width > 0,
               "...and so must the closing one")

        // The same with the reason for a bad line beside it, which is the
        // other thing in this bar that would like more width than there is.
        box.forceActiveFocus()
        box.text = "9, 9, 9, [0,1,2,3,4,5,6,7,8,9], 9, 9, 9, 9"
        box.textEdited()
        waitForRendering(win.contentItem)
        verify(field.error !== "", "the line must not read")
        verify(overflow() <= 1,
               "nor must the reason for it: " + overflow() + " past the end")

        keyClick(Qt.Key_Escape)
        AppController.applySlice(":, :, :, :")
        win.destroy()
    }

    /// A line that does not read is left on screen with the reason beside it,
    /// and the table keeps the selection that did read.
    function test_a_slice_that_does_not_read_says_so_and_changes_nothing() {
        verify(select("/cube")) // 2 x 3 x 4
        const win = createTemporaryObject(windowComponent, testCase)
        waitForRendering(win.contentItem)
        win.selectTab("table")
        waitForRendering(win.contentItem)

        const box = findChild(win.contentItem, "sliceInput")
        const field = box.parent.parent
        compare(field.error, "")

        box.forceActiveFocus()
        box.text = "0, 9, :"
        box.textEdited()
        verify(field.error.indexOf("dim 1") === 0,
               "the message must name the dimension: " + field.error)
        verify(field.error.indexOf("past the end") !== -1)

        keyClick(Qt.Key_Return)
        waitForRendering(win.contentItem)
        compare(AppController.sliceExpression, "/cube[:, :, :]")
        compare(box.text, "0, 9, :", "what was written is not thrown away")
        verify(field.error !== "")

        // Escape puts back what the table is showing.
        keyClick(Qt.Key_Escape)
        compare(box.text, ":, :, :")
        compare(field.error, "")
    }

    /// A line that reads and has not been applied says so, in the bar and on
    /// the well itself. Until this the other half of the contract was silent:
    /// a slice typed and not committed left the bar describing one set of
    /// elements above a table drawn from another.
    function test_a_slice_typed_and_not_applied_says_so() {
        verify(select("/cube")) // 2 x 3 x 4
        const win = createTemporaryObject(windowComponent, testCase)
        waitForRendering(win.contentItem)
        win.selectTab("table")
        waitForRendering(win.contentItem)

        const box = findChild(win.contentItem, "sliceInput")
        const field = box.parent.parent
        const note = findChild(win.contentItem, "sliceNote")
        verify(note, "the bar must carry the note")
        verify(!field.pending, "the box opens on what the table is showing")
        verify(!note.visible)
        compare(field.color, Theme.surfaceInset)

        box.forceActiveFocus()
        box.text = "0, :, :"
        box.textEdited()
        waitForRendering(win.contentItem)

        compare(field.error, "", "the line reads")
        verify(field.pending, "...and is not the one the table is drawn from")
        verify(note.visible, "which the bar says")
        compare(note.color, Theme.accent, "an unfinished line is not a mistake")
        compare(field.color, Theme.surfacePending, "and the well says it too")
        compare(AppController.sliceExpression, "/cube[:, :, :]",
                "nothing has been applied")

        keyClick(Qt.Key_Return)
        waitForRendering(win.contentItem)

        compare(AppController.sliceExpression, "/cube[0, :, :]")
        verify(!field.pending, "Return applies it")
        verify(!note.visible)
        compare(field.color, Theme.surfaceInset)

        AppController.applySlice(":, :, :")
    }

    /// The legend opens over the left of the plot, so its button is at the
    /// left end of the bar.
    function test_the_legend_button_leads_the_bar() {
        verify(select("/cube"))
        const win = createTemporaryObject(windowComponent, testCase)
        waitForRendering(win.contentItem)
        win.selectTab("plot")
        waitForRendering(win.contentItem)

        const legend = findChild(win.contentItem, "legendButton")
        verify(legend, "the legend button must be reachable")
        verify(legend.visible)

        const box = findChild(win.contentItem, "sliceInput")
        const bar = legend.parent
        verify(legend.mapToItem(bar, 0, 0).x < box.mapToItem(bar, 0, 0).x,
               "the legend button must stand left of the slice")

        // It is only ever a question about a plot.
        win.selectTab("table")
        waitForRendering(win.contentItem)
        verify(!legend.visible)
    }

    /// Data settings answer the one question all three data views share, so
    /// the panel stays open across them -- and the view's own panel does not.
    function test_the_data_settings_panel_is_shared_by_the_three_views() {
        verify(select("/cube"))
        const win = createTemporaryObject(windowComponent, testCase)
        waitForRendering(win.contentItem)
        const view = findChild(win.contentItem, "dataView")
        verify(view, "the data views must be reachable")

        win.selectTab("table")
        view.toggleRail("data")
        compare(view.rail, "data")

        for (const id of ["plot", "image", "table"]) {
            win.selectTab(id)
            waitForRendering(win.contentItem)
            compare(view.rail, "data", "data settings must survive " + id)
        }

        // The information view has no rail of its own to keep, and coming
        // back finds the panel where it was left.
        win.selectTab("info")
        waitForRendering(win.contentItem)
        win.selectTab("table")
        waitForRendering(win.contentItem)
        compare(view.rail, "data")
    }

    function test_data_view_shows_numbers_as_a_grid() {
        verify(select("/matrix"))
        const view = createTemporaryObject(dataComponent, testCase, viewSize)
        verify(view, "DataView must instantiate")
        waitForRendering(view)
        compare(AppController.datasetMessage, "")
        compare(view.mode, "table")
    }

    function test_data_view_shows_one_string_as_a_text_pane() {
        // A scalar string is a document, not a cell: the Data Viewer has to
        // hand the whole thing over rather than elide it into a grid.
        verify(select("/str_scalar"))
        verify(AppController.datasetIsString)
        compare(AppController.datasetElementCount, 1)

        const view = createTemporaryObject(dataComponent, testCase, viewSize)
        waitForRendering(view)
        compare(view.mode, "text")

        const strings = AppController.datasetStringModel
        compare(strings.rowCount(), 1)
        const text = strings.data(strings.index(0, 0))
        verify(text.length > 400, "the whole string must survive the trip")
        verify(text.indexOf("Paragraph 11") !== -1, "including its end")
    }

    function test_data_view_stacks_many_strings_under_a_grid() {
        verify(select("/str_vlen"))
        verify(AppController.datasetIsString)
        compare(AppController.datasetElementCount, 3)

        const view = createTemporaryObject(dataComponent, testCase, viewSize)
        waitForRendering(view)
        compare(view.mode, "strings")

        const strings = AppController.datasetStringModel
        compare(strings.rowCount(), 3)
        // The list is a reading of the table, and the table's cells arrive from
        // the file a moment after they are asked for.
        tryVerify(() => strings.data(strings.index(2, 0)) === "five five five",
                  10000, "the strings must arrive")
    }

    function test_data_view_opens_a_compound_out_under_the_grid() {
        // A struct in a grid cell is the whole struct elided, which for a
        // struct is the same as nothing. The tab has to hand over the picked
        // one whole -- named members, and the same element as JSON.
        verify(select("/compound"))
        verify(AppController.datasetIsCompound)
        verify(!AppController.datasetIsNumeric)

        const view = createTemporaryObject(dataComponent, testCase, viewSize)
        waitForRendering(view)
        compare(view.mode, "compound")

        const surface = findChild(view, "tableSurface")
        verify(surface, "the table presentation must be reachable")

        // Nothing has been clicked, so the first cell stands in and the pane
        // says which element it is showing.
        const first = surface.element
        compare(first.label, "0")
        compare(first.fields.length, 2)
        compare(first.fields[0].name, "id")
        compare(first.fields[0].value, "7")
        compare(first.json, '{\n  "id": 7,\n  "value": 1.5\n}')

        // The plot and the image are unavailable for it, as for any dataset
        // whose cells hold no number.
        compare(view.viewMode, "table")
    }

    function test_string_elements_are_labelled_by_subscript() {
        // Rank 2, so a single index would be ambiguous.
        verify(select("/str_grid"))
        const strings = AppController.datasetStringModel
        compare(strings.rowCount(), 4)
        compare(strings.indexOfCell(1, 0), 2)
        compare(strings.indexOfCell(9, 9), -1)
    }

    function test_table_setup_panel_instantiates_against_the_real_model() {
        verify(select("/hypercube"))
        compare(AppController.tableSetupModel.rowCount(), 4)

        const panel = createTemporaryObject(setupComponent, testCase,
                                            { width: 248, height: 600 })
        verify(panel, "TableSetupPanel must instantiate")
        waitForRendering(panel)
    }

    function test_the_data_settings_sidebar_opens_for_anything_with_a_dimension() {
        verify(select("/cube"))
        const view = createTemporaryObject(dataComponent, testCase, viewSize)
        waitForRendering(view)
        verify(!view.setupVisible, "it starts closed")

        view.rail = "data"
        waitForRendering(view)
        verify(view.setupVisible)

        // A scalar is one cell; there is nothing to set up about it, and the
        // sidebar stays away whatever the reader asked for.
        verify(select("/scalar_int"))
        waitForRendering(view)
        verify(!view.setupVisible)
    }

    function test_the_rail_shows_one_panel_at_a_time() {
        verify(select("/matrix"))
        const view = createTemporaryObject(dataComponent, testCase, viewSize)
        waitForRendering(view)
        compare(view.rail, "")

        view.toggleRail("data")
        compare(view.rail, "data")
        // Opening the table's own settings replaces it rather than joining it:
        // two panels would take a third of the window between them.
        view.toggleRail("table")
        compare(view.rail, "table")
        // ...and the same button closes it again.
        view.toggleRail("table")
        compare(view.rail, "")
    }

    function test_a_view_s_own_settings_close_when_the_view_changes() {
        verify(select("/matrix"))
        const view = createTemporaryObject(dataComponent, testCase, viewSize)
        waitForRendering(view)

        view.toggleRail("table")
        view.show("plot")
        compare(view.viewMode, "plot")
        compare(view.rail, "", "table settings no longer apply to anything")

        // Data settings do apply after a mode change -- all three views draw
        // whatever that panel resolves to -- so they stay.
        view.toggleRail("data")
        view.show("image")
        compare(view.viewMode, "image")
        compare(view.rail, "data")
    }

    function test_plot_and_image_are_for_numbers_only() {
        verify(select("/matrix"))
        verify(AppController.datasetIsNumeric)

        const view = createTemporaryObject(dataComponent, testCase, viewSize)
        waitForRendering(view)
        view.show("plot")
        compare(view.viewMode, "plot")

        // Text can be read but not plotted, and the view must not stay on a
        // mode that cannot show what was just selected.
        verify(select("/str_vlen"))
        verify(!AppController.datasetIsNumeric)
        waitForRendering(view)
        compare(view.viewMode, "table")
    }

    function test_the_plot_draws_one_line_per_row() {
        verify(select("/cube")) // 2x3x4 -> 6 rows, 4 columns
        const plot = AppController.datasetPlot
        verify(plot.seriesFromRows)
        compare(plot.seriesCount, 6)
        compare(plot.pointCount, 4)
        verify(plot.hasData)

        const view = createTemporaryObject(dataComponent, testCase, viewSize)
        view.show("plot")
        waitForRendering(view)
    }

    function test_a_vector_plots_as_one_line_not_a_thousand() {
        // defaultOnX keeps a rank-1 dimension on the row axis, so /long_vec is
        // a 1000x1 table. "One line per row" there would be a thousand lines
        // of one point each, which is a plot of nothing.
        verify(select("/long_vec"))
        const plot = AppController.datasetPlot
        verify(!plot.seriesFromRows, "it must transpose itself for a vector")
        compare(plot.seriesCount, 1)
        compare(plot.pointCount, 1000)

        // ...and the reader can still put it back.
        plot.seriesFromRows = true
        compare(plot.pointCount, 1)
        plot.seriesFromRows = false
    }

    function test_the_image_follows_the_same_slice_as_the_grid() {
        verify(select("/hypercube")) // 2x3x4x5
        const image = AppController.datasetImage
        const table = AppController.datasetModel
        compare(image.width, table.columnCount())
        compare(image.height, table.rowCount())
        verify(image.hasData)

        // Narrowing a dimension in the data settings narrows the raster with
        // it, because the raster is that table and not a second reading of the
        // file.
        const before = image.revision
        const setup = AppController.tableSetupModel
        setup.setMode(3, TableSetupModel.Index)
        verify(image.revision !== before, "a rearranged table is a new raster")
        compare(image.width, table.columnCount())
        compare(image.height, table.rowCount())
        setup.setMode(3, TableSetupModel.All)

        const view = createTemporaryObject(dataComponent, testCase, viewSize)
        view.show("image")
        waitForRendering(view)
    }

    function test_each_view_carries_its_own_footer() {
        verify(select("/matrix")) // 4x3
        const view = createTemporaryObject(dataComponent, testCase, viewSize)
        waitForRendering(view)

        // The tab's own footer is gone; what is on screen is reported by
        // whichever view is drawing it.
        compare(view.viewMode, "table")
        compare(AppController.datasetModel.rowCount(), 4)
        compare(AppController.datasetModel.columnCount(), 3)

        view.show("plot")
        waitForRendering(view)
        compare(AppController.datasetPlot.seriesCount, 4)
        compare(AppController.datasetPlot.pointCount, 3)

        view.show("image")
        waitForRendering(view)
        compare(AppController.datasetImage.width, 3)
        compare(AppController.datasetImage.height, 4)
    }

    function test_the_plot_and_the_image_reach_the_screen() {
        // The one thing every other test here takes on trust: that the pixels
        // arrive. A plot library that cannot render under this application --
        // Qt Charts asserts without a QApplication, and a provider that is
        // never installed yields a broken image -- passes every assertion
        // about counts and still shows an empty frame.
        verify(select("/compressed")) // 100x100, a real picture

        const win = createTemporaryObject(viewWindowComponent, testCase)
        verify(win, "the window must instantiate")
        waitForRendering(win.view)

        win.view.show("image")
        waitForRendering(win.view)
        const imageSurface = findChild(win.view, "imageSurface")
        verify(imageSurface, "the image surface must be reachable")
        const raster = grabImage(imageSurface)
        verify(raster.width > 0 && raster.height > 0)
        // A ramp from black to white, so the top-left of the raster and its
        // bottom-right cannot be the same shade. Sampling the middle of the
        // drawn area rather than its corners, which are margin.
        const topLeft = raster.pixel(Math.round(raster.width * 0.3),
                                     Math.round(raster.height * 0.2))
        const bottomRight = raster.pixel(Math.round(raster.width * 0.7),
                                         Math.round(raster.height * 0.8))
        verify(topLeft !== bottomRight,
               "the image view must draw the data, not a flat panel")

        win.view.show("plot")
        waitForRendering(win.view)
        const plotSurface = findChild(win.view, "plotSurface")
        verify(plotSurface, "the plot surface must be reachable")
        const plotted = grabImage(plotSurface)
        // Grabbed from the surface itself, so the surrounding chrome cannot
        // stand in for a plot: every pixel here is either the inset the graph
        // sits on or something the graph drew on it.
        let drawn = 0
        for (let x = 0; x < plotted.width; x += 5) {
            for (let y = 0; y < plotted.height; y += 5) {
                if (plotted.pixel(x, y) !== Theme.surfaceInset)
                    ++drawn
            }
        }
        verify(drawn > 20, "the plot must draw its lines and axes, not a blank inset")

        // And the rank-1 case, which is the one the transpose exists for: a
        // vector must arrive as one line across the view, not as a thousand
        // one-point lines -- which would draw almost nothing at all.
        verify(select("/long_vec"))
        waitForRendering(win.view)
        waitForRendering(win.view)
        compare(AppController.datasetPlot.seriesCount, 1)
        compare(AppController.datasetPlot.pointCount, 1000)
        const line = grabImage(plotSurface)
        let vector = 0
        for (let vx = 0; vx < line.width; vx += 5) {
            for (let vy = 0; vy < line.height; vy += 5) {
                if (line.pixel(vx, vy) !== Theme.surfaceInset)
                    ++vector
            }
        }
        verify(vector > 20, "a vector's one line must be drawn")

        // ...and nothing of the previous selection is drawn under it. This
        // corner of the plot area belongs to neither the vector -- which runs
        // corner to corner -- nor to anything the previous selection should
        // have left behind.
        //
        // Counted in ink rather than in pixels-unlike-the-ground, which is a
        // change this test needed when the plot stopped drawing through Qt
        // Graphs. The grid is neutral and the lines are not, and the grid now
        // reaches this corner: Qt Graphs drew its own grid only through the
        // graphics API and the software renderer that headless runs get
        // dropped it, so for as long as this suite has existed the corner was
        // bare. It is not bare now, and a horizontal rule across it is 420
        // pixels of perfectly correct drawing.
        //
        // What the check is actually about survives that intact. The failure
        // it was written for was Qt Graphs keeping what a series last drew, in
        // the pixel coordinates of the axes it was drawn against, so that a
        // graph reused across selections showed both at once -- and a stale
        // stroke is a stroke, drawn in whichever colour the cycle gave it.
        // Ink in this corner means a line in it.
        let ghost = 0
        for (let gx = Math.round(line.width * 0.55); gx < line.width - 30; ++gx) {
            for (let gy = Math.round(line.height * 0.80); gy < line.height - 40; ++gy) {
                const pixel = line.pixel(gx, gy)
                if (Math.max(pixel.r, pixel.g, pixel.b)
                    - Math.min(pixel.r, pixel.g, pixel.b) > 0.06)
                    ++ghost
            }
        }
        verify(ghost < 50,
               "the previous selection must not still be drawn: " + ghost + " px")
    }

    /// The grid is a control that draws something.
    ///
    /// It was not, for as long as the plot drew through Qt Graphs -- not
    /// because the library refused, but because the library drew it only
    /// through the graphics API and every headless run falls back to the
    /// software renderer, which dropped it without a word. A note in
    /// PlotSurface.qml said for a long time that Qt Graphs drew no grid at
    /// all; docs/screenshots/plot.png, which is taken with the "rhi" backend
    /// asked for by name, always had one in it.
    ///
    /// Now the rules are this application's own Rectangles and they draw
    /// wherever anything draws, which is what makes this assertable here at
    /// last.
    function test_the_grid_draws_when_it_is_asked_to() {
        verify(select("/compressed"))

        const win = createTemporaryObject(viewWindowComponent, testCase)
        waitForRendering(win.view)
        win.view.show("plot")
        waitForRendering(win.view)
        const plot = findChild(win.view, "plotSurface")
        verify(plot, "the plot surface must be reachable")

        // A band above the lines' own extent would be ideal and there is no
        // such band -- the y axis is the extent of the values. So count the
        // rules instead: a horizontal grid line is a row that is almost
        // entirely not the ground, and a row a line crosses is a handful of
        // pixels.
        const rulesIn = (shot) => {
            let found = 0
            for (let y = 0; y < shot.height; ++y) {
                let across = 0
                for (let x = Math.round(shot.width * 0.55);
                     x < shot.width - 40; x += 2) {
                    if (shot.pixel(x, y) !== Theme.surfaceInset)
                        ++across
                }
                if (across > (shot.width * 0.45 - 40) / 4)
                    ++found
            }
            return found
        }

        plot.gridMode = "loose"
        waitForRendering(win.view)
        const withGrid = rulesIn(grabImage(plot))
        verify(withGrid >= 3,
               "the grid must draw its rules: only " + withGrid + " found")

        plot.gridMode = "none"
        waitForRendering(win.view)
        const without = rulesIn(grabImage(plot))
        verify(without < withGrid,
               "turning the grid off must take rules away: " + without
               + " of " + withGrid + " left")

        // ...and the two densities above "off" are ordered. Dense rules
        // between the numbered ticks rather than at more of them, so what this
        // counts is a strictly larger set than `loose` drew -- the same rules
        // plus the minors between them.
        plot.gridMode = "dense"
        waitForRendering(win.view)
        const dense = rulesIn(grabImage(plot))
        verify(dense > withGrid,
               "dense must rule more finely than loose: " + dense + " against "
               + withGrid)

        // Custom rules where it is told to and nowhere else. A step of a
        // quarter of the y span is three rules inside the pane, whatever the
        // data is -- and a step of nothing is not a finer grid, it is no grid,
        // which is what the box holds while a number is half typed.
        plot.gridMode = "custom"
        plot.gridStepX = 0
        plot.gridStepY = (plot.viewMaxY - plot.viewMinY) / 4
        waitForRendering(win.view)
        const custom = rulesIn(grabImage(plot))
        verify(custom >= 2 && custom < dense,
               "a custom step must rule where it says: " + custom)

        plot.gridStepY = 0
        waitForRendering(win.view)
        compare(rulesIn(grabImage(plot)), without,
                "a step of zero must rule nothing at all")

        plot.gridMode = "loose"
        plot.gridStepX = 0
        plot.gridStepY = 0
    }

    /// A right-drag says which part of the plot to look at, and the view goes
    /// there.
    ///
    /// The gesture every plot in every field has and this one did not: the
    /// wheel zooms about a point, so a reader who could *see* the region they
    /// wanted had to arrive at it by turning the wheel and correcting with a
    /// drag, several times, by eye.
    ///
    /// Asserted in the window rather than in the zoom, because the zoom is the
    /// arithmetic and the window is the promise: the band the reader drew is
    /// where the axes end up.
    function test_a_right_drag_puts_the_view_on_the_region_it_draws() {
        verify(select("/compressed"))

        const win = createTemporaryObject(viewWindowComponent, testCase)
        waitForRendering(win.view)
        win.view.show("plot")
        waitForRendering(win.view)

        const plot = findChild(win.view, "plotSurface")
        verify(plot, "the plot surface must be reachable")
        const gestures = findChild(win.view, "plotGestures")
        verify(gestures, "the gesture layer must be reachable")

        const area = plot.plotRect
        verify(area.width > 0 && area.height > 0)

        // A quarter of the pane, in the middle of it. Worked out before the
        // drag, because the axes are about to move under them.
        const spanX = plot.viewMaxX - plot.viewMinX
        const spanY = plot.viewMaxY - plot.viewMinY
        const fromX = area.x + area.width * 0.25
        const toX = area.x + area.width * 0.75
        const fromY = area.y + area.height * 0.25
        const toY = area.y + area.height * 0.75
        const wantMinX = plot.viewMinX + spanX * 0.25
        const wantMaxX = plot.viewMinX + spanX * 0.75
        // y grows downward on screen and upward on the axis, so the band's top
        // edge is the larger value.
        const wantMinY = plot.viewMinY + spanY * 0.25
        const wantMaxY = plot.viewMinY + spanY * 0.75

        mouseDrag(gestures, Math.round(fromX), Math.round(fromY),
                  Math.round(toX - fromX), Math.round(toY - fromY),
                  Qt.RightButton)
        waitForRendering(win.view)

        // Within a pixel's worth of the axis, because the drag is delivered in
        // whole pixels and the band is read back out of them.
        const slackX = spanX / area.width * 2
        const slackY = spanY / area.height * 2
        verify(Math.abs(plot.viewMinX - wantMinX) < slackX,
               "x must start at the band: " + plot.viewMinX + " for " + wantMinX)
        verify(Math.abs(plot.viewMaxX - wantMaxX) < slackX,
               "x must stop at the band: " + plot.viewMaxX + " for " + wantMaxX)
        verify(Math.abs(plot.viewMinY - wantMinY) < slackY,
               "y must start at the band: " + plot.viewMinY + " for " + wantMinY)
        verify(Math.abs(plot.viewMaxY - wantMaxY) < slackY,
               "y must stop at the band: " + plot.viewMaxY + " for " + wantMaxY)

        // ...and a slip is not a region. A band of a few pixels is a
        // magnification of several hundred on an axis nobody meant to touch,
        // and the only way out of it would be a reset -- so it does nothing at
        // all rather than something drastic.
        plot.resetView()
        waitForRendering(win.view)
        const settled = plot.viewMinX
        mouseDrag(gestures, Math.round(area.x + area.width / 2),
                  Math.round(area.y + area.height / 2), 2, 2, Qt.RightButton)
        waitForRendering(win.view)
        compare(plot.viewMinX, settled, "a slip must move nothing")
        verify(!plot.zoomed, "a slip must not zoom")

        plot.resetView()
    }

    /// Nothing a band writes may be drawn off the pane or over another number,
    /// and no length may be drawn over the band.
    ///
    /// Here rather than in each test below because it is the one rule the
    /// whole layout exists to keep -- and because a rule asserted in one place
    /// is a rule a new label cannot be added past. The coordinates are held to
    /// it too, everywhere there is room for them: what they are allowed is a
    /// band drawn into the corner of the pane, which leaves the corner nowhere
    /// outside itself to stand.
    function verifyBandNumbersAreClear(readout, clearOfBand) {
        const boxes = readout.labels
        for (let i = 0; i < boxes.length; ++i) {
            verify(readout.inside(boxes[i]),
                   boxes[i].key + " must be drawn inside the pane")
            if (clearOfBand || boxes[i].key !== "start" && boxes[i].key !== "end") {
                verify(readout.clears(boxes[i], readout.bandRect),
                       boxes[i].key + " must stand clear of the band")
            }
            for (let j = i + 1; j < boxes.length; ++j) {
                verify(readout.clears(boxes[i], boxes[j]),
                       boxes[i].key + " must stand clear of " + boxes[j].key)
            }
        }
    }

    /// A band being drawn says what it is, in the axes' own numbers.
    ///
    /// The rectangle on its own says where the reader is about to look and
    /// nothing about what they are about to see, which is the half a reader
    /// selecting an interval actually wants: the corner they started at, the
    /// corner they have reached, and how wide and how tall it has become.
    ///
    /// Asserted against the surface's own pixel-to-data mapping -- the one
    /// zoomToRegion resolves the band with -- because that identity is the
    /// point of the numbers. A readout worked out a second way would be a
    /// promise the zoom that follows it does not have to keep.
    function test_a_region_drag_writes_its_corners_and_its_lengths() {
        verify(select("/compressed"))

        const win = createTemporaryObject(viewWindowComponent, testCase)
        waitForRendering(win.view)
        win.view.show("plot")
        waitForRendering(win.view)

        const plot = findChild(win.view, "plotSurface")
        verify(plot, "the plot surface must be reachable")
        const gestures = findChild(win.view, "plotGestures")
        verify(gestures, "the gesture layer must be reachable")
        const lines = findChild(win.view, "plotLines")
        verify(lines, "the lines must be reachable")
        const readout = findChild(win.view, "plotBand")
        verify(readout, "the band must be reachable")

        verify(!plot.selecting, "nothing is being selected yet")
        compare(readout.labels.length, 0, "so nothing is written")

        // The crosshair is reading, which is what the band is about to take
        // away: two readouts of two different things over one pane is a
        // picture the reader has to take apart before they can read either.
        mouseMove(lines, Math.round(lines.width * 0.4),
                  Math.round(lines.height * 0.5))
        waitForRendering(win.view)
        verify(plot.reading.valid, "the crosshair must be reading to begin with")

        const area = plot.plotRect
        verify(area.width > 0 && area.height > 0)
        const fromX = Math.round(area.x + area.width * 0.3)
        const fromY = Math.round(area.y + area.height * 0.3)
        const toX = Math.round(area.x + area.width * 0.7)
        const toY = Math.round(area.y + area.height * 0.7)

        mousePress(gestures, fromX, fromY, Qt.RightButton)
        mouseMove(gestures, toX, toY, -1, Qt.RightButton)
        tryVerify(() => plot.selecting, 2000, "the band must be being drawn")
        waitForRendering(win.view)

        verify(!plot.reading.valid, "the crosshair must be off under a band")
        compare(plot.readingFacts.length, 0, "and the footer must stop reading")

        const labels = {}
        for (let i = 0; i < readout.labels.length; ++i)
            labels[readout.labels[i].key] = readout.labels[i]

        // Both corners, as the axes read them.
        verify(labels.start, "the corner the drag started at must be written")
        verify(labels.end, "the corner it has reached must be written")
        compare(labels.start.text,
                plot.readingNumber(plot.dataXAt(fromX)) + ", "
                + plot.readingNumber(plot.dataYAt(fromY)))
        compare(labels.end.text,
                plot.readingNumber(plot.dataXAt(toX)) + ", "
                + plot.readingNumber(plot.dataYAt(toY)))

        // ...and the lengths on both x lines and both y lines, which two
        // fifths of the pane in each direction has room for.
        verify(labels.widthAbove && labels.widthBelow,
               "a band this wide must have its width on both x lines")
        verify(labels.heightLeft && labels.heightRight,
               "a band this tall must have its height on both y lines")
        compare(labels.widthAbove.text, labels.widthBelow.text)
        compare(labels.heightLeft.text, labels.heightRight.text)
        compare(labels.widthAbove.text,
                "∆" + plot.readingNumber(
                    Math.abs(plot.dataXAt(toX) - plot.dataXAt(fromX))))
        compare(labels.heightLeft.text,
                "∆" + plot.readingNumber(
                    Math.abs(plot.dataYAt(toY) - plot.dataYAt(fromY))))

        verifyBandNumbersAreClear(readout, true)

        // The band goes with the button, and so do its numbers -- and the
        // crosshair comes back.
        mouseRelease(gestures, toX, toY, Qt.RightButton)
        waitForRendering(win.view)
        verify(!plot.selecting, "the band must end with the gesture")
        compare(readout.labels.length, 0, "and take its numbers with it")

        mouseMove(lines, Math.round(lines.width * 0.4),
                  Math.round(lines.height * 0.5))
        waitForRendering(win.view)
        verify(plot.reading.valid, "the crosshair must come back")

        mouseMove(lines, -20, -20)
        plot.resetView()
    }

    /// A length is written where there is room for one, and left out where
    /// there is not.
    ///
    /// The corners are the reading and the lengths are the subtraction between
    /// them, so the lengths are what a crowded band can afford to lose. Two
    /// ways it gets crowded: a band narrower than the number that would
    /// measure it, and a band dragged off the edge of the pane -- where the
    /// numbers have to come back inside without landing on the band or on each
    /// other.
    function test_a_length_is_written_only_where_there_is_room_for_one() {
        verify(select("/compressed"))

        const win = createTemporaryObject(viewWindowComponent, testCase)
        waitForRendering(win.view)
        win.view.show("plot")
        waitForRendering(win.view)

        const plot = findChild(win.view, "plotSurface")
        const gestures = findChild(win.view, "plotGestures")
        const readout = findChild(win.view, "plotBand")
        verify(plot && gestures && readout, "the plot and its band must be reachable")

        const area = plot.plotRect
        const midX = Math.round(area.x + area.width * 0.5)

        // A few pixels wide and most of the pane tall. Its width does not fit
        // between the two corners that measure it, so it is not written at
        // all; its height has the whole side of the pane to stand in.
        mousePress(gestures, midX, Math.round(area.y + area.height * 0.2),
                   Qt.RightButton)
        mouseMove(gestures, midX + 4, Math.round(area.y + area.height * 0.8),
                  -1, Qt.RightButton)
        tryVerify(() => plot.selecting, 2000, "the band must be being drawn")
        waitForRendering(win.view)

        let labels = {}
        for (let i = 0; i < readout.labels.length; ++i)
            labels[readout.labels[i].key] = readout.labels[i]
        verify(labels.start && labels.end, "both corners are always written")
        verify(!labels.widthAbove && !labels.widthBelow,
               "a band narrower than its own width must not be given one")
        verify(labels.heightLeft && labels.heightRight,
               "and its height must still be on both y lines")
        verifyBandNumbersAreClear(readout, true)

        mouseRelease(gestures, midX + 4, Math.round(area.y + area.height * 0.8),
                     Qt.RightButton)
        waitForRendering(win.view)
        plot.resetView()
        waitForRendering(win.view)

        // ...and a drag that runs off the pane. The band stops at the edge --
        // which is where the zoom stops too -- so the numbers stop there with
        // it rather than being drawn over the axis labels or clipped in half.
        const startX = Math.round(area.x + area.width * 0.4)
        const startY = Math.round(area.y + area.height * 0.4)
        const offY = Math.round(area.y + area.height * 0.9)
        mousePress(gestures, startX, startY, Qt.RightButton)
        // Left of the pane but inside the window, which is the drag a reader
        // makes when the interval they want runs to the edge of the picture.
        mouseMove(gestures, 0, offY, -1, Qt.RightButton)
        tryVerify(() => plot.selecting, 2000, "the band must be being drawn")
        waitForRendering(win.view)

        labels = {}
        for (let i = 0; i < readout.labels.length; ++i)
            labels[readout.labels[i].key] = readout.labels[i]
        verify(labels.start && labels.end,
               "both corners are written even off the edge")
        compare(labels.end.text,
                plot.readingNumber(plot.viewMinX) + ", "
                + plot.readingNumber(plot.dataYAt(offY)),
                "the corner off the pane reads as the edge it stopped at")
        verifyBandNumbersAreClear(readout, true)

        mouseRelease(gestures, 0, offY, Qt.RightButton)
        waitForRendering(win.view)
        plot.resetView()
    }

    /// The four boxes under View are the window, both ways round.
    ///
    /// They are a readout as well as a control, and that is the half worth
    /// asserting: a region selected with the right button reports here without
    /// a second path between the two, so there is nothing that can go stale.
    function test_the_plotting_range_boxes_say_where_the_view_is_and_put_it_there() {
        verify(select("/compressed"))
        const view = createTemporaryObject(dataComponent, testCase, viewSize)
        waitForRendering(view)
        view.show("plot")
        waitForRendering(view)

        const plot = findChild(view, "plotSurface")
        verify(plot, "the plot surface must be reachable")

        const panel = createTemporaryObject(plotSettingsComponent, testCase,
                                            { width: Theme.railWidth, height: 800,
                                              target: plot })
        verify(panel, "the plot settings panel must instantiate")
        const minX = findChild(panel, "rangeViewMinX")
        const maxX = findChild(panel, "rangeViewMaxX")
        const minY = findChild(panel, "rangeViewMinY")
        const maxY = findChild(panel, "rangeViewMaxY")
        verify(minX && maxX && minY && maxY, "the four boxes must be reachable")

        compare(minX.value, plot.viewMinX)
        compare(maxX.value, plot.viewMaxX)
        compare(minY.value, plot.viewMinY)
        compare(maxY.value, plot.viewMaxY)

        // Stated, and taken. A quarter of the x axis, which is a zoom of four.
        const spanX = plot.axisMaxX - plot.axisMinX
        const wantMin = plot.axisMinX + spanX * 0.25
        const wantMax = plot.axisMinX + spanX * 0.5
        plot.setViewRange(wantMin, wantMax, plot.viewMinY, plot.viewMaxY)
        waitForRendering(view)
        fuzzyCompare(plot.viewMinX, wantMin, spanX * 1e-6)
        fuzzyCompare(plot.viewMaxX, wantMax, spanX * 1e-6)
        fuzzyCompare(plot.zoomX, 4, 1e-6)

        // ...and the boxes followed it without being told.
        compare(minX.value, plot.viewMinX)
        compare(maxX.value, plot.viewMaxX)

        // There is nothing outside the data to look at, so a window past its
        // ends comes back as the nearest one that exists -- and the boxes then
        // show what is in force rather than what was typed, because they are
        // bound rather than held.
        plot.setViewRange(plot.axisMinX - spanX, plot.axisMaxX + spanX,
                          plot.lowerBound, plot.upperBound)
        waitForRendering(view)
        compare(plot.zoomX, 1)
        compare(plot.panX, 0)
        compare(minX.value, plot.viewMinX)
        compare(maxX.value, plot.viewMaxX)

        plot.resetView()
    }

    /// The legend drawn on the plot names the lines that are drawn, and sits
    /// in the corner it is told to.
    ///
    /// It exists for the copy: a plot leaves this application through the
    /// clipboard, and it takes its children with it and nothing beside it, so
    /// six unnamed traces in a document are six traces nobody can read. Which
    /// is why this asserts the *names* and not merely that a box appeared.
    function test_the_legend_on_the_plot_names_the_lines_it_draws() {
        verify(select("/matrix"))
        // A window of its own, because this asserts that the legend is drawn:
        // an item parented into the TestCase is never effectively visible, so
        // everything under one reports invisible whatever its binding says.
        const win = createTemporaryObject(viewWindowComponent, testCase)
        const view = win.view
        waitForRendering(view)
        view.show("plot")
        waitForRendering(view)

        const plot = findChild(view, "plotSurface")
        verify(plot, "the plot surface must be reachable")
        const overlay = findChild(view, "plotOverlayLegend")
        verify(overlay, "the on-plot legend must be reachable")

        // Off until it is asked for: on screen the panel on the left already
        // answers this, and this one stands over part of the drawing.
        compare(plot.legendOnPlot, false)
        verify(!overlay.visible)

        // ...and while it is off it asks the plot *nothing*. Not a nicety: a
        // question put to a plot object is a reading of the file, and one of
        // these sits on every plot in the window whether or not it is the one
        // on screen. The first version of this file reached for `drawnSeries`
        // whatever the setting said, and the custom tab's picture came out an
        // empty pane with a correct axis under it -- the reads it disturbed
        // landed after the frame that was waiting for them.
        //
        // Asserted on the drawn set rather than on the visible flag, because
        // the flag would be false either way and the question would still have
        // been asked.
        verify(AppController.datasetPlot.drawnSeries.length > 0,
               "the fixture must have lines for this to be worth asking about")
        compare(overlay.drawn.length, 0)
        compare(overlay.rows.length, 0)

        plot.legendOnPlot = true
        waitForRendering(view)
        verify(overlay.visible, "asking for it must draw it")

        const drawn = AppController.datasetPlot.drawnSeries
        verify(drawn.length > 0, "the fixture must draw something")
        compare(overlay.rows.length, Math.min(drawn.length, Theme.plotLegendRows))
        for (let i = 0; i < overlay.rows.length; ++i) {
            compare(overlay.rows[i].series, drawn[i])
            compare(overlay.rows[i].label,
                    AppController.datasetPlot.seriesLabel(drawn[i]))
        }

        // Inside the pane, in the corner asked for. Four corners and four
        // different answers, which is what says the property is read rather
        // than the default drawn four times.
        const area = plot.plotRect
        plot.legendCorner = "topLeft"
        waitForRendering(view)
        const left = overlay.x
        const top = overlay.y
        verify(left >= area.x && top >= area.y, "top left must be inside the pane")

        plot.legendCorner = "bottomRight"
        waitForRendering(view)
        verify(overlay.x > left, "the right-hand corners must sit further right")
        verify(overlay.y > top, "the bottom corners must sit further down")
        verify(overlay.x + overlay.width <= area.x + area.width + 1,
               "it must stay inside the pane")
        verify(overlay.y + overlay.height <= area.y + area.height + 1,
               "it must stay inside the pane")

        plot.legendOnPlot = false
    }

    /// A title and two axis names cost nothing until they say something.
    ///
    /// The whole of "no wasted space" is that they are measured into the
    /// gutters rather than reserved: an untitled plot's pane is where it
    /// always was, to the pixel, and each name filled in takes room from
    /// exactly the edge it is drawn against and from no other.
    function test_a_title_costs_nothing_until_there_is_one() {
        verify(select("/compressed"))
        const view = createTemporaryObject(dataComponent, testCase, viewSize)
        waitForRendering(view)
        view.show("plot")
        waitForRendering(view)

        const plot = findChild(view, "plotSurface")
        verify(plot, "the plot surface must be reachable")

        compare(plot.plotTitle, "")
        compare(plot.xLabel, "")
        compare(plot.yLabel, "")

        // The four numbers, as numbers. A `rect` read into a var is a
        // reference to the property rather than a copy of it -- QML value
        // types track what they came from -- so a "before" held that way is
        // the "after" by the time it is compared against, and every assertion
        // below would pass without the feature existing.
        const paneOf = (item) => ({ x: item.plotRect.x, y: item.plotRect.y,
                                    width: item.plotRect.width,
                                    height: item.plotRect.height })
        const bare = paneOf(plot)
        verify(bare.width > 0 && bare.height > 0)

        // A title takes room off the top and off nothing else.
        plot.plotTitle = "pressure over the run"
        waitForRendering(view)
        const titled = paneOf(plot)
        verify(titled.y > bare.y, "a title must take room off the top: "
               + titled.y + " for " + bare.y)
        compare(titled.x, bare.x)
        compare(titled.width, bare.width)
        verify(titled.height < bare.height)

        // An x name off the bottom, which is height and not the origin.
        plot.plotTitle = ""
        plot.xLabel = "seconds"
        waitForRendering(view)
        const named = paneOf(plot)
        compare(named.y, bare.y)
        compare(named.x, bare.x)
        verify(named.height < bare.height, "an x label must take room off the bottom")

        // ...and a y name off the left, which is the origin and the width,
        // because it is drawn turned a quarter.
        plot.xLabel = ""
        plot.yLabel = "bar"
        waitForRendering(view)
        const sideways = paneOf(plot)
        compare(sideways.y, bare.y)
        compare(sideways.height, bare.height)
        verify(sideways.x > bare.x, "a y label must take room off the left")
        verify(sideways.width < bare.width)

        // Emptied again, the pane is exactly where it started.
        plot.yLabel = ""
        waitForRendering(view)
        const back = paneOf(plot)
        compare(back.x, bare.x)
        compare(back.y, bare.y)
        compare(back.width, bare.width)
        compare(back.height, bare.height)
    }

    /// The panel's three boxes reach the picture, and the picture reaches the
    /// boxes back.
    ///
    /// The wiring rather than the drawing -- what a title looks like is
    /// asserted next door, in the pane it takes room from. What is asserted
    /// here is that a box committed writes to the surface and that its binding
    /// survives the write, because these three are per-dataset: a box whose
    /// binding a typed character had discarded would go on showing the words
    /// belonging to the dataset before this one.
    function test_the_label_boxes_write_to_the_plot_and_stay_bound() {
        verify(select("/compressed"))
        const view = createTemporaryObject(dataComponent, testCase, viewSize)
        waitForRendering(view)
        view.show("plot")
        waitForRendering(view)
        const plot = findChild(view, "plotSurface")

        const panel = createTemporaryObject(plotSettingsComponent, testCase,
                                            { width: Theme.railWidth, height: 900,
                                              target: plot })
        verify(panel, "the plot settings panel must instantiate")
        const boxes = [[findChild(panel, "plotTitleField"), "plotTitle"],
                       [findChild(panel, "plotXLabelField"), "xLabel"],
                       [findChild(panel, "plotYLabelField"), "yLabel"]]

        for (let i = 0; i < boxes.length; ++i) {
            const box = boxes[i][0]
            const name = boxes[i][1]
            verify(box, name + " must have a box")
            compare(box.text, "")

            box.text = "written " + name
            panel.commitLabel(box, name)
            compare(plot[name], "written " + name)

            // ...and the box is bound again, so a value arriving from anywhere
            // else -- DatasetMemory restoring one -- shows up in it.
            plot[name] = "from elsewhere"
            compare(box.text, "from elsewhere")
            plot[name] = ""
            compare(box.text, "")
        }
    }

    /// The panel's grid dropdown and its corner radios name what the plot is
    /// actually doing.
    ///
    /// Both of these are controls whose mark is written imperatively by the
    /// thing underneath them -- a ComboBox sets its own currentIndex, a
    /// ButtonGroup sets `checked` -- and an imperative write to a bound
    /// property discards the binding for good. Every setting here is
    /// per-dataset, so a control that lost its binding would go on naming the
    /// density or the corner belonging to the dataset before this one, which
    /// is a control that is wrong rather than one that is stale.
    function test_the_grid_and_corner_controls_follow_the_plot() {
        verify(select("/compressed"))
        const view = createTemporaryObject(dataComponent, testCase, viewSize)
        waitForRendering(view)
        view.show("plot")
        waitForRendering(view)
        const plot = findChild(view, "plotSurface")

        const panel = createTemporaryObject(plotSettingsComponent, testCase,
                                            { width: Theme.railWidth, height: 900,
                                              target: plot })
        verify(panel, "the plot settings panel must instantiate")

        const box = findChild(panel, "gridModeBox")
        verify(box, "the grid dropdown must be reachable")
        compare(panel.gridModeKeys.length, panel.gridModeLabels.length)
        for (let i = 0; i < panel.gridModeKeys.length; ++i) {
            plot.gridMode = panel.gridModeKeys[i]
            compare(box.selectedIndex, i,
                    "the box must name " + panel.gridModeKeys[i])
        }

        // The two step boxes are absent under the other three densities
        // rather than disabled: a control that cannot do anything is a
        // control the reader has to work out the rule for.
        const stepX = findChild(panel, "gridStepXField")
        const stepY = findChild(panel, "gridStepYField")
        verify(stepX && stepY, "the custom steps must be reachable")
        compare(plot.gridMode, "custom")
        verify(panel.customGrid)
        plot.gridMode = "loose"
        verify(!panel.customGrid)

        // A step is a distance, so backwards is not a finer grid and zero is
        // "none" said in the wrong control; both come back as nothing ruled,
        // which is what PlotFrame does with a step it cannot use.
        plot.gridMode = "custom"
        stepX.committed(-5)
        compare(plot.gridStepX, 0)
        stepY.committed(2.5)
        compare(plot.gridStepY, 2.5)

        // ...and the four corners.
        plot.legendOnPlot = true
        for (let i = 0; i < panel.cornerKeys.length; ++i) {
            plot.legendCorner = panel.cornerKeys[i]
            const marks = findAllChecked(panel)
            compare(marks.length, 1,
                    "exactly one corner must be marked, not " + marks.length)
            compare(marks[0].text, panel.cornerLabels[i])
        }

        plot.legendOnPlot = false
        plot.gridMode = "loose"
        plot.gridStepX = 0
        plot.gridStepY = 0
    }

    /// The corner radios that are ticked, by their label. Found by what they
    /// are rather than by an objectName apiece: a Repeater's delegates have no
    /// names of their own, and these are the only radios in the panel whose
    /// text is one of the four corners.
    function findAllChecked(panel) {
        const corners = panel.cornerLabels
        const found = []
        const visit = (item) => {
            if (item.checked === true && corners.indexOf(item.text) >= 0)
                found.push(item)
            for (let i = 0; i < item.children.length; ++i)
                visit(item.children[i])
        }
        visit(panel)
        return found
    }

    /// The plot goes to the clipboard as a picture.
    ///
    /// Copying succeeds by putting something where this program cannot see it,
    /// which is why ImageClipboard can be asked what is on the clipboard: a
    /// feature whose only witness is a paste into some other application is a
    /// feature no test can hold.
    ///
    /// The grab is one more frame rendered, so nothing is on the clipboard in
    /// the call that asks for it -- tryVerify is what waits for the frame.
    function test_a_copied_plot_reaches_the_clipboard() {
        verify(select("/compressed"))

        const win = createTemporaryObject(viewWindowComponent, testCase)
        waitForRendering(win.view)
        win.view.show("plot")
        waitForRendering(win.view)

        const plot = findChild(win.view, "plotSurface")
        verify(plot, "the plot surface must be reachable")
        verify(plot.drawable, "there must be a plot to copy")

        copySpy.clear()
        copyFailedSpy.clear()
        verify(plot.copyImage(), "a drawn plot must accept the request")
        tryVerify(() => copySpy.count > 0 || copyFailedSpy.count > 0, 10000,
                  "the grab must answer one way or the other")
        compare(copyFailedSpy.count, 0,
                copyFailedSpy.count > 0 ? copyFailedSpy.signalArguments[0][0] : "")

        // The frame's own size in device pixels, which is what a reader
        // pastes. Device and not logical: an item is laid out in logical
        // pixels and drawn into a framebuffer with devicePixelRatio of them
        // for each, so copying at the logical size would throw exactly that
        // factor away before the picture left here.
        //
        // The frame is the surface minus whatever the legend panel is holding
        // on the left, which is nothing while it is closed. Asserted exactly
        // rather than within a tolerance, because "a picture of the plot" and
        // "a picture of the window" differ by a great deal more than a
        // rounding and this is the only place that could tell them apart.
        const copied = ImageClipboard.imageOnClipboard()
        const ratio = Math.max(1, Screen.devicePixelRatio)
        verify(copied.width > 0 && copied.height > 0,
               "something must be on the clipboard")
        compare(copied.width,
                Math.round((plot.width - plot.contentLeft) * ratio))
        compare(copied.height, Math.round(plot.height * ratio))
    }

    /// ...and Ctrl+C does it with the pointer over the pane.
    ///
    /// The half of the pair that is easy to get wrong, because it depends on
    /// something nothing else in this window depends on: neither the plot nor
    /// the frame holds the keyboard -- the focus is wherever the reader last
    /// typed -- so the shortcut has to be armed by where the *pointer* is and
    /// not by focus. Which is also what keeps two plots in one window from
    /// both claiming it.
    function test_ctrl_c_copies_the_plot_the_pointer_is_over() {
        verify(select("/compressed"))

        const win = createTemporaryObject(viewWindowComponent, testCase)
        waitForRendering(win.view)
        win.view.show("plot")
        waitForRendering(win.view)
        win.requestActivate()

        const plot = findChild(win.view, "plotSurface")
        const lines = findChild(win.view, "plotLines")
        verify(plot && lines, "the plot and its lines must be reachable")

        // Pointer somewhere else: the shortcut is not armed, so the key goes
        // wherever it would have gone and nothing is copied.
        mouseMove(lines, -20, -20)
        waitForRendering(win.view)
        copySpy.clear()
        copyFailedSpy.clear()
        keyClick(Qt.Key_C, Qt.ControlModifier)
        wait(50)
        compare(copySpy.count, 0, "a plot nobody is pointing at must not copy")

        // ...and over the pane it is.
        mouseMove(lines, Math.round(lines.width / 2),
                  Math.round(lines.height / 2))
        waitForRendering(win.view)
        keyClick(Qt.Key_C, Qt.ControlModifier)
        tryVerify(() => copySpy.count > 0 || copyFailedSpy.count > 0, 10000,
                  "the grab must answer one way or the other")
        compare(copyFailedSpy.count, 0,
                copyFailedSpy.count > 0 ? copyFailedSpy.signalArguments[0][0] : "")

        mouseMove(lines, -20, -20)
    }

    /// The ticks are drawn where the curve is.
    ///
    /// The chrome and the renderer each map a value to a place on the pane,
    /// and they are two implementations of one rule -- three lines of it, in
    /// QML and in C++. If they ever disagree the grid does not look wrong, it
    /// looks authoritative and reads off by a pixel.
    ///
    /// So the two are asserted against each other directly, over the window on
    /// screen and over a window twice as tall: a rule that happens to hold at
    /// the ends of one span is not the same rule.
    function test_the_ticks_agree_with_the_renderer_about_where_a_value_sits() {
        verify(select("/compressed"))

        const win = createTemporaryObject(viewWindowComponent, testCase)
        waitForRendering(win.view)
        win.view.show("plot")
        waitForRendering(win.view)
        const plot = findChild(win.view, "plotSurface")
        const lines = findChild(win.view, "plotLines")
        verify(lines, "the drawing surface must be reachable")

        const agreesOver = (low, high) => {
            for (let i = 0; i <= 10; ++i) {
                const value = low + (high - low) * i / 10
                const mine = lines.parent.yFraction(value)
                const theirs = lines.yFraction(value)
                verify(Math.abs(mine - theirs) < 1e-9,
                       "the chrome puts " + value + " at " + mine
                       + " and the renderer at " + theirs)
            }
        }

        agreesOver(lines.yMin, lines.yMax)
        // ...and outside it, which is where a stroke that leaves the pane is
        // projected to and where an off-by-a-sign would show.
        agreesOver(lines.yMin - (lines.yMax - lines.yMin),
                   lines.yMax + (lines.yMax - lines.yMin))
    }

    /// Start, step and stop: any two describe the x axis and the third follows
    /// from `stop = start + step x len(data)`. They are the x *values* -- point
    /// i sits at start + i x step -- and not a window onto the plot, so the
    /// default is what a reader would write for data with no x of its own.
    function test_two_of_the_three_x_values_describe_the_axis() {
        verify(select("/matrix")) // 4 rows x 3 columns
        const view = createTemporaryObject(dataComponent, testCase, viewSize)
        waitForRendering(view)
        view.show("plot")
        const plot = findChild(view, "plotSurface")
        verify(plot, "the plot surface must be reachable")

        // Nothing stated is 0 : 1 : len(data), the element's own index, which
        // is where every dataset starts.
        verify(plot.autoAxis)
        compare(plot.locks.length, 0)
        compare(plot.dataLength, 3)
        compare(plot.resolved.start, 0)
        compare(plot.resolved.step, 1)
        compare(plot.resolved.stop, 3)

        // ...and the points really are put there. The values never cross into
        // QML, so what the surface can be held to is that it pushed the two
        // numbers the line is built from down to the plot.
        compare(AppController.datasetPlot.xStart, 0)
        compare(AppController.datasetPlot.xStep, 1)

        // Start and stop given: the step is whatever divides the span into
        // len(data) elements.
        plot.rangeStart = 0
        plot.lock("start")
        plot.rangeStop = 30
        plot.lock("stop")
        compare(plot.locks.length, 2)
        compare(plot.resolved.start, 0)
        compare(plot.resolved.stop, 30)
        compare(plot.resolved.step, 10)
        // And the axis is actually drawn against them.
        compare(plot.axisMinX, 0)
        compare(plot.axisMaxX, 30)
        compare(AppController.datasetPlot.xStep, 10)

        // Start and step given: the stop is len(data) steps along.
        plot.locks = []
        plot.rangeStart = 5
        plot.lock("start")
        plot.rangeStep = 2
        plot.lock("step")
        compare(plot.resolved.start, 5)
        compare(plot.resolved.step, 2)
        compare(plot.resolved.stop, 5 + 2 * plot.dataLength)

        // Step and stop given: the start is worked back from the stop.
        plot.locks = []
        plot.rangeStep = 4
        plot.lock("step")
        plot.rangeStop = 100
        plot.lock("stop")
        compare(plot.resolved.stop, 100)
        compare(plot.resolved.start, 100 - 4 * plot.dataLength)

        // One stated is still an answer: the default supplies the next one
        // along -- start before step before stop -- and the third follows.
        plot.locks = []
        plot.rangeStart = 7
        plot.lock("start")
        compare(plot.resolved.start, 7)
        compare(plot.resolved.step, 1)
        compare(plot.resolved.stop, 7 + plot.dataLength)

        plot.locks = []
    }

    function test_a_third_lock_releases_the_oldest() {
        verify(select("/matrix"))
        const view = createTemporaryObject(dataComponent, testCase, viewSize)
        waitForRendering(view)
        view.show("plot")
        const plot = findChild(view, "plotSurface")

        plot.locks = []
        plot.lock("start")
        plot.lock("step")
        compare(plot.locks, ["start", "step"])

        // Never three: the reader states two and the third is computed, so
        // locking a third has to let go of something rather than refuse.
        plot.lock("stop")
        compare(plot.locks, ["step", "stop"])
        verify(!plot.locked("start"))
        verify(plot.locked("step"))
        verify(plot.locked("stop"))

        // Locking one already locked changes nothing.
        plot.lock("stop")
        compare(plot.locks, ["step", "stop"])

        plot.unlock("step")
        compare(plot.locks, ["stop"])
    }

    // Not `..._falls_back_to_the_data`: QtTest reads a trailing `_data` as the
    // data-provider function for a test of the name without it, so a test
    // named that way is silently never run.
    function test_a_range_that_is_not_an_axis_falls_back() {
        verify(select("/matrix"))
        const view = createTemporaryObject(dataComponent, testCase, viewSize)
        waitForRendering(view)
        view.show("plot")
        const plot = findChild(view, "plotSurface")

        plot.locks = []
        plot.rangeStart = 50
        plot.lock("start")
        plot.rangeStop = 10 // below the start: not an axis
        plot.lock("stop")
        verify(!plot.rangeValid)
        // The plot keeps standing on the default axis rather than collapsing
        // while the reader is halfway through typing.
        verify(plot.axisMinX < plot.axisMaxX)
        compare(plot.axisMinX, 0)
        compare(plot.axisMaxX, plot.dataLength)
        compare(AppController.datasetPlot.xStart, 0)
        compare(AppController.datasetPlot.xStep, 1)

        plot.locks = []
    }

    /// The y axis is the values and nothing to set about it. The two boxes and
    /// the handles that used to narrow it are gone: the wheel and the drag look
    /// closer at part of the axis already, and a second way to say the same
    /// thing went stale the moment the selection moved.
    function test_the_y_axis_is_the_extent_of_the_values() {
        verify(select("/matrix")) // 0 .. 32, by tens and ones
        const view = createTemporaryObject(dataComponent, testCase, viewSize)
        waitForRendering(view)
        view.show("plot")
        const plot = findChild(view, "plotSurface")
        const backing = AppController.datasetPlot

        // A little air above and below, so a line at the extreme is a line and
        // not part of the frame -- but the band is the data's, either way.
        verify(plot.lowerBound < backing.minimum)
        verify(plot.upperBound > backing.maximum)
        verify(plot.upperBound > plot.lowerBound)

        // Nothing left to set it with.
        compare(plot.autoRange, undefined)
        compare(plot.rangeMinimum, undefined)
        compare(plot.rangeMaximum, undefined)
    }

    /// Zooming in reads the run on screen again, and says so in the footer.
    ///
    /// The whole chain, from a gesture on the surface to a different set of
    /// values reaching the renderer. The plot holds a summary of the whole line
    /// -- a couple of thousand points however long it is -- so zooming used to
    /// stretch that summary, and the surface's ceiling on magnification was
    /// pinned at 256 for exactly that reason. Now the surface tells the plot
    /// object what is on screen, the object reads that run again once the view
    /// has stopped moving, and the readout stops saying "thinned" when a bucket
    /// has become one element.
    function test_zooming_in_reads_the_run_on_screen_again() {
        verify(select("/trace")) // 20000 elements, summarised to 2000 points
        const win = createTemporaryObject(viewWindowComponent, testCase)
        waitForRendering(win.view)
        win.view.show("plot")
        waitForRendering(win.view)
        const plot = findChild(win.view, "plotSurface")
        const backing = AppController.datasetPlot

        // Let the pane's own width land before anything is counted. The surface
        // pushes it on its first refill and the plot object waits out the drag
        // -- kResizeMilliseconds -- so a count taken before that is a count at
        // whatever width the window this test replaced happened to have.
        wait(600)
        waitForRendering(win.view)

        // The ceiling is the data's now rather than a constant.
        compare(plot.maxZoom, Math.max(256, 20000 / 16))

        // How many points that is follows the pane -- a bucket is a column --
        // so what is asserted here is the shape of the answer and not a number
        // that would change with the size of the window this test opens.
        const whole = backing.pointCount
        verify(whole > 0)
        verify(whole <= 2 * 20000)
        verify(backing.thinned, "the whole line does not fit in what is drawn")
        const summarised = colouredPixels(grabImage(plot), Theme.sliceBarHeight)
        verify(summarised > 20, "the line must be drawn at all: " + summarised)

        // Two hundred times in, about the middle: a hundred elements across the
        // pane, which is a bucket of one however wide the pane is.
        const area = plot.plotRect
        plot.zoomAt(area.x + area.width / 2, area.y + area.height / 2, 200)
        compare(plot.zoomX, 200)
        // The wheel takes both axes with it, and this dataset's spike is nine
        // where its sine is one -- so fifty times in on y is a band of empty
        // air above the line. Put y back: what is being asked about here is
        // resolution along x.
        plot.zoomY = 1.0
        plot.panY = 0.0
        // The gesture itself reads nothing; the tenth of a second after it
        // does.
        wait(400)
        waitForRendering(win.view)

        verify(!backing.thinned, "at a bucket of one the samples are the file's")
        verify(backing.pointCount > 0)
        // And it is drawn where the pane is. A run carries an offset into the
        // line it came from -- PlotLine::positionStart -- and a run drawn
        // without it would be a line somewhere off the frame and a blank plot,
        // which every count above would still agree with.
        const resolved = colouredPixels(grabImage(plot), Theme.sliceBarHeight)
        verify(resolved > 20, "the run that was read must be on the pane: " + resolved)

        // And back out, which needs no read at all: the whole-line summary was
        // never thrown away.
        plot.resetView()
        compare(backing.pointCount, whole)
        verify(backing.thinned)
        waitForRendering(win.view)
        compare(colouredPixels(grabImage(plot), Theme.sliceBarHeight), summarised)
    }

    /// A pane that changes width keeps its picture.
    ///
    /// What a line is thinned to follows the pane -- a bucket is a column --
    /// so every change of width is a question for the plot object. Opening a
    /// rail is the sharpest form of it: the pane narrows by a couple of hundred
    /// pixels in one step, and what the reader saw was the plot go blank and
    /// stay blank until they moved the pointer into it.
    ///
    /// So this asserts the picture rather than the counts: there is a line on
    /// the pane before the rail opens, and there is one on the frame after it.
    function test_a_rail_opening_does_not_empty_the_plot() {
        verify(select("/trace"))
        const win = createTemporaryObject(viewWindowComponent, testCase)
        waitForRendering(win.view)
        win.view.show("plot")
        waitForRendering(win.view)
        const plot = findChild(win.view, "plotSurface")
        verify(plot.drawable)
        const before = colouredPixels(grabImage(plot), Theme.sliceBarHeight)
        verify(before > 20, "the line must be drawn to begin with: " + before)

        win.view.toggleRail("data")
        compare(win.view.rail, "data")
        waitForRendering(win.view)
        verify(plot.drawable, "the plot must still know it has something to draw")
        const narrowed = colouredPixels(grabImage(plot), Theme.sliceBarHeight)
        verify(narrowed > 20, "the line must survive the rail opening: " + narrowed)

        win.view.toggleRail("data")
        compare(win.view.rail, "")
        waitForRendering(win.view)
        const restored = colouredPixels(grabImage(plot), Theme.sliceBarHeight)
        verify(restored > 20, "...and closing it again: " + restored)

        // ...and it survives the slower version of the same thing: a window
        // dragged wider a step at a time. Every one of those steps used to be a
        // re-read of every drawn line, and the reader saw the pane flicker
        // under their hand. The picture is asserted after each, because a blank
        // frame in the middle of a drag is exactly what this is about.
        for (let width = 900; width >= 600; width -= 40) {
            win.width = width
            waitForRendering(win.view)
            const during = colouredPixels(grabImage(plot), Theme.sliceBarHeight)
            verify(during > 20,
                   "the line must be drawn at every width: " + width
                   + " gave " + during)
        }
        // And once the drag stops, the pane is re-thinned for the width it
        // ended at.
        // Comfortably past DatasetPlot::kResizeMilliseconds, which is 200.
        wait(600)
        waitForRendering(win.view)
        const settled = colouredPixels(grabImage(plot), Theme.sliceBarHeight)
        verify(settled > 20, "...and after it settles: " + settled)
    }

    /// Shift zooms x alone; Ctrl zooms y alone.
    ///
    /// A plot of a long trace is read by stretching time without changing what
    /// an amplitude is worth, and a plot of a narrow band is read the other way
    /// round. A wheel that always took both axes made either of those a zoom
    /// followed by a correcting pan, done by eye -- so the two modifiers every
    /// other plot in the field uses do here what they do there.
    ///
    /// Through zoomAxesFor() and zoomAt() rather than through a synthesised
    /// wheel event: what is being asserted is which axis moves, and QtQuickTest
    /// cannot put a modifier on a wheel the WheelHandler will accept.
    function test_a_modifier_holds_one_axis_still() {
        verify(select("/compressed"))
        const win = createTemporaryObject(viewWindowComponent, testCase)
        waitForRendering(win.view)
        win.view.show("plot")
        waitForRendering(win.view)
        const plot = findChild(win.view, "plotSurface")
        const area = plot.plotRect
        const px = area.x + area.width / 2
        const py = area.y + area.height / 2

        compare(plot.zoomAxesFor(Qt.NoModifier), "both")
        compare(plot.zoomAxesFor(Qt.ShiftModifier), "x")
        compare(plot.zoomAxesFor(Qt.ControlModifier), "y")
        // Both together is neither, because a modifier this file does not know
        // about is a window manager's and not an instruction.
        compare(plot.zoomAxesFor(Qt.ShiftModifier | Qt.ControlModifier), "both")

        plot.zoomAt(px, py, 4, "x")
        compare(plot.zoomX, 4)
        compare(plot.zoomY, 1)
        compare(plot.panY, 0)

        plot.resetView()
        plot.zoomAt(px, py, 4, "y")
        compare(plot.zoomY, 4)
        compare(plot.zoomX, 1)
        compare(plot.panX, 0)

        plot.resetView()
        plot.zoomAt(px, py, 4, "both")
        compare(plot.zoomX, 4)
        compare(plot.zoomY, 4)

        // ...and the way back is a double tap, which is the gesture every map
        // and image viewer uses. It costs nothing: the whole-line summary is
        // never thrown away, so the most zoomed-out picture is always already
        // in hand and going to it is a draw.
        const gestures = findChild(win.view, "plotGestures")
        verify(gestures, "the gesture layer must be reachable")
        doubleClickOn(gestures)
        waitForRendering(win.view)
        compare(plot.zoomX, 1)
        compare(plot.zoomY, 1)
        compare(plot.panX, 0)
        compare(plot.panY, 0)
        verify(!plot.zoomed)

        // ...and the drag still pans, which is the thing the double click could
        // have cost: the two gestures share one press, and a mouse area that
        // took the grab and kept it would have frozen the view.
        plot.zoomAt(px, py, 8, "both")
        const started = plot.panX
        mouseDrag(gestures, Math.round(gestures.width / 2),
                  Math.round(gestures.height / 2), -80, 0)
        waitForRendering(win.view)
        verify(plot.panX !== started,
               "dragging must still move the view: " + plot.panX)

        plot.resetView()
    }

    /// The highlight is drawn, and it is drawn wider.
    ///
    /// This was the one part of the move off Qt Graphs with a real technical
    /// risk in it. A line width above 1.0 is an *optional* RHI feature and
    /// several backends ignore it without saying so, and the highlight -- the
    /// only affordance for following one line through a bundle of fifty -- is a
    /// line drawn at double width. So the strokes are built out of triangles
    /// instead, and this is what says the triangles are the width they were
    /// asked for once the whole chain from the legend down is connected.
    ///
    /// One line, so that opacity cannot account for the difference: with a
    /// single line drawn, highlighting it changes its width and nothing else.
    function test_the_highlighted_line_is_drawn_wider() {
        verify(select("/long_vec")) // one line of a thousand points
        const win = createTemporaryObject(viewWindowComponent, testCase)
        waitForRendering(win.view)
        win.view.show("plot")
        waitForRendering(win.view)

        const plot = findChild(win.view, "plotSurface")
        verify(plot, "the plot surface must be reachable")
        compare(AppController.datasetPlot.seriesCount, 1)

        // Counted in ink rather than in pixels-unlike-the-ground: a grab is of
        // the window cropped to the item's size, so it carries the tree and the
        // bar with it, and those are neutral where a drawn line is not.
        compare(plot.highlighted, -1)
        const thin = colouredPixels(grabImage(plot), Theme.sliceBarHeight)
        verify(thin > 20, "the line must be drawn at all: " + thin)

        plot.highlighted = 0
        waitForRendering(win.view)
        const thick = colouredPixels(grabImage(plot), Theme.sliceBarHeight)
        verify(thick > thin * 1.3,
               "a highlighted line must be visibly heavier: " + thin + " -> "
               + thick)

        plot.highlighted = -1
        waitForRendering(win.view)
        compare(colouredPixels(grabImage(plot), Theme.sliceBarHeight), thin)
    }

    /// Markers are punctuation on a line, and they mark samples.
    ///
    /// Only where the line is drawn sample for sample: once the envelope is
    /// summarising, a drawn point stands for a whole bucket and a dot on it
    /// marks nothing. A thousand points on a pane a thousand wide is drawn
    /// whole, so this is the case where they appear.
    function test_markers_put_a_dot_on_every_sample() {
        verify(select("/long_vec"))
        const win = createTemporaryObject(viewWindowComponent, testCase)
        waitForRendering(win.view)
        win.view.show("plot")
        waitForRendering(win.view)

        const plot = findChild(win.view, "plotSurface")
        verify(plot, "the plot surface must be reachable")

        verify(!plot.showMarkers)
        const bare = colouredPixels(grabImage(plot), Theme.sliceBarHeight)
        verify(bare > 20, "the line must be drawn at all: " + bare)

        plot.showMarkers = true
        waitForRendering(win.view)
        const dotted = colouredPixels(grabImage(plot), Theme.sliceBarHeight)
        verify(dotted > bare,
               "markers must put ink on the plot: " + bare + " -> " + dotted)

        plot.showMarkers = false
        waitForRendering(win.view)
        compare(colouredPixels(grabImage(plot), Theme.sliceBarHeight), bare)
    }

    /// The crosshair reads a sample, not a position.
    ///
    /// New with the renderer, and one of the two things the plot could not do
    /// at all while it drew through Qt Graphs. It snaps: a plot is a picture of
    /// measurements that were taken, and a readout of the space between two of
    /// them is a reading of something nobody measured. The same argument the
    /// time base makes about interpolating an x, and the gap makes about a
    /// value that would not read.
    function test_the_crosshair_reads_the_sample_under_the_pointer() {
        verify(select("/compressed")) // 100 x 100, so x runs 0 .. 99
        const win = createTemporaryObject(viewWindowComponent, testCase)
        waitForRendering(win.view)
        win.view.show("plot")
        waitForRendering(win.view)

        const lines = findChild(win.view, "plotLines")
        verify(lines, "the drawing surface must be reachable")
        const frame = lines.parent

        // Nothing until the pointer is over the pane.
        verify(!frame.reading.valid,
               "there must be no reading before anything is pointed at")

        mouseMove(lines, Math.round(lines.width * 0.4),
                  Math.round(lines.height * 0.5))
        waitForRendering(win.view)
        verify(frame.reading.valid, "pointing at the plot must produce a reading")

        // Snapped. The default axis is the element's own index, so a reading
        // that landed on a sample is a whole number and one between two of them
        // is not.
        compare(frame.reading.x, Math.round(frame.reading.x))
        verify(frame.reading.x >= 0 && frame.reading.x <= 99,
               "the reading must be inside the data: x = " + frame.reading.x)
        verify(frame.reading.line >= 0,
               "the reading must say which line it came from")

        // And it follows the pointer.
        const wasAt = frame.reading.x
        mouseMove(lines, Math.round(lines.width * 0.7),
                  Math.round(lines.height * 0.5))
        waitForRendering(win.view)
        verify(frame.reading.valid)
        verify(frame.reading.x > wasAt,
               "moving right must read further along x: " + wasAt + " -> "
               + frame.reading.x)

        // ...and it is drawn, not merely computed: two rules, a ring on the
        // sample and a box with the numbers in it are all ink that was not on
        // the pane before.
        const busy = (shot) => {
            let found = 0
            for (let x = 0; x < shot.width; x += 2) {
                for (let y = Theme.sliceBarHeight; y < shot.height; y += 2) {
                    if (shot.pixel(x, y) !== Theme.surfaceInset)
                        ++found
                }
            }
            return found
        }
        const withCrosshair = busy(grabImage(lines.parent))

        mouseMove(lines, -20, -20)
        waitForRendering(win.view)
        verify(!frame.reading.valid,
               "the reading must go when the pointer leaves the pane")
        verify(busy(grabImage(lines.parent)) < withCrosshair,
               "the crosshair must leave with it")
    }

    /// The numbers go in the bar below the plot, not in a box on top of it.
    ///
    /// They were in a box on top of it, because that is where a plotting
    /// library puts them. It is the wrong place here: this application already
    /// has a strip along the foot of every view whose whole job is to say what
    /// is on screen in numbers, and a second readout in a second style over the
    /// picture is a second convention.
    function test_the_reading_is_reported_in_the_footer() {
        verify(select("/compressed"))
        const win = createTemporaryObject(viewWindowComponent, testCase)
        waitForRendering(win.view)
        win.view.show("plot")
        waitForRendering(win.view)

        const plot = findChild(win.view, "plotSurface")
        const lines = findChild(win.view, "plotLines")
        verify(lines, "the drawing surface must be reachable")

        compare(plot.readingFacts.length, 0)

        mouseMove(lines, Math.round(lines.width * 0.4),
                  Math.round(lines.height * 0.5))
        waitForRendering(win.view)
        verify(plot.reading.valid)

        // The line it came from -- there are sixty-four of them here -- and the
        // two numbers.
        const facts = plot.readingFacts
        compare(facts.length, 3)
        verify(String(facts[0]).startsWith("line "), "got " + facts[0])
        verify(String(facts[1]).startsWith("x "), "got " + facts[1])
        verify(String(facts[2]).startsWith("y "), "got " + facts[2])
        // The default axis counts elements, so an x reads as a whole number
        // rather than as "12.0000" -- four digits of decoration on a count.
        verify(!String(facts[1]).includes("."), "got " + facts[1])

        // ...at the right-hand end of the bar, and not appended to the run of
        // facts on the left. Appended, a reading that grew a digit moved the
        // line count and the point count under a reader who was watching them.
        // The plot's own footer, not the first one in the tree: the view
        // carries three of them -- table, plot, image -- and findChild answers
        // with whichever it reaches first.
        const footer = findChild(win.view, "plotFooter")
        verify(footer, "the plot's footer must be reachable")
        const trailing = findChild(footer, "footerTrailing")
        verify(trailing, "the footer must carry a right-hand readout")
        verify(trailing.visible, "it must be showing: " + trailing.text)
        for (let i = 0; i < facts.length; ++i) {
            verify(String(trailing.text).includes(String(facts[i])),
                   "the reading must be in the right-hand readout: got "
                   + trailing.text)
        }
        verify(trailing.x + trailing.width > footer.width / 2,
               "the reading must sit in the right-hand half of the bar: "
               + (trailing.x + trailing.width) + " of " + footer.width)

        mouseMove(lines, -20, -20)
        waitForRendering(win.view)
        compare(plot.readingFacts.length, 0)
        verify(!trailing.visible,
               "with nothing under the pointer the right-hand readout goes")
    }

    /// ...and a plot of one line does not say which line.
    ///
    /// "Which" has no answer worth printing there, and a line's name can be a
    /// bare row index -- which read as a stray number sitting between two facts
    /// that were labelled: "1 LINE . 1000 POINTS . Y 0.000 ... 999.0 . 0 . X 450".
    function test_a_single_line_is_not_named_in_the_reading() {
        verify(select("/long_vec"))
        const win = createTemporaryObject(viewWindowComponent, testCase)
        waitForRendering(win.view)
        win.view.show("plot")
        waitForRendering(win.view)

        const plot = findChild(win.view, "plotSurface")
        const lines = findChild(win.view, "plotLines")
        compare(AppController.datasetPlot.seriesCount, 1)

        mouseMove(lines, Math.round(lines.width * 0.45),
                  Math.round(lines.height * 0.5))
        waitForRendering(win.view)
        verify(plot.reading.valid)

        const facts = plot.readingFacts
        compare(facts.length, 2)
        verify(String(facts[0]).startsWith("x "), "got " + facts[0])
        verify(String(facts[1]).startsWith("y "), "got " + facts[1])

        mouseMove(lines, -20, -20)
    }

    /// The crosshair is a control now, and it can be turned off.
    function test_the_cursor_can_be_turned_off() {
        verify(select("/compressed"))
        const win = createTemporaryObject(viewWindowComponent, testCase)
        waitForRendering(win.view)
        win.view.show("plot")
        waitForRendering(win.view)

        const plot = findChild(win.view, "plotSurface")
        const lines = findChild(win.view, "plotLines")
        verify(plot.showCursor, "the cursor is on by default")

        mouseMove(lines, Math.round(lines.width * 0.4),
                  Math.round(lines.height * 0.5))
        waitForRendering(win.view)
        verify(plot.reading.valid)

        plot.showCursor = false
        waitForRendering(win.view)
        verify(!plot.reading.valid,
               "turning the cursor off must stop it reading")
        compare(plot.readingFacts.length, 0)

        plot.showCursor = true
    }

    /// The crosshair is clipped to the pane, and that is not tidiness.
    ///
    /// The sample nearest the pointer need not be on screen. Zoom the y axis
    /// into a narrow band and point at part of the line that has left it: the
    /// nearest sample is hundreds of pixels above or below the frame, and the
    /// rule through it was drawn there -- across the bar above the plot.
    ///
    /// Asserted in two halves, because either alone would pass on a broken
    /// build: that the dangerous state is reachable at all, and that the thing
    /// which contains it is the pane.
    function test_the_crosshair_stays_inside_the_plot_area() {
        verify(select("/long_vec")) // a ramp from 0 to 999
        const win = createTemporaryObject(viewWindowComponent, testCase)
        waitForRendering(win.view)
        win.view.show("plot")
        waitForRendering(win.view)

        const plot = findChild(win.view, "plotSurface")
        const lines = findChild(win.view, "plotLines")
        const frame = lines.parent
        const cursor = findChild(win.view, "plotCursor")
        verify(cursor, "the crosshair must be reachable")

        // Whatever else is true, it is clipped and it is the pane.
        verify(cursor.clip, "the crosshair must be clipped")
        compare(cursor.x, frame.area.x)
        compare(cursor.y, frame.area.y)
        compare(cursor.width, frame.area.width)
        compare(cursor.height, frame.area.height)

        // A narrow band around the middle of a ramp: at the left-hand end of
        // the line every sample is far below it.
        plot.zoomY = 20
        waitForRendering(win.view)
        mouseMove(lines, 2, Math.round(lines.height * 0.5))
        waitForRendering(win.view)
        verify(plot.reading.valid)
        verify(plot.reading.py > frame.area.height,
               "the nearest sample must be below the pane for this to test "
               + "anything: py = " + plot.reading.py + " of " + frame.area.height)

        plot.zoomY = 1
        mouseMove(lines, -20, -20)
    }

    function test_a_reversed_cycle_runs_the_other_way() {
        verify(select("/cube"))
        const view = createTemporaryObject(dataComponent, testCase, viewSize)
        waitForRendering(view)
        view.show("plot")
        const plot = findChild(view, "plotSurface")

        plot.colorMode = "viridis"
        const first = String(plot.seriesColor(0, 0, 6))
        const last = String(plot.seriesColor(5, 5, 6))
        verify(first !== last)

        // Which end of a ramp is the dark one is a property of the ramp and not
        // of the data, so reversing swaps the ends and nothing else.
        plot.colorsReversed = true
        compare(String(plot.seriesColor(0, 0, 6)), last)
        compare(String(plot.seriesColor(5, 5, 6)), first)

        plot.colorsReversed = false
        plot.colorMode = "same"
    }

    /// The colour map's own range. Over the map rather than over the data,
    /// because a plot colours by which line a stroke is and not by how big its
    /// numbers are -- but it is the same control answering the same question
    /// the image and the table put over their values.
    function test_the_colour_map_spans_the_slice_the_reader_set() {
        verify(select("/cube")) // 6 rows
        const view = createTemporaryObject(dataComponent, testCase, viewSize)
        waitForRendering(view)
        view.show("plot")
        const plot = findChild(view, "plotSurface")

        // The whole of the map, to start with, so the arithmetic is a no-op
        // until the reader says otherwise.
        compare(plot.colorFrom, 0)
        compare(plot.colorTo, 1)

        plot.colorMode = "viridis"
        const stops = Theme.colorRamps["viridis"]
        // Six lines across the whole map take the sixths of it that avoid both
        // ends: (i + 1) / 7.
        compare(String(plot.seriesColor(0, 0, 6)),
                String(Theme.rampColor(stops, 1 / 7)))
        compare(String(plot.seriesColor(5, 5, 6)),
                String(Theme.rampColor(stops, 6 / 7)))

        // The top half of the map: the same six shares, taken out of the half
        // the reader kept rather than out of the whole of it.
        plot.colorFrom = 0.5
        compare(String(plot.seriesColor(0, 0, 6)),
                String(Theme.rampColor(stops, 0.5 + 0.5 / 7)))
        compare(String(plot.seriesColor(5, 5, 6)),
                String(Theme.rampColor(stops, 0.5 + 0.5 * 6 / 7)))

        plot.colorFrom = 0
        plot.colorTo = 1
        plot.colorMode = "same"
    }

    function test_the_legend_lists_every_line_and_ticks_them() {
        verify(select("/cube")) // 6 rows
        const view = createTemporaryObject(dataComponent, testCase, viewSize)
        waitForRendering(view)
        view.show("plot")
        const plot = findChild(view, "plotSurface")
        const backing = AppController.datasetPlot

        plot.legendOpen = true
        waitForRendering(view)

        // Every line in the table, named by its slice.
        compare(backing.sourceSeriesCount, 6)
        compare(backing.seriesLabel(3), "[1,0,_]")

        // The number the "first %1" button names comes off the plot, so the
        // button and the window it restores cannot drift apart.
        compare(backing.initialSeriesLimit, 64)

        // All of them, to start with: a plot that showed part of its data by
        // default would be misreporting it by default.
        compare(backing.drawnSeries.length, 6)
        backing.setSeriesVisible(3, false)
        compare(backing.drawnSeries.length, 5)
        verify(!backing.seriesVisible(3))

        backing.selectNone()
        compare(backing.drawnSeries.length, 0)
        backing.selectAll()
        compare(backing.drawnSeries.length, 6)

        // Picking a line out of the bundle, and putting it back.
        plot.highlighted = 2
        waitForRendering(view)
        compare(plot.highlighted, 2)
        plot.highlighted = -1
    }

    function test_a_colour_cycle_gives_the_lines_different_colours() {
        verify(select("/cube"))
        const view = createTemporaryObject(dataComponent, testCase, viewSize)
        waitForRendering(view)
        view.show("plot")
        const plot = findChild(view, "plotSurface")

        // The default gives every line a colour of its own; "same" is still
        // there for a reader who wants the bundle back.
        compare(plot.colorMode, "spectrum")
        verify(String(plot.seriesColor(0, 0, 6)) !== String(plot.seriesColor(5, 5, 6)))

        plot.colorMode = "same"
        compare(String(plot.seriesColor(0, 0, 6)), String(plot.seriesColor(5, 5, 6)))

        plot.colorMode = "viridis"
        verify(String(plot.seriesColor(0, 0, 6)) !== String(plot.seriesColor(5, 5, 6)),
               "a ramp must give the ends of the bundle different colours")

        // The lines sit at the middles of n equal shares of the map, never at
        // its ends: line i of n is at (i + 1) / (n + 1). Both ends of a
        // perceptual ramp are a line nobody can see, and the old rule spent
        // them on two of the lines every time.
        const stops = Theme.colorRamps["viridis"]
        for (let i = 0; i < 6; ++i) {
            compare(String(plot.seriesColor(i, i, 6)),
                    String(Theme.rampColor(stops, (i + 1) / 7)))
        }
        verify(String(plot.seriesColor(0, 0, 6)) !== String(Qt.color(stops[0])),
               "the dark end of the map is not spent on a line")
        verify(String(plot.seriesColor(5, 5, 6))
               !== String(Qt.color(stops[stops.length - 1])),
               "nor is the pale end")

        plot.colorMode = "range"
        plot.colorRangeFrom = "#000000"
        plot.colorRangeTo = "#ffffff"
        // Three lines take the quarters, so the middle one is halfway between
        // black and white -- which is grey, in RGB, as the mix is defined.
        const middle = plot.seriesColor(1, 1, 3)
        verify(Math.abs(middle.r - 0.5) < 0.01, "midpoint r: " + middle.r)
        // ...and the other two are the quarter and the three-quarter greys
        // rather than pure black and pure white.
        verify(Math.abs(plot.seriesColor(0, 0, 3).r - 0.25) < 0.01)
        verify(Math.abs(plot.seriesColor(2, 2, 3).r - 0.75) < 0.01)

        // One line has nothing to separate from, and takes the middle of the
        // map. That is the case the rule most exists for: a single line used
        // to be drawn in the map's first colour, which on a viridis is very
        // nearly the plot's own ground.
        verify(Math.abs(plot.seriesColor(0, 0, 1).r - 0.5) < 0.01)

        plot.colorMode = "same"
    }

    /// A palette gives each line a colour of its own, and starts over rather
    /// than running out.
    ///
    /// The wrap is the whole reason `select all` on a table of ten thousand
    /// rows is still a legible request: the twenty-first line takes the first
    /// colour again and the legend tells the two apart, which is a better
    /// bargain than drawing the rest in grey.
    function test_a_palette_gives_each_line_its_own_colour_and_then_repeats() {
        verify(select("/cube"))
        const view = createTemporaryObject(dataComponent, testCase, viewSize)
        waitForRendering(view)
        view.show("plot")
        const plot = findChild(view, "plotSurface")

        const names = Theme.categoricalPaletteNames
        verify(names.length >= 2, "there must be more than one palette to pick")

        for (let p = 0; p < names.length; ++p) {
            plot.colorMode = names[p]
            const stops = Theme.categoricalPalettes[names[p]]
            const n = stops.length

            // Entry i, verbatim -- not interpolated, and not a share of
            // anything. A palette hands back what it holds.
            for (let i = 0; i < n; ++i) {
                compare(String(plot.seriesColor(i, i, n)), String(Qt.color(stops[i])),
                        names[p] + " line " + i)
            }

            // Past the end it starts over, and it keeps starting over.
            compare(String(plot.seriesColor(n, n, 999)),
                    String(Qt.color(stops[0])))
            compare(String(plot.seriesColor(n + 3, n + 3, 999)),
                    String(Qt.color(stops[3])))
            compare(String(plot.seriesColor(2 * n + 1, 2 * n + 1, 999)),
                    String(Qt.color(stops[1])))
        }

        plot.colorMode = "spectrum"
    }

    /// A palette colour is a property of *which* line it is, and of nothing
    /// else -- not of how many lines are drawn beside it, and not of the band
    /// the reader put over a map.
    ///
    /// This is the difference that matters in use. On a map, unticking one
    /// line in the legend re-cuts the shares and every remaining line changes
    /// colour; the reader is following a stroke and it turns a different
    /// colour underneath them. A palette holds still.
    function test_a_palette_holds_a_line_s_colour_still() {
        verify(select("/cube"))
        const view = createTemporaryObject(dataComponent, testCase, viewSize)
        waitForRendering(view)
        view.show("plot")
        const plot = findChild(view, "plotSurface")

        plot.colorMode = "spectrum"
        const alone = String(plot.seriesColor(2, 2, 3))
        compare(String(plot.seriesColor(2, 2, 40)), alone,
                "how many lines are drawn must not recolour line 2")

        // The map band is a map's control, and the panel hides it here; it
        // must not reach the colours either way.
        plot.colorFrom = 0.4
        plot.colorTo = 0.6
        compare(String(plot.seriesColor(2, 2, 3)), alone,
                "the map band must not touch a palette")
        plot.colorFrom = 0
        plot.colorTo = 1

        // A map does the opposite, which is what the palette is being
        // contrasted with.
        plot.colorMode = "viridis"
        verify(String(plot.seriesColor(2, 2, 3)) !== String(plot.seriesColor(2, 2, 40)),
               "a map re-cuts its shares when the count changes")

        plot.colorMode = "spectrum"
    }

    /// ...and it holds it still in the picture, not only in the arithmetic.
    ///
    /// The case above asks the rule a question. This one unticks a line and
    /// looks at what the renderer was handed, which is where the promise was
    /// actually being broken: the surface named a line by its place among the
    /// *drawn* ones, so hiding one slid every line after it a colour down the
    /// palette. A reader following a green stroke watched it turn blue for no
    /// reason they had anything to do with, and the arithmetic above passed
    /// throughout, because it was never the thing that was wrong.
    function test_hiding_a_line_leaves_the_others_the_colour_they_were() {
        verify(select("/cube")) // 6 rows
        const win = createTemporaryObject(viewWindowComponent, testCase)
        waitForRendering(win.view)
        win.view.show("plot")
        waitForRendering(win.view)

        const plot = findChild(win.view, "plotSurface")
        const lines = findChild(win.view, "plotLines")
        const backing = AppController.datasetPlot
        compare(plot.colorMode, "spectrum")
        tryVerify(() => lines.lineCount() === 6, 5000, "six lines are drawn")

        const before = []
        for (let i = 0; i < 6; ++i)
            before.push(String(lines.seriesColor(i)))

        // The second line goes. The four after it each move up one place in
        // what the item is handed, and must not move one place along the
        // palette with it.
        backing.setSeriesVisible(1, false)
        tryVerify(() => lines.lineCount() === 5, 5000, "one line goes")

        const kept = [0, 2, 3, 4, 5]
        for (let k = 0; k < kept.length; ++k) {
            compare(String(lines.seriesColor(k)), before[kept[k]],
                    "line " + kept[k] + " must keep the colour it had")
        }

        // And it comes back to its own colour rather than to the one at the
        // end of the queue.
        backing.setSeriesVisible(1, true)
        tryVerify(() => lines.lineCount() === 6, 5000, "and comes back")
        for (let i = 0; i < 6; ++i) {
            compare(String(lines.seriesColor(i)), before[i],
                    "line " + i + " must be back where it started")
        }
    }

    /// Reversing a palette turns the order of its entries around, the way
    /// reversing a ramp swaps its ends.
    function test_a_reversed_palette_runs_the_other_way() {
        verify(select("/cube"))
        const view = createTemporaryObject(dataComponent, testCase, viewSize)
        waitForRendering(view)
        view.show("plot")
        const plot = findChild(view, "plotSurface")

        plot.colorMode = "spectrum"
        const stops = Theme.categoricalPalettes["spectrum"]
        const n = stops.length

        plot.colorsReversed = true
        compare(String(plot.seriesColor(0, 0, n)), String(Qt.color(stops[n - 1])))
        compare(String(plot.seriesColor(1, 1, n)), String(Qt.color(stops[n - 2])))
        // ...and it still wraps, from the other end. Counting backwards past
        // zero is where a plain % would hand back stops[-1].
        compare(String(plot.seriesColor(n, n, n)), String(Qt.color(stops[n - 1])))
        compare(String(plot.seriesColor(n + 1, n + 1, n)), String(Qt.color(stops[n - 2])))

        plot.colorsReversed = false
        compare(String(plot.seriesColor(0, 0, n)), String(Qt.color(stops[0])))
    }

    /// Every colour in every palette reads against the ground of the theme it
    /// belongs to.
    ///
    /// The floor is WCAG's 3:1 for non-text, which is what a stroke is. This
    /// is the assertion the palettes were generated to satisfy, and it is
    /// worth keeping as one: a colour edited by eye into either list is
    /// exactly the kind of change that looks fine on the theme its author had
    /// open and disappears on the other.
    function test_every_palette_colour_reads_against_its_own_ground() {
        const names = Theme.categoricalPaletteNames
        const was = Theme.dark

        for (let t = 0; t < 2; ++t) {
            Theme.dark = (t === 0)
            for (let p = 0; p < names.length; ++p) {
                const stops = Theme.categoricalPalettes[names[p]]
                for (let i = 0; i < stops.length; ++i) {
                    const ratio = contrast(stops[i], Theme.surfaceInset)
                    verify(ratio >= 3.0,
                           names[p] + " " + i + " (" + stops[i] + ") reads at "
                           + ratio.toFixed(2) + ":1 on the "
                           + (Theme.dark ? "dark" : "light") + " theme's plot ground")
                }
            }
        }

        Theme.dark = was
    }

    /// The two scopes of a palette are the same colours at different weights,
    /// so a line keeps its identity when the theme flips.
    function test_a_palette_is_the_same_length_in_both_themes() {
        const names = Theme.categoricalPaletteNames
        const was = Theme.dark

        Theme.dark = true
        const lengths = names.map(name => Theme.categoricalPalettes[name].length)

        Theme.dark = false
        for (let p = 0; p < names.length; ++p) {
            compare(Theme.categoricalPalettes[names[p]].length, lengths[p],
                    names[p] + " must offer the same number of lines in both themes")
        }

        Theme.dark = was
    }

    /// The panel asks the kind first and the cycle second, and the dropdown
    /// offers one kind at a time.
    ///
    /// The two used to share one flat list, which gave the reader no way to
    /// tell a palette from a map without picking one: with the list closed,
    /// nothing said that `safe` and `viridis` were different kinds of thing.
    function test_the_panel_offers_one_kind_of_cycle_at_a_time() {
        const panel = createTemporaryObject(plotSettingsComponent, testCase,
                                            { width: Theme.railWidth, height: 700 })
        verify(panel, "the plot settings panel must instantiate")

        // Every palette Theme defines is the categorical list, and nothing
        // else is.
        const names = Theme.categoricalPaletteNames
        compare(panel.paletteKeys.length, names.length)
        for (let i = 0; i < names.length; ++i)
            compare(panel.paletteKeys[i], names[i])

        // The maps are the two the reader builds and Theme's named ramps --
        // "same" among them, because one colour for every line is `range`
        // with both ends the same and is certainly not a palette.
        verify(panel.mapKeys.indexOf("same") >= 0)
        verify(panel.mapKeys.indexOf("range") >= 0)
        verify(panel.mapKeys.indexOf("viridis") >= 0)
        for (let i = 0; i < names.length; ++i) {
            compare(panel.mapKeys.indexOf(names[i]), -1,
                    names[i] + " is a palette and must not be offered as a map")
        }
        compare(panel.mapKeys.length, panel.mapLabels.length)

        verify(panel.isPalette("spectrum"))
        verify(!panel.isPalette("viridis"))
        verify(!panel.isPalette("same"))

        // With no plot to read, the panel names the kind a plot opens on.
        verify(panel.categorical)
        compare(panel.colorModeKeys, panel.paletteKeys)
    }

    /// The kind follows the mode, and switching kinds returns the reader to
    /// what they last had in the one they asked for.
    function test_the_kind_follows_the_mode_and_remembers_each_side() {
        verify(select("/cube"))
        const view = createTemporaryObject(dataComponent, testCase, viewSize)
        waitForRendering(view)
        view.show("plot")
        const plot = findChild(view, "plotSurface")

        const panel = createTemporaryObject(plotSettingsComponent, testCase,
                                            { width: Theme.railWidth, height: 700,
                                              target: plot })
        verify(panel, "the plot settings panel must instantiate")

        // The kind is read off the mode rather than stored beside it, so it
        // cannot disagree with what the plot is drawing.
        plot.colorMode = "spectrum"
        verify(panel.categorical)
        compare(panel.colorModeKeys, panel.paletteKeys)

        plot.colorMode = "inferno"
        verify(!panel.categorical)
        compare(panel.colorModeKeys, panel.mapKeys)

        // Going to the other kind and back lands on what was last used there,
        // not on the top of a list -- and a mode set from anywhere counts,
        // which is what makes DatasetMemory's restores stick.
        panel.chooseKind(true)
        compare(plot.colorMode, "spectrum")
        panel.chooseKind(false)
        compare(plot.colorMode, "inferno")

        plot.colorMode = "safe"
        panel.chooseKind(false)
        compare(plot.colorMode, "inferno")
        panel.chooseKind(true)
        compare(plot.colorMode, "safe")

        // Asking for the kind already showing changes nothing.
        panel.chooseKind(true)
        compare(plot.colorMode, "safe")

        plot.colorMode = "spectrum"
    }

    /// That the cycle reaches the drawn lines, and not only the function that
    /// computes it. Everything this application paints is a neutral, so a
    /// coloured pixel on the plot can only have come from a series.
    function test_a_colour_cycle_reaches_the_drawn_lines() {
        verify(select("/compressed")) // 100x100

        const win = createTemporaryObject(viewWindowComponent, testCase)
        waitForRendering(win.view)
        win.view.show("plot")
        waitForRendering(win.view)
        const plot = findChild(win.view, "plotSurface")
        verify(plot, "the plot surface must be reachable")

        // Signal white on the inset: the whole plot is greys, as it has always
        // been.
        plot.colorMode = "same"
        plot.colorSingle = Theme.accent
        waitForRendering(win.view)
        // Below the bar the view acts from: see colouredPixels on what a grab
        // of an item actually contains.
        compare(colouredPixels(grabImage(plot), Theme.sliceBarHeight), 0)

        plot.colorMode = "viridis"
        waitForRendering(win.view)
        verify(colouredPixels(grabImage(plot), Theme.sliceBarHeight) > 20,
               "a ramp must put colour on the plot")

        plot.colorMode = "same"
    }

    function test_the_legend_covers_the_left_of_the_plot_when_open() {
        verify(select("/compressed"))

        const win = createTemporaryObject(viewWindowComponent, testCase)
        waitForRendering(win.view)
        win.view.show("plot")
        waitForRendering(win.view)
        const plot = findChild(win.view, "plotSurface")

        // Closed, it sits entirely off the left edge rather than merely
        // hidden, so that `contentLeft` is arithmetic over its x in both
        // states.
        verify(!plot.legendOpen)
        const before = grabImage(plot)
        const stripe = (shot) => {
            let found = 0
            for (let y = 0; y < shot.height; y += 4) {
                if (shot.pixel(2, y) === Theme.surface)
                    ++found
            }
            return found
        }
        const closed = stripe(before)

        plot.legendOpen = true
        waitForRendering(win.view)
        verify(stripe(grabImage(plot)) > closed,
               "the legend must cover the left edge of the plot when open")

        plot.legendOpen = false
    }

    /// Flipping the theme repaints the lines in the palette of the theme that
    /// is now on.
    ///
    /// This is a regression test with a real bug behind it. The colours reach
    /// a Qt Graphs series by assignment rather than by binding -- the note
    /// above restyle() says why -- so nothing re-ran when the palette under
    /// them changed. View -> Dark Theme repainted every other surface in the
    /// application and left the plot drawn in the palette of the theme the
    /// reader had just left: bright colours meant for a black ground, laid on
    /// a white one at part strength, which is a plot of pastels nobody can
    /// read. It only became possible to see when the palettes became the
    /// theme's own; the fixed ramps never had a light form to be wrong about.
    function test_the_lines_follow_the_theme_when_it_flips() {
        verify(select("/compressed")) // 100 x 100

        const win = createTemporaryObject(viewWindowComponent, testCase)
        waitForRendering(win.view)
        win.view.show("plot")
        waitForRendering(win.view)
        const plot = findChild(win.view, "plotSurface")
        verify(plot, "the plot surface must be reachable")

        const was = Theme.dark
        Theme.dark = true
        plot.colorMode = "spectrum"
        waitForRendering(win.view)

        // Nothing else is touched: the theme is the only thing that moves, and
        // it is the only thing that has to make the lines repaint.
        Theme.dark = false
        waitForRendering(win.view)

        const shot = grabImage(plot)
        const coloured = colouredPixels(shot, Theme.sliceBarHeight)
        verify(coloured > 20, "the lines must still be on the plot")

        // On a white ground the light palette is deep ink. The dark palette
        // put there instead would be pastel -- every one of its colours is
        // lighter than the ground can take -- so counting the strokes that are
        // actually dark is what separates the two cases.
        let deep = 0
        for (let x = 0; x < shot.width; x += 2) {
            for (let y = Theme.sliceBarHeight; y < shot.height; y += 2) {
                const pixel = shot.pixel(x, y)
                if (Math.max(pixel.r, pixel.g, pixel.b)
                    - Math.min(pixel.r, pixel.g, pixel.b) > 0.06
                    && luminance(pixel) < 0.5) {
                    ++deep
                }
            }
        }
        verify(deep > coloured / 4,
               "only " + deep + " of " + coloured + " coloured pixels read as "
               + "ink on the light theme's ground -- the plot is drawn in the "
               + "other theme's palette")

        Theme.dark = was
    }

    /// Pixels with a hue -- a channel spread wide enough that no neutral, and
    /// no blend of one with the ground, could account for it.
    ///
    /// `firstRow` skips the top of the grab. TestCase.grabImage() renders the
    /// item's *window* and returns the item's own size out of the top-left
    /// corner of it, so a grab of the plot surface is really the window down
    /// to the plot's height -- chrome above the plot included. Two of this
    /// system's neutrals are not perfectly achromatic (n8 spreads 16/255
    /// across its channels), so a caption drawn in one of those in the bar
    /// over the plot reads here as colour on the plot.
    function colouredPixels(shot, firstRow) {
        let found = 0
        for (let x = 0; x < shot.width; x += 2) {
            for (let y = firstRow === undefined ? 0 : firstRow;
                 y < shot.height; y += 2) {
                const pixel = shot.pixel(x, y)
                if (Math.max(pixel.r, pixel.g, pixel.b)
                    - Math.min(pixel.r, pixel.g, pixel.b) > 0.06) {
                    ++found
                }
            }
        }
        return found
    }

    /// Every dataset in the pane says what shape it is. The floor below which
    /// the readout is dropped is about how little room is worth eliding into;
    /// it used to be compared against the readout's own width instead, so a
    /// short shape like "(4 x 3)" was dropped for being *narrower* than the
    /// minimum -- which is how half the shapes in the tree went missing.
    function test_every_dataset_in_the_tree_shows_its_shape() {
        const win = createTemporaryObject(treeWindowComponent, testCase)
        verify(win, "the tree window must instantiate")
        waitForRendering(win.tree)
        settleTree(win)

        const shapes = {}
        const visit = (item) => {
            if (item.meta !== undefined && item.name !== undefined)
                shapes[item.name] = item.meta
            for (let i = 0; i < item.children.length; ++i)
                visit(item.children[i])
        }
        visit(win.tree)

        // A four-by-three matrix and a five-element vector are the short ones,
        // and they are exactly the ones that were disappearing.
        compare(shapes["matrix"], "(4 \u00d7 3)")
        compare(shapes["vec_int"], "(5)")
        compare(shapes["cube"], "(2 \u00d7 3 \u00d7 4)")
        compare(shapes["scalar_int"], "scalar")

    }

    /// The tags belong to the name, so they stand beside it. They used to be
    /// three fixed slots at the pane's right edge -- which lined them up into
    /// a column, at the price of putting a tag two hundred pixels from the
    /// thing it qualifies and spending that width on every row that had no tag
    /// to put there.
    function test_the_tree_s_tags_stand_beside_the_name() {
        const win = createTemporaryObject(treeWindowComponent, testCase)
        verify(win, "the tree window must instantiate")
        waitForRendering(win.tree)
        settleTree(win)

        // scalar_int carries one attribute and nothing else, so its row has
        // exactly one tag on it.
        const row = findTreeRow(win.tree, "scalar_int")
        verify(row, "the tree must draw a row for scalar_int")
        const marks = badgesIn(row)
        compare(marks.length, 1)
        compare(marks[0].text, "A")

        // Beside the name, not off at the edge: the tag starts where the name
        // stops, give or take the gap between them.
        const name = findText(row, "scalar_int")
        verify(name, "the row must draw its name")
        const nameEnd = name.mapToItem(row, name.contentWidth, 0).x
        const tagStart = marks[0].mapToItem(row, 0, 0).x
        verify(tagStart >= nameEnd - Theme.gapM,
               "a tag must not sit on top of the name it qualifies")
        verify(tagStart - nameEnd < Theme.s10,
               "a tag " + Math.round(tagStart - nameEnd)
               + "px past the name is a column, not a tag")

        // A row with nothing to say about itself has no tags at all, rather
        // than empty slots holding width open.
        compare(badgesIn(findTreeRow(win.tree, "matrix")).length, 0)

        // View -> Tree Tags still takes them away.
        win.tree.tagsVisible = false
        waitForRendering(win.tree)
        settleTree(win)
        compare(badgesIn(findTreeRow(win.tree, "scalar_int")).length, 0)
        win.tree.tagsVisible = true
    }

    /// Two taps in the middle of `item`, close enough together in time to be a
    /// double click.
    ///
    /// mouseDoubleClickSequence's own default delay is half a second, which is
    /// longer than the platform's double-click interval -- so the stock helper
    /// sends two single clicks and nothing under test ever sees a double one.
    function doubleClickOn(item) {
        mouseDoubleClickSequence(item, item.width / 2, item.height / 2,
                                 Qt.LeftButton, Qt.NoModifier, 20)
    }

    /// Double-clicking a group opens it, and doing it again closes it. That is
    /// what every tree on the desktop does, and it is what a reader reaches for
    /// before they find the caret at the left of the row.
    function test_double_clicking_a_group_opens_and_closes_it() {
        const win = createTemporaryObject(treeWindowComponent, testCase)
        verify(win, "the tree window must instantiate")
        waitForRendering(win.tree)
        settleTree(win)

        let row = findTreeRow(win.tree, "group")
        verify(row, "the tree must draw a row for the group")
        verify(row.hasChildren, "a group with children must say so")
        verify(!row.expanded, "the tree opens closed")

        // Well past the caret, so the press lands on the row itself rather
        // than on the one control that already toggles it.
        doubleClickOn(row)
        waitForRendering(win.tree)
        settleTree(win)
        row = findTreeRow(win.tree, "group")
        verify(row.expanded, "a double click must open a group")
        verify(findTreeRow(win.tree, "nested"),
               "...and the tree must then show what is inside it")

        doubleClickOn(row)
        waitForRendering(win.tree)
        settleTree(win)
        row = findTreeRow(win.tree, "group")
        verify(!row.expanded, "and a second one must close it again")

        // A dataset has nothing to open, and a double click on one is the two
        // selections it looks like rather than an error. (This tree is not
        // wired to the controller -- the window under test is the pane alone --
        // so what it did is read off the signal it emits.)
        const leaf = findTreeRow(win.tree, "matrix")
        verify(leaf, "the tree must draw a row for the dataset")
        verify(!leaf.hasChildren)
        const picked = []
        win.tree.objectSelected.connect(path => picked.push(path))
        doubleClickOn(leaf)
        waitForRendering(win.tree)
        settleTree(win)
        // Once, not twice: the second press of a double click is the double
        // click, and selecting the same object again on the way to it was
        // never anything the reader asked for.
        compare(picked.length, 1)
        compare(picked[0], "/matrix")
        verify(!leaf.expanded, "a dataset has nothing to expand")
    }

    /// A broken link is the one tag state a reader has to act on, so it is the
    /// one drawn in the system's crit colour rather than in its neutral one.
    function test_a_link_that_leads_nowhere_is_marked_in_red() {
        const win = createTemporaryObject(treeWindowComponent, testCase)
        waitForRendering(win.tree)
        settleTree(win)

        const broken = badgesIn(findTreeRow(win.tree, "dangling"))
        compare(broken.length, 1)
        compare(broken[0].text, "L")
        compare(String(broken[0].toneColor), String(Theme.danger))

        const sound = badgesIn(findTreeRow(win.tree, "soft_to_matrix"))
        compare(sound.length, 1)
        compare(sound[0].text, "L")
        compare(String(sound[0].toneColor), String(Theme.textSecondary))
    }

    /// The visible row whose name is `name`, or null.
    function findTreeRow(root, name) {
        const found = []
        const visit = (item) => {
            if (item.name === name && item.path !== undefined)
                found.push(item)
            for (let i = 0; i < item.children.length; ++i)
                visit(item.children[i])
        }
        visit(root)
        return found.length > 0 ? found[0] : null
    }

    /// Every visible Badge under `root`, in order. Found by what it is -- it
    /// is the only thing in a tree row carrying both a tone and a compactness.
    function badgesIn(root) {
        const found = []
        const visit = (item) => {
            if (item.toneColor !== undefined && item.compact !== undefined)
                found.push(item)
            for (let i = 0; i < item.children.length; ++i) {
                if (item.children[i].visible)
                    visit(item.children[i])
            }
        }
        if (root && root.visible)
            visit(root)
        return found
    }

    /// The first descendant of `root` that is drawing exactly `text`. A tree
    /// row holds several Texts -- the expander caret is one -- so "the first
    /// thing that can elide" is not the name.
    function findText(root, text) {
        const found = []
        const visit = (item) => {
            if (item.text === text && item.contentWidth !== undefined)
                found.push(item)
            for (let i = 0; i < item.children.length; ++i)
                visit(item.children[i])
        }
        visit(root)
        return found.length > 0 ? found[0] : null
    }

    function test_the_grid_heads_its_columns_with_index_tuples() {
        verify(select("/cube")) // 2x3x4
        const table = AppController.datasetModel
        // Two dimensions down the rows, one across: a position alone would
        // name nothing here.
        compare(table.rowCount(), 6)
        compare(table.columnCount(), 4)
        compare(table.rowLabel(3), "[1,0,_]")
        compare(table.columnLabel(2), "[_,_,2]")
        compare(table.cellLabel(3, 2), "[1,0,2]")

        const view = createTemporaryObject(dataComponent, testCase, viewSize)
        waitForRendering(view)
        compare(view.mode, "table")
    }

    /// The width setting used to move the header and leave the cells behind:
    /// TableView never re-ran its columnWidthProvider, because calling
    /// forceLayout() closed a binding loop and so was never called.
    function test_setting_the_column_width_moves_the_columns() {
        verify(select("/cube"))
        const view = createTemporaryObject(dataComponent, testCase, viewSize)
        waitForRendering(view)

        const surface = findChild(view, "tableSurface")
        verify(surface, "the table surface must be reachable")

        // A cell, not a header. The header binds the width directly and moved
        // even while this was broken, which is what made it look like it worked.
        const cell = findCell(surface)
        verify(cell, "a cell delegate must be reachable")

        surface.autoWidth = false
        surface.columnWidth = 160
        waitForRendering(view)
        compare(findCell(surface).width, 160)

        surface.columnWidth = 60
        waitForRendering(view)
        compare(findCell(surface).width, 60)
    }

    function test_a_fitted_column_is_as_wide_as_its_widest_value() {
        verify(select("/matrix")) // 4x3 float64, 0..32
        const view = createTemporaryObject(dataComponent, testCase, viewSize)
        waitForRendering(view)
        const surface = findChild(view, "tableSurface")
        verify(surface.autoWidth, "fitting to contents is the default")

        const narrow = findCell(surface).width

        // The same values written with six decimals are far wider, and the
        // column has to follow them or the reader is left with an ellipsis
        // where a number was.
        //
        // Polled rather than asserted after one frame: the measurement is
        // deferred with Qt.callLater and the layout it forces is deferred
        // again -- see ValueGrid's onColumnWidthChanged -- so the delegate is
        // two event-loop passes behind the notation that drives it.
        AppController.datasetModel.floatFormat = 1 // Fixed
        AppController.datasetModel.floatDecimals = 6
        tryVerify(() => findCell(surface).width > narrow, 2000,
                  "a wider notation must widen the column, and " + narrow
                  + " is what it was")

        AppController.datasetModel.floatFormat = 0
    }

    /// The first cell delegate of the grid inside `surface`. TableView's
    /// delegates are not children of anything nameable, so they are found by
    /// what they are: an item carrying the `display` role.
    function findCell(surface) {
        const found = []
        const visit = (item) => {
            if (item.display !== undefined && item.column !== undefined)
                found.push(item)
            for (let i = 0; i < item.children.length; ++i)
                visit(item.children[i])
        }
        visit(surface)
        return found.length > 0 ? found[0] : null
    }

    /// Cells filled by what is in them: the image's reading of a table, put
    /// back over the table itself.
    function test_cells_can_be_filled_from_their_own_value() {
        verify(select("/matrix")) // 4x3 float64, 0 .. 32
        const view = createTemporaryObject(dataComponent, testCase, viewSize)
        waitForRendering(view)
        const surface = findChild(view, "tableSurface")

        // Off to begin with. A grid of numbers is a grid of numbers until the
        // reader asks for something else.
        verify(!surface.colorCells)
        verify(!findCell(surface).filled)

        surface.colorCells = true
        waitForRendering(view)

        // The band is the whole table's extent, taken once -- not the visible
        // block's, which would repaint every cell as the reader scrolled.
        compare(surface.dataLow, 0)
        compare(surface.dataHigh, 32)
        compare(surface.colorLow, 0)
        compare(surface.colorHigh, 32)

        // Two cells at different values are two different colours...
        const first = cellAt(surface, 0, 0)
        const last = cellAt(surface, 3, 2)
        verify(first && last, "both corners of a 4x3 grid must be reachable")
        verify(first.filled && last.filled)
        verify(String(first.fill) !== String(last.fill),
               "0 and 32 must not land on the same colour")

        // ...and the fill is what the cell is actually painted in, banding
        // included: two textures over one grid is one too many, so a stripe
        // under a fill loses.
        const odd = cellAt(surface, 1, 0)
        verify(odd.filled)
        compare(String(odd.color), String(odd.fill))

        // A band the reader sets is the band the ramp is stretched between.
        // Set on the picture's object, because that is where the band lives
        // for both views; the surface reads it rather than keeping its own.
        const image = AppController.datasetImage
        image.autoRange = false
        image.rangeMinimum = 10
        image.rangeMaximum = 20
        compare(surface.colorLow, 10)
        compare(surface.colorHigh, 20)
        // Turned round is still a band: two handles, and nothing stops one
        // being dragged through the other.
        image.rangeMinimum = 20
        image.rangeMaximum = 10
        compare(surface.colorLow, 10)
        compare(surface.colorHigh, 20)
        // Outside it, a value clamps rather than running off the ramp.
        compare(String(cellAt(surface, 0, 0).fill),
                String(cellAt(surface, 0, 1).fill))

        image.autoRange = true
        surface.colorCells = false
    }

    /// Every colour any ramp can produce has to take an ink that reads on it,
    /// or the value in the cell is lost behind the colour that means it.
    function test_a_filled_cell_s_ink_reads_on_its_fill() {
        const ramps = Theme.colorRampNames
        for (let r = 0; r < ramps.length; ++r) {
            const stops = Theme.colorRamps[ramps[r]]
            for (let i = 0; i <= 20; ++i) {
                const fill = Theme.rampColor(stops, i / 20)
                const ratio = contrastRatio(fill, Theme.inkOn(fill))
                verify(ratio >= 4.5,
                       ramps[r] + " at " + (i / 20) + " reads at only "
                       + ratio.toFixed(2) + ":1")
            }
        }

        // ...and the plain black-to-white ramp, which is where both the image
        // and the table start.
        for (let shade = 0; shade <= 1.0001; shade += 0.05) {
            const fill = Qt.rgba(shade, shade, shade, 1)
            const ratio = contrastRatio(fill, Theme.inkOn(fill))
            verify(ratio >= 4.5,
                   "gray at " + shade + " reads at only "
                   + ratio.toFixed(2) + ":1")
        }
    }

    /// WCAG's contrast ratio, which is what Theme.inkOn is choosing to
    /// maximise. Written out here rather than asked of Theme, so the test is
    /// not checking the implementation against itself.
    function contrastRatio(a, b) {
        const luminance = (colour) => {
            const c = Qt.color(colour)
            const linear = v => v <= 0.03928 ? v / 12.92
                                             : Math.pow((v + 0.055) / 1.055, 2.4)
            return 0.2126 * linear(c.r) + 0.7152 * linear(c.g)
                   + 0.0722 * linear(c.b)
        }
        const first = luminance(a)
        const second = luminance(b)
        return (Math.max(first, second) + 0.05)
               / (Math.min(first, second) + 0.05)
    }

    /// The image and the table are asking the same question about the same
    /// numbers, so they ask it with the same control.
    function test_the_image_and_the_table_ask_for_a_range_the_same_way() {
        verify(select("/matrix"))
        const view = createTemporaryObject(dataComponent, testCase, viewSize)
        waitForRendering(view)
        const surface = findChild(view, "tableSurface")
        surface.colorCells = true

        const table = createTemporaryObject(tableSettingsComponent, testCase,
                                            { target: surface })
        const picture = createTemporaryObject(imageSettingsComponent, testCase)
        const inTable = findRangeSetting(table)
        const inImage = findRangeSetting(picture)
        verify(inTable, "the table settings must carry the range control")
        verify(inImage, "...and so must the image settings")

        // Both open on the data's own extent, which is what a float matrix
        // nobody called a picture is read against.
        const image = AppController.datasetImage
        verify(image.autoRange)
        compare(inTable.lower, 0)
        compare(inTable.upper, 32)

        // Typing a range in the table pins it and writes through to the
        // surface, which is what the grid draws from.
        inTable.boundsRequested(8, 24)
        compare(surface.colorAutoRange, false)
        compare(surface.colorLow, 8)
        compare(surface.colorHigh, 24)

        // ...and the picture is looking at that same band, without anyone
        // having told it. One question about one dataset: a reader who sets a
        // range on the table and switches to the image must not find it
        // stretched to its own extremes again.
        compare(image.autoRange, false)
        compare(inImage.lower, 8)
        compare(inImage.upper, 24)

        // The mirror the other way round.
        inImage.boundsRequested(4, 12)
        compare(image.rangeMinimum, 4)
        compare(image.rangeMaximum, 12)
        compare(surface.colorLow, 4)
        compare(surface.colorHigh, 12)
        compare(inTable.lower, 4)
        compare(inTable.upper, 12)

        // So is the ramp itself, its direction, and the stretch of it in use.
        // A range shared between two views that disagreed about which colours
        // it spanned would be half a mirror.
        image.rampName = "inferno"
        image.invert = true
        compare(surface.colorRamp, "inferno")
        compare(surface.colorsReversed, true)
        image.invert = false

        inTable.rampRequested(0.25, 0.75)
        compare(image.rampBegin, 0.25)
        compare(image.rampEnd, 0.75)
        compare(inImage.rampBegin, 0.25)
        compare(inImage.rampEnd, 0.75)
        image.rampBegin = 0
        image.rampEnd = 1

        image.autoRange = true
        surface.colorCells = false
    }

    /// The two halves of the control answer two questions. The handles say
    /// which colours are painted; the boxes say which values reach them.
    ///
    /// They used to be one pair of handles running over the data, which
    /// answered the second question twice and the first not at all -- a reader
    /// wanting the dark half of a ramp had no way to ask for it.
    function test_the_colour_range_and_the_value_range_are_two_questions() {
        verify(select("/matrix")) // float64, 0 … 32
        const view = createTemporaryObject(dataComponent, testCase, viewSize)
        waitForRendering(view)
        const surface = findChild(view, "tableSurface")
        surface.colorCells = true
        waitForRendering(view)

        const image = AppController.datasetImage
        image.rampName = "gray"
        image.ramp = []

        const low = cellAt(surface, 0, 0)   // 0, the bottom of the data
        const high = cellAt(surface, 3, 2)  // 32, the top of it
        verify(low && high)
        const wholeRamp = { low: String(low.fill), high: String(high.fill) }

        // Keeping the top half of the ramp lifts every cell into it: the
        // darkest is no longer black, and the brightest is still the top.
        image.rampBegin = 0.5
        waitForRendering(view)
        verify(String(low.fill) !== wholeRamp.low,
               "the bottom of the data must move off the bottom of the ramp")
        compare(String(high.fill), wholeRamp.high)

        // The value range is the other question, and moves the values rather
        // than the colours: with the ramp still halved, a range that stops at
        // 16 puts the top half of the data at the top of what is kept.
        image.rampBegin = 0
        image.autoRange = false
        image.rangeMinimum = 0
        image.rangeMaximum = 16
        waitForRendering(view)
        compare(String(cellAt(surface, 2, 0).fill), wholeRamp.high,
               "20 is past the top of a 0-16 range and clamps to the last colour")

        image.autoRange = true
        image.rampBegin = 0
        image.rampEnd = 1
        surface.colorCells = false
    }

    /// A dataset of integers is read against whole numbers. A value range of
    /// 0.5 is not a range a grid of them can be read against, and a box
    /// offering one is offering a value the data cannot take.
    function test_the_value_boxes_follow_the_datatype() {
        const view = createTemporaryObject(dataComponent, testCase, viewSize)
        waitForRendering(view)
        const surface = findChild(view, "tableSurface")

        verify(select("/matrix")) // float64
        const table = createTemporaryObject(tableSettingsComponent, testCase,
                                            { target: surface })
        const range = findRangeSetting(table)
        verify(range, "the table settings must carry the range control")
        verify(!range.integer, "a float dataset takes a fractional range")

        verify(select("/cube")) // int32
        verify(range.integer, "an integer dataset does not")
    }

    /// One grayscale, not two. The image and table offer the plain
    /// black-to-white ramp under the name "grayscale"; the plot's list has a
    /// ramp of that name too, and it is a *different* ramp -- it starts at n5
    /// so a line never falls into the plot's own ground. Concatenating the two
    /// lists put "grayscale" in the dropdown twice.
    function test_the_ramp_list_names_grayscale_once() {
        compare(Theme.valueRampKeys.length, Theme.valueRampLabels.length)
        compare(Theme.valueRampKeys[0], "gray")
        compare(Theme.valueRampLabels[0], "grayscale")

        const seen = {}
        for (const label of Theme.valueRampLabels) {
            verify(seen[label] === undefined,
                   "\"" + label + "\" is offered twice")
            seen[label] = true
        }

        // The plot keeps its own, and the two lists differ by exactly that one.
        verify(Theme.colorRampNames.indexOf("grayscale") >= 0)
        verify(Theme.valueRampKeys.indexOf("grayscale") < 0)
        compare(Theme.valueRampKeys.length, Theme.colorRampNames.length)
    }

    /// The shared range control, wherever it is in a panel. Found by what it
    /// is rather than by a name, exactly as findCell does.
    function findRangeSetting(root) {
        const found = []
        const visit = (item) => {
            if (item.rampBegin !== undefined && item.lower !== undefined)
                found.push(item)
            for (let i = 0; i < item.children.length; ++i)
                visit(item.children[i])
        }
        visit(root)
        return found.length > 0 ? found[0] : null
    }

    /// The cell delegate at (row, column), or null.
    function cellAt(surface, row, column) {
        const found = []
        const visit = (item) => {
            if (item.display !== undefined && item.column !== undefined
                && item.row === row && item.column === column)
                found.push(item)
            for (let i = 0; i < item.children.length; ++i)
                visit(item.children[i])
        }
        visit(surface)
        return found.length > 0 ? found[0] : null
    }

    function test_the_index_slider_scrubs_through_the_planes() {
        // The whole point of Index mode is moving through a dimension one
        // plane at a time; a slider that only reports the number the box
        // already shows would be decoration.
        verify(select("/hypercube")) // 2x3x4x5
        const setup = AppController.tableSetupModel
        setup.setMode(2, TableSetupModel.Index) // extent 4, so indices 0..3
        setup.setIndex(2, 0)

        const win = createTemporaryObject(panelWindowComponent, testCase)
        waitForRendering(win.panel)

        const slider = findChild(win.panel, "indexSlider2")
        verify(slider, "index mode must offer a slider")
        verify(slider.enabled, "and it must be draggable")
        compare(slider.to, 3)
        compare(slider.value, 0)

        // Drag past the far end: it clamps rather than running off.
        mouseDrag(slider, slider.width / 2, slider.height / 2, slider.width, 0)
        compare(setup.data(setup.index(2, 0), TableSetupModel.IndexValueRole), 3)
        compare(slider.value, 3, "the handle follows what the model stored")
        compare(AppController.sliceExpression, "/hypercube[:, :, 3, :]")

        // The model is the authority: a value set anywhere else moves the
        // handle back, which the drag's own write to it must not have broken.
        setup.setIndex(2, 1)
        compare(slider.value, 1)
    }

    function test_a_custom_expression_reports_where_it_went_wrong() {
        verify(select("/cube"))
        const setup = AppController.tableSetupModel
        const row = setup.index(2, 0)

        setup.setMode(2, TableSetupModel.Custom)
        compare(setup.data(row, TableSetupModel.ExpressionErrorRole), "")

        setup.setExpression(2, "0,99")
        verify(setup.data(row, TableSetupModel.ExpressionErrorRole) !== "",
               "an out-of-range index must be reported, not swallowed")
        // The grid keeps the last good selection rather than blanking.
        compare(AppController.datasetModel.columnCount(), 4)

        setup.setExpression(2, "0,3")
        compare(setup.data(row, TableSetupModel.ExpressionErrorRole), "")
        compare(AppController.datasetModel.columnCount(), 2)
        compare(AppController.sliceExpression, "/cube[:, :, [0,3]]")
    }

    function test_file_picker_is_the_application_s_own() {
        // Not QtQuick.Dialogs.FileDialog: a native dialog arrives in the
        // host's palette and typeface, and this one has to be instantiable
        // here at all, which a platform dialog is not.
        const picker = createTemporaryObject(pickerComponent, testCase)
        verify(picker, "FilePicker must instantiate")
        verify(String(picker.folder).length > 0, "it must start somewhere")
        compare(picker.selectedPath, "")
    }

    function test_attributes_surface_as_an_info_panel() {
        // The Metadata tab is gone; its rows are the Information tab's last
        // panel now, spliced in by AppController.
        verify(select("/group"))
        compare(AppController.attributeModel.rowCount(), 2)

        const panels = AppController.infoPanels
        const attributes = panels.filter(p => p.title === "attributes")
        compare(attributes.length, 1)
        compare(attributes[0].rows.length, 2)
    }

    function test_info_panels_describe_a_dataset() {
        verify(select("/matrix"))
        const titles = AppController.infoPanels.map(p => p.title)
        verify(titles.indexOf("object") !== -1)
        verify(titles.indexOf("dataspace") !== -1)
        verify(titles.indexOf("datatype") !== -1)
        verify(titles.indexOf("storage") !== -1)
        // Exactly one panel carries the accent rule.
        compare(AppController.infoPanels.filter(p => p.accent).length, 1)
    }

    function test_tree_view_instantiates() {
        const view = createTemporaryObject(treeComponent, testCase)
        verify(view, "ObjectTree must instantiate")
        verify(AppController.treeModel.rowCount() > 0)
    }

    function test_fonts_come_from_the_binary() {
        // The design system's faces must be the ones compiled in, not whatever
        // the host has installed -- a font is as much a system dependency as a
        // shared library, and this binary is meant to carry both itself.
        compare(EmbeddedFonts.missing.length, 0,
                "bundled fonts failed to load: " + EmbeddedFonts.missing.join(", "))
        verify(EmbeddedFonts.families.indexOf("IBM Plex Sans") !== -1,
               "IBM Plex Sans must be registered from the bundled file")
        verify(EmbeddedFonts.families.indexOf("IBM Plex Mono") !== -1,
               "IBM Plex Mono must be registered from the bundled file")

        // ...and the Theme must actually ask for them first.
        compare(Theme.sansFamilies[0], "IBM Plex Sans")
        compare(Theme.monoFamilies[0], "IBM Plex Mono")
    }

    function test_the_icon_comes_from_the_binary_at_every_size() {
        // Same argument as the fonts above. On Linux there is nowhere but the
        // binary for this to be: a Windows executable has a resource slot and
        // an AppImage has an AppDir, and an ELF has neither, so a bare
        // ./H5Scope took whatever the desktop draws for a program it has never
        // heard of.
        const sizes = EmbeddedIcon.sizes
        const wanted = ["16x16", "24x24", "32x32", "48x48", "64x64",
                        "128x128", "256x256"]
        for (const size of wanted) {
            verify(sizes.indexOf(size) !== -1,
                   "the icon must carry a " + size + " render; it has ["
                   + sizes.join(", ") + "]")
        }

        // Seven entries and not one. A QIcon holding only the 256 still draws
        // at every size -- by resampling it -- so the failure this guards
        // against looks exactly like success until the icon is 16 pixels wide
        // and its grid has turned to haze. tools/make-icons.sh draws each of
        // these from the vector; this is what says they all arrived.
        compare(sizes.length, wanted.length,
                "unexpected icon sizes: [" + sizes.join(", ") + "]")
    }

    function test_the_menu_bar_carries_a_file_menu() {
        // design.txt asks for a proper menu rather than a strip of buttons, so
        // what has to hold is that these are real Menus with real Actions --
        // an AppToolButton wearing a menu's clothes would pass a screenshot
        // and fail every one of these assertions.
        const bar = createTemporaryObject(menuBarComponent, testCase)
        verify(bar, "AppMenuBar must instantiate")

        compare(bar.menuCount, 4)
        compare(bar.menus.menuAt(0).title, "File")
        compare(bar.menus.menuAt(1).title, "View")
        compare(bar.menus.menuAt(2).title, "Settings")
        compare(bar.menus.menuAt(3).title, "Help")

        // Settings is its own drawer rather than more of View, because it is
        // the one that is not about the file or about what is drawn from it.
        // Its single submenu says how much of the machine the plots may spend
        // holding what they have read, and the bullet follows the controller's
        // property rather than the row's own `checked`.
        const settings = bar.menus.menuAt(2)
        compare(settings.count, 1)
        const budget = settings.menuAt(0)
        verify(budget, "RAM Budget must be a real submenu")
        compare(budget.title, "RAM Budget")
        compare(budget.count, 3)
        compare(budget.itemAt(0).text, "Low")
        compare(budget.itemAt(1).text, "Medium")
        compare(budget.itemAt(2).text, "Greedy")

        // Open, Open Recent, Reload, Close, a rule, Quit.
        const file = bar.menus.menuAt(0)
        compare(file.count, 6)
        compare(file.itemAt(0).text, "Open\u2026")
        compare(file.itemAt(1).text, "Open Recent")
        compare(file.itemAt(2).text, "Reload")
        compare(file.itemAt(3).text, "Close")
        compare(file.itemAt(5).text, "Quit")

        // The recent list is a submenu and not a row: ten paths would be most
        // of the File drawer, and what a reader wants there most of the time is
        // one of the four verbs around it. It carries "Clear Recent" whether or
        // not there is anything to clear, so the row that erases the record is
        // in the same place every time.
        const recent = file.menuAt(1)
        verify(recent, "Open Recent must be a real submenu")
        compare(recent.title, "Open Recent")
        verify(recent.count >= 2, "a separator and Clear Recent at least")
        compare(recent.itemAt(recent.count - 1).text, "Clear Recent")

        // The row that opens it is created by Qt from AppMenu's delegate, not
        // declared, and that delegate has to be this application's own: the
        // Basic style's MenuItem arrives in Qt's palette on a gutter of its
        // own, which on this drawer reads as a row that has been disabled.
        const recentRow = file.itemAt(1)
        verify(recentRow.opensSubMenu !== undefined,
               "a submenu's row must be an AppMenuItem, not the Basic style's")
        verify(recentRow.opensSubMenu, "...and must know that it opens a drawer")

        // ...and it follows the list. A fixture file has been opened, so there
        // is something to open again, and the row must say so.
        verify(AppController.recentFiles.length > 0,
               "opening the fixture must have been remembered")
        verify(recentRow.enabled,
               "Open Recent must be reachable when there are files in it")

        // Every row states its shortcut, and states the one it actually binds:
        // AppMenuItem reads the string back off the Action rather than being
        // given a second copy that could drift from it.
        compare(file.itemAt(0).shortcutText, "Ctrl+O")
        compare(file.itemAt(0).action.shortcut, "Ctrl+O")
        compare(file.itemAt(2).shortcutText, "Ctrl+R")
        compare(file.itemAt(5).shortcutText, "Ctrl+Q")


        // ...and triggering one reaches the window rather than stopping here.
        menuSpy.target = bar
        menuSpy.signalName = "openRequested"
        compare(menuSpy.count, 0)
        file.itemAt(0).action.trigger()
        compare(menuSpy.count, 1)
        menuSpy.clear()
        menuSpy.target = null
    }

    /// ...and states the whole of it.
    ///
    /// A drawer sized from a Text that elides settles a fraction of a pixel
    /// narrower than the string it is measuring, and "Ctrl+R" comes out as
    /// "...rl+R" -- which is why AppMenuItem measures with TextMetrics and
    /// AppMenu asks its rows how wide they want to be. Nothing in a menu is
    /// long enough to be worth eliding, so nothing in one may.
    function test_no_menu_row_loses_a_character_to_its_own_width() {
        const win = createTemporaryObject(menuWindowComponent, testCase)
        verify(waitForRendering(win.contentItem))
        const bar = win.bar

        for (let m = 0; m < bar.menuCount; ++m) {
            const drawer = bar.menus.menuAt(m)
            drawer.popup()
            waitForRendering(win.contentItem)

            verify(drawer.width >= Theme.menuMinWidth,
                   drawer.title + " must be at least the design's floor wide")
            let rows = 0
            for (let i = 0; i < drawer.count; ++i) {
                const row = drawer.itemAt(i)
                if (!row || row.text === undefined || row.text === "")
                    continue
                ++rows
                verify(row.width > 0, "a row must have been laid out")
                const cut = truncatedTexts(row)
                compare(cut, [],
                        drawer.title + " / " + row.text + " loses: " + cut)
            }
            verify(rows > 0, drawer.title + " must have drawn its rows")
            drawer.close()
            waitForRendering(win.contentItem)
        }
    }

    /// The strings inside `item` that are being drawn with an ellipsis.
    function truncatedTexts(item) {
        const cut = []
        const visit = (it) => {
            if (it.truncated === true && it.text !== "")
                cut.push(it.text)
            for (let i = 0; i < it.children.length; ++i)
                visit(it.children[i])
        }
        visit(item)
        return cut
    }

    function test_the_view_menu_marks_what_is_current() {
        const bar = createTemporaryObject(menuBarComponent, testCase)
        const view = bar.menus.menuAt(1)

        // One row per tab, in the strip's own order -- Information, Table,
        // Plot, Image -- then any custom plots, a rule, New Custom Plot, a
        // rule, Expand, Collapse, Tree Tags, a rule, Dark. There are no custom
        // plots here: init() re-opens the file, and that empties them.
        compare(view.count, 12)
        compare(view.itemAt(0).text, "Information")
        compare(view.itemAt(1).text, "Table")
        compare(view.itemAt(2).text, "Plot")
        compare(view.itemAt(3).text, "Image")
        compare(view.itemAt(5).text, "New Custom Plot")

        // The mark is a bullet the system draws in place of a checkmark, and
        // it follows the window rather than the row's own checked state --
        // which triggering the row would otherwise overwrite.
        const tabs = ["info", "table", "plot", "image"]
        for (let current = 0; current < tabs.length; ++current) {
            bar.currentTabId = tabs[current]
            for (let row = 0; row < tabs.length; ++row) {
                compare(view.itemAt(row).marked, row === current,
                        tabs[current] + " must mark row " + current + " alone")
            }
        }

        // The tag column's toggle is marked the same way, off the window's
        // own state rather than off the row.
        const tags = view.itemAt(9)
        compare(tags.text, "Tree Tags")
        bar.treeTagsVisible = true
        verify(tags.marked)
        bar.treeTagsVisible = false
        verify(!tags.marked)

        // Same for the theme toggle, which tracks the Theme singleton itself.
        const dark = view.itemAt(11)
        compare(dark.text, "Dark Theme")
        compare(dark.marked, Theme.dark)
    }

    function test_the_filter_lives_at_the_bottom_of_the_tree() {
        // design.txt moves the search box out of the chrome and into the tree.
        const win = createTemporaryObject(treeWindowComponent, testCase)
        verify(win, "the window must instantiate")
        waitForRendering(win.tree)
        settleTree(win)

        const filter = findChild(win.tree, "treeFilter")
        verify(filter, "the tree must carry its own filter")

        // At the foot of the pane, not the head of it: below the midpoint, and
        // with its bottom edge on the pane's.
        const top = filter.mapToItem(win.tree, 0, 0).y
        verify(top > win.tree.height / 2,
               "the filter must sit at the bottom of the tree, not the top")
        verify(top + filter.height <= win.tree.height)

        // ...and it is still the controller's filter, wired the way the action
        // bar had it.
        AppController.filterText = "matrix"
        compare(filter.text, "matrix")
        AppController.filterText = ""
        compare(filter.text, "")
    }

    /// Type into the tree's filter box a key at a time, the way a reader does.
    /// Assigning to AppController.filterText would skip the box, and the box is
    /// where the pane writes down what was open before the search.
    function typeIntoFilter(win, text) {
        const filter = findChild(win.tree, "treeFilter")
        verify(filter, "the tree must carry its own filter")
        filter.forceActiveFocus()
        for (let i = 0; i < text.length; ++i) {
            keyClick(text[i])
        }
        tryVerify(() => AppController.filterText === text, 5000,
                  "what was typed must reach the controller")
        return filter
    }

    /// Backspace over whatever is in the box, one key at a time.
    function clearFilter() {
        for (let i = AppController.filterText.length; i > 0; --i) {
            keyClick(Qt.Key_Backspace)
        }
        tryVerify(() => AppController.filterText === "", 5000,
                  "the box must empty")
    }

    /// A tree with `/group/nested/leaf` read but nothing open: the filter can
    /// see two levels down, and none of it is on screen.
    function treeWithGroupRead() {
        const win = createTemporaryObject(treeWindowComponent, testCase)
        verify(win, "the window must instantiate")
        waitForRendering(win.tree)
        settleTree(win)

        // Reading is a round trip per level, so this takes more than one pass.
        for (let pass = 0; pass < 4; ++pass) {
            win.tree.expandToDepth(2)
            settleTree(win)
        }
        verify(AppController.filteredTreeModel.indexForPath("/group/nested/leaf").valid,
               "the fixture's nested group must have been read")
        win.tree.collapseAll()
        waitForRendering(win.tree)
        return win
    }

    function test_the_filter_opens_the_tree_to_what_it_found() {
        const win = treeWithGroupRead()
        const view = findChild(win.tree, "objectTreeView")
        const model = AppController.filteredTreeModel

        const leaf = model.indexForPath("/group/nested/leaf")
        compare(view.rowAtIndex(leaf), -1,
                "nothing is open, so the leaf starts off screen")

        typeIntoFilter(win, "leaf")
        tryVerify(() => view.rowAtIndex(leaf) >= 0, 5000,
                  "the filter must open the tree far enough to show what it found")

        // The branches above it, and only those: a hit is a result, not a
        // request for its contents.
        verify(view.isExpanded(view.rowAtIndex(model.indexForPath("/group"))))
        verify(view.isExpanded(view.rowAtIndex(model.indexForPath("/group/nested"))))

        clearFilter()
    }

    function test_a_name_in_a_branch_nobody_opened_is_still_found() {
        // No treeWithGroupRead() here, deliberately: nothing below the root has
        // been listed. The filter used to match what the reader had expanded,
        // so a search for a name three levels down found nothing at all until
        // they had walked to it by hand. It is answered out of the name index
        // now, and the branch on the way is opened for them.
        const win = createTemporaryObject(treeWindowComponent, testCase)
        verify(win, "the window must instantiate")
        waitForRendering(win.tree)
        settleTree(win)
        win.tree.collapseAll()
        waitForRendering(win.tree)

        const view = findChild(win.tree, "objectTreeView")
        const model = AppController.filteredTreeModel

        typeIntoFilter(win, "leaf")
        tryVerify(() => model.indexForPath("/group/nested/leaf").valid, 5000,
                  "the search must reach a name nobody had expanded the way to")
        tryVerify(() => view.rowAtIndex(model.indexForPath("/group/nested/leaf")) >= 0,
                  5000, "...and the tree must be opened to it")
        compare(model.matchCount, 1, "one name in the file is called leaf")

        clearFilter()
    }

    function test_the_box_says_how_much_of_the_file_it_found() {
        const win = createTemporaryObject(treeWindowComponent, testCase)
        verify(win, "the window must instantiate")
        waitForRendering(win.tree)
        settleTree(win)

        const filter = typeIntoFilter(win, "str_")
        tryVerify(() => filter.hint !== "", 5000,
                  "the box must report what the filter took")
        // str_fixed, str_vlen, str_scalar, str_grid -- counted over the file
        // rather than over what happens to be on screen.
        compare(AppController.filteredTreeModel.matchCount, 4)
        compare(filter.hint, "4 matches")

        clearFilter()
        compare(filter.hint, "", "an empty box reports nothing")
    }

    function test_a_matched_group_is_not_poured_out() {
        const win = treeWithGroupRead()
        const view = findChild(win.tree, "objectTreeView")
        const model = AppController.filteredTreeModel

        // Everything under `/group` matches `group`, because the filter reads
        // paths. Opening it would answer a search for the group with its whole
        // contents, when the row the reader wanted is the group itself.
        typeIntoFilter(win, "group")
        const row = view.rowAtIndex(model.indexForPath("/group"))
        verify(row >= 0, "the group itself must be on screen")
        verify(!view.isExpanded(row), "...and must be left closed")

        clearFilter()
    }

    function test_clearing_the_filter_puts_the_reader_s_branches_back() {
        const win = treeWithGroupRead()
        const view = findChild(win.tree, "objectTreeView")
        const model = AppController.filteredTreeModel

        // One branch open, chosen by the reader.
        view.expand(view.rowAtIndex(model.indexForPath("/group")))
        waitForRendering(win.tree)
        verify(view.isExpanded(view.rowAtIndex(model.indexForPath("/group"))))

        typeIntoFilter(win, "leaf")
        tryVerify(() => view.isExpanded(
                      view.rowAtIndex(model.indexForPath("/group/nested"))),
                  5000, "the search must open the way to the leaf")

        clearFilter()
        waitForRendering(win.tree)

        // What the search opened is closed again; what the reader opened is not.
        verify(view.isExpanded(view.rowAtIndex(model.indexForPath("/group"))),
               "the reader's own branch must survive the search")
        verify(!view.isExpanded(view.rowAtIndex(model.indexForPath("/group/nested"))),
               "...and the search's must not")

        win.tree.collapseAll()
    }

    function test_the_filter_marks_the_letters_it_matched() {
        const win = createTemporaryObject(treeWindowComponent, testCase)
        verify(win, "the window must instantiate")
        waitForRendering(win.tree)
        settleTree(win)

        const view = findChild(win.tree, "objectTreeView")
        const model = AppController.filteredTreeModel

        typeIntoFilter(win, "trix")
        waitForRendering(win.tree)

        const row = view.itemAtIndex(model.indexForPath("/matrix"))
        verify(row, "the matched row must be on screen")
        compare(row.mark.start, 2, "`trix` begins two characters into `matrix`")
        compare(row.mark.length, 4)

        const mark = findChild(row, "matchMark")
        verify(mark, "the row must carry a mark for what was matched")
        verify(mark.visible, "...and it must be drawn")
        verify(mark.width > 0)

        // It stands over the tail of the name, not over the whole of it.
        verify(mark.x > 0, "the mark must start where the match does")

        clearFilter()
        waitForRendering(win.tree)
        verify(!findChild(view.itemAtIndex(model.indexForPath("/matrix")),
                          "matchMark").visible,
               "an empty box marks nothing")
    }

    function test_theme_tokens_are_defined() {
        // The Theme singleton is the single source of truth for the look; if
        // a token disappears every view silently loses its styling.
        verify(Theme.background !== undefined)
        verify(Theme.accent !== undefined)
        verify(Theme.radiusM > 0)
        verify(Theme.rowHeight > 0)
        verify(Theme.menuBarHeight > 0)
        verify(Theme.tinyControlHeight > 0)
    }
}
