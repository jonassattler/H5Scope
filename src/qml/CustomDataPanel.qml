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
        Repeater {
            model: AppController.customPlots.viewNames

            delegate: RowLayout {
                id: viewRow

                required property string modelData

                width: parent.width
                spacing: Theme.gapS

                Text {
                    id: viewLabel

                    Layout.fillWidth: true
                    text: viewRow.modelData
                    font: Theme.bodySmall
                    color: Theme.textPrimary
                    elide: Text.ElideMiddle
                    verticalAlignment: Text.AlignVCenter

                    HoverHandler { id: viewHover }

                    AppToolTip {
                        shown: viewLabel.truncated && viewHover.hovered
                        verbatim: true
                        text: viewLabel.text
                    }
                }

                AppToolButton {
                    objectName: "restoreView"

                    /// Which view this row is for, so the suite can press the
                    /// right one of several.
                    readonly property string viewName: viewRow.modelData

                    text: qsTr("restore")
                    size: "sm"
                    enabled: panel.plotIndex >= 0
                    // Checked before it is applied, and the check has to ask
                    // the file: a view can name a dataset this session has
                    // never looked at. The answer arrives at onViewChecked
                    // below.
                    onClicked: {
                        internal.restoring = viewRow.modelData
                        AppController.customPlots.checkView(viewRow.modelData)
                    }
                }

                AppIconButton {
                    glyph: "close"
                    ink: Theme.danger
                    bare: true
                    hint: qsTr("forget this view")
                    onClicked: AppController.customPlots.removeView(viewRow.modelData)
                }
            }
        }

        Text {
            width: parent.width
            visible: AppController.customPlots.viewNames.length === 0
            text: qsTr("Nothing saved yet. A view keeps the lines, the x axis " +
                       "and the colours, and can be put into any custom plot.")
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

        Repeater {
            id: entries

            objectName: "entryRows"

            model: panel.plot

            // A plain wrapper taking the roles off the model and handing them
            // down, because a required property cannot also be one the
            // component already declares. PostprocessPanel does the same.
            delegate: Item {
                id: holder

                required property int index
                required property string expression
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
                    error: holder.error
                    scaling: holder.scaling
                    scalable: holder.scalable
                    drawn: holder.drawn
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

        function save() {
            if (panel.plotIndex < 0)
                return
            const wanted = viewName.text.trim()
            if (wanted === "")
                return
            internal.saveProblem = AppController.customPlots.saveView(
                wanted, panel.plotIndex, panel.surface
                    ? panel.surface.drawingSettings() : ({}))
            if (internal.saveProblem === "")
                viewName.text = ""
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
