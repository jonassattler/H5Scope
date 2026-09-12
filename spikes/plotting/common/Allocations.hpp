// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// How many times the program asked the allocator for memory, and for how much.
//
// This is the column that matters most here, and it is the one this repository
// already argues for in tests/test_cost.cpp: a duration measures the machine,
// a count measures the program. An allocation count is the same number on this
// laptop and on a CI runner, it does not move when the machine is busy, and it
// is where the cost of handing a renderer a QList<QPointF> -- sixteen bytes a
// point, built and copied on every refill -- becomes visible rather than
// inferred.
//
// Implemented by replacing the global operator new and delete. That is a heavy
// hammer, and it is deliberate: a counter the spikes call on purpose would
// count only what the spikes do, and what is being compared is precisely what
// the *libraries* do underneath. Qt's own allocations are counted too, which is
// the point.
//
// Allocations.cpp carries both the replacement and the accessors below, so any
// translation unit that reads a counter also drags the replacement in. Putting
// the operators in a file nothing references would leave them in the archive
// unlinked, and every count would read zero while looking like a measurement.

#include <cstddef>
#include <cstdint>

namespace spike {

struct AllocationCount {
    std::uint64_t calls = 0;
    std::uint64_t bytes = 0;

    [[nodiscard]] AllocationCount since(const AllocationCount& earlier) const
    {
        return {calls - earlier.calls, bytes - earlier.bytes};
    }
};

/// A snapshot of the counters. Cheap: two relaxed atomic loads.
[[nodiscard]] AllocationCount allocations();

/// Whether the replacement is actually in this binary. A guard for the report:
/// a toolchain that resolves operator new somewhere else would print zeroes,
/// and a zero in a count column must mean "none" and never "not measured".
[[nodiscard]] bool allocationCountingActive();

} // namespace spike
