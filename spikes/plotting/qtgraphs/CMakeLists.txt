# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-License-Identifier: GPL-3.0-only

qt_add_executable(spike-qtgraphs
  main.cpp
  QtGraphsSurface.cpp
)

# Qt is static here, exactly as it is for the application, so a QML module and
# every module the scene imports resolve at link time rather than at run time.
# Getting this wrong builds cleanly and then dies on the first frame with
# `module "QtGraphs" is not installed`.
qt_add_qml_module(spike-qtgraphs
  URI SpikeQtGraphs
  VERSION 1.0
  QML_FILES Main.qml
)

target_link_libraries(spike-qtgraphs PRIVATE
  spikecommon
  Qt6::Quick
  Qt6::Graphs
  Qt6::QuickShapes
  Qt6::qmlshapesplugin
)

qt_import_qml_plugins(spike-qtgraphs)
spike_import_platform_plugins(spike-qtgraphs)

# The correctness checks, as a test. Not wired into the application's CI: they
# need a real display, because the offscreen platform would run them through
# the software rasteriser and a correctness suite that measures a different
# renderer from the one that ships is worse than none.
add_test(NAME verify-qtgraphs
  COMMAND spike-qtgraphs --verify --out "${CMAKE_CURRENT_SOURCE_DIR}/../results")
set_tests_properties(verify-qtgraphs PROPERTIES RESOURCE_LOCK rendering TIMEOUT 600)
