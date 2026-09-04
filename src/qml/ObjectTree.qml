// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import H5Scope.Backend

/// Left pane: the HDF5 hierarchy, between a mono header strip and its filter.
///
/// TreeView pulls rows from the C++ H5TreeModel through the filter proxy. The
/// model reads a group's children only when its row count is first requested
/// -- i.e. on expand -- so nothing here may ask for a count it does not need.
///
/// The filter box sits at the foot of this pane rather than in the chrome above
/// it, which is what design.txt asks for and is also where it belongs: what it
/// narrows is this tree, and nothing else in the window answers to it.
Rectangle {
    id: root

    /// Follows the controller rather than tracking taps locally, so the tree
    /// highlights whatever is selected -- including the object the controller
    /// picks itself when a file is first opened.
    readonly property string currentPath: AppController.currentPath
    signal objectSelected(string path)

    /// Whether the tags beside the names are drawn. Wired to View -> Tree
    /// Tags; they are the most useful thing in the pane on some files and pure
    /// noise on others, so which it is stays the reader's call.
    property bool tagsVisible: true

    /// Expand to a bounded depth only. The model reads a group's children the
    /// first time its row count is asked for, so a truly recursive expand
    /// would walk -- and read -- the entire file, which is exactly what the
    /// lazy tree exists to avoid.
    function expandToDepth(depth) {
        tree.expandRecursively(-1, depth)
    }

    function collapseAll() {
        tree.collapseRecursively()
    }

    color: Theme.surface

    // --- header strip ----------------------------------------------------
    Item {
        id: headerStrip

        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: Theme.treeHeaderHeight

        Text {
            anchors.left: parent.left
            anchors.leftMargin: Theme.gapM
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("file tree")
            font: Theme.micro
            color: Theme.textSecondary
        }

        // A total object count would mean walking the whole file, which is
        // precisely what the lazy tree exists to avoid, so the strip reports
        // the one figure that is free: the file's size on disk.
        Text {
            anchors.right: parent.right
            anchors.rightMargin: Theme.gapM
            anchors.verticalCenter: parent.verticalCenter
            text: AppController.fileSize
            font: Theme.readout
            color: Theme.textDisabled
        }

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: Theme.borderWidth
            color: Theme.border
        }
    }

    // --- the tree --------------------------------------------------------
    TreeView {
        id: tree

        anchors.top: headerStrip.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: filterStrip.top
        anchors.topMargin: Theme.gapS
        clip: true
        model: AppController.filteredTreeModel

        selectionModel: ItemSelectionModel {}

        ScrollBar.vertical: ScrollBar {}

        delegate: Item {
            id: node

            required property int row
            required property int depth
            required property bool expanded
            required property bool hasChildren
            required property string name
            required property string path
            required property bool isGroup
            required property bool isCyclic
            required property bool isLink
            required property bool linkResolves
            required property bool hasAttributes
            required property int attributeCount
            required property bool isImage
            required property string imageSubclass
            /// Where a link leads, in a sentence -- see H5TreeModel's
            /// LinkDescriptionRole. Empty for anything that is not a link.
            required property string linkDescription
            required property string meta
            required property bool isLastChild
            required property var ancestorLines

            readonly property bool current: root.currentPath === node.path
            /// One colour for every row's guides, selected or not.
            ///
            /// They used to be drawn at `border` and lifted to `borderGuide`
            /// on the current row, which made the shape of the file legible
            /// on exactly one line of it: `border` is n4, two steps off the
            /// ground it is drawn on, so the trunk and elbows that say what
            /// is nested in what were all but invisible everywhere else. The
            /// hierarchy is the pane's subject and is worth drawing whether
            /// or not the pointer has been near it, and the selection has a
            /// marker of its own that does not need the guides' help.
            readonly property color guideColor: Theme.borderGuide

            implicitWidth: tree.width
            implicitHeight: Theme.treeRowHeight

            // The selection, drawn from the name rather than from the pane's
            // left edge.
            //
            // It used to fill the row, which put a lit slab over the guides of
            // the row it was on -- so the one line whose place in the file the
            // reader is asking about was the one line whose connectors were
            // hardest to follow. What is selected is an object, and the object
            // on a row is its name; the guides in front of it belong to its
            // ancestors, which are not what was clicked. So the mark starts
            // where the name does, and the tree drawing runs past it unbroken.
            //
            // Positioned rather than anchored: the name's x is the layout's
            // answer, and it moves with the depth of the row.
            Rectangle {
                // A hair in front of the name: the marker's own width and a
                // step of air after it, which is as far back as this can
                // reach without crossing the caret and the elbow behind it.
                x: line.x + textCell.x - Theme.s3
                width: Math.max(0, node.width - x)
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                radius: Theme.radiusS
                // Theme.clear rather than "transparent": the same colour at
                // zero alpha, which is what "no ground here" means when the
                // ground it stands in for is a light one.
                color: node.current ? Theme.surfaceActive
                     : hover.hovered ? Theme.surfaceHover
                                     : Theme.clear(Theme.surfaceHover)

                // Active marker: a 2px signal-white rule, never a filled tint.
                Rectangle {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: Theme.borderWidthAccent
                    color: node.current ? Theme.accent : "transparent"
                }
            }

            HoverHandler { id: hover }

            // A MouseArea rather than a TapHandler, for the double click.
            //
            // TapHandler counts its own taps: it compares the timestamps of two
            // releases against the platform's double-click interval and emits
            // `doubleTapped` when they are close enough. That works only while
            // nothing in between resets the count, and the row is inside a
            // TreeView, which runs a tap handler of its own over the same
            // points -- so a double click on a group opened nothing at all.
            //
            // A MouseArea does not count anything. It answers the
            // QEvent::MouseButtonDblClick the window system itself sends, which
            // is the same event every other application on the desktop opens a
            // folder from, and which is by definition a double click by the
            // reader's own settings rather than by this program's arithmetic.
            //
            // Flicking still works: Flickable takes the grab off a delegate's
            // MouseArea once a press turns into a drag, which is the standard
            // arrangement for every list in Qt Quick.
            MouseArea {
                anchors.fill: parent
                onClicked: root.objectSelected(node.path)
                // What the caret does, from anywhere on the row. A dataset has
                // nothing to open, and a double click on one is simply the two
                // selections it looks like.
                onDoubleClicked: {
                    if (node.hasChildren)
                        tree.toggleExpanded(node.row)
                }
            }

            RowLayout {
                id: line

                anchors.fill: parent
                anchors.leftMargin: Theme.gapS
                anchors.rightMargin: Theme.gapM
                spacing: 0

                // Connector guides: one 16px slot per ancestor that still has
                // a sibling below, then this node's own elbow.
                Row {
                    id: guides
                    Layout.fillHeight: true

                    Repeater {
                        model: node.ancestorLines

                        delegate: Item {
                            required property var modelData

                            width: Theme.gapL
                            height: guides.height

                            Rectangle {
                                x: Theme.gapS
                                width: Theme.borderWidth
                                height: parent.height
                                color: modelData ? node.guideColor : "transparent"
                            }
                        }
                    }

                    Item {
                        width: Theme.gapL
                        height: guides.height

                        // Stops halfway on the last child, so the trunk ends
                        // at the elbow instead of running past it.
                        Rectangle {
                            x: Theme.gapS
                            width: Theme.borderWidth
                            height: node.isLastChild ? parent.height / 2
                                                     : parent.height
                            color: node.guideColor
                        }

                        Rectangle {
                            x: Theme.gapS
                            y: parent.height / 2
                            width: Theme.gapS
                            height: Theme.borderWidth
                            color: node.guideColor
                        }
                    }
                }

                Text {
                    Layout.preferredWidth: Theme.gapL - 2
                    Layout.fillHeight: true
                    text: node.hasChildren ? (node.expanded ? "▾" : "▸") : ""
                    font: Theme.caret
                    color: Theme.textSecondary
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter

                    TapHandler {
                        enabled: node.hasChildren
                        onTapped: tree.toggleExpanded(node.row)
                    }
                }

                // --- the name, and what is measurably true about it -------
                // Both live in one cell that takes all the slack, and the
                // split between them is made here rather than left to the
                // layout: a RowLayout shrinks whatever it likes when a row runs
                // out of room, and which of these two survives that is the
                // whole of the question.
                //
                // The name is what the reader is looking for; the readout is a
                // fact about it. So the readout gets only what the name does
                // not need. A link's target is the longest readout in the pane
                // and the one with the most competition -- a soft link's name
                // and its target are two paths on one row -- so it yields
                // entirely and the name stays whole. Every other readout is a
                // shape or a count, short and stable, and keeps a third of the
                // cell as a floor so a matrix does not stop saying what shape
                // it is the moment it is nested four levels deep.
                //
                // Both are measured with TextMetrics rather than off the Texts
                // themselves: an item whose visibility is derived from its own
                // implicit width is a knot, and this way the arithmetic is
                // settled before anything is laid out at all.
                Item {
                    id: textCell

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.leftMargin: Theme.s1

                    /// What the tags take, and the gap in front of them. They
                    /// come out of the name's share rather than the readout's:
                    /// a tag is a fact about the object with the same claim on
                    /// the row as its shape, and the readout has already
                    /// yielded once.
                    readonly property real tagsWidth:
                        tags.visible && tags.width > 0 ? tags.width + Theme.gapS : 0

                    // advanceWidth, not width: TextMetrics.width rounds down
                    // to whole pixels and a Text elides the moment it is given
                    // half a pixel less than it needs, which costs a whole
                    // character and an ellipsis on top of it.
                    readonly property real metaWidth: {
                        if (node.meta === "")
                            return 0
                        const wanted = metaMetrics.advanceWidth
                        const spare = width - Theme.gapS - textCell.tagsWidth
                                      - nameMetrics.advanceWidth
                        const floor = node.isLink ? 0 : width / 3
                        const room = Math.max(spare, floor)
                        // A readout that fits is always drawn, however short.
                        // The floor below is about how little room is worth
                        // *eliding into* -- it was being applied to the
                        // readout's own width instead, so a short one like
                        // "(4 x 3)" was dropped for being narrower than the
                        // minimum, which is the opposite of what it is for.
                        // That is why half the shapes in the pane were missing.
                        if (wanted <= room)
                            return wanted
                        // Below the floor what is left is an ellipsis and
                        // nothing else, which is worse than giving the space
                        // back -- and the tooltip carries it whole either way.
                        return room >= Theme.treeMetaMinWidth ? room : 0
                    }

                    TextMetrics {
                        id: nameMetrics
                        font: nameLabel.font
                        text: node.name
                    }

                    TextMetrics {
                        id: metaMetrics
                        font: metaLabel.font
                        text: node.meta
                    }

                    // Groups carry the weight, datasets the plain face. Signal
                    // white is rationed, so neither gets a colour of its own.
                    Text {
                        id: nameLabel

                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        width: Math.max(0, textCell.width - textCell.metaWidth
                                           - textCell.tagsWidth
                                           - (textCell.metaWidth > 0 ? Theme.gapS : 0))
                        text: node.name
                        font: node.isGroup ? Theme.bodySmallStrong : Theme.bodySmall
                        color: node.isGroup ? Theme.textEmphasis : Theme.textPrimary
                        elide: Text.ElideRight
                        verticalAlignment: Text.AlignVCenter

                        // A name deep in the tree has little of the row left
                        // to it, and HDF5 allows names of any length -- the
                        // example file carries a 200-character one.
                        AppToolTip {
                            shown: nameLabel.truncated && hover.hovered
                            verbatim: true
                            text: node.path
                        }
                    }

                    // --- the tags ---------------------------------------
                    // Directly after the name, because that is what they are
                    // about. They had been three fixed slots at the pane's
                    // right edge, which lined them up into a column at the
                    // price of standing a tag two hundred pixels away from the
                    // name it qualifies -- and of spending that width on every
                    // row whether or not it had a tag to put there. A tag is
                    // an adjective; it goes next to its noun.
                    //
                    // One letter each, because three of them have to fit
                    // between a name and a shape on a 26px row. A letter is
                    // not self-explanatory, so every one of them says what it
                    // means on hover -- and says the thing the reader would
                    // ask next, which is never "this has attributes" but how
                    // many, not "this is a link" but where to.
                    Row {
                        id: tags

                        x: Math.min(nameMetrics.advanceWidth, nameLabel.width)
                           + Theme.gapS
                        anchors.verticalCenter: parent.verticalCenter
                        visible: root.tagsVisible
                        spacing: Theme.gapXS

                        Badge {
                            id: imageTag

                            visible: node.isImage
                            compact: true
                            tone: "info"
                            text: qsTr("I")

                            HoverHandler { id: imageTagHover }

                            AppToolTip {
                                shown: imageTagHover.hovered
                                text: node.imageSubclass === ""
                                      ? qsTr("Declared an image by the file.")
                                      : qsTr("Declared a %1 image by the file, so the Data Viewer opens on the picture.")
                                            .arg(node.imageSubclass.toLowerCase())
                            }
                        }

                        // A soft or external link, or a hard one that closes a
                        // loop. Red when it leads nowhere at all -- that is a
                        // fault in the file and the one tag state a reader has
                        // to act on. Amber for a loop, which is legal, means
                        // the object is already on screen under another name,
                        // and is only a reason not to expand it.
                        Badge {
                            id: linkTag

                            visible: node.isLink || node.isCyclic
                            compact: true
                            tone: {
                                if (node.isLink && !node.linkResolves)
                                    return "crit"
                                return node.isCyclic ? "warn" : "neutral"
                            }
                            text: qsTr("L")

                            HoverHandler { id: linkTagHover }

                            AppToolTip {
                                shown: linkTagHover.hovered
                                text: {
                                    if (node.linkDescription !== "")
                                        return node.linkDescription
                                    return qsTr("A second name for an object already above it in the tree, so it is not expanded here.")
                                }
                            }
                        }

                        Badge {
                            id: attributeTag

                            visible: node.hasAttributes
                            compact: true
                            tone: "neutral"
                            text: qsTr("A")

                            HoverHandler { id: attributeTagHover }

                            AppToolTip {
                                shown: attributeTagHover.hovered
                                text: node.attributeCount === 1
                                      ? qsTr("1 attribute.")
                                      : qsTr("%1 attributes.")
                                            .arg(node.attributeCount)
                            }
                        }
                    }

                    // Elided from the left: the informative end of a link
                    // target is its last segment, and the informative end of a
                    // shape is its fastest dimension.
                    Text {
                        id: metaLabel

                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        width: textCell.metaWidth
                        visible: width > 0
                        text: node.meta
                        font: Theme.readout
                        color: Theme.textDisabled
                        elide: Text.ElideLeft
                        horizontalAlignment: Text.AlignRight
                        verticalAlignment: Text.AlignVCenter
                    }

                    // The readout in full: its own row's, whether that row is
                    // drawing it elided, or has dropped it for want of width.
                    // A shape and a link target are facts about the object, and
                    // a narrow pane is not a reason to lose them.
                    AppToolTip {
                        shown: hover.hovered && node.meta !== ""
                               && (metaLabel.truncated || textCell.metaWidth === 0)
                        verbatim: true
                        text: node.meta
                    }
                }


            }
        }
    }

    Text {
        anchors.centerIn: parent
        visible: !AppController.hasFile
        text: qsTr("no file open")
        font: Theme.micro
        color: Theme.textDisabled
    }

    // --- the filter ------------------------------------------------------
    // Moved here from the action bar, bindings and all. A hairline along its
    // top separates it from the rows above exactly as the header strip's does
    // below, so the tree reads as one pane bounded top and bottom.
    Item {
        id: filterStrip

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: Theme.smallControlHeight + Theme.gapS * 2

        Rectangle {
            anchors.top: parent.top
            width: parent.width
            height: Theme.borderWidth
            color: Theme.border
        }

        FilterInput {
            objectName: "treeFilter"

            anchors.fill: parent
            anchors.margins: Theme.gapS
            enabled: AppController.hasFile
            placeholderText: qsTr("filter by name or path")
            text: AppController.filterText
            onTextEdited: AppController.filterText = text
        }
    }
}
