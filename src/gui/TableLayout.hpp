// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "h5core/Types.hpp"
#include "postproc/Subscripts.hpp"

#include <hdf5.h>

#include <QString>

#include <cstddef>
#include <optional>
#include <vector>

namespace gui {

/// How one dimension of a dataset contributes indices to the table.
enum class AxisMode {
    All,    ///< every index in the dimension
    Index,  ///< exactly one index
    Range,  ///< an inclusive first..last span
    Custom, ///< an expression, see parseIndexExpression
};

/// Which indices each dimension contributes, and which axis it lands on.
///
/// Rows are the lexicographic product of the selections of every dimension
/// with `onX == false`, columns the product of those with `onX == true`; in
/// both products the lower-numbered dimension is the more significant one.
/// Every dimension belongs to exactly one axis, so a table cell always names a
/// complete index tuple.
struct TableLayout {
    std::vector<std::vector<hsize_t>> indices; ///< sorted, unique, per dimension
    std::vector<bool> onX;                     ///< true = column axis

    [[nodiscard]] std::size_t rank() const { return indices.size(); }
};

/// Whether dimension `dimension` of a rank-`rank` dataset starts out on the
/// column axis. The last dimension varies fastest in the file, so putting it
/// on x is what keeps a matrix looking like its shape; rank 1 is the
/// exception, where the single dimension stays on y so a vector keeps reading
/// as one column rather than one very long row.
[[nodiscard]] bool defaultOnX(std::size_t dimension, std::size_t rank);

/// Where each dimension of a dataset starts out, before the reader touches
/// anything.
struct DefaultAxes {
    std::vector<bool> onX;             ///< true = column axis
    std::optional<std::size_t> pinned; ///< this dimension starts at one index
};

/// The starting point for a dataset of `shape`, taking the HDF5 Image spec's
/// attributes into account when it carries them.
///
/// Without them the rule is defaultOnX: nothing hidden, everything above rank 2
/// unrolled down the rows. A dataset that *says* it is a picture gets the one
/// arrangement that draws as one -- its height on the row axis, its width on
/// the column axis, and its colour components pinned to a single channel,
/// because a grayscale raster has one value per cell and there is no
/// arrangement that shows the picture and keeps that axis whole. Only a
/// dataset carrying CLASS="IMAGE" with a shape matching what its subclass
/// implies is rearranged; nothing is inferred from a shape alone.
[[nodiscard]] DefaultAxes defaultAxes(const std::vector<hsize_t>& shape,
                                      const std::optional<h5core::ImageInfo>& image);

/// The layout a dataset starts with: the axes from defaultAxes, every index of
/// every dimension selected, except a pinned dimension which contributes its
/// first index alone.
[[nodiscard]] TableLayout defaultLayout(const std::vector<hsize_t>& shape,
                                        const std::optional<h5core::ImageInfo>& image = {});

/// A TableLayout resolved into the table it describes: which dimensions land
/// on which axis, how many entries each axis then has, and where a given cell
/// of that table sits in the dataset.
///
/// Split out from DatasetTableModel because the image presentation needs a
/// second one of these -- the same table with its colour dimension held at one
/// channel rather than spread along an axis -- and has to be able to build and
/// read one without disturbing the table the grid is showing.
class TableAxes
{
public:
    TableAxes() = default;
    /// `empty` is a null dataspace: no elements at all, where the empty product
    /// over the axes would otherwise make one cell out of nothing.
    explicit TableAxes(TableLayout layout, bool empty = false);

    [[nodiscard]] const TableLayout& layout() const { return layout_; }
    [[nodiscard]] const std::vector<std::size_t>& xDims() const { return xDims_; }
    [[nodiscard]] const std::vector<std::size_t>& yDims() const { return yDims_; }
    [[nodiscard]] qint64 rows() const { return rows_; }
    [[nodiscard]] qint64 columns() const { return columns_; }
    [[nodiscard]] std::size_t rank() const { return layout_.rank(); }

    /// The full index tuple of a cell.
    [[nodiscard]] std::vector<hsize_t> coordinates(qint64 row, qint64 column) const;

    /// How many table columns from `column` name consecutive indices of the
    /// fastest x-dimension, capped at `limit`. That run is one hyperslab, so
    /// it is one read.
    [[nodiscard]] int runLength(qint64 column, int limit) const;

    /// The same table with `dimension` contributing `index` and nothing else.
    /// Returns *this unchanged when the dimension or the index is out of
    /// range, so a caller reading a stale channel gets the table it had rather
    /// than an empty one.
    [[nodiscard]] TableAxes pinned(std::size_t dimension, hsize_t index) const;

    /// The same table read as a picture whose colour channels lie along
    /// `dimension`: that dimension held at `index`, and -- only if what is left
    /// would not draw as a picture -- the highest-numbered of the others along
    /// the rows and the rest down them.
    ///
    /// This is what naming a colour axis means to the image, and it is the
    /// image's own business: it happens in a copy of these axes and the table
    /// beside it keeps whatever it was showing. It used to be done by writing
    /// the arrangement back into the data settings panel, which changed the
    /// slice of every view at once -- so choosing which dimension held colour
    /// rearranged the grid the reader was reading.
    ///
    /// An arrangement that already resolves to a picture is kept, because one
    /// the reader made themselves says something more specific than this rule
    /// does.
    [[nodiscard]] TableAxes asPicture(std::size_t dimension, hsize_t index) const;

private:
    /// Positions within `dims`' selections for the `value`th entry of that
    /// axis, most significant dimension first.
    void positions(qint64 value, const std::vector<std::size_t>& dims,
                   std::vector<hsize_t>& out) const;

    TableLayout layout_;
    std::vector<std::size_t> xDims_; ///< column-axis dimensions, ascending
    std::vector<std::size_t> yDims_; ///< row-axis dimensions, ascending
    qint64 rows_ = 0;
    qint64 columns_ = 0;
};

/// The subscript grammar lives in postproc, because the postprocessing
/// pipeline reads exactly the same one: a Slice operation in the pipeline and
/// the slice line above the table are the same notation, and two parsers for
/// it would be two grammars the moment either was touched. The names are
/// re-exported here because this is where the table's own code has always
/// found them.
using postproc::IndexExpression;
using postproc::parseIndexExpression;
using postproc::readSubscripts;

} // namespace gui
