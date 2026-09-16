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
#include "gui/DatasetImage.hpp"
#include "gui/DatasetPlot.hpp"
#include "gui/DatasetLookup.hpp"
#include "gui/DatasetTableModel.hpp"
#include "gui/H5Thread.hpp"
#include "gui/H5TreeModel.hpp"
#include "gui/PostprocessModel.hpp"
#include "gui/PlotProjection.hpp"
#include "gui/TableSetupModel.hpp"
#include "postproc/MemberPath.hpp"

#include "h5core/Attribute.hpp"
#include "h5core/Dataset.hpp"
#include "h5core/Error.hpp"
#include "h5core/File.hpp"
#include "support/AsyncModels.hpp"
#include "support/H5Reader.hpp"
#include "support/MemberChain.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <QImage>
#include <QVariantMap>

#include <algorithm>
#include <cmath>
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
        // generic_string for the reason TempFile gives: the QML suite reads
        // this through QML, where a separator is "/" whatever the platform.
        return (directory_ / "example.h5").generic_string();
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

const h5core::NodeInfo* find(const std::vector<h5core::NodeInfo>& nodes, const std::string& name)
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
QModelIndex indexForName(QAbstractItemModel* tree, const QModelIndex& parent, const QString& name)
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

/// The indented rows of the datatype panel: the type opened out until nothing
/// is left but base types, each as "depth label value".
///
/// Flattened to strings on purpose. What this has to state is an order and a
/// nesting, and a list of sentences states both at once -- where three parallel
/// vectors would state them in a form nobody reads at a glance.
QStringList typeTree(const gui::AppController& controller)
{
    QStringList tree;
    for (const QVariant& panel : controller.infoPanels()) {
        const QVariantMap fields = panel.toMap();
        if (fields.value("title").toString() != QStringLiteral("datatype")) {
            continue;
        }
        for (const QVariant& row : fields.value("rows").toList()) {
            const QVariantMap cells = row.toMap();
            const int depth = cells.value("depth").toInt();
            if (depth > 0) {
                tree << QStringLiteral("%1 %2 %3")
                            .arg(depth)
                            .arg(cells.value("label").toString(),
                                 cells.value("value").toString());
            }
        }
    }
    return tree;
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
    CHECK(infoRow(controller, QStringLiteral("File")) == QStringLiteral("example_external.h5"));
    CHECK(infoRow(controller, QStringLiteral("Target")) == QStringLiteral("/no/such/object"));
    CHECK_FALSE(controller.datasetTabVisible());
    CHECK_FALSE(controller.metadataTabVisible());
    // No object behind the name means no attribute count to state.
    CHECK_FALSE(controller.statusRight().contains(QStringLiteral(" attrs")));
}

TEST_CASE("a null dataspace holds nothing, not one unreadable value", "[example][dataspace]")
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

TEST_CASE("datatypes HDF5 2.x added, and widths a switch would miss", "[example][datatype]")
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

TEST_CASE("a type the library cannot convert is refused before it is read", "[example][datatype]")
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

TEST_CASE("a missing filter blocks the data only when it is mandatory", "[example][filters]")
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
        CHECK(dataset.info().externalFiles == std::vector<std::string>{"example_raw.bin"});
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

TEST_CASE("an attribute holding no elements is not an unreadable one", "[example][attributes]")
{
    const auto file = openExample();
    CHECK(attributeValue(file, "/", "empty_attribute") == "[]");
    // The neighbours still read, so the empty case is not being special-cased
    // into silence.
    CHECK(attributeValue(file, "/", "format_version") == "3");
    CHECK(attributeValue(file, "/", "quality") == "GOOD");
    CHECK_THAT(attributeValue(file, "/", "long_attribute"), ContainsSubstring("(744 more)"));
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
                              controller.datasetModel()->index(99999, 9999), Qt::DisplayRole)
              .toString() == QStringLiteral("7"));

    const auto* table = qobject_cast<const gui::DatasetTableModel*>(controller.datasetModel());
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

    const auto* table = qobject_cast<const gui::DatasetTableModel*>(controller.datasetModel());
    REQUIRE(table != nullptr);

    SECTION("pixel-interleaved truecolour: height on y, width on x, one channel")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/images/rgb_256x256x3")));
        // Not 65,536 rows of three channels, which is what the shape alone
        // would give and what no reader wants to look at.
        CHECK(controller.datasetModel()->rowCount() == 256);
        CHECK(controller.datasetModel()->columnCount() == 256);
        CHECK(controller.sliceExpression() == QStringLiteral("/images/rgb_256x256x3[:, :, 0]"));
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
        CHECK(controller.sliceExpression() == QStringLiteral("/images/indexed_64x64[:, :]"));
    }

    SECTION("the pinned channel is a starting point, not a binding")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/images/rgb_256x256x3")));
        auto* setup = qobject_cast<gui::TableSetupModel*>(controller.tableSetupModel());
        REQUIRE(setup != nullptr);
        setup->setIndex(2, 2); // the blue channel
        CHECK(controller.sliceExpression() == QStringLiteral("/images/rgb_256x256x3[:, :, 2]"));
        CHECK(controller.datasetModel()->rowCount() == 256);

        // And the whole channel axis is still reachable: nothing is hidden,
        // only defaulted.
        setup->setMode(2, gui::TableSetupModel::All);
        CHECK(controller.datasetModel()->columnCount() == 256);
        CHECK(controller.datasetModel()->rowCount() == 256 * 3);
    }
}

TEST_CASE("the image defaults come from the metadata, not from the data", "[example][images]")
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

TEST_CASE("a compound's type resolves all the way down", "[example][types]")
{
    // The nested compound is the one type in the file that reaches every class
    // a member chain can land on: a fixed string, an integer, a compound, an
    // array, an enum and a float. Following it here is what says the tree is a
    // tree rather than one level with names on it.
    const auto file = openExample();
    const h5test::Dataset ds(file, "/types/compound/nested");
    const h5core::TypeInfo& type = ds.info().type;

    REQUIRE(type.cls == h5core::TypeClass::Compound);
    REQUIRE(type.members.size() == 6);

    const auto memberNamed = [&type](const std::string& name) {
        const auto it = std::find_if(
            type.members.begin(), type.members.end(),
            [&name](const h5core::TypeMember& m) { return m.name == name; });
        REQUIRE(it != type.members.end());
        return *it;
    };

    SECTION("a member that is a compound carries its own members")
    {
        const h5core::TypeMember position = memberNamed("position");
        REQUIRE(position.type.cls == h5core::TypeClass::Compound);
        REQUIRE(position.type.members.size() == 3);
        CHECK(position.type.members[0].name == "x");
        CHECK(position.type.members[2].name == "z");
        CHECK(position.type.members[1].type.cls == h5core::TypeClass::Float);
        // Offsets are within the member, not within the element that holds it.
        CHECK(position.type.members[0].offset == 0);
    }

    SECTION("a member that is an array carries its dimensions as numbers")
    {
        // The description has said "array[4] of float64" all along. What is new
        // is the 4 as a number, which is the axis `.samples` appends.
        const h5core::TypeMember samples = memberNamed("samples");
        REQUIRE(samples.type.cls == h5core::TypeClass::Array);
        REQUIRE(samples.type.arrayDims == std::vector<hsize_t>{4});
        REQUIRE(samples.type.base != nullptr);
        CHECK(samples.type.base->cls == h5core::TypeClass::Float);
        CHECK(samples.type.base->description == "float64");
    }

    SECTION("a member that is an enum keeps its symbols and gains no members")
    {
        const h5core::TypeMember quality = memberNamed("quality");
        REQUIRE(quality.type.cls == h5core::TypeClass::Enum);
        CHECK(quality.type.memberNames.size() == 3);
        CHECK(quality.type.members.empty());
    }

    SECTION("a member that is a string says which kind")
    {
        const h5core::TypeMember station = memberNamed("station");
        REQUIRE(station.type.cls == h5core::TypeClass::String);
        CHECK_FALSE(station.type.isVariableLength);
        CHECK(station.type.size == 16);
    }

    SECTION("a vlen carries what it holds one of")
    {
        const h5test::Dataset tags(file, "/types/vlen_int32");
        REQUIRE(tags.info().type.cls == h5core::TypeClass::VarLen);
        REQUIRE(tags.info().type.base != nullptr);
        CHECK(tags.info().type.base->cls == h5core::TypeClass::Integer);
    }
}

TEST_CASE("an array of structs is a member like any other", "[example][member]")
{
    // /types/compound/tracks is the one composition the rest of the file does
    // not have: an array member whose elements are themselves compounds. It is
    // where "an array member appends an axis" and "a chain goes on through a
    // compound" have to hold at once.
    const auto file = openExample();
    const h5test::Dataset whole(file, "/types/compound/tracks");
    const h5core::TypeInfo& type = whole.info().type;
    REQUIRE(whole.info().shape == std::vector<hsize_t>{4});

    SECTION("the axis it appends is the array's, and the chain goes on past it")
    {
        const h5test::Field x(file, "/types/compound/tracks",
                              h5test::chainOf(type, {"trail", "x"}));
        // Rank 2 out of a rank-1 dataset: the dataset's own axis, then the
        // three the array contributed.
        CHECK(x.info().shape == std::vector<hsize_t>{4, 3});
        CHECK(x.info().type.cls == h5core::TypeClass::Float);
        CHECK(x.info().isNumeric());

        // Track i, point p, was written {i + p, 2i + p, 3i + p}.
        const auto values = x.readNumericWindow({0, 0}, {4, 3});
        REQUIRE(values.values.size() == 12);
        for (hsize_t i = 0; i < 4; ++i) {
            for (hsize_t p = 0; p < 3; ++p) {
                INFO("track " << i << ", point " << p);
                CHECK(values.values[i * 3 + p]
                      == static_cast<double>(i) + static_cast<double>(p));
            }
        }
    }

    SECTION("the whole array member keeps its own axis and stays a compound")
    {
        const h5test::Field trail(file, "/types/compound/tracks",
                                  h5test::chainOf(type, {"trail"}));
        CHECK(trail.info().shape == std::vector<hsize_t>{4, 3});
        CHECK(trail.info().type.cls == h5core::TypeClass::Compound);
        // Three numbers per cell, so the grid prints the struct and the
        // compound pane opens it out -- exactly as the dataset itself does.
        CHECK_FALSE(trail.info().isNumeric());
    }

    SECTION("it is offered as a chain, under the name the file gave it")
    {
        const QStringList chains = postproc::memberChains(type);
        CHECK(chains
              == QStringList{QStringLiteral(".name"), QStringLiteral(".trail"),
                             QStringLiteral(".trail.x"), QStringLiteral(".trail.y"),
                             QStringLiteral(".trail.z"), QStringLiteral(".hops")});
        // And what is offered is what resolves, which is the only reason the
        // list is worth having.
        for (const QString& chain : chains) {
            INFO(chain.toStdString());
            CHECK(postproc::resolveMemberChain(chain, type).valid());
        }
    }

    SECTION("and its JSON opens the list out, because the list holds structs")
    {
        const h5core::ElementValue element = whole.readElement({1});
        CHECK_THAT(element.json, ContainsSubstring(R"("name": "T-01")"));
        CHECK_THAT(element.json,
                   ContainsSubstring("\"trail\": [\n    {\n      \"x\": 1,"));
        CHECK_THAT(element.json, ContainsSubstring(R"("hops": 10)"));
    }

    SECTION("the datatype panel draws the array's members under it")
    {
        gui::AppController controller;
        REQUIRE(h5test::openFileAndSettle(controller,
                                          QString::fromStdString(example().path())));
        REQUIRE(h5test::selectAndSettle(controller,
                                        QStringLiteral("/types/compound/tracks")));
        const QStringList tree = typeTree(controller);
        REQUIRE(tree.size() == 6);
        CHECK(tree[0].startsWith(QStringLiteral("1 name string")));
        CHECK(tree[1] == QStringLiteral("1 trail array[3] of compound {x, y, z}"));
        CHECK(tree[2] == QStringLiteral("2 x float64"));
        CHECK(tree[4] == QStringLiteral("2 z float64"));
        CHECK(tree[5] == QStringLiteral("1 hops int32"));
    }
}

TEST_CASE("the datatype panel draws a compound as a tree", "[example][info]")
{
    // The same type the case above walks, as the reader meets it: the tree is
    // that walk, printed. Asserted through infoPanels() because that is the
    // property QML binds.
    gui::AppController controller;
    REQUIRE(h5test::openFileAndSettle(controller,
                                      QString::fromStdString(example().path())));
    REQUIRE(h5test::selectAndSettle(controller,
                                    QStringLiteral("/types/compound/nested")));

    // The one-line answer stays where it was; the tree is the other entry.
    CHECK(infoRow(controller, QStringLiteral("Members"))
          == QStringLiteral("station, timestamp, position, samples, quality, weight"));

    const QStringList tree = typeTree(controller);
    REQUIRE(tree.size() == 10);

    SECTION("the members come in file order, one row each")
    {
        CHECK(tree[0] == QStringLiteral("1 station string (16 bytes)"));
        CHECK(tree[1] == QStringLiteral("1 timestamp int64"));
        CHECK(tree[9] == QStringLiteral("1 weight float32"));
    }

    SECTION("a member that is a compound carries its own members below it")
    {
        CHECK(tree[2] == QStringLiteral("1 position compound {x, y, z}"));
        CHECK(tree[3] == QStringLiteral("2 x float64"));
        CHECK(tree[4] == QStringLiteral("2 y float64"));
        CHECK(tree[5] == QStringLiteral("2 z float64"));
    }

    SECTION("an array is one row: it has already said what it holds")
    {
        // "array[4] of float64" resolves to float64 in the saying of it, so an
        // "element" row under it would be a level of indentation naming nothing.
        CHECK(tree[6] == QStringLiteral("1 samples array[4] of float64"));
        CHECK(tree[7].startsWith(QStringLiteral("1 quality")));
    }

    SECTION("an enum says what its numbers mean, which nothing else would")
    {
        CHECK(tree[7] == QStringLiteral("1 quality enum (3 values)"));
        CHECK(tree[8] == QStringLiteral("2 values BAD, SUSPECT, GOOD"));
    }

    SECTION("a vlen resolves through to what it holds one of")
    {
        REQUIRE(h5test::selectAndSettle(controller,
                                        QStringLiteral("/plotting/events")));
        const QStringList events = typeTree(controller);
        REQUIRE(events.size() >= 2);
        CHECK(events.back() == QStringLiteral("1 tags vlen of int32"));
        CHECK(events.contains(QStringLiteral("2 x float64")));
    }

    SECTION("a type with no parts gets no tree at all")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/data/ramp")));
        CHECK(typeTree(controller).isEmpty());
    }
}

TEST_CASE("a member of a compound reads as a dataset of its own",
          "[example][member]")
{
    // /types/compound/nested holds six readings, i = 0..5, with
    //   position = {i, 2i, 3i}   samples[s] = i + s/4   weight = i/2
    // which is enough arithmetic to tell a member that was read from one that
    // was guessed at.
    const auto file = openExample();
    const h5test::Dataset whole(file, "/types/compound/nested");
    const h5core::TypeInfo& type = whole.info().type;

    SECTION("a scalar member keeps the shape and changes the type")
    {
        const h5test::Field weight(file, "/types/compound/nested",
                                   h5test::chainOf(type, {"weight"}));
        CHECK(weight.info().shape == std::vector<hsize_t>{6});
        CHECK(weight.info().type.cls == h5core::TypeClass::Float);
        CHECK(weight.info().isNumeric());

        const auto values = weight.readNumericWindow({0}, {6});
        REQUIRE(values.values
                == std::vector<double>{0.0, 0.5, 1.0, 1.5, 2.0, 2.5});
    }

    SECTION("a chain goes through a compound member to what it holds")
    {
        const h5test::Field y(file, "/types/compound/nested",
                              h5test::chainOf(type, {"position", "y"}));
        CHECK(y.info().shape == std::vector<hsize_t>{6});
        const auto values = y.readNumericWindow({0}, {6});
        REQUIRE(values.values == std::vector<double>{0.0, 2.0, 4.0, 6.0, 8.0, 10.0});
    }

    SECTION("an array member appends its dimension to the shape")
    {
        // This is the case the whole design turns on: .samples is not one value
        // per record, it is four, and those four are an axis of the result like
        // any other -- which is what lets the table lay them out and the slice
        // line address them.
        const h5test::Field samples(file, "/types/compound/nested",
                                    h5test::chainOf(type, {"samples"}));
        REQUIRE(samples.info().shape == std::vector<hsize_t>{6, 4});
        CHECK(samples.info().type.cls == h5core::TypeClass::Float);

        const auto values = samples.readNumericWindow({0, 0}, {6, 4});
        REQUIRE(values.values.size() == 24);
        for (hsize_t i = 0; i < 6; ++i) {
            for (hsize_t s = 0; s < 4; ++s) {
                const double expected =
                    static_cast<double>(i) + static_cast<double>(s) / 4.0;
                CHECK(values.values[i * 4 + s] == expected);
            }
        }
    }

    SECTION("a hyperslab of an array member cuts both halves of the shape")
    {
        const h5test::Field samples(file, "/types/compound/nested",
                                    h5test::chainOf(type, {"samples"}));
        // Records 2 and 3, samples 1 and 2 of each.
        const auto block = samples.readNumericWindow({2, 1}, {2, 2});
        CHECK(block.count == std::vector<hsize_t>{2, 2});
        REQUIRE(block.values == std::vector<double>{2.25, 2.5, 3.25, 3.5});
    }

    SECTION("a string member is text, and says so rather than plotting")
    {
        const h5test::Field station(file, "/types/compound/nested",
                                    h5test::chainOf(type, {"station"}));
        CHECK(station.info().type.cls == h5core::TypeClass::String);
        CHECK_FALSE(station.info().isNumeric());
        const auto cells = station.readWindow({0}, {3});
        REQUIRE(cells.cells == std::vector<std::string>{"ST-000", "ST-001", "ST-002"});
    }

    SECTION("an enum member reads as its symbol, as it does in the grid")
    {
        const h5test::Field quality(file, "/types/compound/nested",
                                    h5test::chainOf(type, {"quality"}));
        const auto cells = quality.readWindow({0}, {4});
        REQUIRE(cells.cells
                == std::vector<std::string>{"BAD", "SUSPECT", "GOOD", "BAD"});
    }

    SECTION("a member that is itself a compound still opens out")
    {
        const h5test::Field position(file, "/types/compound/nested",
                                     h5test::chainOf(type, {"position"}));
        CHECK(position.info().type.cls == h5core::TypeClass::Compound);
        const h5core::ElementValue element = position.readElement({2});
        REQUIRE(element.fields.size() == 3);
        CHECK(element.fields[0].name == "x");
        CHECK(element.json == "{\n  \"x\": 2,\n  \"y\": 4,\n  \"z\": 6\n}");
    }
}

TEST_CASE("every kind of member of a compound is reachable", "[example][member]")
{
    // /plotting/events is a hundred thousand records holding one of each:
    //   time float64, energy float32, station string, position compound,
    //   samples array[4], quality enum, tags vlen int32
    // with tags[i] holding i % 4 entries, so every fourth list is empty.
    const auto file = openExample();
    const h5test::Dataset events(file, "/plotting/events");
    const h5core::TypeInfo& type = events.info().type;
    REQUIRE(events.info().shape == std::vector<hsize_t>{100000});
    REQUIRE(type.members.size() == 7);

    SECTION("a float member is a line of a hundred thousand numbers")
    {
        const h5test::Field energy(file, "/plotting/events",
                                   h5test::chainOf(type, {"energy"}));
        CHECK(energy.info().shape == std::vector<hsize_t>{100000});
        CHECK(energy.info().isNumeric());
        // The spikes are at i % 10000 == 7, and they are what an envelope has
        // to keep: 500 against a swell that never leaves 10..90.
        const auto around = energy.readNumericWindow({0}, {10});
        CHECK(around.values[7] == 500.0);
        CHECK(around.values[6] < 100.0);
    }

    SECTION("a chain through a compound member")
    {
        const h5test::Field x(file, "/plotting/events",
                              h5test::chainOf(type, {"position", "x"}));
        const auto values = x.readNumericWindow({0}, {5});
        REQUIRE(values.values == std::vector<double>{0.0, 1.0, 2.0, 3.0, 4.0});
    }

    SECTION("an array member appends its axis at this scale too")
    {
        const h5test::Field samples(file, "/plotting/events",
                                    h5test::chainOf(type, {"samples"}));
        REQUIRE(samples.info().shape == std::vector<hsize_t>{100000, 4});
        const auto block = samples.readNumericWindow({50000, 0}, {2, 4});
        REQUIRE(block.values
                == std::vector<double>{50000.0, 50000.25, 50000.5, 50000.75, 50001.0,
                                       50001.25, 50001.5, 50001.75});
    }

    SECTION("a ragged member read whole is the list each record holds")
    {
        const h5test::Field tags(file, "/plotting/events",
                                 h5test::chainOf(type, {"tags"}));
        // No axis appended: a vlen's length differs in every record, so there
        // is no dimension it could be.
        CHECK(tags.info().shape == std::vector<hsize_t>{100000});
        CHECK(tags.info().type.cls == h5core::TypeClass::VarLen);
        CHECK_FALSE(tags.info().isNumeric());

        const auto cells = tags.readWindow({0}, {4});
        REQUIRE(cells.cells.size() == 4);
        // i % 4 entries, holding i * 10 + t.
        CHECK(cells.cells[0] == "[]");
        CHECK_THAT(cells.cells[3], ContainsSubstring("30"));
        CHECK_THAT(cells.cells[3], ContainsSubstring("32"));
    }

    SECTION("one index of a ragged member is a number, and a gap where there is none")
    {
        const h5test::Field first(file, "/plotting/events",
                                  h5test::chainOf(type, {"tags"}, 0));
        // Still the dataset's own shape, and now a number -- so it plots.
        CHECK(first.info().shape == std::vector<hsize_t>{100000});
        CHECK(first.info().type.cls == h5core::TypeClass::Integer);
        CHECK(first.info().isNumeric());

        const auto values = first.readNumericWindow({0}, {5});
        REQUIRE(values.values.size() == 5);
        // Record 0 has an empty list: no value there, which is a NaN, which is
        // where a stroke ends rather than a number a line is drawn through.
        CHECK(std::isnan(values.values[0]));
        CHECK(values.values[1] == 10.0);
        CHECK(values.values[2] == 20.0);
        CHECK(values.values[3] == 30.0);
        CHECK(std::isnan(values.values[4]));

        // And as text, a record with nothing there prints nothing rather than
        // a number it does not have.
        const auto cells = first.readWindow({0}, {2});
        CHECK(cells.cells[0].empty());
        CHECK(cells.cells[1] == "10");
    }

    SECTION("an index past every list is every record having none")
    {
        const h5test::Field third(file, "/plotting/events",
                                  h5test::chainOf(type, {"tags"}, 2));
        const auto values = third.readNumericWindow({0}, {4});
        // Only records with three entries have a third one: i % 4 == 3.
        CHECK(std::isnan(values.values[0]));
        CHECK(std::isnan(values.values[2]));
        CHECK(values.values[3] == 32.0);
    }

    SECTION("a string member and an enum member read as what they are")
    {
        const h5test::Field station(file, "/plotting/events",
                                    h5test::chainOf(type, {"station"}));
        CHECK(station.readWindow({0}, {2}).cells
              == std::vector<std::string>{"S-000", "S-001"});

        const h5test::Field quality(file, "/plotting/events",
                                    h5test::chainOf(type, {"quality"}));
        CHECK(quality.readWindow({0}, {3}).cells
              == std::vector<std::string>{"BAD", "SUSPECT", "GOOD"});
    }
}

TEST_CASE("a member selection is the same selection written shorter",
          "[example][member]")
{
    // The identity the whole notation rests on:
    //
    //     array[i1,i2,i3].b[i4]  ==  (array[:,:,:].b)[i1,i2,i3,i4]
    //
    // It holds because the subscript after `.b` binds to the axes `b` itself
    // contributes, and nothing else. This asserts the right-hand side against
    // the elements themselves, which is what a reader writing the left-hand
    // side is promised.
    const auto file = openExample();
    const h5test::Dataset whole(file, "/types/compound/nested");
    const h5test::Field samples(file, "/types/compound/nested",
                                h5test::chainOf(whole.info().type, {"samples"}));

    // (nested.samples)[3, 2] -- record 3, sample 2.
    const auto one = samples.readNumericWindow({3, 2}, {1, 1});
    REQUIRE(one.values.size() == 1);
    CHECK(one.values[0] == 3.5);

    // Which is exactly what nested[3].samples[2] names, read the long way: the
    // whole struct, opened out, its samples field, its third entry.
    const h5core::ElementValue record = whole.readElement({3});
    REQUIRE(record.fields.size() == 6);
    const auto field = std::find_if(
        record.fields.begin(), record.fields.end(),
        [](const h5core::FieldValue& f) { return f.name == "samples"; });
    REQUIRE(field != record.fields.end());
    CHECK_THAT(field->value, ContainsSubstring("3.5"));

    // And the axes are in the order the identity says: the dataset's first,
    // the member's after, so one slice addresses both halves.
    REQUIRE(samples.info().shape
            == std::vector<hsize_t>{whole.info().shape[0], 4});
}

TEST_CASE("the viewer draws a member of a compound", "[example][member]")
{
    gui::AppController controller;
    REQUIRE(h5test::openFileAndSettle(controller,
                                      QString::fromStdString(example().path())));

    SECTION("picking a member turns a struct into numbers")
    {
        // /types/compound/table_4x5 is rank 2 of {id: int32, value: float64},
        // value = i/8 in row-major order. Before a member is picked there is
        // nothing here any plot can draw.
        REQUIRE(h5test::selectAndSettle(controller,
                                        QStringLiteral("/types/compound/table_4x5")));
        REQUIRE(controller.datasetIsCompound());
        REQUIRE_FALSE(controller.datasetIsNumeric());

        REQUIRE(controller.applyMember(QStringLiteral(".value")).isEmpty());

        // The dataset is still a compound -- that is what keeps the member box
        // on screen -- but what is being drawn is a float, and the plot and the
        // image exist for it now.
        CHECK(controller.datasetIsCompound());
        CHECK(controller.datasetIsNumeric());
        CHECK(controller.datasetIsFloat());
        CHECK(controller.datasetRank() == 2);
        CHECK(controller.memberText() == QStringLiteral(".value"));

        auto* table = qobject_cast<gui::DatasetTableModel*>(controller.datasetModel());
        REQUIRE(table != nullptr);
        CHECK(table->numeric());
        // Row 1, column 2 is element 7 of the flat order: 7/8. Read through the
        // member, so the cell is the number and not the struct it sits in.
        CHECK(h5test::settledData(controller.datasetModel(),
                                  controller.datasetModel()->index(1, 2),
                                  Qt::DisplayRole)
                  .toString()
              == QStringLiteral("0.875"));
    }

    SECTION("an array member appends an axis the table can lay out")
    {
        REQUIRE(h5test::selectAndSettle(controller,
                                        QStringLiteral("/types/compound/nested")));
        REQUIRE(controller.datasetRank() == 1);

        REQUIRE(controller.applyMember(QStringLiteral(".samples")).isEmpty());
        // Six records of four samples: the member's axis is a dimension of the
        // table like any other, which is the whole point of appending it.
        CHECK(controller.datasetRank() == 2);
        CHECK(controller.datasetElementCount() == 24);
        CHECK(controller.sliceText() == QStringLiteral(":, :"));
    }

    SECTION("a subscript on the member is folded onto the slice line")
    {
        // The identity, as the two boxes show it: what was typed on the chain
        // ends up on the slice, and the chain prints back bare. The member's
        // axes are ordinary dimensions and this is where they are addressed.
        REQUIRE(h5test::selectAndSettle(controller,
                                        QStringLiteral("/types/compound/nested")));
        REQUIRE(controller.applyMember(QStringLiteral(".samples[2]")).isEmpty());

        CHECK(controller.memberText() == QStringLiteral(".samples"));
        CHECK(controller.sliceText() == QStringLiteral(":, 2"));
    }

    SECTION("a chain keeps the slice the reader had already set up")
    {
        REQUIRE(h5test::selectAndSettle(controller,
                                        QStringLiteral("/types/compound/nested")));
        REQUIRE(controller.applySlice(QStringLiteral("1:4")).isEmpty());
        REQUIRE(controller.applyMember(QStringLiteral(".samples")).isEmpty());
        // The chain only ever changes the axes after the dataset's own, so the
        // leading subscript is the one that was there.
        CHECK(controller.sliceText() == QStringLiteral("1:4, :"));
    }

    SECTION("a chain that does not read changes nothing and says why")
    {
        REQUIRE(h5test::selectAndSettle(controller,
                                        QStringLiteral("/types/compound/nested")));
        const QString before = controller.sliceText();
        const QString reason = controller.applyMember(QStringLiteral(".enrgy"));
        CHECK_THAT(reason.toStdString(), ContainsSubstring("no member 'enrgy'"));
        CHECK(controller.memberText().isEmpty());
        CHECK(controller.sliceText() == before);
        // Checked without applying, the way the bar reports a slice as it is
        // typed, and for the same reason: nothing is read to find out.
        CHECK_FALSE(controller.memberError(QStringLiteral(".enrgy")).isEmpty());
        CHECK(controller.memberError(QStringLiteral(".weight")).isEmpty());
    }

    SECTION("the line a plot draws pastes back as an expression")
    {
        // The hand-off to a custom tab. What the legend offers has to be a line
        // someone can paste into an entry box, so the subscript's two halves go
        // on either side of the chain: the dataset's own axes before the member
        // is named, the axes it appends after.
        REQUIRE(h5test::selectAndSettle(controller,
                                        QStringLiteral("/types/compound/nested")));
        REQUIRE(controller.applyMember(QStringLiteral(".samples")).isEmpty());

        auto* table = qobject_cast<gui::DatasetTableModel*>(controller.datasetModel());
        REQUIRE(table != nullptr);
        // Six records down the rows, four samples across: one row is one
        // record's four samples.
        const QString line = table->lineExpression(3, true);
        CHECK(line == QStringLiteral("/types/compound/nested[3].samples[:]"));

        // And it reads back as the same selection it was written from.
        const gui::Expression parts = gui::splitExpression(line);
        REQUIRE(parts.valid());
        CHECK(parts.path == QStringLiteral("/types/compound/nested"));
        CHECK(parts.member == QStringLiteral(".samples[:]"));
    }

    SECTION("with no member the expression is what it always was")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/data/matrix")));
        auto* table = qobject_cast<gui::DatasetTableModel*>(controller.datasetModel());
        REQUIRE(table != nullptr);
        CHECK_THAT(table->lineExpression(0, true).toStdString(),
                   ContainsSubstring("/data/matrix["));
        CHECK_THAT(table->lineExpression(0, true).toStdString(),
                   !ContainsSubstring("."));
    }

    SECTION("coming back to a dataset comes back to the member")
    {
        REQUIRE(h5test::selectAndSettle(controller,
                                        QStringLiteral("/types/compound/nested")));
        REQUIRE(controller.applyMember(QStringLiteral(".weight")).isEmpty());
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/data/matrix")));
        CHECK(controller.memberText().isEmpty());
        REQUIRE(h5test::selectAndSettle(controller,
                                        QStringLiteral("/types/compound/nested")));
        CHECK(controller.memberText() == QStringLiteral(".weight"));
        CHECK(controller.datasetIsNumeric());
    }
}

TEST_CASE("the pipeline's select row runs over a real compound",
          "[example][member][postproc]")
{
    // /plotting/events is a hundred thousand structs with one of every member
    // class in it, which is what makes this worth stating here rather than in
    // test_models: the shape column tells a story only a real member can tell.
    gui::AppController controller;
    REQUIRE(h5test::openFileAndSettle(controller,
                                      QString::fromStdString(example().path())));
    REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/plotting/events")));

    gui::PostprocessModel* pipeline = controller.postprocessModel();
    REQUIRE(pipeline != nullptr);
    const auto rowOf = [pipeline](int row, int role) {
        return pipeline->data(pipeline->index(row, 0), role);
    };

    SECTION("the list offers every chain, nested ones under their own names")
    {
        const QStringList choices =
            rowOf(1, gui::PostprocessModel::ChoicesRole).toStringList();
        CHECK(choices.startsWith(QStringLiteral(".time")));
        CHECK(choices.contains(QStringLiteral(".position")));
        CHECK(choices.contains(QStringLiteral(".position.x")));
        CHECK(choices.contains(QStringLiteral(".samples")));
        CHECK(choices.contains(QStringLiteral(".tags")));
        // An array's dimensions are axes rather than names, so nothing goes
        // under `.samples`; and a chain cannot go on through a vlen.
        CHECK_FALSE(choices.contains(QStringLiteral(".samples.0")));
        CHECK(choices.size() == 10);
    }

    SECTION("the shape column states what naming a member did")
    {
        // The input row is the dataset's own shape and the select row is what
        // the slice below it sees. Reading the two together is the whole point
        // of the column, and on a member that appends an axis they differ.
        CHECK(rowOf(0, gui::PostprocessModel::ShapeRole).toString()
              == QStringLiteral("100000"));
        CHECK(rowOf(1, gui::PostprocessModel::ShapeRole).toString()
              == QStringLiteral("100000"));

        pipeline->setArgument(1, QStringLiteral(".samples"));
        CHECK(rowOf(0, gui::PostprocessModel::ShapeRole).toString()
              == QStringLiteral("100000"));
        CHECK(rowOf(1, gui::PostprocessModel::ShapeRole).toString()
              == QString::fromUtf8("100000 \u00d7 4"));
    }

    SECTION("a ragged member indexed is still what is selected")
    {
        // `.tags[0]` is a selection the list of names cannot hold, because the
        // subscripts live on the slice line. It goes in front of the list
        // rather than leaving the box showing something nobody chose.
        REQUIRE(controller.applyMember(QStringLiteral(".tags[0]")).isEmpty());
        const QString current =
            rowOf(1, gui::PostprocessModel::ArgumentRole).toString();
        CHECK(current == QStringLiteral(".tags[0]"));
        CHECK(rowOf(1, gui::PostprocessModel::ChoicesRole).toStringList().front()
              == current);
    }

    SECTION("an operation runs on the member, and the output says so")
    {
        pipeline->setArgument(1, QStringLiteral(".samples"));
        REQUIRE(controller.applySlice(QStringLiteral("0:10, :")).isEmpty());
        pipeline->setEnabled(true);
        REQUIRE(pipeline->active());

        pipeline->addStep(QStringLiteral("max"));
        pipeline->setArgument(3, QStringLiteral("1"));
        CHECK(pipeline->error().isEmpty());
        CHECK(rowOf(3, gui::PostprocessModel::ShapeRole).toString()
              == QStringLiteral("10"));
        CHECK(rowOf(pipeline->rowCount() - 1,
                    gui::PostprocessModel::ShapeRole).toString()
              == QStringLiteral("10"));
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
        // The nested struct opens out under its own name, one member per line
        // and indented under it.
        CHECK_THAT(element.json,
                   ContainsSubstring("\"position\": {\n    \"x\": 0,"));
        // The array of four does not: four numbers on four lines is a worse
        // reading of four numbers than four numbers on one.
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
        CHECK(element.value(QStringLiteral("label")).toString() == QStringLiteral("[1,2]"));
        CHECK(element.value(QStringLiteral("json")).toString() ==
              QStringLiteral("{\n  \"id\": 7,\n  \"value\": 0.875\n}"));
        CHECK(element.value(QStringLiteral("fields")).toList().size() == 2);

        // A cell that is not there is not an error, it is nothing.
        CHECK(table->elementAt(99, 0).isEmpty());
    }
}

TEST_CASE("the colour axis comes from the file, and stays the reader's", "[example][images]")
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
        REQUIRE(
            h5test::selectAndSettle(controller, QStringLiteral("/images/multispectral_64x64x12")));
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
        REQUIRE(
            h5test::selectAndSettle(controller, QStringLiteral("/images/multispectral_64x64x12")));
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
        REQUIRE(
            h5test::selectAndSettle(controller, QStringLiteral("/images/multispectral_64x64x12")));
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

TEST_CASE("only a dataset that says it is an image is treated as one", "[example][images]")
{
    const auto file = openExample();

    SECTION("the tag follows the attribute, not the shape")
    {
        // Tagged, and rearranged.
        CHECK(h5test::Dataset(file, "/images/rgb_256x256x3").info().image.has_value());
        CHECK(h5test::Dataset(file, "/images/gray_512x512").info().image.has_value());
        // Image-shaped and silent about it: rank 3 with three trailing
        // channels, and rank 4 of RGB frames.
        CHECK_FALSE(
            h5test::Dataset(file, "/images/multispectral_64x64x12").info().image.has_value());
        CHECK_FALSE(h5test::Dataset(file, "/images/stack_8x64x64x3").info().image.has_value());
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

TEST_CASE("the interlace and the origin are read as the spec defines them", "[example][images]")
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

TEST_CASE("the tree tags the datasets that declare themselves images", "[example][images][tree]")
{
    gui::AppController controller;
    REQUIRE(h5test::openFileAndSettle(controller, QString::fromStdString(example().path())));
    QAbstractItemModel* tree = controller.treeModel();

    const QModelIndex images = indexForName(tree, QModelIndex{}, QStringLiteral("images"));
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
TEST_CASE("every tree tag carries the fact behind it", "[example][tree][tags]")
{
    gui::AppController controller;
    REQUIRE(h5test::openFileAndSettle(controller, QString::fromStdString(example().path())));
    QAbstractItemModel* tree = controller.treeModel();

    const QModelIndex images = indexForName(tree, QModelIndex{}, QStringLiteral("images"));
    const QModelIndex links = indexForName(tree, QModelIndex{}, QStringLiteral("links"));
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
    CHECK(role(rgb, gui::H5TreeModel::ImageSubclassRole).toString() ==
          QStringLiteral("Truecolour"));
    CHECK(role(at(images, "indexed_64x64"), gui::H5TreeModel::ImageSubclassRole).toString() ==
          QStringLiteral("Indexed"));
    // Nothing that is not an image claims to be one.
    CHECK(role(at(images, "palette"), gui::H5TreeModel::ImageSubclassRole).toString().isEmpty());

    // [A] -- how many, not merely that there are some.
    CHECK(role(rgb, gui::H5TreeModel::HasAttributesRole).toBool());
    CHECK(role(rgb, gui::H5TreeModel::AttributeCountRole).toInt() >= 1);
    const QModelIndex plain = at(images, "field_256x256");
    CHECK(role(plain, gui::H5TreeModel::AttributeCountRole).toInt() == 2);

    // [L] -- where it leads, and for a broken one which half is missing. A
    // hard link leads nowhere but to itself and says nothing.
    const auto description = [&](const char* name) {
        return role(at(links, name), gui::H5TreeModel::LinkDescriptionRole).toString();
    };
    CHECK_THAT(description("soft_to_matrix").toStdString(),
               ContainsSubstring("Soft link") && ContainsSubstring("/data/matrix"));
    CHECK(description("hard_to_matrix").isEmpty());

    // The two that fail, which are the rows drawn in red. An external link has
    // two things that can be absent and the sentence has to say which.
    CHECK_FALSE(role(at(links, "soft_dangling"), gui::H5TreeModel::LinkResolvesRole).toBool());
    CHECK_THAT(description("soft_dangling").toStdString(),
               ContainsSubstring("no object at that path"));
    CHECK_THAT(description("external_missing_file").toStdString(),
               ContainsSubstring("External link") &&
                   ContainsSubstring("the file or the object is missing"));
}

// A count and its noun are one phrase. "1 items" and "Element size 1 bytes"
// are not phrases; they are a number with a fixed string stapled to it.
TEST_CASE("counts read in the singular when there is one of them", "[example][tree][info]")
{
    gui::AppController controller;
    REQUIRE(h5test::openFileAndSettle(controller, QString::fromStdString(example().path())));
    QAbstractItemModel* tree = controller.treeModel();

    // /links/loop holds exactly one member.
    const QModelIndex links = indexForName(tree, QModelIndex{}, QStringLiteral("links"));
    REQUIRE(links.isValid());
    const QModelIndex loop = indexForName(tree, links, QStringLiteral("loop"));
    REQUIRE(loop.isValid());
    CHECK(h5test::settledData(tree, loop, gui::H5TreeModel::MetaRole).toString() ==
          QStringLiteral("1 item"));

    // ...and a group with more than one still reads in the plural.
    CHECK(h5test::settledData(tree, links, gui::H5TreeModel::MetaRole).toString() ==
          QStringLiteral("12 items"));

    // uint8 is one byte wide; int32 is four.
    REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/images/mislabelled_truecolor")));
    CHECK(infoRow(controller, QStringLiteral("Element size")) == QStringLiteral("1 byte"));
    REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/data/big_endian_int32")));
    CHECK(infoRow(controller, QStringLiteral("Element size")) == QStringLiteral("4 bytes"));
}

TEST_CASE("every row of the tree says something about itself", "[example][tree]")
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
            const QString path =
                h5test::settledData(tree, node, gui::H5TreeModel::PathRole).toString();
            const QString meta =
                h5test::settledData(tree, node, gui::H5TreeModel::MetaRole).toString();
            INFO(path.toStdString());
            CHECK_FALSE(meta.isEmpty());
            ++rows;
            self(self, node);
        }
    };
    walk(walk, QModelIndex{});
    CHECK(rows > 100);

    const QModelIndex committed = indexForName(tree, QModelIndex{}, QStringLiteral("committed"));
    REQUIRE(committed.isValid());
    const QModelIndex celsius = indexForName(tree, committed, QStringLiteral("celsius_t"));
    REQUIRE(celsius.isValid());
    // A committed type has no shape and no children; what it is *of* is the
    // whole of what the row has to say.
    CHECK(h5test::settledData(tree, celsius, gui::H5TreeModel::MetaRole).toString() ==
          QStringLiteral("float64"));
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
        for (const char* path :
             {"/stress/many_children_4096", "/stress/nested_16x64", "/stress/empty_group", "/"}) {
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
        const QModelIndex self = indexForName(&tree, links, QStringLiteral("soft_to_self"));
        REQUIRE(self.isValid());
        CHECK(h5test::settledData(&tree, self, gui::H5TreeModel::IsCyclicRole).toBool());
        CHECK_FALSE(h5test::settledHasChildren(&tree, self));
        CHECK(h5test::settledRowCount(&tree, self) == 0);
    }

    SECTION("a hard link from a subgroup back to its ancestor")
    {
        const QModelIndex loop = indexForName(&tree, links, QStringLiteral("loop"));
        REQUIRE(loop.isValid());
        const QModelIndex back = indexForName(&tree, loop, QStringLiteral("back_to_links"));
        REQUIRE(back.isValid());
        CHECK(h5test::settledData(&tree, back, gui::H5TreeModel::IsCyclicRole).toBool());
        CHECK_FALSE(h5test::settledHasChildren(&tree, back));
        CHECK(h5test::settledData(&tree, back, gui::H5TreeModel::MetaRole).toString() ==
              QStringLiteral("cycle"));
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

TEST_CASE("the tree costs what is on screen, not what is in the file", "[example][tree][cost]")
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
        const QModelIndex wide = h5test::reveal(tree, QStringLiteral("/stress/many_children_4096"));
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
        const QModelIndex wide = h5test::reveal(tree, QStringLiteral("/stress/many_children_4096"));
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
        CHECK_FALSE(
            tree.data(tree.index(0, 0, wide), gui::H5TreeModel::MetaRole).toString().isEmpty());
    }

    SECTION("a member count beside a group row is not taken by counting")
    {
        // Sixteen groups of sixty-four. Saying how many members each one holds
        // by listing it costs 1024 link resolutions to draw 16 rows, which is
        // quadratic in the shape acquisition files actually have.
        const QModelIndex nested = h5test::reveal(tree, QStringLiteral("/stress/nested_16x64"));
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
        CHECK(tree.data(tree.index(0, 0, nested), gui::H5TreeModel::MetaRole).toString() ==
              QStringLiteral("64 items"));
    }

    SECTION("a row that has already been drawn is free the second time")
    {
        const QModelIndex wide = h5test::reveal(tree, QStringLiteral("/stress/many_children_4096"));
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

// ---------------------------------------------------------------------------
// /plotting: the stress set, held to what its notes claim
// ---------------------------------------------------------------------------
//
// Each dataset in that group carries a note saying which way it is difficult.
// A note is a comment and a comment is not checked, so these are the same
// claims asserted -- and asserted through the plot, because every one of them
// is about what survives being reduced to something a screen can show.

TEST_CASE("the plot's stress set is as difficult as it says", "[example][plotting][gui]")
{
    gui::AppController controller;
    REQUIRE(h5test::openFileAndSettle(controller, QString::fromStdString(example().path())));
    gui::DatasetPlot* plot = controller.datasetPlot();
    REQUIRE(plot != nullptr);

    SECTION("twelve one-sample spikes in a million survive the thinning")
    {
        // The claim the whole plot is arranged around, and the one that is
        // trivially falsifiable: the extent is +/-9 and the noise it hides in
        // is +/-0.05, so an extent of +/-0.05 means the spikes were thinned
        // away.
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/plotting/spikes_1M")));
        REQUIRE(plot->hasData());
        REQUIRE(plot->thinned());
        CHECK(plot->pointCount() <= gui::DatasetPlot::kMaxPoints);
        CHECK(plot->maximum() == 9.0);
        CHECK(plot->minimum() == -9.0);
    }

    SECTION("seventeen impulses in ten million survive it too")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/plotting/adc_10M")));
        REQUIRE(plot->hasData());
        REQUIRE(plot->thinned());
        CHECK(plot->sourcePointCount() == 10000000);
        CHECK(plot->pointCount() <= gui::DatasetPlot::kMaxPoints);
        // A stride over ten million points into two thousand is nearly five
        // thousand wide, and none of the seventeen indices is a multiple of it.
        CHECK(plot->maximum() == 32000.0);
        CHECK(plot->minimum() == -32000.0);
    }

    SECTION("and a closer look at one of them is the sample itself")
    {
        // What the whole-line summary cannot do, and what the stress set was
        // written to make visible. From a mile up, the impulse at 4999999 is
        // the extreme of a bucket nearly five thousand samples wide -- drawn,
        // because an envelope cannot lose it, but two pixels wide and sitting
        // wherever its bucket starts. Zoom in and the plot reads that run of the
        // file again, until a bucket is one element and what is drawn is what
        // was recorded.
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/plotting/adc_10M")));
        REQUIRE(plot->hasData());

        // Four hundred samples across the pane, which is a bucket of one.
        plot->setVisibleRange(4999800.0, 5000200.0);
        h5test::settleFor(300);

        const gui::PlotLine closest = plot->lineOf(0);
        // Twice the pane's own thousand buckets, because a run is read an
        // octave finer than the pane needs -- see DatasetPlot::detailBuckets.
        REQUIRE(closest.count == 2048);
        CHECK(closest.positionStep == 1.0);
        CHECK(closest.positionStart == 4999680.0); // aligned to the data, not to the view

        const auto at = static_cast<qsizetype>(4999999 - 4999680);
        CHECK(closest.values[at] == -32000.0);
        // Its neighbours are the trace, not the impulse: one sample wide is one
        // sample wide, which no summary of this line could have said.
        CHECK(std::abs(closest.values[at - 1]) < 20000.0);
        CHECK(std::abs(closest.values[at + 1]) < 20000.0);

        // The extent is still the whole record's, so the axis did not rescale
        // to the run on screen while the reader was looking at it.
        CHECK(plot->maximum() == 32000.0);
        CHECK(plot->minimum() == -32000.0);

        // And zooming back out is the summary again, in the same call.
        plot->setVisibleRange(0.0, 10000000.0);
        CHECK(plot->lineOf(0).positionStep > 1000.0);
    }

    SECTION("missing data arrives missing, and is not filled in")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/plotting/gaps_1M")));
        REQUIRE(plot->hasData());

        const gui::PlotLine line = plot->lineOf(0);
        REQUIRE(line.count > 0);
        const std::vector<QPointF> drawable = gui::samplesOf(line, plot->drawingAxis());
        // A fifth of the line is missing, so a good fraction of what was read
        // has to be missing too -- and what is left is what gets drawn, with
        // the holes ending one stroke and starting the next.
        CHECK(drawable.size() < static_cast<std::size_t>(line.count));
        CHECK(drawable.size() > static_cast<std::size_t>(line.count) / 2);
    }

    SECTION("eighteen decades, either side of zero")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/plotting/decades_200k")));
        REQUIRE(plot->hasData());
        // Both ends of the range are present and the reader zooms to whichever
        // of them they are reading: the extent is the whole of it, and the
        // envelope is what keeps the small end from being thinned away on the
        // way to a pane that is showing the large one.
        CHECK(plot->maximum() > 1e6);
        CHECK(plot->minimum() < 0.0);
    }

    SECTION("numbers that do not fit a float, among ordinary ones")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/plotting/extremes_100k")));
        REQUIRE(plot->hasData());
        // The infinities are not readings and are not counted; 1e300 is.
        CHECK(plot->maximum() == 1e300);
        CHECK(plot->minimum() == -1e300);
    }

    SECTION("a time base where float32 gives up")
    {
        REQUIRE(
            h5test::selectAndSettle(controller, QStringLiteral("/plotting/epoch_seconds_500k")));
        REQUIRE(plot->hasData());
        CHECK(plot->minimum() >= 1.7e9);
        CHECK(plot->maximum() <= 1.7e9 + 500.0);

        // The trap itself, stated as arithmetic rather than as a comment: two
        // timestamps a millisecond apart are the same float32.
        const auto first = static_cast<float>(1.7e9);
        const auto second = static_cast<float>(1.7e9 + 0.001);
        CHECK(first == second);
    }

    SECTION("ten thousand lines at once")
    {
        REQUIRE(h5test::selectAndSettle(controller,
                                        QStringLiteral("/plotting/noisy_lines_10000x1024")));
        REQUIRE(plot->hasData());
        CHECK(plot->sourceSeriesCount() == 10000);
        // A selection opens on a window, and says so.
        CHECK(plot->seriesCount() == gui::DatasetPlot::initialSeriesLimit());

        plot->selectAll();
        CHECK(plot->seriesCount() == 10000);
        // And past a thousand lines they share a budget rather than each
        // holding the full couple of thousand points: ten thousand of those
        // would be a hundred and sixty megabytes held, and twenty million
        // doubles walked on every frame of a drag.
        CHECK(plot->pointCount() <= gui::DatasetPlot::kMinPoints);
        CHECK(plot->pointCount() > 0);
    }

    SECTION("a tone at the thinning stride is still a tone")
    {
        // Sampled by stride this becomes a slow swell that is not in the file.
        // Drawn as an envelope its extremes are its own, at every zoom.
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/plotting/beat_1M")));
        REQUIRE(plot->hasData());
        REQUIRE(plot->thinned());
        CHECK(plot->maximum() > 0.95);
        CHECK(plot->minimum() < -0.95);
    }
}
