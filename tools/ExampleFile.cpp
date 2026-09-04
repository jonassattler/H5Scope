// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

// Writes the example files the viewer is exercised against.
//
//   example.h5            everything HDF5 can express that a viewer must render
//   example_external.h5   the target of the external links and of the VDS
//   example_raw.bin       the raw data of the one externally-stored dataset
//
// Deliberately generated rather than committed as binaries: what the files
// contain is readable in this source rather than only in a hex dump, and they
// are written by the same pinned HDF5 the application links.
//
// The generator is a library so that `tests/test_example.cpp` writes exactly
// the file `make-example-file` does, and asserts against it.

#include "ExampleFile.hpp"

#include <hdf5.h>

// H5Zregister and H5Z_class2_t live here rather than in H5Zpublic.h: registering
// a filter is a library-extension concern, not part of the everyday API.
#include <H5Zdevelop.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>
#include <vector>

namespace h5example {
namespace {

// --- error handling -------------------------------------------------------

void must(herr_t status, const char* what)
{
    if (status < 0) {
        throw std::runtime_error(std::string("example: ") + what);
    }
}

hid_t mustId(hid_t id, const char* what)
{
    if (id < 0) {
        throw std::runtime_error(std::string("example: ") + what);
    }
    return id;
}

/// An owning HDF5 identifier. The generator opens a great many of them and
/// every one is checked, so the check and the close live in one place.
class Id
{
public:
    Id(hid_t id, herr_t (*closer)(hid_t), const char* what)
        : id_(mustId(id, what)), closer_(closer)
    {}
    ~Id()
    {
        if (id_ >= 0) {
            closer_(id_);
        }
    }
    Id(Id&& other) noexcept : id_(other.id_), closer_(other.closer_) { other.id_ = -1; }
    Id(const Id&) = delete;
    Id& operator=(const Id&) = delete;
    Id& operator=(Id&&) = delete;

    operator hid_t() const { return id_; } // NOLINT(google-explicit-constructor)

private:
    hid_t id_;
    herr_t (*closer_)(hid_t);
};

/// Suppresses HDF5's automatic error printing for the lifetime of the object,
/// so an optional feature can be probed without spraying a stack trace.
class QuietErrors
{
public:
    QuietErrors()
    {
        H5Eget_auto2(H5E_DEFAULT, &function_, &data_);
        H5Eset_auto2(H5E_DEFAULT, nullptr, nullptr);
    }
    ~QuietErrors()
    {
        H5Eclear2(H5E_DEFAULT);
        H5Eset_auto2(H5E_DEFAULT, function_, data_);
    }
    QuietErrors(const QuietErrors&) = delete;
    QuietErrors& operator=(const QuietErrors&) = delete;
    QuietErrors(QuietErrors&&) = delete;
    QuietErrors& operator=(QuietErrors&&) = delete;

private:
    H5E_auto2_t function_ = nullptr;
    void* data_ = nullptr;
};

// --- small writers --------------------------------------------------------

Id makeSpace(const std::vector<hsize_t>& dims)
{
    if (dims.empty()) {
        return {H5Screate(H5S_SCALAR), &H5Sclose, "scalar dataspace"};
    }
    return {H5Screate_simple(static_cast<int>(dims.size()), dims.data(), nullptr),
            &H5Sclose, "simple dataspace"};
}

void writeDataset(hid_t parent, const char* name, hid_t type,
                  const std::vector<hsize_t>& dims, const void* data,
                  hid_t createProps = H5P_DEFAULT)
{
    const Id space = makeSpace(dims);
    const Id dataset(
        H5Dcreate2(parent, name, type, space, H5P_DEFAULT, createProps, H5P_DEFAULT),
        &H5Dclose, "create dataset");
    if (data != nullptr) {
        must(H5Dwrite(dataset, type, H5S_ALL, H5S_ALL, H5P_DEFAULT, data),
             "write dataset");
    }
}

/// Same, but the file type and the memory type differ -- which is the normal
/// case for a fixed-precision or byte-order-flipped on-disk type.
void writeConverted(hid_t parent, const char* name, hid_t fileType, hid_t memoryType,
                    const std::vector<hsize_t>& dims, const void* data,
                    hid_t createProps = H5P_DEFAULT)
{
    const Id space = makeSpace(dims);
    const Id dataset(
        H5Dcreate2(parent, name, fileType, space, H5P_DEFAULT, createProps, H5P_DEFAULT),
        &H5Dclose, "create dataset");
    if (data != nullptr) {
        must(H5Dwrite(dataset, memoryType, H5S_ALL, H5S_ALL, H5P_DEFAULT, data),
             "write dataset");
    }
}

Id variableString()
{
    Id type(H5Tcopy(H5T_C_S1), &H5Tclose, "copy C string type");
    must(H5Tset_size(type, H5T_VARIABLE), "variable-length string");
    must(H5Tset_cset(type, H5T_CSET_UTF8), "utf-8 string");
    return type;
}

Id fixedString(std::size_t width, H5T_str_t padding = H5T_STR_NULLTERM)
{
    Id type(H5Tcopy(H5T_C_S1), &H5Tclose, "copy C string type");
    must(H5Tset_size(type, width), "fixed string width");
    must(H5Tset_strpad(type, padding), "string padding");
    return type;
}

void stringAttribute(hid_t object, const char* name, const std::string& value)
{
    const Id type = variableString();
    const Id space(H5Screate(H5S_SCALAR), &H5Sclose, "attribute dataspace");
    const Id attribute(H5Acreate2(object, name, type, space, H5P_DEFAULT, H5P_DEFAULT),
                       &H5Aclose, "create string attribute");
    const char* pointer = value.c_str();
    must(H5Awrite(attribute, type, &pointer), "write string attribute");
}

void stringArrayAttribute(hid_t object, const char* name,
                          const std::vector<const char*>& values)
{
    const Id type = variableString();
    const Id space = makeSpace({static_cast<hsize_t>(values.size())});
    const Id attribute(H5Acreate2(object, name, type, space, H5P_DEFAULT, H5P_DEFAULT),
                       &H5Aclose, "create string array attribute");
    must(H5Awrite(attribute, type, values.data()), "write string array attribute");
}

template<typename T>
void numericAttribute(hid_t object, const char* name, hid_t type,
                      const std::vector<hsize_t>& dims, const std::vector<T>& values)
{
    const Id space = makeSpace(dims);
    const Id attribute(H5Acreate2(object, name, type, space, H5P_DEFAULT, H5P_DEFAULT),
                       &H5Aclose, "create numeric attribute");
    if (!values.empty()) {
        must(H5Awrite(attribute, type, values.data()), "write numeric attribute");
    }
}

void intAttribute(hid_t object, const char* name, std::int32_t value)
{
    numericAttribute<std::int32_t>(object, name, H5T_NATIVE_INT32, {}, {value});
}

void doubleAttribute(hid_t object, const char* name, double value)
{
    numericAttribute<double>(object, name, H5T_NATIVE_DOUBLE, {}, {value});
}

Id makeGroup(hid_t parent, const char* name)
{
    return {H5Gcreate2(parent, name, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT), &H5Gclose,
            "create group"};
}

Id chunked(const std::vector<hsize_t>& chunk)
{
    Id props(H5Pcreate(H5P_DATASET_CREATE), &H5Pclose, "dataset creation plist");
    must(H5Pset_chunk(props, static_cast<int>(chunk.size()), chunk.data()), "set chunk");
    return props;
}

// --- datatypes reused across sections -------------------------------------

struct Point
{
    double x;
    double y;
    double z;
};

struct Reading
{
    char station[16];
    std::int64_t timestamp;
    Point position;       ///< a compound inside a compound
    double samples[4];    ///< an array member
    std::int32_t quality; ///< an enum member
    float weight;
};

struct Simple
{
    std::int32_t id;
    double value;
};

Id qualityEnum()
{
    Id type(H5Tenum_create(H5T_NATIVE_INT32), &H5Tclose, "create enum");
    std::int32_t value = 0;
    value = 0;
    must(H5Tenum_insert(type, "BAD", &value), "enum BAD");
    value = 1;
    must(H5Tenum_insert(type, "SUSPECT", &value), "enum SUSPECT");
    value = 2;
    must(H5Tenum_insert(type, "GOOD", &value), "enum GOOD");
    return type;
}

Id pointType()
{
    Id type(H5Tcreate(H5T_COMPOUND, sizeof(Point)), &H5Tclose, "create point compound");
    must(H5Tinsert(type, "x", HOFFSET(Point, x), H5T_NATIVE_DOUBLE), "point x");
    must(H5Tinsert(type, "y", HOFFSET(Point, y), H5T_NATIVE_DOUBLE), "point y");
    must(H5Tinsert(type, "z", HOFFSET(Point, z), H5T_NATIVE_DOUBLE), "point z");
    return type;
}

Id readingType()
{
    const Id station = fixedString(16);
    const Id point = pointType();
    const Id quality = qualityEnum();
    const hsize_t sampleDims[] = {4};
    const Id samples(H5Tarray_create2(H5T_NATIVE_DOUBLE, 1, sampleDims), &H5Tclose,
                     "samples array type");

    Id type(H5Tcreate(H5T_COMPOUND, sizeof(Reading)), &H5Tclose,
            "create reading compound");
    must(H5Tinsert(type, "station", HOFFSET(Reading, station), station), "station");
    must(H5Tinsert(type, "timestamp", HOFFSET(Reading, timestamp), H5T_NATIVE_INT64),
         "timestamp");
    must(H5Tinsert(type, "position", HOFFSET(Reading, position), point), "position");
    must(H5Tinsert(type, "samples", HOFFSET(Reading, samples), samples), "samples");
    must(H5Tinsert(type, "quality", HOFFSET(Reading, quality), quality), "quality");
    must(H5Tinsert(type, "weight", HOFFSET(Reading, weight), H5T_NATIVE_FLOAT), "weight");
    return type;
}

// --- the pretend third-party filter ---------------------------------------
//
// Filter 32004 is LZ4 in the HDF5 filter registry, and is exactly the kind of
// filter a real file carries and a plain build cannot decode. Registering an
// identity function here is what lets the file be *written*; nothing registers
// it in the viewer, so the viewer meets it as a filter it does not have --
// which is the state worth rendering.

size_t identityFilter(unsigned /*flags*/, size_t /*count*/, const unsigned* /*values*/,
                      size_t bytes, size_t* /*bufferSize*/, void** /*buffer*/)
{
    return bytes;
}

const H5Z_class2_t kPretendLz4 = {H5Z_CLASS_T_VERS,
                                  static_cast<H5Z_filter_t>(32004),
                                  1,
                                  1,
                                  "lz4 (stand-in written by make-example-file)",
                                  nullptr,
                                  nullptr,
                                  &identityFilter};

// --- image helpers --------------------------------------------------------

/// Tag a dataset as an image per the HDF5 Image and Palette Specification 1.2,
/// which is the convention every other HDF5 viewer looks for.
void tagImage(hid_t dataset, const char* subclass, const char* interlace = nullptr)
{
    stringAttribute(dataset, "CLASS", "IMAGE");
    stringAttribute(dataset, "IMAGE_VERSION", "1.2");
    stringAttribute(dataset, "IMAGE_SUBCLASS", subclass);
    if (interlace != nullptr) {
        stringAttribute(dataset, "INTERLACE_MODE", interlace);
    }
}

void tagImageByName(hid_t parent, const char* name, const char* subclass,
                    const char* interlace = nullptr)
{
    const Id dataset(H5Dopen2(parent, name, H5P_DEFAULT), &H5Dclose, "reopen image");
    tagImage(dataset, subclass, interlace);
}

// --- the companion file ---------------------------------------------------

void writeExternalFile(const std::string& path)
{
    const Id file(H5Fcreate(path.c_str(), H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT),
                  &H5Fclose, "create external file");
    stringAttribute(file, "title", "Companion file: external-link and VDS targets");

    {
        const Id group = makeGroup(file, "external");
        stringAttribute(group, "note",
                        "Reached from example.h5 through an external link");

        std::vector<std::int32_t> values(25);
        for (std::size_t i = 0; i < values.size(); ++i) {
            values[i] = static_cast<std::int32_t>(i * i);
        }
        writeDataset(group, "squares", H5T_NATIVE_INT32, {5, 5}, values.data());
        {
            const Id dataset(H5Dopen2(group, "squares", H5P_DEFAULT), &H5Dclose,
                             "reopen squares");
            stringAttribute(dataset, "units", "dimensionless");
        }

        const Id nested = makeGroup(group, "deeper");
        const std::vector<double> ramp{0.0, 0.25, 0.5, 0.75, 1.0};
        writeDataset(nested, "ramp", H5T_NATIVE_DOUBLE, {5}, ramp.data());
    }

    // The two rows a virtual dataset in example.h5 stitches together.
    {
        const Id group = makeGroup(file, "vds_source");
        std::vector<double> a(10);
        std::vector<double> b(10);
        for (std::size_t i = 0; i < 10; ++i) {
            a[i] = static_cast<double>(i);
            b[i] = 100.0 + static_cast<double>(i);
        }
        writeDataset(group, "row_a", H5T_NATIVE_DOUBLE, {10}, a.data());
        writeDataset(group, "row_b", H5T_NATIVE_DOUBLE, {10}, b.data());
    }
}

// --- /data: plain numbers, from a scalar to rank 12 ------------------------

void writeData(hid_t file)
{
    const Id group = makeGroup(file, "data");
    stringAttribute(group, "purpose",
                    "Ordinary numeric datasets, one per shape worth testing");

    // A scalar, which has rank 0 but exactly one element.
    const std::int32_t answer = 42;
    writeDataset(group, "scalar_int", H5T_NATIVE_INT32, {}, &answer);
    {
        const Id dataset(H5Dopen2(group, "scalar_int", H5P_DEFAULT), &H5Dclose, "reopen");
        stringAttribute(dataset, "units", "kelvin");
        stringAttribute(dataset, "long_name", "The answer, in kelvin");
    }
    const double pi = std::numbers::pi;
    writeDataset(group, "scalar_double", H5T_NATIVE_DOUBLE, {}, &pi);

    // Rank 1..3, values encoding their own coordinates so a transposition or
    // an off-by-one in the table is arithmetic rather than a guess.
    std::vector<std::int32_t> vector(24);
    for (std::size_t i = 0; i < vector.size(); ++i) {
        vector[i] = static_cast<std::int32_t>(i);
    }
    writeDataset(group, "vector", H5T_NATIVE_INT32, {24}, vector.data());

    std::vector<double> matrix(4 * 3);
    for (hsize_t r = 0; r < 4; ++r) {
        for (hsize_t c = 0; c < 3; ++c) {
            matrix[r * 3 + c] = static_cast<double>(r) * 10.0 + static_cast<double>(c);
        }
    }
    writeDataset(group, "matrix", H5T_NATIVE_DOUBLE, {4, 3}, matrix.data());
    {
        const Id dataset(H5Dopen2(group, "matrix", H5P_DEFAULT), &H5Dclose, "reopen");
        stringAttribute(dataset, "description", "Element (r, c) holds r*10 + c");
        numericAttribute<double>(dataset, "valid_range", H5T_NATIVE_DOUBLE, {2},
                                 {0.0, 32.0});
    }

    std::vector<std::int32_t> cube(2 * 3 * 4);
    for (std::size_t i = 0; i < cube.size(); ++i) {
        cube[i] = static_cast<std::int32_t>(i);
    }
    writeDataset(group, "cube", H5T_NATIVE_INT32, {2, 3, 4}, cube.data());

    // Rank 4 and 5: more dimensions than a table has axes, so the slice
    // controls have to spread several of them across one axis.
    std::vector<std::int32_t> hyper4(2 * 3 * 4 * 5);
    for (std::size_t i = 0; i < hyper4.size(); ++i) {
        hyper4[i] = static_cast<std::int32_t>(i);
    }
    writeDataset(group, "rank4", H5T_NATIVE_INT32, {2, 3, 4, 5}, hyper4.data());

    std::vector<float> hyper5(3 * 4 * 5 * 6 * 7);
    for (std::size_t i = 0; i < hyper5.size(); ++i) {
        hyper5[i] = static_cast<float>(i) / 10.0F;
    }
    writeDataset(group, "rank5", H5T_NATIVE_FLOAT, {3, 4, 5, 6, 7}, hyper5.data());

    // Rank 8, every extent 2: the flat index is the binary reading of the
    // coordinate, so any dimension the viewer drops is visible in the values.
    std::vector<std::uint8_t> rank8(256);
    for (std::size_t i = 0; i < rank8.size(); ++i) {
        rank8[i] = static_cast<std::uint8_t>(i);
    }
    writeDataset(group, "rank8", H5T_NATIVE_UINT8, {2, 2, 2, 2, 2, 2, 2, 2},
                 rank8.data());

    // Rank 12, with singleton dimensions mixed in -- a shape that looks
    // degenerate but is entirely legal, and that a naive index calculation
    // gets wrong.
    std::vector<std::int16_t> rank12(2 * 1 * 2 * 1 * 2 * 1 * 2 * 1 * 2 * 1 * 2 * 3);
    for (std::size_t i = 0; i < rank12.size(); ++i) {
        rank12[i] = static_cast<std::int16_t>(i);
    }
    writeDataset(group, "rank12", H5T_NATIVE_INT16, {2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 3},
                 rank12.data());

    // Degenerate two-dimensional shapes: a column and a row.
    std::vector<double> column(64);
    std::vector<double> row(64);
    for (std::size_t i = 0; i < 64; ++i) {
        column[i] = std::sin(static_cast<double>(i) / 8.0);
        row[i] = std::cos(static_cast<double>(i) / 8.0);
    }
    writeDataset(group, "column_64x1", H5T_NATIVE_DOUBLE, {64, 1}, column.data());
    writeDataset(group, "row_1x64", H5T_NATIVE_DOUBLE, {1, 64}, row.data());

    // Nothing at all, in the two ways HDF5 can express it.
    writeDataset(group, "empty_extent", H5T_NATIVE_INT32, {0}, nullptr);
    {
        // A null dataspace holds no elements and has no shape -- distinct from
        // a scalar, which has no shape but one element.
        const Id space(H5Screate(H5S_NULL), &H5Sclose, "null dataspace");
        const Id dataset(H5Dcreate2(group, "null_space", H5T_NATIVE_INT32, space,
                                    H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT),
                         &H5Dclose, "create null dataset");
        stringAttribute(dataset, "note", "H5S_NULL: no elements, and no shape either");
    }

    // Values that break naive formatting.
    const std::vector<double> specials{std::numeric_limits<double>::quiet_NaN(),
                                       std::numeric_limits<double>::infinity(),
                                       -std::numeric_limits<double>::infinity(),
                                       0.0,
                                       -0.0,
                                       std::numeric_limits<double>::denorm_min(),
                                       std::numeric_limits<double>::max(),
                                       std::numeric_limits<double>::lowest()};
    writeDataset(group, "special_floats", H5T_NATIVE_DOUBLE, {8}, specials.data());

    const std::vector<std::int64_t> extremes{std::numeric_limits<std::int64_t>::min(), -1,
                                             0, std::numeric_limits<std::int64_t>::max()};
    writeDataset(group, "int64_extremes", H5T_NATIVE_INT64, {4}, extremes.data());

    const std::vector<std::uint64_t> unsignedExtremes{
        0, 1, std::numeric_limits<std::uint64_t>::max()};
    writeDataset(group, "uint64_extremes", H5T_NATIVE_UINT64, {3},
                 unsignedExtremes.data());

    // Big-endian on disk: the viewer must convert rather than show byte soup.
    const std::vector<std::int32_t> counts{1, 256, 65536, 16777216};
    writeConverted(group, "big_endian_int32", H5T_STD_I32BE, H5T_NATIVE_INT32, {4},
                   counts.data());
}

// --- /types: one dataset per datatype class --------------------------------

void writeTypes(hid_t file)
{
    const Id group = makeGroup(file, "types");
    stringAttribute(group, "purpose", "One dataset per HDF5 datatype class");

    // Integers, every width and both signs.
    {
        const Id integers = makeGroup(group, "integer");
        const std::vector<std::int8_t> i8{-128, -1, 0, 1, 127};
        const std::vector<std::int16_t> i16{-32768, -1, 0, 1, 32767};
        const std::vector<std::int32_t> i32{-2147483647 - 1, -1, 0, 1, 2147483647};
        const std::vector<std::int64_t> i64{-5, -1, 0, 1, 5};
        const std::vector<std::uint8_t> u8{0, 1, 127, 128, 255};
        const std::vector<std::uint16_t> u16{0, 1, 32768, 65534, 65535};
        const std::vector<std::uint32_t> u32{0, 1, 2147483648U, 4294967294U, 4294967295U};
        const std::vector<std::uint64_t> u64{0, 1, 2, 3, 4};
        writeDataset(integers, "int8", H5T_NATIVE_INT8, {5}, i8.data());
        writeDataset(integers, "int16", H5T_NATIVE_INT16, {5}, i16.data());
        writeDataset(integers, "int32", H5T_NATIVE_INT32, {5}, i32.data());
        writeDataset(integers, "int64", H5T_NATIVE_INT64, {5}, i64.data());
        writeDataset(integers, "uint8", H5T_NATIVE_UINT8, {5}, u8.data());
        writeDataset(integers, "uint16", H5T_NATIVE_UINT16, {5}, u16.data());
        writeDataset(integers, "uint32", H5T_NATIVE_UINT32, {5}, u32.data());
        writeDataset(integers, "uint64", H5T_NATIVE_UINT64, {5}, u64.data());

        // 20 bits of precision inside a 32-bit word: legal, and not one of the
        // widths a switch over element size would list.
        Id odd(H5Tcopy(H5T_STD_I32LE), &H5Tclose, "copy int32");
        must(H5Tset_precision(odd, 20), "20-bit precision");
        must(H5Tset_offset(odd, 0), "zero offset");
        const std::vector<std::int32_t> narrow{-524288, -1, 0, 1, 524287};
        writeConverted(integers, "int20_in_int32", odd, H5T_NATIVE_INT32, {5},
                       narrow.data());
    }

    // Floats, including the half precision HDF5 2.x exposes natively.
    {
        const Id floats = makeGroup(group, "float");
        const std::vector<float> f32{-1.5F, 0.0F, 0.1F, 1e30F, 3.14159F};
        const std::vector<double> f64{-1.5, 0.0, 0.1, 1e300, std::numbers::pi};
        writeDataset(floats, "float32", H5T_NATIVE_FLOAT, {5}, f32.data());
        writeDataset(floats, "float64", H5T_NATIVE_DOUBLE, {5}, f64.data());
        writeConverted(floats, "float16", H5T_IEEE_F16LE, H5T_NATIVE_FLOAT, {5},
                       f32.data());
        // long double: 16 bytes on this platform, and a width no viewer that
        // switches on element size will have a case for.
        const std::vector<long double> f80{-1.5L, 0.0L, 0.1L, 1e300L, 2.5L};
        writeDataset(floats, "longdouble", H5T_NATIVE_LDOUBLE, {5}, f80.data());
    }

    // Complex numbers: a datatype class HDF5 2.0 added, so a viewer written
    // against 1.x has no case for it at all.
    {
        const std::vector<std::complex<float>> values{
            {0.0F, 0.0F}, {1.0F, -1.0F}, {-2.5F, 0.5F}, {3.0F, 4.0F}};
        writeDataset(group, "complex64", H5T_NATIVE_FLOAT_COMPLEX, {4}, values.data());
    }

    // Strings: fixed and variable, every padding, and non-ASCII content.
    {
        const Id strings = makeGroup(group, "string");

        constexpr std::size_t width = 12;
        std::vector<char> fixed(width * 4, '\0');
        const char* words[] = {"alpha", "beta", "gamma", "delta"};
        for (std::size_t i = 0; i < 4; ++i) {
            std::snprintf(fixed.data() + i * width, width, "%s", words[i]);
        }
        const Id nullTerm = fixedString(width, H5T_STR_NULLTERM);
        writeDataset(strings, "fixed_nullterm", nullTerm, {4}, fixed.data());

        std::vector<char> padded(width * 4, ' ');
        for (std::size_t i = 0; i < 4; ++i) {
            std::memcpy(padded.data() + i * width, words[i], std::strlen(words[i]));
        }
        const Id spacePad = fixedString(width, H5T_STR_SPACEPAD);
        writeDataset(strings, "fixed_spacepad", spacePad, {4}, padded.data());

        const Id vlen = variableString();
        const char* utf8[] = {"plain ascii", "h\u00e9llo w\u00f6rld",
                              "\u65e5\u672c\u8a9e", ""};
        writeDataset(strings, "vlen_utf8", vlen, {4}, utf8);

        // Rank 2, so the flat list of panes has to label an element by a
        // coordinate rather than by a single subscript.
        const char* grid[] = {"north west", "north",  "north east",
                              "west",       "centre", "east",
                              "south west", "south",  "south east"};
        writeDataset(strings, "grid_3x3", vlen, {3, 3}, grid);

        // One long, multi-line string: the shape a provenance record takes.
        std::string document = "H5Scope example document\n"
                               "==============================\n\n";
        for (int paragraph = 0; paragraph < 20; ++paragraph) {
            document += "Paragraph " + std::to_string(paragraph) +
                        ": a line of prose long enough to need wrapping at any "
                        "reasonable pane width, repeated often enough that the "
                        "whole thing cannot fit on one screen.\n";
        }
        const char* scalar[] = {document.c_str()};
        writeDataset(strings, "scalar_document", vlen, {}, scalar);

        // Enough strings that the pane stack has to page rather than build
        // every pane up front.
        std::vector<std::string> owned(500);
        std::vector<const char*> many(500);
        for (std::size_t i = 0; i < owned.size(); ++i) {
            owned[i] =
                "record " + std::to_string(i) + ": " + std::string(1 + (i % 40), '.');
            many[i] = owned[i].c_str();
        }
        writeDataset(strings, "many_500", vlen, {500}, many.data());
    }

    // Compound, plain and richly nested.
    {
        const Id compounds = makeGroup(group, "compound");

        const Id simpleType(H5Tcreate(H5T_COMPOUND, sizeof(Simple)), &H5Tclose,
                            "simple compound");
        must(H5Tinsert(simpleType, "id", HOFFSET(Simple, id), H5T_NATIVE_INT32), "id");
        must(H5Tinsert(simpleType, "value", HOFFSET(Simple, value), H5T_NATIVE_DOUBLE),
             "value");
        const std::vector<Simple> simple{{7, 1.5}, {9, 2.5}, {11, -3.25}};
        writeDataset(compounds, "simple", simpleType, {3}, simple.data());

        const Id rich = readingType();
        std::vector<Reading> readings(6);
        for (std::size_t i = 0; i < readings.size(); ++i) {
            std::snprintf(readings[i].station, sizeof(readings[i].station), "ST-%03zu",
                          i);
            readings[i].timestamp = 1700000000LL + static_cast<std::int64_t>(i) * 3600;
            readings[i].position = {static_cast<double>(i), static_cast<double>(i) * 2.0,
                                    static_cast<double>(i) * 3.0};
            for (int s = 0; s < 4; ++s) {
                readings[i].samples[s] =
                    static_cast<double>(i) + static_cast<double>(s) / 4.0;
            }
            readings[i].quality = static_cast<std::int32_t>(i % 3);
            readings[i].weight = 0.5F * static_cast<float>(i);
        }
        writeDataset(compounds, "nested", rich, {6}, readings.data());
        {
            const Id dataset(H5Dopen2(compounds, "nested", H5P_DEFAULT), &H5Dclose,
                             "reopen nested");
            stringAttribute(
                dataset, "note",
                "Members: fixed string, int64, nested compound, array, enum, float");
        }

        // A table shape rather than a list: rank 2 of records.
        std::vector<Simple> table(4 * 5);
        for (std::size_t i = 0; i < table.size(); ++i) {
            table[i] = {static_cast<std::int32_t>(i), static_cast<double>(i) / 8.0};
        }
        writeDataset(compounds, "table_4x5", simpleType, {4, 5}, table.data());
    }

    // Enum, array, vlen.
    {
        const Id quality = qualityEnum();
        const std::vector<std::int32_t> values{2, 0, 1, 2, 1, 7}; // 7 has no name
        writeDataset(group, "enum_quality", quality, {6}, values.data());
        {
            const Id dataset(H5Dopen2(group, "enum_quality", H5P_DEFAULT), &H5Dclose,
                             "reopen enum");
            stringAttribute(dataset, "note", "The last value, 7, is outside the enum");
        }

        // The boolean convention h5py writes.
        Id boolean(H5Tenum_create(H5T_NATIVE_INT8), &H5Tclose, "bool enum");
        std::int8_t flag = 0;
        must(H5Tenum_insert(boolean, "FALSE", &flag), "FALSE");
        flag = 1;
        must(H5Tenum_insert(boolean, "TRUE", &flag), "TRUE");
        const std::vector<std::int8_t> flags{1, 0, 1, 1, 0};
        writeDataset(group, "enum_bool", boolean, {5}, flags.data());

        const hsize_t arrayDims[] = {3, 2};
        const Id arrayType(H5Tarray_create2(H5T_NATIVE_INT32, 2, arrayDims), &H5Tclose,
                           "array type");
        std::vector<std::int32_t> arrays(4 * 6);
        for (std::size_t i = 0; i < arrays.size(); ++i) {
            arrays[i] = static_cast<std::int32_t>(i);
        }
        writeDataset(group, "array_3x2", arrayType, {4}, arrays.data());

        // Ragged rows, which is what a vlen is for.
        const Id vlenType(H5Tvlen_create(H5T_NATIVE_INT32), &H5Tclose, "vlen type");
        std::vector<std::vector<std::int32_t>> ragged{
            {1}, {1, 2, 3}, {}, {4, 5, 6, 7, 8, 9}};
        std::vector<hvl_t> vlens(ragged.size());
        for (std::size_t i = 0; i < ragged.size(); ++i) {
            vlens[i].len = ragged[i].size();
            vlens[i].p = ragged[i].empty() ? nullptr : ragged[i].data();
        }
        writeDataset(group, "vlen_int32", vlenType, {4}, vlens.data());
    }

    // Bitfield, opaque, and the time class HDF5 never finished.
    {
        const std::vector<std::uint32_t> bits{0x00000000U, 0x0000FFFFU, 0xDEADBEEFU,
                                              0xFFFFFFFFU};
        writeDataset(group, "bitfield32", H5T_STD_B32LE, {4}, bits.data());

        Id opaque(H5Tcreate(H5T_OPAQUE, 8), &H5Tclose, "opaque type");
        must(H5Tset_tag(opaque, "raw sensor frame"), "opaque tag");
        std::vector<std::uint8_t> blobs(4 * 8);
        for (std::size_t i = 0; i < blobs.size(); ++i) {
            blobs[i] = static_cast<std::uint8_t>(i * 17);
        }
        writeDataset(group, "opaque_blob", opaque, {4}, blobs.data());

        // H5T_TIME is in the format specification but has no conversion path
        // in the library; a viewer must survive meeting one.
        // Written through the file type itself: the library has no conversion
        // path to or from H5T_TIME, so a memory type of int64 would fail here
        // exactly as it fails for a reader.
        const std::vector<std::int64_t> stamps{0, 1000000000, 1700000000, 2000000000};
        writeDataset(group, "time_unix", H5T_UNIX_D64LE, {4}, stamps.data());
    }
}

// --- /committed: named datatypes, shared by the datasets that use them -----

void writeCommitted(hid_t file)
{
    const Id group = makeGroup(file, "committed");
    stringAttribute(
        group, "purpose",
        "Named datatypes: objects in their own right, and shared by datasets");

    {
        Id celsius(H5Tcopy(H5T_IEEE_F64LE), &H5Tclose, "copy float64");
        must(H5Tcommit2(group, "celsius_t", celsius, H5P_DEFAULT, H5P_DEFAULT,
                        H5P_DEFAULT),
             "commit celsius_t");
        // A named datatype carries attributes exactly as a dataset does.
        stringAttribute(celsius, "units", "degree_Celsius");
        stringAttribute(celsius, "note", "Committed float64; two datasets share it");

        std::vector<double> readings(48);
        for (std::size_t i = 0; i < readings.size(); ++i) {
            readings[i] = 20.0 + 5.0 * std::sin(static_cast<double>(i) / 4.0);
        }
        writeDataset(group, "morning", celsius, {48}, readings.data());
        for (double& value : readings) {
            value += 3.5;
        }
        writeDataset(group, "afternoon", celsius, {48}, readings.data());
    }

    {
        const Id record = readingType();
        must(
            H5Tcommit2(group, "reading_t", record, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT),
            "commit reading_t");
        stringAttribute(record, "schema_version", "2");

        std::vector<Reading> readings(3);
        for (std::size_t i = 0; i < readings.size(); ++i) {
            std::snprintf(readings[i].station, sizeof(readings[i].station), "SHARED%zu",
                          i);
            readings[i].timestamp = 1700000000LL + static_cast<std::int64_t>(i);
            readings[i].position = {1.0, 2.0, 3.0};
            for (int s = 0; s < 4; ++s) {
                readings[i].samples[s] = static_cast<double>(s);
            }
            readings[i].quality = 2;
            readings[i].weight = 1.0F;
        }
        writeDataset(group, "records", record, {3}, readings.data());
    }
}

// --- /storage: every dataset layout ---------------------------------------

void writeStorage(hid_t file, const std::string& externalFileName)
{
    const Id group = makeGroup(file, "storage");
    stringAttribute(group, "purpose", "One dataset per storage layout");

    std::vector<std::int32_t> values(20 * 20);
    for (std::size_t i = 0; i < values.size(); ++i) {
        values[i] = static_cast<std::int32_t>(i);
    }

    // Contiguous: the default.
    writeDataset(group, "contiguous", H5T_NATIVE_INT32, {20, 20}, values.data());

    // Chunked, with a chunk that does not divide the shape evenly.
    {
        const Id props = chunked({7, 6});
        writeDataset(group, "chunked_7x6", H5T_NATIVE_INT32, {20, 20}, values.data(),
                     props);
    }

    // Compact: the data lives in the object header, so storage is tiny and
    // there is no separate data block at all.
    {
        Id props(H5Pcreate(H5P_DATASET_CREATE), &H5Pclose, "compact plist");
        must(H5Pset_layout(props, H5D_COMPACT), "compact layout");
        const std::vector<std::int32_t> small(16, 3);
        writeDataset(group, "compact", H5T_NATIVE_INT32, {4, 4}, small.data(), props);
    }

    // Extendable: an unlimited maximum extent, which the info panel must
    // render as a word rather than as 2^64-1.
    {
        const hsize_t dims[] = {6, 4};
        const hsize_t maxDims[] = {H5S_UNLIMITED, 8};
        const Id space(H5Screate_simple(2, dims, maxDims), &H5Sclose, "extendable space");
        const Id props = chunked({3, 4});
        const Id dataset(H5Dcreate2(group, "extendable", H5T_NATIVE_INT32, space,
                                    H5P_DEFAULT, props, H5P_DEFAULT),
                         &H5Dclose, "create extendable");
        must(H5Dwrite(dataset, H5T_NATIVE_INT32, H5S_ALL, H5S_ALL, H5P_DEFAULT,
                      values.data()),
             "write extendable");
        stringAttribute(dataset, "note", "Max shape is unlimited x 8");
    }

    // A fill value, never written: every element reads back as the fill and
    // the dataset occupies no storage at all.
    {
        Id props(H5Pcreate(H5P_DATASET_CREATE), &H5Pclose, "fill plist");
        const std::int32_t fill = -999;
        must(H5Pset_fill_value(props, H5T_NATIVE_INT32, &fill), "set fill value");
        must(H5Pset_alloc_time(props, H5D_ALLOC_TIME_LATE), "late allocation");
        writeDataset(group, "fill_value_only", H5T_NATIVE_INT32, {8, 8}, nullptr, props);
        const Id dataset(H5Dopen2(group, "fill_value_only", H5P_DEFAULT), &H5Dclose,
                         "reopen fill");
        stringAttribute(
            dataset, "note",
            "Never written: reads back as the fill value -999, stores 0 bytes");
    }

    // Raw data in a companion file outside the HDF5 container.
    {
        Id props(H5Pcreate(H5P_DATASET_CREATE), &H5Pclose, "external plist");
        const hsize_t bytes = 20 * 20 * sizeof(std::int32_t);
        must(H5Pset_external(props, "example_raw.bin", 0, bytes), "set external storage");
        writeDataset(group, "external_raw", H5T_NATIVE_INT32, {20, 20}, values.data(),
                     props);
        const Id dataset(H5Dopen2(group, "external_raw", H5P_DEFAULT), &H5Dclose,
                         "reopen external");
        stringAttribute(dataset, "note", "Raw data lives in example_raw.bin, not here");
    }

    // Virtual: two rows stitched out of two one-dimensional datasets in the
    // companion file. Nothing of the data is stored here.
    {
        const hsize_t virtualDims[] = {2, 10};
        const Id virtualSpace(H5Screate_simple(2, virtualDims, nullptr), &H5Sclose,
                              "virtual space");
        const hsize_t sourceDims[] = {10};
        const Id sourceSpace(H5Screate_simple(1, sourceDims, nullptr), &H5Sclose,
                             "source space");
        Id props(H5Pcreate(H5P_DATASET_CREATE), &H5Pclose, "virtual plist");

        const char* sources[] = {"/vds_source/row_a", "/vds_source/row_b"};
        for (hsize_t row = 0; row < 2; ++row) {
            const hsize_t start[] = {row, 0};
            const hsize_t count[] = {1, 10};
            must(H5Sselect_hyperslab(virtualSpace, H5S_SELECT_SET, start, nullptr, count,
                                     nullptr),
                 "select virtual row");
            must(H5Pset_virtual(props, virtualSpace, externalFileName.c_str(),
                                sources[row], sourceSpace),
                 "map virtual row");
        }
        must(H5Sselect_all(virtualSpace), "reset virtual selection");

        const Id dataset(H5Dcreate2(group, "virtual", H5T_NATIVE_DOUBLE, virtualSpace,
                                    H5P_DEFAULT, props, H5P_DEFAULT),
                         &H5Dclose, "create virtual dataset");
        stringAttribute(
            dataset, "note",
            "Rows come from /vds_source/row_a and row_b in the companion file");
    }
}

// --- /filters: the compression pipeline ------------------------------------

void writeFilters(hid_t file)
{
    const Id group = makeGroup(file, "filters");
    stringAttribute(group, "purpose", "One dataset per filter pipeline");

    // Smooth enough that a compressor has something to find.
    const hsize_t rows = 120;
    const hsize_t columns = 120;
    std::vector<std::int32_t> smooth(rows * columns);
    std::vector<double> real(rows * columns);
    for (hsize_t r = 0; r < rows; ++r) {
        for (hsize_t c = 0; c < columns; ++c) {
            const auto index = static_cast<std::size_t>(r * columns + c);
            smooth[index] = static_cast<std::int32_t>(r * 4 + c / 3);
            real[index] = std::sin(static_cast<double>(r) / 12.0) *
                          std::cos(static_cast<double>(c) / 9.0);
        }
    }

    {
        Id props = chunked({20, 20});
        must(H5Pset_deflate(props, 6), "deflate 6");
        writeDataset(group, "deflate", H5T_NATIVE_INT32, {rows, columns}, smooth.data(),
                     props);
    }
    {
        Id props = chunked({20, 20});
        must(H5Pset_shuffle(props), "shuffle");
        must(H5Pset_deflate(props, 9), "deflate 9");
        must(H5Pset_fletcher32(props), "fletcher32");
        writeDataset(group, "shuffle_deflate_fletcher32", H5T_NATIVE_INT32,
                     {rows, columns}, smooth.data(), props);
    }
    {
        Id props = chunked({20, 20});
        must(H5Pset_fletcher32(props), "fletcher32 only");
        writeDataset(group, "fletcher32", H5T_NATIVE_INT32, {rows, columns},
                     smooth.data(), props);
    }
    {
        // N-bit only pays when the stored precision is below the word width.
        Id narrow(H5Tcopy(H5T_STD_I32LE), &H5Tclose, "copy int32");
        must(H5Tset_precision(narrow, 12), "12-bit precision");
        must(H5Tset_offset(narrow, 0), "zero offset");
        Id props = chunked({20, 20});
        must(H5Pset_nbit(props), "nbit");
        std::vector<std::int32_t> narrowValues(rows * columns);
        for (std::size_t i = 0; i < narrowValues.size(); ++i) {
            narrowValues[i] = static_cast<std::int32_t>(i % 2048) - 1024;
        }
        writeConverted(group, "nbit", narrow, H5T_NATIVE_INT32, {rows, columns},
                       narrowValues.data(), props);
    }
    {
        Id props = chunked({20, 20});
        must(H5Pset_scaleoffset(props, H5Z_SO_FLOAT_DSCALE, 3), "scaleoffset");
        writeDataset(group, "scaleoffset", H5T_NATIVE_DOUBLE, {rows, columns},
                     real.data(), props);
    }

    // SZIP is optional in an HDF5 build; write it only if this one has an
    // encoder, and say so in the file either way.
    {
        unsigned config = 0;
        bool wrote = false;
        if (H5Zget_filter_info(H5Z_FILTER_SZIP, &config) >= 0 &&
            (config & H5Z_FILTER_CONFIG_ENCODE_ENABLED) != 0) {
            QuietErrors quiet;
            Id props = chunked({20, 32});
            if (H5Pset_szip(props, H5_SZIP_NN_OPTION_MASK, 16) >= 0) {
                const Id space = makeSpace({rows, columns});
                const hid_t dataset = H5Dcreate2(group, "szip", H5T_NATIVE_INT32, space,
                                                 H5P_DEFAULT, props, H5P_DEFAULT);
                if (dataset >= 0) {
                    wrote = H5Dwrite(dataset, H5T_NATIVE_INT32, H5S_ALL, H5S_ALL,
                                     H5P_DEFAULT, smooth.data()) >= 0;
                    H5Dclose(dataset);
                }
            }
        }
        stringAttribute(group, "szip",
                        wrote ? "written" : "skipped: no SZIP encoder in this build");
    }

    // A filter this build can encode only because the generator registered a
    // stand-in for it. Nothing registers it in the viewer, so the viewer meets
    // a mandatory filter it cannot decode -- the state a file carrying a
    // third-party plugin puts a plain build in.
    {
        must(H5Zregister(&kPretendLz4), "register the stand-in filter");
        Id props = chunked({20, 20});
        const unsigned parameters[] = {0};
        must(H5Pset_filter(props, kPretendLz4.id, H5Z_FLAG_MANDATORY, 1, parameters),
             "set the stand-in filter");
        writeDataset(group, "unavailable_mandatory", H5T_NATIVE_INT32, {rows, columns},
                     smooth.data(), props);
        const Id dataset(H5Dopen2(group, "unavailable_mandatory", H5P_DEFAULT), &H5Dclose,
                         "reopen unavailable");
        stringAttribute(dataset, "note",
                        "Filter 32004 is mandatory and absent from a plain build: "
                        "the data genuinely cannot be read");
        // Unregistered by writeExampleFiles once the file is closed. HDF5
        // refuses while anything still names the filter, and leaving it
        // registered would make the writing process the one process in which
        // the file reads -- which is exactly the state this dataset exists to
        // rule out.
    }

    // An *optional* filter that is absent: HDF5 skips it on write and on read,
    // so the pipeline names a filter this build does not have and the data is
    // readable regardless.
    {
        QuietErrors quiet;
        Id props = chunked({20, 20});
        const unsigned parameters[] = {2, 0};
        if (H5Pset_filter(props, static_cast<H5Z_filter_t>(32008), H5Z_FLAG_OPTIONAL, 2,
                          parameters) >= 0) {
            const Id space = makeSpace({rows, columns});
            const hid_t dataset =
                H5Dcreate2(group, "unavailable_optional", H5T_NATIVE_INT32, space,
                           H5P_DEFAULT, props, H5P_DEFAULT);
            if (dataset >= 0) {
                H5Dwrite(dataset, H5T_NATIVE_INT32, H5S_ALL, H5S_ALL, H5P_DEFAULT,
                         smooth.data());
                stringAttribute(dataset, "note",
                                "Filter 32008 is optional and absent: HDF5 skips it and "
                                "the data reads back normally");
                H5Dclose(dataset);
            }
        }
    }
}

// --- /images: rasters, in the shapes real image data comes in --------------

/// A saturated colour wheel, so a channel swap or a transposed axis is
/// obvious rather than subtle.
void hueToRgb(double hue, double value, std::uint8_t* out)
{
    const double h = std::fmod(hue, 1.0) * 6.0;
    const double f = h - std::floor(h);
    const auto sector = static_cast<int>(h) % 6;
    const double p = 0.0;
    const double q = 1.0 - f;
    const double t = f;
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
    switch (sector) {
    case 0:
        r = 1.0;
        g = t;
        b = p;
        break;
    case 1:
        r = q;
        g = 1.0;
        b = p;
        break;
    case 2:
        r = p;
        g = 1.0;
        b = t;
        break;
    case 3:
        r = p;
        g = q;
        b = 1.0;
        break;
    case 4:
        r = t;
        g = p;
        b = 1.0;
        break;
    default:
        r = 1.0;
        g = p;
        b = q;
        break;
    }
    out[0] = static_cast<std::uint8_t>(std::clamp(r * value, 0.0, 1.0) * 255.0);
    out[1] = static_cast<std::uint8_t>(std::clamp(g * value, 0.0, 1.0) * 255.0);
    out[2] = static_cast<std::uint8_t>(std::clamp(b * value, 0.0, 1.0) * 255.0);
}

void writeImages(hid_t file)
{
    const Id group = makeGroup(file, "images");
    stringAttribute(group, "purpose",
                    "Rasters: interleaved and planar, grayscale, indexed, and a stack");

    constexpr hsize_t size = 256;

    // Truecolour, pixel-interleaved: height x width x 3, the shape a viewer
    // meets most often.
    {
        std::vector<std::uint8_t> pixels(size * size * 3);
        for (hsize_t y = 0; y < size; ++y) {
            for (hsize_t x = 0; x < size; ++x) {
                const double hue = static_cast<double>(x) / static_cast<double>(size);
                const double value =
                    1.0 - static_cast<double>(y) / static_cast<double>(size);
                hueToRgb(hue, value, &pixels[(y * size + x) * 3]);
            }
        }
        writeDataset(group, "rgb_256x256x3", H5T_NATIVE_UINT8, {size, size, 3},
                     pixels.data(), chunked({32, 32, 3}));
        tagImageByName(group, "rgb_256x256x3", "IMAGE_TRUECOLOR", "INTERLACE_PIXEL");
    }

    // The same picture stored plane by plane: 3 x height x width. The channel
    // is the *first* axis, so a viewer that assumes the last one draws noise.
    {
        std::vector<std::uint8_t> planes(3 * size * size);
        std::uint8_t rgb[3] = {};
        for (hsize_t y = 0; y < size; ++y) {
            for (hsize_t x = 0; x < size; ++x) {
                const double hue = static_cast<double>(x) / static_cast<double>(size);
                const double value =
                    1.0 - static_cast<double>(y) / static_cast<double>(size);
                hueToRgb(hue, value, rgb);
                for (hsize_t c = 0; c < 3; ++c) {
                    planes[(c * size + y) * size + x] = rgb[c];
                }
            }
        }
        writeDataset(group, "rgb_planar_3x256x256", H5T_NATIVE_UINT8, {3, size, size},
                     planes.data(), chunked({1, 32, 32}));
        tagImageByName(group, "rgb_planar_3x256x256", "IMAGE_TRUECOLOR",
                       "INTERLACE_PLANE");
    }

    // Four channels, with alpha falling off towards the edges.
    {
        constexpr hsize_t small = 128;
        std::vector<std::uint8_t> pixels(small * small * 4);
        for (hsize_t y = 0; y < small; ++y) {
            for (hsize_t x = 0; x < small; ++x) {
                std::uint8_t* pixel = &pixels[(y * small + x) * 4];
                hueToRgb(static_cast<double>(y) / static_cast<double>(small), 1.0, pixel);
                const double dx = (static_cast<double>(x) - 63.5) / 63.5;
                const double dy = (static_cast<double>(y) - 63.5) / 63.5;
                pixel[3] = static_cast<std::uint8_t>(
                    std::clamp(1.0 - std::sqrt(dx * dx + dy * dy), 0.0, 1.0) * 255.0);
            }
        }
        writeDataset(group, "rgba_128x128x4", H5T_NATIVE_UINT8, {small, small, 4},
                     pixels.data());
        tagImageByName(group, "rgba_128x128x4", "IMAGE_TRUECOLOR", "INTERLACE_PIXEL");
    }

    // Grayscale, the one shape the image view was built for.
    {
        constexpr hsize_t big = 512;
        std::vector<std::uint8_t> pixels(big * big);
        for (hsize_t y = 0; y < big; ++y) {
            for (hsize_t x = 0; x < big; ++x) {
                const double dx = (static_cast<double>(x) - 255.5) / 255.5;
                const double dy = (static_cast<double>(y) - 255.5) / 255.5;
                const double r = std::sqrt(dx * dx + dy * dy);
                pixels[y * big + x] = static_cast<std::uint8_t>(
                    127.5 * (1.0 + std::sin(r * 18.0)) * std::exp(-r));
            }
        }
        writeDataset(group, "gray_512x512", H5T_NATIVE_UINT8, {big, big}, pixels.data(),
                     chunked({64, 64}));
        const Id dataset(H5Dopen2(group, "gray_512x512", H5P_DEFAULT), &H5Dclose,
                         "reopen gray");
        tagImage(dataset, "IMAGE_GRAYSCALE");
        numericAttribute<std::uint8_t>(dataset, "IMAGE_MINMAXRANGE", H5T_NATIVE_UINT8,
                                       {2}, {0, 255});
    }

    // Indexed colour: the raster holds palette indices and the palette is a
    // second dataset the image points at by reference.
    {
        constexpr hsize_t indexed = 64;
        std::vector<std::uint8_t> palette(256 * 3);
        for (hsize_t i = 0; i < 256; ++i) {
            hueToRgb(static_cast<double>(i) / 256.0, 1.0, &palette[i * 3]);
        }
        writeDataset(group, "palette", H5T_NATIVE_UINT8, {256, 3}, palette.data());
        {
            const Id dataset(H5Dopen2(group, "palette", H5P_DEFAULT), &H5Dclose,
                             "reopen palette");
            stringAttribute(dataset, "CLASS", "PALETTE");
            stringAttribute(dataset, "PAL_VERSION", "1.2");
            stringAttribute(dataset, "PAL_COLORMODEL", "RGB");
            stringAttribute(dataset, "PAL_TYPE", "STANDARD8");
        }

        std::vector<std::uint8_t> indices(indexed * indexed);
        for (hsize_t y = 0; y < indexed; ++y) {
            for (hsize_t x = 0; x < indexed; ++x) {
                indices[y * indexed + x] = static_cast<std::uint8_t>((x * 4) ^ (y * 4));
            }
        }
        writeDataset(group, "indexed_64x64", H5T_NATIVE_UINT8, {indexed, indexed},
                     indices.data());
        const Id dataset(H5Dopen2(group, "indexed_64x64", H5P_DEFAULT), &H5Dclose,
                         "reopen indexed");
        tagImage(dataset, "IMAGE_INDEXED");

        // The spec's PALETTE attribute is a reference to the palette dataset.
        H5R_ref_t reference{};
        must(H5Rcreate_object(group, "palette", H5P_DEFAULT, &reference),
             "reference to palette");
        const Id space = makeSpace({1});
        const Id attribute(
            H5Acreate2(dataset, "PALETTE", H5T_STD_REF, space, H5P_DEFAULT, H5P_DEFAULT),
            &H5Aclose, "create PALETTE attribute");
        must(H5Awrite(attribute, H5T_STD_REF, &reference), "write PALETTE attribute");
        must(H5Rdestroy(&reference), "release reference");
    }

    // A stack of frames: rank 4, and multi-channel, so the slice controls have
    // to pick a frame *and* keep the channel axis whole.
    {
        constexpr hsize_t frames = 8;
        constexpr hsize_t side = 64;
        std::vector<std::uint8_t> stack(frames * side * side * 3);
        for (hsize_t f = 0; f < frames; ++f) {
            for (hsize_t y = 0; y < side; ++y) {
                for (hsize_t x = 0; x < side; ++x) {
                    const double hue = std::fmod(static_cast<double>(x) / side +
                                                     static_cast<double>(f) / frames,
                                                 1.0);
                    hueToRgb(hue, 1.0 - static_cast<double>(y) / side,
                             &stack[((f * side + y) * side + x) * 3]);
                }
            }
        }
        writeDataset(group, "stack_8x64x64x3", H5T_NATIVE_UINT8, {frames, side, side, 3},
                     stack.data(), chunked({1, 32, 32, 3}));
        const Id dataset(H5Dopen2(group, "stack_8x64x64x3", H5P_DEFAULT), &H5Dclose,
                         "reopen stack");
        stringAttribute(dataset, "note",
                        "8 frames of 64x64 RGB: rank 4, and only two axes fit on screen");
        stringAttribute(dataset, "dimensions", "frame, row, column, channel");
    }

    // Twelve bands of 16-bit data: the multi-channel case where the channel
    // axis is far too long to be a colour.
    {
        constexpr hsize_t side = 64;
        constexpr hsize_t bands = 12;
        std::vector<std::uint16_t> cube(side * side * bands);
        for (hsize_t y = 0; y < side; ++y) {
            for (hsize_t x = 0; x < side; ++x) {
                for (hsize_t b = 0; b < bands; ++b) {
                    const double v =
                        std::sin((static_cast<double>(x) + static_cast<double>(b) * 3.0) /
                                 8.0) *
                        std::cos(static_cast<double>(y) / 6.0);
                    cube[(y * side + x) * bands + b] =
                        static_cast<std::uint16_t>((v + 1.0) * 32000.0);
                }
            }
        }
        writeDataset(group, "multispectral_64x64x12", H5T_NATIVE_UINT16,
                     {side, side, bands}, cube.data(), chunked({16, 16, 12}));
        const Id dataset(H5Dopen2(group, "multispectral_64x64x12", H5P_DEFAULT),
                         &H5Dclose, "reopen multispectral");
        stringAttribute(dataset, "dimensions", "row, column, band");
    }

    // A grayscale image that asks to be drawn inverted and from the lower
    // left. The first the viewer honours; the second it does not, and the
    // Information panel says so rather than drawing it upside down in silence.
    {
        constexpr hsize_t side = 32;
        std::vector<std::uint8_t> pixels(side * side);
        for (hsize_t y = 0; y < side; ++y) {
            for (hsize_t x = 0; x < side; ++x) {
                pixels[y * side + x] = static_cast<std::uint8_t>(x * 8);
            }
        }
        writeDataset(group, "gray_white_is_zero", H5T_NATIVE_UINT8, {side, side},
                     pixels.data());
        const Id dataset(H5Dopen2(group, "gray_white_is_zero", H5P_DEFAULT), &H5Dclose,
                         "reopen inverted gray");
        tagImage(dataset, "IMAGE_GRAYSCALE");
        numericAttribute<std::uint8_t>(dataset, "IMAGE_WHITE_IS_ZERO", H5T_NATIVE_UINT8,
                                       {}, {1});
        numericAttribute<std::uint8_t>(dataset, "IMAGE_MINMAXRANGE", H5T_NATIVE_UINT8,
                                       {2}, {0, 248});
        stringAttribute(dataset, "DISPLAY_ORIGIN", "LL");
    }

    // A dataset that says it is a truecolour image and has a shape no
    // truecolour image can have. The file is allowed to be wrong about itself,
    // and a viewer that rearranges on the strength of an attribute alone draws
    // nonsense when it is.
    {
        constexpr hsize_t side = 16;
        std::vector<std::uint8_t> pixels(side * side);
        for (std::size_t i = 0; i < pixels.size(); ++i) {
            pixels[i] = static_cast<std::uint8_t>(i);
        }
        writeDataset(group, "mislabelled_truecolor", H5T_NATIVE_UINT8, {side, side},
                     pixels.data());
        tagImageByName(group, "mislabelled_truecolor", "IMAGE_TRUECOLOR",
                       "INTERLACE_PIXEL");
    }

    // A continuous field rather than a picture: the same raster read as an
    // image, as a set of lines, and as a table of numbers.
    {
        std::vector<double> field(size * size);
        for (hsize_t y = 0; y < size; ++y) {
            for (hsize_t x = 0; x < size; ++x) {
                const double dx = (static_cast<double>(x) - 128.0) / 32.0;
                const double dy = (static_cast<double>(y) - 128.0) / 32.0;
                field[y * size + x] = std::exp(-(dx * dx + dy * dy) / 8.0) *
                                      std::sin(dx * 2.0) * std::cos(dy * 2.0);
            }
        }
        writeDataset(group, "field_256x256", H5T_NATIVE_DOUBLE, {size, size},
                     field.data(), chunked({32, 32}));
        const Id dataset(H5Dopen2(group, "field_256x256", H5P_DEFAULT), &H5Dclose,
                         "reopen field");
        numericAttribute<double>(dataset, "valid_range", H5T_NATIVE_DOUBLE, {2},
                                 {-1.0, 1.0});
        stringAttribute(dataset, "units", "volt");
    }
}

// --- /large: more data than fits on a screen, or in memory -----------------

void writeLarge(hid_t file)
{
    const Id group = makeGroup(file, "large");
    stringAttribute(group, "purpose",
                    "Datasets no viewport can hold: the paging and thinning paths");

    // Four million cells. Smooth, so deflate keeps the file reasonable.
    {
        constexpr hsize_t side = 2000;
        std::vector<std::int32_t> values(side * side);
        for (hsize_t r = 0; r < side; ++r) {
            for (hsize_t c = 0; c < side; ++c) {
                values[r * side + c] = static_cast<std::int32_t>((r * side + c) % 100000);
            }
        }
        Id props = chunked({100, 100});
        must(H5Pset_shuffle(props), "shuffle");
        must(H5Pset_deflate(props, 4), "deflate");
        writeDataset(group, "grid_2000x2000", H5T_NATIVE_INT32, {side, side},
                     values.data(), props);
    }

    // Two million samples of a signal: far more points than a plot has pixels.
    {
        constexpr hsize_t count = 2000000;
        std::vector<float> signal(count);
        for (hsize_t i = 0; i < count; ++i) {
            const double t = static_cast<double>(i) / 1000.0;
            signal[i] = static_cast<float>(std::sin(t) + 0.3 * std::sin(t * 17.0) +
                                           0.05 * std::sin(t * 313.0));
        }
        Id props = chunked({65536});
        must(H5Pset_shuffle(props), "shuffle");
        must(H5Pset_deflate(props, 4), "deflate");
        writeDataset(group, "signal_2M", H5T_NATIVE_FLOAT, {count}, signal.data(), props);
        const Id dataset(H5Dopen2(group, "signal_2M", H5P_DEFAULT), &H5Dclose,
                         "reopen signal");
        stringAttribute(dataset, "note",
                        "A spike narrower than the plot's stride is not drawn");
        doubleAttribute(dataset, "sample_rate_hz", 1000.0);
    }

    // A tall table: half a million rows of four columns.
    {
        constexpr hsize_t rows = 500000;
        std::vector<float> table(rows * 4);
        for (hsize_t r = 0; r < rows; ++r) {
            const double t = static_cast<double>(r) / 500.0;
            table[r * 4 + 0] = static_cast<float>(t);
            table[r * 4 + 1] = static_cast<float>(std::sin(t));
            table[r * 4 + 2] = static_cast<float>(std::cos(t));
            table[r * 4 + 3] = static_cast<float>(r % 997);
        }
        Id props = chunked({8192, 4});
        must(H5Pset_deflate(props, 4), "deflate");
        writeDataset(group, "table_500000x4", H5T_NATIVE_FLOAT, {rows, 4}, table.data(),
                     props);
        const Id dataset(H5Dopen2(group, "table_500000x4", H5P_DEFAULT), &H5Dclose,
                         "reopen table");
        stringArrayAttribute(dataset, "column_names", {"t", "sin", "cos", "counter"});
    }

    // A billion elements that were never written: four gigabytes of logical
    // extent in no bytes of file at all. Anything that reads a dataset whole
    // dies here; anything that reads only what it shows does not notice.
    {
        Id props = chunked({1024, 1024});
        const std::int32_t fill = 7;
        must(H5Pset_fill_value(props, H5T_NATIVE_INT32, &fill), "fill");
        must(H5Pset_alloc_time(props, H5D_ALLOC_TIME_LATE), "late allocation");
        writeDataset(group, "unallocated_100000x10000", H5T_NATIVE_INT32, {100000, 10000},
                     nullptr, props);
        const Id dataset(H5Dopen2(group, "unallocated_100000x10000", H5P_DEFAULT),
                         &H5Dclose, "reopen unallocated");
        stringAttribute(dataset, "note",
                        "10^9 elements, 0 bytes stored: every cell reads back as 7");
    }
}

// --- /links: every way one name can stand for another ----------------------

void writeLinks(hid_t file, const std::string& externalFileName)
{
    const Id group = makeGroup(file, "links");
    stringAttribute(group, "purpose", "Hard, soft, external and circular links");

    // Hard: a second name for the same object. Nothing distinguishes the
    // original from the link; both are the object.
    must(H5Lcreate_hard(file, "/data/matrix", group, "hard_to_matrix", H5P_DEFAULT,
                        H5P_DEFAULT),
         "hard link to a dataset");
    must(H5Lcreate_hard(file, "/data", group, "hard_to_data_group", H5P_DEFAULT,
                        H5P_DEFAULT),
         "hard link to a group");

    // Soft: a stored path, resolved on use. The target may be anything, or
    // nothing at all.
    must(
        H5Lcreate_soft("/data/matrix", group, "soft_to_matrix", H5P_DEFAULT, H5P_DEFAULT),
        "soft link to a dataset");
    must(H5Lcreate_soft("/images", group, "soft_to_images", H5P_DEFAULT, H5P_DEFAULT),
         "soft link to a group");
    must(H5Lcreate_soft("/no/such/object", group, "soft_dangling", H5P_DEFAULT,
                        H5P_DEFAULT),
         "dangling soft link");
    must(H5Lcreate_soft("/links/soft_to_matrix", group, "soft_to_soft", H5P_DEFAULT,
                        H5P_DEFAULT),
         "soft link to a soft link");
    // A soft link that points at its own container: legal, and a loop.
    must(H5Lcreate_soft("/links", group, "soft_to_self", H5P_DEFAULT, H5P_DEFAULT),
         "soft link to its own group");

    // External: a path in another file.
    must(H5Lcreate_external(externalFileName.c_str(), "/external/squares", group,
                            "external_dataset", H5P_DEFAULT, H5P_DEFAULT),
         "external link to a dataset");
    must(H5Lcreate_external(externalFileName.c_str(), "/external", group,
                            "external_group", H5P_DEFAULT, H5P_DEFAULT),
         "external link to a group");
    must(H5Lcreate_external(externalFileName.c_str(), "/no/such/object", group,
                            "external_missing_target", H5P_DEFAULT, H5P_DEFAULT),
         "external link to a missing object");
    must(H5Lcreate_external("no_such_file.h5", "/anything", group,
                            "external_missing_file", H5P_DEFAULT, H5P_DEFAULT),
         "external link to a missing file");

    // A hard link from a subgroup back to its ancestor: the cycle a tree that
    // walks blindly never returns from.
    {
        const Id loop = makeGroup(group, "loop");
        stringAttribute(loop, "note", "Contains a hard link back to /links");
        must(H5Lcreate_hard(file, "/links", loop, "back_to_links", H5P_DEFAULT,
                            H5P_DEFAULT),
             "cycle");
    }
}

// --- /stress: shapes of the hierarchy itself -------------------------------

void writeStress(hid_t file)
{
    const Id group = makeGroup(file, "stress");
    stringAttribute(group, "purpose", "Awkward hierarchies rather than awkward data");

    // More children than a listing can hold at once.
    {
        const Id many = makeGroup(group, "many_children_512");
        const std::int32_t value = 1;
        for (int i = 0; i < 512; ++i) {
            char name[32] = {};
            std::snprintf(name, sizeof(name), "item_%03d", i);
            writeDataset(many, name, H5T_NATIVE_INT32, {}, &value);
        }
    }

    // Deeper than any layout has indentation for.
    {
        std::vector<Id> levels;
        levels.reserve(24);
        hid_t parent = group;
        for (int depth = 0; depth < 24; ++depth) {
            char name[32] = {};
            std::snprintf(name, sizeof(name), "level_%02d", depth);
            levels.emplace_back(H5Gcreate2(parent, depth == 0 ? "deep" : name,
                                           H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT),
                                &H5Gclose, "deep group");
            parent = levels.back();
        }
        const std::int32_t value = 24;
        writeDataset(parent, "leaf", H5T_NATIVE_INT32, {}, &value);
    }

    // Names HDF5 allows and a viewer must not assume away. Everything except
    // '/' and a leading '.' is legal in a link name.
    {
        const Id names = makeGroup(group, "awkward_names");
        const std::int32_t value = 0;
        writeDataset(names, "with space", H5T_NATIVE_INT32, {}, &value);
        writeDataset(names, "with.dots.everywhere", H5T_NATIVE_INT32, {}, &value);
        writeDataset(names, "with\ttab", H5T_NATIVE_INT32, {}, &value);
        writeDataset(names, "1_leading_digit", H5T_NATIVE_INT32, {}, &value);
        writeDataset(names, "UPPER_and_lower", H5T_NATIVE_INT32, {}, &value);
        writeDataset(names, "\u00fc\u00f1\u00ef\u00e7\u00f8d\u00e9", H5T_NATIVE_INT32, {},
                     &value);
        writeDataset(names, "\u6e2c\u5b9a\u5024", H5T_NATIVE_INT32, {}, &value);
        writeDataset(names, "emoji \U0001F30D name", H5T_NATIVE_INT32, {}, &value);
        writeDataset(names, std::string(200, 'x').c_str(), H5T_NATIVE_INT32, {}, &value);
        writeDataset(names, "-leading-dash", H5T_NATIVE_INT32, {}, &value);
    }

    // A group whose links are indexed by creation order rather than by name,
    // so the order HDF5 hands them back is not alphabetical.
    {
        Id props(H5Pcreate(H5P_GROUP_CREATE), &H5Pclose, "group creation plist");
        must(H5Pset_link_creation_order(props,
                                        H5P_CRT_ORDER_TRACKED | H5P_CRT_ORDER_INDEXED),
             "track creation order");
        const Id ordered(
            H5Gcreate2(group, "creation_order", H5P_DEFAULT, props, H5P_DEFAULT),
            &H5Gclose, "creation-order group");
        stringAttribute(ordered, "note", "Created zulu, mike, alpha -- in that order");
        const std::int32_t value = 0;
        writeDataset(ordered, "zulu", H5T_NATIVE_INT32, {}, &value);
        writeDataset(ordered, "mike", H5T_NATIVE_INT32, {}, &value);
        writeDataset(ordered, "alpha", H5T_NATIVE_INT32, {}, &value);
    }

    // A group holding nothing.
    {
        const Id empty = makeGroup(group, "empty_group");
        stringAttribute(empty, "note", "No members at all");
    }

    // An object carrying a great many attributes.
    {
        const Id crowded = makeGroup(group, "many_attributes");
        for (int i = 0; i < 64; ++i) {
            char name[32] = {};
            std::snprintf(name, sizeof(name), "attr_%02d", i);
            intAttribute(crowded, name, i);
        }
    }
}

// --- references, written last so their targets already exist ---------------

void writeReferences(hid_t file)
{
    const Id group(H5Gopen2(file, "/types", H5P_DEFAULT), &H5Gclose, "reopen /types");

    // Object references: a dataset, a group and a named datatype, side by side
    // in one array.
    {
        std::vector<H5R_ref_t> references(3);
        must(H5Rcreate_object(file, "/data/matrix", H5P_DEFAULT, &references[0]),
             "reference to a dataset");
        must(H5Rcreate_object(file, "/images", H5P_DEFAULT, &references[1]),
             "reference to a group");
        must(H5Rcreate_object(file, "/committed/celsius_t", H5P_DEFAULT, &references[2]),
             "reference to a named datatype");
        writeDataset(group, "reference_object", H5T_STD_REF, {3}, references.data());
        for (H5R_ref_t& reference : references) {
            must(H5Rdestroy(&reference), "release reference");
        }
        const Id dataset(H5Dopen2(group, "reference_object", H5P_DEFAULT), &H5Dclose,
                         "reopen object references");
        stringAttribute(dataset, "targets",
                        "/data/matrix, /images, /committed/celsius_t");
    }

    // A region reference: not an object, but a selection inside one.
    {
        const hsize_t matrixDims[] = {4, 3};
        const Id space(H5Screate_simple(2, matrixDims, nullptr), &H5Sclose,
                       "region space");
        const hsize_t start[] = {1, 0};
        const hsize_t count[] = {2, 2};
        must(H5Sselect_hyperslab(space, H5S_SELECT_SET, start, nullptr, count, nullptr),
             "select region");
        H5R_ref_t reference{};
        must(H5Rcreate_region(file, "/data/matrix", space, H5P_DEFAULT, &reference),
             "region reference");
        writeDataset(group, "reference_region", H5T_STD_REF, {1}, &reference);
        must(H5Rdestroy(&reference), "release region reference");
        const Id dataset(H5Dopen2(group, "reference_region", H5P_DEFAULT), &H5Dclose,
                         "reopen region reference");
        stringAttribute(dataset, "target", "/data/matrix rows 1-2, columns 0-1");
    }
}

// --- attributes on the root group ------------------------------------------

void writeRootAttributes(hid_t file)
{
    stringAttribute(file, "title", "H5Scope example file");
    stringAttribute(file, "description",
                    "Every HDF5 feature the viewer is expected to render, in one file. "
                    "Generated by tools/make-example-file.cpp.");
    stringAttribute(file, "conventions", "None; this file is deliberately eclectic");
    intAttribute(file, "format_version", 3);
    doubleAttribute(file, "scale_factor", 0.001);

    // A fixed-length string attribute, which is stored quite differently from
    // the variable-length ones above.
    {
        const Id type = fixedString(32);
        const Id space(H5Screate(H5S_SCALAR), &H5Sclose, "attribute space");
        const Id attribute(
            H5Acreate2(file, "generator", type, space, H5P_DEFAULT, H5P_DEFAULT),
            &H5Aclose, "fixed string attribute");
        char text[32] = {};
        std::snprintf(text, sizeof(text), "make-example-file");
        must(H5Awrite(attribute, type, text), "write fixed string attribute");
    }

    stringArrayAttribute(file, "history",
                         {"created", "checked against h5dump", "read by H5Scope"});
    numericAttribute<double>(file, "coefficients", H5T_NATIVE_DOUBLE, {5},
                             {1.0, -0.5, 0.25, -0.125, 0.0625});
    numericAttribute<std::int32_t>(file, "shape_2x3", H5T_NATIVE_INT32, {2, 3},
                                   {1, 2, 3, 4, 5, 6});
    // Zero elements: an attribute that exists and holds nothing.
    numericAttribute<std::int32_t>(file, "empty_attribute", H5T_NATIVE_INT32, {0}, {});
    // More elements than any panel will print, so the rendering has to elide.
    {
        std::vector<std::int32_t> many(1000);
        for (std::size_t i = 0; i < many.size(); ++i) {
            many[i] = static_cast<std::int32_t>(i);
        }
        numericAttribute<std::int32_t>(file, "long_attribute", H5T_NATIVE_INT32, {1000},
                                       many);
    }
    stringAttribute(file, "unicode \u2713",
                    "value with \u00e9\u00e8\u00ea and \u6f22\u5b57");

    // A compound attribute and an enum attribute: both are as legal on an
    // attribute as on a dataset, and both need the same rendering.
    {
        const Id type(H5Tcreate(H5T_COMPOUND, sizeof(Simple)), &H5Tclose,
                      "compound attribute type");
        must(H5Tinsert(type, "id", HOFFSET(Simple, id), H5T_NATIVE_INT32), "id");
        must(H5Tinsert(type, "value", HOFFSET(Simple, value), H5T_NATIVE_DOUBLE),
             "value");
        const Id space(H5Screate(H5S_SCALAR), &H5Sclose, "attribute space");
        const Id attribute(
            H5Acreate2(file, "compound_attribute", type, space, H5P_DEFAULT, H5P_DEFAULT),
            &H5Aclose, "compound attribute");
        const Simple value{5, 2.5};
        must(H5Awrite(attribute, type, &value), "write compound attribute");
    }
    {
        const Id type = qualityEnum();
        const Id space(H5Screate(H5S_SCALAR), &H5Sclose, "attribute space");
        const Id attribute(
            H5Acreate2(file, "quality", type, space, H5P_DEFAULT, H5P_DEFAULT), &H5Aclose,
            "enum attribute");
        const std::int32_t value = 2;
        must(H5Awrite(attribute, type, &value), "write enum attribute");
    }
}

/// One reference attribute, written after every target exists.
void writeRootReferenceAttribute(hid_t file)
{
    H5R_ref_t reference{};
    must(H5Rcreate_object(file, "/images/rgb_256x256x3", H5P_DEFAULT, &reference),
         "reference for the root attribute");
    const Id space(H5Screate(H5S_SCALAR), &H5Sclose, "attribute space");
    const Id attribute(
        H5Acreate2(file, "cover_image", H5T_STD_REF, space, H5P_DEFAULT, H5P_DEFAULT),
        &H5Aclose, "reference attribute");
    must(H5Awrite(attribute, H5T_STD_REF, &reference), "write reference attribute");
    must(H5Rdestroy(&reference), "release reference");
}

} // namespace

void writeExampleFiles(const std::filesystem::path& directory)
{
    std::filesystem::create_directories(directory);

    const std::string externalName = "example_external.h5";
    writeExternalFile((directory / externalName).string());

    // H5Pset_external names the raw-data file relative to the working
    // directory, so write from the output directory rather than depending on
    // where the caller was invoked.
    const std::filesystem::path previous = std::filesystem::current_path();
    std::filesystem::current_path(directory);
    struct Restore
    {
        const std::filesystem::path& path;
        ~Restore() { std::filesystem::current_path(path); }
    } restore{previous};

    {
        const Id file(H5Fcreate("example.h5", H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT),
                      &H5Fclose, "create example.h5");
        writeRootAttributes(file);
        writeData(file);
        writeTypes(file);
        writeCommitted(file);
        writeStorage(file, externalName);
        writeFilters(file);
        writeImages(file);
        writeLarge(file);
        writeLinks(file, externalName);
        writeStress(file);
        writeReferences(file);
        writeRootReferenceAttribute(file);
    }

    // The stand-in for the third-party filter goes away with the file that
    // needed it, so this process reads example.h5 on the same terms as any
    // other: filter 32004 is absent, and /filters/unavailable_mandatory
    // cannot be decoded.
    must(H5Zunregister(kPretendLz4.id), "unregister the stand-in filter");
}

} // namespace h5example
