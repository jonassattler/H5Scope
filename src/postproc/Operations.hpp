// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "postproc/Array.hpp"

#include <hdf5.h>

#include <QString>

#include <optional>
#include <vector>

namespace postproc {

/// The operations a pipeline step can be.
///
/// Each one is its numpy namesake and nothing else. That is not a convenience:
/// a reader of HDF5 files opens one with h5py, so `transpose` and `reshape` and
/// an `axis` argument already mean something exact to them, and an operation
/// here that meant nearly that would be worse than no operation at all. Where
/// this had a choice it took numpy's answer -- including the awkward ones, like
/// a reduction over an empty axis being an error rather than an infinity.
enum class OperationKind {
    Slice,
    Transpose,
    Min,
    Max,
    Abs,
    Reshape,
};

/// One step of a pipeline: an operation and the argument it was given, kept as
/// the text that was typed rather than as what it parsed to. The text is what
/// the panel shows, what is remembered per dataset, and what an error message
/// is about; it is re-read against the shape every time, because the shape
/// above it can change under it.
struct Step {
    OperationKind kind = OperationKind::Abs;
    QString argument;

    [[nodiscard]] bool operator==(const Step&) const = default;
};

/// What an operation is called and what it wants written beside it.
struct OperationInfo {
    OperationKind kind = OperationKind::Abs;
    QString name;          ///< as the dropdown and the row print it
    QString argumentLabel; ///< "" for an operation that takes none
    QString placeholder;   ///< an example, shown in the empty box
};

/// Every operation, in the order the dropdown offers them.
[[nodiscard]] const std::vector<OperationInfo>& operations();
[[nodiscard]] const OperationInfo& operationInfo(OperationKind kind);
/// The operation of that name, or nothing. Used to read a remembered pipeline
/// back, where the name is what was written down.
[[nodiscard]] std::optional<OperationKind> operationNamed(const QString& name);

/// The shape an operation leaves behind, or why it cannot run.
///
/// Pure arithmetic over the shape: not one element is read. That is what lets
/// the panel print a shape against every row while the reader is still typing
/// the argument of the row above, on a dataset far too large to run.
struct ShapeResult {
    std::vector<hsize_t> shape;
    QString error; ///< empty when the step can run

    [[nodiscard]] bool ok() const { return error.isEmpty(); }
};

[[nodiscard]] ShapeResult shapeAfter(const Step& step,
                                     const std::vector<hsize_t>& shape);

/// The array an operation leaves behind, or why it could not run. The reasons
/// are exactly shapeAfter's, checked again here because this is also the entry
/// point the tests use.
struct ArrayResult {
    Array array;
    QString error;

    [[nodiscard]] bool ok() const { return error.isEmpty(); }
};

[[nodiscard]] ArrayResult apply(const Step& step, const Array& input);

/// `2 × 3 × 4`, or `scalar` for rank 0. The shape column of the panel, and the
/// one place the multiplication sign is chosen.
[[nodiscard]] QString describeShape(const std::vector<hsize_t>& shape);

} // namespace postproc
