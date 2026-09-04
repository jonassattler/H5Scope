// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "Types.hpp"

#include <hdf5.h>

#include <string>
#include <vector>

namespace h5core {

/// A rectangular block of dataset elements, already rendered to display text.
struct DataWindow {
    std::vector<hsize_t> offset; ///< origin of the block within the dataset
    std::vector<hsize_t> count;  ///< extent of the block per dimension
    std::vector<std::string> cells; ///< row-major, size == product(count)

    [[nodiscard]] const std::string& at(hsize_t row, hsize_t column) const;
};

/// The same block of elements as numbers rather than as text.
///
/// `DataWindow` is what a grid cell needs; a line plot and a grayscale raster
/// need the value itself. Everything arrives as `double` -- HDF5 converts on
/// the way out -- so there is one numeric path rather than one per width.
struct NumericWindow {
    std::vector<hsize_t> offset; ///< origin of the block within the dataset
    std::vector<hsize_t> count;  ///< extent of the block per dimension
    std::vector<double> values;  ///< row-major, size == product(count)
};

/// One element of a dataset, taken apart.
///
/// A compound has no single value, so the one line a grid cell can show is the
/// whole struct elided. `fields` is that struct opened out -- empty for every
/// other class, which has nothing to open -- and `json` is the element as a
/// JSON value, whatever it is.
struct ElementValue {
    std::vector<hsize_t> offset; ///< which element this is
    std::vector<FieldValue> fields;
    std::string json;
    std::string text; ///< the same element as the grid would print it
};

/// Something rectangular that can be read a block at a time.
///
/// This is the whole of what the Data Viewer asks of the thing it is drawing:
/// its shape and datatype, a name for it, a block of it as text, the same block
/// as numbers, and one element taken apart. `Dataset` is the obvious
/// implementation and was the only one for a long time, which is why these five
/// were declared on it directly.
///
/// The second one is `postproc::ComputedDataset`, and it is why this interface
/// exists. Postprocessing has to transpose and reduce, and neither can be done
/// a block at a time -- an operation with an axis in it needs the whole
/// selection at once. So the result is computed into memory and then handed to
/// the views through the same five functions the file answers, and the table,
/// the plot, the image, the string list and the compound pane draw it without
/// knowing which kind of thing they were given.
///
/// Deliberately read-only and deliberately small. A source that could be
/// written to, or that answered questions about chunking and filters, would be
/// a second copy of `Dataset`'s interface rather than the part of it the views
/// use.
class DataSource
{
public:
    DataSource() = default;
    virtual ~DataSource() = default;

    DataSource(const DataSource&) = delete;
    DataSource& operator=(const DataSource&) = delete;
    DataSource(DataSource&&) = delete;
    DataSource& operator=(DataSource&&) = delete;

    /// The shape, the datatype, and everything else the Information panels and
    /// the default layout are built from.
    [[nodiscard]] virtual const DatasetInfo& info() const noexcept = 0;

    /// What to call this on screen. A path within the file for a dataset; the
    /// same path with what was done to it for a computed one.
    [[nodiscard]] virtual const std::string& path() const noexcept = 0;

    /// Read the hyperslab starting at `offset` with extent `count`, as text.
    /// `offset` and `count` match the rank; `count` is clamped to the bounds.
    [[nodiscard]] virtual DataWindow readWindow(const std::vector<hsize_t>& offset,
                                                const std::vector<hsize_t>& count) const = 0;

    /// The same hyperslab as numbers. Throws H5Error when the datatype is not
    /// numeric (see h5core::isNumeric).
    [[nodiscard]] virtual NumericWindow
    readNumericWindow(const std::vector<hsize_t>& offset,
                      const std::vector<hsize_t>& count) const = 0;

    /// The single element at `offset`, with its compound members if it has any.
    [[nodiscard]] virtual ElementValue
    readElement(const std::vector<hsize_t>& offset) const = 0;
};

} // namespace h5core
