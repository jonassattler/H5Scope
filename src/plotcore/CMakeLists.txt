# SPDX-FileCopyrightText: 2026 Andreas Martin
# SPDX-License-Identifier: GPL-3.0-only

# The plot without a window manager, a file or a QML engine.
#
# PlotProjection, PlotLevels and PlotPyramid already lived in src/gui and were
# already free of HDF5 and of the scene graph -- that is why they can be tested
# with no window. They stay in those headers so DatasetPlot and CustomPlot keep
# including what they always included. This target is the library they compile
# into, plus the QWidget host and the RAM store that sit on the same arithmetic.
#
# gui links this rather than compiling the three sources itself. A second
# copy of the same .cpp would be two answers to one question.

add_library(plotcore STATIC
  "${CMAKE_CURRENT_SOURCE_DIR}/../gui/PlotProjection.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/../gui/PlotLevels.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/../gui/PlotPyramid.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/../gui/PlotColumns.cpp"
  PlotWidget.cpp
  LineStore.cpp
  View.cpp
)

target_include_directories(plotcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(plotcore PUBLIC Qt6::Widgets)
target_compile_features(plotcore PUBLIC cxx_std_20)
