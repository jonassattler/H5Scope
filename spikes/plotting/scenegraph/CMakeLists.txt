# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-License-Identifier: GPL-3.0-only

qt_add_executable(spike-scenegraph
  main.cpp
  PlotItem.cpp
  SceneGraphSurface.cpp
)

# PlotItem is QML_ELEMENT, so it is registered into this module's URI and the
# scene can declare one. Nothing else here imports anything Qt Quick does not
# already carry -- which is the whole point of the variant being measured.
qt_add_qml_module(spike-scenegraph
  URI SpikeSceneGraph
  VERSION 1.0
  QML_FILES Main.qml
  SOURCES PlotItem.hpp PlotItem.cpp
)

target_link_libraries(spike-scenegraph PRIVATE
  spikecommon
  Qt6::Quick
)

qt_import_qml_plugins(spike-scenegraph)
spike_import_platform_plugins(spike-scenegraph)

# The correctness checks, as a test. Not wired into the application's CI: they
# need a real display, because the offscreen platform would run them through
# the software rasteriser and a correctness suite that measures a different
# renderer from the one that ships is worse than none.
add_test(NAME verify-scenegraph
  COMMAND spike-scenegraph --verify --out "${CMAKE_CURRENT_SOURCE_DIR}/../results")
set_tests_properties(verify-scenegraph PROPERTIES RESOURCE_LOCK rendering TIMEOUT 600)
