# SPDX-FileCopyrightText: 2026 Andreas Martin
# SPDX-License-Identifier: GPL-3.0-only
#
# The Qt-free plot library, compiled into the shared object ctypes loads.
# No vcpkg and no Qt: PlotWidget.cpp stays out, because it is a real widget.
# The same list is what build.bat and the wheel both compile, so the two
# cannot grow a second copy of the sources.
#
# From the repository:
#
#   python/h5plot/build.bat
#
#   cmake -S python/h5plot -B python/h5plot/build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
#   cmake --build python/h5plot/build
#
# A wheel build passes -DH5SCOPE_SRC. The relative default only works when
# this file is still sitting in the checkout; an sdist of python/ is not
# allowed to reach ../src.

cmake_minimum_required(VERSION 3.16)

# CMAKE_MSVC_RUNTIME_LIBRARY is ignored unless this policy is NEW, and the
# try_compile project() runs has to use the same runtime as the library or
# the two disagree about which CRT they were linked with.
cmake_policy(SET CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(h5plot LANGUAGES CXX)

# The static runtime is what lets a Windows wheel be one DLL. The DLL
# runtime would need VCRUNTIME140.dll beside it, and delvewheel, which
# repairs extension modules, does not pick up a ctypes library.
if(NOT H5SCOPE_SRC)
  set(H5SCOPE_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
endif()

add_library(h5plot SHARED
  "${H5SCOPE_SRC}/gui/PlotProjection.cpp"
  "${H5SCOPE_SRC}/gui/PlotLevels.cpp"
  "${H5SCOPE_SRC}/gui/PlotPyramid.cpp"
  "${H5SCOPE_SRC}/gui/PlotColumns.cpp"
  "${H5SCOPE_SRC}/plotcore/LineStore.cpp"
  "${H5SCOPE_SRC}/plotcore/View.cpp"
  "${H5SCOPE_SRC}/plotcore/plotcore_c.cpp"
)

target_include_directories(h5plot PRIVATE
  "${H5SCOPE_SRC}/plotcore/stubs"
  "${H5SCOPE_SRC}"
)
target_compile_features(h5plot PRIVATE cxx_std_20)
target_compile_definitions(h5plot PRIVATE H5PLOT_EXPORT)
if(MSVC)
  # On the target, and again as a bare /MT. CMAKE_MSVC_RUNTIME_LIBRARY is
  # set above, and it is still not what ends up on the command line when a
  # /MD from the release flags is the later switch: that one wins. The DLL
  # then imports VCRUNTIME140.dll. ctypes does not search PATH, so the copy
  # that lives next to the Visual Studio tools is invisible and the loader
  # reports that this DLL could not be found.
  set_target_properties(h5plot PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  target_compile_options(h5plot PRIVATE
    "$<$<CONFIG:Debug>:/MTd>"
    "$<$<NOT:$<CONFIG:Debug>>:/MT>")
endif()
if(MINGW)
  # -static-libstdc++ leaves libwinpthread-1.dll: g++ appends -lpthread
  # after the objects and libstdc++, and without -static that resolves to
  # the import library. An archive named in the link options comes before
  # the objects, is scanned while nothing needs it yet, and is dropped.
  # -static makes the trailing -lpthread pick libwinpthread.a. TDM's win32
  # thread model has no winpthread at all, and -static is harmless there.
  target_link_options(h5plot PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# Flat, including the multi-config generators. Visual Studio otherwise nests
# the DLL under Release/ and the copy into the package looks in the wrong place.
set(_h5plot_out "${CMAKE_BINARY_DIR}")
set_target_properties(h5plot PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${_h5plot_out}"
  LIBRARY_OUTPUT_DIRECTORY "${_h5plot_out}"
  RUNTIME_OUTPUT_DIRECTORY_DEBUG "${_h5plot_out}"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${_h5plot_out}"
  RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${_h5plot_out}"
  RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${_h5plot_out}"
  LIBRARY_OUTPUT_DIRECTORY_DEBUG "${_h5plot_out}"
  LIBRARY_OUTPUT_DIRECTORY_RELEASE "${_h5plot_out}"
  LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO "${_h5plot_out}"
  LIBRARY_OUTPUT_DIRECTORY_MINSIZEREL "${_h5plot_out}"
)
if(WIN32)
  # MinGW's prefix is "lib", which would ship libh5plot.dll. The loader on
  # Windows asks for h5plot.dll, which is the name MSVC already produces.
  set_target_properties(h5plot PROPERTIES PREFIX "")
endif()
