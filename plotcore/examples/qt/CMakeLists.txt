# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-License-Identifier: GPL-3.0-only

# Every feature plotcore has, on one Qt Widgets window. Run with --self-test to
# have it walk them itself, which is what CI does.
qt_add_executable(plotcore-qt-demo
  main.cpp
  DemoWindow.cpp
  DemoWindow.hpp
  Signals.cpp
  Signals.hpp
)
target_link_libraries(plotcore-qt-demo PRIVATE plotcore::qt)

# A static Qt resolves its platform plugins at link time (CLAUDE.md, invariant
# 3): the ones this platform's Qt built, and offscreen for the self-test.
get_target_property(plotcore_qt_type Qt6::Core TYPE)
if(plotcore_qt_type STREQUAL "STATIC_LIBRARY")
  set(plotcore_qt_plugins "")
  foreach(plugin
      Qt6::QWindowsIntegrationPlugin
      Qt6::QWaylandIntegrationPlugin
      Qt6::QWaylandXdgShellIntegrationPlugin
      Qt6::QXcbIntegrationPlugin
      Qt6::QXcbGlxIntegrationPlugin
      Qt6::QCocoaIntegrationPlugin
      Qt6::QOffscreenIntegrationPlugin)
    if(TARGET ${plugin})
      list(APPEND plotcore_qt_plugins ${plugin})
    endif()
  endforeach()
  qt_import_plugins(plotcore-qt-demo INCLUDE ${plotcore_qt_plugins})
endif()

if(PLOTCORE_BUILD_TESTS)
  add_test(NAME plotcore_qt_demo COMMAND plotcore-qt-demo --self-test)
  set_tests_properties(plotcore_qt_demo PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)
endif()
