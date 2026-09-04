// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "DataSource.hpp"
#include "File.hpp"
#include "Handle.hpp"
#include "Types.hpp"

#include <hdf5.h>

#include <string>
#include <vector>

namespace h5core {

/// Read-only view of one dataset in an open file.
class Dataset : public DataSource
{
public:
    Dataset(const File& file, const std::string& path);

    [[nodiscard]] const DatasetInfo& info() const noexcept override { return info_; }
    [[nodiscard]] const std::string& path() const noexcept override { return path_; }

    /// Read the hyperslab starting at `offset` with extent `count`.
    ///
    /// Datasets routinely exceed RAM, so the whole dataset is never
    /// materialised; the table view asks for the block it is about to paint.
    /// `offset` and `count` must match the dataset rank, and `count` is
    /// clamped to the dataset bounds. readNumericWindow() below is the same
    /// read with a different element type; there are no others.
    ///
    /// Throws H5Error if the dataset uses a filter this build cannot decode.
    [[nodiscard]] DataWindow readWindow(const std::vector<hsize_t>& offset,
                                        const std::vector<hsize_t>& count) const override;

    /// Read the same hyperslab as readWindow(), but as numbers.
    ///
    /// Throws H5Error when the datatype is not numeric (see h5core::isNumeric)
    /// or uses a filter this build cannot decode. Integers wider than 53 bits
    /// lose precision on the way into a double; that is the price of having
    /// exactly one numeric path, and it costs nothing at any scale a plot or a
    /// screen can resolve.
    [[nodiscard]] NumericWindow
    readNumericWindow(const std::vector<hsize_t>& offset,
                      const std::vector<hsize_t>& count) const override;

    /// Read the single element at `offset` and take it apart: its compound
    /// members if it has any, and the whole of it as JSON.
    ///
    /// The Data Viewer's compound presentation is built on this. It is one
    /// element rather than a window because that is what a reader is looking
    /// at -- a struct is read one at a time, the way a string is.
    [[nodiscard]] ElementValue
    readElement(const std::vector<hsize_t>& offset) const override;

    /// Convenience for scalar and small datasets: read everything.
    /// Throws if the element count exceeds `maxElements`.
    [[nodiscard]] DataWindow readAll(hsize_t maxElements = 1u << 20u) const;

private:
    /// The clamped hyperslab and the memory dataspace matching it. Both read
    /// paths need exactly this and nothing about it depends on the element
    /// type, so neither of them spells it out.
    struct Selection {
        Handle fileSpace;
        Handle memorySpace;
        std::vector<hsize_t> clamped; ///< `count`, cut to the dataset bounds
        hsize_t elements = 0;
    };

    [[nodiscard]] Selection selectWindow(const std::vector<hsize_t>& offset,
                                         const std::vector<hsize_t>& count) const;

    std::string path_;
    Handle dataset_;
    DatasetInfo info_;
};

} // namespace h5core
