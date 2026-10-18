// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

// The sizes of plotcore.h's structures as this target's C compiler lays them
// out, for plotcore-sys's test to hold the checked-in bindings to.

#include "plotcore.h"

#include <cstddef>

extern "C" std::size_t plotcore_sys_size_of(int which)
{
    switch (which) {
    case 0:
        return sizeof(plotcore_rgba);
    case 1:
        return sizeof(plotcore_chrome);
    case 2:
        return sizeof(plotcore_rect);
    case 3:
        return sizeof(plotcore_segment);
    case 4:
        return sizeof(plotcore_line_info);
    case 5:
        return sizeof(plotcore_text);
    case 6:
        return sizeof(plotcore_legend_entry);
    case 7:
        return sizeof(plotcore_reading);
    default:
        return 0;
    }
}
