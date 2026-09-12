# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-License-Identifier: GPL-3.0-only

# Everything the three spikes share: the data they draw, the clock that times
# them, the counter that counts what they allocate, and the predicates that say
# whether what landed on screen is the truth. Kept in one library so that a
# difference between two spikes' numbers is a difference between two
# renderers and not between two harnesses.

add_library(spikecommon STATIC
  Allocations.cpp
  FrameTimer.cpp
  ImageChecks.cpp
  PlotBench.cpp
  ScriptedGesture.cpp
  SpikeMain.cpp
  SpikeRunner.cpp
  SyntheticSource.cpp
  Verify.cpp
)

target_include_directories(spikecommon PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
# Qt6::Widgets because SpikeMain can construct a QApplication -- only the
# QCustomPlot spike asks it to, and the branch has to compile in all three.
target_link_libraries(spikecommon PUBLIC Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Widgets)
set_target_properties(spikecommon PROPERTIES AUTOMOC ON)
