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

    /// Immediate children of the group at `path`, in link order.
    /// Throws if `path` is not a group.
    [[nodiscard]] std::vector<NodeInfo> children(const std::string& path) const;

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
};

/// Join a parent path and a child link name into an absolute path,
/// collapsing the double slash that a naive concat produces at the root.
std::string joinPath(const std::string& parent, const std::string& name);

} // namespace h5core
