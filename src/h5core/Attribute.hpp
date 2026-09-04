// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "File.hpp"
#include "Types.hpp"

#include <string>
#include <vector>

namespace h5core {

/// Read every attribute attached to the object at `path`, rendered to text.
///
/// Attributes are small by design in HDF5, so unlike datasets they are read
/// whole. `maxElements` bounds the rendering of a pathologically large one.
[[nodiscard]] std::vector<AttributeInfo> readAttributes(const File& file,
                                                        const std::string& path,
                                                        std::size_t maxElements = 256);

} // namespace h5core
