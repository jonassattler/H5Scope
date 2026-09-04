// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "Attribute.hpp"

#include "DataType.hpp"
#include "Error.hpp"
#include "Handle.hpp"

#include <format>
#include <numeric>
#include <sstream>

namespace h5core {
namespace {

struct AttributeContext {
    std::vector<AttributeInfo>* out = nullptr;
    std::size_t maxElements = 256;
};

std::string renderValue(hid_t attribute, hid_t nativeType, hid_t space,
                        const std::vector<hsize_t>& shape, std::size_t maxElements)
{
    const std::size_t elementSize = H5Tget_size(nativeType);
    if (elementSize == 0) {
        return "<unreadable>";
    }

    const auto total = static_cast<std::size_t>(
        std::accumulate(shape.begin(), shape.end(), static_cast<hsize_t>(1),
                        std::multiplies<>{}));

    // An attribute may hold no elements at all. There is nothing to read and
    // H5Aread would fail on the null buffer, which is not the same as the
    // attribute being unreadable.
    if (total == 0) {
        return "[]";
    }

    std::vector<unsigned char> buffer(total * elementSize);
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

herr_t attributeCallback(hid_t location, const char* name, const H5A_info_t* /*info*/,
                         void* opData)
{
    auto* ctx = static_cast<AttributeContext*>(opData);
    if (ctx == nullptr || name == nullptr) {
        return 0;
    }

    AttributeInfo attr;
    attr.name = name;

    // Never throw out of an HDF5 iteration callback: HDF5 C frames sit between
    // here and the caller. A broken attribute is reported in-band instead.
    Handle handle(H5Aopen(location, name, H5P_DEFAULT), &H5Aclose);
    if (!handle.valid()) {
        H5Eclear2(H5E_DEFAULT);
        attr.value = "<unreadable>";
        ctx->out->push_back(std::move(attr));
        return 0;
    }

    Handle type(H5Aget_type(handle.get()), &H5Tclose);
    Handle space(H5Aget_space(handle.get()), &H5Sclose);
    if (!type.valid() || !space.valid()) {
        H5Eclear2(H5E_DEFAULT);
        attr.value = "<unreadable>";
        ctx->out->push_back(std::move(attr));
        return 0;
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
        attr.value = renderValue(handle.get(), nativeType.get(), space.get(), attr.shape,
                                 ctx->maxElements);
    }

    ctx->out->push_back(std::move(attr));
    return 0;
}

} // namespace

std::vector<AttributeInfo> readAttributes(const File& file, const std::string& path,
                                          std::size_t maxElements)
{
    Handle object(H5Oopen(file.id(), path.c_str(), H5P_DEFAULT), &H5Oclose);
    if (!object.valid()) {
        throwError(std::format("Cannot open object '{}'", path));
    }

    std::vector<AttributeInfo> result;
    AttributeContext ctx{&result, maxElements};

    hsize_t index = 0;
    if (H5Aiterate2(object.get(), H5_INDEX_NAME, H5_ITER_INC, &index, &attributeCallback,
                    &ctx) < 0) {
        throwError(std::format("Failed to list attributes of '{}'", path));
    }

    return result;
}

} // namespace h5core
