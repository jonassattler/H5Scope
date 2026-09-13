// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick

/// The bar along the bottom of each of the data views: what is on screen,
/// stated in numbers.
///
///     4 rows · 3 cols · min 0.000 · max 32.00
///
/// There is one of these per view rather than one for the window, because what
/// is worth saying about a grid of cells, a bundle of lines and a raster is not
/// the same thing.
///
/// It carries no controls. Everything that *acts* on the view -- the slice, the
/// two settings panels, the plot's legend -- sits in the bar above it, which is
/// the arrangement the design asks for and also the more honest one: a strip
/// that both reports and acts reads as neither.
///
/// The design puts these numbers in the window's own status strip. They are
/// not there here because that strip is already spoken for: it carries the
/// file, the path and the datatype -- what the *selection* is, ambient and
/// unchanging while the reader works -- where these say what this view is
/// drawing right now, and change as it is zoomed, turned and resliced.
Rectangle {
    id: footer

    /// Segments of the readout, joined by the system's mono separator.
    property var facts: []

    /// Segments pinned to the right-hand end of the bar.
    ///
    /// One bar with two ends, because the two kinds of fact in it answer to
    /// different things. What is on the left describes the *view* -- how many
    /// lines, how many points, what the window is -- and changes when the
    /// reader arranges something. What is on the right is the reading under the
    /// pointer, and changes with every pixel the pointer moves. Appended to one
    /// run they pushed each other about: the left-hand facts shifted as the
    /// reading grew a digit, so the numbers a reader was watching moved while
    /// they were watching them. At opposite ends neither disturbs the other.
    property var trailingFacts: []

    implicitHeight: Theme.statusBarHeight
    color: Theme.background

    Rectangle {
        anchors.top: parent.top
        width: parent.width
        height: Theme.borderWidth
        color: Theme.border
    }

    Text {
        id: readout

        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        // Up to the reading, never under it. The reading is the shorter run and
        // the one whose width changes as the pointer moves, so it keeps its
        // room and this gives way -- a footer that elided the reading would be
        // eliding the only fact on the bar that is about where the reader is
        // pointing right now.
        anchors.right: trailing.visible ? trailing.left : parent.right
        anchors.leftMargin: Theme.gapM
        anchors.rightMargin: Theme.gapM
        // `·` is the system's mono metadata separator, as in the status
        // strip along the foot of the window.
        text: footer.facts.join("  ·  ")
        font: Theme.microLabel
        color: Theme.textDisabled
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter

        HoverHandler { id: readoutHover }

        // A narrow window elides the readout from the right, which is where
        // the most specific facts are -- the range on screen, the zoom.
        AppToolTip {
            shown: readout.truncated && readoutHover.hovered
            verbatim: true
            text: footer.facts.join("   ")
        }
    }

    Text {
        id: trailing

        objectName: "footerTrailing"

        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.rightMargin: Theme.gapM
        visible: footer.trailingFacts.length > 0
        text: footer.trailingFacts.join("  ·  ")
        font: Theme.microLabel
        color: Theme.textDisabled
        horizontalAlignment: Text.AlignRight
        verticalAlignment: Text.AlignVCenter
    }

    /// "1 col", not "1 cols". The readout is uppercased by the label font,
    /// which makes a wrong plural read as a defect rather than as a typo.
    function counted(n, singular, plural) {
        return n + " " + (n === 1 ? singular : plural)
    }
}
