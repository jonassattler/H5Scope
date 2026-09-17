// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import QtQuick.Layouts
import H5Scope.Backend

/// "Information" tab: everything known about the selection, as a reflowing set
/// of Panels -- object, dataspace, datatype, storage, attributes.
///
/// Laid out in columns rather than in a grid. A grid puts the panels in rows,
/// and a row is as tall as the tallest thing in it -- so one panel carrying a
/// sentence-long warning left every panel beside it sitting over a hole the
/// height of that warning, which is the "too much space between the cards"
/// that has nothing visible causing it. Columns have no rows to line up, so a
/// short panel is followed by the next one and nothing else.
///
/// The attributes panel spans the full width below them, because attribute
/// values are free text and are the one thing here that benefits from the room.
///
/// The tab itself does not scroll: every card the selection produces is on
/// screen at once, and the one with more in it than its share of the height
/// scrolls its own body instead -- see Panel.qml. A page that scrolled as a
/// page answered "what is this object?" with a column to be travelled down,
/// where the panel a reader wanted was as likely to be below the fold as not
/// and nothing on screen said which. This tab is a dashboard and not a
/// document, and a dashboard that has to be scrolled to be read is a document.
Rectangle {
    id: root

    color: Theme.background
    // The floor a card gives way to is a share of this tab (`cardFloor`), so
    // the cards fit whatever height the window has. What they cannot do is fit
    // a height smaller than their own chrome -- a header is a fixed number of
    // pixels and there is no arrangement of six of them inside two hundred.
    // Clipped rather than drawn over the status strip.
    clip: true

    readonly property var panels: AppController.infoPanels

    /// The one panel that takes the whole width. Named rather than tested for
    /// in three places.
    readonly property string wideTitle: "attributes"

    // Heights are shared out by QtQuick.Layouts rather than by arithmetic of
    // this file's own: every card asks for its natural height as a maximum,
    // gives way no further than its floor, and the engine takes the shortfall
    // off the cards with the most to spare -- slightly more than in
    // proportion, so the long one yields before the short one does. Working it
    // out here instead would mean a layout that reads the heights it is
    // deciding, which is the same loop the dealing note below is about, only
    // with a card's own height on both sides of it.
    ColumnLayout {
        id: page

        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.gapXL
        // As tall as the cards want, and never taller than the window. Both
        // halves are load-bearing. Filling the window regardless is what a
        // layout does by default, and a layout with more height than its
        // items can use does not leave the slack at the bottom -- it grows
        // every cell and centres each item in the one it was given, which
        // draws this page as cards adrift in the middle of holes. Capping it
        // at what the cards want is also what makes the shortfall real: below
        // this height there is nothing left to give and the cards start
        // scrolling instead.
        height: Math.min(implicitHeight, page.available)
        spacing: Theme.gutter
        visible: root.panels.length > 0

        /// The room the window gives the page. Read off the tab rather than
        /// off the layout, so that nothing the layout decides can reach a
        /// number the layout is decided by.
        readonly property real available: root.height - Theme.gapXL * 2

        // The CSS is repeat(auto-fit, minmax(340px, 1fr)); this is the
        // same rule arithmetic, since QML has no auto-fit. The width each
        // column is then drawn at is the RowLayout's to share out, which is
        // the same division by another route.
        readonly property int columns:
            Math.max(1, Math.floor((width + Theme.gutter)
                                   / (Theme.panelMinWidth + Theme.gutter)))

        /// The panels dealt into `columns` lists, in order, each panel
        /// going to whichever column is currently shortest.
        ///
        /// Shortest by row count, not by measured height: a layout that
        /// reads the heights it is deciding is a binding loop, and a row
        /// count is a good enough proxy because every row is one line of
        /// the same two faces. It is only choosing which column, and being
        /// a row or two out chooses the same column nearly always.
        readonly property var columnised: {
            const lists = []
            const weights = []
            for (let c = 0; c < columns; ++c) {
                lists.push([])
                weights.push(0)
            }
            for (const panel of root.panels) {
                if (panel.title === root.wideTitle)
                    continue
                let shortest = 0
                for (let c = 1; c < columns; ++c) {
                    if (weights[c] < weights[shortest])
                        shortest = c
                }
                lists[shortest].push(panel)
                // The header, plus a row each -- or the sentence a panel
                // with nothing to list says instead, which is about two.
                weights[shortest] +=
                    2 + (panel.rows === undefined ? 0 : panel.rows.length)
            }
            return lists
        }

        readonly property var widePanels:
            root.panels.filter(panel => panel.title === root.wideTitle)

        /// The deepest stack of cards the page draws: the longest column, and
        /// the wide panel under the whole of it.
        readonly property int deepest: {
            let longest = 0
            for (const list of columnised)
                longest = Math.max(longest, list.length)
            // At least one, so that the share below is a number even on the
            // page with nothing on it.
            return Math.max(1, longest + widePanels.length)
        }

        /// How short a card may be pushed *on this page*, which is not always
        /// as much as a card would like to keep.
        ///
        /// Theme.panelMinHeight is what a card keeps when there is anything to
        /// keep it out of; this is what there is. Six cards in one column each
        /// holding a header and three rows want more window than a short one
        /// has, and a floor the page cannot afford is a floor that pushes the
        /// last card off the bottom -- which is the one thing this layout
        /// exists to prevent. So the floor is also an equal share of the page:
        /// where the room runs out every card keeps its header and whatever
        /// that share leaves under it, and the page still ends where the
        /// window does.
        readonly property real cardFloor:
            Math.max(0, (available - Theme.gutter * (deepest - 1)) / deepest)

        RowLayout {
            id: band

            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.gutter

            Repeater {
                model: page.columnised

                delegate: ColumnLayout {
                    id: stack

                    required property var modelData

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    // The band is as tall as the column that wants the most,
                    // so every other column has room to spare -- and the same
                    // rule as the page: capped at what its own cards want and
                    // put at the top of what is left, rather than stretched
                    // and its cards spread down it.
                    Layout.maximumHeight: stack.implicitHeight
                    Layout.alignment: Qt.AlignTop
                    spacing: Theme.gutter

                    Repeater {
                        model: stack.modelData
                        delegate: InfoPanel {}
                    }
                }
            }
        }

        Repeater {
            model: page.widePanels
            delegate: InfoPanel {}
        }
    }

    /// One panel and its rows. An inline component so the column layout above
    /// can instantiate it in two places without either being a copy.
    component InfoPanel: Panel {
        id: panel

        required property var modelData

        readonly property bool empty: panel.modelData.emptyText !== undefined
                                      && panel.modelData.emptyText !== ""

        Layout.fillWidth: true
        Layout.fillHeight: true
        // What it would like: its own natural height, and never more -- a card
        // stretched to fill the page is a header with a field of empty surface
        // under it.
        Layout.maximumHeight: panel.implicitHeight
        // What it will settle for: its own floor, never more than the page
        // can afford to give every card that number, and never more than it
        // wanted in the first place. That last `min` is what keeps a short
        // card whole -- where its natural height is already inside the floor,
        // its minimum and its maximum are the same number and the engine has
        // nothing to take from it, however much the card beside it is
        // overflowing.
        Layout.minimumHeight: Math.min(panel.implicitHeight,
                                       panel.minimumHeight, page.cardFloor)

        title: modelData.title
        meta: modelData.meta
        accent: modelData.accent

        // Nothing to list, and a panel that says so in words. An empty table
        // headed "attributes" holding a single row reading "Attributes  0"
        // reads as something that failed to load rather than as an object that
        // simply has none.
        Text {
            width: panel.bodyWidth
            visible: panel.empty
            height: visible ? implicitHeight + Theme.gapS * 2 : 0
            topPadding: Theme.gapS
            bottomPadding: Theme.gapS
            text: panel.empty ? panel.modelData.emptyText : ""
            font: Theme.caption
            color: Theme.textDisabled
            wrapMode: Text.WordWrap
        }

        Repeater {
            model: panel.empty ? [] : panel.modelData.rows

            delegate: Item {
                id: infoRow

                required property int index
                required property var modelData

                readonly property bool last:
                    infoRow.index === panel.modelData.rows.length - 1

                /// How far in the label is drawn. Non-zero only in the datatype
                /// panel, where a compound is opened out until nothing is left
                /// but base types, and the indent is the whole of what says
                /// which member a row belongs to.
                readonly property int depth:
                    infoRow.modelData.depth === undefined
                        ? 0 : infoRow.modelData.depth

                width: panel.bodyWidth
                // Snapped, for the rule at the foot of it.
                //
                // A row is as tall as its text, and text is measured in
                // fractions of a logical pixel -- so without this every row
                // boundary in the panel lands at a different fraction of a
                // physical one, and the hairline drawn there is smeared across
                // two rows of the screen at a different share of each. That is
                // what made these lines look like several different weights of
                // line: they were. Snapping the row is what fixes it, exactly
                // as ValueGrid snaps its cell -- every seam after the first is
                // a multiple of the row. Rounding is to the nearest physical
                // pixel and the row carries a gap step of air at each end, so
                // nothing can be clipped by it.
                implicitHeight: Theme.snap(Math.max(key.implicitHeight,
                                                    value.implicitHeight)
                                           + Theme.gapS + Theme.gapS)

                // The label column, wide enough for the longest label the
                // application produces and never more than a share of the row
                // -- a panel spanning the window must not spend a third of it
                // on the word "Path". It had been a flat 116px, which is
                // narrower than "IMAGE_SUBCLASS" and so cut the names of the
                // attributes off in the one panel that is all names.
                SelectableText {
                    id: key

                    anchors.left: parent.left
                    // The indent comes out of the label column rather than
                    // pushing the value column along with it: the values are a
                    // column and stay one, whatever depth the name beside them
                    // is at. A tree that moved both would be unreadable by the
                    // third level.
                    anchors.leftMargin: infoRow.depth * Theme.gapL
                    anchors.top: parent.top
                    anchors.topMargin: Theme.gapS
                    width: Math.max(Theme.gapL,
                                    Math.min(Theme.infoLabelWidth,
                                             infoRow.width * 0.45)
                                    - infoRow.depth * Theme.gapL)
                    text: infoRow.modelData.label
                    font: Theme.micro
                    color: Theme.textSecondary
                }

                SelectableText {
                    id: value

                    anchors.left: key.right
                    anchors.leftMargin: Theme.gapL
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.topMargin: Theme.gapS
                    text: infoRow.modelData.value
                    font: Theme.mono
                    color: infoRow.modelData.isWarning
                           ? Theme.warning : Theme.textPrimary
                    // Words first, and only inside a word when the word is
                    // wider than the column -- which a path is and a sentence
                    // is not. It had been WrapAnywhere for every value alike,
                    // so a warning broke mid-syllable: "which thi / s shape".
                    wrapMode: Text.Wrap
                }

                // Between the rows, not under the last of them: a rule along
                // the foot of the panel is the panel's own border drawn twice,
                // a hairline's width above itself.
                //
                // `border` -- line-1, the weight this panel is already drawn
                // with, at its rim and under its header. It was `surfaceRaised`,
                // which is the next surface up from the one the panel stands on
                // and six values of 255 away from it: a separator that has to be
                // hunted for is not separating anything. A rule between rows is
                // a line and takes a line colour.
                //
                // `hairline` rather than `borderWidth`, so it is a whole number
                // of physical pixels wherever the window is: one logical pixel
                // is one and a half physical ones at 150%, and half a pixel of
                // a line is half its colour.
                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: Theme.hairline
                    color: Theme.border
                    visible: !infoRow.last
                }
            }
        }
    }

    Column {
        anchors.centerIn: parent
        spacing: Theme.gapS
        visible: root.panels.length === 0

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("no object selected")
            font: Theme.micro
            color: Theme.textSecondary
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: AppController.hasFile
                  ? qsTr("Select a group or dataset in the tree to describe it.")
                  : qsTr("Open an HDF5 file to begin.")
            font: Theme.body
            color: Theme.textSecondary
        }
    }
}
