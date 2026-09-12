// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import QtQuick.Controls.Basic
import H5Scope.Backend

/// The saved views, offered as a drawer.
///
/// The rows are built by an Instantiator over the names rather than declared,
/// which is the pattern the File menu's Open Recent already uses: a drawer
/// whose rows come from a list has to insert and remove them as the list
/// changes, and Menu has insertItem and removeItem for exactly that.
///
/// Each row carries a dot in the mark gutter saying how much of the open file
/// that view can still draw -- green for all of it, amber for some, red for
/// none -- and the list is ordered by that before it is ordered by name.
/// Nothing is hidden: a view that fits nothing at all is still the fastest way
/// back to an arrangement the reader built, and they are the ones who know
/// whether this is the right file for it.
AppMenu {
    id: menu

    /// Which view was chosen.
    signal picked(string name)

    // Greyed rather than empty when there are none, so the reader learns the
    // action exists and that they have not saved anything to use it on.
    enabled: AppController.customPlots.viewNames.length > 0

    Instantiator {
        model: AppController.customPlots.viewNames

        onObjectAdded: (index, object) => menu.insertItem(index, object)
        onObjectRemoved: (index, object) => menu.removeItem(object)

        delegate: AppMenuItem {
            id: viewRow

            required property string modelData

            /// Recomputed whenever the list is announced, which is whenever a
            /// file is opened and the states are worked out again. A function
            /// call is not something a binding can see through on its own.
            readonly property int state:
                AppController.customPlots.viewNames.length >= 0
                ? AppController.customPlots.stateOf(viewRow.modelData) : 0

            text: viewRow.modelData
            marked: true
            markInk: Theme.matchColor(viewRow.state, viewRow.highlighted)
            onTriggered: menu.picked(viewRow.modelData)
        }
    }
}
