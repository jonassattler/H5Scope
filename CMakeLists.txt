# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-License-Identifier: GPL-3.0-only

cmake_minimum_required(VERSION 3.26)

# --- vcpkg toolchain resolution -------------------------------------------
# Every dependency (Qt, HDF5, Catch2) is built and pinned by vcpkg. The system
# libraries are deliberately never used, so a toolchain file is mandatory.
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "vcpkg toolchain file")
  else()
    message(FATAL_ERROR
      "VCPKG_ROOT is not set and no CMAKE_TOOLCHAIN_FILE was given.\n"
      "This project never links against system libraries; all dependencies come "
      "from vcpkg. Set it up with:\n"
      "  export VCPKG_ROOT=/path/to/vcpkg\n"
      "then re-run: cmake --preset debug")
  endif()
endif()

# --- version --------------------------------------------------------------
# Counted from the history, not typed. See cmake/Version.cmake.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Version)
h5scope_resolve_version()

project(H5Scope VERSION ${H5SCOPE_VERSION} LANGUAGES CXX C)

message(STATUS "H5Scope ${H5SCOPE_VERSION_STRING} (${H5SCOPE_COMMIT})")

# One generated header, reachable from anything that links `gui`. Written into
# the build tree so a version bump never shows up as a source change.
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/src/Version.hpp.in"
  "${CMAKE_BINARY_DIR}/generated/h5scope/Version.hpp"
  @ONLY)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# --- the C++ runtime travels too -------------------------------------------
# The oldest system this project supports is RHEL 8, which is glibc 2.28 and
# libstdc++ from GCC 8.5. C++20 needs a compiler far newer than that, so the
# release is built on RHEL 8 with gcc-toolset-13 (tools/ci/el8.Dockerfile) --
# and a GCC 13 that links libstdc++ dynamically produces a binary needing
# GLIBCXX_3.4.31, which the 8.5 runtime beside it does not define. It builds,
# it passes its tests on the build machine, and it refuses to start on the
# target with a message about a version not found.
#
# Linking the C++ runtime statically settles it: the new library is inside the
# executable, and nothing is asked of the host's libstdc++ because the host's
# libstdc++ is not opened. It also costs nothing this project was not already
# paying -- Qt and HDF5 are static here for the same reason, and one more
# static library on a self-contained binary is the consistent answer rather
# than a special case.
#
# tools/check-glibc-floor.sh is what proves it stayed true; CI runs it over
# every executable the release publishes.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  add_link_options(-static-libstdc++ -static-libgcc)
endif()

if(PROJECT_IS_TOP_LEVEL)
  set_property(GLOBAL PROPERTY USE_FOLDERS ON)
endif()

# --- dependencies ----------------------------------------------------------
# Graphs rather than Charts for the Data Viewer's plot: Charts is the
# QGraphicsView-era module -- its ChartView wraps a QGraphicsWidget and asserts
# without a QApplication, which is the whole Qt Widgets stack dragged into a Qt
# Quick application to draw polylines. Graphs renders on the scene graph and
# needs none of it.
find_package(Qt6 CONFIG REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 QuickTest Test
                                           LabsFolderListModel Graphs QuickShapes
                                           QuickDialogs2)
# Module mode is deliberate: vcpkg's hdf5 wrapper hooks find_package(HDF5) to
# set HDF5_USE_STATIC_LIBRARIES and define H5_BUILT_AS_STATIC_LIB for us.
find_package(HDF5 REQUIRED COMPONENTS C)

qt_standard_project_setup()

# --- licences --------------------------------------------------------------
# A self-contained executable is the only thing this project distributes, so
# the notices its dependencies require have to travel inside it. This gathers
# the licence text of every port on the link line into one generated file; that
# file, LICENSE and THIRD-PARTY-NOTICES.md are compiled in as resources by
# src/gui, and `--license` and `--notices` print them back out. See
# cmake/ThirdPartyLicenses.cmake for why the inventory is not the notice.
include(ThirdPartyLicenses)
set(H5SCOPE_THIRD_PARTY_LICENSES
    "${CMAKE_BINARY_DIR}/generated/THIRD-PARTY-LICENSES.txt")
h5scope_collect_third_party_licenses("${H5SCOPE_THIRD_PARTY_LICENSES}")

# QML modules must live at a path mirroring their URI, or qmllint and the QML
# tooling cannot resolve imports between them. Setting one root makes Qt place
# each module at <root>/<uri-as-path> automatically.
set(QT_QML_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/qml")

add_subdirectory(src)
add_subdirectory(tools)

include(CTest)
if(BUILD_TESTING)
  find_package(Catch2 3 CONFIG REQUIRED)
  add_subdirectory(tests)
endif()
