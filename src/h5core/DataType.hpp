// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "Types.hpp"

#include <hdf5.h>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace h5core {

/// `elements` of `elementSize` bytes each, as a byte count -- or nothing when
/// the product does not fit in `std::size_t`.
///
/// Every read here sizes its buffer from a count the *file* states, and
/// H5Aread and H5Dread then fill that buffer with every element there is.
/// A product that wraps is a small buffer and a large write into it, so the
/// multiplication is checked rather than trusted: a file is allowed to claim a
/// dataspace no machine could hold, and this is not allowed to be an overrun
/// when it does.
[[nodiscard]] std::optional<std::size_t> bufferBytes(hsize_t elements,
                                                     std::size_t elementSize) noexcept;

/// The dimensions of an H5T_ARRAY, outermost first; empty for any other class.
[[nodiscard]] std::vector<hsize_t> arrayDims(hid_t type);

/// The name of member `index` of a compound or an enum, with the string HDF5
/// allocated for it handed back. Nothing when HDF5 has no name to give.
[[nodiscard]] std::optional<std::string> memberName(hid_t type, unsigned index);

/// Describe an HDF5 datatype without taking ownership of `type`.
TypeInfo describeType(hid_t type);

/// Format one element of `type` located at `data` as display text.
///
/// `data` must point at an element already converted to the *native* type
/// (H5Tget_native_type). Variable-length payloads reachable from `data` are
/// read but not freed; the caller owns them and must reclaim via VlenGuard.
std::string formatElement(hid_t type, const void* data);

/// The members of one compound element at `data`, each rendered on its own.
/// Empty for every other datatype class, which has nothing to decompose.
///
/// `data` must point at an element already converted to the *native* type, and
/// the same ownership rule as formatElement applies to anything variable-length
/// reachable from it.
[[nodiscard]] std::vector<FieldValue> describeCompoundElement(hid_t type,
                                                              const void* data);

/// One element of `type` at `data` as a JSON value.
///
/// Numbers come out as numbers, strings and enum symbols as strings, a compound
/// as an object, an array or a vlen as an array, and anything the format keeps
/// as bytes -- a bitfield, an opaque, a reference -- as the string formatElement
/// would have printed. A value JSON cannot hold is quoted rather than dropped:
/// `NaN` and the infinities come out as "nan", "inf" and "-inf", which is
/// lossless where `null` would not be.
///
/// Written over several lines, indented, because the pane it lands in shows one
/// element and the reader is reading it rather than parsing it. A struct always
/// opens out; a list does so only when it holds structs or lists of its own, so
/// `"samples": [0, 0.25, 0.5, 0.75]` stays on the line its name is on. `depth`
/// is how far the value is already indented -- callers pass 0 and the recursion
/// counts up.
///
/// The same `data` contract as formatElement.
[[nodiscard]] std::string toJson(hid_t type, const void* data, int depth = 0);

/// Scope guard reclaiming variable-length data allocated by H5Dread/H5Aread.
///
/// HDF5 allocates buffers for variable-length elements (strings, vlen arrays)
/// and for references that the caller must hand back or they leak. In HDF5 2.x the call is
/// H5Treclaim; the older H5Dvlen_reclaim was removed.
class VlenGuard
{
public:
    VlenGuard(hid_t type, hid_t space, void* buffer) noexcept;
    ~VlenGuard();

    VlenGuard(const VlenGuard&) = delete;
    VlenGuard& operator=(const VlenGuard&) = delete;
    VlenGuard(VlenGuard&&) = delete;
    VlenGuard& operator=(VlenGuard&&) = delete;

private:
    hid_t type_;
    hid_t space_;
    void* buffer_;
    bool needed_;
};

} // namespace h5core
