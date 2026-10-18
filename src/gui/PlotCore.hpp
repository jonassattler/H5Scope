// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// The plot's arithmetic, as the viewer sees it.
//
// The projection, the fold, the pyramid and the policy over held runs are
// plotcore's -- a library of their own, with no Qt and no HDF5 in it, which the
// viewer is one user of. They were gui's until 0.7.2, and the models are
// written in their terms throughout; bringing them into this namespace keeps
// `PlotLine` meaning what it has always meant here rather than renaming every
// use of it, and keeps one definition of each.

#include "plotcore/Levels.hpp"
#include "plotcore/Projection.hpp"
#include "plotcore/Pyramid.hpp"

namespace gui {
using namespace plotcore;
} // namespace gui
