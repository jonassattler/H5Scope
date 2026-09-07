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
#
# Named per platform rather than in one list, because these are targets and a
# target that the Qt for this platform does not build simply does not exist:
# leaving QXcbIntegrationPlugin in the list on Windows is not a plugin that
# never loads, it is a link against a name CMake cannot resolve, and the
# configure fails.
if(WIN32)
  set(h5scope_platform_plugins Qt6::QWindowsIntegrationPlugin)
else()
  set(h5scope_platform_plugins
    Qt6::QWaylandIntegrationPlugin
    Qt6::QWaylandXdgShellIntegrationPlugin
    Qt6::QXcbIntegrationPlugin
    Qt6::QXcbGlxIntegrationPlugin
  )
endif()

qt_import_plugins(H5Scope
  INCLUDE
    ${h5scope_platform_plugins}
    # Headless: CI smoke tests and any automation without a display. Built for
    # every platform Qt supports, so these are outside the branch above.
    Qt6::QOffscreenIntegrationPlugin
    Qt6::QMinimalIntegrationPlugin
)

# --- what the file itself says it is, on Windows ---------------------------
# The icon and the version block. Both live in a resource compiled into the
# executable, which is where Windows looks for them -- there is no equivalent
# of the .desktop file beside the binary, so an executable without this is one
# Explorer draws as a blank sheet and whose properties are empty.
#
# The version comes from cmake/Version.cmake through configure_file, so the
# Details tab and `--version` cannot disagree.
if(WIN32)
  # Harmless where CMake has already enabled it, decisive where it has not:
  # a .rc among a target's sources is silently ignored rather than compiled
  # if the RC language was never enabled, and the result is a build that
  # succeeds and an executable with no icon.
  enable_language(RC)

  # Forward slashes: rc.exe accepts them in a path, and a backslash inside a
  # resource string is an escape character.
  file(TO_CMAKE_PATH "${CMAKE_SOURCE_DIR}/packaging/h5scope.ico"
       H5SCOPE_ICON_PATH)
  configure_file(
    "${CMAKE_SOURCE_DIR}/packaging/h5scope.rc.in"
    "${CMAKE_BINARY_DIR}/generated/h5scope.rc"
    @ONLY)
  target_sources(H5Scope PRIVATE "${CMAKE_BINARY_DIR}/generated/h5scope.rc")
endif()

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
  # The name to keep is asked of the target rather than rebuilt from the
  # version, and the pattern carries the suffix. Spelling the name out worked
  # for exactly as long as the suffix was empty: on Windows the target is
  # H5Scope-0.3.2.exe and the name to keep was H5Scope-0.3.2, so the two did
  # not match and the step removed the executable it had just linked --
  #
  #   [242/244] Linking CXX executable bin\H5Scope-0.3.2.exe
  #   -- Removing superseded build H5Scope-0.3.2.exe
  #
  # -- leaving a build that succeeded, a test suite that passed, and nothing
  # to publish. The suffix in the pattern earns its place too: without it the
  # glob also matches H5Scope-0.3.2.pdb and this step deletes that instead.
  COMMAND ${CMAKE_COMMAND}
    -DPRUNE_DIR=${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
    -DPRUNE_PATTERN=H5Scope-*${CMAKE_EXECUTABLE_SUFFIX}
    -DPRUNE_KEEP=$<TARGET_FILE_NAME:H5Scope>
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
