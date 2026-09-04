// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import QtTest
import H5Scope
import H5Scope.Backend

/// Exercises the real QML bindings, not just the C++ beneath them: these
/// catch a tab that stops appearing or a binding that stops updating, which
/// the C++ suite cannot see.
TestCase {
    id: testCase
    name: "TabVisibility"
    when: windowShown

    // Injected by the test harness (see tst_qml.cpp).
    readonly property string fixture: TestFixture.path

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

    function initTestCase() {
        openFixture()
    }

    function cleanupTestCase() {
        AppController.closeFile()
    }

    function test_dataset_with_attributes_shows_three_tabs() {
        verify(select("/scalar_int"))
        verify(AppController.datasetTabVisible)
        verify(AppController.metadataTabVisible)
    }

    function test_dataset_without_attributes_hides_metadata() {
        verify(select("/matrix"))
        verify(AppController.datasetTabVisible)
        verify(!AppController.metadataTabVisible)
    }

    function test_group_with_attributes_hides_dataset() {
        verify(select("/group"))
        verify(!AppController.datasetTabVisible)
        verify(AppController.metadataTabVisible)
    }

    function test_plain_group_shows_only_information() {
        verify(select("/group/nested"))
        verify(!AppController.datasetTabVisible)
        verify(!AppController.metadataTabVisible)
    }

    function test_rank_drives_the_table_setup_panel() {
        verify(select("/cube"))
        compare(AppController.datasetRank, 3)
        // One row per dimension, each knowing how far it goes.
        const setup = AppController.tableSetupModel
        compare(setup.rowCount(), 3)
        compare(setup.data(setup.index(0, 0), TableSetupModel.ExtentRole), 2)

        verify(select("/matrix"))
        compare(AppController.datasetRank, 2)
        compare(AppController.tableSetupModel.rowCount(), 2)

        // A scalar has nothing to set up, and the panel says so by being empty.
        verify(select("/scalar_int"))
        compare(AppController.datasetRank, 0)
        compare(AppController.tableSetupModel.rowCount(), 0)
    }
}
