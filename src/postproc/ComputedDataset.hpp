// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "h5core/DataSource.hpp"
#include "postproc/Array.hpp"

#include <hdf5.h>

#include <string>
#include <vector>

namespace postproc {

/// The result of a pipeline, answering the questions a dataset answers.
///
/// The views ask five things of what they are drawing -- its shape and type, a
/// name, a block as text, the same block as numbers, one element taken apart --
/// and this answers all five out of memory. That is the whole of how
/// postprocessing reaches the table, the plot and the image: they are handed
/// one of these instead of the `h5core::Dataset`, and not one of them has a
/// branch for which it got.
///
/// Two things it deliberately does not carry. There is no `ImageInfo`, so a
/// dataset that declared itself a picture stops being one the moment a pipeline
/// runs on it -- which is what was asked for, and is right: the Image spec
/// fixes which dimension is height and which is colour, and a transpose or a
/// reduction has just made that statement untrue. And there are no filters, no
/// chunking and no storage size, because none of those describe a buffer; the
/// Information panels read them off the real dataset, which is still there.
///
/// The element type follows the input's class rather than being `float64` for
/// everything. All six operations preserve integrality -- a minimum, a maximum,
/// an absolute value, a transpose, a reshape and a slice of whole numbers are
/// whole numbers -- so an integer dataset keeps printing `3` rather than
/// `3.000000` in the grid.
class ComputedDataset : public h5core::DataSource
{
public:
    /// `origin` is the dataset this was computed from: its type class decides
    /// how the values print, and its path is what the result is named after.
    ComputedDataset(Array values, const h5core::DatasetInfo& origin,
                    const std::string& originPath, const std::string& what);

    [[nodiscard]] const h5core::DatasetInfo& info() const noexcept override
    {
        return info_;
    }
    [[nodiscard]] const std::string& path() const noexcept override { return path_; }

    [[nodiscard]] h5core::DataWindow
    readWindow(const std::vector<hsize_t>& offset,
               const std::vector<hsize_t>& count) const override;

    [[nodiscard]] h5core::NumericWindow
    readNumericWindow(const std::vector<hsize_t>& offset,
                      const std::vector<hsize_t>& count) const override;

    [[nodiscard]] h5core::ElementValue
    readElement(const std::vector<hsize_t>& offset) const override;

    [[nodiscard]] const Array& array() const noexcept { return values_; }

private:
    /// `count` cut to the bounds, as the file's own read does.
    [[nodiscard]] std::vector<hsize_t> clamp(const std::vector<hsize_t>& offset,
                                             const std::vector<hsize_t>& count) const;
    /// One value written the way this datatype class writes them.
    [[nodiscard]] std::string format(double value) const;

    Array values_;
    h5core::DatasetInfo info_;
    std::string path_;
    bool integral_ = false;
};

} // namespace postproc
