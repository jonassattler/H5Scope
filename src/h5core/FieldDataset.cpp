// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "FieldDataset.hpp"

#include "DataType.hpp"
#include "Error.hpp"
#include "Thread.hpp"

#include <algorithm>
#include <cstring>
#include <format>
#include <functional>
#include <limits>
#include <numeric>

namespace h5core {

namespace {

hsize_t product(const std::vector<hsize_t>& dims)
{
    return std::accumulate(dims.begin(), dims.end(), static_cast<hsize_t>(1),
                           std::multiplies<>{});
}

/// One level of the walk down a chain: the member's type, and the array
/// dimensions it was wrapped in if it had any.
struct Level {
    Handle type;
    std::vector<hsize_t> dims; ///< empty unless the member is an H5T_ARRAY
};

std::vector<hsize_t> arrayDimsOf(hid_t type)
{
    if (H5Tget_class(type) != H5T_ARRAY) {
        return {};
    }
    const int rank = H5Tget_array_ndims(type);
    std::vector<hsize_t> dims(static_cast<std::size_t>(std::max(rank, 0)));
    if (rank > 0) {
        H5Tget_array_dims2(type, dims.data());
    }
    return dims;
}

/// Walk a chain down a dataset's datatype, opening each member on the way.
///
/// The names are checked as well as the indices. The selection was resolved
/// against a TypeInfo -- a description, made earlier, of what may by now be a
/// different file -- and a member index that still exists under a different
/// name is the one way this could read the wrong field and say nothing at all.
std::vector<Level> walkChain(hid_t fileType, const MemberSelection& member,
                             const std::string& path)
{
    std::vector<Level> levels;
    Handle current(H5Tcopy(fileType), &H5Tclose);

    for (const MemberLink& link : member.links) {
        // A chain may pass through an array of compounds, in which case the
        // compound to look the member up in is the one the array holds.
        while (current.valid() && H5Tget_class(current.get()) == H5T_ARRAY) {
            current = Handle(H5Tget_super(current.get()), &H5Tclose);
        }
        if (!current.valid() || H5Tget_class(current.get()) != H5T_COMPOUND) {
            throw H5Error(std::format(
                "'{}' has no member '{}': what holds it is not a compound", path,
                link.name));
        }
        const int count = H5Tget_nmembers(current.get());
        if (link.index >= static_cast<unsigned>(std::max(count, 0))) {
            throw H5Error(std::format("'{}' has no member '{}'", path, link.name));
        }
        char* found = H5Tget_member_name(current.get(), link.index);
        const std::string name = (found != nullptr) ? std::string(found) : std::string{};
        if (found != nullptr) {
            H5free_memory(found);
        }
        if (name != link.name) {
            throw H5Error(std::format("'{}' member {} is '{}', not '{}'", path,
                                      link.index, name, link.name));
        }

        Handle memberType(H5Tget_member_type(current.get(), link.index), &H5Tclose);
        if (!memberType.valid()) {
            throwError(std::format("Cannot read the type of '{}.{}'", path, link.name));
        }
        levels.push_back(Level{Handle(H5Tcopy(memberType.get()), &H5Tclose),
                               arrayDimsOf(memberType.get())});
        current = std::move(memberType);
    }
    return levels;
}

/// The flat index, within a member's own axes, of the point `offset + step`.
hsize_t flatten(const std::vector<hsize_t>& dims, const std::vector<hsize_t>& offset,
                const std::vector<hsize_t>& step)
{
    hsize_t flat = 0;
    for (std::size_t d = 0; d < dims.size(); ++d) {
        flat = flat * dims[d] + offset[d] + step[d];
    }
    return flat;
}

/// Advance an odometer over `count`, least significant axis first. False when
/// it has wrapped past the end, which is how the walk below terminates.
bool advance(std::vector<hsize_t>& step, const std::vector<hsize_t>& count)
{
    for (std::size_t d = step.size(); d-- > 0;) {
        if (++step[d] < count[d]) {
            return true;
        }
        step[d] = 0;
    }
    return false;
}

} // namespace

FieldDataset::FieldDataset(const File& file, const std::string& path,
                           MemberSelection member)
    : Dataset(file, path), member_(std::move(member))
{
    thread::check(__func__);

    name_ = path + member_.text;
    originRank_ = info_.shape.size();

    Handle fileType(H5Dget_type(dataset_.get()), &H5Tclose);
    if (!fileType.valid()) {
        throwError(std::format("Cannot read the datatype of '{}'", path));
    }
    // Throws when the chain does not apply. Its own message is the useful one.
    (void)walkChain(fileType.get(), member_, path);

    // The derived dataset keeps the storage facts -- the layout, the filters,
    // the external files -- because they are all still true: this is the same
    // dataset read through a narrower datatype. Only the shape and the type are
    // the chain's to change. The image attributes are not kept, for
    // ComputedDataset's reason: the Image spec fixes which dimension is height
    // and which is colour, and a member projection has just made that untrue.
    derived_ = info_;
    derived_.type = member_.type;
    derived_.shape = info_.shape;
    derived_.shape.insert(derived_.shape.end(), member_.dims.begin(),
                          member_.dims.end());
    derived_.maxShape = derived_.shape;
    derived_.chunk.clear();
    derived_.image.reset();
    if (derived_.space == Dataspace::Scalar && !member_.dims.empty()) {
        derived_.space = Dataspace::Simple;
    }
}

Handle FieldDataset::memoryTypeFor(bool asDouble) const
{
    Handle fileType(H5Dget_type(dataset_.get()), &H5Tclose);
    if (!fileType.valid()) {
        throwError(std::format("Cannot read the datatype of '{}'", path_));
    }
    const std::vector<Level> levels = walkChain(fileType.get(), member_, path_);
    if (levels.empty()) {
        throwError("A member chain with no members");
    }

    // The innermost type: what one value of the result is in memory. An array
    // is unwrapped here, because its dimensions are axes of the result and get
    // rebuilt on the way back up. A vlen is not: it is one value.
    Handle leaf(H5Tcopy(levels.back().type.get()), &H5Tclose);
    if (H5Tget_class(leaf.get()) == H5T_ARRAY) {
        leaf = Handle(H5Tget_super(leaf.get()), &H5Tclose);
    }

    Handle inner;
    if (H5Tget_class(leaf.get()) == H5T_VLEN) {
        Handle base(H5Tget_super(leaf.get()), &H5Tclose);
        Handle baseMemory =
            asDouble ? Handle(H5Tcopy(H5T_NATIVE_DOUBLE), &H5Tclose)
                     : Handle(H5Tget_native_type(base.get(), H5T_DIR_ASCEND), &H5Tclose);
        if (!baseMemory.valid()) {
            throwError(std::format("'{}' cannot be read into memory", name_));
        }
        inner = Handle(H5Tvlen_create(baseMemory.get()), &H5Tclose);
    } else if (asDouble) {
        inner = Handle(H5Tcopy(H5T_NATIVE_DOUBLE), &H5Tclose);
    } else {
        inner = Handle(H5Tget_native_type(leaf.get(), H5T_DIR_ASCEND), &H5Tclose);
    }
    if (!inner.valid()) {
        throwError(std::format("'{}' cannot be read into memory", name_));
    }

    // Back up the chain, wrapping each level in a compound holding that one
    // member and nothing else. HDF5 matches compound members by name, so a
    // memory type naming one field *is* a read of one field: the rest of the
    // struct never leaves the file.
    for (std::size_t i = levels.size(); i-- > 0;) {
        Handle held = std::move(inner);
        if (!levels[i].dims.empty()) {
            held = Handle(H5Tarray_create2(held.get(),
                                           static_cast<unsigned>(levels[i].dims.size()),
                                           levels[i].dims.data()),
                          &H5Tclose);
            if (!held.valid()) {
                throwError("Cannot rebuild an array member in memory");
            }
        }
        const std::size_t size = H5Tget_size(held.get());
        if (size == 0) {
            throwError("A member with no size");
        }
        Handle wrapper(H5Tcreate(H5T_COMPOUND, size), &H5Tclose);
        if (!wrapper.valid()) {
            throwError("Cannot build a partial compound type");
        }
        check(H5Tinsert(wrapper.get(), member_.links[i].name.c_str(), 0, held.get()),
              std::format("Cannot select member '{}'", member_.links[i].name));
        inner = std::move(wrapper);
    }
    return inner;
}

FieldDataset::Extract FieldDataset::extract(const std::vector<hsize_t>& offset,
                                            const std::vector<hsize_t>& count,
                                            bool asDouble) const
{
    const std::size_t rank = derived_.shape.size();
    if (offset.size() != rank || count.size() != rank) {
        throw H5Error(std::format("Window rank {} does not match rank {} for '{}'",
                                  offset.size(), rank, name_));
    }

    // The leading axes are the dataset's own and are selected in the file. The
    // trailing ones are the member's, and are walked in memory afterwards,
    // because HDF5 cannot take a hyperslab inside an H5T_ARRAY member.
    const auto split = static_cast<long>(originRank_);
    const std::vector<hsize_t> leadOffset(offset.begin(), offset.begin() + split);
    const std::vector<hsize_t> leadCount(count.begin(), count.begin() + split);

    Extract read;
    read.trailOffset.assign(offset.begin() + split, offset.end());
    read.trailCount.assign(count.begin() + split, count.end());
    for (std::size_t d = 0; d < member_.dims.size(); ++d) {
        read.trailOffset[d] = std::min(read.trailOffset[d], member_.dims[d]);
        read.trailCount[d] =
            std::min(read.trailCount[d], member_.dims[d] - read.trailOffset[d]);
    }

    Selection selection = selectWindow(leadOffset, leadCount);
    read.leadCount = selection.clamped;
    read.leading = selection.elements;

    read.memoryType = memoryTypeFor(asDouble);
    read.stride = H5Tget_size(read.memoryType.get());
    const auto perElement = static_cast<std::size_t>(std::max<hsize_t>(
        product(member_.dims), 1));
    read.width = read.stride / perElement;

    // What one value is, for formatting: the vlen itself when the whole list is
    // the value, its base when an index has picked one element out of it.
    read.valueType = valueTypeOf(read.memoryType.get());

    if (!selection.memorySpace.valid() || read.leading == 0
        || product(read.trailCount) == 0) {
        read.leading = 0;
        return read;
    }

    read.memorySpace = std::move(selection.memorySpace);
    const std::optional<std::size_t> bytes = bufferBytes(read.leading, read.stride);
    if (!bytes.has_value()) {
        throw H5Error(std::format("A window of {} elements of '{}' is larger than memory",
                                  read.leading, name_));
    }
    read.values.resize(*bytes);
    check(H5Dread(dataset_.get(), read.memoryType.get(), read.memorySpace.get(),
                  selection.fileSpace.get(), H5P_DEFAULT, read.values.data()),
          std::format("Failed to read '{}'", name_));
    return read;
}

Handle FieldDataset::valueTypeOf(hid_t memoryType) const
{
    // Down through the wrappers to what they hold, which is where the value is.
    Handle current(H5Tcopy(memoryType), &H5Tclose);
    for (std::size_t i = 0; i < member_.links.size(); ++i) {
        Handle held(H5Tget_member_type(current.get(), 0), &H5Tclose);
        current = std::move(held);
        if (H5Tget_class(current.get()) == H5T_ARRAY) {
            current = Handle(H5Tget_super(current.get()), &H5Tclose);
        }
    }
    const bool indexed =
        !member_.links.empty() && member_.links.back().vlenIndex.has_value();
    if (indexed && H5Tget_class(current.get()) == H5T_VLEN) {
        current = Handle(H5Tget_super(current.get()), &H5Tclose);
    }
    return current;
}

const unsigned char* FieldDataset::valueAt(const Extract& read, hsize_t element,
                                           hsize_t slot) const
{
    const unsigned char* at = read.values.data()
                              + static_cast<std::size_t>(element) * read.stride
                              + static_cast<std::size_t>(slot) * read.width;

    const std::optional<hsize_t> index =
        member_.links.empty() ? std::nullopt : member_.links.back().vlenIndex;
    if (!index.has_value()) {
        return at;
    }

    // An indexed vlen: the value is inside the list this record happens to
    // carry, and a record whose list is shorter simply has none there. Null is
    // that answer, and each read turns it into the one it can show -- an empty
    // cell, or a gap in a line.
    hvl_t list{};
    std::memcpy(&list, at, sizeof(hvl_t));
    if (list.p == nullptr || *index >= list.len) {
        return nullptr;
    }
    return static_cast<const unsigned char*>(list.p)
           + static_cast<std::size_t>(*index) * H5Tget_size(read.valueType.get());
}

DataWindow FieldDataset::readWindow(const std::vector<hsize_t>& offset,
                                    const std::vector<hsize_t>& count) const
{
    thread::check(__func__);
    const Extract read = extract(offset, count, false);

    DataWindow window;
    window.offset = offset;
    window.count = read.leadCount;
    window.count.insert(window.count.end(), read.trailCount.begin(),
                        read.trailCount.end());
    if (read.leading == 0) {
        return window;
    }

    VlenGuard reclaim(read.memoryType.get(), read.memorySpace.get(),
                      const_cast<unsigned char*>(read.values.data()));

    window.cells.reserve(static_cast<std::size_t>(read.leading)
                         * static_cast<std::size_t>(product(read.trailCount)));
    for (hsize_t e = 0; e < read.leading; ++e) {
        std::vector<hsize_t> step(read.trailCount.size(), 0);
        do {
            const hsize_t slot =
                flatten(member_.dims, read.trailOffset, step);
            const unsigned char* at = valueAt(read, e, slot);
            window.cells.push_back(at != nullptr
                                       ? formatElement(read.valueType.get(), at)
                                       : std::string{});
        } while (advance(step, read.trailCount));
    }
    return window;
}

NumericWindow FieldDataset::readNumericWindow(const std::vector<hsize_t>& offset,
                                              const std::vector<hsize_t>& count) const
{
    thread::check(__func__);
    if (!isNumeric(derived_.type.cls)) {
        throw H5Error(std::format("'{}' holds {}, which has no numeric value", name_,
                                  derived_.type.description));
    }
    const Extract read = extract(offset, count, true);

    NumericWindow window;
    window.offset = offset;
    window.count = read.leadCount;
    window.count.insert(window.count.end(), read.trailCount.begin(),
                        read.trailCount.end());
    if (read.leading == 0) {
        return window;
    }

    VlenGuard reclaim(read.memoryType.get(), read.memorySpace.get(),
                      const_cast<unsigned char*>(read.values.data()));

    window.values.reserve(static_cast<std::size_t>(read.leading)
                          * static_cast<std::size_t>(product(read.trailCount)));
    for (hsize_t e = 0; e < read.leading; ++e) {
        std::vector<hsize_t> step(read.trailCount.size(), 0);
        do {
            const hsize_t slot = flatten(member_.dims, read.trailOffset, step);
            const unsigned char* at = valueAt(read, e, slot);
            // A record whose vlen is too short has no value here, and a NaN is
            // what says so to a plot: PlotProjection already ends a stroke on
            // one rather than drawing a line through nothing.
            double value = std::numeric_limits<double>::quiet_NaN();
            if (at != nullptr) {
                std::memcpy(&value, at, sizeof(double));
            }
            window.values.push_back(value);
        } while (advance(step, read.trailCount));
    }
    return window;
}

ElementValue FieldDataset::readElement(const std::vector<hsize_t>& offset) const
{
    thread::check(__func__);
    const std::vector<hsize_t> one(derived_.shape.size(), 1);
    const Extract read = extract(offset, one, false);

    ElementValue element;
    element.offset = offset;
    if (read.leading == 0) {
        return element;
    }

    VlenGuard reclaim(read.memoryType.get(), read.memorySpace.get(),
                      const_cast<unsigned char*>(read.values.data()));

    const hsize_t slot = flatten(member_.dims, read.trailOffset,
                                 std::vector<hsize_t>(read.trailCount.size(), 0));
    const unsigned char* at = valueAt(read, 0, slot);
    if (at == nullptr) {
        return element;
    }
    element.fields = describeCompoundElement(read.valueType.get(), at);
    element.json = toJson(read.valueType.get(), at);
    element.text = formatElement(read.valueType.get(), at);
    return element;
}

} // namespace h5core
