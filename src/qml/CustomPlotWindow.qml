// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import QtQuick.Window
import H5Scope.Backend

/// A custom plot in a window of its own.
///
/// The first second window this application has had. Every other thing that
/// looks like one -- the file picker, the About panel, the error box -- is an
/// in-scene modal Dialog, because each of those is a question the reader has to
/// answer before going on. This is the opposite: a plot torn off precisely so
/// that the reader can go on looking at the file while it stays open beside
/// them, which a modal cannot do and an in-scene panel cannot do either.
///
/// The tab *leaves* the strip while this is up and comes back when it closes.
/// One plot lives in one place: two views of one object would make every rail,
/// every zoom and every highlight a question about which of them is the real
/// one, for the sake of a second copy of a picture the reader already has.
///
/// It carries its own FocusRelease. That helper watches one window for presses
/// that land outside a text field -- see its header for why it cannot be done
/// from QML -- and the one the main window owns watches the main window.
/// Without this the name box and the entry boxes in here would keep the caret
/// after the reader pressed somewhere else.
///
/// Theme is a singleton, so the palette and the typefaces are shared for free,
/// `Theme.dark` included. `Theme.pixelRatio` is not: the main window binds it
/// from its own screen, and this window dragged to a monitor at another scale
/// will snap its hairlines to that screen's grid rather than to its own. It is
/// a rule the design system draws and not a correctness problem, and fixing it
/// means making the token per-window, which every file that reads it would
/// have to learn about.
Window {
    id: window

    /// The plot this holds, and its place in the set.
    property var plot
    property int plotIndex: -1

    width: 1000
    height: 700
    minimumWidth: Math.ceil(view.barMinimumWidth)
    minimumHeight: 320
    color: Theme.background

    // The plot's name first: on a desktop showing four of these, the name is
    // what tells them apart and the file is the same in all of them.
    title: window.plot
           ? qsTr("%1 — H5Scope").arg(window.plot.name)
           : qsTr("H5Scope")

    // Closing it puts the tab back rather than throwing the plot away. The
    // window is where it is being *shown*, not where it lives.
    onClosing: {
        if (window.plotIndex >= 0)
            AppController.customPlots.setDetached(window.plotIndex, false)
    }

    FocusRelease {
        window: window
    }

    CustomView {
        id: view

        objectName: "customWindowView"

        anchors.fill: parent
        plot: window.plot
        plotIndex: window.plotIndex
        detached: true
        // Its own window, so it is on screen whenever the window is. Nothing
        // else can be in front of it here.
        active: window.visible
    }
}
