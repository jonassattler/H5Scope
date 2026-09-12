// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import QtQuick.Layouts
import H5Scope.Backend

/// What a custom plot draws, and against what: the saved views, the x axis,
/// and the lines themselves.
///
/// Three questions in the order a reader answers them backwards. The lines are
/// at the bottom because that is where the list grows and where the pointer
/// spends its time; the x axis above them because it is one decision that
/// applies to all of them; and the views at the top because they are the way in
/// and out of the whole arrangement.
///
/// This is the wide rail rather than the 212 one. An entry states a path, a
/// subscript and where it sits when it is the wrong length -- three things on
/// one line -- and stacked into 212 each of them became three lines and the
/// list stopped reading as a list. The pipeline panel already takes the wide
/// rail for the same reason.
SettingsPanel {
    id: panel

    objectName: "customDataPanel"

    /// The warning a restore raises when the view no longer fits the file.
    /// Exposed because a Popup is not in the item tree, so the QML suite --
    /// which finds things by walking it -- cannot reach one any other way.
    readonly property alias restoreDialog: restoreWarning

    /// The CustomPlot being described, and its place in the set.
    property var plot
    property int plotIndex: -1
    /// The surface, which is what the range axis controls write to.
    property var surface


    title: qsTr("data settings")

    // --- the views -------------------------------------------------------
    SettingRow {
        label: qsTr("views")

        RowLayout {
            width: parent.width
            spacing: Theme.gapS

            // Opens on what the tab is called, because that is the name the
            // view will nearly always want: a reader who has named a plot
            // "morning vs afternoon" and then saves it is saving morning
            // versus afternoon. Set rather than bound -- a binding on `text`
            // breaks the moment they type over it, and this has to keep
            // following the tab's name until they do.
            FilterInput {
                id: viewName

                objectName: "viewName"

                Layout.fillWidth: true
                implicitHeight: Theme.smallControlHeight
                font: Theme.body
                placeholderText: qsTr("save as")
                invalid: internal.saveProblem !== ""

                onTextEdited: internal.saveProblem = ""
                onAccepted: internal.save()

                Component.onCompleted: internal.offerPlotName()
            }

            AppToolButton {
                text: qsTr("save")
                size: "sm"
                enabled: viewName.text.trim() !== "" && panel.plotIndex >= 0
                onClicked: internal.save()

                AppToolTip {
                    shown: parent.hovered
                    text: qsTr("keep these lines, this x axis and these " +
                               "colours under a name")
                }
            }
        }

        Text {
            width: parent.width
            visible: text !== ""
            text: internal.saveProblem
            font: Theme.caption
            color: Theme.warning
            wrapMode: Text.WordWrap
        }

        // A view belongs to the session rather than to the tab it was saved
        // from, which is what makes saving one worth doing: a comparison built
        // once can be put into a fresh tab beside another.
        //
        // A Column of its own inside the row, because SettingRow spaces its
        // children at gapXS -- right for a caption under a control, too tight
        // for a stack of cards to read as separate ones.
        Column {
            width: parent.width
            spacing: Theme.gapS

            Repeater {
                model: AppController.customPlots.viewNames

            // Each view is a card rather than a line of text with two buttons
            // after it. Three of these stacked in a rail read as one paragraph
            // with some controls scattered through it unless something says
            // where one stops and the next begins -- and what a view is is a
            // *thing* the reader made and can pick up again, which a bordered
            // inset says and a run of text does not.
            delegate: Rectangle {
                id: viewRow

                required property string modelData

                width: parent ? parent.width : 0
                implicitHeight: viewLine.implicitHeight + Theme.gapS * 2
                radius: Theme.radiusS
                color: viewHover.hovered ? Theme.surfaceHover
                                         : Theme.surfaceRaised
                border.width: Theme.borderWidth
                border.color: Theme.border

                HoverHandler { id: viewHover }

                /// How much of the open file this view can still draw. Read
                /// off the list rather than computed here, and re-read
                /// whenever the list is announced -- which is whenever a file
                /// is opened and the states are worked out again.
                readonly property int state:
                    AppController.customPlots.viewNames.length >= 0
                    ? AppController.customPlots.stateOf(viewRow.modelData) : 0

                RowLayout {
                    id: viewLine

                    anchors.fill: parent
                    anchors.leftMargin: Theme.gapS
                    anchors.topMargin: Theme.gapS
                    anchors.bottomMargin: Theme.gapS
                    // Flush right, where every other rightmost control in this
                    // panel is. A card inset on this side too would put its
                    // cross eight pixels in from the crosses on the entry rows
                    // below it, and two columns of the same glyph that nearly
                    // line up read worse than one that does.
                    anchors.rightMargin: 0
                    spacing: Theme.gapS

                    // Green for all of it, amber for some, red for none. The
                    // same dot the strip's drawer draws, in the same colours,
                    // which is why the mapping is Theme's rather than either
                    // file's.
                    Rectangle {
                        Layout.preferredWidth: Theme.indicatorSize / 2
                        Layout.preferredHeight: Theme.indicatorSize / 2
                        Layout.alignment: Qt.AlignVCenter
                        radius: width / 2
                        color: Theme.matchColor(viewRow.state, false)

                        HoverHandler { id: dotHover }

                        AppToolTip {
                            shown: dotHover.hovered
                            text: viewRow.state >= 2
                                  ? qsTr("every dataset this names is in the open file")
                                  : viewRow.state >= 1
                                    ? qsTr("some of what this names is in the open file")
                                    : qsTr("none of what this names is in the open file")
                        }
                    }

                    Text {
                        id: viewLabel

                        Layout.fillWidth: true
                        text: viewRow.modelData
                        font: Theme.bodySmall
                        color: Theme.textPrimary
                        elide: Text.ElideMiddle
                        verticalAlignment: Text.AlignVCenter

                        AppToolTip {
                            shown: viewLabel.truncated && viewHover.hovered
                            verbatim: true
                            text: viewLabel.text
                        }
                    }

                    AppToolButton {
                        objectName: "restoreView"

                        /// Which view this row is for, so the suite can press
                        /// the right one of several.
                        readonly property string viewName: viewRow.modelData

                        text: qsTr("restore")
                        size: "sm"
                        enabled: panel.plotIndex >= 0
                        // Checked before it is applied, and the check has to
                        // ask the file: a view can name a dataset this session
                        // has never looked at. The answer arrives at
                        // onViewChecked below.
                        onClicked: {
                            internal.restoring = viewRow.modelData
                            AppController.customPlots.checkView(viewRow.modelData)
                        }
                    }

                    AppIconButton {
                        glyph: "close"
                        ink: Theme.danger
                        bare: true
                        onClicked: AppController.customPlots.removeView(
                                       viewRow.modelData)
                    }
                    }
                }
            }
        }

        Text {
            width: parent.width
            visible: AppController.customPlots.viewNames.length === 0
            text: qsTr("Nothing saved yet. A view keeps its name, its lines, " +
                       "its x axis and its colours, and can be put into any " +
                       "custom plot.")
            font: Theme.caption
            color: Theme.textDisabled
            wrapMode: Text.WordWrap
        }
    }

    // --- the x axis ------------------------------------------------------
    SettingRow {
        label: qsTr("x axis")

        AppComboBox {
            width: parent.width
            model: [qsTr("index"), qsTr("range"), qsTr("time series")]
            selectedIndex: panel.plot ? panel.plot.xMode : 0
            onActivated: (index) => {
                if (panel.plot)
                    panel.plot.xMode = index
            }
        }

        Text {
            width: parent.width
            text: {
                if (!panel.plot)
                    return ""
                if (panel.plot.xMode === CustomPlot.Index)
                    return qsTr("Each sample at its own position, 0, 1, 2 …")
                if (panel.plot.xMode === CustomPlot.Range)
                    return qsTr("x = start + i × step, stated below.")
                return qsTr("Each sample at the value another slice holds in " +
                            "the same place.")
            }
            font: Theme.caption
            color: Theme.textDisabled
            wrapMode: Text.WordWrap
        }

        RangeAxisSetting {
            width: parent.width
            visible: panel.plot && panel.plot.xMode === CustomPlot.Range
            target: panel.surface
        }

        // The time base, written exactly the way an entry is. The same
        // notation in every box on this panel is most of what makes the panel
        // learnable: a reader who can write a line can write the axis.
        ColumnLayout {
            width: parent.width
            spacing: Theme.gapXS
            visible: panel.plot && panel.plot.xMode === CustomPlot.Dataset

            FilterInput {
                id: timeBase

                objectName: "timeBaseBox"

                Layout.fillWidth: true
                implicitHeight: Theme.smallControlHeight
                text: panel.plot ? panel.plot.xExpression : ""
                placeholderText: qsTr("/group/time[:]")
                invalid: internal.timeProblem !== ""
                         || (panel.plot && panel.plot.xError !== "")
                pending: panel.plot
                         && timeBase.text.trim() !== panel.plot.xExpression
                         && internal.timeProblem === ""

                onTextEdited: {
                    internal.timeProblem = panel.plot
                        ? panel.plot.xExpressionError(timeBase.text) : ""
                }
                onAccepted: internal.commitTimeBase()
                onActiveFocusChanged: if (!activeFocus) internal.commitTimeBase()
                Keys.onEscapePressed: {
                    timeBase.text = panel.plot ? panel.plot.xExpression : ""
                    internal.timeProblem = ""
                    timeBase.focus = false
                }
            }

            Text {
                Layout.fillWidth: true
                visible: text !== ""
                text: internal.timeProblem !== "" ? internal.timeProblem
                    : (panel.plot && panel.plot.xError !== "")
                      ? panel.plot.xError
                      : ""
                font: Theme.caption
                color: Theme.warning
                wrapMode: Text.WordWrap
            }
        }
    }

    // --- the lines -------------------------------------------------------
    SettingRow {
        label: qsTr("y axis")

        // A Column of its own, because SettingRow spaces its children at
        // gapXS -- right for a caption under a control, too tight for a stack
        // of cards to read as separate ones. The same arrangement the saved
        // views above take.
        Column {
            width: parent.width
            spacing: Theme.gapS

            Repeater {
                id: entries

                objectName: "entryRows"

                model: panel.plot

                // A plain wrapper taking the roles off the model and handing
                // them down, because a required property cannot also be one the
                // component already declares. PostprocessPanel does the same.
                delegate: Item {
                    id: holder

                    required property int index
                    required property string expression
                    required property string alias
                    required property string error
                    required property int scaling
                    required property bool scalable
                    required property bool drawn

                    width: parent ? parent.width : 0
                    implicitHeight: entry.implicitHeight

                    CustomEntryRow {
                        id: entry

                        width: holder.width
                        plot: panel.plot
                        rowIndex: holder.index
                        expression: holder.expression
                        alias: holder.alias
                        error: holder.error
                        scaling: holder.scaling
                        scalable: holder.scalable
                        drawn: holder.drawn
                    }
                }
            }
        }

        AppToolButton {
            objectName: "addEntry"

            width: parent.width
            text: qsTr("add")
            size: "sm"
            enabled: panel.plot !== null && panel.plot !== undefined
            // An empty row rather than a dialog: what goes in it is a line of
            // text, and the box it goes in is the one already on screen.
            onClicked: {
                if (panel.plot)
                    panel.plot.addExpression("")
            }

            AppToolTip {
                shown: parent.hovered
                text: qsTr("write a path and a subscript yourself")
            }
        }

        Text {
            width: parent.width
            visible: panel.plot && panel.plot.sourceSeriesCount === 0
            text: qsTr("Nothing here yet. Press the plus beside a dataset in " +
                       "the tree, or add a line and write it out.")
            font: Theme.caption
            color: Theme.textDisabled
            wrapMode: Text.WordWrap
        }
    }

    QtObject {
        id: internal

        property string saveProblem: ""
        property string timeProblem: ""
        /// The view whose check is in flight, so the answer can be told from
        /// an answer about some other one.
        property string restoring: ""

        /// Put the tab's name in the box.
        function resetViewName() {
            viewName.text = panel.plot ? panel.plot.name : ""
            internal.saveProblem = ""
        }

        /// ...unless the reader is part way through writing something else
        /// there. A tab renamed from its own bar must not take the caret's
        /// line away mid-word.
        function offerPlotName() {
            if (!viewName.activeFocus)
                internal.resetViewName()
        }

        function save() {
            if (panel.plotIndex < 0)
                return
            const wanted = viewName.text.trim()
            if (wanted === "")
                return
            internal.saveProblem = AppController.customPlots.saveView(
                wanted, panel.plotIndex, panel.surface
                    ? panel.surface.drawingSettings() : ({}))
            // Back to the tab's name rather than empty: the box states what
            // saving now would be called, and after a save that is still the
            // tab's name. Set outright rather than offered -- the box still
            // has the caret, and what was in it has just been used up.
            if (internal.saveProblem === "")
                internal.resetViewName()
        }

        function commitTimeBase() {
            if (!panel.plot)
                return
            const wanted = timeBase.text.trim()
            if (wanted === panel.plot.xExpression) {
                internal.timeProblem = ""
                return
            }
            panel.plot.xExpression = wanted
            timeBase.text = panel.plot.xExpression
            internal.timeProblem = ""
        }
    }

    // The tab renamed, or a different tab put in front of this panel. Either
    // way the name the box is offering is no longer the right one.
    Connections {
        target: panel.plot

        function onNameChanged() { internal.offerPlotName() }
    }

    onPlotChanged: internal.offerPlotName()

    Connections {
        target: AppController.customPlots

        function onViewChecked(name, issues, reasons) {
            if (name !== internal.restoring)
                return
            internal.restoring = ""
            if (issues === 0) {
                AppController.customPlots.restoreView(name, panel.plotIndex)
                return
            }
            // Counted rather than listed in the prompt itself: the number is
            // the decision -- one stale line is a different question from
            // eleven -- and the reasons are there for a reader who wants them.
            restoreWarning.viewName = name
            restoreWarning.issues = issues
            restoreWarning.reasons = reasons
            restoreWarning.open()
        }

        function onViewRestored(index, settings) {
            if (index === panel.plotIndex && panel.surface)
                panel.surface.applyDrawingSettings(settings)
        }
    }

    RestoreViewDialog {
        id: restoreWarning

        onAccepted: AppController.customPlots.restoreView(restoreWarning.viewName,
                                                          panel.plotIndex)
    }
}
