# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-License-Identifier: GPL-3.0-only

qt_add_library(gui STATIC
  AppController.cpp
  H5Thread.cpp
  H5Session.cpp
  H5TreeModel.cpp
  DatasetTableModel.cpp
  AttributeTableModel.cpp
  ObjectInfoModel.cpp
  TreeFilterProxyModel.cpp
  DatasetStringListModel.cpp
  DatasetPlot.cpp
  DatasetImage.cpp
  DatasetImageProvider.cpp
  TableLayout.cpp
  TableSetupModel.cpp
  PostprocessModel.cpp
  FileSystem.cpp
  FocusRelease.cpp
  EmbeddedFonts.cpp
)

# The design system's typefaces travel inside the binary. A GUI application
# that picks its fonts up from the host is exactly as system-dependent as one
# that picks its libraries up from the host, and this project's whole premise
# is that it does neither. IBM Plex is SIL OFL 1.1; fonts/LICENSE.txt ships the
# terms alongside the files.
#
# The licence is a resource rather than a file beside the binary for the same
# reason the fonts are: the OFL requires its terms to accompany the font
# software, and a single self-contained executable is the only thing this
# project distributes. The About dialog reads it back out of :/fonts.
qt_add_resources(gui "fonts"
  PREFIX "/fonts"
  BASE "fonts"
  FILES
    fonts/IBMPlexSans-Regular.ttf
    fonts/IBMPlexSans-SemiBold.ttf
    fonts/IBMPlexMono-Regular.ttf
    fonts/IBMPlexMono-Medium.ttf
    fonts/LICENSE.txt
)

# The project's own licence and the notices its dependencies require, on the
# same reasoning that put the OFL text above into the binary rather than beside
# it -- and for a stronger reason, because the GPL is the licence this whole
# executable is conveyed under.
#
# GPL-3.0-only section 6 conveys object code under the terms of sections 4 and
# 5, and section 4 says a copy of the License must be given to all recipients
# along with the Program. A release attaches these files too, but the described
# way to use H5Scope is to download one executable and run it, and that
# reader has nowhere else to look. `--license` and `--notices` in main.cpp are
# how they are read back out.
#
# Absolute paths, because two of the three are outside this directory: the
# aliases are what give them a name inside `:/licenses`.
set(licence_files
  "${CMAKE_SOURCE_DIR}/LICENSE"
  "${CMAKE_SOURCE_DIR}/THIRD-PARTY-NOTICES.md"
  "${H5SCOPE_THIRD_PARTY_LICENSES}"
)
set_source_files_properties("${CMAKE_SOURCE_DIR}/LICENSE"
  PROPERTIES QT_RESOURCE_ALIAS "GPL-3.0.txt")
set_source_files_properties("${CMAKE_SOURCE_DIR}/THIRD-PARTY-NOTICES.md"
  PROPERTIES QT_RESOURCE_ALIAS "THIRD-PARTY-NOTICES.md")
set_source_files_properties("${H5SCOPE_THIRD_PARTY_LICENSES}"
  PROPERTIES QT_RESOURCE_ALIAS "THIRD-PARTY-LICENSES.txt")

qt_add_resources(gui "licenses"
  PREFIX "/licenses"
  FILES ${licence_files}
)

target_include_directories(gui PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/..
  # The generated version header, so `#include "h5scope/Version.hpp"` works
  # from here and from anything that links this.
  ${CMAKE_BINARY_DIR}/generated
)
# Qt6::Graphs for the plot presentation: DatasetPlot loads a QXYSeries in one
# bulk replace, which is the only way to fill a graph of thousands of points
# without a redraw per point. Graphs rather than Charts because Charts draws
# through the Qt Widgets graphics scene and would require a QApplication in
# what is otherwise a pure Qt Quick application. PUBLIC because
# DatasetPlot.hpp is part of this library's interface.
target_link_libraries(gui PUBLIC Qt6::Quick Qt6::Qml Qt6::Graphs h5core postproc)

# AppController is declared QML_ELEMENT/QML_SINGLETON, so the module needs a
# URI matching the QML import below.
qt_add_qml_module(gui
  URI H5Scope.Backend
  VERSION 1.0
)
