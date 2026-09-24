// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "Attribute.hpp"

#include "DataType.hpp"
#include "Error.hpp"
#include "Thread.hpp"
#include "Handle.hpp"

#include <algorithm>
#include <exception>
#include <format>
#include <new>
#include <optional>
#include <sstream>

namespace h5core {
namespace {

struct AttributeContext {
    std::vector<AttributeInfo>* out = nullptr;
    std::size_t maxElements = 256;
    /// What stopped the walk from inside the callback, to be thrown once HDF5's
    /// frames are no longer in the way.
    std::exception_ptr failure;
};

std::string renderValue(hid_t attribute, hid_t nativeType, hid_t space, std::size_t maxElements)
{
    const std::size_t elementSize = H5Tget_size(nativeType);
    if (elementSize == 0) {
        return "<unreadable>";
    }

    // Counted by the dataspace rather than multiplied out of the shape. The
    // two agree for a simple dataspace, and part company exactly where it
    // matters: a *null* one has rank 0 like a scalar, so the empty product of
    // its shape says one element where there are none -- and that element was
    // then read into a zeroed buffer and printed as a value the file never
    // held. The count is also the number H5Aread fills the buffer with, which
    // is the only number the buffer may be sized by.
    const hssize_t points = H5Sget_simple_extent_npoints(space);
    if (points < 0) {
        H5Eclear2(H5E_DEFAULT);
        return "<unreadable>";
    }
    const auto total = static_cast<std::size_t>(points);

    // An attribute may hold no elements at all. There is nothing to read and
    // H5Aread would fail on the null buffer, which is not the same as the
    // attribute being unreadable.
    if (total == 0) {
        return "[]";
    }

    const std::optional<std::size_t> bytes = bufferBytes(total, elementSize);
    if (!bytes.has_value()) {
        return "<too large to read>";
    }
    std::vector<unsigned char> buffer(*bytes);
    if (H5Aread(attribute, nativeType, buffer.data()) < 0) {
        H5Eclear2(H5E_DEFAULT);
        return "<unreadable>";
    }
    VlenGuard reclaim(nativeType, space, buffer.data());

    if (total == 1) {
        return formatElement(nativeType, buffer.data());
    }

    const std::size_t shown = std::min(total, maxElements);
    std::ostringstream out;
    out << "[";
    for (std::size_t i = 0; i < shown; ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << formatElement(nativeType, buffer.data() + i * elementSize);
    }
    if (shown < total) {
        out << ", ... (" << (total - shown) << " more)";
    }
    out << "]";
    return out.str();
}

/// One attribute, described and rendered. Throws only what the allocator
/// throws; everything HDF5 says no to is reported in-band.
AttributeInfo describeAttribute(hid_t location, const char* name, std::size_t maxElements)
{
    AttributeInfo attr;
    attr.name = name;

    Handle handle(H5Aopen(location, name, H5P_DEFAULT), &H5Aclose);
    if (!handle.valid()) {
        H5Eclear2(H5E_DEFAULT);
        attr.value = "<unreadable>";
        return attr;
    }

    Handle type(H5Aget_type(handle.get()), &H5Tclose);
    Handle space(H5Aget_space(handle.get()), &H5Sclose);
    if (!type.valid() || !space.valid()) {
        H5Eclear2(H5E_DEFAULT);
        attr.value = "<unreadable>";
        return attr;
    }

    attr.type = describeType(type.get());

    const int rank = H5Sget_simple_extent_ndims(space.get());
    if (rank > 0) {
        attr.shape.resize(static_cast<std::size_t>(rank));
        if (H5Sget_simple_extent_dims(space.get(), attr.shape.data(), nullptr) < 0) {
            H5Eclear2(H5E_DEFAULT);
            attr.shape.clear();
        }
    }

    Handle nativeType(H5Tget_native_type(type.get(), H5T_DIR_ASCEND), &H5Tclose);
    if (!nativeType.valid()) {
        // No conversion path -- H5T_TIME is the one that reaches here. The
        // attribute is fine; this build simply cannot turn it into a value.
        H5Eclear2(H5E_DEFAULT);
        attr.value = "<no conversion for this datatype>";
    } else {
        try {
            attr.value = renderValue(handle.get(), nativeType.get(), space.get(), maxElements);
        } catch (const std::bad_alloc&) {
            // H5Aread has no partial form, so an attribute is read whole or not
            // at all -- and one whose dataspace is larger than this machine can
            // hold is a statement about that one attribute, not a reason to
            // show none of the others beside it.
            attr.value = "<too large to read>";
        }
    }
    return attr;
}

herr_t attributeCallback(hid_t location, const char* name, const H5A_info_t* /*info*/,
                         void* opData) noexcept
{
    auto* ctx = static_cast<AttributeContext*>(opData);
    if (ctx == nullptr || name == nullptr) {
        return 0;
    }

    // Never throw out of an HDF5 iteration callback: HDF5 C frames sit between
    // here and the caller, and unwinding through them is undefined behaviour
    // rather than an error. A broken attribute is reported in-band, and the
    // one thing that is not HDF5's to report -- the allocator refusing, on an
    // attribute whose dataspace is larger than memory -- is carried out and
    // rethrown on the far side of H5Aiterate2, where it is an ordinary
    // exception again.
    try {
        ctx->out->push_back(describeAttribute(location, name, ctx->maxElements));
    } catch (...) {
        ctx->failure = std::current_exception();
        return -1;
    }
    return 0;
}

} // namespace

std::vector<AttributeInfo> readAttributes(const File& file, const std::string& path,
                                          std::size_t maxElements)
{
    thread::check(__func__);
    Handle object(H5Oopen(file.id(), path.c_str(), H5P_DEFAULT), &H5Oclose);
    if (!object.valid()) {
        throwError(std::format("Cannot open object '{}'", path));
    }

    std::vector<AttributeInfo> result;
    AttributeContext ctx{&result, maxElements, {}};

    hsize_t index = 0;
    const herr_t walked =
        H5Aiterate2(object.get(), H5_INDEX_NAME, H5_ITER_INC, &index, &attributeCallback, &ctx);
    if (ctx.failure) {
        H5Eclear2(H5E_DEFAULT);
        std::rethrow_exception(ctx.failure);
    }
    if (walked < 0) {
        throwError(std::format("Failed to list attributes of '{}'", path));
    }

    return result;
}

} // namespace h5core
