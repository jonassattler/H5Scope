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
  src/Levels.cpp
  src/Parallel.cpp
  src/Projection.cpp
  src/Pyramid.cpp
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

if(PLOTCORE_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(PLOTCORE_INSTALL)
  include(CMakePackageConfigHelpers)
  install(TARGETS plotcore_core EXPORT plotcoreTargets
          ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
          LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
          RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
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
