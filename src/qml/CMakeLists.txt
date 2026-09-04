# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-License-Identifier: GPL-3.0-only

# The QML module. Files sit at the module root rather than in a subdirectory:
# qt_add_qml_module maps them into the URI's namespace itself, and a nested
# directory would be treated as an extra one needing its own qmldir.
set_source_files_properties(Theme.qml
  PROPERTIES QT_QML_SINGLETON_TYPE TRUE
)

qt_add_qml_module(appqml
  URI H5Scope
  VERSION 1.0
  RESOURCE_PREFIX /qt/qml
  QML_FILES
    Main.qml
    Theme.qml
    AppMenuBar.qml
    AppMenu.qml
    AppMenuItem.qml
    AppMenuSeparator.qml
    StatusStrip.qml
    ObjectTree.qml
    InfoView.qml
    DataView.qml
    TableSurface.qml
    PlotSurface.qml
    ImageSurface.qml
    ViewFooter.qml
    SliceField.qml
    ViewMessage.qml
    SettingsPanel.qml
    SettingRow.qml
    TableSettingsPanel.qml
    PlotSettingsPanel.qml
    ImageSettingsPanel.qml
    ValueGrid.qml
    TableSetupPanel.qml
    PostprocessPanel.qml
    PostprocessStepRow.qml
    TextPane.qml
    CompoundPane.qml
    FilePicker.qml
    AboutDialog.qml
    Panel.qml
    Badge.qml
    AppIcon.qml
    AppIconButton.qml
    AppComboBox.qml
    FilterInput.qml
    NumberField.qml
    RealField.qml
    FieldStepper.qml
    DatasetMemory.qml
    AppToolButton.qml
    AppToolTip.qml
    AppTabButton.qml
    ColorSwatchButton.qml
    PlotLegend.qml
    AppRadioButton.qml
    AppCheckBox.qml
    AppSlider.qml
    AppRangeSlider.qml
    RealRangeSlider.qml
    ValueRangeSetting.qml
    SelectableText.qml
)

# A static Qt resolves QML modules at link time rather than by scanning
# directories at run time, so every module this one imports is named here
# together with its plugin, or the engine reports it as missing.
#
#   Qt.labs.folderlistmodel  FilePicker.qml lists directories with it rather
#                            than handing the job to the platform's file
#                            dialog, so the picker is drawn from Theme like
#                            every other surface here.
#   QtGraphs                 PlotSurface.qml.
#   QtQuick.Shapes           AppIcon.qml draws the orientation controls'
#                            glyphs as vector paths, because the design system
#                            ships no icon font and this binary carries only
#                            the four text faces it actually sets.
#   QtQuick.Dialogs          ColorSwatchButton.qml, for the one control this
#                            application does not draw itself. See the note
#                            there: picking a colour out of a wheel is a solved
#                            problem and Qt ships the solution.
target_link_libraries(appqml PRIVATE
  Qt6::Quick
  gui
  Qt6::LabsFolderListModel
  Qt6::qmlfolderlistmodelplugin
  Qt6::Graphs
  Qt6::QuickShapes
  Qt6::qmlshapesplugin
  Qt6::QuickDialogs2
  Qt6::qtquickdialogsplugin
)
