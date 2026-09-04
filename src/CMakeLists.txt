# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-License-Identifier: GPL-3.0-only

add_subdirectory(h5core)
add_subdirectory(postproc)
add_subdirectory(gui)
add_subdirectory(qml)

qt_add_executable(H5Scope
  main.cpp
)

# appqmlplugin is linked explicitly: qt_import_qml_plugins() decides what to
# import by scanning a target's own QML files, and this executable has none --
# it resolves the module by URI at run time in main.cpp, which the scanner
# cannot see. Without this the static binary starts and then fails with
# 'No module named "H5Scope" found'.
target_link_libraries(H5Scope PRIVATE
  gui
  appqml
  appqmlplugin
  Qt6::QuickControls2
)

# A static Qt resolves QML modules at link time rather than by scanning
# directories at run time, so every module the app imports has to be pulled in
# explicitly or the engine reports them as missing.
qt_import_qml_plugins(H5Scope)

# A static Qt resolves platform plugins at link time, so every platform the
# binary must run on has to be named here -- there is no plugin directory to
# fall back on at run time. Importing only offscreen, as this once did, built a
# binary that aborted at startup on any real desktop with
#   "Could not find the Qt platform plugin ... in \"\""
# which is the single way a self-contained executable can fail to be
# self-contained.
#
# Wayland needs its shell integration alongside the platform plugin: without a
# shell the client connects and then never gets a surface.
qt_import_plugins(H5Scope
  INCLUDE
    Qt6::QWaylandIntegrationPlugin
    Qt6::QWaylandXdgShellIntegrationPlugin
    Qt6::QXcbIntegrationPlugin
    Qt6::QXcbGlxIntegrationPlugin
    # Headless: CI smoke tests and any automation without a display.
    Qt6::QOffscreenIntegrationPlugin
    Qt6::QMinimalIntegrationPlugin
)

# The version is in the file name, so a directory of builds is self-describing
# and a downloaded artefact still says what it is once it has left the page it
# came from.
set_target_properties(H5Scope PROPERTIES
  OUTPUT_NAME "H5Scope-${H5SCOPE_VERSION}"
  WIN32_EXECUTABLE ON
  MACOSX_BUNDLE OFF
)

# ...which would otherwise leave one executable per commit built in this tree.
# This drops the superseded ones and points a stable `H5Scope` at the
# current build, so there is always one name that means "what I just built".
add_custom_command(TARGET H5Scope POST_BUILD
  COMMAND ${CMAKE_COMMAND}
    -DPRUNE_DIR=${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
    -DPRUNE_PATTERN=H5Scope-*
    -DPRUNE_KEEP=H5Scope-${H5SCOPE_VERSION}
    -DPRUNE_LINK=H5Scope
    -P ${CMAKE_SOURCE_DIR}/cmake/PruneVersionedBinaries.cmake
  VERBATIM
)

# The licence, the inventory and the notices, laid down beside the executable
# they belong to. They are compiled into the binary as well -- `--license` and
# `--notices` -- but a file next to it can be read without running anything,
# which is what a licence is for, and this is what the release attaches.
#
# Done by the build rather than by CI so that a local release build is the same
# artefact as a published one, and so the two can never disagree about which
# notices go with which binary. The prune step above only ever touches
# H5Scope-*, so these are safe in the same directory.
add_custom_command(TARGET H5Scope POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_SOURCE_DIR}/LICENSE"
    "${CMAKE_SOURCE_DIR}/THIRD-PARTY-NOTICES.md"
    "${H5SCOPE_THIRD_PARTY_LICENSES}"
    "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
  VERBATIM
)
