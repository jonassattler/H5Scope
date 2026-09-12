# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-License-Identifier: GPL-3.0-only

# Reads the two files the spikes write and assembles the page. Qt6::Core only:
# it opens no window, and a tool that gathers results should not need a display
# to do it.
add_executable(plot-gallery plot-gallery.cpp)
target_link_libraries(plot-gallery PRIVATE Qt6::Core)
