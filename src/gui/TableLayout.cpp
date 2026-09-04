// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "TableLayout.hpp"

#include <QStringList>

#include <algorithm>
#include <limits>
#include <optional>

namespace gui {
namespace {

/// Entries an axis presents: the product of its dimensions' selections, or 1
/// when it carries no dimension at all (which is how a scalar stays one cell).
/// Clamped to what an int can index, as QAbstractItemModel counts in int and a
/// product of "All" selections can outrun that on a big enough dataset.
qint64 axisExtent(const TableLayout& layout, const std::vector<std::size_t>& dims)
{
    constexpr qint64 kMax = std::numeric_limits<int>::max();
    qint64 total = 1;
    for (const std::size_t d : dims) {
        const auto size = static_cast<qint64>(layout.indices[d].size());
        if (size == 0) {
            return 0;
        }
        if (total > kMax / size) {
            return kMax;
        }
        total *= size;
    }
    return total;
}


} // namespace

bool defaultOnX(std::size_t dimension, std::size_t rank)
{
    return rank > 1 && dimension + 1 == rank;
}

DefaultAxes defaultAxes(const std::vector<hsize_t>& shape,
                        const std::optional<h5core::ImageInfo>& image)
{
    DefaultAxes axes;
    axes.onX.reserve(shape.size());
    for (std::size_t d = 0; d < shape.size(); ++d) {
        axes.onX.push_back(defaultOnX(d, shape.size()));
    }

    // A file may declare CLASS="IMAGE" and then not have the shape its
    // subclass implies. The tag still says what the file says; the axes do not
    // move on the strength of an attribute the dataspace contradicts.
    if (!image.has_value() || !image->shapeMatches) {
        return axes;
    }

    std::fill(axes.onX.begin(), axes.onX.end(), false);
    axes.onX[image->rowDim] = false;
    axes.onX[image->columnDim] = true;
    if (image->channelDim.has_value()) {
        axes.pinned = *image->channelDim;
    }
    return axes;
}

TableLayout defaultLayout(const std::vector<hsize_t>& shape,
                          const std::optional<h5core::ImageInfo>& image)
{
    const DefaultAxes axes = defaultAxes(shape, image);

    TableLayout layout;
    layout.indices.reserve(shape.size());
    layout.onX.reserve(shape.size());
    for (std::size_t d = 0; d < shape.size(); ++d) {
        const bool pinned = axes.pinned.has_value() && *axes.pinned == d;
        const auto count = pinned ? std::min<hsize_t>(shape[d], 1) : shape[d];
        std::vector<hsize_t> selected(static_cast<std::size_t>(count));
        for (hsize_t i = 0; i < count; ++i) {
            selected[static_cast<std::size_t>(i)] = i;
        }
        layout.indices.push_back(std::move(selected));
        layout.onX.push_back(axes.onX[d]);
    }
    return layout;
}


TableAxes::TableAxes(TableLayout layout, bool empty) : layout_(std::move(layout))
{
    for (std::size_t d = 0; d < layout_.onX.size(); ++d) {
        (layout_.onX[d] ? xDims_ : yDims_).push_back(d);
    }
    rows_ = empty ? 0 : axisExtent(layout_, yDims_);
    columns_ = empty ? 0 : axisExtent(layout_, xDims_);
}

void TableAxes::positions(qint64 value, const std::vector<std::size_t>& dims,
                          std::vector<hsize_t>& out) const
{
    // An odometer read from its least significant wheel: the last dimension of
    // the axis turns fastest, which is what makes the axis lexicographic.
    out.assign(dims.size(), 0);
    for (std::size_t i = dims.size(); i-- > 0;) {
        const auto size = static_cast<qint64>(layout_.indices[dims[i]].size());
        if (size <= 0) {
            continue;
        }
        out[i] = static_cast<hsize_t>(value % size);
        value /= size;
    }
}

std::vector<hsize_t> TableAxes::coordinates(qint64 row, qint64 column) const
{
    std::vector<hsize_t> coords(rank(), 0);
    std::vector<hsize_t> slot;

    // A dimension of no extent selects nothing, which empties one axis while
    // the other still has entries -- an axis carrying no dimension is one
    // entry wide. Its header is still asked for, so leave those coordinates at
    // zero rather than indexing an empty selection.
    const auto fill = [&](const std::vector<std::size_t>& dims) {
        for (std::size_t i = 0; i < dims.size(); ++i) {
            const std::vector<hsize_t>& selected = layout_.indices[dims[i]];
            if (!selected.empty()) {
                coords[dims[i]] =
                    selected[std::min<std::size_t>(slot[i], selected.size() - 1)];
            }
        }
    };

    positions(row, yDims_, slot);
    fill(yDims_);
    positions(column, xDims_, slot);
    fill(xDims_);
    return coords;
}

int TableAxes::runLength(qint64 column, int limit) const
{
    // The last x-dimension is the one that turns fastest along a table row, so
    // a span of columns is one contiguous hyperslab exactly as long as that
    // dimension's selected indices stay consecutive. With All or a Range that
    // is the whole span and a row costs one read; with scattered Custom
    // indices it degrades to a read per cell, which is the price of asking for
    // scattered indices.
    if (xDims_.empty() || limit <= 1) {
        return std::max(limit, 1);
    }
    const std::vector<hsize_t>& fastest = layout_.indices[xDims_.back()];
    if (fastest.empty()) {
        return 1;
    }

    const auto position = static_cast<std::size_t>(column) % fastest.size();
    int run = 1;
    while (run < limit && position + run < fastest.size()
           && fastest[position + run] == fastest[position] + run) {
        ++run;
    }
    return run;
}

TableAxes TableAxes::asPicture(std::size_t dimension, hsize_t index) const
{
    if (dimension >= layout_.rank()) {
        return *this;
    }

    TableLayout held = layout_;
    held.indices[dimension] = {index};
    {
        TableAxes axes(held);
        // Two dimensions of extent make a picture. What the reader arranged is
        // one already, more often than not -- a dataset that declares itself an
        // image opens arranged -- and their arrangement is the more specific
        // statement.
        if (axes.rows() >= 2 && axes.columns() >= 2) {
            return axes;
        }
    }

    // The highest-numbered dimension that is not the colour axis varies fastest
    // of those left, so it is the one that runs along a row -- the same
    // reasoning defaultOnX gives for a dataset with no colour axis at all.
    std::size_t fastest = 0;
    for (std::size_t d = 0; d < held.rank(); ++d) {
        if (d != dimension) {
            fastest = d;
        }
    }
    for (std::size_t d = 0; d < held.rank(); ++d) {
        held.onX[d] = (d == fastest);
    }
    TableAxes axes(std::move(held));
    // An axis of no entries would blank the picture, and that is the wrong
    // answer to give back for a channel the reader merely named.
    return (axes.rows() > 0 && axes.columns() > 0) ? axes : *this;
}

TableAxes TableAxes::pinned(std::size_t dimension, hsize_t index) const
{
    if (dimension >= layout_.rank()) {
        return *this;
    }
    TableLayout held = layout_;
    held.indices[dimension] = {index};
    // An axis of no entries would blank the whole table, and this is asked for
    // by a colour channel that has moved on rather than by anything the reader
    // did, so an empty result is the wrong answer to give back.
    TableAxes axes(std::move(held));
    return (axes.rows() > 0 && axes.columns() > 0) ? axes : *this;
}

} // namespace gui
