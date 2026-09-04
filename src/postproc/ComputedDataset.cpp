// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "postproc/ComputedDataset.hpp"

#include "h5core/DataType.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
#include <utility>

namespace postproc {
namespace {

/// Whether a value can be written as a whole number without saying anything
/// the double does not hold. Everything outside the exactly-representable
/// range, and every NaN and infinity, is written as a float instead -- an
/// integer print of one of those would be a number nobody computed.
bool wholeNumber(double value)
{
    constexpr double kExact = 9007199254740992.0; // 2^53
    return std::isfinite(value) && value == std::trunc(value)
           && std::abs(value) <= kExact;
}

} // namespace

ComputedDataset::ComputedDataset(Array values, const h5core::DatasetInfo& origin,
                                 const std::string& originPath,
                                 const std::string& what)
    : values_(std::move(values)), path_(originPath + " " + what)
{
    info_.shape = values_.shape();
    info_.maxShape = info_.shape;
    info_.space = info_.shape.empty() ? h5core::Dataspace::Scalar
                                      : h5core::Dataspace::Simple;
    info_.layout = h5core::Layout::Contiguous;
    info_.storageSize = values_.size() * sizeof(double);

    // The class carries over; the width does not, because there is only one
    // width in memory. An integer dataset that has been sliced and transposed
    // still holds integers and should still print as one.
    integral_ = origin.type.cls == h5core::TypeClass::Integer;
    info_.type.cls = integral_ ? h5core::TypeClass::Integer : h5core::TypeClass::Float;
    info_.type.size = sizeof(double);
    info_.type.isSigned = true;
    info_.type.convertible = true;
    info_.type.description = integral_ ? "integer (postprocessed)"
                                       : "float64 (postprocessed)";

    // Deliberately no ImageInfo: see the header. A pipeline has just made
    // whatever the Image spec said about these dimensions untrue.
}

std::vector<hsize_t> ComputedDataset::clamp(const std::vector<hsize_t>& offset,
                                            const std::vector<hsize_t>& count) const
{
    std::vector<hsize_t> clamped = count;
    clamped.resize(info_.shape.size(), 1);
    for (std::size_t d = 0; d < info_.shape.size(); ++d) {
        const hsize_t start = d < offset.size() ? offset[d] : 0;
        clamped[d] = start >= info_.shape[d]
                         ? 0
                         : std::min(clamped[d], info_.shape[d] - start);
    }
    return clamped;
}

std::string ComputedDataset::format(double value) const
{
    if (integral_ && wholeNumber(value)) {
        // Through the same formatter the file's own integers go through, so
        // the grid cannot tell a computed cell from a read one.
        const auto whole = static_cast<std::int64_t>(value);
        return h5core::formatElement(H5T_NATIVE_INT64, &whole);
    }
    return h5core::formatElement(H5T_NATIVE_DOUBLE, &value);
}

h5core::NumericWindow
ComputedDataset::readNumericWindow(const std::vector<hsize_t>& offset,
                                   const std::vector<hsize_t>& count) const
{
    h5core::NumericWindow window;
    window.offset = offset;
    window.count = clamp(offset, count);

    const hsize_t total = elementCount(window.count);
    window.values.reserve(static_cast<std::size_t>(total));
    if (total == 0) {
        return window;
    }
    if (info_.shape.empty()) {
        window.values.push_back(values_.at({}));
        return window;
    }

    // Row-major over the block, reading each element through the array's own
    // strides -- which may be a transpose of the buffer, so this cannot be a
    // memcpy however contiguous the block looks.
    const std::size_t rank = info_.shape.size();
    std::vector<hsize_t> index(rank, 0);
    std::vector<hsize_t> position(rank, 0);
    for (hsize_t n = 0; n < total; ++n) {
        for (std::size_t d = 0; d < rank; ++d) {
            position[d] = (d < offset.size() ? offset[d] : 0) + index[d];
        }
        window.values.push_back(values_.at(position));
        for (std::size_t d = rank; d-- > 0;) {
            if (++index[d] < window.count[d]) {
                break;
            }
            index[d] = 0;
        }
    }
    return window;
}

h5core::DataWindow ComputedDataset::readWindow(const std::vector<hsize_t>& offset,
                                               const std::vector<hsize_t>& count) const
{
    const h5core::NumericWindow numbers = readNumericWindow(offset, count);

    h5core::DataWindow window;
    window.offset = numbers.offset;
    window.count = numbers.count;
    window.cells.reserve(numbers.values.size());
    for (const double value : numbers.values) {
        window.cells.push_back(format(value));
    }
    return window;
}

h5core::ElementValue
ComputedDataset::readElement(const std::vector<hsize_t>& offset) const
{
    h5core::ElementValue element;
    element.offset = offset;

    const double value =
        info_.shape.empty() ? values_.at({}) : values_.at(offset);
    element.text = format(value);
    // Through h5core's own JSON writer, so NaN and the infinities come out as
    // the quoted strings it settled on rather than as a null.
    element.json = h5core::toJson(H5T_NATIVE_DOUBLE, &value);
    return element;
}

} // namespace postproc
