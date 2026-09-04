// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <hdf5.h>

#include <cstddef>
#include <memory>
#include <vector>

namespace postproc {

/// An n-dimensional array of doubles, as a strided view over a shared buffer.
///
/// Doubles because that is what this application already reads: h5core has
/// exactly one numeric path and `readNumericWindow` widens every integer and
/// every float width into a `double` on the way out of HDF5. A second,
/// datatype-preserving path would be a second copy of that decision, and the
/// only thing it would buy is exactness above 2^53 -- which the plot and the
/// image have already given up.
///
/// Strided rather than flat because three of the six operations do not need to
/// move a single element. A transpose permutes the strides, a reshape of a
/// contiguous array rewrites the shape, and a slice whose indices form an
/// arithmetic progression -- which is every `:`, every `a:b`, every `::k` and
/// every bare index -- is an offset and a stride. Chaining them costs nothing
/// but arithmetic, and the one copy that does happen is the read from the file.
///
/// `strides_` is signed: `::-1` is a negative step, and a view that runs
/// backwards through its buffer is the whole reason not to store extents here.
class Array
{
public:
    /// The empty scalar. Rank 0, one element, value 0.
    Array();

    /// Take ownership of `values` as a contiguous row-major array of `shape`.
    /// The caller is responsible for the two agreeing; `valid()` says whether
    /// they do.
    Array(std::vector<hsize_t> shape, std::vector<double> values);

    [[nodiscard]] const std::vector<hsize_t>& shape() const noexcept { return shape_; }
    [[nodiscard]] std::size_t rank() const noexcept { return shape_.size(); }

    /// The number of elements: the product of the shape, which is 1 for a
    /// scalar and 0 if any dimension is empty.
    [[nodiscard]] hsize_t size() const noexcept;

    /// Whether the buffer is large enough for every index this shape and these
    /// strides can reach. False only for an Array built from mismatched parts.
    [[nodiscard]] bool valid() const noexcept;

    /// C-contiguous in numpy's sense: the strides are the row-major ones, with
    /// dimensions of extent 1 disregarded because no stride of theirs is ever
    /// used. An array with no elements is trivially contiguous.
    [[nodiscard]] bool contiguous() const noexcept;

    /// The element at a full index tuple. `index` must match the rank and be
    /// in bounds; this is the slow path, for tests and single lookups.
    [[nodiscard]] double at(const std::vector<hsize_t>& index) const;

    /// The `position`th element in row-major logical order.
    [[nodiscard]] double linear(hsize_t position) const;

    /// The same array as a contiguous buffer, in row-major order. Returns the
    /// existing buffer untouched when it is already contiguous and covers
    /// exactly this array, so a chain that never left its strides never copies.
    [[nodiscard]] Array materialised() const;

    /// Every element in row-major order. Materialises first if it has to.
    [[nodiscard]] std::vector<double> values() const;

    /// The axes permuted: `axes[i]` says which of the input's dimensions
    /// becomes dimension `i`. Metadata only.
    [[nodiscard]] Array transposed(const std::vector<std::size_t>& axes) const;

    /// The elements each dimension names, in the order it names them.
    /// `drop[d]` removes dimension `d` from the result, which is what an
    /// integer subscript does in Python. Metadata only when every dimension's
    /// indices form an arithmetic progression, which covers every subscript
    /// but a scattered list; a gather otherwise.
    [[nodiscard]] Array
    selected(const std::vector<std::vector<hsize_t>>& indices,
             const std::vector<bool>& drop) const;

    /// The same elements under a new shape, in C order. Materialises first
    /// when the strides are not contiguous, as numpy's reshape does. The
    /// caller has already checked that the products agree.
    [[nodiscard]] Array reshaped(std::vector<hsize_t> shape) const;

    /// The buffer this view sits on, for the tests that assert nothing was
    /// copied.
    [[nodiscard]] const void* buffer() const noexcept { return storage_.get(); }

private:
    /// Row-major strides for `shape`, in elements.
    [[nodiscard]] static std::vector<std::ptrdiff_t>
    rowMajorStrides(const std::vector<hsize_t>& shape);

    std::shared_ptr<const std::vector<double>> storage_;
    std::vector<hsize_t> shape_;
    std::vector<std::ptrdiff_t> strides_; ///< in elements, may be negative
    std::ptrdiff_t offset_ = 0;           ///< of element (0,0,...) in storage_
};

/// Whether `indices` counts by a constant step, and what that step is.
///
/// `[2,4,6]` does and `[0,2,5]` does not, which is the difference between a
/// slice that is a view and a slice that has to be gathered. A single index
/// counts as a progression of step 1: there is no second element for the step
/// to be read off, and any value would do.
struct Progression {
    bool uniform = false;
    hsize_t start = 0;
    std::ptrdiff_t step = 1;
};

[[nodiscard]] Progression asProgression(const std::vector<hsize_t>& indices);

/// The product of `shape`, saturating rather than wrapping: a rank-12 dataset
/// of plausible extents overflows a 64-bit count, and a wrapped product would
/// report a huge selection as a small one and then read it.
[[nodiscard]] hsize_t elementCount(const std::vector<hsize_t>& shape);

} // namespace postproc
