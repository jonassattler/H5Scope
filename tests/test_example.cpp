// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

// The example file, read back through the application's own stack.
//
// tools/ExampleFile.cpp writes one file holding everything HDF5 can express
// that a viewer has to render -- every datatype class, every storage layout,
// every link kind, datasets larger than memory, and rasters with a channel
// axis. This suite asserts what the viewer makes of each of them.
//
// Every case here was a defect the file found. They are grouped by the
// question the reader is asking, not by the layer that answers it, because
// what matters is the answer that reaches the screen.

#include "ExampleFile.hpp"

#include "gui/AppController.hpp"
#include "support/AsyncModels.hpp"
#include "gui/H5Thread.hpp"
#include "gui/DatasetImage.hpp"
#include "gui/DatasetTableModel.hpp"
#include "gui/H5TreeModel.hpp"
#include "gui/TableSetupModel.hpp"
#include "h5core/Attribute.hpp"
#include "h5core/Dataset.hpp"
#include "h5core/Error.hpp"
#include "h5core/File.hpp"
#include "support/H5Reader.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <QImage>
#include <QVariantMap>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <string>

using Catch::Matchers::ContainsSubstring;

namespace {

/// The example file, written once for the whole suite and removed with it.
/// Writing it costs about a second, and every test here reads the same bytes.
class ExampleFixture
{
public:
    ExampleFixture()
    {
        std::random_device device;
        directory_ = std::filesystem::temp_directory_path() /
                     ("h5scope_example_" + std::to_string(device()));
        // Written on the HDF5 thread like every other HDF5 call in this
        // process. The generator does not go through h5core and so would not
        // be caught by its guard, which is exactly why it is worth being
        // deliberate about: two threads in the library is two threads in the
        // library whichever one of them wrote the file.
        gui::H5Thread::instance().invoke([this](gui::H5Session&) {
            h5example::writeExampleFiles(directory_);
            return 0;
        });
    }
    ~ExampleFixture()
    {
        std::error_code ec;
        std::filesystem::remove_all(directory_, ec);
    }
    ExampleFixture(const ExampleFixture&) = delete;
    ExampleFixture& operator=(const ExampleFixture&) = delete;

    [[nodiscard]] std::string path() const
    {
        return (directory_ / "example.h5").string();
    }

private:
    std::filesystem::path directory_;
};

const ExampleFixture& example()
{
    static const ExampleFixture fixture;
    return fixture;
}

/// The example file, opened in the application's HDF5 session.
///
/// A reader rather than a `h5core::File`: the file lives on the thread that
/// owns HDF5 and this is a test's handle to it. Every call on it is a blocking
/// round trip, which is the right thing for a test and the wrong thing for the
/// window -- see H5Reader.hpp.
h5test::Reader openExample()
{
    return h5test::Reader(example().path());
}

const h5core::NodeInfo* find(const std::vector<h5core::NodeInfo>& nodes,
                             const std::string& name)
{
    const auto it = std::find_if(nodes.begin(), nodes.end(),
                                 [&](const auto& node) { return node.name == name; });
    return (it == nodes.end()) ? nullptr : &*it;
}

/// Read syscalls this process has made so far, from /proc/self/io's `syscr`,
/// or nothing where that file does not exist.
///
/// The unit the tree's cost is actually measured in. A duration would be a
/// test of the machine it runs on -- these reads are microseconds against a
/// page cache and milliseconds against a network filesystem, which is where
/// large HDF5 files live and where this pane was found to be unusable. A count
/// is the same number everywhere.
std::optional<long long> readSyscalls()
{
    std::ifstream io("/proc/self/io");
    if (!io) {
        return std::nullopt;
    }
    std::string key;
    long long value = 0;
    while (io >> key >> value) {
        if (key == "syscr:") {
            return value;
        }
    }
    return std::nullopt;
}

/// The child of `parent` with this link name, or an invalid index.
///
/// Waits for the listing before looking through it: the tree answers with what
/// it has and asks the file for the rest, so a count taken the instant it is
/// asked for is zero by design.
QModelIndex indexForName(QAbstractItemModel* tree, const QModelIndex& parent,
                         const QString& name)
{
    const int rows = h5test::settledRowCount(tree, parent);
    for (int row = 0; row < rows; ++row) {
        const QModelIndex child = tree->index(row, 0, parent);
        // The name is out of the link table and is there the moment the row is,
        // so this one role needs no waiting.
        if (h5test::settledData(tree, child, gui::H5TreeModel::NameRole).toString() == name) {
            return child;
        }
    }
    return {};
}

/// The value of one Information-tab row, or an empty string when the panels do
/// not carry that label. Asserting through infoPanels() is deliberate: it is
/// the property QML binds, so this is what a reader actually sees.
QString infoRow(const gui::AppController& controller, const QString& label)
{
    for (const QVariant& panel : controller.infoPanels()) {
        for (const QVariant& row : panel.toMap().value("rows").toList()) {
            const QVariantMap fields = row.toMap();
            if (fields.value("label").toString() == label) {
                return fields.value("value").toString();
            }
        }
    }
    return {};
}

std::string attributeValue(const h5test::Reader& file, const std::string& path,
                           const std::string& name)
{
    for (const auto& attribute : file.attributes(path)) {
        if (attribute.name == name) {
            return attribute.value;
        }
    }
    return "<absent>";
}

} // namespace

TEST_CASE("a link says where it points", "[example][links]")
{
    const auto file = openExample();
    const auto links = file.children("/links");

    SECTION("a hard link is the object and says nothing further")
    {
        const auto* node = find(links, "hard_to_matrix");
        REQUIRE(node != nullptr);
        CHECK(node->link == h5core::LinkType::Hard);
        CHECK(node->kind == h5core::NodeKind::Dataset);
        CHECK(node->linkTarget.empty());
    }

    SECTION("a soft link reports its target and the kind it resolves to")
    {
        const auto* node = find(links, "soft_to_matrix");
        REQUIRE(node != nullptr);
        CHECK(node->link == h5core::LinkType::Soft);
        CHECK(node->linkTarget == "/data/matrix");
        // The resolved kind, not "a link": what the reader is about to look at
        // is a dataset, and the tree has to treat it as one.
        CHECK(node->kind == h5core::NodeKind::Dataset);
        CHECK(node->resolves());
    }

    SECTION("an external link reports its file as well, and resolves through it")
    {
        const auto* node = find(links, "external_group");
        REQUIRE(node != nullptr);
        CHECK(node->link == h5core::LinkType::External);
        CHECK(node->linkFile == "example_external.h5");
        CHECK(node->linkTarget == "/external");
        // A group across an external link is a group: browsable on exactly the
        // terms every other group is.
        CHECK(node->kind == h5core::NodeKind::Group);
        CHECK(file.children("/links/external_group").size() == 2);
    }

    SECTION("a link that resolves to nothing still says what it stores")
    {
        const auto* dangling = find(links, "soft_dangling");
        REQUIRE(dangling != nullptr);
        CHECK(dangling->link == h5core::LinkType::Soft);
        CHECK(dangling->linkTarget == "/no/such/object");
        CHECK_FALSE(dangling->resolves());

        const auto* missing = find(links, "external_missing_file");
        REQUIRE(missing != nullptr);
        CHECK(missing->linkFile == "no_such_file.h5");
        CHECK_FALSE(missing->resolves());
    }

    SECTION("a broken link is a link, even though it is not an object")
    {
        CHECK(file.hasLink("/links/soft_dangling"));
        CHECK_FALSE(file.exists("/links/soft_dangling"));
        CHECK_FALSE(file.hasLink("/links/no_such_link"));

        // nodeInfo answers for it rather than throwing: a link pointing nowhere
        // is a state of the file, not a failure to read it.
        const auto node = file.nodeInfo("/links/soft_dangling");
        CHECK(node.kind == h5core::NodeKind::Unresolved);
        CHECK(node.linkTarget == "/no/such/object");
    }
}

TEST_CASE("a broken link can be selected and explains itself", "[example][links][gui]")
{
    gui::AppController controller;
    REQUIRE(h5test::openFileAndSettle(controller, QString::fromStdString(example().path())));

    // Clicking a dangling link used to do nothing at all -- selectPath refused
    // it, so the panels kept describing whatever was selected before.
    REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/links/external_missing_target")));
    CHECK(infoRow(controller, QStringLiteral("Link")) == QStringLiteral("External link"));
    CHECK(infoRow(controller, QStringLiteral("File")) ==
          QStringLiteral("example_external.h5"));
    CHECK(infoRow(controller, QStringLiteral("Target")) ==
          QStringLiteral("/no/such/object"));
    CHECK_FALSE(controller.datasetTabVisible());
    CHECK_FALSE(controller.metadataTabVisible());
    // No object behind the name means no attribute count to state.
    CHECK_FALSE(controller.statusRight().contains(QStringLiteral(" attrs")));
}

TEST_CASE("a null dataspace holds nothing, not one unreadable value",
          "[example][dataspace]")
{
    const auto file = openExample();
    const h5test::Dataset dataset(file, "/data/null_space");

    CHECK(dataset.info().isNull());
    CHECK_FALSE(dataset.info().isScalar());
    CHECK(dataset.info().rank() == 0);
    CHECK(dataset.info().elementCount() == 0);

    // And reading it yields nothing rather than failing.
    const auto window = dataset.readWindow({}, {});
    CHECK(window.cells.empty());

    const h5test::Dataset scalar(file, "/data/scalar_int");
    CHECK(scalar.info().isScalar());
    CHECK(scalar.info().elementCount() == 1);
}

TEST_CASE("the table of a null dataspace is empty", "[example][dataspace][gui]")
{
    gui::AppController controller;
    REQUIRE(h5test::openFileAndSettle(controller, QString::fromStdString(example().path())));
    REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/data/null_space")));

    CHECK(controller.datasetElementCount() == 0);
    CHECK(controller.datasetModel()->rowCount() == 0);
    CHECK(controller.datasetModel()->columnCount() == 0);
    CHECK(infoRow(controller, QStringLiteral("Dataspace")) == QStringLiteral("Null"));
}

TEST_CASE("datatypes HDF5 2.x added, and widths a switch would miss",
          "[example][datatype]")
{
    const auto file = openExample();

    SECTION("a complex number is a class of its own, not an unknown blob")
    {
        const h5test::Dataset dataset(file, "/types/complex64");
        CHECK(dataset.info().type.cls == h5core::TypeClass::Complex);
        CHECK_THAT(dataset.info().type.description, ContainsSubstring("complex64"));

        const auto window = dataset.readWindow({0}, {4});
        REQUIRE(window.cells.size() == 4);
        CHECK(window.cells[1] == "1-1i");
        CHECK(window.cells[3] == "3+4i");
    }

    SECTION("half precision reads as a number")
    {
        const h5test::Dataset dataset(file, "/types/float/float16");
        CHECK(dataset.info().type.description == "float16");
        const auto window = dataset.readWindow({0}, {5});
        REQUIRE(window.cells.size() == 5);
        CHECK(window.cells[0] == "-1.5");
        CHECK(window.cells[1] == "0");
        // 0.1f is not representable in binary16; this is the value stored.
        CHECK_THAT(window.cells[2], ContainsSubstring("0.0999"));
    }

    SECTION("an integer narrower than its word says so")
    {
        const h5test::Dataset dataset(file, "/types/integer/int20_in_int32");
        CHECK(dataset.info().type.description == "int32 (20-bit)");
    }
}

TEST_CASE("a type the library cannot convert is refused before it is read",
          "[example][datatype]")
{
    const auto file = openExample();
    const h5test::Dataset dataset(file, "/types/time_unix");

    CHECK(dataset.info().type.cls == h5core::TypeClass::Time);
    CHECK_FALSE(dataset.info().type.convertible);
    CHECK_FALSE(dataset.info().readable());
    CHECK_THAT(dataset.info().unreadableReason(), ContainsSubstring("cannot convert"));

    // The failure is a sentence, not the HDF5 error stack: reaching the read
    // and letting H5Tget_native_type report it is what used to put "#0: ..."
    // in front of the reader.
    try {
        [[maybe_unused]] const auto window = dataset.readWindow({0}, {4});
        FAIL("reading an unconvertible type should throw");
    }
    catch (const h5core::H5Error& error) {
        CHECK_THAT(error.summary(), ContainsSubstring("cannot convert"));
        CHECK_THAT(error.summary(), !ContainsSubstring("#0:"));
    }
}

TEST_CASE("a missing filter blocks the data only when it is mandatory",
          "[example][filters]")
{
    const auto file = openExample();

    SECTION("mandatory and absent: the data genuinely cannot be read")
    {
        const h5test::Dataset dataset(file, "/filters/unavailable_mandatory");
        REQUIRE(dataset.info().unavailableFilters.size() == 1);
        REQUIRE(dataset.info().blockingFilters.size() == 1);
        CHECK_FALSE(dataset.info().readable());
        // The name the file records for a third-party filter, not just its
        // number: it is the only thing saying what this build is missing.
        CHECK_THAT(dataset.info().filters.front(), ContainsSubstring("lz4"));
        CHECK_THAT(dataset.info().filters.front(), ContainsSubstring("32004"));
    }

    SECTION("optional and absent: HDF5 skips it and the values are exact")
    {
        const h5test::Dataset dataset(file, "/filters/unavailable_optional");
        REQUIRE(dataset.info().unavailableFilters.size() == 1);
        CHECK(dataset.info().blockingFilters.empty());
        CHECK(dataset.info().readable());

        const auto window = dataset.readWindow({0, 0}, {1, 4});
        REQUIRE(window.cells.size() == 4);
        CHECK(window.cells[3] == "1");
    }

    SECTION("a filter that is present is neither")
    {
        const h5test::Dataset dataset(file, "/filters/deflate");
        CHECK(dataset.info().filters == std::vector<std::string>{"deflate (level 6)"});
        CHECK(dataset.info().unavailableFilters.empty());
        CHECK(dataset.info().readable());
    }
}

TEST_CASE("storage that is not in this file says where it is", "[example][storage]")
{
    const auto file = openExample();

    SECTION("raw data in a companion file")
    {
        const h5test::Dataset dataset(file, "/storage/external_raw");
        CHECK(dataset.info().externalFiles ==
              std::vector<std::string>{"example_raw.bin"});
    }

    SECTION("a virtual dataset names every source it stitches together")
    {
        const h5test::Dataset dataset(file, "/storage/virtual");
        CHECK(dataset.info().layout == h5core::Layout::Virtual);
        REQUIRE(dataset.info().virtualSources.size() == 2);
        CHECK_THAT(dataset.info().virtualSources[0], ContainsSubstring("row_a"));
        CHECK_THAT(dataset.info().virtualSources[1], ContainsSubstring("row_b"));
        // And the values come through the mapping.
        const auto window = dataset.readWindow({0, 0}, {2, 1});
        REQUIRE(window.cells.size() == 2);
        CHECK(window.cells[0] == "0");
        CHECK(window.cells[1] == "100");
    }

    SECTION("a dataset that was never written reads back as its fill value")
    {
        const h5test::Dataset dataset(file, "/storage/fill_value_only");
        CHECK(dataset.info().storageSize == 0);
        CHECK(dataset.readWindow({0, 0}, {1, 1}).cells.front() == "-999");
    }

    SECTION("an unlimited extent is carried through as the sentinel it is")
    {
        const h5test::Dataset dataset(file, "/storage/extendable");
        REQUIRE(dataset.info().maxShape.size() == 2);
        CHECK(dataset.info().maxShape[0] == H5S_UNLIMITED);
        CHECK(dataset.info().maxShape[1] == 8);
    }
}

TEST_CASE("a named datatype shows the type it holds", "[example][datatype][gui]")
{
    const auto file = openExample();
    CHECK(file.nodeInfo("/committed/celsius_t").kind == h5core::NodeKind::NamedDataType);
    CHECK(file.namedType("/committed/celsius_t").description == "float64");
    CHECK(file.namedType("/committed/reading_t").cls == h5core::TypeClass::Compound);

    // The type is the only thing a named datatype has to say, so the panel that
    // says it is the point of selecting one.
    gui::AppController controller;
    REQUIRE(h5test::openFileAndSettle(controller, QString::fromStdString(example().path())));
    REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/committed/celsius_t")));
    CHECK(infoRow(controller, QStringLiteral("Type")) == QStringLiteral("float64"));
    CHECK(infoRow(controller, QStringLiteral("Class")) == QStringLiteral("Float"));
}

TEST_CASE("an attribute holding no elements is not an unreadable one",
          "[example][attributes]")
{
    const auto file = openExample();
    CHECK(attributeValue(file, "/", "empty_attribute") == "[]");
    // The neighbours still read, so the empty case is not being special-cased
    // into silence.
    CHECK(attributeValue(file, "/", "format_version") == "3");
    CHECK(attributeValue(file, "/", "quality") == "GOOD");
    CHECK_THAT(attributeValue(file, "/", "long_attribute"),
               ContainsSubstring("(744 more)"));
}

TEST_CASE("a dataset larger than memory is browsed, not loaded", "[example][large]")
{
    gui::AppController controller;
    REQUIRE(h5test::openFileAndSettle(controller, QString::fromStdString(example().path())));
    REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/large/unallocated_100000x10000")));

    // A billion elements, none of them stored: anything that reads a dataset
    // whole dies here.
    CHECK(controller.datasetElementCount() == 1000000000);
    CHECK(controller.datasetModel()->rowCount() == 100000);
    CHECK(controller.datasetModel()->columnCount() == 10000);
    // The far corner of a billion elements, and only the block around it is
    // read -- a frame after it is asked for, which is what the settle is.
    CHECK(h5test::settledData(controller.datasetModel(),
                              controller.datasetModel()->index(99999, 9999),
                              Qt::DisplayRole)
              .toString()
          == QStringLiteral("7"));

    const auto* table =
        qobject_cast<const gui::DatasetTableModel*>(controller.datasetModel());
    REQUIRE(table != nullptr);
    const auto grid = table->sampleValues(0, -1, 64, 0, -1, 64);
    CHECK(grid.rows == 64);
    CHECK(grid.columns == 64);
    CHECK(grid.rowStride > 1);
    CHECK(grid.at(0, 0) == 7.0);
}

TEST_CASE("a dataset that declares itself an image opens as one", "[example][images]")
{
    gui::AppController controller;
    REQUIRE(h5test::openFileAndSettle(controller, QString::fromStdString(example().path())));

    const auto* table =
        qobject_cast<const gui::DatasetTableModel*>(controller.datasetModel());
    REQUIRE(table != nullptr);

    SECTION("pixel-interleaved truecolour: height on y, width on x, one channel")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/images/rgb_256x256x3")));
        // Not 65,536 rows of three channels, which is what the shape alone
        // would give and what no reader wants to look at.
        CHECK(controller.datasetModel()->rowCount() == 256);
        CHECK(controller.datasetModel()->columnCount() == 256);
        CHECK(controller.sliceExpression() ==
              QStringLiteral("/images/rgb_256x256x3[:, :, 0]"));
        CHECK(controller.datasetImage()->width() == 256);
        CHECK(controller.datasetImage()->height() == 256);
    }

    SECTION("plane-interleaved truecolour: the channel axis is the first one")
    {
        REQUIRE(
            h5test::selectAndSettle(controller, QStringLiteral("/images/rgb_planar_3x256x256")));
        CHECK(controller.datasetModel()->rowCount() == 256);
        CHECK(controller.datasetModel()->columnCount() == 256);
        CHECK(controller.sliceExpression() ==
              QStringLiteral("/images/rgb_planar_3x256x256[0, :, :]"));
    }

    SECTION("four channels are pinned exactly as three are")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/images/rgba_128x128x4")));
        CHECK(controller.datasetModel()->rowCount() == 128);
        CHECK(controller.datasetModel()->columnCount() == 128);
    }

    SECTION("a single-channel image is already right, and is left alone")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/images/indexed_64x64")));
        CHECK(controller.datasetModel()->rowCount() == 64);
        CHECK(controller.datasetModel()->columnCount() == 64);
        CHECK(controller.sliceExpression() ==
              QStringLiteral("/images/indexed_64x64[:, :]"));
    }

    SECTION("the pinned channel is a starting point, not a binding")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/images/rgb_256x256x3")));
        auto* setup = qobject_cast<gui::TableSetupModel*>(controller.tableSetupModel());
        REQUIRE(setup != nullptr);
        setup->setIndex(2, 2); // the blue channel
        CHECK(controller.sliceExpression() ==
              QStringLiteral("/images/rgb_256x256x3[:, :, 2]"));
        CHECK(controller.datasetModel()->rowCount() == 256);

        // And the whole channel axis is still reachable: nothing is hidden,
        // only defaulted.
        setup->setMode(2, gui::TableSetupModel::All);
        CHECK(controller.datasetModel()->columnCount() == 256);
        CHECK(controller.datasetModel()->rowCount() == 256 * 3);
    }
}

TEST_CASE("the image defaults come from the metadata, not from the data",
          "[example][images]")
{
    gui::AppController controller;
    REQUIRE(h5test::openFileAndSettle(controller, QString::fromStdString(example().path())));
    auto* image = controller.datasetImage();

    SECTION("IMAGE_MINMAXRANGE fixes the black and white points")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/images/gray_512x512")));
        CHECK_FALSE(image->autoRange());
        CHECK(image->rangeMinimum() == 0.0);
        CHECK(image->rangeMaximum() == 255.0);
        // The data itself never reaches 255; taking the range from the file is
        // what keeps two images of the same scene comparable.
        CHECK(image->maximum() < 255.0);
    }

    SECTION("IMAGE_WHITE_IS_ZERO flips the ramp")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/images/gray_white_is_zero")));
        CHECK(image->invert());
        CHECK_FALSE(image->autoRange());
        CHECK(image->rangeMaximum() == 248.0);
    }

    SECTION("a picture that gives no range is drawn against its whole datatype")
    {
        // The spec's attribute is optional and this one omits it, so the
        // datatype answers instead: a byte raster runs 0 to 255 whatever this
        // particular frame happens to reach. Its own indices stop well short
        // of the top, and stretching them to fill the ramp would be the viewer
        // inventing contrast the file never claimed.
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/images/indexed_64x64")));
        CHECK_FALSE(image->autoRange());
        CHECK(image->rangeMinimum() == 0.0);
        CHECK(image->rangeMaximum() == 255.0);
        CHECK(image->maximum() < 255.0);
    }

    SECTION("a dataset that says nothing gets the range read off its values")
    {
        // Not a picture at all -- a float field with no CLASS -- so there is
        // no datatype span to draw it against and its own extent is the only
        // scale there is.
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/images/field_256x256")));
        CHECK(image->autoRange());
        CHECK_FALSE(image->invert());
    }

    SECTION("and the settings do not leak into the next selection")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/images/gray_white_is_zero")));
        REQUIRE(image->invert());
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/images/field_256x256")));
        CHECK_FALSE(image->invert());
        CHECK(image->autoRange());
    }
}

TEST_CASE("only the colours the reader kept are painted", "[example][images]")
{
    gui::AppController controller;
    REQUIRE(h5test::openFileAndSettle(controller, QString::fromStdString(example().path())));
    auto* image = controller.datasetImage();
    REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/images/gray_512x512")));

    // The whole ramp to begin with: black at one end, white at the other.
    const QImage whole = image->render();
    REQUIRE(!whole.isNull());

    const auto darkest = [](const QImage& raster) {
        int found = 255;
        for (int y = 0; y < raster.height(); ++y) {
            for (int x = 0; x < raster.width(); ++x) {
                found = std::min(found, qGray(raster.pixel(x, y)));
            }
        }
        return found;
    };
    const auto brightest = [](const QImage& raster) {
        int found = 0;
        for (int y = 0; y < raster.height(); ++y) {
            for (int x = 0; x < raster.width(); ++x) {
                found = std::max(found, qGray(raster.pixel(x, y)));
            }
        }
        return found;
    };

    SECTION("the top half of the ramp is the top half of the greys")
    {
        image->setRampBegin(0.5);
        const QImage kept = image->render();
        // Nothing below the middle of the ramp is painted any more, and the
        // picture still reaches the top: a stretch of the ramp is a stretch of
        // the colours, not a stretch of the values.
        CHECK(darkest(kept) >= 126);
        CHECK(brightest(kept) >= brightest(whole) - 1);
    }

    SECTION("a narrow band is a narrow spread of colour")
    {
        image->setRampBegin(0.4);
        image->setRampEnd(0.6);
        const QImage kept = image->render();
        CHECK(darkest(kept) >= 101);
        CHECK(brightest(kept) <= 154);
    }

    SECTION("the band is a stretch of the ramp, not of the data")
    {
        // Where a value falls on the ramp is the value range's business, and
        // narrowing the colours does not move it: the extremes still land on
        // the ends of what is kept.
        image->setRampBegin(0.25);
        image->setRampEnd(0.75);
        const QImage kept = image->render();
        CHECK(darkest(kept) >= 63);
        CHECK(darkest(kept) <= 66);
    }

    SECTION("it is clamped to the ramp it is a stretch of")
    {
        image->setRampBegin(-1.0);
        image->setRampEnd(4.0);
        CHECK(image->rampBegin() == 0.0);
        CHECK(image->rampEnd() == 1.0);
    }
}

TEST_CASE("a compound is read apart, and as JSON", "[example][types]")
{
    const auto file = openExample();

    SECTION("a nested compound opens out one level at a time")
    {
        const h5test::Dataset ds(file, "/types/compound/nested");
        const h5core::ElementValue element = ds.readElement({0});

        REQUIRE(element.fields.size() == 6);
        CHECK(element.fields[0].name == "station");
        CHECK(element.fields[0].value == "ST-000");
        // A member that is itself a compound is one field, printed whole. The
        // JSON below is where its own members are separated out again.
        CHECK(element.fields[2].name == "position");
        CHECK_THAT(element.fields[2].type, ContainsSubstring("compound"));
        // An enum member reads as its symbol in both forms, because the name is
        // what the file gave the value.
        CHECK(element.fields[4].value == "BAD");

        CHECK_THAT(element.json, ContainsSubstring(R"("station": "ST-000")"));
        CHECK_THAT(element.json,
                   ContainsSubstring(R"("position": {"x": 0, "y": 0, "z": 0})"));
        CHECK_THAT(element.json, ContainsSubstring(R"("samples": [0, 0.25, 0.5, 0.75])"));
        CHECK_THAT(element.json, ContainsSubstring(R"("quality": "BAD")"));
    }

    SECTION("a value JSON has no literal for is quoted rather than dropped")
    {
        // `null` would say the value is absent, which is a different statement
        // from the one the file makes.
        const h5test::Dataset ds(file, "/data/special_floats");
        std::vector<std::string> rendered;
        for (hsize_t i = 0; i < ds.info().shape.front(); ++i) {
            rendered.push_back(ds.readElement({i}).json);
        }
        CHECK(std::find(rendered.begin(), rendered.end(), "\"nan\"") != rendered.end());
        CHECK(std::find(rendered.begin(), rendered.end(), "\"inf\"") != rendered.end());
        CHECK(std::find(rendered.begin(), rendered.end(), "\"-inf\"") != rendered.end());
    }

    SECTION("the viewer reports a compound as one, and hands out its cells")
    {
        gui::AppController controller;
        REQUIRE(h5test::openFileAndSettle(controller, QString::fromStdString(example().path())));
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/types/compound/table_4x5")));
        CHECK(controller.datasetIsCompound());
        CHECK_FALSE(controller.datasetIsNumeric());

        auto* table = qobject_cast<gui::DatasetTableModel*>(controller.datasetModel());
        REQUIRE(table != nullptr);
        const QVariantMap element = table->elementAt(1, 2);
        CHECK(element.value(QStringLiteral("label")).toString()
              == QStringLiteral("[1,2]"));
        CHECK(element.value(QStringLiteral("json")).toString()
              == QStringLiteral(R"({"id": 7, "value": 0.875})"));
        CHECK(element.value(QStringLiteral("fields")).toList().size() == 2);

        // A cell that is not there is not an error, it is nothing.
        CHECK(table->elementAt(99, 0).isEmpty());
    }
}

TEST_CASE("the colour axis comes from the file, and stays the reader's",
          "[example][images]")
{
    gui::AppController controller;
    REQUIRE(h5test::openFileAndSettle(controller, QString::fromStdString(example().path())));
    auto* image = controller.datasetImage();

    SECTION("a truecolour image opens in colour, on the axis the spec names")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/images/rgb_256x256x3")));
        CHECK(image->channelSelectable());
        CHECK(image->channelDimension() == 2);
        CHECK(image->channelCount() == 3);
        CHECK(image->colorMode() == gui::DatasetImage::ColorMode::Rgb);
        CHECK(image->redIndex() == 0);
        CHECK(image->greenIndex() == 1);
        CHECK(image->blueIndex() == 2);
        // Three planes of one table, so the picture is the picture and not a
        // third of it.
        CHECK(image->width() == 256);
        CHECK(image->height() == 256);

        // The channels really are read separately: this image is a hue ramp,
        // so no two of them agree.
        const QImage raster = image->render();
        REQUIRE_FALSE(raster.isNull());
        const QRgb pixel = raster.pixel(200, 40);
        CHECK_FALSE(qRed(pixel) == qGreen(pixel));
    }

    SECTION("a planar truecolour image takes the first dimension instead")
    {
        REQUIRE(
            h5test::selectAndSettle(controller, QStringLiteral("/images/rgb_planar_3x256x256")));
        CHECK(image->channelDimension() == 0);
        CHECK(image->colorMode() == gui::DatasetImage::ColorMode::Rgb);
    }

    SECTION("a four-component truecolour image opens as RGBA")
    {
        // The bug: it opened as RGB, so a raster the file stored with a
        // coverage was drawn as though it were solid and the reader had to
        // know to go and ask for the fourth plane.
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/images/rgba_128x128x4")));
        CHECK(image->channelDimension() == 2);
        CHECK(image->channelCount() == 4);
        CHECK(image->colorMode() == gui::DatasetImage::ColorMode::Rgba);
        CHECK(image->redIndex() == 0);
        CHECK(image->greenIndex() == 1);
        CHECK(image->blueIndex() == 2);
        CHECK(image->alphaIndex() == 3);

        // And the coverage is drawn: this one falls off towards the edges, so
        // the corners are clear and the middle is solid.
        const QImage raster = image->render();
        REQUIRE_FALSE(raster.isNull());
        REQUIRE(raster.format() == QImage::Format_ARGB32);
        CHECK(qAlpha(raster.pixel(0, 0)) == 0);
        CHECK(qAlpha(raster.pixel(64, 64)) > 200);

        // A three-deep axis still opens as RGB, which is all it can be.
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/images/rgb_256x256x3")));
        CHECK(image->colorMode() == gui::DatasetImage::ColorMode::Rgb);
    }

    SECTION("a single-channel image has no colour axis to choose")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/images/indexed_64x64")));
        // Rank 2 is exactly the two dimensions the picture is made of, so the
        // mode is fixed to grayscale whatever is asked for.
        CHECK_FALSE(image->channelSelectable());
        CHECK(image->channelDimension() == -1);
        image->setColorMode(gui::DatasetImage::ColorMode::Rgb);
        CHECK(image->colorMode() == gui::DatasetImage::ColorMode::Grayscale);
        CHECK(image->channelChoices().size() == 1); // "none", and nothing else
    }

    SECTION("a file wrong about its own image gets no colour axis either")
    {
        // Truecolour at rank 2, which no truecolour image can be. The tag
        // stands, because the file does say it; the arrangement does not.
        REQUIRE(
            h5test::selectAndSettle(controller, QStringLiteral("/images/mislabelled_truecolor")));
        CHECK(image->channelDimension() == -1);
        CHECK(image->colorMode() == gui::DatasetImage::ColorMode::Grayscale);
    }

    SECTION("a reader can name a colour axis the file never mentioned")
    {
        // Twelve bands, no Image spec attributes at all: the default is the
        // ordinary table, four thousand rows of twelve columns.
        REQUIRE(h5test::selectAndSettle(controller, 
            QStringLiteral("/images/multispectral_64x64x12")));
        REQUIRE(image->channelDimension() == -1);
        REQUIRE(controller.datasetModel()->rowCount() == 64 * 64);
        const QString slice = controller.sliceExpression();

        image->setChannelDimension(2);
        CHECK(image->channelCount() == 12);

        // The picture arranges itself around that axis -- 64 by 64, out of an
        // arrangement that could not have shown one, since a table twelve
        // columns wide with the colour axis held is a single column.
        CHECK(image->width() == 64);
        CHECK(image->height() == 64);

        // ...and the table is untouched. Naming a colour axis is a statement
        // about the picture; it used to be written back into the data settings
        // panel, which changed the slice under the grid and the plot as well.
        CHECK(controller.datasetModel()->rowCount() == 64 * 64);
        CHECK(controller.datasetModel()->columnCount() == 12);
        CHECK(controller.sliceExpression() == slice);

        // The channels are indices into the whole dimension, whatever the
        // table has selected along it.
        image->setColorMode(gui::DatasetImage::ColorMode::Rgb);
        CHECK(image->redIndex() == 0);
        CHECK(image->greenIndex() == 1);
        CHECK(image->blueIndex() == 2);
        image->setRedIndex(0);
        image->setGreenIndex(5);
        image->setBlueIndex(11);
        CHECK(image->width() == 64);
        CHECK(image->height() == 64);
        CHECK_FALSE(image->render().isNull());
        CHECK(controller.sliceExpression() == slice);
    }

    SECTION("RGB opens on the first three channels, in order")
    {
        // The bug: with no colour axis there was no extent to hold the four
        // indices inside, so they were all clamped to zero -- and by the time
        // a reader named an axis, red, green and blue were the same channel
        // and a truecolour picture came out grey.
        REQUIRE(h5test::selectAndSettle(controller, 
            QStringLiteral("/images/multispectral_64x64x12")));
        REQUIRE(image->channelDimension() == -1);

        image->setChannelDimension(2);
        image->setColorMode(gui::DatasetImage::ColorMode::Rgb);
        CHECK(image->colorMode() == gui::DatasetImage::ColorMode::Rgb);
        CHECK(image->redIndex() == 0);
        CHECK(image->greenIndex() == 1);
        CHECK(image->blueIndex() == 2);

        // A picture whose three channels differ really is in colour.
        const QImage raster = image->render();
        REQUIRE_FALSE(raster.isNull());
    }

    SECTION("four channels can be read as RGBA")
    {
        REQUIRE(h5test::selectAndSettle(controller, 
            QStringLiteral("/images/multispectral_64x64x12")));
        image->setChannelDimension(2);
        image->setColorMode(gui::DatasetImage::ColorMode::Rgba);
        CHECK(image->colorMode() == gui::DatasetImage::ColorMode::Rgba);
        CHECK(image->alphaIndex() == 3);

        const QImage raster = image->render();
        REQUIRE_FALSE(raster.isNull());
        REQUIRE(raster.format() == QImage::Format_ARGB32);
        // The fourth plane really is read as coverage: this band is not
        // uniformly full, so the picture cannot be entirely opaque.
        bool translucent = false;
        for (int y = 0; y < raster.height() && !translucent; ++y) {
            for (int x = 0; x < raster.width(); ++x) {
                if (qAlpha(raster.pixel(x, y)) < 255) {
                    translucent = true;
                    break;
                }
            }
        }
        CHECK(translucent);

        // A colour axis three channels deep has no coverage to read, so the
        // mode reports what it can actually draw rather than what was asked.
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/images/rgb_256x256x3")));
        image->setColorMode(gui::DatasetImage::ColorMode::Rgba);
        CHECK(image->colorMode() == gui::DatasetImage::ColorMode::Rgb);
    }

    SECTION("an index past the end of the colour axis is held inside it")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/images/rgb_256x256x3")));
        image->setBlueIndex(97);
        CHECK(image->blueIndex() == 2);
    }

    SECTION("and none of it leaks into the next selection")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/images/rgb_256x256x3")));
        REQUIRE(image->colorMode() == gui::DatasetImage::ColorMode::Rgb);
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/images/field_256x256")));
        CHECK(image->channelDimension() == -1);
        CHECK(image->colorMode() == gui::DatasetImage::ColorMode::Grayscale);
    }
}

TEST_CASE("only a dataset that says it is an image is treated as one",
          "[example][images]")
{
    const auto file = openExample();

    SECTION("the tag follows the attribute, not the shape")
    {
        // Tagged, and rearranged.
        CHECK(h5test::Dataset(file, "/images/rgb_256x256x3").info().image.has_value());
        CHECK(h5test::Dataset(file, "/images/gray_512x512").info().image.has_value());
        // Image-shaped and silent about it: rank 3 with three trailing
        // channels, and rank 4 of RGB frames.
        CHECK_FALSE(h5test::Dataset(file, "/images/multispectral_64x64x12")
                        .info()
                        .image.has_value());
        CHECK_FALSE(
            h5test::Dataset(file, "/images/stack_8x64x64x3").info().image.has_value());
        // CLASS="PALETTE" is not CLASS="IMAGE".
        CHECK_FALSE(h5test::Dataset(file, "/images/palette").info().image.has_value());
        CHECK_FALSE(h5test::Dataset(file, "/data/matrix").info().image.has_value());
    }

    SECTION("an untagged raster keeps the ordinary default")
    {
        gui::AppController controller;
        REQUIRE(h5test::openFileAndSettle(controller, QString::fromStdString(example().path())));
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/images/stack_8x64x64x3")));
        CHECK(controller.datasetRank() == 4);
        CHECK(controller.sliceExpression() ==
              QStringLiteral("/images/stack_8x64x64x3[:, :, :, :]"));
        CHECK(controller.datasetModel()->rowCount() == 8 * 64 * 64);
    }

    SECTION("a file wrong about its own image is tagged but not rearranged")
    {
        const h5test::Dataset dataset(file, "/images/mislabelled_truecolor");
        REQUIRE(dataset.info().image.has_value());
        CHECK(dataset.info().image->subclass == h5core::ImageSubclass::Truecolor);
        // Rank 2 is not a shape a truecolour image can have.
        CHECK_FALSE(dataset.info().image->shapeMatches);

        gui::AppController controller;
        REQUIRE(h5test::openFileAndSettle(controller, QString::fromStdString(example().path())));
        REQUIRE(
            h5test::selectAndSettle(controller, QStringLiteral("/images/mislabelled_truecolor")));
        CHECK(controller.sliceExpression() ==
              QStringLiteral("/images/mislabelled_truecolor[:, :]"));
    }
}

TEST_CASE("the interlace and the origin are read as the spec defines them",
          "[example][images]")
{
    const auto file = openExample();

    const auto pixel = h5test::Dataset(file, "/images/rgb_256x256x3").info().image;
    REQUIRE(pixel.has_value());
    CHECK(pixel->interlace == h5core::Interlace::Pixel);
    CHECK(pixel->rowDim == 0);
    CHECK(pixel->columnDim == 1);
    CHECK(pixel->channelDim == 2);
    CHECK(pixel->version == "1.2");

    const auto plane = h5test::Dataset(file, "/images/rgb_planar_3x256x256").info().image;
    REQUIRE(plane.has_value());
    CHECK(plane->interlace == h5core::Interlace::Plane);
    CHECK(plane->channelDim == 0);
    CHECK(plane->rowDim == 1);
    CHECK(plane->columnDim == 2);

    // DISPLAY_ORIGIN is read and reported, and only the spec's default is
    // actually drawn: the raster is never flipped behind the reader's back.
    const auto flipped = h5test::Dataset(file, "/images/gray_white_is_zero").info().image;
    REQUIRE(flipped.has_value());
    CHECK(flipped->displayOrigin == "LL");
    CHECK_FALSE(flipped->originHonoured);
    CHECK(h5test::Dataset(file, "/images/gray_512x512").info().image->originHonoured);
}

TEST_CASE("the tree tags the datasets that declare themselves images",
          "[example][images][tree]")
{
    gui::AppController controller;
    REQUIRE(h5test::openFileAndSettle(controller, QString::fromStdString(example().path())));
    QAbstractItemModel* tree = controller.treeModel();

    const QModelIndex images =
        indexForName(tree, QModelIndex{}, QStringLiteral("images"));
    REQUIRE(images.isValid());

    const auto tagged = [&](const char* name) {
        const QModelIndex node = indexForName(tree, images, QString::fromUtf8(name));
        REQUIRE(node.isValid());
        return h5test::settledData(tree, node, gui::H5TreeModel::IsImageRole).toBool();
    };

    CHECK(tagged("rgb_256x256x3"));
    CHECK(tagged("gray_512x512"));
    CHECK(tagged("indexed_64x64"));
    // Tagged even though the claim does not match the shape: the file does say
    // it is an image, and the Information panel is where the disagreement is
    // spelled out.
    CHECK(tagged("mislabelled_truecolor"));
    CHECK_FALSE(tagged("palette"));
    CHECK_FALSE(tagged("multispectral_64x64x12"));
    CHECK_FALSE(tagged("stack_8x64x64x3"));
}

// A tag is one letter on a tree row. A letter says nothing on its own, so what
// makes the tag worth drawing is what it says when the pointer rests on it --
// and that has to be the thing the reader would ask next, which is never "this
// has attributes" but how many, and never "this is a link" but where to.
TEST_CASE("every tree tag carries the fact behind it",
          "[example][tree][tags]")
{
    gui::AppController controller;
    REQUIRE(h5test::openFileAndSettle(controller, QString::fromStdString(example().path())));
    QAbstractItemModel* tree = controller.treeModel();

    const QModelIndex images =
        indexForName(tree, QModelIndex{}, QStringLiteral("images"));
    const QModelIndex links =
        indexForName(tree, QModelIndex{}, QStringLiteral("links"));
    REQUIRE(images.isValid());
    REQUIRE(links.isValid());

    const auto at = [&](const QModelIndex& parent, const char* name) {
        const QModelIndex node = indexForName(tree, parent, QString::fromUtf8(name));
        REQUIRE(node.isValid());
        return node;
    };
    const auto role = [&](const QModelIndex& node, int which) {
        return h5test::settledData(tree, node, which);
    };

    // [I] -- which kind of picture the file says it is. The Data Viewer opens
    // on a raster rather than on a table of channels because of this word.
    const QModelIndex rgb = at(images, "rgb_256x256x3");
    CHECK(role(rgb, gui::H5TreeModel::ImageSubclassRole).toString()
          == QStringLiteral("Truecolour"));
    CHECK(role(at(images, "indexed_64x64"),
               gui::H5TreeModel::ImageSubclassRole).toString()
          == QStringLiteral("Indexed"));
    // Nothing that is not an image claims to be one.
    CHECK(role(at(images, "palette"),
               gui::H5TreeModel::ImageSubclassRole).toString().isEmpty());

    // [A] -- how many, not merely that there are some.
    CHECK(role(rgb, gui::H5TreeModel::HasAttributesRole).toBool());
    CHECK(role(rgb, gui::H5TreeModel::AttributeCountRole).toInt() >= 1);
    const QModelIndex plain = at(images, "field_256x256");
    CHECK(role(plain, gui::H5TreeModel::AttributeCountRole).toInt() == 2);

    // [L] -- where it leads, and for a broken one which half is missing. A
    // hard link leads nowhere but to itself and says nothing.
    const auto description = [&](const char* name) {
        return role(at(links, name),
                    gui::H5TreeModel::LinkDescriptionRole).toString();
    };
    CHECK_THAT(description("soft_to_matrix").toStdString(),
               ContainsSubstring("Soft link") && ContainsSubstring("/data/matrix"));
    CHECK(description("hard_to_matrix").isEmpty());

    // The two that fail, which are the rows drawn in red. An external link has
    // two things that can be absent and the sentence has to say which.
    CHECK_FALSE(role(at(links, "soft_dangling"),
                     gui::H5TreeModel::LinkResolvesRole).toBool());
    CHECK_THAT(description("soft_dangling").toStdString(),
               ContainsSubstring("no object at that path"));
    CHECK_THAT(description("external_missing_file").toStdString(),
               ContainsSubstring("External link")
                   && ContainsSubstring("the file or the object is missing"));
}

// A count and its noun are one phrase. "1 items" and "Element size 1 bytes"
// are not phrases; they are a number with a fixed string stapled to it.
TEST_CASE("counts read in the singular when there is one of them",
          "[example][tree][info]")
{
    gui::AppController controller;
    REQUIRE(h5test::openFileAndSettle(controller, QString::fromStdString(example().path())));
    QAbstractItemModel* tree = controller.treeModel();

    // /links/loop holds exactly one member.
    const QModelIndex links =
        indexForName(tree, QModelIndex{}, QStringLiteral("links"));
    REQUIRE(links.isValid());
    const QModelIndex loop = indexForName(tree, links, QStringLiteral("loop"));
    REQUIRE(loop.isValid());
    CHECK(h5test::settledData(tree, loop, gui::H5TreeModel::MetaRole).toString()
          == QStringLiteral("1 item"));

    // ...and a group with more than one still reads in the plural.
    CHECK(h5test::settledData(tree, links, gui::H5TreeModel::MetaRole).toString()
          == QStringLiteral("12 items"));

    // uint8 is one byte wide; int32 is four.
    REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/images/mislabelled_truecolor")));
    CHECK(infoRow(controller, QStringLiteral("Element size"))
          == QStringLiteral("1 byte"));
    REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/data/big_endian_int32")));
    CHECK(infoRow(controller, QStringLiteral("Element size"))
          == QStringLiteral("4 bytes"));
}

TEST_CASE("every row of the tree says something about itself",
          "[example][tree]")
{
    gui::AppController controller;
    REQUIRE(h5test::openFileAndSettle(controller, QString::fromStdString(example().path())));
    QAbstractItemModel* tree = controller.treeModel();

    // The readout is a column, and a column with holes in it is not one. Every
    // kind the example file carries has to reach a branch of ensureReadout that
    // has something to print -- named datatypes are the two that used not to.
    int rows = 0;
    const auto walk = [&](auto&& self, const QModelIndex& parent) -> void {
        const int count = h5test::settledRowCount(tree, parent);
        for (int row = 0; row < count; ++row) {
            const QModelIndex node = tree->index(row, 0, parent);
            const QString path = h5test::settledData(
                tree, node, gui::H5TreeModel::PathRole).toString();
            const QString meta = h5test::settledData(
                tree, node, gui::H5TreeModel::MetaRole).toString();
            INFO(path.toStdString());
            CHECK_FALSE(meta.isEmpty());
            ++rows;
            self(self, node);
        }
    };
    walk(walk, QModelIndex{});
    CHECK(rows > 100);

    const QModelIndex committed =
        indexForName(tree, QModelIndex{}, QStringLiteral("committed"));
    REQUIRE(committed.isValid());
    const QModelIndex celsius =
        indexForName(tree, committed, QStringLiteral("celsius_t"));
    REQUIRE(celsius.isValid());
    // A committed type has no shape and no children; what it is *of* is the
    // whole of what the row has to say.
    CHECK(h5test::settledData(tree, celsius, gui::H5TreeModel::MetaRole).toString()
          == QStringLiteral("float64"));
}

TEST_CASE("the hierarchy survives its own awkward shapes", "[example][tree]")
{
    const auto file = openExample();

    SECTION("names HDF5 allows and a viewer must not assume away")
    {
        const auto names = file.children("/stress/awkward_names");
        CHECK(names.size() == 10);
        CHECK(find(names, "with space") != nullptr);
        CHECK(find(names, "with.dots.everywhere") != nullptr);
        CHECK(find(names, "測定値") != nullptr);
    }

    SECTION("a rank-12 shape is carried, singleton dimensions and all")
    {
        const h5test::Dataset dataset(file, "/data/rank12");
        CHECK(dataset.info().rank() == 12);
        CHECK(dataset.info().elementCount() == 192);
    }

    SECTION("a group with many children lists them all")
    {
        CHECK(file.children("/stress/many_children_512").size() == 512);
        CHECK(file.children("/stress/many_children_4096").size() == 4096);
    }

    SECTION("a group's size is the same whether it is counted or listed")
    {
        for (const char* path : {"/stress/many_children_4096", "/stress/nested_16x64",
                                 "/stress/empty_group", "/"}) {
            INFO(path);
            CHECK(file.memberCount(path) == file.children(path).size());
        }
    }
}

TEST_CASE("a loop in the file is shown once and never followed", "[example][tree]")
{
    // Both ways a name can lead back to where it came from. Neither may be
    // expandable, or the tree recurses until it runs out of memory.
    const h5test::Reader file(example().path());
    gui::H5TreeModel tree;
    tree.open();
    REQUIRE(gui::H5Thread::instance().drain());

    const QModelIndex links = indexForName(&tree, QModelIndex{}, QStringLiteral("links"));
    REQUIRE(links.isValid());

    SECTION("a soft link pointing at its own container")
    {
        // The case that has no identity at all until the link is followed,
        // which is why this is settled when a row is identified and not when
        // its parent is listed.
        const QModelIndex self =
            indexForName(&tree, links, QStringLiteral("soft_to_self"));
        REQUIRE(self.isValid());
        CHECK(h5test::settledData(&tree, self, gui::H5TreeModel::IsCyclicRole).toBool());
        CHECK_FALSE(h5test::settledHasChildren(&tree, self));
        CHECK(h5test::settledRowCount(&tree, self) == 0);
    }

    SECTION("a hard link from a subgroup back to its ancestor")
    {
        const QModelIndex loop = indexForName(&tree, links, QStringLiteral("loop"));
        REQUIRE(loop.isValid());
        const QModelIndex back =
            indexForName(&tree, loop, QStringLiteral("back_to_links"));
        REQUIRE(back.isValid());
        CHECK(h5test::settledData(&tree, back, gui::H5TreeModel::IsCyclicRole).toBool());
        CHECK_FALSE(h5test::settledHasChildren(&tree, back));
        CHECK(h5test::settledData(&tree, back, gui::H5TreeModel::MetaRole).toString()
              == QStringLiteral("cycle"));
    }
}

TEST_CASE("no HDF5 error stack reaches a reader", "[example][errors]")
{
    gui::AppController controller;
    REQUIRE(h5test::openFileAndSettle(controller, QString::fromStdString(example().path())));

    // The two datasets that cannot be read are the two that used to answer
    // with a stack trace. A message with "#0:" in it is a log line, not a
    // sentence, and neither belongs in front of a reader.
    for (const char* path : {"/types/time_unix", "/filters/unavailable_mandatory"}) {
        REQUIRE(h5test::selectAndSettle(controller, QString::fromUtf8(path)));
        const QString message = controller.datasetMessage();
        INFO(path << ": " << message.toStdString());
        CHECK_FALSE(message.isEmpty());
        CHECK_FALSE(message.contains(QStringLiteral("#0:")));
        CHECK_FALSE(message.contains(QLatin1Char('\n')));
    }
}

TEST_CASE("the tree costs what is on screen, not what is in the file",
          "[example][tree][cost]")
{
    // The property the whole design of H5TreeModel rests on, asserted in the
    // one unit that means the same thing on every machine: read syscalls.
    //
    // Every figure below used to be proportional to the size of the level being
    // looked at rather than to the size of the viewport, which is what made a
    // file of a few thousand datasets unusable. Nothing here settles inside a
    // measured region *per row* -- doing that would take the batching apart and
    // measure forty round trips instead of one, which is the opposite of what
    // is being defended. The thresholds are loose on purpose: what is held is
    // the shape of the cost, not a number.
    const auto baseline = readSyscalls();
    if (!baseline.has_value()) {
        SKIP("/proc/self/io is not available on this platform");
    }

    gui::H5TreeModel tree;
    const h5test::Reader file(example().path());
    tree.open();
    h5test::settle();

    const QModelIndex stress = h5test::reveal(tree, QStringLiteral("/stress"));
    REQUIRE(stress.isValid());

    SECTION("expanding a group of four thousand does not open four thousand objects")
    {
        const QModelIndex wide =
            h5test::reveal(tree, QStringLiteral("/stress/many_children_4096"));
        REQUIRE(wide.isValid());

        const long long before = *readSyscalls();
        const int rows = h5test::settledRowCount(&tree, wide); // what the click costs
        const long long spent = *readSyscalls() - before;

        CHECK(rows == 4096);
        INFO("reads to list 4096 members: " << spent);
        // Reading the link table. Opening every member, which is what this used
        // to do, is 4096 reads and more.
        CHECK(spent < rows / 4);
    }

    SECTION("drawing a screenful of it costs a screenful, not a group")
    {
        const QModelIndex wide =
            h5test::reveal(tree, QStringLiteral("/stress/many_children_4096"));
        REQUIRE(h5test::settledRowCount(&tree, wide) == 4096);

        constexpr int kViewport = 40;
        const long long before = *readSyscalls();
        // Asked for the way a viewport asks: every row of one layout pass, and
        // then the frame ends. The model turns that into one job.
        for (int row = 0; row < kViewport; ++row) {
            const QModelIndex index = tree.index(row, 0, wide);
            (void)tree.data(index, gui::H5TreeModel::IsGroupRole);
            (void)tree.data(index, gui::H5TreeModel::HasAttributesRole);
            (void)tree.data(index, gui::H5TreeModel::MetaRole);
        }
        h5test::settle();
        const long long spent = *readSyscalls() - before;

        INFO("reads to draw 40 of 4096 rows: " << spent);
        CHECK(spent < 4 * kViewport);
        // ...and they really were described.
        CHECK_FALSE(tree.data(tree.index(0, 0, wide), gui::H5TreeModel::MetaRole)
                        .toString()
                        .isEmpty());
    }

    SECTION("a member count beside a group row is not taken by counting")
    {
        // Sixteen groups of sixty-four. Saying how many members each one holds
        // by listing it costs 1024 link resolutions to draw 16 rows, which is
        // quadratic in the shape acquisition files actually have.
        const QModelIndex nested =
            h5test::reveal(tree, QStringLiteral("/stress/nested_16x64"));
        REQUIRE(nested.isValid());
        const int rows = h5test::settledRowCount(&tree, nested);
        REQUIRE(rows == 16);

        const long long before = *readSyscalls();
        for (int row = 0; row < rows; ++row) {
            (void)tree.data(tree.index(row, 0, nested), gui::H5TreeModel::MetaRole);
        }
        h5test::settle();
        const long long spent = *readSyscalls() - before;

        INFO("reads to draw 16 member counts: " << spent);
        CHECK(spent < 4 * rows);
        CHECK(tree.data(tree.index(0, 0, nested), gui::H5TreeModel::MetaRole).toString()
              == QStringLiteral("64 items"));
    }

    SECTION("a row that has already been drawn is free the second time")
    {
        const QModelIndex wide =
            h5test::reveal(tree, QStringLiteral("/stress/many_children_4096"));
        REQUIRE(h5test::settledRowCount(&tree, wide) == 4096);
        const QModelIndex first = tree.index(0, 0, wide);
        (void)h5test::settledData(&tree, first, gui::H5TreeModel::MetaRole);

        const long long before = *readSyscalls();
        for (int i = 0; i < 100; ++i) {
            (void)tree.data(first, gui::H5TreeModel::MetaRole);
            (void)tree.data(first, gui::H5TreeModel::AttributeCountRole);
            (void)tree.data(first, gui::H5TreeModel::IsImageRole);
        }
        // Scrolling back over ground already seen reads nothing at all, and
        // asks the HDF5 thread for nothing either: the readout is computed once
        // per node and kept.
        CHECK(*readSyscalls() - before <= 2);
        CHECK(gui::H5Thread::instance().outstanding() == 0);
    }
}
