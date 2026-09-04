// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "postproc/Array.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace postproc {

namespace {

/// The lowest and highest offsets into the buffer this view can reach.
/// Negative strides mean the first element is not the lowest one.
std::pair<std::ptrdiff_t, std::ptrdiff_t>
reach(std::ptrdiff_t offset, const std::vector<hsize_t>& shape,
      const std::vector<std::ptrdiff_t>& strides)
{
    std::ptrdiff_t low = offset;
    std::ptrdiff_t high = offset;
    for (std::size_t d = 0; d < shape.size(); ++d) {
        if (shape[d] == 0) {
            return {0, -1}; // nothing is reached at all
        }
        const std::ptrdiff_t span =
            strides[d] * static_cast<std::ptrdiff_t>(shape[d] - 1);
        if (span < 0) {
            low += span;
        } else {
            high += span;
        }
    }
    return {low, high};
}

} // namespace

hsize_t elementCount(const std::vector<hsize_t>& shape)
{
    constexpr hsize_t kMax = std::numeric_limits<hsize_t>::max();
    hsize_t total = 1;
    for (const hsize_t extent : shape) {
        if (extent == 0) {
            return 0;
        }
        if (total > kMax / extent) {
            return kMax;
        }
        total *= extent;
    }
    return total;
}

Progression asProgression(const std::vector<hsize_t>& indices)
{
    Progression result;
    if (indices.empty()) {
        return result;
    }
    result.start = indices.front();
    if (indices.size() == 1) {
        result.uniform = true;
        return result;
    }
    result.step = static_cast<std::ptrdiff_t>(indices[1])
                  - static_cast<std::ptrdiff_t>(indices[0]);
    for (std::size_t i = 2; i < indices.size(); ++i) {
        const std::ptrdiff_t step = static_cast<std::ptrdiff_t>(indices[i])
                                    - static_cast<std::ptrdiff_t>(indices[i - 1]);
        if (step != result.step) {
            return result;
        }
    }
    result.uniform = true;
    return result;
}

std::vector<std::ptrdiff_t> Array::rowMajorStrides(const std::vector<hsize_t>& shape)
{
    std::vector<std::ptrdiff_t> strides(shape.size(), 1);
    for (std::size_t d = shape.size(); d-- > 0;) {
        if (d + 1 < shape.size()) {
            strides[d] = strides[d + 1] * static_cast<std::ptrdiff_t>(shape[d + 1]);
        }
    }
    return strides;
}

Array::Array() : storage_(std::make_shared<const std::vector<double>>(1, 0.0)) {}

Array::Array(std::vector<hsize_t> shape, std::vector<double> values)
    : storage_(std::make_shared<const std::vector<double>>(std::move(values))),
      shape_(std::move(shape)),
      strides_(rowMajorStrides(shape_))
{
}

hsize_t Array::size() const noexcept { return elementCount(shape_); }

bool Array::valid() const noexcept
{
    if (shape_.size() != strides_.size()) {
        return false;
    }
    if (size() == 0) {
        return true;
    }
    const auto [low, high] = reach(offset_, shape_, strides_);
    return low >= 0 && high < static_cast<std::ptrdiff_t>(storage_->size());
}

bool Array::contiguous() const noexcept
{
    if (size() == 0) {
        return true;
    }
    std::ptrdiff_t expected = 1;
    for (std::size_t d = shape_.size(); d-- > 0;) {
        if (shape_[d] == 1) {
            continue; // its stride is never used, so it cannot disagree
        }
        if (strides_[d] != expected) {
            return false;
        }
        expected *= static_cast<std::ptrdiff_t>(shape_[d]);
    }
    return true;
}

double Array::at(const std::vector<hsize_t>& index) const
{
    std::ptrdiff_t position = offset_;
    for (std::size_t d = 0; d < index.size() && d < strides_.size(); ++d) {
        position += strides_[d] * static_cast<std::ptrdiff_t>(index[d]);
    }
    if (position < 0 || position >= static_cast<std::ptrdiff_t>(storage_->size())) {
        return 0.0;
    }
    return (*storage_)[static_cast<std::size_t>(position)];
}

double Array::linear(hsize_t position) const
{
    // Unravel row-major, most significant dimension first.
    std::ptrdiff_t at = offset_;
    hsize_t remaining = position;
    for (std::size_t d = shape_.size(); d-- > 0;) {
        if (shape_[d] == 0) {
            return 0.0;
        }
        const hsize_t index = remaining % shape_[d];
        remaining /= shape_[d];
        at += strides_[d] * static_cast<std::ptrdiff_t>(index);
    }
    if (at < 0 || at >= static_cast<std::ptrdiff_t>(storage_->size())) {
        return 0.0;
    }
    return (*storage_)[static_cast<std::size_t>(at)];
}

std::vector<double> Array::values() const
{
    const hsize_t total = size();
    std::vector<double> out;
    out.reserve(static_cast<std::size_t>(total));

    if (contiguous() && offset_ >= 0
        && offset_ + static_cast<std::ptrdiff_t>(total)
               <= static_cast<std::ptrdiff_t>(storage_->size())) {
        const auto first =
            storage_->begin() + static_cast<std::ptrdiff_t>(offset_);
        out.assign(first, first + static_cast<std::ptrdiff_t>(total));
        return out;
    }

    // A walk with a rolling index rather than an unravel per element: the
    // whole point of the strided form is that the next element is one add
    // away from this one.
    if (shape_.empty()) {
        out.push_back(at({}));
        return out;
    }
    std::vector<hsize_t> index(shape_.size(), 0);
    std::ptrdiff_t position = offset_;
    for (hsize_t n = 0; n < total; ++n) {
        out.push_back(position >= 0
                              && position < static_cast<std::ptrdiff_t>(storage_->size())
                          ? (*storage_)[static_cast<std::size_t>(position)]
                          : 0.0);
        for (std::size_t d = shape_.size(); d-- > 0;) {
            position += strides_[d];
            if (++index[d] < shape_[d]) {
                break;
            }
            position -= strides_[d] * static_cast<std::ptrdiff_t>(shape_[d]);
            index[d] = 0;
        }
    }
    return out;
}

Array Array::materialised() const
{
    // Already exactly its own buffer: hand back the same storage rather than
    // a copy of it, which is what makes a slice-transpose-reshape chain free.
    if (contiguous() && offset_ == 0 && size() == storage_->size()) {
        return *this;
    }
    return Array(shape_, values());
}

Array Array::transposed(const std::vector<std::size_t>& axes) const
{
    Array out = *this;
    out.shape_.resize(axes.size());
    out.strides_.resize(axes.size());
    for (std::size_t d = 0; d < axes.size(); ++d) {
        out.shape_[d] = shape_[axes[d]];
        out.strides_[d] = strides_[axes[d]];
    }
    return out;
}

Array Array::selected(const std::vector<std::vector<hsize_t>>& indices,
                      const std::vector<bool>& drop) const
{
    const bool uniform =
        std::all_of(indices.begin(), indices.end(), [](const auto& list) {
            return asProgression(list).uniform;
        });

    if (uniform) {
        Array out = *this;
        out.shape_.clear();
        out.strides_.clear();
        for (std::size_t d = 0; d < indices.size(); ++d) {
            const Progression run = asProgression(indices[d]);
            out.offset_ +=
                strides_[d] * static_cast<std::ptrdiff_t>(run.start);
            if (d < drop.size() && drop[d]) {
                continue;
            }
            out.shape_.push_back(static_cast<hsize_t>(indices[d].size()));
            out.strides_.push_back(strides_[d] * run.step);
        }
        return out;
    }

    // A scattered list along at least one dimension, so there is no stride
    // that describes it and the elements have to be fetched.
    std::vector<hsize_t> outShape;
    for (std::size_t d = 0; d < indices.size(); ++d) {
        if (d < drop.size() && drop[d]) {
            continue;
        }
        outShape.push_back(static_cast<hsize_t>(indices[d].size()));
    }

    const hsize_t total = elementCount(outShape);
    std::vector<double> gathered;
    gathered.reserve(static_cast<std::size_t>(total));

    std::vector<hsize_t> cursor(indices.size(), 0);
    std::vector<hsize_t> source(indices.size(), 0);
    for (hsize_t n = 0; n < total; ++n) {
        for (std::size_t d = 0; d < indices.size(); ++d) {
            source[d] = indices[d][cursor[d]];
        }
        gathered.push_back(at(source));

        // Step the cursor, skipping the dropped dimensions -- each of those
        // is pinned to its single index and never advances.
        for (std::size_t d = indices.size(); d-- > 0;) {
            if (d < drop.size() && drop[d]) {
                continue;
            }
            if (++cursor[d] < indices[d].size()) {
                break;
            }
            cursor[d] = 0;
        }
    }
    return Array(std::move(outShape), std::move(gathered));
}

Array Array::reshaped(std::vector<hsize_t> shape) const
{
    if (!contiguous()) {
        return materialised().reshaped(std::move(shape));
    }
    Array out = *this;
    out.shape_ = std::move(shape);
    out.strides_ = rowMajorStrides(out.shape_);
    return out;
}

} // namespace postproc
