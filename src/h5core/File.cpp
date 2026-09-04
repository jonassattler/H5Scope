// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "File.hpp"

#include "DataType.hpp"
#include "Error.hpp"
#include "Image.hpp"
#include "Thread.hpp"

#include <cstring>
#include <format>

namespace h5core {
namespace {

struct IterateContext {
    const File* file = nullptr;
    std::string parentPath;
    std::vector<NodeInfo>* out = nullptr;
    File::Resolve resolve = File::Resolve::Objects;
    /// The containing file's number, so a hard link's identity can be recorded
    /// without opening the object it names. Hard links never leave their file,
    /// so this is that object's file number too.
    unsigned long fileNumber = 0;
};

/// Reduce an object token to an integer, purely so the tree can spot a hard
/// link it has already visited. The token is opaque and this is not an
/// address in any meaningful sense; it is an identity, compared and nothing
/// else.
haddr_t identityOf(const H5O_token_t& token)
{
    haddr_t address = 0;
    std::memcpy(&address, &token, sizeof(address));
    return address;
}

NodeKind kindFromObjectType(H5O_type_t type)
{
    switch (type) {
    case H5O_TYPE_GROUP:           return NodeKind::Group;
    case H5O_TYPE_DATASET:         return NodeKind::Dataset;
    case H5O_TYPE_NAMED_DATATYPE:  return NodeKind::NamedDataType;
    default:                       return NodeKind::Unknown;
    }
}

/// Read what the link itself says, without following it. A soft link stores a
/// path and an external link stores a file and a path; both are text in the
/// file, and both are worth showing whether or not they lead anywhere.
void describeLink(hid_t location, const char* name, const H5L_info2_t& info,
                  NodeInfo& node)
{
    if (info.type == H5L_TYPE_HARD) {
        node.link = LinkType::Hard;
        return;
    }
    node.link = (info.type == H5L_TYPE_EXTERNAL) ? LinkType::External : LinkType::Soft;

    std::vector<char> value(info.u.val_size + 1, '\0');
    if (H5Lget_val(location, name, value.data(), info.u.val_size, H5P_DEFAULT) < 0) {
        H5Eclear2(H5E_DEFAULT);
        return;
    }

    if (node.link == LinkType::Soft) {
        node.linkTarget = value.data();
        return;
    }

    unsigned flags = 0;
    const char* file = nullptr;
    const char* object = nullptr;
    if (H5Lunpack_elink_val(value.data(), info.u.val_size, &flags, &file, &object) < 0) {
        H5Eclear2(H5E_DEFAULT);
        return;
    }
    node.linkFile = (file != nullptr) ? file : "";
    node.linkTarget = (object != nullptr) ? object : "";
}

/// Follow the link and record what it opened. Every link type is probed the
/// same way, so an external link that resolves reports the group or dataset it
/// resolves to and is browsable on exactly those terms.
void resolveObject(hid_t location, const char* name, NodeInfo& node)
{
    // Probing first: a link pointing nowhere is a normal state for a file, not
    // an error, and dereferencing it would be one.
    //
    // Only for the two link types that can point nowhere, though. A hard link
    // *is* its object -- there is nothing to dangle -- so the probe on one is
    // a whole object-header read that can only ever answer yes, and in an
    // ordinary file almost every link is hard.
    if (node.link != LinkType::Hard
        && H5Oexists_by_name(location, name, H5P_DEFAULT) <= 0) {
        H5Eclear2(H5E_DEFAULT);
        node.kind = NodeKind::Unresolved;
        return;
    }

    // BASIC and NUM_ATTRS out of one read. The attribute count is in the
    // object header beside the type, and every tree row wants both.
    H5O_info2_t objectInfo{};
    if (H5Oget_info_by_name3(location, name, &objectInfo,
                             H5O_INFO_BASIC | H5O_INFO_NUM_ATTRS, H5P_DEFAULT)
        < 0) {
        H5Eclear2(H5E_DEFAULT);
        node.kind = NodeKind::Unresolved;
        return;
    }

    node.kind = kindFromObjectType(objectInfo.type);
    node.fileNumber = objectInfo.fileno;
    node.attributeCount = static_cast<std::size_t>(objectInfo.num_attrs);
    node.address = identityOf(objectInfo.token);
}

/// H5Literate2 callback. Never throws: HDF5 unwinds C frames between us and
/// the caller, so a link that will not resolve is recorded as such instead.
herr_t linkCallback(hid_t group, const char* name, const H5L_info2_t* info, void* opData)
{
    auto* ctx = static_cast<IterateContext*>(opData);
    if (ctx == nullptr || name == nullptr || info == nullptr) {
        return 0;
    }

    NodeInfo node;
    node.name = name;
    node.path = joinPath(ctx->parentPath, name);

    describeLink(group, name, *info, node);
    if (ctx->resolve == File::Resolve::Objects) {
        resolveObject(group, name, node);
    } else if (info->type == H5L_TYPE_HARD) {
        // The link table already holds the object's token for a hard link, so
        // identity costs nothing here even though the kind is not being
        // fetched. That is what keeps cycle detection at listing time: a group
        // that closes a loop is caught before anything is opened.
        node.fileNumber = ctx->fileNumber;
        node.address = identityOf(info->u.token);
    }

    ctx->out->push_back(std::move(node));
    return 0;
}

} // namespace

std::string joinPath(const std::string& parent, const std::string& name)
{
    if (parent.empty() || parent == "/") {
        return "/" + name;
    }
    return parent + "/" + name;
}

bool File::isHDF5(const std::string& path)
{
    thread::check(__func__);
    const htri_t result = H5Fis_accessible(path.c_str(), H5P_DEFAULT);
    if (result < 0) {
        H5Eclear2(H5E_DEFAULT);
        return false;
    }
    return result > 0;
}

File::File(const std::string& path) : path_(path)
{
    thread::check("File::File");
    initErrorHandling();

    file_ = Handle(H5Fopen(path.c_str(), H5F_ACC_RDONLY, H5P_DEFAULT), &H5Fclose);
    if (!file_.valid()) {
        throwError(std::format("Cannot open '{}' as an HDF5 file", path));
    }

    // Asked once and kept, so that listing a group can record a hard link's
    // identity without opening what it names -- see IterateContext.
    if (H5Fget_fileno(file_.get(), &fileNumber_) < 0) {
        H5Eclear2(H5E_DEFAULT);
        fileNumber_ = 0;
    }
}

bool File::exists(const std::string& path) const
{
    thread::check(__func__);
    if (path == "/") {
        return true;
    }
    const htri_t result = H5Oexists_by_name(file_.get(), path.c_str(), H5P_DEFAULT);
    if (result < 0) {
        H5Eclear2(H5E_DEFAULT);
        return false;
    }
    return result > 0;
}

std::vector<NodeInfo> File::children(const std::string& path, Resolve resolve) const
{
    thread::check(__func__);
    Handle group(H5Gopen2(file_.get(), path.c_str(), H5P_DEFAULT), &H5Gclose);
    if (!group.valid()) {
        throwError(std::format("Cannot open group '{}'", path));
    }

    std::vector<NodeInfo> result;
    IterateContext ctx{this, path, &result, resolve, fileNumber_};

    // The count is free -- it is in the group's own header, which is open --
    // and reserving on it saves a listing of eight thousand names some
    // thirteen reallocations and copies.
    H5G_info_t groupInfo{};
    if (H5Gget_info(group.get(), &groupInfo) >= 0) {
        result.reserve(static_cast<std::size_t>(groupInfo.nlinks));
    } else {
        H5Eclear2(H5E_DEFAULT);
    }

    hsize_t index = 0;
    // Increasing-name order keeps the tree stable between runs; HDF5's native
    // link order is creation order, which varies between writers.
    if (H5Literate2(group.get(), H5_INDEX_NAME, H5_ITER_INC, &index, &linkCallback, &ctx) < 0) {
        throwError(std::format("Failed to list children of '{}'", path));
    }

    return result;
}

void File::resolve(NodeInfo& node) const
{
    thread::check(__func__);
    resolveObject(file_.get(), node.path.c_str(), node);
}

hsize_t File::memberCount(const std::string& path) const
{
    thread::check(__func__);
    H5G_info_t info{};
    if (H5Gget_info_by_name(file_.get(), path.c_str(), &info, H5P_DEFAULT) < 0) {
        throwError(std::format("Cannot count the members of '{}'", path));
    }
    return info.nlinks;
}

DatasetOutline File::datasetOutline(const std::string& path, bool mayBeImage) const
{
    thread::check(__func__);
    Handle dataset(H5Dopen2(file_.get(), path.c_str(), H5P_DEFAULT), &H5Dclose);
    if (!dataset.valid()) {
        throwError(std::format("Cannot open dataset '{}'", path));
    }

    Handle space(H5Dget_space(dataset.get()), &H5Sclose);
    if (!space.valid()) {
        throwError(std::format("Cannot read dataspace of '{}'", path));
    }

    DatasetOutline outline;
    switch (H5Sget_simple_extent_type(space.get())) {
    case H5S_SCALAR: outline.space = Dataspace::Scalar; break;
    case H5S_NULL:   outline.space = Dataspace::Null; break;
    default:         outline.space = Dataspace::Simple; break;
    }

    const int rank = H5Sget_simple_extent_ndims(space.get());
    if (rank > 0) {
        outline.shape.resize(static_cast<std::size_t>(rank));
        if (H5Sget_simple_extent_dims(space.get(), outline.shape.data(), nullptr) < 0) {
            H5Eclear2(H5E_DEFAULT);
            outline.shape.clear();
        }
    }

    if (mayBeImage) {
        // Only the two attributes that decide the tag, not the whole of the
        // Image specification: the rest of it is read when the Data Viewer
        // opens the picture, which is when it is needed.
        if (const auto image = readImageOutline(dataset.get())) {
            outline.image = true;
            outline.subclass = *image;
        }
    }
    return outline;
}

bool File::hasLink(const std::string& path) const
{
    thread::check(__func__);
    if (path == "/") {
        return true;
    }
    const htri_t result = H5Lexists(file_.get(), path.c_str(), H5P_DEFAULT);
    if (result < 0) {
        H5Eclear2(H5E_DEFAULT);
        return false;
    }
    return result > 0;
}

NodeInfo File::nodeInfo(const std::string& path) const
{
    thread::check(__func__);
    NodeInfo node;
    node.path = path;

    if (path == "/") {
        node.name = "/";
    } else {
        const auto slash = path.find_last_of('/');
        node.name = (slash == std::string::npos) ? path : path.substr(slash + 1);
    }

    if (path != "/") {
        if (!hasLink(path)) {
            throwError(std::format("Nothing is linked at '{}'", path));
        }
        H5L_info2_t linkInfo{};
        if (H5Lget_info2(file_.get(), path.c_str(), &linkInfo, H5P_DEFAULT) >= 0) {
            describeLink(file_.get(), path.c_str(), linkInfo, node);
        } else {
            H5Eclear2(H5E_DEFAULT);
        }
    }

    resolveObject(file_.get(), path.c_str(), node);
    return node;
}

TypeInfo File::namedType(const std::string& path) const
{
    thread::check(__func__);
    Handle type(H5Topen2(file_.get(), path.c_str(), H5P_DEFAULT), &H5Tclose);
    if (!type.valid()) {
        throwError(std::format("Cannot open named datatype '{}'", path));
    }
    return describeType(type.get());
}

std::size_t File::attributeCount(const std::string& path) const
{
    thread::check(__func__);
    H5O_info2_t info{};
    if (H5Oget_info_by_name3(file_.get(), path.c_str(), &info, H5O_INFO_NUM_ATTRS,
                             H5P_DEFAULT) < 0) {
        throwError(std::format("Cannot count attributes of '{}'", path));
    }
    return static_cast<std::size_t>(info.num_attrs);
}

bool File::hasAttributes(const std::string& path) const
{
    return attributeCount(path) > 0;
}

} // namespace h5core
