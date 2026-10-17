# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-License-Identifier: GPL-3.0-only

# The plot without a window, a file or a toolkit.
#
# Everything the viewer decides about a line before a pixel is touched -- the
# envelope it is folded into, the pyramid every zoom is folded out of, the
# policy over which runs to hold, where each point lands and how a stroke is
# built out of triangles -- is arithmetic over doubles, and it is all here. The
# viewer is one of its users: it reaches this with add_subdirectory() and links
# plotcore::core like anything else would.
#
# A project of its own, buildable with nothing but CMake and a C++20 compiler:
# no vcpkg, no Qt, no HDF5. That is what lets the C++, Python and Rust
# libraries built on it stand alone, and what CI holds it to.

cmake_minimum_required(VERSION 3.26)
project(plotcore VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)

# On when this is the top level, and whatever the including project says when
# it is not -- a normal variable set before add_subdirectory() is honoured,
# which is how the viewer hands over its own BUILD_TESTING.
option(PLOTCORE_BUILD_TESTS "Build plotcore's tests" ${PROJECT_IS_TOP_LEVEL})
# Off inside another project: the viewer installs one executable and nothing
# that looks like a development package.
option(PLOTCORE_INSTALL "Install plotcore's headers, library and CMake package"
       ${PROJECT_IS_TOP_LEVEL})

# A wheel's library is loaded by whatever Python is on the machine, and Python
# ships the C runtime but not the C++ one (MSVCP140.dll): so inside a wheel the
# MSVC runtime is linked in, as -static-libstdc++ links libstdc++ in below. Not
# otherwise, because a static archive's runtime is its consumers' to match.
if(SKBUILD AND MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(PROJECT_IS_TOP_LEVEL)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
  if(MSVC)
    add_compile_options(/W4 /utf-8)
  else()
    add_compile_options(-Wall -Wextra)
  endif()
endif()

find_package(Threads REQUIRED)

add_library(plotcore_core
  src/Budget.cpp
  src/Camera.cpp
  src/Figure.cpp
  src/Format.cpp
  src/Json.cpp
  src/Levels.cpp
  src/Palettes.cpp
  src/Parallel.cpp
  src/Plot.cpp
  src/Projection.cpp
  src/Pyramid.cpp
  src/Ticks.cpp
)
add_library(plotcore::core ALIAS plotcore_core)
set_target_properties(plotcore_core PROPERTIES
  EXPORT_NAME core
  OUTPUT_NAME plotcore
  # A static archive that a shared library -- the C interface, a Python
  # module, a Rust crate -- can be linked into.
  POSITION_INDEPENDENT_CODE ON
)
target_compile_features(plotcore_core PUBLIC cxx_std_20)
target_include_directories(plotcore_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
# The pool's threads. PUBLIC because a static archive's dependencies are its
# user's to link.
target_link_libraries(plotcore_core PUBLIC Threads::Threads)

# --- the C interface ------------------------------------------------------
# What the Python and Rust libraries are built on: plotcore::Figure behind a
# C header, as one shared library that exports that header and nothing else.
# Off inside the viewer, which links the C++ directly.
option(PLOTCORE_BUILD_CAPI "Build plotcore_c, the C interface" ${PROJECT_IS_TOP_LEVEL})
if(PLOTCORE_BUILD_CAPI)
  add_library(plotcore_c SHARED capi/plotcore.cpp)
  add_library(plotcore::c ALIAS plotcore_c)
  set_target_properties(plotcore_c PROPERTIES
    EXPORT_NAME c
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    PUBLIC_HEADER capi/plotcore.h
  )
  if(NOT SKBUILD)
    # Versioned where it is installed as a library. Not inside a wheel, which
    # is a zip: the two symlinks would go into it as two more copies.
    set_target_properties(plotcore_c PROPERTIES
      VERSION ${PROJECT_VERSION}
      SOVERSION ${PROJECT_VERSION_MAJOR})
  endif()
  target_compile_definitions(plotcore_c PRIVATE
    PLOTCORE_VERSION_STRING="${PROJECT_VERSION}")
  target_include_directories(plotcore_c PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/capi>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  )
  target_link_libraries(plotcore_c PRIVATE plotcore::core)
  if(NOT MSVC AND NOT APPLE)
    # One file to hand to Python or Rust: the C++ runtime inside it rather
    # than beside it, for the release floor's reason (CLAUDE.md, invariant 4).
    target_link_options(plotcore_c PRIVATE -static-libstdc++ -static-libgcc
                        LINKER:--exclude-libs,ALL)
  endif()
endif()

# --- the Qt adapter -------------------------------------------------------
# plotcore::Figure in a QWidget, and the example that walks every feature on
# one. Off unless asked for, because it is the one part of this project that
# needs a toolkit; the viewer asks for it, so CI builds and runs it against the
# viewer's own Qt.
option(PLOTCORE_WITH_QT "Build plotcore::qt and its example" OFF)
if(PLOTCORE_WITH_QT)
  find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets)
  add_library(plotcore_qt STATIC
    qt/src/PlotWidget.cpp
    qt/include/plotcore/qt/PlotWidget.hpp
  )
  add_library(plotcore::qt ALIAS plotcore_qt)
  set_target_properties(plotcore_qt PROPERTIES EXPORT_NAME qt AUTOMOC ON)
  target_include_directories(plotcore_qt PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/qt/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  )
  target_link_libraries(plotcore_qt PUBLIC plotcore::core Qt6::Widgets)
  add_subdirectory(examples/qt)
endif()

if(PLOTCORE_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

# --- a Python wheel ---------------------------------------------------------
# scikit-build-core, from plotcore/python/pyproject.toml: the C interface and
# nothing else, beside the package that loads it with ctypes. It links no
# Python, which is what lets one wheel per platform serve every version.
if(SKBUILD)
  if(NOT PLOTCORE_BUILD_CAPI)
    message(FATAL_ERROR "a wheel is the C interface: PLOTCORE_BUILD_CAPI must be ON")
  endif()
  install(TARGETS plotcore_c
          LIBRARY DESTINATION plotcore
          RUNTIME DESTINATION plotcore)
endif()

if(PLOTCORE_INSTALL)
  include(CMakePackageConfigHelpers)
  install(TARGETS plotcore_core EXPORT plotcoreTargets
          ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
          LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
          RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  if(PLOTCORE_BUILD_CAPI)
    install(TARGETS plotcore_c EXPORT plotcoreTargets
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  endif()
  install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  install(EXPORT plotcoreTargets NAMESPACE plotcore::
          DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/plotcore)
  configure_package_config_file(cmake/plotcoreConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/plotcoreConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/plotcore)
  write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/plotcoreConfigVersion.cmake"
    COMPATIBILITY SameMinorVersion)
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/plotcoreConfig.cmake"
                "${CMAKE_CURRENT_BINARY_DIR}/plotcoreConfigVersion.cmake"
          DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/plotcore)
endif()
