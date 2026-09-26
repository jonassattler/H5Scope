// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "H5Session.hpp"

#include "h5core/Error.hpp"

namespace gui {

void H5Session::open(const std::string& path)
{
    close();
    // Constructed straight into the member: a File that threw is a File that
    // never existed, and there is no half-open state for a later job to find.
    file_ = std::make_unique<h5core::File>(path);
    path_ = path;
}

void H5Session::close()
{
    clearSelection();
    // Before the file: a Dataset outliving the File it came from is a handle
    // into something already closed.
    heldDatasets_.clear();
    file_.reset();
    path_.clear();
}

void H5Session::clearSelection()
{
    computed_.reset();
    dataset_.reset();
    datasetPath_.clear();
    member_ = {};
}

void H5Session::setMember(h5core::MemberSelection member)
{
    if (member.text == member_.text) {
        return;
    }
    member_ = std::move(member);
    // Which member is being read is part of what was opened, not an argument to
    // a read -- so the open one goes and is opened again through the new chain,
    // here rather than at the next read. The selection's dataset being open is
    // what `source()` answers with, and the job that opened it was the one that
    // described the selection, which has already run and will not run again.
    const std::string path = datasetPath_;
    dataset_.reset();
    datasetPath_.clear();
    if (!path.empty()) {
        static_cast<void>(dataset(path));
    }
}

h5core::Dataset* H5Session::dataset(const std::string& path)
{
    if (dataset_ != nullptr && datasetPath_ == path) {
        return dataset_.get();
    }
    if (file_ == nullptr) {
        return nullptr;
    }
    try {
        dataset_ = member_.empty()
                       ? std::make_unique<h5core::Dataset>(*file_, path)
                       : std::make_unique<h5core::FieldDataset>(*file_, path, member_);
        datasetPath_ = path;
    } catch (const h5core::H5Error&) {
        // Not an error to propagate: the caller asked whether this path is a
        // readable dataset, and null is the answer. Whoever needs the reason
        // opens it themselves and lets the throw out.
        dataset_.reset();
        datasetPath_.clear();
    }
    return dataset_.get();
}

std::shared_ptr<h5core::Dataset> H5Session::held(const std::string& path,
                                                 const h5core::MemberSelection& member)
{
    // Separated by a NUL rather than run together. A link name holds a '.' as
    // freely as any other character, so `/run` read through `.3` and the
    // dataset `/run.3` spell the same string once they are concatenated -- and
    // a tab drawing one would have been handed the other, read at whatever rank
    // it happened to have. HDF5 names are C strings and cannot hold a NUL, so
    // nothing in a path can reach across it.
    std::string key = path;
    key.push_back('\0');
    key += member.text;
    for (auto& [name, dataset] : heldDatasets_) {
        if (name == key) {
            return dataset;
        }
    }
    if (file_ == nullptr) {
        return nullptr;
    }
    std::shared_ptr<h5core::Dataset> opened;
    try {
        opened = member.empty() ? std::make_shared<h5core::Dataset>(*file_, path)
                                : std::make_shared<h5core::FieldDataset>(*file_, path, member);
    } catch (const h5core::H5Error&) {
        // Null is the answer, as it is in dataset(): the caller asked whether
        // this path is a readable dataset and is about to say so in the entry's
        // own error line, which is where the reason belongs.
        return nullptr;
    }
    if (heldDatasets_.size() >= kHeldDatasets) {
        heldDatasets_.erase(heldDatasets_.begin());
    }
    heldDatasets_.emplace_back(key, opened);
    return opened;
}

void H5Session::setComputed(std::shared_ptr<const h5core::DataSource> computed)
{
    computed_ = std::move(computed);
}

const h5core::DataSource* H5Session::source() const noexcept
{
    if (computed_ != nullptr) {
        return computed_.get();
    }
    return dataset_.get();
}

} // namespace gui
