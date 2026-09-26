// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "support/TestFile.hpp"

#include "h5core/Attribute.hpp"
#include "h5core/Dataset.hpp"
#include "h5core/DataType.hpp"
#include "h5core/Error.hpp"
#include "h5core/FieldDataset.hpp"
#include "h5core/File.hpp"
#include "h5core/Handle.hpp"
#include "h5core/Types.hpp"

#include "support/MemberChain.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>

#include <hdf5.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <utility>
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

TEST_CASE_METHOD(Fixture, "listing without opening", "[h5core][file]")
{
    // Resolve::Links is what makes a wide group cheap to expand: the names and
    // the link types come out of one structure, and what each name points at
    // is asked for later, per row that is actually shown.
    const h5core::File file(temp.path());

    SECTION("the same names come back either way, in the same order")
    {
        REQUIRE(childNames(file.children("/", h5core::File::Resolve::Links))
                == childNames(file.children("/", h5core::File::Resolve::Objects)));
    }

    SECTION("link information is there, object information is not yet")
    {
        const auto links = file.children("/", h5core::File::Resolve::Links);
        const auto* soft = findChild(links, "soft_to_matrix");
        REQUIRE(soft != nullptr);
        REQUIRE(soft->link == h5core::LinkType::Soft);
        REQUIRE(soft->linkTarget == "/matrix");
        // Not followed, so nothing is known about what is on the other end.
        REQUIRE(soft->kind == h5core::NodeKind::Unknown);
    }

    SECTION("a hard link still carries its object's identity")
    {
        // The link table holds the token itself, so cycle detection costs
        // nothing even in a listing that opens nothing.
        const auto links = file.children("/", h5core::File::Resolve::Links);
        const auto* target = findChild(links, "matrix");
        const auto* link = findChild(links, "link_to_matrix");
        REQUIRE(target->address.has_value());
        REQUIRE(link->address == target->address);
        REQUIRE(link->fileNumber == target->fileNumber);
    }

    SECTION("resolving one afterwards gives what listing objects would have")
    {
        auto links = file.children("/", h5core::File::Resolve::Links);
        auto* group = const_cast<h5core::NodeInfo*>(findChild(links, "group"));
        REQUIRE(group != nullptr);
        file.resolve(*group);
        REQUIRE(group->kind == h5core::NodeKind::Group);

        const auto objects = file.children("/", h5core::File::Resolve::Objects);
        const auto* eager = findChild(objects, "group");
        REQUIRE(group->kind == eager->kind);
        REQUIRE(group->address == eager->address);
        REQUIRE(group->attributeCount == eager->attributeCount);
    }

    SECTION("a link that leads nowhere resolves to Unresolved, not to a throw")
    {
        auto links = file.children("/", h5core::File::Resolve::Links);
        auto* dangling = const_cast<h5core::NodeInfo*>(findChild(links, "dangling"));
        REQUIRE(dangling != nullptr);
        file.resolve(*dangling);
        REQUIRE(dangling->kind == h5core::NodeKind::Unresolved);
        REQUIRE_FALSE(dangling->resolves());
    }
}

TEST_CASE_METHOD(Fixture, "asking a group its size without listing it",
                 "[h5core][file]")
{
    const h5core::File file(temp.path());

    SECTION("the count matches the listing")
    {
        REQUIRE(file.memberCount("/") == file.children("/").size());
        REQUIRE(file.memberCount("/group/nested") == 1);
    }

    SECTION("a dataset is not a group and says so")
    {
        REQUIRE_THROWS_AS(file.memberCount("/matrix"), h5core::H5Error);
    }
}

TEST_CASE_METHOD(Fixture, "the outline of a dataset", "[h5core][file]")
{
    // What a tree row needs, and deliberately no more: everything else in
    // DatasetInfo is further reads that no row shows.
    const h5core::File file(temp.path());

    SECTION("shape and dataspace agree with the full read")
    {
        for (const char* path : {"/matrix", "/scalar_int", "/empty", "/cube"}) {
            const auto outline = file.datasetOutline(path);
            const h5core::Dataset dataset(file, path);
            INFO(path);
            REQUIRE(outline.shape == dataset.info().shape);
            REQUIRE(outline.space == dataset.info().space);
        }
    }

    SECTION("nothing here claims to be an image")
    {
        REQUIRE_FALSE(file.datasetOutline("/matrix").image);
        // And the probe can be skipped outright when the object header has
        // already said there are no attributes to probe.
        REQUIRE_FALSE(file.datasetOutline("/matrix", false).image);
    }

    SECTION("a group is not a dataset and says so")
    {
        REQUIRE_THROWS_AS(file.datasetOutline("/group"), h5core::H5Error);
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

    SECTION("a compound's members carry their own types")
    {
        const h5core::Dataset ds(file, "/compound");
        const auto& members = ds.info().type.members;
        REQUIRE(members.size() == 2);

        CHECK(members[0].name == "id");
        CHECK(members[0].type.cls == h5core::TypeClass::Integer);
        CHECK(members[0].type.description == "int32");

        CHECK(members[1].name == "value");
        CHECK(members[1].type.cls == h5core::TypeClass::Float);
        CHECK(members[1].type.description == "float64");

        // The offsets are what a read of one member on its own is built out
        // of, so they have to be the file's and not this test's arithmetic.
        CHECK(members[0].offset < members[1].offset);
        CHECK(members[1].offset + members[1].type.size <= ds.info().type.size);
    }

    SECTION("the flat member names stay what they were")
    {
        // memberNames carries an enum's symbols as well as a compound's member
        // names, and the Information panel prints it. The member tree is beside
        // it rather than in place of it, and this is the assertion that says so.
        const h5core::Dataset ds(file, "/compound");
        REQUIRE(ds.info().type.memberNames == std::vector<std::string>{"id", "value"});
        REQUIRE(ds.info().type.memberNames.size() == ds.info().type.members.size());

        const h5core::Dataset colours(file, "/enum");
        CHECK(colours.info().type.memberNames.size() == 3);
        CHECK(colours.info().type.members.empty());
    }

    SECTION("a type that holds no other type names none")
    {
        const h5core::Dataset ds(file, "/matrix");
        CHECK(ds.info().type.members.empty());
        CHECK(ds.info().type.arrayDims.empty());
        CHECK(ds.info().type.base == nullptr);
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

TEST_CASE_METHOD(Fixture, "a member reads as a dataset of its own",
                 "[h5core][member]")
{
    const h5core::File file(temp.path());
    const h5core::Dataset whole(file, "/compound");

    SECTION("it reports the member's type and the dataset's shape")
    {
        const h5core::FieldDataset value(
            file, "/compound", h5test::chainOf(whole.info().type, {"value"}));

        CHECK(value.info().shape == whole.info().shape);
        CHECK(value.info().type.cls == h5core::TypeClass::Float);
        CHECK(value.info().isNumeric());
        CHECK(value.path() == "/compound.value");
    }

    SECTION("it reads that member's values and no others")
    {
        const h5core::FieldDataset value(
            file, "/compound", h5test::chainOf(whole.info().type, {"value"}));
        const auto numbers = value.readNumericWindow({0}, {2});
        REQUIRE(numbers.values == std::vector<double>{1.5, 2.5});

        const h5core::FieldDataset id(file, "/compound",
                                      h5test::chainOf(whole.info().type, {"id"}));
        CHECK(id.info().type.cls == h5core::TypeClass::Integer);
        const auto ids = id.readNumericWindow({0}, {2});
        REQUIRE(ids.values == std::vector<double>{7.0, 9.0});
        // As text it prints like the integer it is, not like a double.
        CHECK(id.readWindow({0}, {2}).cells == std::vector<std::string>{"7", "9"});
    }

    SECTION("a hyperslab of a member is still a hyperslab")
    {
        const h5core::FieldDataset value(
            file, "/compound", h5test::chainOf(whole.info().type, {"value"}));
        const auto second = value.readNumericWindow({1}, {1});
        CHECK(second.count == std::vector<hsize_t>{1});
        REQUIRE(second.values == std::vector<double>{2.5});

        // Clamped to the bounds, exactly as the dataset's own read is.
        const auto over = value.readNumericWindow({0}, {99});
        CHECK(over.count == std::vector<hsize_t>{2});
    }

    SECTION("one element of a member is one value, not a struct")
    {
        const h5core::FieldDataset value(
            file, "/compound", h5test::chainOf(whole.info().type, {"value"}));
        const h5core::ElementValue element = value.readElement({1});
        CHECK(element.text == "2.5");
        CHECK(element.json == "2.5");
        // The whole dataset's element is the struct; this one is a number, so
        // there is nothing left to open out.
        CHECK(element.fields.empty());
    }

    SECTION("a chain that does not apply is refused, not guessed at")
    {
        h5core::MemberSelection wrong;
        wrong.links.push_back(h5core::MemberLink{0, "nonesuch", std::nullopt});
        wrong.text = ".nonesuch";
        REQUIRE_THROWS_AS(h5core::FieldDataset(file, "/compound", wrong),
                          h5core::H5Error);

        // The index is right and the name is not: the one way a stale chain
        // could read the wrong field and never say so.
        h5core::MemberSelection renamed;
        renamed.links.push_back(h5core::MemberLink{0, "value", std::nullopt});
        renamed.text = ".value";
        REQUIRE_THROWS_AS(h5core::FieldDataset(file, "/compound", renamed),
                          h5core::H5Error);
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

        // Numbers stay numbers and names are quoted, so this parses -- and it
        // is written out over lines, because the pane it lands in is showing
        // one element to somebody reading it.
        CHECK(element.json == "{\n  \"id\": 9,\n  \"value\": 2.5\n}");
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

TEST_CASE("JSON is written to be read, and not only to be parsed", "[h5core][json]")
{
    // No file at all: toJson takes a datatype and a buffer. A type built here
    // is also the only way to state the rule about a list of structs, because
    // nothing in the fixtures or the example file has one.
    struct Point {
        double x;
        double y;
    };
    struct Holder {
        Point trail[2];
        std::int32_t count;
    };

    h5core::Handle point(H5Tcreate(H5T_COMPOUND, sizeof(Point)), &H5Tclose);
    REQUIRE(point.valid());
    H5Tinsert(point.get(), "x", HOFFSET(Point, x), H5T_NATIVE_DOUBLE);
    H5Tinsert(point.get(), "y", HOFFSET(Point, y), H5T_NATIVE_DOUBLE);

    const hsize_t two = 2;
    h5core::Handle trail(H5Tarray_create2(point.get(), 1, &two), &H5Tclose);
    REQUIRE(trail.valid());

    h5core::Handle holder(H5Tcreate(H5T_COMPOUND, sizeof(Holder)), &H5Tclose);
    REQUIRE(holder.valid());
    H5Tinsert(holder.get(), "trail", HOFFSET(Holder, trail), trail.get());
    H5Tinsert(holder.get(), "count", HOFFSET(Holder, count), H5T_NATIVE_INT32);

    const Holder value{{{1.0, 2.0}, {3.0, 4.0}}, 7};

    SECTION("a scalar is one line, whatever it is a member of")
    {
        CHECK(h5core::toJson(H5T_NATIVE_DOUBLE, &value.trail[0].x) == "1");
        CHECK(h5core::toJson(H5T_NATIVE_INT32, &value.count) == "7");
    }

    SECTION("a struct opens out, one member to a line")
    {
        CHECK(h5core::toJson(point.get(), &value.trail[0])
              == "{\n  \"x\": 1,\n  \"y\": 2\n}");
    }

    SECTION("a list of structs opens out too, indented under its own name")
    {
        // Each element on its own line, and each element's members indented
        // under that -- so the whole of it reads as a shape rather than as one
        // very long line the pane has to wrap.
        CHECK(h5core::toJson(holder.get(), &value)
              == "{\n"
                 "  \"trail\": [\n"
                 "    {\n      \"x\": 1,\n      \"y\": 2\n    },\n"
                 "    {\n      \"x\": 3,\n      \"y\": 4\n    }\n"
                 "  ],\n"
                 "  \"count\": 7\n"
                 "}");
    }

    SECTION("a list of numbers stays on the line its name is on")
    {
        // The other half of the same rule. Four samples on four lines is a
        // worse reading of four samples than four samples on one, and an array
        // member of a hundred would be a hundred lines of nothing.
        const hsize_t four = 4;
        h5core::Handle samples(H5Tarray_create2(H5T_NATIVE_DOUBLE, 1, &four),
                               &H5Tclose);
        REQUIRE(samples.valid());
        const double values[4] = {0.0, 0.25, 0.5, 0.75};
        CHECK(h5core::toJson(samples.get(), values) == "[0, 0.25, 0.5, 0.75]");
    }

    SECTION("nothing in it is nothing to open out")
    {
        // An empty list is two characters, not two lines with a blank between
        // them -- and an empty list is what every fourth record of the example
        // file's `tags` holds, so this is the common case rather than an edge.
        h5core::Handle list(H5Tvlen_create(H5T_NATIVE_INT32), &H5Tclose);
        REQUIRE(list.valid());
        const hvl_t none{0, nullptr};
        CHECK(h5core::toJson(list.get(), &none) == "[]");
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

// ---------------------------------------------------------------------------
// Attributes the Image specification says are scalars, and are not
// ---------------------------------------------------------------------------

namespace {

/// A file holding one dataset whose CLASS and IMAGE_SUBCLASS are *arrays* of
/// strings rather than the scalars the spec calls for.
///
/// This is not a shape any writer intends, and it is a shape a file is allowed
/// to be in -- which is the point. H5Aread has no partial form: it fills the
/// buffer with every element the attribute has, so a reader that sized the
/// buffer for the one element it wanted wrote past the end of it by the rest,
/// on nothing worse than a malformed file. The assertions below are that the
/// first element still decides, which is what the attribute could only have
/// meant; the value of the case is that it is read at all.
void writeArrayValuedImageTags(const std::string& path, bool variableLength)
{
    const hid_t file =
        H5Fcreate(path.c_str(), H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
    REQUIRE(file >= 0);

    const std::vector<hsize_t> dims{4, 4};
    const std::vector<std::uint8_t> pixels(16, 0);
    const hid_t space = H5Screate_simple(2, dims.data(), nullptr);
    const hid_t dataset = H5Dcreate2(file, "img", H5T_NATIVE_UINT8, space, H5P_DEFAULT,
                                     H5P_DEFAULT, H5P_DEFAULT);
    REQUIRE(dataset >= 0);
    REQUIRE(H5Dwrite(dataset, H5T_NATIVE_UINT8, H5S_ALL, H5S_ALL, H5P_DEFAULT,
                     pixels.data())
            >= 0);

    // Three elements where the spec says one. Both string flavours, because
    // they overrun differently: a variable-length element is a pointer and a
    // fixed-length one is its own bytes.
    const auto tag = [&](const char* name, const char* first, const char* rest) {
        const hid_t type = H5Tcopy(H5T_C_S1);
        if (variableLength) {
            REQUIRE(H5Tset_size(type, H5T_VARIABLE) >= 0);
        } else {
            REQUIRE(H5Tset_size(type, 32) >= 0);
        }
        const hsize_t count = 3;
        const hid_t attributeSpace = H5Screate_simple(1, &count, nullptr);
        const hid_t attribute = H5Acreate2(dataset, name, type, attributeSpace,
                                           H5P_DEFAULT, H5P_DEFAULT);
        REQUIRE(attribute >= 0);
        if (variableLength) {
            const char* values[3] = {first, rest, rest};
            REQUIRE(H5Awrite(attribute, type, values) >= 0);
        } else {
            std::vector<char> values(3 * 32, '\0');
            const auto put = [&](std::size_t slot, const char* text) {
                const std::size_t length = std::min<std::size_t>(std::strlen(text), 31);
                std::memcpy(values.data() + slot * 32, text, length);
            };
            put(0, first);
            put(1, rest);
            put(2, rest);
            REQUIRE(H5Awrite(attribute, type, values.data()) >= 0);
        }
        H5Aclose(attribute);
        H5Sclose(attributeSpace);
        H5Tclose(type);
    };

    tag("CLASS", "IMAGE", "IMAGE");
    tag("IMAGE_SUBCLASS", "IMAGE_GRAYSCALE", "IMAGE_TRUECOLOR");

    H5Dclose(dataset);
    H5Sclose(space);
    H5Fclose(file);
}

} // namespace

TEST_CASE("an image tag written as an array is read, not overrun",
          "[h5core][image]")
{
    const bool variableLength = GENERATE(true, false);
    h5test::TempFile temp{"arraytags"};
    writeArrayValuedImageTags(temp.path(), variableLength);

    const h5core::File file(temp.path());

    SECTION("the outline reads the first element and stops there")
    {
        const auto outline = file.datasetOutline("/img");
        REQUIRE(outline.image);
        // The first element of IMAGE_SUBCLASS, not the last one written.
        REQUIRE(outline.subclass == h5core::ImageSubclass::Grayscale);
    }

    SECTION("so does the full description")
    {
        const h5core::Dataset dataset(file, "/img");
        REQUIRE(dataset.info().image.has_value());
        REQUIRE(dataset.info().image->subclass == h5core::ImageSubclass::Grayscale);
        // Rank 2 is the shape a single-channel subclass implies, so the tag is
        // honoured rather than merely reported.
        REQUIRE(dataset.info().image->shapeMatches);
    }
}

namespace {

/// A 4x4 grayscale image whose IMAGE_MINMAXRANGE is `low, high`.
void writeImageWithRange(const std::string& path, double low, double high)
{
    const hid_t file = H5Fcreate(path.c_str(), H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
    REQUIRE(file >= 0);

    const std::vector<hsize_t> dims{4, 4};
    const std::vector<std::uint8_t> pixels(16, 0);
    const hid_t space = H5Screate_simple(2, dims.data(), nullptr);
    const hid_t dataset =
        H5Dcreate2(file, "img", H5T_NATIVE_UINT8, space, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    REQUIRE(dataset >= 0);
    REQUIRE(H5Dwrite(dataset, H5T_NATIVE_UINT8, H5S_ALL, H5S_ALL, H5P_DEFAULT, pixels.data()) >= 0);

    const auto text = [&](const char* name, const char* value) {
        const hid_t type = H5Tcopy(H5T_C_S1);
        REQUIRE(H5Tset_size(type, std::strlen(value) + 1) >= 0);
        const hid_t scalar = H5Screate(H5S_SCALAR);
        const hid_t attribute = H5Acreate2(dataset, name, type, scalar, H5P_DEFAULT, H5P_DEFAULT);
        REQUIRE(attribute >= 0);
        REQUIRE(H5Awrite(attribute, type, value) >= 0);
        H5Aclose(attribute);
        H5Sclose(scalar);
        H5Tclose(type);
    };
    text("CLASS", "IMAGE");
    text("IMAGE_SUBCLASS", "IMAGE_GRAYSCALE");

    const hsize_t two = 2;
    const double range[2] = {low, high};
    const hid_t rangeSpace = H5Screate_simple(1, &two, nullptr);
    const hid_t attribute = H5Acreate2(dataset, "IMAGE_MINMAXRANGE", H5T_NATIVE_DOUBLE, rangeSpace,
                                       H5P_DEFAULT, H5P_DEFAULT);
    REQUIRE(attribute >= 0);
    REQUIRE(H5Awrite(attribute, H5T_NATIVE_DOUBLE, range) >= 0);
    H5Aclose(attribute);
    H5Sclose(rangeSpace);

    H5Dclose(dataset);
    H5Sclose(space);
    H5Fclose(file);
}

} // namespace

TEST_CASE("an image range that reaches an infinity is not a range", "[h5core][image]")
{
    // `-inf < inf` is true, so ordering alone let this through, and a span
    // between the two puts every pixel at inf / inf -- a NaN the colour ramp
    // then used as an index.
    constexpr double inf = std::numeric_limits<double>::infinity();
    const auto [low, high] = GENERATE_COPY(std::pair{-inf, inf}, std::pair{-inf, 0.0},
                                           std::pair{0.0, inf}, std::pair{0.0, std::nan("")});
    h5test::TempFile temp{"infrange"};
    writeImageWithRange(temp.path(), low, high);

    const h5core::File file(temp.path());
    const h5core::Dataset dataset(file, "/img");
    REQUIRE(dataset.info().image.has_value());
    CHECK_FALSE(dataset.info().image->minimum.has_value());
    CHECK_FALSE(dataset.info().image->maximum.has_value());
}

TEST_CASE("a finite image range is still the range", "[h5core][image]")
{
    h5test::TempFile temp{"finiterange"};
    writeImageWithRange(temp.path(), -2.5, 300.0);

    const h5core::File file(temp.path());
    const h5core::Dataset dataset(file, "/img");
    REQUIRE(dataset.info().image.has_value());
    CHECK(dataset.info().image->minimum == std::optional<double>{-2.5});
    CHECK(dataset.info().image->maximum == std::optional<double>{300.0});
}

// ---------------------------------------------------------------------------
// Attributes that hold nothing
// ---------------------------------------------------------------------------

namespace {

/// One group carrying three attributes whose dataspace is H5S_NULL -- one
/// numeric, one fixed-length string, one variable-length string -- and one
/// ordinary scalar beside them, so the listing is known to have gone on past
/// the empty ones.
void writeNullAttributes(const std::string& path)
{
    const hid_t file = H5Fcreate(path.c_str(), H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
    REQUIRE(file >= 0);
    const hid_t group = H5Gcreate2(file, "holder", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    REQUIRE(group >= 0);

    const hid_t nothing = H5Screate(H5S_NULL);
    REQUIRE(nothing >= 0);
    const auto empty = [&](const char* name, hid_t type) {
        const hid_t attribute = H5Acreate2(group, name, type, nothing, H5P_DEFAULT, H5P_DEFAULT);
        REQUIRE(attribute >= 0);
        H5Aclose(attribute);
    };

    empty("a_number", H5T_NATIVE_INT32);
    const hid_t fixed = H5Tcopy(H5T_C_S1);
    REQUIRE(H5Tset_size(fixed, 8) >= 0);
    empty("b_fixed", fixed);
    const hid_t variable = H5Tcopy(H5T_C_S1);
    REQUIRE(H5Tset_size(variable, H5T_VARIABLE) >= 0);
    empty("c_variable", variable);

    const hid_t scalar = H5Screate(H5S_SCALAR);
    const hid_t present =
        H5Acreate2(group, "d_present", H5T_NATIVE_INT32, scalar, H5P_DEFAULT, H5P_DEFAULT);
    REQUIRE(present >= 0);
    const std::int32_t seven = 7;
    REQUIRE(H5Awrite(present, H5T_NATIVE_INT32, &seven) >= 0);

    H5Aclose(present);
    H5Sclose(scalar);
    H5Tclose(variable);
    H5Tclose(fixed);
    H5Sclose(nothing);
    H5Gclose(group);
    H5Fclose(file);
}

} // namespace

TEST_CASE("an attribute with a null dataspace reads as nothing, not as a zero",
          "[h5core][attribute]")
{
    // A null dataspace has rank 0, as a scalar does, and the product of an
    // empty shape is one. Counting the elements that way read one element out
    // of an attribute that holds none -- into a zeroed buffer -- and printed
    // the 0 it found there as though the file had said it.
    h5test::TempFile temp{"nullattrs"};
    writeNullAttributes(temp.path());

    const h5core::File file(temp.path());
    const auto attrs = h5core::readAttributes(file, "/holder");
    REQUIRE(attrs.size() == 4);

    CHECK(attrs[0].name == "a_number");
    CHECK(attrs[0].value == "[]");
    CHECK(attrs[0].type.cls == h5core::TypeClass::Integer);
    CHECK(attrs[1].name == "b_fixed");
    CHECK(attrs[1].value == "[]");
    CHECK(attrs[2].name == "c_variable");
    CHECK(attrs[2].value == "[]");

    // ...and the one that does hold something is still read.
    CHECK(attrs[3].name == "d_present");
    CHECK(attrs[3].value == "7");
}

TEST_CASE("a buffer is sized by a product that cannot wrap", "[h5core][memory]")
{
    CHECK(h5core::bufferBytes(0, 8) == std::optional<std::size_t>{0});
    CHECK(h5core::bufferBytes(1000, 0) == std::optional<std::size_t>{0});
    CHECK(h5core::bufferBytes(1000, 8) == std::optional<std::size_t>{8000});

    // A dataspace a file may state and no machine may hold: two to the
    // sixty-first elements of eight bytes is two to the sixty-fourth, which
    // wraps to nothing at all -- a zero-byte buffer for H5Aread to fill.
    const hsize_t huge = hsize_t{1} << 61;
    CHECK_FALSE(h5core::bufferBytes(huge, 8).has_value());
    CHECK(h5core::bufferBytes(huge, 4) == std::optional<std::size_t>{std::size_t{1} << 63});
}

TEST_CASE("an element count saturates rather than wrapping", "[h5core][memory]")
{
    CHECK(h5core::elementCount({}) == 1);
    CHECK(h5core::elementCount({0}) == 0);
    CHECK(h5core::elementCount({2, 3, 4}) == 24);

    // Eight thousand along each of five axes is 2^65, which wraps to zero:
    // exactly what HDF5's own count of such a dataspace reports.
    const std::vector<hsize_t> five(5, 8192);
    CHECK(h5core::elementCount(five) == h5core::kCountSaturated);

    // One that wraps to something small is the dangerous one: a buffer sized
    // for 2^24 elements and a selection of 2^64 + 2^24 of them.
    CHECK(h5core::elementCount({(hsize_t{1} << 40) + 1, hsize_t{1} << 24}) ==
          h5core::kCountSaturated);

    // An empty axis anywhere is nothing, even after the rest has saturated.
    CHECK(h5core::elementCount({hsize_t{1} << 40, hsize_t{1} << 40, 0}) == 0);
}

namespace {

/// A chunked dataset whose extents multiply past 2^64 to a small number.
/// Chunked and never written, so the file is a few kilobytes: HDF5 allocates
/// nothing for chunks nobody wrote, and counts the dataspace without looking.
void writeWrappingDataset(const std::string& path)
{
    const hid_t file = H5Fcreate(path.c_str(), H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
    REQUIRE(file >= 0);
    const hsize_t dims[2] = {(hsize_t{1} << 40) + 1, hsize_t{1} << 24};
    const hsize_t chunk[2] = {1, 1024};
    const hid_t space = H5Screate_simple(2, dims, nullptr);
    REQUIRE(space >= 0);
    const hid_t create = H5Pcreate(H5P_DATASET_CREATE);
    REQUIRE(H5Pset_chunk(create, 2, chunk) >= 0);
    const hid_t dataset =
        H5Dcreate2(file, "wraps", H5T_NATIVE_DOUBLE, space, H5P_DEFAULT, create, H5P_DEFAULT);
    REQUIRE(dataset >= 0);
    H5Dclose(dataset);
    H5Pclose(create);
    H5Sclose(space);
    H5Fclose(file);
}

} // namespace

TEST_CASE("a dataset whose extents wrap a 64-bit count is refused, not read", "[h5core][memory]")
{
    h5test::TempFile temp{"wraps"};
    writeWrappingDataset(temp.path());

    const h5core::File file(temp.path());
    const h5core::Dataset dataset(file, "/wraps");
    CHECK(dataset.info().elementCount() == h5core::kCountSaturated);

    // The whole of it, which is what a full read and a pipeline ask for.
    const std::vector<hsize_t> origin{0, 0};
    CHECK_THROWS_AS(dataset.readAll(1u << 24), h5core::H5Error);
    CHECK_THROWS_AS(dataset.readNumericWindow(origin, dataset.info().shape), h5core::H5Error);
    CHECK_THROWS_AS(dataset.readWindow(origin, dataset.info().shape), h5core::H5Error);

    // ...while a window of it is still an ordinary read, of fill values.
    const h5core::NumericWindow window = dataset.readNumericWindow(origin, {2, 3});
    CHECK(window.values == std::vector<double>(6, 0.0));
}

namespace {

/// A dataset of four new-style object references, each to the root group.
void writeReferences(const std::string& path)
{
    const hid_t file = H5Fcreate(path.c_str(), H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
    REQUIRE(file >= 0);
    H5R_ref_t refs[4];
    for (H5R_ref_t& ref : refs) {
        REQUIRE(H5Rcreate_object(file, "/", H5P_DEFAULT, &ref) >= 0);
    }
    const hsize_t four = 4;
    const hid_t space = H5Screate_simple(1, &four, nullptr);
    const hid_t dataset =
        H5Dcreate2(file, "refs", H5T_STD_REF, space, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    REQUIRE(dataset >= 0);
    REQUIRE(H5Dwrite(dataset, H5T_STD_REF, H5S_ALL, H5S_ALL, H5P_DEFAULT, refs) >= 0);
    for (H5R_ref_t& ref : refs) {
        H5Rdestroy(&ref);
    }
    H5Dclose(dataset);
    H5Sclose(space);
    H5Fclose(file);
}

} // namespace

TEST_CASE("a read of references hands them back", "[h5core][memory]")
{
    // A reference read into memory is an H5R_ref_t, which HDF5 allocates and
    // which holds a count on the file it names. H5Treclaim releases both, and
    // the guard only asked for it when the type held a string or a vlen.
    h5test::TempFile temp{"refs"};
    writeReferences(temp.path());

    const h5core::File file(temp.path());
    const h5core::Dataset dataset(file, "/refs");
    REQUIRE(dataset.info().type.cls == h5core::TypeClass::Reference);

    const int before = H5Iget_ref(file.id());
    for (int i = 0; i < 3; ++i) {
        const h5core::DataWindow window = dataset.readWindow({0}, {4});
        REQUIRE(window.cells.size() == 4);
        (void)dataset.readElement({2});
    }
    CHECK(H5Iget_ref(file.id()) == before);
}

TEST_CASE("a value is read wherever it sits, aligned or not", "[h5core][format]")
{
    // A member of a packed compound, or the nth element of an array of odd
    // width, sits at whatever byte the layout puts it on. Each of these is
    // written one byte past an aligned address, which is a misaligned load
    // for every width but one if the formatter dereferences rather than copies.
    alignas(16) unsigned char bytes[32] = {};
    unsigned char* at = bytes + 1;

    const std::int64_t large = -1234567890123LL;
    std::memcpy(at, &large, sizeof(large));
    CHECK(h5core::formatElement(H5T_NATIVE_INT64, at) == "-1234567890123");

    const std::uint32_t word = 4000000000U;
    std::memcpy(at, &word, sizeof(word));
    CHECK(h5core::formatElement(H5T_NATIVE_UINT32, at) == "4000000000");

    const double value = 0.25;
    std::memcpy(at, &value, sizeof(value));
    CHECK(h5core::formatElement(H5T_NATIVE_DOUBLE, at) == "0.25");
    CHECK(h5core::toJson(H5T_NATIVE_DOUBLE, at) == "0.25");

    const float single = -1.5F;
    std::memcpy(at, &single, sizeof(single));
    CHECK(h5core::formatElement(H5T_NATIVE_FLOAT, at) == "-1.5");
}
