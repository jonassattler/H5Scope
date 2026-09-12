# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-License-Identifier: GPL-3.0-only

qt_add_executable(spike-qcustomplot
  main.cpp
  QCustomPlotItem.cpp
  QCustomPlotSurface.cpp
)

qt_add_qml_module(spike-qcustomplot
  URI SpikeQCustomPlot
  VERSION 1.0
  QML_FILES Main.qml
  SOURCES QCustomPlotItem.hpp QCustomPlotItem.cpp
)

# Qt6::Widgets arrives through QCustomPlot::QCustomPlot's interface, and it is
# not optional: the class derives from QWidget. Named again here because this
# spike also constructs the QApplication itself.
target_link_libraries(spike-qcustomplot PRIVATE
  spikecommon
  Qt6::Quick
  Qt6::Widgets
  QCustomPlot::QCustomPlot
)

qt_import_qml_plugins(spike-qcustomplot)
spike_import_platform_plugins(spike-qcustomplot)

# The correctness checks, as a test. Not wired into the application's CI: they
# need a real display, because the offscreen platform would run them through
# the software rasteriser and a correctness suite that measures a different
# renderer from the one that ships is worse than none.
add_test(NAME verify-qcustomplot
  COMMAND spike-qcustomplot --verify --out "${CMAKE_CURRENT_SOURCE_DIR}/../results")
set_tests_properties(verify-qcustomplot PROPERTIES RESOURCE_LOCK rendering TIMEOUT 600)
