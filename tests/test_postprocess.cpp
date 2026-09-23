// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "data/NumpyGolden.hpp"

#include "postproc/Array.hpp"
#include "postproc/Operations.hpp"
#include "postproc/Pipeline.hpp"
#include "postproc/Script.hpp"
#include "postproc/Subscripts.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <QElapsedTimer>

#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using Catch::Matchers::ContainsSubstring;
using postproc::Array;
using postproc::OperationKind;
using postproc::Step;

namespace {

/// Equal as a golden comparison has to mean it: NaN matches NaN, because a NaN
/// coming out of a reduction is the answer rather than the absence of one, and
/// the signed zeros are distinguished, because `abs(-0.0)` is the one case in
/// this whole suite where the sign bit is the entire result.
bool same(double left, double right)
{
    if (std::isnan(left) || std::isnan(right)) {
        return std::isnan(left) && std::isnan(right);
    }
    if (left == 0.0 && right == 0.0) {
        return std::signbit(left) == std::signbit(right);
    }
    return left == right;
}

Step stepFor(const std::string& operation, const std::string& argument)
{
    const auto kind = postproc::operationNamed(QString::fromStdString(operation));
    REQUIRE(kind.has_value());
    return Step{*kind, QString::fromStdString(argument)};
}

std::string describe(const std::vector<hsize_t>& shape)
{
    return postproc::describeShape(shape).toStdString();
}

/// An array of `shape` counting from zero, which is enough to tell any
/// rearrangement from any other.
Array counting(const std::vector<hsize_t>& shape)
{
    const auto total = static_cast<std::size_t>(postproc::elementCount(shape));
    std::vector<double> values(total);
    for (std::size_t i = 0; i < total; ++i) {
        values[i] = static_cast<double>(i);
    }
    return Array(shape, std::move(values));
}

/// A source that claims a shape without holding one, and records whether
/// anybody read it. The cap is a promise *not* to read, so the way to test it
/// is to make reading observable rather than to build the array it refuses.
class HugeSource : public h5core::DataSource
{
public:
    explicit HugeSource(std::vector<hsize_t> shape)
    {
        info_.shape = std::move(shape);
        info_.maxShape = info_.shape;
        info_.type.cls = h5core::TypeClass::Float;
        info_.type.description = "float64";
        info_.type.size = sizeof(double);
    }

    [[nodiscard]] const h5core::DatasetInfo& info() const noexcept override
    {
        return info_;
    }
    [[nodiscard]] const std::string& path() const noexcept override { return path_; }

    [[nodiscard]] h5core::DataWindow readWindow(const std::vector<hsize_t>&,
                                                const std::vector<hsize_t>&) const override
    {
        reads = true;
        return {};
    }
    [[nodiscard]] h5core::NumericWindow
    readNumericWindow(const std::vector<hsize_t>& offset,
                      const std::vector<hsize_t>& count) const override
    {
        reads = true;
        // Zeros, but the right number of them: a selection under the cap has
        // to come back complete or the pipeline reports a short read instead
        // of the thing being tested.
        h5core::NumericWindow window;
        window.offset = offset;
        window.count = count;
        window.values.assign(
            static_cast<std::size_t>(postproc::elementCount(count)), 0.0);
        return window;
    }
    [[nodiscard]] h5core::ElementValue
    readElement(const std::vector<hsize_t>&) const override
    {
        reads = true;
        return {};
    }

    mutable bool reads = false;

private:
    h5core::DatasetInfo info_;
    std::string path_ = "/large";
};

} // namespace

// ---------------------------------------------------------------------------
// The comparison against numpy
// ---------------------------------------------------------------------------

TEST_CASE("every operation answers what numpy answers", "[postproc][numpy]")
{
    // One case per assertion would be a thousand CTest entries and a thousand
    // lines of output; what matters is which ones disagreed, so the loop
    // collects the disagreements and the assertion is that there were none.
    std::vector<std::string> wrong;
    std::size_t checked = 0;
    std::size_t refusals = 0;

    for (const golden::Case& example : golden::cases()) {
        const Step step = stepFor(example.operation, example.argument);
        const Array input(example.shape, example.input);
        const postproc::ArrayResult result = postproc::apply(step, input);
        const postproc::ShapeResult predicted =
            postproc::shapeAfter(step, example.shape);

        // Divergence one, and the only one in the operations themselves: an
        // expression that selects nothing is an error here where numpy hands
        // back an empty array. That is this application's rule everywhere a
        // subscript is written -- an empty axis would blank the grid with no
        // explanation -- and it is stated in the README rather than hidden.
        const bool emptySelection = step.kind == OperationKind::Slice
                                    && !example.raised && example.output.empty();

        if (example.raised || emptySelection) {
            ++refusals;
            if (result.ok()) {
                wrong.push_back(example.name + ": ran, where numpy refused ("
                                + example.why + ")");
            }
            // Whatever refuses the elements must refuse the shape too, or the
            // panel would print a shape for a row that cannot produce one.
            if (predicted.ok() != result.ok()) {
                wrong.push_back(example.name
                                + ": the shape and the elements disagree about "
                                  "whether this can run");
            }
            continue;
        }

        ++checked;
        if (!result.ok()) {
            wrong.push_back(example.name + ": refused with '"
                            + result.error.toStdString() + "'");
            continue;
        }
        if (result.array.shape() != example.outShape) {
            wrong.push_back(example.name + ": shape " + describe(result.array.shape())
                            + ", numpy said " + describe(example.outShape));
            continue;
        }
        if (!predicted.ok() || predicted.shape != example.outShape) {
            wrong.push_back(example.name
                            + ": shapeAfter said " + describe(predicted.shape)
                            + " but the operation produced "
                            + describe(example.outShape));
            continue;
        }

        const std::vector<double> values = result.array.values();
        if (values.size() != example.output.size()) {
            wrong.push_back(example.name + ": "
                            + std::to_string(values.size()) + " elements, numpy "
                            + std::to_string(example.output.size()));
            continue;
        }
        for (std::size_t i = 0; i < values.size(); ++i) {
            if (!same(values[i], example.output[i])) {
                wrong.push_back(example.name + ": element "
                                + std::to_string(i) + " is "
                                + std::to_string(values[i]) + ", numpy said "
                                + std::to_string(example.output[i]));
                break;
            }
        }
    }

    INFO("checked " << checked << " answers and " << refusals << " refusals");
    if (!wrong.empty()) {
        std::string report =
            std::to_string(wrong.size()) + " disagreements with numpy:\n";
        for (std::size_t i = 0; i < wrong.size() && i < 20; ++i) {
            report += "  " + wrong[i] + "\n";
        }
        FAIL(report);
    }
    REQUIRE(checked > 100);
    REQUIRE(refusals > 100);
}

// ---------------------------------------------------------------------------
// The array engine
// ---------------------------------------------------------------------------

TEST_CASE("a shape is counted without overflowing", "[postproc][array]")
{
    REQUIRE(postproc::elementCount({}) == 1);
    REQUIRE(postproc::elementCount({0}) == 0);
    REQUIRE(postproc::elementCount({2, 3, 4}) == 24);
    REQUIRE(postproc::elementCount({7, 0, 9}) == 0);

    SECTION("a product too big for the count saturates rather than wrapping")
    {
        // Rank 12 of plausible extents is what /data/rank12 is, and a wrapped
        // product would report a selection of 10^19 elements as a small one
        // and then set about reading it.
        const std::vector<hsize_t> huge(12, 1u << 20u);
        REQUIRE(postproc::elementCount(huge)
                == std::numeric_limits<hsize_t>::max());
    }
}

TEST_CASE("an index list is a progression or it is not", "[postproc][array]")
{
    REQUIRE(postproc::asProgression({0, 1, 2, 3}).uniform);
    REQUIRE(postproc::asProgression({0, 2, 4}).step == 2);
    REQUIRE(postproc::asProgression({9, 6, 3}).step == -3);
    REQUIRE(postproc::asProgression({4}).uniform);
    REQUIRE_FALSE(postproc::asProgression({0, 2, 5}).uniform);
    REQUIRE_FALSE(postproc::asProgression({}).uniform);
}

TEST_CASE("rearranging never copies", "[postproc][array]")
{
    // The claim the whole strided form is here to make: slice, transpose and
    // reshape are arithmetic, and a chain of them touches no element. If this
    // fails the operations are still correct and a 128 MB selection has just
    // become several of them.
    const Array input = counting({4, 6});
    const void* buffer = input.buffer();

    const Array sliced =
        input.selected({{0, 1, 2, 3}, {1, 3, 5}}, {false, false});
    REQUIRE(sliced.buffer() == buffer);
    REQUIRE(sliced.shape() == std::vector<hsize_t>{4, 3});

    const Array turned = sliced.transposed({1, 0});
    REQUIRE(turned.buffer() == buffer);
    REQUIRE(turned.shape() == std::vector<hsize_t>{3, 4});

    SECTION("a bare index drops its dimension and still does not copy")
    {
        const Array pinned = input.selected({{2}, {0, 1, 2, 3, 4, 5}}, {true, false});
        REQUIRE(pinned.buffer() == buffer);
        REQUIRE(pinned.shape() == std::vector<hsize_t>{6});
        REQUIRE(pinned.values()
                == std::vector<double>{12.0, 13.0, 14.0, 15.0, 16.0, 17.0});
    }

    SECTION("a reshape of a contiguous array does not copy either")
    {
        const Array flat = input.reshaped({24});
        REQUIRE(flat.buffer() == buffer);
    }

    SECTION("but a reshape of a transposed one has to")
    {
        // numpy copies here too: the elements are not in the order the new
        // shape reads them in, so there is nothing to reinterpret.
        const Array flat = input.transposed({1, 0}).reshaped({24});
        REQUIRE(flat.buffer() != buffer);
        REQUIRE(flat.values().front() == 0.0);
        REQUIRE(flat.values()[1] == 6.0);
    }

    SECTION("a scattered selection has to gather")
    {
        const Array scattered =
            input.selected({{0, 1, 3}, {0, 2, 5}}, {false, false});
        REQUIRE(scattered.buffer() != buffer);
        REQUIRE(scattered.shape() == std::vector<hsize_t>{3, 3});
        REQUIRE(scattered.at({0, 0}) == 0.0);
        REQUIRE(scattered.at({2, 2}) == 23.0);
    }
}

TEST_CASE("a strided view reads in the order it was asked for", "[postproc][array]")
{
    const Array input = counting({3, 4});

    SECTION("a descent comes out backwards")
    {
        const Array reversed = input.selected({{2, 1, 0}, {0, 1, 2, 3}},
                                              {false, false});
        REQUIRE(reversed.values().front() == 8.0);
        REQUIRE(reversed.values().back() == 3.0);
        REQUIRE(reversed.buffer() == input.buffer());
    }

    SECTION("a transpose reads down the columns")
    {
        REQUIRE(input.transposed({1, 0}).values()
                == std::vector<double>{0, 4, 8, 1, 5, 9, 2, 6, 10, 3, 7, 11});
    }
}

TEST_CASE("a block of a strided view reads what walking it element by element "
          "would have",
          "[postproc][array]")
{
    // block() is the fast path under every computed window the views draw, and
    // at() is the definition it has to keep agreeing with. So the assertion is
    // that they agree -- over a transpose and a descent as well as over a plain
    // array, because those are where a rolling position can drift.
    const auto walked = [](const Array& array, const std::vector<hsize_t>& offset,
                           const std::vector<hsize_t>& count) {
        std::vector<double> out;
        std::vector<hsize_t> index(count.size(), 0);
        const hsize_t total = postproc::elementCount(count);
        for (hsize_t n = 0; n < total; ++n) {
            std::vector<hsize_t> at(count.size(), 0);
            for (std::size_t d = 0; d < count.size(); ++d) {
                at[d] = offset[d] + index[d];
            }
            out.push_back(array.at(at));
            for (std::size_t d = count.size(); d-- > 0;) {
                if (++index[d] < count[d]) {
                    break;
                }
                index[d] = 0;
            }
        }
        return out;
    };

    SECTION("the whole of a plain array")
    {
        const Array input = counting({3, 4});
        REQUIRE(input.block({0, 0}, {3, 4}) == input.values());
    }

    SECTION("a rectangle inside one")
    {
        const Array input = counting({3, 4});
        REQUIRE(input.block({1, 1}, {2, 2}) == std::vector<double>{5, 6, 9, 10});
        REQUIRE(input.block({1, 1}, {2, 2}) == walked(input, {1, 1}, {2, 2}));
    }

    SECTION("a rectangle of a transpose")
    {
        const Array input = counting({3, 4}).transposed({1, 0});
        REQUIRE(input.block({0, 0}, {4, 3}) == input.values());
        REQUIRE(input.block({1, 1}, {2, 2}) == walked(input, {1, 1}, {2, 2}));
    }

    SECTION("a rectangle of a descent, which runs backwards through the buffer")
    {
        const Array input =
            counting({3, 4}).selected({{2, 1, 0}, {3, 2, 1, 0}}, {false, false});
        REQUIRE(input.block({0, 0}, {3, 4}) == input.values());
        REQUIRE(input.block({1, 1}, {2, 3}) == walked(input, {1, 1}, {2, 3}));
    }

    SECTION("a scalar is its one value")
    {
        const Array scalar({}, {7.0});
        REQUIRE(scalar.block({}, {}) == std::vector<double>{7.0});
    }

    SECTION("a block of no extent is no values")
    {
        const Array input = counting({3, 4});
        REQUIRE(input.block({0, 0}, {0, 4}).empty());
        REQUIRE(input.block({1, 1}, {2, 0}).empty());
    }
}

// ---------------------------------------------------------------------------
// What each operation refuses, and why
// ---------------------------------------------------------------------------

TEST_CASE("an operation says what is wrong with its argument", "[postproc][ops]")
{
    const std::vector<hsize_t> cube{2, 3, 4};

    SECTION("a transpose names every axis or none of them")
    {
        REQUIRE(postproc::shapeAfter({OperationKind::Transpose, "0, 1"}, cube).error
                    .toStdString()
                != "");
        REQUIRE_THAT(
            postproc::shapeAfter({OperationKind::Transpose, "0, 1"}, cube)
                .error.toStdString(),
            ContainsSubstring("every axis or none"));
    }

    SECTION("a transpose cannot name one axis twice")
    {
        REQUIRE_THAT(postproc::shapeAfter({OperationKind::Transpose, "0, 1, 1"}, cube)
                         .error.toStdString(),
                     ContainsSubstring("twice"));
    }

    SECTION("an axis past the end says which array it was past the end of")
    {
        REQUIRE_THAT(
            postproc::shapeAfter({OperationKind::Max, "3"}, cube).error.toStdString(),
            ContainsSubstring("out of bounds"));
        REQUIRE_THAT(
            postproc::shapeAfter({OperationKind::Max, "3"}, cube).error.toStdString(),
            ContainsSubstring("rank 3"));
    }

    SECTION("a reduction over an axis of no elements has no answer")
    {
        // numpy's own refusal, and for the same reason: the minimum of no
        // numbers is not a number and an identity would be an infinity nobody
        // computed.
        REQUIRE_THAT(postproc::shapeAfter({OperationKind::Min, "0"}, {0, 3})
                         .error.toStdString(),
                     ContainsSubstring("nothing to reduce"));
        // ...but reducing the other way round is fine, and empty.
        const postproc::ShapeResult along =
            postproc::shapeAfter({OperationKind::Min, "1"}, {0, 3});
        REQUIRE(along.ok());
        REQUIRE(along.shape == std::vector<hsize_t>{0});
    }

    SECTION("only one dimension of a reshape can be left to work out")
    {
        REQUIRE_THAT(postproc::shapeAfter({OperationKind::Reshape, "-1, -1"}, cube)
                         .error.toStdString(),
                     ContainsSubstring("only one dimension"));
    }

    SECTION("a reshape that does not divide says so")
    {
        REQUIRE_THAT(postproc::shapeAfter({OperationKind::Reshape, "5"}, cube)
                         .error.toStdString(),
                     ContainsSubstring("cannot be reshaped"));
        REQUIRE_THAT(postproc::shapeAfter({OperationKind::Reshape, "-1, 5"}, cube)
                         .error.toStdString(),
                     ContainsSubstring("divide evenly"));
    }

    SECTION("a shape is written in any of the three ways it is usually written")
    {
        for (const char* written : {"2, 12", "(2, 12)", "[2,12]", "2 12"}) {
            const postproc::ShapeResult result =
                postproc::shapeAfter({OperationKind::Reshape, QString::fromLatin1(written)},
                                     cube);
            INFO(written);
            REQUIRE(result.ok());
            REQUIRE(result.shape == std::vector<hsize_t>{2, 12});
        }
    }

    SECTION("an empty argument is each operation's own default")
    {
        REQUIRE(postproc::shapeAfter({OperationKind::Transpose, ""}, cube).shape
                == std::vector<hsize_t>{4, 3, 2});
        REQUIRE(postproc::shapeAfter({OperationKind::Min, ""}, cube).shape.empty());
        REQUIRE(postproc::shapeAfter({OperationKind::Abs, ""}, cube).shape == cube);
        REQUIRE_FALSE(postproc::shapeAfter({OperationKind::Reshape, ""}, cube).ok());
    }
}

TEST_CASE("a slice step drops what an integer subscript names", "[postproc][ops]")
{
    // The distinction the grammar was built to keep, now load-bearing twice
    // over: `1` and `1:2` select the same element and only the first of them
    // takes the dimension with it, exactly as in Python.
    const std::vector<hsize_t> cube{2, 3, 4};

    REQUIRE(postproc::shapeAfter({OperationKind::Slice, "1, :, :"}, cube).shape
            == std::vector<hsize_t>{3, 4});
    REQUIRE(postproc::shapeAfter({OperationKind::Slice, "1:2, :, :"}, cube).shape
            == std::vector<hsize_t>{1, 3, 4});
    REQUIRE(postproc::shapeAfter({OperationKind::Slice, "1, 2, 3"}, cube).shape.empty());
    REQUIRE(postproc::shapeAfter({OperationKind::Slice, "0"}, cube).shape
            == std::vector<hsize_t>{3, 4});
    REQUIRE(postproc::shapeAfter({OperationKind::Slice, "..., 0"}, cube).shape
            == std::vector<hsize_t>{2, 3});
}

TEST_CASE("the operations are what the panel offers", "[postproc][ops]")
{
    REQUIRE(postproc::operations().size() == 17);
    for (const postproc::OperationInfo& info : postproc::operations()) {
        INFO(info.name.toStdString());
        REQUIRE_FALSE(info.name.isEmpty());
        // One voice down the chain: see the note in operations().
        REQUIRE(info.name == info.name.toLower());
        REQUIRE(postproc::operationNamed(info.name) == info.kind);
        REQUIRE(postproc::operationInfo(info.kind).name == info.name);
    }
    REQUIRE_FALSE(postproc::operationNamed(QStringLiteral("Median")).has_value());
    // Read back from a remembered pipeline, where the case is whatever was
    // written down -- including the capitalised names this catalogue used
    // before, which are still in every settings file written until now.
    REQUIRE(postproc::operationNamed(QStringLiteral("Transpose"))
            == OperationKind::Transpose);
}

TEST_CASE("a shape reads as a shape", "[postproc][ops]")
{
    REQUIRE(describe({}) == "scalar");
    REQUIRE(describe({5}) == "5");
    REQUIRE(describe({2, 3, 4}) == "2 × 3 × 4");
    REQUIRE(describe({0}) == "0");
}

TEST_CASE("normalize puts the smallest finite element at one end and the largest "
          "at the other",
          "[postproc][ops]")
{
    const double inf = std::numeric_limits<double>::infinity();
    const double nan = std::numeric_limits<double>::quiet_NaN();

    const auto run = [](const std::string& argument, std::vector<double> values) {
        const auto length = static_cast<hsize_t>(values.size());
        const Array input({length}, std::move(values));
        const postproc::ArrayResult result =
            postproc::apply(stepFor("normalize", argument), input);
        REQUIRE(result.ok());
        return result.array.values();
    };

    SECTION("onto 0..1 unless it is told otherwise")
    {
        const std::vector<double> out = run("", {2.0, 4.0, 6.0});
        REQUIRE(out == std::vector<double>{0.0, 0.5, 1.0});
        REQUIRE(run("-1, 1", {2.0, 4.0, 6.0}) == std::vector<double>{-1.0, 0.0, 1.0});
        REQUIRE(run("max=10", {2.0, 4.0, 6.0}) == std::vector<double>{0.0, 5.0, 10.0});
        // Reversed ends reverse the map, which is what they say.
        REQUIRE(run("1, 0", {2.0, 4.0, 6.0}) == std::vector<double>{1.0, 0.5, 0.0});
    }

    SECTION("exactly onto the ends asked for, even where they do not subtract "
            "exactly")
    {
        const std::vector<double> out = run("0.1, 0.3", {-7.0, 1.0, 5.0});
        REQUIRE(out.front() == 0.1);
        REQUIRE(out.back() == 0.3);
    }

    SECTION("over the finite elements, leaving the others as they are")
    {
        const std::vector<double> out = run("", {2.0, nan, 4.0, inf, -inf, 6.0});
        REQUIRE(out[0] == 0.0);
        REQUIRE(std::isnan(out[1]));
        REQUIRE(out[2] == 0.5);
        REQUIRE(out[3] == inf);
        REQUIRE(out[4] == -inf);
        REQUIRE(out[5] == 1.0);
    }

    SECTION("a flat line lands on the low end, and nothing finite stays as it was")
    {
        REQUIRE(run("3, 5", {7.0, 7.0, 7.0}) == std::vector<double>{3.0, 3.0, 3.0});
        const std::vector<double> none = run("", {nan, inf});
        REQUIRE(std::isnan(none[0]));
        REQUIRE(none[1] == inf);
    }
}

TEST_CASE("the operations with more than one argument read them as numpy does",
          "[postproc][ops]")
{
    const std::vector<hsize_t> cube{2, 3, 4};
    const auto shape = [&cube](const std::string& operation, const std::string& argument) {
        return postproc::shapeAfter(stepFor(operation, argument), cube);
    };
    const auto refusal = [&shape](const std::string& operation, const std::string& argument) {
        const postproc::ShapeResult result = shape(operation, argument);
        INFO(operation << "(" << argument << ")");
        REQUIRE_FALSE(result.ok());
        return result.error.toStdString();
    };

    SECTION("by position, by name, or both")
    {
        REQUIRE(shape("diff", "1, 0").shape == std::vector<hsize_t>{1, 3, 4});
        REQUIRE(shape("diff", "axis=0").shape == std::vector<hsize_t>{1, 3, 4});
        REQUIRE(shape("diff", "2, axis=1").shape == std::vector<hsize_t>{2, 1, 4});
        REQUIRE(shape("sum", "0, 1, initial=5").shape == std::vector<hsize_t>{4});
        REQUIRE(shape("sum", "axis=(0, 1)").shape == std::vector<hsize_t>{4});
        // min and max take the keyword too, so every fold is written alike.
        REQUIRE(shape("max", "axis=0").shape == std::vector<hsize_t>{3, 4});
        REQUIRE(shape("clip", "None, 1").ok());
        REQUIRE(shape("clip", "a_max=1").ok());
    }

    SECTION("and refuse what Python would refuse")
    {
        REQUIRE_THAT(refusal("sum", "0, axis=1"), ContainsSubstring("given twice"));
        REQUIRE_THAT(refusal("sum", "initial=1, 0"), ContainsSubstring("positional"));
        REQUIRE_THAT(refusal("clip", "low=0"), ContainsSubstring("not an argument"));
        REQUIRE_THAT(refusal("diff", "1, 0, 2"), ContainsSubstring("at most 2"));
        REQUIRE_THAT(refusal("sqrt", "2"), ContainsSubstring("no arguments"));
        REQUIRE_THAT(refusal("abs", "2"), ContainsSubstring("no arguments"));
    }

    SECTION("a number is asked for where one is needed")
    {
        REQUIRE_THAT(refusal("pow", ""), ContainsSubstring("enter a number"));
        REQUIRE_THAT(refusal("add", "two"), ContainsSubstring("not a number"));
        REQUIRE_THAT(refusal("sum", "initial=x"), ContainsSubstring("not a number"));
        REQUIRE_THAT(refusal("diff", "1.5"), ContainsSubstring("whole number"));
        REQUIRE_THAT(refusal("diff", "-1"), ContainsSubstring("non-negative"));
        REQUIRE(shape("multiply", "inf").ok());
        REQUIRE(shape("add", "-1e-3").ok());
    }

    SECTION("a tuple and a list of axes are two spellings, not one")
    {
        REQUIRE_THAT(refusal("sum", "(0, 1), 2"), ContainsSubstring("not both"));
    }
}

TEST_CASE("a computed integer stays one only while the steps keep it whole",
          "[postproc][ops]")
{
    const auto whole = [](const std::string& operation, const std::string& argument) {
        return postproc::preservesIntegers(stepFor(operation, argument));
    };
    REQUIRE(whole("sum", ""));
    REQUIRE(whole("sum", "0, initial=3"));
    REQUIRE_FALSE(whole("sum", "initial=0.5"));
    REQUIRE(whole("cumprod", ""));
    REQUIRE(whole("diff", "2"));
    REQUIRE(whole("pow", "2"));
    REQUIRE_FALSE(whole("pow", "0.5"));
    REQUIRE_FALSE(whole("pow", "-1"));
    REQUIRE(whole("add", "4"));
    REQUIRE_FALSE(whole("add", "1.5"));
    REQUIRE(whole("clip", "0, None"));
    REQUIRE_FALSE(whole("clip", "0, 2.5"));
    REQUIRE_FALSE(whole("sqrt", ""));
    REQUIRE_FALSE(whole("normalize", ""));

    const std::vector<Step> steps = {stepFor("slice", ":"), stepFor("sum", ""),
                                     stepFor("sqrt", "")};
    REQUIRE(postproc::preservesIntegers(steps, 2));
    REQUIRE_FALSE(postproc::preservesIntegers(steps, 3));
}

// ---------------------------------------------------------------------------
// The pipeline
// ---------------------------------------------------------------------------

TEST_CASE("a pipeline stops at the first step that cannot run", "[postproc][pipeline]")
{
    const std::vector<hsize_t> cube{2, 3, 4};
    const std::vector<Step> steps = {
        {OperationKind::Slice, ":, :, :"},
        {OperationKind::Max, "0"},
        {OperationKind::Transpose, "9, 9"}, // no such axes
        {OperationKind::Abs, ""},
    };

    const postproc::Trace trace = postproc::trace(cube, steps, steps.size());
    REQUIRE_FALSE(trace.ok());
    REQUIRE(trace.ran == 2);
    REQUIRE(trace.output == std::vector<hsize_t>{3, 4});
    REQUIRE_THAT(trace.error.toStdString(), ContainsSubstring("out of bounds"));

    SECTION("the step that refused says why and the ones after it say nothing")
    {
        REQUIRE(trace.stages[0].ok());
        REQUIRE(trace.stages[1].ok());
        REQUIRE_FALSE(trace.stages[2].ok());
        // Not an error and not a shape: it did not run, and it is not the
        // reason anything stopped.
        REQUIRE(trace.stages[3].ok());
        REQUIRE(trace.stages[3].shape.empty());
    }
}

TEST_CASE("a pipeline can be asked for only part of itself", "[postproc][pipeline]")
{
    // What clicking a row does: everything below it greys out and the output
    // is the shape at the row that was clicked.
    const std::vector<hsize_t> cube{2, 3, 4};
    const std::vector<Step> steps = {
        {OperationKind::Slice, ":, :, :"},
        {OperationKind::Max, "0"},
        {OperationKind::Min, "0"},
    };

    REQUIRE(postproc::trace(cube, steps, 1).output == cube);
    REQUIRE(postproc::trace(cube, steps, 2).output == std::vector<hsize_t>{3, 4});
    REQUIRE(postproc::trace(cube, steps, 3).output == std::vector<hsize_t>{4});
    REQUIRE(postproc::trace(cube, steps, 99).output == std::vector<hsize_t>{4});
}

TEST_CASE("a shape is worked out without resolving what it counts",
          "[postproc][pipeline]")
{
    // The shape of a slice is how many indices each subscript names, and
    // nothing else. Working that out by writing the indices down and measuring
    // them is what made selecting a large dataset slow: the panel asks for this
    // on every keystroke and on every change of selection, and `:` on a
    // dimension of ten million resolved to eighty megabytes built and freed.
    //
    // A shape no machine could hold the indices of is how that is asserted
    // without a clock in the test: a duration measures the machine and a count
    // measures the program, and the count here would be "one allocation" --
    // which nothing in this suite can see. The dimensions below are 10^9 and
    // 10^7, so an implementation that resolved them would ask for eight
    // gigabytes and this test would fail by dying rather than by reporting.
    // That is the intended failure and it is not subtle, which is the point.
    const std::vector<hsize_t> enormous{1000000000, 10000000};
    const std::vector<Step> whole = {{OperationKind::Slice, ":, :"}};
    REQUIRE(postproc::trace(enormous, whole, 1).output == enormous);

    SECTION("and every way of writing one is counted the same way it is read")
    {
        // The two readers of a slice line share a grammar and must not drift
        // apart in it, so each of these is counted and resolved and the answers
        // compared. A bare index drops its dimension and a list of one does
        // not, which is the distinction most easily lost.
        const std::vector<hsize_t> shape{10, 12};
        const QStringList lines{":, :",      "3, :",      "3:7, 2:5",  "::2, ::3",
                                "::-1, :",   "9:4:-1, :", "[3], :",    "[0,2,5], :",
                                "-3:, :",    ":-1, :",    "0, 0",      "...",
                                "5",         "[0,2,0], :"};
        for (const QString& line : lines) {
            INFO(line.toStdString());
            const postproc::Trace counted = postproc::trace(
                shape, {{OperationKind::Slice, line}}, 1);

            std::vector<postproc::IndexExpression> chosen;
            QStringList written;
            QString error;
            REQUIRE(postproc::readSubscripts(line, shape, chosen, written, error));
            std::vector<hsize_t> resolved;
            for (const postproc::IndexExpression& subscript : chosen) {
                if (subscript.form != postproc::IndexExpression::Form::Single) {
                    resolved.push_back(static_cast<hsize_t>(subscript.indices.size()));
                }
            }
            CHECK(counted.ok());
            CHECK(counted.output == resolved);
        }
    }

    SECTION("...and a line that will not read is refused in the same words")
    {
        const std::vector<hsize_t> shape{10, 12};
        const QStringList refused{"20, :", "3:3, :", "0, 0, 0", "1.5, :",
                                  "[, :",  "::0, :", "..., ..."};
        for (const QString& line : refused) {
            INFO(line.toStdString());
            const postproc::Trace counted = postproc::trace(
                shape, {{OperationKind::Slice, line}}, 1);

            std::vector<postproc::IndexExpression> chosen;
            QStringList written;
            QString error;
            CHECK_FALSE(postproc::readSubscripts(line, shape, chosen, written, error));
            CHECK_FALSE(counted.ok());
            CHECK(counted.error == error);
        }
    }
}

TEST_CASE("a scalar has no subscripts and is not asked for any",
          "[postproc][pipeline]")
{
    const std::vector<Step> steps = {{OperationKind::Slice, ""},
                                     {OperationKind::Abs, ""}};
    const postproc::Trace trace = postproc::trace({}, steps, steps.size());
    REQUIRE(trace.ok());
    REQUIRE(trace.ran == 2);
    REQUIRE(trace.output.empty());
}

TEST_CASE("a selection too large to hold is refused before it is read",
          "[postproc][pipeline]")
{
    // Everything below the panel streams, which is why this application opens
    // a dataset of 10^9 elements without noticing. A pipeline cannot, so it
    // says so -- and the whole point is that it says so *first*.
    const HugeSource huge({1000, 1000, 1000});
    const std::vector<Step> steps = {{OperationKind::Slice, ":, :, :"}};

    const postproc::RunResult result = postproc::run(huge, steps, 1);
    REQUIRE_FALSE(result.usable());
    REQUIRE_THAT(result.error.toStdString(), ContainsSubstring("narrow the slice"));
    REQUIRE_THAT(result.error.toStdString(),
                 ContainsSubstring(std::to_string(postproc::kMaxElements)));
    REQUIRE_FALSE(huge.reads);

    SECTION("and a slice that narrows it below the cap is read after all")
    {
        const std::vector<Step> narrowed = {{OperationKind::Slice, "0, :, :"}};
        const postproc::RunResult ran = postproc::run(huge, narrowed, 1);
        REQUIRE(huge.reads);
        REQUIRE(ran.error.isEmpty());
        REQUIRE(ran.array.shape() == std::vector<hsize_t>{1000, 1000});
    }

    SECTION("the shape walk is not capped, because it reads nothing")
    {
        // The panel prints a shape against every row of a dataset far too
        // large to run, which is what makes the cap a refusal rather than a
        // wall.
        const postproc::Trace walk =
            postproc::trace(huge.info().shape, steps, steps.size());
        REQUIRE(walk.ok());
        REQUIRE(walk.output == std::vector<hsize_t>{1000, 1000, 1000});
    }
}


TEST_CASE("a whole dimension parses in time proportional to itself",
          "[postproc][subscripts]")
{
    // `:` on a dimension of two million used to take longer than anyone would
    // wait, because the parser dropped duplicates with a linear scan per index
    // -- 10^12 comparisons. Nothing noticed while only a *custom* subscript
    // reached the parser: the panel resolved `:` and a range for itself. The
    // pipeline parses every subscript of every dimension, so it walked
    // straight into it, and `inspect-file` went from three seconds to not
    // finishing.
    //
    // Timed rather than asserted structurally, because what is being pinned
    // down is a complexity class and there is nothing else to look at. The
    // bound is a thousand times the two milliseconds this actually takes, so
    // it cannot flake; a quadratic would miss it by a factor of a million.
    constexpr hsize_t kWide = 2000000;

    QElapsedTimer timer;
    timer.start();
    const postproc::IndexExpression whole =
        postproc::parseIndexExpression(QStringLiteral(":"), kWide);
    const qint64 elapsed = timer.elapsed();

    REQUIRE(whole.valid());
    REQUIRE(whole.indices.size() == kWide);
    REQUIRE(whole.form == postproc::IndexExpression::Form::Whole);
    REQUIRE(elapsed < 2000);

    SECTION("and a list still drops what it names twice")
    {
        // The check is skipped for a single term because one cannot repeat
        // itself; a list can, and still pays for it.
        const postproc::IndexExpression listed =
            postproc::parseIndexExpression(QStringLiteral("[3,1,3,0,1]"), 10);
        REQUIRE(listed.valid());
        REQUIRE(listed.indices == std::vector<hsize_t>{3, 1, 0});
        REQUIRE(listed.form == postproc::IndexExpression::Form::Scattered);

        // Overlapping runs are a list too, and the order the first mention
        // put them in is the order that survives.
        const postproc::IndexExpression overlapping =
            postproc::parseIndexExpression(QStringLiteral("2:5,0:4"), 10);
        REQUIRE(overlapping.indices == std::vector<hsize_t>{2, 3, 4, 0, 1});
    }

    SECTION("a single descending run keeps every index it names")
    {
        const postproc::IndexExpression down =
            postproc::parseIndexExpression(QStringLiteral("::-1"), 5);
        REQUIRE(down.indices == std::vector<hsize_t>{4, 3, 2, 1, 0});
    }
}

// ---------------------------------------------------------------------------
// The script
// ---------------------------------------------------------------------------

namespace {

h5core::TypeInfo floats()
{
    h5core::TypeInfo type;
    type.cls = h5core::TypeClass::Float;
    type.description = "float64";
    type.size = 8;
    return type;
}

/// A struct with a four-long array of doubles in it, as `/plotting/events`
/// has: the case where a member appends an axis.
h5core::TypeInfo withSamples()
{
    h5core::TypeInfo samples;
    samples.cls = h5core::TypeClass::Array;
    samples.description = "array[4] of float64";
    samples.arrayDims = {4};
    samples.base = std::make_shared<const h5core::TypeInfo>(floats());

    h5core::TypeInfo type;
    type.cls = h5core::TypeClass::Compound;
    type.description = "compound";
    type.memberNames = {"time", "samples"};
    type.members = {h5core::TypeMember{"time", floats(), 0},
                    h5core::TypeMember{"samples", samples, 8}};
    return type;
}

postproc::Script script(const char* text)
{
    return postproc::parseScript(QString::fromUtf8(text));
}

} // namespace

TEST_CASE("a script reads as the rows it stands for", "[postproc][script]")
{
    const postproc::Script read = script("/group/dataset\n"
                                         ".select(samples)\n"
                                         ".slice(1, :)\n"
                                         ".max(1)\n"
                                         ".transpose\n"
                                         ".reshape(2, 10)");
    REQUIRE(read.ok());
    REQUIRE(read.path == QStringLiteral("/group/dataset"));
    REQUIRE(read.member == QStringLiteral(".samples"));
    REQUIRE(read.sliced);
    REQUIRE(read.slice == QStringLiteral("1, :"));
    REQUIRE(read.steps == std::vector<Step>{{OperationKind::Max, "1"},
                                            {OperationKind::Transpose, ""},
                                            {OperationKind::Reshape, "2, 10"}});

    SECTION("the line breaks are optional")
    {
        const postproc::Script flat = script(
            "/group/dataset.select(samples).slice(1, :).max(1).transpose.reshape(2, 10)");
        REQUIRE(flat == read);
        // ...and so is the space around a step.
        REQUIRE(script("/group/dataset .select(samples)\n  .slice(1, :) .max (1)"
                       ".transpose .reshape(2, 10)")
                == read);
    }

    SECTION("it is written one step to a line, and reads back as itself")
    {
        const QString lines = postproc::writeScript(read);
        REQUIRE(lines
                == QStringLiteral("/group/dataset\n.select(samples)\n.slice(1, :)\n"
                                  ".max(1)\n.transpose\n.reshape(2, 10)"));
        REQUIRE(postproc::parseScript(lines) == read);

        // On one line every step keeps its parentheses, because there they are
        // what says where the path stops.
        const QString flat = postproc::writeScript(read, postproc::ScriptLayout::OneLine);
        REQUIRE(flat
                == QStringLiteral("/group/dataset.select(samples).slice(1, :).max(1)"
                                  ".transpose().reshape(2, 10)"));
        REQUIRE(postproc::parseScript(flat) == read);
    }

    SECTION("a slice after the first is a step like any other")
    {
        const postproc::Script twice = script("/d\n.slice(0)\n.max\n.slice(1)");
        REQUIRE(twice.slice == QStringLiteral("0"));
        REQUIRE(twice.steps
                == std::vector<Step>{{OperationKind::Max, ""}, {OperationKind::Slice, "1"}});
        // ...and a pipeline that never slices reads the whole of the dataset.
        const postproc::Script none = script("/d\n.max(0)");
        REQUIRE_FALSE(none.sliced);
        REQUIRE(postproc::pipelineOf(none, {}, 2).front().argument.isEmpty());
    }
}

TEST_CASE("a path runs to the end of its line, or to the first operation on it",
          "[postproc][script]")
{
    // A link name holds a '.' as freely as a '[': these are all datasets.
    REQUIRE(script("/data/run.3\n.max").path == QStringLiteral("/data/run.3"));
    REQUIRE(script("/data/run.3").path == QStringLiteral("/data/run.3"));
    REQUIRE(script("/stress/odd.name(1)").path == QStringLiteral("/stress/odd.name(1)"));
    // Without parentheses an operation's name on the first line is part of the
    // path: one line has nothing else to say where the path stops.
    REQUIRE(script("/data/run.max").path == QStringLiteral("/data/run.max"));
    REQUIRE(script("/data/run.max").steps.empty());
    // With them it is a step.
    REQUIRE(script("/a.b/c.max(0)").path == QStringLiteral("/a.b/c"));
    REQUIRE(script("  /a/b  \n.abs").path == QStringLiteral("/a/b"));
    REQUIRE(script("/a/b\r\n.abs\r\n").steps
            == std::vector<Step>{{OperationKind::Abs, ""}});
}

TEST_CASE("a script says what is wrong with it", "[postproc][script]")
{
    const auto why = [](const char* text) {
        const postproc::Script read = script(text);
        INFO(text);
        REQUIRE_FALSE(read.ok());
        return read.error.toStdString();
    };
    REQUIRE_THAT(why(""), ContainsSubstring("path of a dataset"));
    REQUIRE_THAT(why("/d\nmax(0)"), ContainsSubstring("'max(0)' is not a step"));
    REQUIRE_THAT(why("/d\n.median(0)"), ContainsSubstring("'median' is not an operation"));
    REQUIRE_THAT(why("/d\n.max(0"), ContainsSubstring("never closed"));
    REQUIRE_THAT(why("/d\n."), ContainsSubstring("followed by an operation"));
    REQUIRE_THAT(why("/d\n.slice(:)\n.select(samples)"), ContainsSubstring("comes first"));
    REQUIRE_THAT(why("/d\n.select(a)\n.select(b)"), ContainsSubstring("comes first"));
    REQUIRE_THAT(why("/d\n.select()"), ContainsSubstring("needs a member"));
}

TEST_CASE("a script is checked against a dataset without reading it",
          "[postproc][script]")
{
    SECTION("the shape after every step, as the rows would state it")
    {
        const postproc::ScriptCheck check =
            postproc::checkScript(script("/d\n.slice(0)\n.max(0)\n.cumsum"), {2, 3, 4},
                                  floats());
        REQUIRE(check.ok());
        REQUIRE(check.output == std::vector<hsize_t>{4});
    }

    SECTION("a refusal names the step that gave it")
    {
        const postproc::ScriptCheck check =
            postproc::checkScript(script("/d\n.max(0)\n.max(5)"), {2, 3}, floats());
        REQUIRE_THAT(check.error.toStdString(), ContainsSubstring(".max(5): axis 5"));
        REQUIRE(check.output == std::vector<hsize_t>{3});

        const postproc::ScriptCheck slice =
            postproc::checkScript(script("/d\n.slice(9)"), {2, 3}, floats());
        REQUIRE_THAT(slice.error.toStdString(), ContainsSubstring(".slice(9): "));
    }

    SECTION("a struct is not a number, and a member of one can be")
    {
        const postproc::ScriptCheck whole =
            postproc::checkScript(script("/events\n.max"), {10}, withSamples());
        REQUIRE_THAT(whole.error.toStdString(), ContainsSubstring(".select"));

        const postproc::ScriptCheck member = postproc::checkScript(
            script("/events\n.select(samples)\n.max(1)"), {10}, withSamples());
        REQUIRE(member.ok());
        REQUIRE(member.input == std::vector<hsize_t>{10, 4});
        REQUIRE(member.output == std::vector<hsize_t>{10});

        const postproc::ScriptCheck missing = postproc::checkScript(
            script("/events\n.select(nothing)"), {10}, withSamples());
        REQUIRE_THAT(missing.error.toStdString(), ContainsSubstring(".select(nothing): "));
    }

    SECTION("a subscript on the member and one on the slice are the same selection")
    {
        // The identity the whole member notation rests on, kept here by the
        // same function the slice bar uses rather than by a second copy of it.
        const postproc::ScriptCheck shortSpelling = postproc::checkScript(
            script("/events\n.select(samples[2])\n.slice(0:5)"), {10}, withSamples());
        const postproc::ScriptCheck longSpelling = postproc::checkScript(
            script("/events\n.select(samples)\n.slice(0:5, 2)"), {10}, withSamples());
        INFO(shortSpelling.error.toStdString() << " / " << longSpelling.error.toStdString());
        REQUIRE(shortSpelling.ok());
        REQUIRE(longSpelling.ok());
        REQUIRE(shortSpelling.pipeline == longSpelling.pipeline);
        REQUIRE(shortSpelling.output == std::vector<hsize_t>{5});
    }
}
