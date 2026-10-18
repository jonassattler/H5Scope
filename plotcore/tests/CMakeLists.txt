# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-License-Identifier: GPL-3.0-only

# plotcore asserted with no file, no thread of its own and no window.
#
# Catch2 is whatever the including project found -- the viewer's comes from
# vcpkg -- and fetched at the version the viewer pins when plotcore is built
# on its own, so the two builds run the same framework.
find_package(Catch2 3 CONFIG QUIET)
if(NOT Catch2_FOUND)
  include(FetchContent)
  FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.15.3
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(Catch2)
  list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
endif()
include(Catch)

# --- what the plot decides before a pixel is touched ----------------------
# Arithmetic only: projectLine takes doubles and fills two vectors, so this
# needs no window, no engine and no file. The four things the plot got wrong
# for as long as it drew through Qt Graphs are all decided here, and a grab of
# something rendered is a poor way to assert any of them.
add_executable(test_plotprojection test_plotprojection.cpp)
target_link_libraries(test_plotprojection PRIVATE plotcore::core Catch2::Catch2WithMain)
catch_discover_tests(test_plotprojection)

# --- which runs to hold, and which to read next ---------------------------
# The fold a line is reduced by, the policy over the runs it is reduced into,
# and the pyramid that holds it at every resolution at once. Large enough in
# places that the build spreads over the pool, which is the path worth
# asserting rather than only the one small enough to stay on this thread.
add_executable(test_plotlevels test_plotlevels.cpp)
target_link_libraries(test_plotlevels PRIVATE plotcore::core Catch2::Catch2WithMain)
catch_discover_tests(test_plotlevels)
