// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "Handle.hpp"
#include "Types.hpp"

#include <hdf5.h>

#include <string>
#include <vector>

namespace h5core {

/// An open, read-only HDF5 file.
///
/// Deliberately Qt-free so the whole backend is testable headless. All failures
/// are reported as H5Error.
class File
{
public:
    /// Open `path` read-only. Throws H5Error if it is not a readable HDF5 file.
    explicit File(const std::string& path);

    [[nodiscard]] const std::string& path() const noexcept { return path_; }
    [[nodiscard]] hid_t id() const noexcept { return file_.get(); }

    /// How much a listing is asked to find out about each name in it.
    ///
    /// Reading the link table of a group is one traversal of one structure.
    /// Following each of its links to see what kind of object is on the other
    /// end is a separate object-header read *per link*, scattered across the
    /// whole file -- which is the entire cost of listing a large group, and is
    /// wasted whenever the caller is a viewport that will show forty of eight
    /// thousand rows.
    enum class Resolve {
        /// Follow every link and report what it opens.
        Objects,
        /// Read the link table and stop. Every name, every link type and every
        /// soft or external target comes back; `kind` and `attributeCount` do
        /// not, and the caller is expected to call resolve() on the ones it
        /// actually shows. A hard link still carries its object's identity,
        /// because the link table holds that already.
        Links,
    };

    /// Immediate children of the group at `path`, in link order.
    /// Throws if `path` is not a group.
    [[nodiscard]] std::vector<NodeInfo> children(const std::string& path,
                                                 Resolve resolve = Resolve::Objects) const;

    /// Follow `node`'s link and fill in what it opens: its kind, its identity
    /// and how many attributes it carries, from one read of one object header.
    /// A link that leads nowhere comes back as NodeKind::Unresolved, which is a
    /// state of the file rather than a failure to read it. Idempotent.
    void resolve(NodeInfo& node) const;

    /// How many links the group at `path` holds, without listing them.
    ///
    /// H5Gget_info reads the count out of the group's own header; listing the
    /// group to take its size reads every object it names. The difference is
    /// the whole of the difference between a tree that draws a member count
    /// beside every group row and one that cannot afford to.
    [[nodiscard]] hsize_t memberCount(const std::string& path) const;

    /// The shape of the dataset at `path`, and whether the file calls it an
    /// image -- and nothing else. See DatasetOutline for why this is not
    /// simply Dataset::info().
    ///
    /// `mayBeImage` is the caller's answer to "does this object have any
    /// attributes at all", which it already knows from resolve(). False skips
    /// the CLASS probe entirely, because a dataset with no attributes cannot
    /// be declaring itself anything.
    [[nodiscard]] DatasetOutline datasetOutline(const std::string& path,
                                                bool mayBeImage = true) const;

    /// Describe the object at `path`, or the link there when it resolves to
    /// nothing. `path` may be "/" for the root group. Throws only when there
    /// is no link at `path` at all -- a broken soft or external link is a
    /// state of the file, not a failure to read it, and comes back as a
    /// NodeInfo whose kind is NodeKind::Unresolved.
    [[nodiscard]] NodeInfo nodeInfo(const std::string& path) const;

    /// True when the object at `path` carries at least one attribute. Used to
    /// decide whether the Metadata tab is shown at all.
    [[nodiscard]] bool hasAttributes(const std::string& path) const;

    [[nodiscard]] std::size_t attributeCount(const std::string& path) const;

    /// The datatype a committed (named) datatype object holds. Throws unless
    /// `path` names one. A named datatype is an object in its own right, and
    /// the type is the only thing it has to say.
    [[nodiscard]] TypeInfo namedType(const std::string& path) const;

    /// Whether `path` names an existing object in the file. A link that does
    /// not resolve is not an object, and this is false for it.
    [[nodiscard]] bool exists(const std::string& path) const;

    /// Whether there is a link at `path`, whatever it points at. True for a
    /// dangling soft link and for an external link into a missing file --
    /// both of which the tree shows and the reader may click on.
    [[nodiscard]] bool hasLink(const std::string& path) const;

    /// Check that this is an HDF5 file before attempting a full open.
    [[nodiscard]] static bool isHDF5(const std::string& path);

private:
    std::string path_;
    Handle file_;
    /// H5Fget_fileno, taken once at open. Half of an object's identity, and
    /// the half a hard link's own entry in a group's link table does not carry.
    unsigned long fileNumber_ = 0;
};

/// Join a parent path and a child link name into an absolute path,
/// collapsing the double slash that a naive concat produces at the root.
std::string joinPath(const std::string& parent, const std::string& name);

} // namespace h5core
