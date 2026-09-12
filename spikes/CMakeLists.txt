# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-License-Identifier: GPL-3.0-only

# The plotting bake-off. A separate project on purpose: the root
# CMakeLists.txt does not add_subdirectory() this, so nothing here can change
# what H5Scope builds, links or ships. See spikes/README.md.

cmake_minimum_required(VERSION 3.26)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "vcpkg toolchain file")
  else()
    message(FATAL_ERROR
      "VCPKG_ROOT is not set and no CMAKE_TOOLCHAIN_FILE was given.\n"
      "The spikes resolve their dependencies exactly as the application does:\n"
      "  export VCPKG_ROOT=/path/to/vcpkg\n"
      "then re-run: cmake --preset spike-release -S spikes")
  endif()
endif()

project(H5ScopePlotSpikes VERSION 0.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# The same warnings the application builds under, for the same reason: these
# are for the person reading the build. Not -Werror.
if(NOT MSVC)
  add_compile_options(-Wall -Wextra)
endif()

# Graphs for the baseline spike, QuickShapes because Qt Graphs draws its 2-D
# series through it, Widgets because QCustomPlot is a QWidget-era library and
# its headers include <QWidget> whether or not one is ever shown.
find_package(Qt6 CONFIG REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Graphs
                                            QuickShapes Widgets PrintSupport)
# QCustomPlot's vcpkg port is built with qmake and installs a header and a
# static archive -- no CMake package config at all -- so it is found by hand and
# wrapped in an imported target, which is what lets the spike that uses it read
# like every other target here.
#
# Not QUIET and not optional: a bake-off missing one of its three candidates
# should fail to configure, not build two thirds of itself and produce a table
# with a hole in it.
find_path(QCUSTOMPLOT_INCLUDE_DIR qcustomplot.h REQUIRED)
find_library(QCUSTOMPLOT_LIBRARY NAMES qcustomplot REQUIRED)
add_library(QCustomPlot::QCustomPlot STATIC IMPORTED)
set_target_properties(QCustomPlot::QCustomPlot PROPERTIES
  IMPORTED_LOCATION "${QCUSTOMPLOT_LIBRARY}"
  INTERFACE_INCLUDE_DIRECTORIES "${QCUSTOMPLOT_INCLUDE_DIR}"
  # QCustomPlot is a QWidget. Nothing here ever shows one -- the spike renders
  # it through toPainter() into a QQuickPaintedItem -- but the class derives
  # from QWidget and the headers include <QtWidgets/QWidget>, so the whole
  # widgets library is on the link line whether or not a widget is created.
  # That is itself part of the answer this bake-off is meant to produce.
  # ...and Qt6::PrintSupport, which is not optional either. QCustomPlot's
  # savePdf() is compiled whenever QtPrintSupport's headers were present when
  # the port was built, and vcpkg's were, so the archive carries unresolved
  # references to QPrinter whether or not anything calls it:
  #   undefined reference to `QPrinter::printEngine() const'
  # On Linux that module links against CUPS, which is a system library this
  # project's release floor does not otherwise require -- and is the kind of
  # thing a self-contained binary is meant not to have.
  INTERFACE_LINK_LIBRARIES "Qt6::Widgets;Qt6::PrintSupport;Qt6::Gui;Qt6::Core"
)
message(STATUS "QCustomPlot: ${QCUSTOMPLOT_LIBRARY}")

qt_standard_project_setup(REQUIRES 6.5)

enable_testing()

# A static Qt resolves platform plugins at link time -- there is no plugin
# directory to fall back on -- so every platform a spike must open a window on
# is named here. The application says the same thing in src/CMakeLists.txt:30;
# leaving it out builds a binary that starts and then reports
#   Could not find the Qt platform plugin "wayland" in ""
# and quietly falls through to whichever plugin it did link, which for a
# benchmark means measuring a different stack from the one that ships.
function(spike_import_platform_plugins target)
  qt_import_plugins(${target}
    INCLUDE
      Qt6::QWaylandIntegrationPlugin
      Qt6::QWaylandXdgShellIntegrationPlugin
      Qt6::QXcbIntegrationPlugin
      Qt6::QXcbGlxIntegrationPlugin
      Qt6::QOffscreenIntegrationPlugin
      Qt6::QMinimalIntegrationPlugin
  )
endfunction()

add_subdirectory(plotting/common)
add_subdirectory(plotting/qtgraphs)
add_subdirectory(plotting/scenegraph)
add_subdirectory(plotting/qcustomplot)
add_subdirectory(plotting/verify)
