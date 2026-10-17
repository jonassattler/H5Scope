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

# --- the whole plot, as a host drives it ----------------------------------
# The store, the frame and the gestures, through the same calls a Qt, Python
# or Rust host makes and checked in the pixels and the elements they hand
# back, because a host sees nothing else.
add_executable(test_plotfigure test_plotfigure.cpp)
target_link_libraries(test_plotfigure PRIVATE plotcore::core Catch2::Catch2WithMain)
catch_discover_tests(test_plotfigure)

# --- the C interface, from C ----------------------------------------------
# Compiled as C, because a header only a C++ compiler accepts is no C
# interface; and run over the golden script, whose digest the Python and Rust
# bindings are held to (golden/README.md).
if(PLOTCORE_BUILD_CAPI)
  enable_language(C)
  add_executable(test_capi test_capi.c)
  set_target_properties(test_capi PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
  target_link_libraries(test_capi PRIVATE plotcore::c)
  if(NOT WIN32)
    target_link_libraries(test_capi PRIVATE m)
  endif()
  add_test(NAME capi
           COMMAND test_capi ${CMAKE_CURRENT_SOURCE_DIR}/golden/script.txt
                             ${CMAKE_CURRENT_SOURCE_DIR}/golden/expected.txt)
  # The library beside the test on Windows, where nothing else finds a DLL.
  set_tests_properties(capi PROPERTIES ENVIRONMENT_MODIFICATION
                       "PATH=path_list_prepend:$<TARGET_FILE_DIR:plotcore_c>")
endif()
