// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "Dataset.hpp"

#include "DataType.hpp"
#include "Error.hpp"
#include "Image.hpp"
#include "Thread.hpp"

#include <algorithm>
#include <format>
#include <numeric>
#include <stdexcept>

namespace h5core {
namespace {

Layout layoutOf(hid_t createProps)
{
    switch (H5Pget_layout(createProps)) {
    case H5D_CONTIGUOUS: return Layout::Contiguous;
    case H5D_CHUNKED:    return Layout::Chunked;
    case H5D_COMPACT:    return Layout::Compact;
    case H5D_VIRTUAL:    return Layout::Virtual;
    default:             return Layout::Unknown;
    }
}

std::string filterName(H5Z_filter_t id, const unsigned* values, unsigned count,
                       const char* recordedName)
{
    switch (id) {
    case H5Z_FILTER_DEFLATE:
        return (count > 0) ? std::format("deflate (level {})", values[0]) : "deflate";
    case H5Z_FILTER_SHUFFLE:  return "shuffle";
    case H5Z_FILTER_FLETCHER32: return "fletcher32";
    case H5Z_FILTER_SZIP:     return "szip";
    case H5Z_FILTER_NBIT:     return "nbit";
    case H5Z_FILTER_SCALEOFFSET: return "scaleoffset";
    default:
        break;
    }
    // A third-party filter writes its own name into the pipeline message. That
    // name is the only thing in the file that says what a build without the
    // plugin is missing, so print it rather than only the number.
    if (recordedName != nullptr && recordedName[0] != '\0') {
        return std::format("{} (#{})", recordedName, static_cast<int>(id));
    }
    return std::format("filter #{}", static_cast<int>(id));
}

/// Record the filter pipeline, separating filters we can decode from those we
/// cannot. A file may reference a third-party plugin (blosc, lz4, zstd) that is
/// simply absent from this build; that must read as a clear message rather than
/// an obscure read failure later.
///
/// Missing is not the same as fatal. HDF5 skips an *optional* filter it does
/// not have -- on writing, and again on reading -- so a dataset whose pipeline
/// names one still reads back exactly right. Only a mandatory filter stops the
/// data, and only those go in blockingFilters.
void collectFilters(hid_t createProps, DatasetInfo& info)
{
    const int count = H5Pget_nfilters(createProps);
    if (count <= 0) {
        H5Eclear2(H5E_DEFAULT);
        return;
    }

    for (int i = 0; i < count; ++i) {
        unsigned flags = 0;
        std::size_t valueCount = 8;
        unsigned values[8] = {};
        char name[128] = {};
        unsigned filterConfig = 0;

        const H5Z_filter_t id =
            H5Pget_filter2(createProps, static_cast<unsigned>(i), &flags, &valueCount,
                           values, sizeof(name), name, &filterConfig);
        if (id < 0) {
            H5Eclear2(H5E_DEFAULT);
            continue;
        }

        // Every filter HDF5 sets through H5Pset_deflate and its siblings is
        // marked optional, so the flag says nothing about an ordinary pipeline.
        // It matters in exactly one case: a filter this build does not have.
        const bool optional = (flags & H5Z_FLAG_OPTIONAL) != 0;
        const std::string label =
            filterName(id, values, static_cast<unsigned>(valueCount), name);
        info.filters.push_back(label);

        const htri_t available = H5Zfilter_avail(id);
        bool decodable = available > 0;
        if (available <= 0) {
            H5Eclear2(H5E_DEFAULT);
        } else if ((filterConfig & H5Z_FILTER_CONFIG_DECODE_ENABLED) == 0) {
            // Available is not enough; it must also be able to decode.
            decodable = false;
        }
        if (decodable) {
            continue;
        }

        info.unavailableFilters.push_back(label);
        if (!optional) {
            info.blockingFilters.push_back(label);
        }
    }
}

/// Files holding the raw data outside the container, and the source mappings
/// of a virtual dataset. Both are cases where the bytes are not where the rest
/// of the file is, which is exactly what a reader needs told.
void collectStorageSources(hid_t createProps, DatasetInfo& info)
{
    const int externalCount = H5Pget_external_count(createProps);
    if (externalCount > 0) {
        for (int i = 0; i < externalCount; ++i) {
            char name[512] = {};
            off_t offset = 0;
            hsize_t size = 0;
            if (H5Pget_external(createProps, static_cast<unsigned>(i), sizeof(name), name,
                                &offset, &size)
                < 0) {
                H5Eclear2(H5E_DEFAULT);
                continue;
            }
            info.externalFiles.emplace_back(name);
        }
    } else {
        H5Eclear2(H5E_DEFAULT);
    }

    if (info.layout != Layout::Virtual) {
        return;
    }
    std::size_t mappings = 0;
    if (H5Pget_virtual_count(createProps, &mappings) < 0) {
        H5Eclear2(H5E_DEFAULT);
        return;
    }
    for (std::size_t i = 0; i < mappings; ++i) {
        char file[512] = {};
        char object[512] = {};
        if (H5Pget_virtual_filename(createProps, i, file, sizeof(file)) < 0
            || H5Pget_virtual_dsetname(createProps, i, object, sizeof(object)) < 0) {
            H5Eclear2(H5E_DEFAULT);
            continue;
        }
        info.virtualSources.push_back(std::format("{}:{}", file, object));
    }
}

} // namespace

const std::string& DataWindow::at(hsize_t row, hsize_t column) const
{
    const hsize_t columns = (count.size() >= 2) ? count[count.size() - 1] : 1;
    const std::size_t index = static_cast<std::size_t>(row * columns + column);
    if (index >= cells.size()) {
        throw std::out_of_range("DataWindow index out of range");
    }
    return cells[index];
}

Dataset::Dataset(const File& file, const std::string& path) : path_(path)
{
    thread::check("Dataset::Dataset");
    dataset_ = Handle(H5Dopen2(file.id(), path.c_str(), H5P_DEFAULT), &H5Dclose);
    if (!dataset_.valid()) {
        throwError(std::format("Cannot open dataset '{}'", path));
    }

    Handle space(H5Dget_space(dataset_.get()), &H5Sclose);
    if (!space.valid()) {
        throwError(std::format("Cannot read dataspace of '{}'", path));
    }

    // Scalar, simple and null are three different things, and only the first
    // two have elements. Asking for dimensions before asking which it is turns
    // "no data at all" into "one value that will not read".
    switch (H5Sget_simple_extent_type(space.get())) {
    case H5S_SCALAR: info_.space = Dataspace::Scalar; break;
    case H5S_NULL:   info_.space = Dataspace::Null; break;
    default:         info_.space = Dataspace::Simple; break;
    }

    const int rank = H5Sget_simple_extent_ndims(space.get());
    if (rank < 0) {
        throwError(std::format("Cannot read rank of '{}'", path));
    }

    info_.shape.resize(static_cast<std::size_t>(rank));
    info_.maxShape.resize(static_cast<std::size_t>(rank));
    if (rank > 0) {
        check(H5Sget_simple_extent_dims(space.get(), info_.shape.data(),
                                        info_.maxShape.data()),
              std::format("Cannot read dimensions of '{}'", path));
    }

    Handle type(H5Dget_type(dataset_.get()), &H5Tclose);
    if (!type.valid()) {
        throwError(std::format("Cannot read datatype of '{}'", path));
    }
    info_.type = describeType(type.get());

    Handle createProps(H5Dget_create_plist(dataset_.get()), &H5Pclose);
    if (createProps.valid()) {
        info_.layout = layoutOf(createProps.get());
        if (info_.layout == Layout::Chunked) {
            info_.chunk.resize(static_cast<std::size_t>(rank));
            if (H5Pget_chunk(createProps.get(), rank, info_.chunk.data()) < 0) {
                H5Eclear2(H5E_DEFAULT);
                info_.chunk.clear();
            }
        }
        collectFilters(createProps.get(), info_);
        collectStorageSources(createProps.get(), info_);
    }

    // Whether this is a picture is a question about its attributes, not its
    // shape, and the answer decides what the Data Viewer opens on.
    info_.image = readImageInfo(dataset_.get(), info_.shape);

    info_.storageSize = H5Dget_storage_size(dataset_.get());
}

Dataset::Selection Dataset::selectWindow(const std::vector<hsize_t>& offset,
                                         const std::vector<hsize_t>& count) const
{
    if (!info_.readable()) {
        throw H5Error(
            std::format("Dataset '{}' cannot be read: {}", path_,
                        info_.unreadableReason()));
    }

    const std::size_t rank = info_.shape.size();
    if (offset.size() != rank || count.size() != rank) {
        throw H5Error(std::format(
            "Window rank {} does not match dataset rank {} for '{}'",
            offset.size(), rank, path_));
    }

    Selection selection;
    selection.fileSpace = Handle(H5Dget_space(dataset_.get()), &H5Sclose);
    if (!selection.fileSpace.valid()) {
        throwError(std::format("Cannot read dataspace of '{}'", path_));
    }

    // Clamp the request to the dataset so a viewport near the edge is legal.
    selection.clamped = count;
    for (std::size_t i = 0; i < rank; ++i) {
        if (offset[i] >= info_.shape[i]) {
            selection.clamped[i] = 0;
        } else {
            selection.clamped[i] = std::min(count[i], info_.shape[i] - offset[i]);
        }
    }

    selection.elements = std::accumulate(selection.clamped.begin(),
                                         selection.clamped.end(),
                                         static_cast<hsize_t>(1), std::multiplies<>{});

    // A null dataspace has rank 0 like a scalar, but selects nothing: the
    // empty product above says one element and there is none.
    if (info_.isNull()) {
        selection.elements = 0;
        return selection;
    }

    if (rank > 0 && selection.elements == 0) {
        return selection;
    }

    if (rank > 0) {
        check(H5Sselect_hyperslab(selection.fileSpace.get(), H5S_SELECT_SET,
                                  offset.data(), nullptr, selection.clamped.data(),
                                  nullptr),
              std::format("Cannot select region of '{}'", path_));
    }

    selection.memorySpace =
        Handle(rank > 0 ? H5Screate_simple(static_cast<int>(rank),
                                           selection.clamped.data(), nullptr)
                        : H5Screate(H5S_SCALAR),
               &H5Sclose);
    if (!selection.memorySpace.valid()) {
        throwError("Cannot create memory dataspace");
    }

    return selection;
}

DataWindow Dataset::readWindow(const std::vector<hsize_t>& offset,
                               const std::vector<hsize_t>& count) const
{
    thread::check(__func__);
    const Selection selection = selectWindow(offset, count);

    DataWindow window;
    window.offset = offset;
    window.count = selection.clamped;

    if (!selection.memorySpace.valid()) {
        return window; // an empty window: nothing was selected to read
    }

    Handle fileType(H5Dget_type(dataset_.get()), &H5Tclose);
    Handle nativeType(H5Tget_native_type(fileType.get(), H5T_DIR_ASCEND), &H5Tclose);
    if (!nativeType.valid()) {
        // selectWindow above rejects an unconvertible type, so reaching here
        // means the type became unconvertible between describing and reading.
        H5Eclear2(H5E_DEFAULT);
        throw H5Error(
            std::format("Dataset '{}' cannot be read: {}", path_,
                        info_.unreadableReason()));
    }

    const std::size_t elementSize = H5Tget_size(nativeType.get());
    if (elementSize == 0) {
        throwError("Datatype has zero size");
    }

    std::vector<unsigned char> buffer(
        static_cast<std::size_t>(selection.elements) * elementSize);
    check(H5Dread(dataset_.get(), nativeType.get(), selection.memorySpace.get(),
                  selection.fileSpace.get(), H5P_DEFAULT, buffer.data()),
          std::format("Failed to read '{}'", path_));

    // Variable-length payloads are allocated by HDF5 and must be handed back;
    // the guard covers every return path below, including a throw from
    // formatElement.
    VlenGuard reclaim(nativeType.get(), selection.memorySpace.get(), buffer.data());

    window.cells.reserve(static_cast<std::size_t>(selection.elements));
    for (hsize_t i = 0; i < selection.elements; ++i) {
        window.cells.push_back(
            formatElement(nativeType.get(), buffer.data() + i * elementSize));
    }

    return window;
}

NumericWindow Dataset::readNumericWindow(const std::vector<hsize_t>& offset,
                                         const std::vector<hsize_t>& count) const
{
    thread::check(__func__);
    // Checked before the hyperslab rather than after the read: H5Dread would
    // report a conversion failure, and "no conversion path" is a far worse
    // account of a text dataset than saying it is not numeric.
    if (!info_.isNumeric()) {
        throw H5Error(std::format(
            "Dataset '{}' holds {}, which has no numeric value", path_,
            info_.type.description));
    }

    const Selection selection = selectWindow(offset, count);

    NumericWindow window;
    window.offset = offset;
    window.count = selection.clamped;

    if (!selection.memorySpace.valid()) {
        return window;
    }

    // H5T_NATIVE_DOUBLE as the memory type, so HDF5 does the widening and this
    // file carries no switch over integer widths at all.
    window.values.resize(static_cast<std::size_t>(selection.elements));
    check(H5Dread(dataset_.get(), H5T_NATIVE_DOUBLE, selection.memorySpace.get(),
                  selection.fileSpace.get(), H5P_DEFAULT, window.values.data()),
          std::format("Failed to read '{}' as numbers", path_));

    return window;
}

ElementValue Dataset::readElement(const std::vector<hsize_t>& offset) const
{
    thread::check(__func__);
    ElementValue element;
    element.offset = offset;

    const std::vector<hsize_t> one(info_.shape.size(), 1);
    const Selection selection = selectWindow(offset, one);
    if (!selection.memorySpace.valid() || selection.elements == 0) {
        return element;
    }

    Handle fileType(H5Dget_type(dataset_.get()), &H5Tclose);
    Handle nativeType(H5Tget_native_type(fileType.get(), H5T_DIR_ASCEND), &H5Tclose);
    if (!nativeType.valid()) {
        H5Eclear2(H5E_DEFAULT);
        throw H5Error(std::format("Dataset '{}' cannot be read: {}", path_,
                                  info_.unreadableReason()));
    }

    const std::size_t elementSize = H5Tget_size(nativeType.get());
    if (elementSize == 0) {
        throwError("Datatype has zero size");
    }

    std::vector<unsigned char> buffer(elementSize);
    check(H5Dread(dataset_.get(), nativeType.get(), selection.memorySpace.get(),
                  selection.fileSpace.get(), H5P_DEFAULT, buffer.data()),
          std::format("Failed to read '{}'", path_));

    // As in readWindow: the guard covers every return path below, including a
    // throw from the formatting.
    VlenGuard reclaim(nativeType.get(), selection.memorySpace.get(), buffer.data());

    element.fields = describeCompoundElement(nativeType.get(), buffer.data());
    element.json = toJson(nativeType.get(), buffer.data());
    element.text = formatElement(nativeType.get(), buffer.data());
    return element;
}

DataWindow Dataset::readAll(hsize_t maxElements) const
{
    thread::check(__func__);
    const hsize_t total = info_.elementCount();
    if (total > maxElements) {
        throw H5Error(std::format(
            "Dataset '{}' has {} elements, above the {} element limit for a full read",
            path_, total, maxElements));
    }

    const std::vector<hsize_t> offset(info_.shape.size(), 0);
    return readWindow(offset, info_.shape);
}

} // namespace h5core
