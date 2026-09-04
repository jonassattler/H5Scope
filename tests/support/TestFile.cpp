// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "TestFile.hpp"

#include <hdf5.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <random>
#include <string>
#include <stdexcept>
#include <vector>

namespace h5test {
namespace {

std::filesystem::path makeUniqueDirectory(const std::string& stem)
{
    static std::atomic<unsigned> counter{0};
    std::random_device device;
    const auto suffix = std::to_string(device()) + "_" + std::to_string(counter++);
    auto dir = std::filesystem::temp_directory_path() / ("h5scope_" + stem + "_" + suffix);
    std::filesystem::create_directories(dir);
    return dir;
}

void must(herr_t status, const char* what)
{
    if (status < 0) {
        throw std::runtime_error(std::string("fixture: ") + what);
    }
}

hid_t mustId(hid_t id, const char* what)
{
    if (id < 0) {
        throw std::runtime_error(std::string("fixture: ") + what);
    }
    return id;
}

/// Write a simple dataset of `type` with the given dims from `data`.
void writeDataset(hid_t parent, const char* name, hid_t type,
                  const std::vector<hsize_t>& dims, const void* data)
{
    const hid_t space =
        dims.empty() ? mustId(H5Screate(H5S_SCALAR), "scalar space")
                     : mustId(H5Screate_simple(static_cast<int>(dims.size()), dims.data(),
                                               nullptr),
                              "simple space");
    const hid_t dset = mustId(
        H5Dcreate2(parent, name, type, space, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT),
        "create dataset");
    if (data != nullptr) {
        must(H5Dwrite(dset, type, H5S_ALL, H5S_ALL, H5P_DEFAULT, data), "write dataset");
    }
    H5Dclose(dset);
    H5Sclose(space);
}

void writeStringAttribute(hid_t object, const char* name, const char* value)
{
    const hid_t type = mustId(H5Tcopy(H5T_C_S1), "copy string type");
    must(H5Tset_size(type, H5T_VARIABLE), "vlen string");
    const hid_t space = mustId(H5Screate(H5S_SCALAR), "attr space");
    const hid_t attr =
        mustId(H5Acreate2(object, name, type, space, H5P_DEFAULT, H5P_DEFAULT),
               "create attribute");
    must(H5Awrite(attr, type, &value), "write attribute");
    H5Aclose(attr);
    H5Sclose(space);
    H5Tclose(type);
}

void writeIntAttribute(hid_t object, const char* name, std::int32_t value)
{
    const hid_t space = mustId(H5Screate(H5S_SCALAR), "attr space");
    const hid_t attr = mustId(
        H5Acreate2(object, name, H5T_NATIVE_INT32, space, H5P_DEFAULT, H5P_DEFAULT),
        "create attribute");
    must(H5Awrite(attr, H5T_NATIVE_INT32, &value), "write attribute");
    H5Aclose(attr);
    H5Sclose(space);
}

struct Record {
    std::int32_t id;
    double value;
};

} // namespace

TempFile::TempFile(std::string stem)
    : directory_(makeUniqueDirectory(stem)),
      path_((directory_ / (stem + ".h5")).string())
{
}

TempFile::~TempFile()
{
    std::error_code ec;
    std::filesystem::remove_all(directory_, ec);
}

void writeFixture(const std::string& path)
{
    const hid_t file = mustId(
        H5Fcreate(path.c_str(), H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT), "create file");

    // --- scalar with an attribute ---------------------------------------
    {
        const std::int32_t value = 42;
        writeDataset(file, "scalar_int", H5T_NATIVE_INT32, {}, &value);
        const hid_t dset = mustId(H5Dopen2(file, "scalar_int", H5P_DEFAULT), "open scalar");
        writeStringAttribute(dset, "units", "kelvin");
        H5Dclose(dset);
    }

    // --- 1-D / 2-D / 3-D numeric ----------------------------------------
    {
        const std::array<std::int32_t, 5> vec{1, 2, 3, 4, 5};
        writeDataset(file, "vec_int", H5T_NATIVE_INT32, {5}, vec.data());

        // Row-major 4x3, values chosen so row/column mix-ups are obvious.
        std::array<double, 12> matrix{};
        for (std::size_t r = 0; r < 4; ++r) {
            for (std::size_t c = 0; c < 3; ++c) {
                matrix[r * 3 + c] = static_cast<double>(r) * 10.0 + static_cast<double>(c);
            }
        }
        writeDataset(file, "matrix", H5T_NATIVE_DOUBLE, {4, 3}, matrix.data());

        std::array<std::int32_t, 24> cube{};
        for (std::size_t i = 0; i < cube.size(); ++i) {
            cube[i] = static_cast<std::int32_t>(i);
        }
        writeDataset(file, "cube", H5T_NATIVE_INT32, {2, 3, 4}, cube.data());

        // Rank 4, so a table layout has to spread more than one dimension
        // across an axis. Values are the row-major flat index, which makes any
        // coordinate mix-up arithmetic rather than a guess:
        // element (a,b,c,d) holds a*60 + b*20 + c*5 + d.
        std::vector<std::int32_t> hypercube(120);
        for (std::size_t i = 0; i < hypercube.size(); ++i) {
            hypercube[i] = static_cast<std::int32_t>(i);
        }
        writeDataset(file, "hypercube", H5T_NATIVE_INT32, {2, 3, 4, 5},
                     hypercube.data());
    }

    // --- strings, fixed and variable length ------------------------------
    {
        constexpr std::size_t width = 8;
        const hid_t type = mustId(H5Tcopy(H5T_C_S1), "copy string type");
        must(H5Tset_size(type, width), "fixed string size");
        std::array<char, width * 3> fixed{};
        const char* words[] = {"alpha", "beta", "gamma"};
        for (std::size_t i = 0; i < 3; ++i) {
            std::snprintf(fixed.data() + i * width, width, "%s", words[i]);
        }
        writeDataset(file, "str_fixed", type, {3}, fixed.data());
        H5Tclose(type);

        const hid_t vtype = mustId(H5Tcopy(H5T_C_S1), "copy string type");
        must(H5Tset_size(vtype, H5T_VARIABLE), "vlen string size");
        const char* vlen[] = {"one", "three three", "five five five"};
        writeDataset(file, "str_vlen", vtype, {3}, vlen);

        // One string, long enough and with enough newlines that a viewer has
        // to scroll it rather than fit it in a grid cell. This is the shape a
        // provenance record or an embedded config takes in a real file.
        static const std::string document = [] {
            std::string text = "H5Scope fixture document.\n\n";
            for (int paragraph = 0; paragraph < 12; ++paragraph) {
                text += "Paragraph " + std::to_string(paragraph)
                        + ": a line of prose long enough to need wrapping at any "
                          "reasonable pane width, repeated so the whole thing "
                          "cannot fit on one screen.\n";
            }
            return text;
        }();
        const char* scalar[] = {document.c_str()};
        writeDataset(file, "str_scalar", vtype, {}, scalar);

        // Rank 2, so the flat list has to label elements "[r, c]" rather than
        // by a single subscript.
        const char* grid[] = {"north west", "north east", "south west",
                              "south east"};
        writeDataset(file, "str_grid", vtype, {2, 2}, grid);
        H5Tclose(vtype);
    }

    // --- compound ---------------------------------------------------------
    {
        const hid_t type = mustId(H5Tcreate(H5T_COMPOUND, sizeof(Record)), "compound");
        must(H5Tinsert(type, "id", HOFFSET(Record, id), H5T_NATIVE_INT32), "insert id");
        must(H5Tinsert(type, "value", HOFFSET(Record, value), H5T_NATIVE_DOUBLE),
             "insert value");
        const std::array<Record, 2> records{Record{7, 1.5}, Record{9, 2.5}};
        writeDataset(file, "compound", type, {2}, records.data());
        H5Tclose(type);
    }

    // --- enum -------------------------------------------------------------
    {
        const hid_t type = mustId(H5Tenum_create(H5T_NATIVE_INT32), "enum");
        std::int32_t value = 0;
        value = 0; must(H5Tenum_insert(type, "RED", &value), "RED");
        value = 1; must(H5Tenum_insert(type, "GREEN", &value), "GREEN");
        value = 2; must(H5Tenum_insert(type, "BLUE", &value), "BLUE");
        const std::array<std::int32_t, 3> values{2, 0, 1};
        writeDataset(file, "enum", type, {3}, values.data());
        H5Tclose(type);
    }

    // --- chunked + gzip ---------------------------------------------------
    {
        const std::vector<hsize_t> dims{100, 100};
        const std::vector<hsize_t> chunk{10, 10};
        const hid_t space =
            mustId(H5Screate_simple(2, dims.data(), nullptr), "compressed space");
        const hid_t props = mustId(H5Pcreate(H5P_DATASET_CREATE), "create plist");
        must(H5Pset_chunk(props, 2, chunk.data()), "set chunk");
        must(H5Pset_deflate(props, 6), "set deflate");

        std::vector<std::int32_t> data(100 * 100);
        for (std::size_t i = 0; i < data.size(); ++i) {
            data[i] = static_cast<std::int32_t>(i);
        }
        const hid_t dset = mustId(H5Dcreate2(file, "compressed", H5T_NATIVE_INT32, space,
                                             H5P_DEFAULT, props, H5P_DEFAULT),
                                  "create compressed");
        must(H5Dwrite(dset, H5T_NATIVE_INT32, H5S_ALL, H5S_ALL, H5P_DEFAULT, data.data()),
             "write compressed");
        H5Dclose(dset);
        H5Pclose(props);
        H5Sclose(space);
    }

    // --- long 1-D vector --------------------------------------------------
    // Deliberately longer than the table model's block size so that paging,
    // and any stale-window bug in it, is actually exercised.
    {
        std::vector<std::int32_t> data(1000);
        for (std::size_t i = 0; i < data.size(); ++i) {
            data[i] = static_cast<std::int32_t>(i);
        }
        writeDataset(file, "long_vec", H5T_NATIVE_INT32, {1000}, data.data());
    }

    // --- empty dataset ----------------------------------------------------
    writeDataset(file, "empty", H5T_NATIVE_INT32, {0}, nullptr);

    // --- groups, nesting and attributes -----------------------------------
    {
        const hid_t group = mustId(
            H5Gcreate2(file, "group", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT), "create group");
        writeStringAttribute(group, "title", "example group");
        writeIntAttribute(group, "version", 3);

        const hid_t nested = mustId(
            H5Gcreate2(group, "nested", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT), "nested");
        const std::array<std::int32_t, 2> leaf{11, 22};
        writeDataset(nested, "leaf", H5T_NATIVE_INT32, {2}, leaf.data());
        H5Gclose(nested);
        H5Gclose(group);
    }

    // --- hard link: two paths, one object. Exercises cycle detection. ------
    must(H5Lcreate_hard(file, "matrix", file, "link_to_matrix", H5P_DEFAULT, H5P_DEFAULT),
         "hard link");

    // --- soft links, one that resolves and one that does not ---------------
    // What kind of link a name is, and where it points, is information the
    // file carries and the tree has to show -- especially for the broken one,
    // whose target is the only thing about it worth reading.
    must(H5Lcreate_soft("/matrix", file, "soft_to_matrix", H5P_DEFAULT, H5P_DEFAULT),
         "soft link");
    must(H5Lcreate_soft("/no/such/object", file, "dangling", H5P_DEFAULT, H5P_DEFAULT),
         "dangling soft link");

    H5Fclose(file);
}

} // namespace h5test
