# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-License-Identifier: GPL-3.0-only

# The example file the viewer is exercised against. A generator rather than a
# committed binary, so what the file contains is legible in source and is
# written by the same pinned HDF5 the application links. A library rather than
# only a program, so tests/test_example.cpp asserts against exactly the file
# `make-example-file` writes.
add_library(example_file STATIC ExampleFile.cpp)
target_include_directories(example_file PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(example_file PUBLIC HDF5::HDF5)
target_compile_features(example_file PUBLIC cxx_std_20)

add_executable(make-example-file make-example-file.cpp)
target_link_libraries(make-example-file PRIVATE example_file)

# What the application would show, printed. Drives the real models, so the
# output can be diffed against h5ls/h5dump to check the viewer against the file
# rather than against itself.
qt_add_executable(inspect-file inspect-file.cpp)
target_link_libraries(inspect-file PRIVATE gui Qt6::Gui Qt6::Graphs)
qt_import_plugins(inspect-file INCLUDE Qt6::QOffscreenIntegrationPlugin)

# Neither of these is a Qt type; the moc pass over them is pure cost.
set_target_properties(example_file make-example-file
                      PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
