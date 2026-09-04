// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "gui/H5Thread.hpp"

#include "h5core/Attribute.hpp"
#include "h5core/Dataset.hpp"
#include "h5core/File.hpp"

#include <string>
#include <utility>
#include <vector>

namespace h5test {

/// Run `body` on the thread that owns HDF5 and wait for it.
///
/// For the handful of things a test does to a file that are not reading it --
/// writing the fixture, mostly. Everything in this process that touches the
/// library goes through that thread, and a generator is no exception just
/// because it does not go through h5core and so would not be caught by the
/// guard there.
template<typename Body>
void onH5(Body&& body)
{
    gui::H5Thread::instance().invoke([&](gui::H5Session&) {
        body();
        return 0;
    });
}

/// A test's way of reading an HDF5 file, now that reading one is not something
/// a test's own thread may do.
///
/// Every call here is a blocking round trip to the thread that owns HDF5. That
/// is exactly what the application must never do and exactly what a test
/// should: a suite that had to spin an event loop to find out how many children
/// a group has would be a suite about the event loop. The application's own
/// asynchrony is tested where it lives, in `test_h5thread` and in the model
/// suites.
///
/// The names match `h5core::File`'s, so a test reads the same as it did before
/// the file moved off this thread.
class Reader
{
public:
    explicit Reader(std::string path)
    {
        gui::H5Thread::instance().invoke([&](gui::H5Session& session) {
            session.open(path);
            return 0;
        });
    }

    ~Reader()
    {
        gui::H5Thread::instance().invoke([](gui::H5Session& session) {
            session.close();
            return 0;
        });
    }

    Reader(const Reader&) = delete;
    Reader& operator=(const Reader&) = delete;
    Reader(Reader&&) = delete;
    Reader& operator=(Reader&&) = delete;

    /// Anything at all, as a job on the HDF5 thread. `body` is handed the open
    /// file and must return plain data -- never an open identifier.
    template<typename Body>
    auto read(Body&& body) const
    {
        return gui::H5Thread::instance().invoke(
            [&](gui::H5Session& session) { return body(*session.file()); });
    }

    [[nodiscard]] std::vector<h5core::NodeInfo>
    children(const std::string& path,
             h5core::File::Resolve resolve = h5core::File::Resolve::Objects) const
    {
        return read([&](h5core::File& file) { return file.children(path, resolve); });
    }
    [[nodiscard]] h5core::NodeInfo nodeInfo(const std::string& path) const
    {
        return read([&](h5core::File& file) { return file.nodeInfo(path); });
    }
    [[nodiscard]] h5core::TypeInfo namedType(const std::string& path) const
    {
        return read([&](h5core::File& file) { return file.namedType(path); });
    }
    [[nodiscard]] hsize_t memberCount(const std::string& path) const
    {
        return read([&](h5core::File& file) { return file.memberCount(path); });
    }
    [[nodiscard]] h5core::DatasetOutline
    datasetOutline(const std::string& path, bool mayBeImage = true) const
    {
        return read(
            [&](h5core::File& file) { return file.datasetOutline(path, mayBeImage); });
    }
    [[nodiscard]] std::size_t attributeCount(const std::string& path) const
    {
        return read([&](h5core::File& file) { return file.attributeCount(path); });
    }
    [[nodiscard]] bool hasAttributes(const std::string& path) const
    {
        return read([&](h5core::File& file) { return file.hasAttributes(path); });
    }
    [[nodiscard]] bool exists(const std::string& path) const
    {
        return read([&](h5core::File& file) { return file.exists(path); });
    }
    [[nodiscard]] bool hasLink(const std::string& path) const
    {
        return read([&](h5core::File& file) { return file.hasLink(path); });
    }
    [[nodiscard]] std::vector<h5core::AttributeInfo>
    attributes(const std::string& path, std::size_t maxElements = 256) const
    {
        return read([&](h5core::File& file) {
            return h5core::readAttributes(file, path, maxElements);
        });
    }
};

/// One dataset, described once and read across the thread thereafter.
///
/// `h5core::Dataset` holds an open identifier and therefore belongs to the HDF5
/// thread; this is what a test holds instead. `info()` is a copy taken when it
/// was opened, which is what nearly every assertion wants; the three read
/// functions go across.
class Dataset
{
public:
    Dataset(const Reader& reader, std::string path) : path_(std::move(path))
    {
        info_ = reader.read([&](h5core::File& file) {
            return h5core::Dataset(file, path_).info();
        });
    }

    [[nodiscard]] const h5core::DatasetInfo& info() const { return info_; }
    [[nodiscard]] const std::string& path() const { return path_; }

    [[nodiscard]] h5core::DataWindow readWindow(const std::vector<hsize_t>& offset,
                                                const std::vector<hsize_t>& count) const
    {
        return gui::H5Thread::instance().invoke([&](gui::H5Session& session) {
            return h5core::Dataset(*session.file(), path_).readWindow(offset, count);
        });
    }
    [[nodiscard]] h5core::NumericWindow
    readNumericWindow(const std::vector<hsize_t>& offset,
                      const std::vector<hsize_t>& count) const
    {
        return gui::H5Thread::instance().invoke([&](gui::H5Session& session) {
            return h5core::Dataset(*session.file(), path_)
                .readNumericWindow(offset, count);
        });
    }
    [[nodiscard]] h5core::ElementValue
    readElement(const std::vector<hsize_t>& offset) const
    {
        return gui::H5Thread::instance().invoke([&](gui::H5Session& session) {
            return h5core::Dataset(*session.file(), path_).readElement(offset);
        });
    }

private:
    std::string path_;
    h5core::DatasetInfo info_;
};

} // namespace h5test
