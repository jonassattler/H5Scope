// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import QtQuick.Controls.Basic

/// The design system's one card treatment: a hairline border, a 2px radius, a
/// mono uppercase header bar and no drop shadow. Nothing in this application
/// is a "card" in any other sense.
///
/// `accent` draws the 2px signal-white rule along the top edge. The system
/// rations that to one Panel per screen, so the Information tab marks only the
/// object panel with it.
///
/// A card is as tall as what it holds unless something outside it says
/// otherwise, and when something does, the body scrolls rather than the card
/// growing past the room it was given. That is what lets a page of cards be a
/// page: every card stays on screen and keeps its header, and the one with
/// more in it than fits is the one that moves. It is the metrics note's rule
/// in Theme.qml -- content scrolls, chrome never does -- applied to the card
/// rather than to the window.
Rectangle {
    id: panel

    property string title
    property string meta
    property bool accent: false
    property int contentPadding: Theme.gapL

    default property alias content: body.data

    /// The width the content is laid out at. Read from outside rather than
    /// worked out again there: the padding and the border are this file's
    /// business, and a second copy of the arithmetic is one to keep in step.
    readonly property alias bodyWidth: body.width

    /// Whether the body is taller than the room the card was given -- which is
    /// to say, whether there is anything below the fold to scroll to.
    ///
    /// A hairline of tolerance because the two sides come from the same
    /// arithmetic by different routes: when the card is at its natural height
    /// these are equal, and a float's worth of disagreement would otherwise
    /// put a scrollbar on a card that has nothing hidden.
    readonly property bool scrolls: body.implicitHeight > scroller.height
                                    + Theme.hairline

    /// How short the card may be squeezed before it stops being a card. Its
    /// own chrome, plus enough body that what is behind the scrollbar is
    /// evidently more of the same list.
    readonly property real minimumHeight: headerBar.height + Theme.panelMinHeight
                                          + contentPadding * 2
                                          + border.width * 2

    // One step above the page, so the row separators inside -- which sit a
    // further step up -- stay visible against it.
    color: Theme.surface
    radius: Theme.radiusS
    border.width: Theme.borderWidth
    border.color: Theme.border

    implicitHeight: headerBar.height + body.implicitHeight + contentPadding * 2
                    + border.width * 2

    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: panel.radius
        anchors.rightMargin: panel.radius
        height: Theme.borderWidthAccent
        color: Theme.accent
        visible: panel.accent
        z: 1
    }

    Item {
        id: headerBar

        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: panel.border.width
        height: Theme.treeHeaderHeight

        Text {
            anchors.left: parent.left
            anchors.leftMargin: Theme.gapM
            anchors.verticalCenter: parent.verticalCenter
            text: panel.title
            font: Theme.micro
            color: Theme.textSecondary
        }

        Text {
            id: metaLabel

            anchors.right: parent.right
            anchors.rightMargin: Theme.gapM
            anchors.verticalCenter: parent.verticalCenter
            text: panel.meta
            font: Theme.readout
            color: Theme.textDisabled
            elide: Text.ElideRight

            HoverHandler { id: metaHover }

            AppToolTip {
                shown: metaLabel.truncated && metaHover.hovered
                verbatim: true
                text: panel.meta
            }
        }

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: Theme.borderWidth
            color: Theme.border
        }
    }

    // The body sits in a viewport of its own so that a card given less height
    // than it wants can still show all of what it holds, a screenful at a
    // time. `body.implicitHeight` is what the rows measure to and never what
    // the viewport measures to, so the card's implicitHeight above stays the
    // card's *natural* height however short it is actually drawn -- which is
    // what the layout above it reads to decide how much to give it, and why
    // reading it is not a loop.
    //
    // The viewport reaches to the card's inner edge and the padding is on the
    // Column instead, so the scrollbar is drawn over the padding rather than
    // over the last few pixels of a value. Insetting the content when it
    // scrolls would be the other way to do that, and it is a binding loop:
    // the width decides the wrapping, the wrapping decides the height, and the
    // height is what says whether it scrolls.
    Flickable {
        id: scroller

        anchors.top: headerBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.topMargin: panel.contentPadding
        anchors.leftMargin: panel.border.width
        anchors.rightMargin: panel.border.width
        anchors.bottomMargin: panel.contentPadding + panel.border.width
        clip: panel.scrolls
        interactive: panel.scrolls
        contentWidth: width
        contentHeight: body.implicitHeight
        boundsBehavior: Flickable.StopAtBounds

        // Always on rather than as-needed: as-needed is Basic's fading
        // overlay, and a card that is hiding half of what it holds has to say
        // so while nobody is touching it.
        ScrollBar.vertical: ScrollBar {
            policy: panel.scrolls ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
        }

        Column {
            id: body

            x: panel.contentPadding
            width: scroller.width - panel.contentPadding * 2
        }
    }
}
