// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import QtQuick.Controls.Basic
import H5Scope.Backend

/// A submenu listing the custom plots by name, for the three places that offer
/// to put something into one.
///
/// The rows are built by an Instantiator over the set rather than declared,
/// which is the pattern the File menu's Open Recent already uses: a menu whose
/// rows come from a model has to insert and remove them as the model changes,
/// and Menu has insertItem and removeItem for exactly that.
///
/// It reports the *position* rather than the plot, because every invokable
/// that acts on a whole tab takes one -- and because a menu holding a pointer
/// to an object the reader can close while the menu is open is a menu holding
/// a dangling pointer.
AppMenu {
    id: menu

    /// Which plot was chosen.
    signal picked(int index)

    // Greyed rather than empty when there are none. The reader learns that the
    // action exists and that they have not made a plot to use it on, which an
    // absent row does not say.
    enabled: AppController.customPlots.count > 0

    Instantiator {
        model: AppController.customPlots

        onObjectAdded: (index, object) => menu.insertItem(index, object)
        onObjectRemoved: (index, object) => menu.removeItem(object)

        delegate: AppMenuItem {
            required property int index
            required property string name

            text: name
            onTriggered: menu.picked(index)
        }
    }
}
