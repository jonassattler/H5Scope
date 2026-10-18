// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// The threads a large fold is spread over.
//
// The pyramid is built out of folds that are compute bound -- extremesOf()
// tests every element and branches on it -- so what decides how long the first
// picture of a ten-million-element line takes is how many cores are folding.
// This used to be QThreadPool, which was the last thing tying the arithmetic to
// Qt. A pool of its own is a few dozen lines, and it can promise something
// QThreadPool did not: the thread that asks takes pieces too, so a fold asked
// for from inside another can never wait on a pool that is busy waiting on it.

#include <functional>

namespace plotcore {

/// Threads that take pieces, the calling one included: what the machine
/// reports, and at least one.
[[nodiscard]] int poolThreads();

/// Call `piece(i)` for every `i` in `[0, pieces)`, spread over the pool, and
/// return once every one of them has returned.
///
/// The pieces must not depend on one another's order; they are taken as the
/// threads come free. The caller runs pieces as well rather than only waiting,
/// so a single piece never leaves the calling thread at all.
void parallelFor(long long pieces, const std::function<void(long long)>& piece);

} // namespace plotcore
