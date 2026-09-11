# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-License-Identifier: GPL-3.0-only

# The example file the viewer is exercised against. A generator rather than a
# committed binary, so what the file contains is legible in source and is
# written by the same pinned HDF5 the application links. A library rather than
# only a program, so tests/test_example.cpp asserts against exactly the file
# `make-example-file` writes.
add_library(example_file STATIC ExampleFile.cpp)
target_include_directories(example_file PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(example_file PUBLIC HDF5::HDF5)
target_compile_features(example_file PUBLIC cxx_std_20)

add_executable(make-example-file make-example-file.cpp)
target_link_libraries(make-example-file PRIVATE example_file)

# What the application would show, printed. Drives the real models, so the
# output can be diffed against h5ls/h5dump to check the viewer against the file
# rather than against itself.
qt_add_executable(inspect-file inspect-file.cpp)
target_link_libraries(inspect-file PRIVATE gui Qt6::Gui Qt6::Graphs)
qt_import_plugins(inspect-file INCLUDE Qt6::QOffscreenIntegrationPlugin)

# Neither of these is a Qt type; the moc pass over them is pure cost.
set_target_properties(example_file make-example-file
                      PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

# What the object tree costs, measured through the models the window binds to.
# Qt Core only: nothing here draws, and the numbers are about the model.
qt_add_executable(bench-tree bench-tree.cpp)
target_link_libraries(bench-tree PRIVATE gui Qt6::Core)
qt_import_plugins(bench-tree INCLUDE Qt6::QOffscreenIntegrationPlugin)

# ...and what the Data Viewer costs, once a dataset has been chosen. The other
# half of the same question, and a separate tool because the two have separate
# causes: the tree pays for the walk and this pays for the read. Both print the
# table in BenchReport.hpp, so they can be read side by side.
#
# Qt6::Gui because DatasetImage renders a QImage; nothing here opens a window.
qt_add_executable(bench-data bench-data.cpp)
target_link_libraries(bench-data PRIVATE gui postproc Qt6::Core Qt6::Gui)
qt_import_plugins(bench-data INCLUDE Qt6::QOffscreenIntegrationPlugin)

# The pictures in README.md, taken off the application's own window. See the
# header of make-screenshots.cpp: they went stale because they were taken by
# hand, and this is what makes them a thing the build produces instead.
#
# The same libraries the application links, because it *is* the application: it
# loads H5Scope/Main.qml exactly as src/main.cpp does, and a static Qt resolves
# QML modules at link time, so appqmlplugin and QtGraphs have to be named here
# for the same reason they are named there.
qt_add_executable(make-screenshots make-screenshots.cpp)
target_link_libraries(make-screenshots PRIVATE
  gui appqml appqmlplugin example_file
  Qt6::Quick Qt6::QuickControls2 Qt6::Graphs
)
# What the QML import scanner reads to decide which module plugins to link is
# the *target's* source directory, and there is not a line of QML in tools/.
# Without this it links none of them and the program dies at startup with
# 'module "QtQuick.Controls.Basic" is not installed' -- for a window whose QML
# is all in src/qml, which is where the scanner is pointed instead. Named as a
# path rather than as a list of plugins so that an import added to a view is
# picked up here by rebuilding rather than by remembering.
set_target_properties(make-screenshots PROPERTIES
  QT_QML_IMPORT_SCANNER_EXTRA_ROOT_PATHS ${CMAKE_SOURCE_DIR}/src/qml
)
qt_import_qml_plugins(make-screenshots)
# Headless by default, so the pictures do not depend on there being a desktop.
qt_import_plugins(make-screenshots INCLUDE Qt6::QOffscreenIntegrationPlugin)

# `cmake --build --preset release --target screenshots` refreshes what the
# README shows, in place. It rewrites only the files whose picture actually
# changed, so a run that changes nothing leaves `git status` clean.
#
# Not part of `all`: it writes into the source tree, which a build must never
# do unless it was asked to.
add_custom_target(screenshots
  COMMAND make-screenshots --out "${CMAKE_SOURCE_DIR}/docs/screenshots"
  COMMENT "Taking the README's screenshots off the application's window"
  USES_TERMINAL
  VERBATIM
)
