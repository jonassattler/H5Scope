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

    function initTestCase() {
        verify(AppController.openFile(fixture), "fixture must open")
    }

    function cleanupTestCase() {
        AppController.closeFile()
    }

    function test_dataset_with_attributes_shows_three_tabs() {
        verify(AppController.selectPath("/scalar_int"))
        verify(AppController.datasetTabVisible)
        verify(AppController.metadataTabVisible)
    }

    function test_dataset_without_attributes_hides_metadata() {
        verify(AppController.selectPath("/matrix"))
        verify(AppController.datasetTabVisible)
        verify(!AppController.metadataTabVisible)
    }

    function test_group_with_attributes_hides_dataset() {
        verify(AppController.selectPath("/group"))
        verify(!AppController.datasetTabVisible)
        verify(AppController.metadataTabVisible)
    }

    function test_plain_group_shows_only_information() {
        verify(AppController.selectPath("/group/nested"))
        verify(!AppController.datasetTabVisible)
        verify(!AppController.metadataTabVisible)
    }

    function test_rank_drives_the_table_setup_panel() {
        verify(AppController.selectPath("/cube"))
        compare(AppController.datasetRank, 3)
        // One row per dimension, each knowing how far it goes.
        const setup = AppController.tableSetupModel
        compare(setup.rowCount(), 3)
        compare(setup.data(setup.index(0, 0), TableSetupModel.ExtentRole), 2)

        verify(AppController.selectPath("/matrix"))
        compare(AppController.datasetRank, 2)
        compare(AppController.tableSetupModel.rowCount(), 2)

        // A scalar has nothing to set up, and the panel says so by being empty.
        verify(AppController.selectPath("/scalar_int"))
        compare(AppController.datasetRank, 0)
        compare(AppController.tableSetupModel.rowCount(), 0)
    }
}
