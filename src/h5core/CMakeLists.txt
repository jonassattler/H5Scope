# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-License-Identifier: GPL-3.0-only

# Backend over the HDF5 C API. Deliberately free of any Qt dependency so the
# whole layer can be unit-tested headless.
add_library(h5core STATIC
  Types.cpp
  Error.cpp
  DataType.cpp
  File.cpp
  Image.cpp
  Dataset.cpp
  Attribute.cpp
)

target_include_directories(h5core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(h5core PUBLIC HDF5::HDF5)
target_compile_features(h5core PUBLIC cxx_std_20)
