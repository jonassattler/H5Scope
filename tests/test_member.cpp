// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

// The member chain grammar and what it resolves to.
//
// Not one line of this opens a file. A chain is resolved against a TypeInfo,
// which is plain data, so the rules can be stated against a datatype built here
// by hand -- which is also the clearest way to write them down. What a chain
// then *reads* is tests/test_example.cpp's question, against a real file.

#include "postproc/MemberPath.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <string>
#include <vector>

using Catch::Matchers::ContainsSubstring;
using h5core::TypeClass;
using h5core::TypeInfo;
using h5core::TypeMember;

namespace {

TypeInfo scalar(TypeClass cls, const std::string& description, std::size_t size)
{
    TypeInfo info;
    info.cls = cls;
    info.description = description;
    info.size = size;
    return info;
}

TypeInfo arrayOf(const TypeInfo& base, std::vector<hsize_t> dims)
{
    TypeInfo info;
    info.cls = TypeClass::Array;
    info.description = "array of " + base.description;
    info.arrayDims = std::move(dims);
    info.base = std::make_shared<const TypeInfo>(base);
    return info;
}

TypeInfo vlenOf(const TypeInfo& base)
{
    TypeInfo info;
    info.cls = TypeClass::VarLen;
    info.description = "vlen of " + base.description;
    info.isVariableLength = true;
    info.base = std::make_shared<const TypeInfo>(base);
    return info;
}

TypeInfo compound(std::vector<TypeMember> members)
{
    TypeInfo info;
    info.cls = TypeClass::Compound;
    info.description = "compound";
    for (const TypeMember& member : members) {
        info.memberNames.push_back(member.name);
    }
    info.members = std::move(members);
    return info;
}

const TypeInfo kDouble = scalar(TypeClass::Float, "float64", 8);
const TypeInfo kInt = scalar(TypeClass::Integer, "int32", 4);
const TypeInfo kText = scalar(TypeClass::String, "string (8 bytes)", 8);

/// The shape of a real event table: a nested compound, an array member, a
/// string, and a ragged one.
TypeInfo eventType()
{
    return compound({
        TypeMember{"time", kDouble, 0},
        TypeMember{"station", kText, 8},
        TypeMember{"position",
                   compound({TypeMember{"x", kDouble, 0}, TypeMember{"y", kDouble, 8},
                             TypeMember{"z", kDouble, 16}}),
                   16},
        TypeMember{"samples", arrayOf(kDouble, {4}), 40},
        TypeMember{"tags", vlenOf(kInt), 72},
    });
}

postproc::MemberChain read(const QString& text)
{
    return postproc::resolveMemberChain(text, eventType());
}

} // namespace

TEST_CASE("a member chain is taken apart before it is resolved", "[member][parse]")
{
    std::vector<postproc::MemberStep> chain;
    QString error;

    SECTION("nothing written is no chain, and not a mistake")
    {
        REQUIRE(postproc::parseMemberChain(QString{}, chain, error));
        CHECK(chain.empty());
        CHECK(error.isEmpty());
    }

    SECTION("a chain of names")
    {
        REQUIRE(postproc::parseMemberChain(".position.x", chain, error));
        REQUIRE(chain.size() == 2);
        CHECK(chain[0].name == "position");
        CHECK(chain[1].name == "x");
        CHECK_FALSE(chain[1].bracketed);
    }

    SECTION("a subscript is kept with the member that carries it")
    {
        REQUIRE(postproc::parseMemberChain(".samples[1:3]", chain, error));
        REQUIRE(chain.size() == 1);
        CHECK(chain[0].name == "samples");
        CHECK(chain[0].subscript == "1:3");
        CHECK(chain[0].bracketed);
    }

    SECTION("an empty subscript is not the same as no subscript")
    {
        // `.b[]` says something about b's axes and says nothing; `.b` says
        // nothing about them at all. Only one of the two is a mistake, and the
        // parser has to keep them apart for the resolver to say which.
        REQUIRE(postproc::parseMemberChain(".samples[]", chain, error));
        REQUIRE(chain.size() == 1);
        CHECK(chain[0].bracketed);
        CHECK(chain[0].subscript.isEmpty());
    }

    SECTION("a subscript may hold its own brackets, as a scattered one does")
    {
        REQUIRE(postproc::parseMemberChain(".samples[[0,2]]", chain, error));
        REQUIRE(chain.size() == 1);
        CHECK(chain[0].subscript == "[0,2]");
    }

    SECTION("what is not a chain says so")
    {
        CHECK_FALSE(postproc::parseMemberChain("samples", chain, error));
        CHECK_THAT(error.toStdString(), ContainsSubstring("'.'"));

        CHECK_FALSE(postproc::parseMemberChain(".", chain, error));
        CHECK_THAT(error.toStdString(), ContainsSubstring("no member name"));

        CHECK_FALSE(postproc::parseMemberChain(".samples[0", chain, error));
        CHECK_THAT(error.toStdString(), ContainsSubstring("matching ']'"));
    }
}

TEST_CASE("a member chain resolves to the axes it appends", "[member][resolve]")
{
    SECTION("a scalar member appends nothing")
    {
        const auto chain = read(".time");
        REQUIRE(chain.valid());
        CHECK(chain.selection.dims.empty());
        CHECK(chain.selection.type.cls == TypeClass::Float);
        CHECK(chain.selection.text == ".time");
        CHECK(chain.folded.isEmpty());
    }

    SECTION("a chain goes through a compound member")
    {
        const auto chain = read(".position.y");
        REQUIRE(chain.valid());
        REQUIRE(chain.selection.links.size() == 2);
        CHECK(chain.selection.links[0].name == "position");
        CHECK(chain.selection.links[1].index == 1);
        CHECK(chain.selection.dims.empty());
        CHECK(chain.selection.type.description == "float64");
    }

    SECTION("an array member appends its dimension and names it")
    {
        const auto chain = read(".samples");
        REQUIRE(chain.valid());
        REQUIRE(chain.selection.dims == std::vector<hsize_t>{4});
        // What one element of the result is, with the axis taken off it.
        CHECK(chain.selection.type.description == "float64");
        CHECK(chain.dimNames == QStringList{"samples"});
        // No subscript was written, so the axis is whole.
        CHECK(chain.folded == QStringList{QString{}});
    }

    SECTION("a string member resolves and is simply not a number")
    {
        const auto chain = read(".station");
        REQUIRE(chain.valid());
        CHECK(chain.selection.type.cls == TypeClass::String);
        CHECK_FALSE(h5core::isNumeric(chain.selection.type.cls));
    }
}

TEST_CASE("a member subscript folds onto the slice line", "[member][resolve]")
{
    // This is the identity the notation rests on, as arithmetic:
    //
    //     array[i1,i2,i3].b[i4]  ==  (array[:,:,:].b)[i1,i2,i3,i4]
    //
    // The chain resolves to the axes `.b` appends, and its subscript comes back
    // in `folded` to be written onto the slice line over those axes. There is
    // no second rule for a member subscript, which is exactly why the two
    // spellings cannot drift apart.

    SECTION("an index on a member is an index on the axis it appended")
    {
        const auto chain = read(".samples[2]");
        REQUIRE(chain.valid());
        REQUIRE(chain.selection.dims == std::vector<hsize_t>{4});
        REQUIRE(chain.folded == QStringList{"2"});
        // The chain itself carries no subscript: it names a member, and the
        // subscript went where every other subscript lives.
        CHECK(chain.selection.links.back().vlenIndex == std::nullopt);
    }

    SECTION("a range on a member folds as the range it was written as")
    {
        // `1:3` and `1` select a different number of elements and, written as
        // Python writes them, a different rank. The folded text is what was
        // written rather than what it resolved to, for the slice line's own
        // reason: a box that rewrites a subscript is a box that argues.
        const auto chain = read(".samples[1:3]");
        REQUIRE(chain.valid());
        CHECK(chain.folded == QStringList{"1:3"});
    }

    SECTION("a member with no axes has nothing to subscript, and says so")
    {
        const auto chain = read(".time[0]");
        CHECK_FALSE(chain.valid());
        CHECK_THAT(chain.error.toStdString(), ContainsSubstring("no dimensions"));
    }

    SECTION("a subscript that does not read is reported against its member")
    {
        const auto chain = read(".samples[9]");
        CHECK_FALSE(chain.valid());
        CHECK_THAT(chain.error.toStdString(), ContainsSubstring(".samples"));
    }
}

TEST_CASE("a ragged member is indexable and not sliceable", "[member][vlen]")
{
    // A vlen has a different length in every element, and every view in this
    // program is a rectangle. That is the whole of the rule: one index has a
    // shape, a range of one does not.

    SECTION("selected on its own it keeps the shape and stays a list")
    {
        const auto chain = read(".tags");
        REQUIRE(chain.valid());
        CHECK(chain.selection.dims.empty());
        CHECK(chain.selection.type.cls == TypeClass::VarLen);
        // Which is not a number, so it prints in the grid and does not plot.
        CHECK_FALSE(h5core::isNumeric(chain.selection.type.cls));
    }

    SECTION("one index of it is a number, and the shape is still the dataset's")
    {
        const auto chain = read(".tags[3]");
        REQUIRE(chain.valid());
        CHECK(chain.selection.dims.empty());
        CHECK(chain.folded.isEmpty());
        CHECK(chain.selection.type.cls == TypeClass::Integer);
        // The index stays on the chain: it is the one subscript with nowhere
        // else to go, because a vlen appends no axis for it to fold onto.
        REQUIRE(chain.selection.links.back().vlenIndex.has_value());
        CHECK(*chain.selection.links.back().vlenIndex == 3);
    }

    SECTION("a range of it is refused, and the refusal says what to do instead")
    {
        const auto chain = read(".tags[0:2]");
        CHECK_FALSE(chain.valid());
        CHECK_THAT(chain.error.toStdString(), ContainsSubstring("no single shape"));
        CHECK_THAT(chain.error.toStdString(), ContainsSubstring(".tags[0]"));
    }

    SECTION("a chain cannot go on through one")
    {
        const auto chain = read(".tags.x");
        CHECK_FALSE(chain.valid());
        CHECK_THAT(chain.error.toStdString(), ContainsSubstring("vlen"));
    }
}

TEST_CASE("a chain that does not apply says what the type does have",
          "[member][resolve]")
{
    SECTION("a misspelt member is answered with the list")
    {
        const auto chain = read(".enrgy");
        CHECK_FALSE(chain.valid());
        CHECK_THAT(chain.error.toStdString(), ContainsSubstring("no member 'enrgy'"));
        // The names are in the file and nowhere else, and this is the moment
        // they are being asked for.
        CHECK_THAT(chain.error.toStdString(), ContainsSubstring("time"));
        CHECK_THAT(chain.error.toStdString(), ContainsSubstring("samples"));
    }

    SECTION("a chain into something with no members says which")
    {
        const auto chain = read(".time.x");
        CHECK_FALSE(chain.valid());
        CHECK_THAT(chain.error.toStdString(), ContainsSubstring("no members"));
        CHECK_THAT(chain.error.toStdString(), ContainsSubstring(".time"));
    }

    SECTION("a chain on a dataset that is not a compound at all")
    {
        const auto chain = postproc::resolveMemberChain(".anything", kDouble);
        CHECK_FALSE(chain.valid());
        CHECK_THAT(chain.error.toStdString(), ContainsSubstring("this dataset"));
    }
}

TEST_CASE("a type lists the chains it offers", "[member][chains]")
{
    // What the pipeline's select row is chosen from, and what a completer will
    // offer. Arithmetic over the type, so it costs no read -- which is the
    // whole reason it can be built the moment a dataset is selected.
    const QStringList chains = postproc::memberChains(eventType());

    SECTION("in file order, depth first")
    {
        CHECK(chains
              == QStringList{".time", ".station", ".position", ".position.x",
                             ".position.y", ".position.z", ".samples", ".tags"});
    }

    SECTION("an intermediate compound is offered as well as its members")
    {
        // Selecting `.position` is a selection like any other -- the table
        // shows three numbers per row -- and a reader looking for `x` finds it
        // under the name the file gave it rather than having to know it is
        // there.
        CHECK(chains.contains(QStringLiteral(".position")));
        CHECK(chains.contains(QStringLiteral(".position.x")));
    }

    SECTION("an array is a leaf: its dimensions are axes, not names")
    {
        // `.samples[2]` is the same selection as `.samples` with a 2 on the
        // slice line, so the list holds the name and the line holds the
        // subscript. Nothing here ends in a bracket.
        for (const QString& chain : chains) {
            INFO(chain.toStdString());
            CHECK_FALSE(chain.contains(QLatin1Char('[')));
        }
    }

    SECTION("a chain stops at a vlen, because a chain cannot go through one")
    {
        const TypeInfo ragged = compound({
            TypeMember{"lists",
                       vlenOf(compound({TypeMember{"a", kDouble, 0}})), 0},
        });
        CHECK(postproc::memberChains(ragged) == QStringList{".lists"});
    }

    SECTION("a dataset of an array of structs is entered, as resolving enters it")
    {
        // The same unwrapping resolveMemberChain does. If the two disagreed the
        // list would offer a chain the resolver then refused, which is worse
        // than offering nothing.
        const TypeInfo grid =
            arrayOf(compound({TypeMember{"a", kDouble, 0}}), {2, 3});
        CHECK(postproc::memberChains(grid) == QStringList{".a"});
        CHECK(postproc::resolveMemberChain(".a", grid).valid());
    }

    SECTION("nothing to select from is an empty list, not a refusal")
    {
        CHECK(postproc::memberChains(kDouble).isEmpty());
    }

    SECTION("a compound of thousands stops rather than filling a dropdown")
    {
        std::vector<TypeMember> many;
        for (int i = 0; i < 500; ++i) {
            many.push_back(TypeMember{"m" + std::to_string(i), kDouble,
                                      static_cast<std::size_t>(i) * 8});
        }
        const TypeInfo wide = compound(std::move(many));
        CHECK(postproc::memberChains(wide).size() == postproc::kMaxMemberChains);
        CHECK(postproc::memberChains(wide, 3).size() == 3);
    }
}
