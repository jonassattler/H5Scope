// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "h5core/DataSource.hpp"
#include "h5core/Dataset.hpp"
#include "h5core/File.hpp"

#include <memory>
#include <string>

namespace gui {

/// Everything HDF5 owns, kept on the one thread that is allowed to touch it.
///
/// The open file, the dataset the views are drawing, and the pipeline's output
/// when one is running. None of it is reachable from the GUI thread: a
/// reference to this is handed to a job by `H5Thread`, and a job runs on the
/// HDF5 thread by construction. That is the whole of the containment -- there
/// is no `shared_ptr<File>` for a model to hold and no way to get one.
///
/// It matters for destruction as much as for use. A `h5core::File` whose last
/// reference happened to be dropped on the GUI thread would call H5Fclose
/// there, which is exactly the kind of violation that used to be invisible and
/// now aborts. Owning it here, by value, means it is opened, used and closed in
/// one place.
///
/// The pipeline's output is here for a reason that is easy to miss: a
/// `postproc::ComputedDataset` holds nothing but doubles, but it *renders* them
/// through `h5core::formatElement`, which is HDF5's own type machinery. It is
/// no freer of this thread than the file is.
class H5Session
{
public:
    H5Session() = default;
    ~H5Session() = default;

    H5Session(const H5Session&) = delete;
    H5Session& operator=(const H5Session&) = delete;
    H5Session(H5Session&&) = delete;
    H5Session& operator=(H5Session&&) = delete;

    /// Open `path`, closing whatever was open first. Throws h5core::H5Error,
    /// which the job's caller turns into a message.
    void open(const std::string& path);
    void close();

    [[nodiscard]] bool isOpen() const noexcept { return file_ != nullptr; }
    /// The open file, or null. A plain pointer because every caller is already
    /// on the HDF5 thread and the session outlives the job.
    [[nodiscard]] h5core::File* file() noexcept { return file_.get(); }
    [[nodiscard]] const std::string& path() const noexcept { return path_; }

    /// The dataset at `path`, opened if it is not already the one open.
    /// Kept between calls so that scrolling a table does not reopen it once a
    /// row. Returns null and leaves the session unchanged if it will not open.
    [[nodiscard]] h5core::Dataset* dataset(const std::string& path);
    /// Drop the open dataset and any computed result over it.
    void clearSelection();

    /// Install the pipeline's output as what the views read. Passing nullptr
    /// puts them back on the file.
    void setComputed(std::shared_ptr<const h5core::DataSource> computed);

    /// What the views actually draw: the computed result when a pipeline is
    /// running, the dataset otherwise, null when neither is there.
    [[nodiscard]] const h5core::DataSource* source() const noexcept;

private:
    std::unique_ptr<h5core::File> file_;
    std::string path_;
    std::string datasetPath_;
    std::unique_ptr<h5core::Dataset> dataset_;
    std::shared_ptr<const h5core::DataSource> computed_;
};

} // namespace gui
