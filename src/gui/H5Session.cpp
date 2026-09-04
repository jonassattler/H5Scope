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
    file_.reset();
    path_.clear();
}

void H5Session::clearSelection()
{
    computed_.reset();
    dataset_.reset();
    datasetPath_.clear();
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
        dataset_ = std::make_unique<h5core::Dataset>(*file_, path);
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
