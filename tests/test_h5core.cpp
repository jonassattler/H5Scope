// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "support/TestFile.hpp"

#include "h5core/Attribute.hpp"
#include "h5core/Dataset.hpp"
#include "h5core/Error.hpp"
#include "h5core/File.hpp"
#include "h5core/Types.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>

#include <algorithm>
#include <string>
#include <vector>

using Catch::Matchers::ContainsSubstring;

namespace {

/// One fixture file shared by the whole suite; it is read-only.
struct Fixture {
    h5test::TempFile temp{"core"};

    Fixture() { h5test::writeFixture(temp.path()); }
};

std::vector<std::string> childNames(const std::vector<h5core::NodeInfo>& nodes)
{
    std::vector<std::string> names;
    names.reserve(nodes.size());
    for (const auto& node : nodes) {
        names.push_back(node.name);
    }
    return names;
}

const h5core::NodeInfo* findChild(const std::vector<h5core::NodeInfo>& nodes,
                                  const std::string& name)
{
    const auto it = std::find_if(nodes.begin(), nodes.end(),
                                 [&](const auto& n) { return n.name == name; });
    return (it == nodes.end()) ? nullptr : &*it;
}

} // namespace

TEST_CASE_METHOD(Fixture, "opening files", "[h5core][file]")
{
    SECTION("a valid file opens and reports its path")
    {
        const h5core::File file(temp.path());
        REQUIRE(file.path() == temp.path());
        REQUIRE(h5core::File::isHDF5(temp.path()));
    }

    SECTION("a missing file throws rather than aborting")
    {
        REQUIRE_THROWS_AS(h5core::File("/nonexistent/nope.h5"), h5core::H5Error);
    }

    SECTION("a non-HDF5 file is rejected")
    {
        REQUIRE_FALSE(h5core::File::isHDF5("/etc/hostname"));
    }
}

TEST_CASE_METHOD(Fixture, "walking the hierarchy", "[h5core][file]")
{
    const h5core::File file(temp.path());

    SECTION("root lists every top-level object in name order")
    {
        const auto names = childNames(file.children("/"));
        REQUIRE(std::is_sorted(names.begin(), names.end()));
        REQUIRE(std::find(names.begin(), names.end(), "matrix") != names.end());
        REQUIRE(std::find(names.begin(), names.end(), "group") != names.end());
    }

    SECTION("kinds are classified")
    {
        const auto children = file.children("/");
        REQUIRE(findChild(children, "group")->kind == h5core::NodeKind::Group);
        REQUIRE(findChild(children, "matrix")->kind == h5core::NodeKind::Dataset);
    }

    SECTION("nested groups are reachable")
    {
        const auto names = childNames(file.children("/group/nested"));
        REQUIRE(names == std::vector<std::string>{"leaf"});
    }

    SECTION("a hard link resolves to the same object identity as its target")
    {
        // This is what lets the tree model break hard-link cycles.
        const auto children = file.children("/");
        const auto* target = findChild(children, "matrix");
        const auto* link = findChild(children, "link_to_matrix");
        REQUIRE(target != nullptr);
        REQUIRE(link != nullptr);
        REQUIRE(link->address.has_value());
        REQUIRE(link->address == target->address);
        REQUIRE(link->fileNumber == target->fileNumber);
    }

    SECTION("listing a dataset as a group fails cleanly")
    {
        REQUIRE_THROWS_AS(file.children("/matrix"), h5core::H5Error);
    }

    SECTION("existence checks")
    {
        REQUIRE(file.exists("/matrix"));
        REQUIRE(file.exists("/"));
        REQUIRE_FALSE(file.exists("/does_not_exist"));
    }
}

TEST_CASE_METHOD(Fixture, "dataset metadata", "[h5core][dataset]")
{
    const h5core::File file(temp.path());

    SECTION("2-D shape and type")
    {
        const h5core::Dataset ds(file, "/matrix");
        REQUIRE(ds.info().shape == std::vector<hsize_t>{4, 3});
        REQUIRE(ds.info().rank() == 2);
        REQUIRE(ds.info().type.cls == h5core::TypeClass::Float);
        REQUIRE(ds.info().type.description == "float64");
    }

    SECTION("scalar has rank 0 but one element")
    {
        const h5core::Dataset ds(file, "/scalar_int");
        REQUIRE(ds.info().isScalar());
        REQUIRE(ds.info().rank() == 0);
        REQUIRE(ds.info().elementCount() == 1);
    }

    SECTION("chunking and the compression pipeline are reported")
    {
        const h5core::Dataset ds(file, "/compressed");
        REQUIRE(ds.info().layout == h5core::Layout::Chunked);
        REQUIRE(ds.info().chunk == std::vector<hsize_t>{10, 10});
        REQUIRE(ds.info().filters.size() == 1);
        REQUIRE_THAT(ds.info().filters.front(), ContainsSubstring("deflate"));
        // gzip ships with our pinned HDF5, so it must be decodable.
        REQUIRE(ds.info().readable());
    }

    SECTION("an uncompressed dataset reports no filters")
    {
        const h5core::Dataset ds(file, "/matrix");
        REQUIRE(ds.info().filters.empty());
        REQUIRE(ds.info().readable());
    }

    SECTION("compound member names are exposed")
    {
        const h5core::Dataset ds(file, "/compound");
        REQUIRE(ds.info().type.cls == h5core::TypeClass::Compound);
        REQUIRE(ds.info().type.memberNames == std::vector<std::string>{"id", "value"});
    }

    SECTION("enum symbols are exposed")
    {
        const h5core::Dataset ds(file, "/enum");
        REQUIRE(ds.info().type.cls == h5core::TypeClass::Enum);
        REQUIRE(ds.info().type.memberNames
                == std::vector<std::string>{"RED", "GREEN", "BLUE"});
    }

    SECTION("variable and fixed strings are distinguished")
    {
        const h5core::Dataset vlen(file, "/str_vlen");
        REQUIRE(vlen.info().type.cls == h5core::TypeClass::String);
        REQUIRE(vlen.info().type.isVariableLength);

        const h5core::Dataset fixed(file, "/str_fixed");
        REQUIRE(fixed.info().type.cls == h5core::TypeClass::String);
        REQUIRE_FALSE(fixed.info().type.isVariableLength);
    }

    SECTION("opening a group as a dataset throws")
    {
        REQUIRE_THROWS_AS(h5core::Dataset(file, "/group"), h5core::H5Error);
    }
}

TEST_CASE_METHOD(Fixture, "reading data", "[h5core][dataset]")
{
    const h5core::File file(temp.path());

    SECTION("a full 2-D read is row-major")
    {
        const h5core::Dataset ds(file, "/matrix");
        const auto window = ds.readAll();
        REQUIRE(window.cells.size() == 12);
        // fixture value is row*10 + column
        REQUIRE(window.at(0, 0) == "0");
        REQUIRE(window.at(0, 2) == "2");
        REQUIRE(window.at(3, 0) == "30");
        REQUIRE(window.at(3, 2) == "32");
    }

    SECTION("a hyperslab reads only the requested block")
    {
        const h5core::Dataset ds(file, "/matrix");
        const auto window = ds.readWindow({1, 1}, {2, 2});
        REQUIRE(window.count == std::vector<hsize_t>{2, 2});
        REQUIRE(window.cells.size() == 4);
        REQUIRE(window.at(0, 0) == "11");
        REQUIRE(window.at(1, 1) == "22");
    }

    SECTION("a window overhanging the edge is clamped, not an error")
    {
        const h5core::Dataset ds(file, "/matrix");
        const auto window = ds.readWindow({3, 2}, {10, 10});
        REQUIRE(window.count == std::vector<hsize_t>{1, 1});
        REQUIRE(window.cells.size() == 1);
        REQUIRE(window.at(0, 0) == "32");
    }

    SECTION("a window starting past the end yields nothing")
    {
        const h5core::Dataset ds(file, "/matrix");
        const auto window = ds.readWindow({99, 0}, {2, 2});
        REQUIRE(window.cells.empty());
    }

    SECTION("a mismatched rank is rejected")
    {
        const h5core::Dataset ds(file, "/matrix");
        REQUIRE_THROWS_AS(ds.readWindow({0}, {1}), h5core::H5Error);
    }

    SECTION("scalar reads")
    {
        const h5core::Dataset ds(file, "/scalar_int");
        const auto window = ds.readAll();
        REQUIRE(window.cells.size() == 1);
        REQUIRE(window.cells.front() == "42");
    }

    SECTION("an empty dataset reads as no cells")
    {
        const h5core::Dataset ds(file, "/empty");
        REQUIRE(ds.info().elementCount() == 0);
        REQUIRE(ds.readAll().cells.empty());
    }

    SECTION("variable-length strings round-trip")
    {
        const h5core::Dataset ds(file, "/str_vlen");
        const auto window = ds.readAll();
        REQUIRE(window.cells
                == std::vector<std::string>{"one", "three three", "five five five"});
    }

    SECTION("fixed-length strings are trimmed of padding")
    {
        const h5core::Dataset ds(file, "/str_fixed");
        const auto window = ds.readAll();
        REQUIRE(window.cells == std::vector<std::string>{"alpha", "beta", "gamma"});
    }

    SECTION("compound elements render member by member")
    {
        const h5core::Dataset ds(file, "/compound");
        const auto window = ds.readAll();
        REQUIRE(window.cells.size() == 2);
        REQUIRE_THAT(window.cells[0], ContainsSubstring("id=7"));
        REQUIRE_THAT(window.cells[0], ContainsSubstring("value=1.5"));
    }

    SECTION("one compound element is read apart, and as JSON")
    {
        // A grid cell can only ever show the whole struct elided onto one
        // line, so the members have to be reachable one at a time as well.
        const h5core::Dataset ds(file, "/compound");
        const h5core::ElementValue element = ds.readElement({1});

        REQUIRE(element.fields.size() == 2);
        CHECK(element.fields[0].name == "id");
        CHECK(element.fields[0].type == "int32");
        CHECK(element.fields[0].value == "9");
        CHECK(element.fields[1].name == "value");
        CHECK(element.fields[1].type == "float64");
        CHECK(element.fields[1].value == "2.5");

        // Numbers stay numbers and names are quoted, so this parses.
        CHECK(element.json == R"({"id": 9, "value": 2.5})");
        // The same element as the grid prints it, so the two cannot disagree.
        CHECK(element.text == ds.readAll().cells[1]);
    }

    SECTION("a non-compound element has no fields to take apart")
    {
        // Every element still has a JSON form; only a compound has members.
        const h5core::Dataset ds(file, "/vec_int");
        const h5core::ElementValue element = ds.readElement({2});
        CHECK(element.fields.empty());
        CHECK(element.json == element.text);
        CHECK(element.json == ds.readAll().cells[2]);

        const h5core::Dataset text(file, "/str_fixed");
        CHECK(text.readElement({1}).json == R"("beta")");
    }

    SECTION("enum elements render as their symbol")
    {
        const h5core::Dataset ds(file, "/enum");
        const auto window = ds.readAll();
        REQUIRE(window.cells == std::vector<std::string>{"BLUE", "RED", "GREEN"});
    }

    SECTION("compressed data decodes transparently")
    {
        const h5core::Dataset ds(file, "/compressed");
        const auto window = ds.readWindow({0, 0}, {2, 3});
        REQUIRE(window.at(0, 0) == "0");
        REQUIRE(window.at(0, 1) == "1");
        REQUIRE(window.at(1, 0) == "100");
    }

    SECTION("a 3-D slab selects along the leading dimension")
    {
        const h5core::Dataset ds(file, "/cube");
        // cube[1][0][0] == 12 for a 2x3x4 ramp
        const auto window = ds.readWindow({1, 0, 0}, {1, 1, 1});
        REQUIRE(window.cells.size() == 1);
        REQUIRE(window.cells.front() == "12");
    }

    SECTION("a long vector reads correctly beyond the first block")
    {
        const h5core::Dataset ds(file, "/long_vec");
        REQUIRE(ds.info().shape == std::vector<hsize_t>{1000});
        const auto tail = ds.readWindow({900}, {4});
        REQUIRE(tail.cells
                == std::vector<std::string>{"900", "901", "902", "903"});
    }

    SECTION("readAll refuses a dataset above the element budget")
    {
        const h5core::Dataset ds(file, "/compressed"); // 10,000 elements
        REQUIRE_THROWS_AS(ds.readAll(100), h5core::H5Error);
    }
}

TEST_CASE_METHOD(Fixture, "reading data as numbers", "[h5core][dataset]")
{
    const h5core::File file(temp.path());

    SECTION("floats come back as themselves")
    {
        const h5core::Dataset ds(file, "/matrix"); // 4x3, value = row*10 + column
        const auto window = ds.readNumericWindow({0, 0}, {4, 3});
        REQUIRE(window.count == std::vector<hsize_t>{4, 3});
        REQUIRE(window.values.size() == 12);
        REQUIRE(window.values[0] == 0.0);
        REQUIRE(window.values[4] == 11.0);  // row 1, column 1
        REQUIRE(window.values[11] == 32.0); // row 3, column 2
    }

    SECTION("integers are widened rather than reinterpreted")
    {
        const h5core::Dataset ds(file, "/cube"); // 2x3x4 int32, value = flat index
        const auto window = ds.readNumericWindow({0, 0, 0}, {1, 1, 4});
        REQUIRE(window.values.size() == 4);
        REQUIRE(window.values[0] == 0.0);
        REQUIRE(window.values[3] == 3.0);
    }

    SECTION("it agrees with the text path, element for element")
    {
        const h5core::Dataset ds(file, "/matrix");
        const auto text = ds.readWindow({1, 0}, {2, 3});
        const auto numbers = ds.readNumericWindow({1, 0}, {2, 3});
        REQUIRE(text.cells.size() == numbers.values.size());
        for (std::size_t i = 0; i < numbers.values.size(); ++i) {
            REQUIRE(std::stod(text.cells[i]) == numbers.values[i]);
        }
    }

    SECTION("a window overhanging the edge is clamped, not an error")
    {
        const h5core::Dataset ds(file, "/matrix");
        const auto window = ds.readNumericWindow({3, 2}, {10, 10});
        REQUIRE(window.count == std::vector<hsize_t>{1, 1});
        REQUIRE(window.values.size() == 1);
        REQUIRE(window.values[0] == 32.0);
    }

    SECTION("a window starting past the end yields nothing")
    {
        const h5core::Dataset ds(file, "/matrix");
        const auto window = ds.readNumericWindow({99, 0}, {2, 2});
        REQUIRE(window.values.empty());
    }

    SECTION("a scalar reads as one value")
    {
        const h5core::Dataset ds(file, "/scalar_int");
        const auto window = ds.readNumericWindow({}, {});
        REQUIRE(window.values.size() == 1);
    }

    SECTION("text says it is text rather than failing to convert")
    {
        // The message matters: HDF5's own account of this is "no conversion
        // path", which tells the reader nothing about their dataset.
        const h5core::Dataset ds(file, "/str_vlen");
        REQUIRE_THROWS_WITH(ds.readNumericWindow({0}, {3}),
                            ContainsSubstring("no numeric value"));
    }

    SECTION("so do the types that only look numeric")
    {
        // An enum has an ordinal and a compound has members, but neither has a
        // value to plot; h5core::isNumeric is the one place that decides.
        REQUIRE_THROWS_AS(h5core::Dataset(file, "/enum").readNumericWindow({0}, {3}),
                          h5core::H5Error);
        REQUIRE_THROWS_AS(h5core::Dataset(file, "/compound").readNumericWindow({0}, {2}),
                          h5core::H5Error);
    }

    SECTION("an undecodable filter is reported before anything is read")
    {
        const h5core::Dataset ds(file, "/compressed");
        // Readable in this build, so this is the shape of the check rather
        // than the failure: it must not throw for a filter we do have.
        REQUIRE(ds.info().readable());
        REQUIRE_NOTHROW(ds.readNumericWindow({0, 0}, {2, 2}));
    }
}

TEST_CASE_METHOD(Fixture, "attributes", "[h5core][attribute]")
{
    const h5core::File file(temp.path());

    SECTION("presence drives whether the Metadata tab appears")
    {
        REQUIRE(file.hasAttributes("/group"));
        REQUIRE(file.hasAttributes("/scalar_int"));
        REQUIRE_FALSE(file.hasAttributes("/matrix"));
        REQUIRE(file.attributeCount("/group") == 2);
        REQUIRE(file.attributeCount("/matrix") == 0);
    }

    SECTION("group attributes read back with values")
    {
        const auto attrs = h5core::readAttributes(file, "/group");
        REQUIRE(attrs.size() == 2);
        // H5_INDEX_NAME ordering: title, version
        REQUIRE(attrs[0].name == "title");
        REQUIRE(attrs[0].value == "example group");
        REQUIRE(attrs[1].name == "version");
        REQUIRE(attrs[1].value == "3");
        REQUIRE(attrs[1].type.cls == h5core::TypeClass::Integer);
    }

    SECTION("dataset attributes read back")
    {
        const auto attrs = h5core::readAttributes(file, "/scalar_int");
        REQUIRE(attrs.size() == 1);
        REQUIRE(attrs[0].name == "units");
        REQUIRE(attrs[0].value == "kelvin");
    }

    SECTION("an object without attributes yields an empty list")
    {
        REQUIRE(h5core::readAttributes(file, "/matrix").empty());
    }
}
