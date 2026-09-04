// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "Types.hpp"

#include <hdf5.h>

#include <optional>
#include <vector>

namespace h5core {

/// Read the HDF5 Image and Palette Specification 1.2 attributes off an open
/// dataset, or nothing when it does not carry CLASS="IMAGE".
///
/// The spec is a *convention*, not part of the format: a raster and a matrix
/// of the same shape are the same dataset as far as HDF5 is concerned, and
/// these attributes are the only thing that tells them apart. That is why this
/// lives beside the core types rather than in them, and why nothing is
/// inferred -- a dataset that does not say it is an image is not treated as
/// one, however image-shaped it looks.
///
/// `shape` is the dataset's own, and is checked against what the subclass
/// implies; see ImageInfo::shapeMatches.
[[nodiscard]] std::optional<ImageInfo> readImageInfo(hid_t dataset,
                                                     const std::vector<hsize_t>& shape);

/// Just the two attributes a tree row needs: whether the dataset says it is an
/// image at all, and which kind it says it is. Nothing else about the
/// specification is read.
///
/// Separate from readImageInfo() because it is asked of every dataset row a
/// viewport passes over, and the rest of that function reads four more
/// attributes and checks them against the shape -- work that only the Data
/// Viewer, opening one picture, has any use for.
[[nodiscard]] std::optional<ImageSubclass> readImageOutline(hid_t dataset);

} // namespace h5core
