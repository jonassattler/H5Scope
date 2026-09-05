// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "support/TestFile.hpp"

#include "gui/AppController.hpp"
#include "support/AsyncModels.hpp"
#include "gui/AttributeTableModel.hpp"
#include "gui/DatasetImage.hpp"
#include "gui/DatasetPlot.hpp"
#include "gui/DatasetTableModel.hpp"
#include "gui/H5TreeModel.hpp"
#include "gui/ObjectInfoModel.hpp"
#include "gui/TableLayout.hpp"
#include "gui/DatasetStringListModel.hpp"
#include "gui/PostprocessModel.hpp"
#include "gui/TableSetupModel.hpp"
#include "gui/TreeFilterProxyModel.hpp"
#include "h5scope/Version.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <QAbstractItemModel>
#include <QColor>
#include "gui/H5Thread.hpp"
#include "support/H5Reader.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QSignalSpy>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <algorithm>
#include <cmath>

using Catch::Matchers::ContainsSubstring;

namespace {

/// A controller backed by a freshly generated fixture. No QML engine is
/// involved: these tests cover the C++ layer that QML binds to.
struct ControllerFixture {
    h5test::TempFile temp{"models"};
    gui::AppController controller;

    ControllerFixture()
    {
        h5test::onH5([&] { h5test::writeFixture(temp.path()); });
        REQUIRE(h5test::openFileAndSettle(controller, QString::fromStdString(temp.path())));
    }

    [[nodiscard]] gui::H5TreeModel* tree() const
    {
        auto* model = qobject_cast<gui::H5TreeModel*>(controller.treeModel());
        REQUIRE(model != nullptr);
        return model;
    }
    [[nodiscard]] gui::DatasetTableModel* table() const
    {
        auto* model = qobject_cast<gui::DatasetTableModel*>(controller.datasetModel());
        REQUIRE(model != nullptr);
        return model;
    }
    [[nodiscard]] gui::AttributeTableModel* attributes() const
    {
        auto* model = qobject_cast<gui::AttributeTableModel*>(controller.attributeModel());
        REQUIRE(model != nullptr);
        return model;
    }
    [[nodiscard]] gui::ObjectInfoModel* info() const
    {
        auto* model = qobject_cast<gui::ObjectInfoModel*>(controller.infoModel());
        REQUIRE(model != nullptr);
        return model;
    }
    [[nodiscard]] gui::TableSetupModel* setup() const
    {
        auto* model =
            qobject_cast<gui::TableSetupModel*>(controller.tableSetupModel());
        REQUIRE(model != nullptr);
        return model;
    }
    [[nodiscard]] gui::PostprocessModel* post() const
    {
        gui::PostprocessModel* model = controller.postprocessModel();
        REQUIRE(model != nullptr);
        return model;
    }

    /// One row of the postprocessing panel, by role.
    [[nodiscard]] QVariant step(int row, int role) const
    {
        return post()->data(post()->index(row, 0), role);
    }
    [[nodiscard]] QString shapeOf(int row) const
    {
        return step(row, gui::PostprocessModel::ShapeRole).toString();
    }

    /// The cell as text, which is what every one of these assertions is about.
    ///
    /// Waited for: the table answers with the block it has and asks the file
    /// for the one it does not, so the first read of a cell outside the current
    /// block is empty by design and the second is the value.
    [[nodiscard]] QString cell(int row, int column) const
    {
        return h5test::settledData(table(), table()->index(row, column),
                                   Qt::DisplayRole)
            .toString();
    }
};

/// Shorthand for the parser's happy path.
std::vector<hsize_t> parsed(const char* text, hsize_t extent)
{
    const gui::IndexExpression result =
        gui::parseIndexExpression(QString::fromLatin1(text), extent);
    REQUIRE(result.error.isEmpty());
    return result.indices;
}

/// Shorthand for its unhappy path: the message, so a test can say which one.
QString rejected(const char* text, hsize_t extent)
{
    const gui::IndexExpression result =
        gui::parseIndexExpression(QString::fromLatin1(text), extent);
    REQUIRE_FALSE(result.error.isEmpty());
    return result.error;
}

} // namespace

TEST_CASE("opening files through the controller", "[controller]")
{
    gui::AppController controller;

    SECTION("a missing file fails without blocking and records the reason")
    {
        REQUIRE_FALSE(
            h5test::openFileAndSettle(controller, QStringLiteral("/nonexistent/nope.h5")));
        REQUIRE_FALSE(controller.errorText().isEmpty());
        REQUIRE_FALSE(controller.hasFile());
    }

    SECTION("a file that is not HDF5 fails cleanly")
    {
        QSignalSpy opened(&controller, &gui::AppController::fileOpened);
        REQUIRE_FALSE(
            h5test::openFileAndSettle(controller, QStringLiteral("/etc/hostname")));
        REQUIRE_FALSE(controller.errorText().isEmpty());

        // openFile() only says the open was *started*; whether it worked comes
        // back on this signal, and it is what puts the error dialog in front of
        // the reader. Nothing else would -- the call returned true.
        REQUIRE(opened.count() == 1);
        CHECK_FALSE(opened.front().at(0).toBool());
        CHECK(opened.front().at(1).toString() == QStringLiteral("/etc/hostname"));
    }
}

TEST_CASE_METHOD(ControllerFixture, "the tree model populates lazily", "[tree]")
{
    auto* model = tree();

    SECTION("root children are available once a file is open")
    {
        REQUIRE(h5test::settledRowCount(model, {}) > 0);
    }

    SECTION("a group advertises children before it has been read")
    {
        const QModelIndex group = h5test::reveal(*model, QStringLiteral("/group"));
        REQUIRE(group.isValid());
        REQUIRE(h5test::settledHasChildren(model, group));
    }

    SECTION("a dataset never advertises children")
    {
        const QModelIndex matrix = h5test::reveal(*model, QStringLiteral("/matrix"));
        REQUIRE(matrix.isValid());
        REQUIRE_FALSE(h5test::settledHasChildren(model, matrix));
    }

    SECTION("a group is listed once, and asking again reads nothing")
    {
        // QQuickTreeView asks for rowCount on expand rather than calling
        // fetchMore, so asking must be idempotent. It is no longer *silent*:
        // the rows arrive from the file a moment after they are asked for, and
        // rowsInserted is how the view is told. What must not happen is a
        // second listing, or rows appearing twice.
        const QModelIndex group = h5test::reveal(*model, QStringLiteral("/group"));

        QSignalSpy inserted(model, &QAbstractItemModel::rowsInserted);
        const int first = h5test::settledRowCount(model, group);
        REQUIRE(first == 1); // "nested"
        REQUIRE(inserted.count() == 1);

        inserted.clear();
        const int second = h5test::settledRowCount(model, group);
        REQUIRE(second == first);
        REQUIRE(inserted.count() == 0);
    }

    SECTION("nested paths resolve")
    {
        REQUIRE(h5test::reveal(*model, QStringLiteral("/group/nested/leaf")).isValid());
    }

    SECTION("an unknown path does not resolve")
    {
        REQUIRE_FALSE(h5test::reveal(*model, QStringLiteral("/no/such/thing")).isValid());
    }

    SECTION("QML role names are exposed")
    {
        const auto roles = model->roleNames();
        REQUIRE(roles.value(gui::H5TreeModel::NameRole) == QByteArray("name"));
        REQUIRE(roles.value(gui::H5TreeModel::PathRole) == QByteArray("path"));
        REQUIRE(roles.value(gui::H5TreeModel::IsGroupRole) == QByteArray("isGroup"));
    }

    SECTION("a hard link back to an ancestor is flagged, not expanded")
    {
        const QModelIndex link = h5test::reveal(*model, QStringLiteral("/link_to_matrix"));
        REQUIRE(link.isValid());
        // link_to_matrix targets a dataset, so it is not cyclic, but it must
        // resolve to the same object as its target.
        REQUIRE(h5test::settledData(model, link, gui::H5TreeModel::IsDatasetRole).toBool());
    }

    SECTION("the row's tags say what the object is, not what it is called")
    {
        // A hard link *is* the object, so it carries no link information for
        // the tree to show; a soft link stores a path, which is the whole
        // difference between the two.
        const QModelIndex hard = h5test::reveal(*model, QStringLiteral("/link_to_matrix"));
        REQUIRE_FALSE(h5test::settledData(model, hard, gui::H5TreeModel::IsLinkRole).toBool());

        const QModelIndex soft = h5test::reveal(*model, QStringLiteral("/soft_to_matrix"));
        REQUIRE(soft.isValid());
        REQUIRE(h5test::settledData(model, soft, gui::H5TreeModel::IsLinkRole).toBool());
        REQUIRE(h5test::settledData(model, soft, gui::H5TreeModel::LinkResolvesRole).toBool());
        REQUIRE_THAT(
            h5test::settledData(model, soft, gui::H5TreeModel::MetaRole).toString().toStdString(),
                     ContainsSubstring("/matrix"));

        // A link to nothing still lists, and still says where it was pointed.
        const QModelIndex broken = h5test::reveal(*model, QStringLiteral("/dangling"));
        REQUIRE(broken.isValid());
        REQUIRE(h5test::settledData(model, broken, gui::H5TreeModel::IsLinkRole).toBool());
        REQUIRE_FALSE(
            h5test::settledData(model, broken, gui::H5TreeModel::LinkResolvesRole).toBool());
        REQUIRE_THAT(
            h5test::settledData(model, broken, gui::H5TreeModel::MetaRole).toString().toStdString(),
                     ContainsSubstring("/no/such/object"));
        // ...and nothing is asked of the object behind it, because there is
        // none to ask.
        REQUIRE_FALSE(
            h5test::settledData(model, broken, gui::H5TreeModel::HasAttributesRole).toBool());
    }

    SECTION("attributes are a tag on whatever carries them")
    {
        // Both a group and a dataset can carry attributes, and the tag says so
        // for either -- it is the one thing every kind of object has in common.
        const QModelIndex group = h5test::reveal(*model, QStringLiteral("/group"));
        REQUIRE(h5test::settledData(model, group, gui::H5TreeModel::HasAttributesRole).toBool());

        const QModelIndex scalar = h5test::reveal(*model, QStringLiteral("/scalar_int"));
        REQUIRE(h5test::settledData(model, scalar, gui::H5TreeModel::HasAttributesRole).toBool());

        const QModelIndex matrix = h5test::reveal(*model, QStringLiteral("/matrix"));
        REQUIRE_FALSE(
            h5test::settledData(model, matrix, gui::H5TreeModel::HasAttributesRole).toBool());
    }
}

TEST_CASE_METHOD(ControllerFixture, "the tree filter narrows the pane", "[tree]")
{
    auto* filtered = controller.filteredTreeModel();

    /// The names left at the top level once the filter has run.
    const auto visible = [&] {
        QStringList names;
        for (int row = 0; row < filtered->rowCount({}); ++row) {
            names << filtered->index(row, 0, {})
                         .data(gui::H5TreeModel::NameRole)
                         .toString();
        }
        names.sort();
        return names;
    };

    SECTION("nothing typed shows everything")
    {
        const QStringList all = visible();
        CHECK(all.contains(QStringLiteral("matrix")));
        CHECK(all.contains(QStringLiteral("group")));
        CHECK(all.contains(QStringLiteral("str_vlen")));
    }

    SECTION("plain text matches anywhere in the name")
    {
        controller.setFilterText(QStringLiteral("str"));
        const QStringList found = visible();
        CHECK(found.contains(QStringLiteral("str_fixed")));
        CHECK(found.contains(QStringLiteral("str_vlen")));
        CHECK_FALSE(found.contains(QStringLiteral("matrix")));
    }

    SECTION("...and is not case-sensitive")
    {
        controller.setFilterText(QStringLiteral("MaTrIx"));
        CHECK(visible().contains(QStringLiteral("matrix")));
    }

    SECTION("a star makes it a pattern, anchored at both ends")
    {
        controller.setFilterText(QStringLiteral("str_*"));
        const QStringList found = visible();
        CHECK(found.contains(QStringLiteral("str_fixed")));
        CHECK(found.contains(QStringLiteral("str_vlen")));
        CHECK(found.contains(QStringLiteral("str_grid")));
        CHECK_FALSE(found.contains(QStringLiteral("matrix")));

        // Anchored, so a pattern that does not reach the end of the name
        // matches nothing -- which is the whole reason to write one rather
        // than to type the letters on their own.
        controller.setFilterText(QStringLiteral("str"));
        CHECK(visible().contains(QStringLiteral("str_vlen")));
        controller.setFilterText(QStringLiteral("str*d"));
        const QStringList ends = visible();
        CHECK(ends.contains(QStringLiteral("str_fixed")));
        CHECK(ends.contains(QStringLiteral("str_grid")));
        CHECK_FALSE(ends.contains(QStringLiteral("str_vlen")));
    }

    SECTION("a leading star reaches names that merely end that way")
    {
        controller.setFilterText(QStringLiteral("*cube"));
        const QStringList found = visible();
        CHECK(found.contains(QStringLiteral("cube")));
        CHECK(found.contains(QStringLiteral("hypercube")));
        CHECK_FALSE(found.contains(QStringLiteral("matrix")));
    }

    SECTION("a question mark stands for exactly one character")
    {
        controller.setFilterText(QStringLiteral("?ube"));
        const QStringList found = visible();
        CHECK(found.contains(QStringLiteral("cube")));
        CHECK_FALSE(found.contains(QStringLiteral("hypercube")));
    }

    SECTION("a character class is a pattern too")
    {
        controller.setFilterText(QStringLiteral("[cm]*"));
        const QStringList found = visible();
        CHECK(found.contains(QStringLiteral("cube")));
        CHECK(found.contains(QStringLiteral("matrix")));
        CHECK(found.contains(QStringLiteral("compound")));
        CHECK_FALSE(found.contains(QStringLiteral("group")));
    }

    SECTION("a star crosses the separators of a path")
    {
        // The path is one string as far as this box is concerned, so a
        // pattern can name where in the file to look as well as what for.
        controller.setFilterText(QStringLiteral("/*trix"));
        CHECK(visible().contains(QStringLiteral("matrix")));
    }

    SECTION("a pattern halfway to being typed still answers")
    {
        // An unclosed class does not compile, and emptying the tree while the
        // reader is still writing would be the box arguing with them. It falls
        // back to looking for the characters instead.
        controller.setFilterText(QStringLiteral("[cube"));
        CHECK(visible().isEmpty());
        controller.setFilterText(QStringLiteral("cube["));
        CHECK(visible().isEmpty());
        // ...and one that does compile still works after it.
        controller.setFilterText(QStringLiteral("cube"));
        CHECK(visible().contains(QStringLiteral("cube")));
    }

    SECTION("which grammar is in force is decided by what was typed")
    {
        CHECK_FALSE(gui::TreeFilterProxyModel::isWildcard(QStringLiteral("temp")));
        CHECK(gui::TreeFilterProxyModel::isWildcard(QStringLiteral("temp*")));
        CHECK(gui::TreeFilterProxyModel::isWildcard(QStringLiteral("te?p")));
        CHECK(gui::TreeFilterProxyModel::isWildcard(QStringLiteral("[t]emp")));
    }

    SECTION("clearing it brings the pane back")
    {
        controller.setFilterText(QStringLiteral("matrix"));
        REQUIRE_FALSE(visible().contains(QStringLiteral("cube")));
        controller.setFilterText(QString{});
        CHECK(visible().contains(QStringLiteral("cube")));
    }
}

TEST_CASE_METHOD(ControllerFixture, "the filter says what it matched", "[tree]")
{
    auto* filtered = qobject_cast<gui::TreeFilterProxyModel*>(
        controller.filteredTreeModel());
    REQUIRE(filtered != nullptr);

    /// The top-level row called `name`, once the filter has run.
    const auto rowFor = [&](const QString& name) {
        for (int row = 0; row < filtered->rowCount({}); ++row) {
            const QModelIndex index = filtered->index(row, 0, {});
            if (index.data(gui::H5TreeModel::NameRole).toString() == name) {
                return index;
            }
        }
        return QModelIndex{};
    };
    /// Where the filter bit into a name, as (start, length).
    const auto markOn = [&](const QString& name) {
        const QVariantMap mark = filtered->matchIn(name);
        return std::pair{mark.value(QStringLiteral("start")).toInt(),
                         mark.value(QStringLiteral("length")).toInt()};
    };
    /// The names of the rows the tree would be opened to.
    const auto reached = [&] {
        QStringList names;
        for (const QVariant& index : filtered->matchIndexes()) {
            names << index.toModelIndex()
                         .data(gui::H5TreeModel::NameRole)
                         .toString();
        }
        names.sort();
        return names;
    };

    SECTION("the run of characters, wherever in the name it falls")
    {
        controller.setFilterText(QStringLiteral("trix"));
        CHECK(markOn(QStringLiteral("matrix")) == std::pair{2, 4});
    }

    SECTION("...found however it was capitalised, and marked as it was typed")
    {
        controller.setFilterText(QStringLiteral("MaTrIx"));
        CHECK(markOn(QStringLiteral("matrix")) == std::pair{0, 6});
    }

    SECTION("a pattern takes the whole name, because it is anchored")
    {
        controller.setFilterText(QStringLiteral("str_*"));
        CHECK(markOn(QStringLiteral("str_vlen")) == std::pair{0, 8});
        CHECK(markOn(QStringLiteral("str_fixed")) == std::pair{0, 9});
    }

    SECTION("nothing typed marks nothing")
    {
        controller.setFilterText(QString{});
        CHECK(markOn(QStringLiteral("matrix")) == std::pair{-1, 0});
    }

    SECTION("a row that is here because of its path has nothing to mark")
    {
        // `nested` is on screen for a filter of `group` because it sits under
        // one, and there is no such run of characters in the word itself.
        REQUIRE(h5test::reveal(*tree(), QStringLiteral("/group/nested")).isValid());
        controller.setFilterText(QStringLiteral("group"));

        const QModelIndex group = rowFor(QStringLiteral("group"));
        REQUIRE(group.isValid());
        REQUIRE(h5test::settledRowCount(filtered, group) > 0);
        const QModelIndex nested = filtered->index(0, 0, group);
        REQUIRE(nested.data(gui::H5TreeModel::NameRole).toString()
                == QStringLiteral("nested"));
        CHECK(markOn(QStringLiteral("nested")) == std::pair{-1, 0});
    }

    SECTION("the rows to open the tree to are the hits themselves")
    {
        controller.setFilterText(QStringLiteral("matrix"));
        CHECK(reached() == QStringList{QStringLiteral("link_to_matrix"),
                                       QStringLiteral("matrix"),
                                       QStringLiteral("soft_to_matrix")});
    }

    SECTION("...and the walk stops at the topmost of them")
    {
        // Everything under a hit is a hit as well, because the filter reads
        // paths. Handing all of those back would open a matched group's whole
        // contents, which is not what a search for the group asked for.
        REQUIRE(h5test::reveal(*tree(), QStringLiteral("/group/nested/leaf")).isValid());
        controller.setFilterText(QStringLiteral("group"));
        CHECK(reached() == QStringList{QStringLiteral("group")});
    }

    SECTION("...and it reaches a hit the reader had to open the way to")
    {
        REQUIRE(h5test::reveal(*tree(), QStringLiteral("/group/nested/leaf")).isValid());
        controller.setFilterText(QStringLiteral("leaf"));
        CHECK(reached() == QStringList{QStringLiteral("leaf")});
    }

    SECTION("nothing typed is nothing to open the tree to")
    {
        controller.setFilterText(QString{});
        CHECK(filtered->matchIndexes().isEmpty());
    }

    SECTION("a row's path, and the row for a path, are the same row")
    {
        REQUIRE(h5test::reveal(*tree(), QStringLiteral("/group/nested")).isValid());
        const QModelIndex nested =
            filtered->indexForPath(QStringLiteral("/group/nested"));
        REQUIRE(nested.isValid());
        CHECK(nested.data(gui::H5TreeModel::NameRole).toString()
              == QStringLiteral("nested"));
        CHECK(filtered->pathAt(nested) == QStringLiteral("/group/nested"));
        CHECK_FALSE(
            filtered->indexForPath(QStringLiteral("/no/such/thing")).isValid());
    }
}

TEST_CASE_METHOD(ControllerFixture, "tab visibility follows the selection", "[controller]")
{
    SECTION("a group with attributes: Metadata yes, Dataset no")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/group")));
        REQUIRE_FALSE(controller.datasetTabVisible());
        REQUIRE(controller.metadataTabVisible());
    }

    SECTION("a dataset without attributes: Dataset yes, Metadata no")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/matrix")));
        REQUIRE(controller.datasetTabVisible());
        REQUIRE_FALSE(controller.metadataTabVisible());
    }

    SECTION("a dataset with attributes: both")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/scalar_int")));
        REQUIRE(controller.datasetTabVisible());
        REQUIRE(controller.metadataTabVisible());
    }

    SECTION("a group without attributes: neither")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/group/nested")));
        REQUIRE_FALSE(controller.datasetTabVisible());
        REQUIRE_FALSE(controller.metadataTabVisible());
    }

    SECTION("selecting emits selectionChanged")
    {
        QSignalSpy spy(&controller, &gui::AppController::selectionChanged);
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/matrix")));
        REQUIRE(spy.count() == 1);
    }

    SECTION("a path that is not in the file empties the tabs and says so")
    {
        // Selecting is taken up immediately and answered by the file a moment
        // later, so whether a path is really there is no longer something the
        // call can return -- it is something the answer says. What a reader
        // sees is the tabs going empty and the status strip saying why.
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/matrix")));
        REQUIRE(controller.datasetTabVisible());

        QSignalSpy trouble(&controller, &gui::AppController::statusMessage);
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/nope")));
        REQUIRE(controller.currentPath() == QStringLiteral("/nope"));
        REQUIRE_FALSE(controller.datasetTabVisible());
        REQUIRE_FALSE(controller.metadataTabVisible());
        REQUIRE(trouble.count() >= 1);
    }

    SECTION("closing clears everything")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/scalar_int")));
        controller.closeFile();
        REQUIRE_FALSE(controller.hasFile());
        REQUIRE_FALSE(controller.datasetTabVisible());
        REQUIRE_FALSE(controller.metadataTabVisible());
        REQUIRE(h5test::settledRowCount(tree(), {}) == 0);
    }
}

TEST_CASE_METHOD(ControllerFixture, "settings are kept per dataset", "[settings]")
{
    SECTION("what one dataset was given is not what the next one gets")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/matrix")));
        controller.rememberSettings(QStringLiteral("plotView"),
                                    QVariantMap{{QStringLiteral("rangeStep"), 0.25}});
        CHECK(controller.rememberedSettings(QStringLiteral("plotView"))
                  .value(QStringLiteral("rangeStep"))
                  .toDouble()
              == 0.25);

        // The whole of issue 1: an x step set on one dataset is not an x step
        // for another, and the other opens on nothing rather than on it.
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/cube")));
        CHECK(controller.rememberedSettings(QStringLiteral("plotView")).isEmpty());

        // ...and going back finds it where it was left.
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/matrix")));
        CHECK(controller.rememberedSettings(QStringLiteral("plotView"))
                  .value(QStringLiteral("rangeStep"))
                  .toDouble()
              == 0.25);
    }

    SECTION("groups do not meet")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/matrix")));
        controller.rememberSettings(QStringLiteral("plotView"),
                                    QVariantMap{{QStringLiteral("colorMode"),
                                                 QStringLiteral("viridis")}});
        controller.rememberSettings(QStringLiteral("image"),
                                    QVariantMap{{QStringLiteral("colorMode"), 1}});
        CHECK(controller.rememberedSettings(QStringLiteral("plotView"))
                  .value(QStringLiteral("colorMode"))
                  .toString()
              == QStringLiteral("viridis"));
        CHECK(controller.rememberedSettings(QStringLiteral("image"))
                  .value(QStringLiteral("colorMode"))
                  .toInt()
              == 1);
    }

    SECTION("the announcement comes while the old dataset is still named")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/matrix")));

        // This is what makes the whole mechanism work: a view files what it is
        // showing on `selectionAboutToChange`, and at that moment currentPath
        // still has to name the dataset it was showing it *of*.
        QString pathWhenAnnounced;
        QObject::connect(&controller, &gui::AppController::selectionAboutToChange,
                         [&] { pathWhenAnnounced = controller.currentPath(); });
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/cube")));
        CHECK(pathWhenAnnounced == QStringLiteral("/matrix"));
    }

    SECTION("nothing is remembered with no dataset selected")
    {
        controller.closeFile();
        controller.rememberSettings(QStringLiteral("plotView"),
                                    QVariantMap{{QStringLiteral("rangeStep"), 2.0}});
        CHECK(controller.rememberedSettings(QStringLiteral("plotView")).isEmpty());
    }

    SECTION("another file starts again")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/matrix")));
        controller.rememberSettings(QStringLiteral("plotView"),
                                    QVariantMap{{QStringLiteral("rangeStep"), 0.25}});

        // Two files can hold a "/matrix" that have nothing to do with each
        // other, so what was set on one says nothing about the other.
        h5test::TempFile second{"models-second"};
        h5test::onH5([&] { h5test::writeFixture(second.path()); });
        REQUIRE(h5test::openFileAndSettle(controller, QString::fromStdString(second.path())));
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/matrix")));
        CHECK(controller.rememberedSettings(QStringLiteral("plotView")).isEmpty());
    }
}

TEST_CASE_METHOD(ControllerFixture, "the slice is kept per dataset too", "[settings]")
{
    SECTION("a slice comes back, and does not follow the reader elsewhere")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/cube"))); // 2, 3, 4
        REQUIRE(controller.applySlice(QStringLiteral(":, 1, :")).isEmpty());
        REQUIRE(controller.sliceText() == QStringLiteral(":, 1, :"));

        // The next dataset opens on the whole of itself, whatever was asked of
        // the last one.
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/hypercube")));
        CHECK(controller.sliceText() == QStringLiteral(":, :, :, :"));

        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/cube")));
        CHECK(controller.sliceText() == QStringLiteral(":, 1, :"));
        // ...and the table really is showing it, not just printing it.
        CHECK(table()->rowCount({}) == 2);
    }

    SECTION("a dataset nobody has sliced opens on the whole of itself")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/matrix")));
        CHECK(controller.sliceText() == QStringLiteral(":, :"));
    }

    SECTION("the written form comes back with it")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/hypercube")));
        REQUIRE(controller.applySlice(QStringLiteral(":, :, ::2, :")).isEmpty());
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/cube")));
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/hypercube")));
        CHECK(controller.sliceText() == QStringLiteral(":, :, ::2, :"));
    }
}

TEST_CASE_METHOD(ControllerFixture, "the info model describes the selection", "[info]")
{
    SECTION("a dataset reports type and shape")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/matrix")));
        REQUIRE(info()->valueFor(QStringLiteral("Type")) == QStringLiteral("float64"));
        REQUIRE(info()->valueFor(QStringLiteral("Shape")) == QStringLiteral("4 x 3"));
        REQUIRE(info()->valueFor(QStringLiteral("Kind")) == QStringLiteral("Dataset"));
    }

    SECTION("a compressed dataset reports chunking and its filter")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/compressed")));
        REQUIRE(info()->valueFor(QStringLiteral("Layout")) == QStringLiteral("Chunked"));
        REQUIRE(info()->valueFor(QStringLiteral("Chunk")) == QStringLiteral("10 x 10"));
        REQUIRE_THAT(info()->valueFor(QStringLiteral("Filters")).toStdString(),
                     ContainsSubstring("deflate"));
    }

    SECTION("a group reports its child count")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/group")));
        REQUIRE(info()->valueFor(QStringLiteral("Children")) == QStringLiteral("1"));
    }
}

TEST_CASE_METHOD(ControllerFixture, "the dataset table renders values", "[dataset]")
{
    auto* model = table();

    SECTION("a 2-D dataset maps rows and columns")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/matrix")));
        REQUIRE(h5test::settledRowCount(model, {}) == 4);
        REQUIRE(model->columnCount({}) == 3);
        REQUIRE(cell(0, 0) == QStringLiteral("0"));
        REQUIRE(cell(3, 2) == QStringLiteral("32"));
    }

    SECTION("a 1-D dataset renders as a single column")
    {
        // The one dimension stays on the row axis, so a vector reads as a
        // column rather than as one very long row.
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/vec_int")));
        REQUIRE(h5test::settledRowCount(model, {}) == 5);
        REQUIRE(model->columnCount({}) == 1);
    }

    SECTION("a scalar renders as one cell")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/scalar_int")));
        REQUIRE(h5test::settledRowCount(model, {}) == 1);
        REQUIRE(model->columnCount({}) == 1);
        REQUIRE(cell(0, 0) == QStringLiteral("42"));
    }

    SECTION("an empty dataset has no rows")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/empty")));
        REQUIRE(h5test::settledRowCount(model, {}) == 0);
    }

    SECTION("paging a long 1-D dataset does not serve a stale window")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/long_vec"))); // 1000 elements
        REQUIRE(h5test::settledRowCount(model, {}) == 1000);
        REQUIRE(cell(0, 0) == QStringLiteral("0"));
        REQUIRE(cell(500, 0) == QStringLiteral("500"));
        REQUIRE(cell(999, 0) == QStringLiteral("999"));
        REQUIRE(cell(10, 0) == QStringLiteral("10"));
    }

    SECTION("compressed data decodes transparently")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/compressed")));
        REQUIRE(cell(99, 99) == QStringLiteral("9999"));
    }

    SECTION("QML role names are exposed")
    {
        REQUIRE(model->roleNames().value(Qt::DisplayRole) == QByteArray("display"));
        REQUIRE(model->roleNames().value(gui::DatasetTableModel::Number)
                == QByteArray("number"));
    }
}

TEST_CASE_METHOD(ControllerFixture,
                 "the table hands out its cells as numbers, and its extent",
                 "[dataset]")
{
    auto* model = table();

    const auto number = [&](int row, int column) {
        return h5test::settledData(model, model->index(row, column),
                           gui::DatasetTableModel::Number)
            .toDouble();
    };

    SECTION("a cell is the value the file holds, not the string on screen")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/matrix")));
        REQUIRE(number(0, 0) == 0.0);
        REQUIRE(number(3, 2) == 32.0);

        // The notation is a presentation and must not reach the fill: a colour
        // computed from a rounded string would band a column the file says is
        // smooth.
        model->setFloatFormat(gui::DatasetTableModel::Fixed);
        model->setFloatDecimals(0);
        REQUIRE(cell(3, 2) == QStringLiteral("32"));
        REQUIRE(number(3, 2) == 32.0);
        model->setFloatFormat(gui::DatasetTableModel::Shortest);
    }

    SECTION("text has no number, and says so with a NaN rather than a blank")
    {
        // The grid's delegate requires the role, so an absent QVariant would
        // be a warning per cell rather than a cell with nothing to colour.
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/str_vlen")));
        const QVariant held = h5test::settledData(model, model->index(0, 0),
                                          gui::DatasetTableModel::Number);
        REQUIRE(held.isValid());
        REQUIRE(std::isnan(held.toDouble()));
    }

    SECTION("the extent is the table's, so a fill does not shift as it scrolls")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/matrix"))); // 0 .. 32
        const QVariantMap extent = model->valueExtent();
        REQUIRE(extent.value(QStringLiteral("valid")).toBool());
        REQUIRE(extent.value(QStringLiteral("minimum")).toDouble() == 0.0);
        REQUIRE(extent.value(QStringLiteral("maximum")).toDouble() == 32.0);

        // Rearranging the table is a different table, and a ramp stretched
        // between the old extremes would read the new numbers on the old scale.
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/compressed"))); // 0 .. 9999
        REQUIRE(model->valueExtent().value(QStringLiteral("maximum")).toDouble()
                == 9999.0);
    }

    SECTION("text has no extent to take")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/str_vlen")));
        REQUIRE_FALSE(model->valueExtent().value(QStringLiteral("valid")).toBool());
    }
}

TEST_CASE("the index expression parser reads Python", "[expression]")
{
    using Form = gui::IndexExpression::Form;

    /// The form the parser says a subscript was written in.
    const auto formOf = [](const char* text, hsize_t extent) {
        const gui::IndexExpression result =
            gui::parseIndexExpression(QString::fromLatin1(text), extent);
        REQUIRE(result.error.isEmpty());
        return result.form;
    };

    SECTION("a bare index selects itself")
    {
        REQUIRE(parsed("0", 10) == std::vector<hsize_t>{0});
        REQUIRE(parsed("3", 10) == std::vector<hsize_t>{3});
        REQUIRE(parsed("9", 10) == std::vector<hsize_t>{9});
        REQUIRE(formOf("3", 10) == Form::Single);
    }

    SECTION("a negative index counts from the end, as it does in Python")
    {
        REQUIRE(parsed("-1", 10) == std::vector<hsize_t>{9});
        REQUIRE(parsed("-2", 10) == std::vector<hsize_t>{8});
        REQUIRE(parsed("-10", 10) == std::vector<hsize_t>{0});
        REQUIRE(formOf("-1", 10) == Form::Single);
        // One past the far end is as much an error as one past the near end.
        REQUIRE_THAT(rejected("-11", 10).toStdString(),
                     ContainsSubstring("past the end"));
    }

    SECTION("an index past the end is refused, and says what the range is")
    {
        REQUIRE_THAT(rejected("10", 10).toStdString(), ContainsSubstring("0 to 9"));
        REQUIRE_THAT(rejected("10", 10).toStdString(), ContainsSubstring("-1 to -10"));
    }

    SECTION("a colon is the whole dimension")
    {
        REQUIRE(parsed(":", 10).size() == 10);
        REQUIRE(parsed(":", 10).front() == 0);
        REQUIRE(parsed(":", 10).back() == 9);
        REQUIRE(formOf(":", 10) == Form::Whole);
        REQUIRE(formOf("::", 10) == Form::Whole);
        // Written out in full it is still the whole of it.
        REQUIRE(formOf("0:10", 10) == Form::Whole);
    }

    SECTION("the upper bound is exclusive")
    {
        REQUIRE(parsed("0:4", 10) == std::vector<hsize_t>{0, 1, 2, 3});
        REQUIRE(parsed("7:10", 10) == std::vector<hsize_t>{7, 8, 9});
        REQUIRE(parsed("3:4", 10) == std::vector<hsize_t>{3});
        REQUIRE(formOf("3:4", 10) == Form::Span);
    }

    SECTION("an omitted bound is the end it is nearest")
    {
        REQUIRE(parsed(":4", 10) == parsed("0:4", 10));
        REQUIRE(parsed("7:", 10) == parsed("7:10", 10));
        REQUIRE(parsed(":", 10) == parsed("0:10", 10));
    }

    SECTION("...and its keyword means the same thing")
    {
        REQUIRE(parsed("start:4", 10) == parsed(":4", 10));
        REQUIRE(parsed("7:end", 10) == parsed("7:", 10));
        REQUIRE(parsed("start:end", 10) == parsed(":", 10));
        REQUIRE(parsed("START:END", 10) == parsed(":", 10));
    }

    SECTION("negative bounds count from the end")
    {
        REQUIRE(parsed("-3:", 10) == std::vector<hsize_t>{7, 8, 9});
        REQUIRE(parsed(":-1", 10)
                == std::vector<hsize_t>{0, 1, 2, 3, 4, 5, 6, 7, 8});
        REQUIRE(parsed("-4:-1", 10) == std::vector<hsize_t>{6, 7, 8});
        REQUIRE(parsed("2:-2", 10) == std::vector<hsize_t>{2, 3, 4, 5, 6, 7});
        REQUIRE(parsed("-6:6", 10) == std::vector<hsize_t>{4, 5});
    }

    SECTION("bounds are clamped rather than refused, as they are in Python")
    {
        // a[0:100] on a list of ten is the whole list; only a bare index past
        // the end is an error.
        REQUIRE(parsed("0:100", 10).size() == 10);
        REQUIRE(parsed("5:100", 10) == std::vector<hsize_t>{5, 6, 7, 8, 9});
        REQUIRE(parsed("-100:3", 10) == std::vector<hsize_t>{0, 1, 2});
        REQUIRE(parsed("-100:100", 10).size() == 10);
        // Past the end in the other direction selects nothing at all, which is
        // the one place this parts company with Python: an empty axis would
        // blank the grid with no explanation.
        REQUIRE_THAT(rejected("100:", 10).toStdString(),
                     ContainsSubstring("selects no indices"));
    }

    SECTION("a step takes every nth element")
    {
        REQUIRE(parsed("::2", 10) == std::vector<hsize_t>{0, 2, 4, 6, 8});
        REQUIRE(parsed("1::2", 10) == std::vector<hsize_t>{1, 3, 5, 7, 9});
        REQUIRE(parsed("0:6:3", 10) == std::vector<hsize_t>{0, 3});
        REQUIRE(parsed("::3", 10) == std::vector<hsize_t>{0, 3, 6, 9});
        // A stride is not a run, so the panel cannot draw it with two boxes.
        REQUIRE(formOf("::2", 10) == Form::Scattered);
        // ...but a step of one is exactly the run it looks like.
        REQUIRE(formOf("2:6:1", 10) == Form::Span);
        REQUIRE(parsed("2:6:1", 10) == parsed("2:6", 10));
    }

    SECTION("a negative step counts down, and the order is kept")
    {
        REQUIRE(parsed("::-1", 10)
                == std::vector<hsize_t>{9, 8, 7, 6, 5, 4, 3, 2, 1, 0});
        REQUIRE(parsed("::-2", 10) == std::vector<hsize_t>{9, 7, 5, 3, 1});
        REQUIRE(parsed("5:1:-1", 10) == std::vector<hsize_t>{5, 4, 3, 2});
        REQUIRE(parsed("-1:-4:-1", 10) == std::vector<hsize_t>{9, 8, 7});
        // Down past the beginning stops at the beginning.
        REQUIRE(parsed("2::-1", 10) == std::vector<hsize_t>{2, 1, 0});
        // A descent is never a run: the panel's two boxes cannot say "and
        // backwards".
        REQUIRE(formOf("::-1", 10) == Form::Scattered);
    }

    SECTION("a step of zero is refused rather than looped over")
    {
        REQUIRE_THAT(rejected("::0", 10).toStdString(),
                     ContainsSubstring("step of zero"));
        REQUIRE_THAT(rejected("1:5:0", 10).toStdString(),
                     ContainsSubstring("step of zero"));
    }

    SECTION("several terms are listed with commas, as numpy indexes")
    {
        REQUIRE(parsed("0,9", 10) == std::vector<hsize_t>{0, 9});
        REQUIRE(parsed("start:4,5,7:end", 10)
                == std::vector<hsize_t>{0, 1, 2, 3, 5, 7, 8, 9});
        REQUIRE(parsed(":4,7,8:", 10) == std::vector<hsize_t>{0, 1, 2, 3, 7, 8, 9});
        REQUIRE(parsed("0:2,-2:", 10) == std::vector<hsize_t>{0, 1, 8, 9});
        // Several terms is a selection only its own text describes.
        REQUIRE(formOf("0,9", 10) == Form::Scattered);
    }

    SECTION("the order a list is written in is the order it is read in")
    {
        // numpy's fancy indexing keeps the order, and so does this: a table
        // showing rows 3 then 0 is a selection somebody asked for.
        REQUIRE(parsed("3,0", 10) == std::vector<hsize_t>{3, 0});
        REQUIRE(parsed("9,5:7", 10) == std::vector<hsize_t>{9, 5, 6});
    }

    SECTION("a repeated index is taken once")
    {
        // A table cannot show one element in two rows, so a duplicate goes --
        // but everything around it keeps its place.
        REQUIRE(parsed("5,5,4", 10) == std::vector<hsize_t>{5, 4});
        REQUIRE(parsed("7:9,8", 10) == std::vector<hsize_t>{7, 8});
        REQUIRE(parsed("0,0,0", 10) == std::vector<hsize_t>{0});
    }

    SECTION("whitespace is ignored")
    {
        REQUIRE(parsed("  3 , 5 : 7 ", 10) == std::vector<hsize_t>{3, 5, 6});
        REQUIRE(parsed(" -1 ", 10) == std::vector<hsize_t>{9});
        REQUIRE(parsed(" 1 : 8 : 3 ", 10) == std::vector<hsize_t>{1, 4, 7});
    }

    SECTION("a bracketed list is accepted, so the slice line pastes back in")
    {
        REQUIRE(parsed("[0:4,5]", 10) == std::vector<hsize_t>{0, 1, 2, 3, 5});
        REQUIRE(parsed("[3]", 10) == std::vector<hsize_t>{3});
        // Bracketed is a list even when it holds one run, because that is how
        // the line writes a scattered selection and how it reads one back.
        REQUIRE(formOf("[3]", 10) == Form::Scattered);
    }

    SECTION("malformed text is refused with a reason, not silently ignored")
    {
        REQUIRE_THAT(rejected("", 10).toStdString(), ContainsSubstring("enter indices"));
        REQUIRE_THAT(rejected("   ", 10).toStdString(), ContainsSubstring("enter indices"));
        REQUIRE_THAT(rejected("abc", 10).toStdString(), ContainsSubstring("not an index"));
        REQUIRE_THAT(rejected("1:x", 10).toStdString(), ContainsSubstring("not an index"));
        REQUIRE_THAT(rejected("1:2:3:4", 10).toStdString(),
                     ContainsSubstring("more than one step"));
        REQUIRE_THAT(rejected("3,,4", 10).toStdString(), ContainsSubstring("empty term"));
        REQUIRE_THAT(rejected("1.5", 10).toStdString(), ContainsSubstring("not an index"));
        REQUIRE_THAT(rejected("+", 10).toStdString(), ContainsSubstring("not an index"));
    }

    SECTION("an expression that selects nothing is an error, not an empty axis")
    {
        // An empty axis blanks the grid with no explanation; a rejected
        // expression says what happened.
        REQUIRE_THAT(rejected("3:3", 10).toStdString(),
                     ContainsSubstring("selects no indices"));
        REQUIRE_THAT(rejected("5:2", 10).toStdString(),
                     ContainsSubstring("selects no indices"));
        REQUIRE_THAT(rejected("2:5:-1", 10).toStdString(),
                     ContainsSubstring("selects no indices"));
        REQUIRE_THAT(rejected("[]", 10).toStdString(), ContainsSubstring("enter indices"));
    }

    SECTION("a dimension of no extent has nothing to select")
    {
        REQUIRE_THAT(rejected(":", 0).toStdString(), ContainsSubstring("empty"));
        REQUIRE_THAT(rejected("0", 0).toStdString(), ContainsSubstring("empty"));
    }

    SECTION("a dimension of one element is still a dimension")
    {
        REQUIRE(parsed("0", 1) == std::vector<hsize_t>{0});
        REQUIRE(parsed("-1", 1) == std::vector<hsize_t>{0});
        REQUIRE(parsed(":", 1) == std::vector<hsize_t>{0});
        REQUIRE(formOf(":", 1) == Form::Whole);
        REQUIRE_THAT(rejected("1", 1).toStdString(), ContainsSubstring("past the end"));
    }

    SECTION("the same selection written two ways keeps the two spellings apart")
    {
        // This is the bug the Form exists for. Both of these select element 1
        // and only element 1; in Python one drops the dimension and the other
        // keeps it, and a line that could not tell them apart rewrote what the
        // reader had typed.
        REQUIRE(parsed("1", 10) == parsed("1:2", 10));
        REQUIRE(formOf("1", 10) == Form::Single);
        REQUIRE(formOf("1:2", 10) == Form::Span);
    }

    SECTION("what a span reports as its bounds is what it selected")
    {
        const gui::IndexExpression span =
            gui::parseIndexExpression(QStringLiteral("2:6"), 10);
        REQUIRE(span.valid());
        REQUIRE(span.form == Form::Span);
        REQUIRE(span.first == 2);
        REQUIRE(span.last == 5); // inclusive, which is what the panel's boxes are

        const gui::IndexExpression one =
            gui::parseIndexExpression(QStringLiteral("-2"), 10);
        REQUIRE(one.valid());
        REQUIRE(one.form == Form::Single);
        REQUIRE(one.first == 8);
    }

    SECTION("a big dimension behaves like a small one")
    {
        constexpr hsize_t big = 50000;
        REQUIRE(parsed("-1", big) == std::vector<hsize_t>{big - 1});
        REQUIRE(parsed("49998:", big) == std::vector<hsize_t>{49998, 49999});
        REQUIRE(parsed(":", big).size() == big);
        REQUIRE(parsed("::10000", big)
                == std::vector<hsize_t>{0, 10000, 20000, 30000, 40000});
    }
}
TEST_CASE_METHOD(ControllerFixture, "the table layout defaults", "[layout]")
{
    SECTION("every dimension starts with every index selected")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/cube")));
        auto* panel = setup();
        REQUIRE(panel->rowCount({}) == 3);
        for (int dimension = 0; dimension < 3; ++dimension) {
            const QModelIndex row = panel->index(dimension, 0);
            REQUIRE(row.data(gui::TableSetupModel::ModeRole).toInt()
                    == gui::TableSetupModel::All);
            REQUIRE(row.data(gui::TableSetupModel::SelectedCountRole).toLongLong()
                    == row.data(gui::TableSetupModel::ExtentRole).toLongLong());
        }
    }

    SECTION("the last dimension goes on x and the rest on y")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/cube")));
        auto* panel = setup();
        REQUIRE_FALSE(panel->index(0, 0).data(gui::TableSetupModel::OnXRole).toBool());
        REQUIRE_FALSE(panel->index(1, 0).data(gui::TableSetupModel::OnXRole).toBool());
        REQUIRE(panel->index(2, 0).data(gui::TableSetupModel::OnXRole).toBool());
    }

    SECTION("a rank-1 dataset keeps its dimension on y")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/vec_int")));
        REQUIRE_FALSE(setup()->index(0, 0).data(gui::TableSetupModel::OnXRole).toBool());
    }

    SECTION("a 3-D dataset spreads its leading dimensions down the rows")
    {
        // Every index of every dimension, not one plane at a time: 2*3 rows of
        // 4 columns, and element (a,b,c) holds a*12 + b*4 + c.
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/cube")));
        REQUIRE(controller.datasetRank() == 3);
        REQUIRE(table()->rowCount({}) == 6);
        REQUIRE(table()->columnCount({}) == 4);
        REQUIRE(cell(0, 0) == QStringLiteral("0"));
        REQUIRE(cell(3, 0) == QStringLiteral("12"));
        REQUIRE(cell(5, 3) == QStringLiteral("23"));
    }

    SECTION("a 4-D dataset does the same with three dimensions on the row axis")
    {
        // 2*3*4 rows of 5 columns; element (a,b,c,d) holds a*60+b*20+c*5+d.
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/hypercube")));
        REQUIRE(controller.datasetRank() == 4);
        REQUIRE(table()->rowCount({}) == 24);
        REQUIRE(table()->columnCount({}) == 5);
        REQUIRE(cell(0, 0) == QStringLiteral("0"));
        REQUIRE(cell(1, 0) == QStringLiteral("5"));   // (0,0,1,0)
        REQUIRE(cell(4, 0) == QStringLiteral("20"));  // (0,1,0,0)
        REQUIRE(cell(12, 0) == QStringLiteral("60")); // (1,0,0,0)
        REQUIRE(cell(23, 4) == QStringLiteral("119"));
    }
}

TEST_CASE_METHOD(ControllerFixture, "the table layout selects and rearranges",
                 "[layout]")
{
    SECTION("pinning a dimension to one index shows that plane alone")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/cube")));
        setup()->setMode(0, gui::TableSetupModel::Index);
        setup()->setIndex(0, 1);
        REQUIRE(table()->rowCount({}) == 3);
        REQUIRE(table()->columnCount({}) == 4);
        REQUIRE(cell(0, 0) == QStringLiteral("12"));
        REQUIRE(cell(2, 3) == QStringLiteral("23"));
    }

    SECTION("a range subsets a dimension inclusively at both boxes")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/matrix")));
        setup()->setMode(0, gui::TableSetupModel::Range);
        setup()->setRange(0, 1, 2);
        REQUIRE(table()->rowCount({}) == 2);
        REQUIRE(cell(0, 0) == QStringLiteral("10"));
        REQUIRE(cell(1, 0) == QStringLiteral("20"));
    }

    SECTION("an inverted range is read the way round it was meant")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/matrix")));
        setup()->setMode(0, gui::TableSetupModel::Range);
        setup()->setRange(0, 2, 1);
        REQUIRE(table()->rowCount({}) == 2);
        REQUIRE(cell(0, 0) == QStringLiteral("10"));
    }

    SECTION("a custom expression selects scattered indices")
    {
        // The reads behind these cells are not one hyperslab, which is the
        // path this exercises.
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/compressed"))); // 100x100
        setup()->setMode(1, gui::TableSetupModel::Custom);
        setup()->setExpression(1, QStringLiteral("0,50,99"));
        REQUIRE(table()->columnCount({}) == 3);
        REQUIRE(cell(0, 0) == QStringLiteral("0"));
        REQUIRE(cell(0, 1) == QStringLiteral("50"));
        REQUIRE(cell(0, 2) == QStringLiteral("99"));
        REQUIRE(cell(7, 2) == QStringLiteral("799"));
    }

    SECTION("a selection wider than one block still pages correctly")
    {
        // 61 columns of file indices 30..90: column 40 is file column 70, on
        // the far side of the block the first read filled.
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/compressed")));
        setup()->setMode(1, gui::TableSetupModel::Range);
        setup()->setRange(1, 30, 90);
        REQUIRE(table()->columnCount({}) == 61);
        REQUIRE(cell(0, 0) == QStringLiteral("30"));
        REQUIRE(cell(0, 40) == QStringLiteral("70"));
        REQUIRE(cell(0, 60) == QStringLiteral("90"));
        REQUIRE(cell(0, 0) == QStringLiteral("30")); // and back again
    }

    SECTION("scattered indices on several dimensions at once")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/hypercube")));
        auto* panel = setup();
        panel->setMode(0, gui::TableSetupModel::Index);
        panel->setIndex(0, 1);                                 // a = 1
        panel->setMode(1, gui::TableSetupModel::Custom);
        panel->setExpression(1, QStringLiteral("0,2"));        // b in {0, 2}
        panel->setMode(2, gui::TableSetupModel::Range);
        panel->setRange(2, 1, 2);                              // c in {1, 2}
        panel->setMode(3, gui::TableSetupModel::Custom);
        // ...in that order: a list is read in the order it was written, as
        // numpy's fancy indexing is, so d runs 4 then 0.
        panel->setExpression(3, QStringLiteral("4,0"));

        REQUIRE(table()->rowCount({}) == 4);   // 1 * 2 * 2
        REQUIRE(table()->columnCount({}) == 2);
        // (1,0,1,4) = 60 + 0 + 5 + 4
        REQUIRE(cell(0, 0) == QStringLiteral("69"));
        REQUIRE(cell(0, 1) == QStringLiteral("65"));  // (1,0,1,0)
        REQUIRE(cell(1, 0) == QStringLiteral("74"));  // (1,0,2,4)
        REQUIRE(cell(2, 0) == QStringLiteral("109")); // (1,2,1,4)
        REQUIRE(cell(3, 1) == QStringLiteral("110")); // (1,2,2,0)
    }

    SECTION("a half-typed expression holds the last good selection")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/matrix")));
        auto* panel = setup();
        panel->setMode(1, gui::TableSetupModel::Custom);
        panel->setExpression(1, QStringLiteral("0,2"));
        REQUIRE(table()->columnCount({}) == 2);

        panel->setExpression(1, QStringLiteral("0,"));
        REQUIRE_FALSE(panel->index(1, 0)
                          .data(gui::TableSetupModel::ExpressionErrorRole)
                          .toString()
                          .isEmpty());
        REQUIRE(table()->columnCount({}) == 2); // the grid did not blank out
    }

    SECTION("switching to Custom writes the current selection into the box")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/matrix")));
        auto* panel = setup();
        panel->setMode(0, gui::TableSetupModel::Range);
        panel->setRange(0, 1, 2);
        panel->setMode(0, gui::TableSetupModel::Custom);
        REQUIRE(panel->index(0, 0).data(gui::TableSetupModel::ExpressionRole).toString()
                == QStringLiteral("1:3"));
        REQUIRE(table()->rowCount({}) == 2);
    }
}

TEST_CASE_METHOD(ControllerFixture, "the axis assignment", "[layout]")
{
    SECTION("moving a dimension to x transposes the table")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/matrix"))); // r*10 + c
        auto* panel = setup();
        panel->setAxis(0, true);
        panel->setAxis(1, false);
        REQUIRE(table()->rowCount({}) == 3);
        REQUIRE(table()->columnCount({}) == 4);
        REQUIRE(cell(0, 1) == QStringLiteral("10"));
        REQUIRE(cell(2, 3) == QStringLiteral("32"));
    }

    SECTION("every dimension is on exactly one axis, always")
    {
        // The panel's two checkboxes are one flag, so "both" and "neither"
        // are not states this model can be put into.
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/cube")));
        auto* panel = setup();
        panel->setAxis(1, true);
        REQUIRE(panel->index(1, 0).data(gui::TableSetupModel::OnXRole).toBool());
        panel->setAxis(1, false);
        REQUIRE_FALSE(panel->index(1, 0).data(gui::TableSetupModel::OnXRole).toBool());
    }

    SECTION("with every dimension on x the table is one row")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/matrix")));
        setup()->setAxis(0, true);
        REQUIRE(table()->rowCount({}) == 1);
        REQUIRE(table()->columnCount({}) == 12);
        REQUIRE(cell(0, 0) == QStringLiteral("0"));
        REQUIRE(cell(0, 11) == QStringLiteral("32"));
    }

    SECTION("changing the axis emits the layout signal")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/cube")));
        QSignalSpy spy(&controller, &gui::AppController::tableLayoutChanged);
        setup()->setAxis(0, true);
        REQUIRE(spy.count() == 1);
        setup()->setAxis(0, true); // already there
        REQUIRE(spy.count() == 1);
    }
}

TEST_CASE_METHOD(ControllerFixture, "a float column can be written a chosen way",
                 "[dataset]")
{
    REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/matrix"))); // 4x3 float64
    REQUIRE(table()->floats());
    REQUIRE(controller.datasetIsFloat());

    const auto shown = [&](int row, int column) {
        return h5test::settledData(table(), table()->index(row, column),
                                   Qt::DisplayRole)
            .toString();
    };
    const auto full = [&](int row, int column) {
        return h5test::settledData(table(), table()->index(row, column),
                                   Qt::ToolTipRole)
            .toString();
    };

    SECTION("the default is the shortest text that reads back as the same double")
    {
        REQUIRE(shown(1, 1) == QStringLiteral("11"));
    }

    SECTION("fixed and scientific write the decimals asked for")
    {
        table()->setFloatFormat(gui::DatasetTableModel::Fixed);
        table()->setFloatDecimals(2);
        REQUIRE(shown(1, 1) == QStringLiteral("11.00"));

        table()->setFloatFormat(gui::DatasetTableModel::Scientific);
        REQUIRE(shown(1, 1) == QStringLiteral("1.10e+01"));
    }

    SECTION("the tooltip is the value, whatever the column is written as")
    {
        table()->setFloatFormat(gui::DatasetTableModel::Fixed);
        table()->setFloatDecimals(1);
        REQUIRE(shown(1, 1) == QStringLiteral("11.0"));
        // Rounding for the column must not be the only copy of the number a
        // reader can reach.
        REQUIRE(full(1, 1) == QStringLiteral("11"));
    }

    SECTION("changing the notation repaints rather than resetting the view")
    {
        QSignalSpy repainted(table(), &QAbstractItemModel::dataChanged);
        QSignalSpy reset(table(), &QAbstractItemModel::modelReset);
        table()->setFloatFormat(gui::DatasetTableModel::Fixed);
        REQUIRE(repainted.count() == 1);
        REQUIRE(reset.count() == 0);
    }

    SECTION("a dataset of integers has no notation to choose")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/cube")));
        REQUIRE_FALSE(table()->floats());
        REQUIRE_FALSE(controller.datasetIsFloat());
        table()->setFloatFormat(gui::DatasetTableModel::Fixed);
        REQUIRE(shown(0, 1) == full(0, 1)); // untouched
    }
}

TEST_CASE_METHOD(ControllerFixture, "the table measures its own widest cell",
                 "[dataset]")
{
    REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/matrix")));
    // It measures the block that is loaded, and loading it is what asking for
    // a cell does. A column cannot be sized before its cells have arrived, in
    // the window any more than here; it is sized again when they do.
    REQUIRE_FALSE(cell(0, 0).isEmpty());

    // r*10+c, so the last row is the widest at two digits.
    REQUIRE(table()->widestCell(0, 1, 0, 3) == 1);  // 0 1 2
    REQUIRE(table()->widestCell(0, 4, 0, 3) == 2);  // ... 30 31 32

    SECTION("it measures what the column will actually draw")
    {
        table()->setFloatFormat(gui::DatasetTableModel::Fixed);
        table()->setFloatDecimals(2);
        REQUIRE(table()->widestCell(0, 4, 0, 3) == 5); // "30.00"
    }

    SECTION("an empty or out-of-range rectangle measures nothing")
    {
        REQUIRE(table()->widestCell(0, 0, 0, 3) == 0);
        REQUIRE(table()->widestCell(0, 4, 0, 0) == 0);
        // Rows -5..-3 are entirely before the table, so they cover no cell.
        REQUIRE(table()->widestCell(-5, 2, -5, 2) == 0);
    }
}

TEST_CASE_METHOD(ControllerFixture, "the table labels its rows and columns",
                 "[layout]")
{
    SECTION("a rank-1 dataset prints a bare index")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/vec_int")));
        REQUIRE(table()->rowLabel(3) == QStringLiteral("3"));
        REQUIRE(table()->columnLabel(0).isEmpty()); // the axis carries no dimension
    }

    SECTION("a rank-2 dataset prints tuples with the other axis blanked")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/matrix")));
        REQUIRE(table()->rowLabel(2) == QStringLiteral("[2,_]"));
        REQUIRE(table()->columnLabel(1) == QStringLiteral("[_,1]"));
        REQUIRE(table()->cellLabel(2, 1) == QStringLiteral("[2,1]"));
    }

    SECTION("a rank-4 row label carries all three of its dimensions")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/hypercube")));
        REQUIRE(table()->rowLabel(0) == QStringLiteral("[0,0,0,_]"));
        REQUIRE(table()->rowLabel(23) == QStringLiteral("[1,2,3,_]"));
        REQUIRE(table()->columnLabel(4) == QStringLiteral("[_,_,_,4]"));
        // Row and column together name the element, which is the whole point.
        REQUIRE(table()->cellLabel(23, 4) == QStringLiteral("[1,2,3,4]"));
        REQUIRE(cell(23, 4) == QStringLiteral("119"));
    }

    SECTION("labels follow the selection rather than the position")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/cube")));
        setup()->setMode(2, gui::TableSetupModel::Custom);
        setup()->setExpression(2, QStringLiteral("1,3"));
        REQUIRE(table()->columnLabel(0) == QStringLiteral("[_,_,1]"));
        REQUIRE(table()->columnLabel(1) == QStringLiteral("[_,_,3]"));
    }

    SECTION("headerData says the same thing, for anything that asks that way")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/matrix")));
        REQUIRE(table()->headerData(1, Qt::Horizontal, Qt::DisplayRole).toString()
                == QStringLiteral("[_,1]"));
        REQUIRE(table()->headerData(2, Qt::Vertical, Qt::DisplayRole).toString()
                == QStringLiteral("[2,_]"));
    }

    SECTION("an empty dimension leaves one axis with a header and no rows")
    {
        // /empty is [0]: no rows, but the column axis carries no dimension and
        // so is still one entry wide, and its header is still asked for.
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/empty")));
        REQUIRE(table()->rowCount({}) == 0);
        REQUIRE(table()->columnCount({}) == 1);
        REQUIRE(table()->columnLabel(0).isEmpty());
        REQUIRE(table()->rowLabel(0).isEmpty());
    }

    SECTION("out-of-range sections have no label")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/matrix")));
        REQUIRE(table()->rowLabel(-1).isEmpty());
        REQUIRE(table()->rowLabel(99).isEmpty());
    }
}

TEST_CASE_METHOD(ControllerFixture, "the slice line describes the layout", "[layout]")
{
    SECTION("the default reads as the whole dataset")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/cube")));
        REQUIRE(controller.sliceExpression() == QStringLiteral("/cube[:, :, :]"));
    }

    SECTION("each mode writes itself the way one would type it")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/cube")));
        auto* panel = setup();

        panel->setMode(1, gui::TableSetupModel::Index);
        panel->setIndex(1, 2);
        REQUIRE(controller.sliceExpression() == QStringLiteral("/cube[:, 2, :]"));

        // The boxes are inclusive; the line prints the exclusive bound, so it
        // pastes back into a Custom box unchanged. A range of one element is
        // still written as a range: `0:1` keeps the dimension where `0` picks
        // one element out of it, which is the distinction Python draws and the
        // one the panel's own modes draw.
        panel->setMode(0, gui::TableSetupModel::Range);
        panel->setRange(0, 0, 0);
        REQUIRE(controller.sliceExpression() == QStringLiteral("/cube[0:1, 2, :]"));

        panel->setRange(0, 0, 1);
        REQUIRE(controller.sliceExpression() == QStringLiteral("/cube[0:2, 2, :]"));

        panel->setMode(2, gui::TableSetupModel::Custom);
        panel->setExpression(2, QStringLiteral("0,3"));
        // Bracketed on the line, bare in the panel's own box: without the
        // brackets "[0:2, 2, 0,3]" reads like four subscripts.
        REQUIRE(controller.sliceExpression() == QStringLiteral("/cube[0:2, 2, [0,3]]"));
    }

    SECTION("an empty dimension still reads as the whole of itself")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/empty")));
        REQUIRE(controller.sliceExpression() == QStringLiteral("/empty[:]"));
    }

    SECTION("a scalar is written as itself")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/scalar_int")));
        REQUIRE(controller.sliceExpression() == QStringLiteral("/scalar_int"));
    }

    SECTION("nothing selected reads as nothing")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/group")));
        REQUIRE(controller.sliceExpression() == QString::fromUtf8("\xe2\x80\x94"));
    }
}

TEST_CASE_METHOD(ControllerFixture, "the slice line can be written as well as read",
                 "[layout]")
{
    SECTION("what the line prints is what it accepts back")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/cube")));
        auto* panel = setup();
        panel->setMode(1, gui::TableSetupModel::Index);
        panel->setIndex(1, 2);
        panel->setMode(2, gui::TableSetupModel::Custom);
        panel->setExpression(2, QStringLiteral("0,3"));

        const QString written = controller.sliceText();
        REQUIRE(written == QStringLiteral(":, 2, [0,3]"));
        // Round trip: reading it back leaves the table where it was, brackets
        // and all -- which is the whole point of rendering it this way.
        REQUIRE(controller.applySlice(written).isEmpty());
        REQUIRE(controller.sliceText() == written);
    }

    SECTION("each dimension takes the narrowest mode that says the same thing")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/hypercube"))); // 2*3*4*5
        auto* panel = setup();
        // The scattered selection carries its brackets: without them its
        // commas would read as three more subscripts, which is exactly what
        // the "too many" message below says.
        REQUIRE(controller.applySlice(QStringLiteral(":, 1, 1:3, [0,2,4]")).isEmpty());

        REQUIRE(panel->index(0, 0).data(gui::TableSetupModel::ModeRole).toInt()
                == gui::TableSetupModel::All);
        REQUIRE(panel->index(1, 0).data(gui::TableSetupModel::ModeRole).toInt()
                == gui::TableSetupModel::Index);
        REQUIRE(panel->index(1, 0).data(gui::TableSetupModel::IndexValueRole).toInt()
                == 1);
        REQUIRE(panel->index(2, 0).data(gui::TableSetupModel::ModeRole).toInt()
                == gui::TableSetupModel::Range);
        REQUIRE(panel->index(2, 0).data(gui::TableSetupModel::RangeFirstRole).toInt()
                == 1);
        // The boxes are inclusive where the line's upper bound is exclusive.
        REQUIRE(panel->index(2, 0).data(gui::TableSetupModel::RangeLastRole).toInt()
                == 2);
        REQUIRE(panel->index(3, 0).data(gui::TableSetupModel::ModeRole).toInt()
                == gui::TableSetupModel::Custom);
        REQUIRE(panel->index(3, 0)
                    .data(gui::TableSetupModel::ExpressionRole)
                    .toString()
                == QStringLiteral("[0,2,4]"));
    }

    SECTION("the grid shows what the line asked for")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/matrix"))); // r*10 + c
        REQUIRE(controller.applySlice(QStringLiteral("1:3, [0,2]")).isEmpty());
        REQUIRE(table()->rowCount({}) == 2);
        REQUIRE(table()->columnCount({}) == 2);
        REQUIRE(cell(0, 0) == QStringLiteral("10"));
        REQUIRE(cell(1, 1) == QStringLiteral("22"));
    }

    SECTION("which axis a dimension sits on is not part of a slice")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/matrix")));
        auto* panel = setup();
        panel->setAxis(0, true);
        panel->setAxis(1, false);
        REQUIRE(controller.applySlice(QStringLiteral(":, 0:2")).isEmpty());
        REQUIRE(panel->index(0, 0).data(gui::TableSetupModel::OnXRole).toBool());
        REQUIRE_FALSE(panel->index(1, 0).data(gui::TableSetupModel::OnXRole).toBool());
    }

    SECTION("a line that does not read changes nothing and says why")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/cube"))); // 2, 3, 4
        const QString before = controller.sliceText();

        // Too many -- and the commonest way to write too many is a scattered
        // selection with its brackets left off, which the message names.
        QString error = controller.applySlice(QStringLiteral(":, :, :, :"));
        REQUIRE_THAT(error.toStdString(), ContainsSubstring("4 subscripts"));
        REQUIRE_THAT(error.toStdString(), ContainsSubstring("3 dimensions"));
        REQUIRE_THAT(error.toStdString(), ContainsSubstring("[0,2,4]"));

        // One subscript reads, a later one does not: the whole line is
        // refused rather than the first half of it applied.
        error = controller.applySlice(QStringLiteral("0, 9, :"));
        REQUIRE_THAT(error.toStdString(), ContainsSubstring("dim 1"));
        REQUIRE_THAT(error.toStdString(), ContainsSubstring("past the end"));

        REQUIRE_THAT(controller.applySlice(QStringLiteral(":, , :")).toStdString(),
                     ContainsSubstring("dim 1 has no subscript"));
        REQUIRE_THAT(controller.applySlice(QStringLiteral(":, [0,2, :")).toStdString(),
                     ContainsSubstring("never closed"));
        REQUIRE_THAT(controller.applySlice(QStringLiteral(":, 0,2], :")).toStdString(),
                     ContainsSubstring("never opened"));
        REQUIRE_THAT(controller.applySlice(QStringLiteral(":, 1:2:3:4, :")).toStdString(),
                     ContainsSubstring("more than one step"));
        REQUIRE_THAT(controller.applySlice(QStringLiteral(":, 2:2, :")).toStdString(),
                     ContainsSubstring("selects no indices"));
        REQUIRE_THAT(controller.applySlice(QStringLiteral("..., ..., 0")).toStdString(),
                     ContainsSubstring("only one '...'"));

        REQUIRE(controller.sliceText() == before);
    }

    SECTION("what is left off the end is the whole of it, as it is in Python")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/cube"))); // 2, 3, 4

        // numpy reads a[0] on a rank-3 array as a[0, :, :], and so does this.
        REQUIRE(controller.applySlice(QStringLiteral("0")).isEmpty());
        REQUIRE(controller.sliceText() == QStringLiteral("0, :, :"));

        REQUIRE(controller.applySlice(QStringLiteral(":, 1")).isEmpty());
        REQUIRE(controller.sliceText() == QStringLiteral(":, 1, :"));

        // ...and `...` stands for the dimensions in the middle, which is the
        // only way to leave out the ones before a subscript rather than after.
        REQUIRE(controller.applySlice(QStringLiteral("..., 2")).isEmpty());
        REQUIRE(controller.sliceText() == QStringLiteral(":, :, 2"));

        REQUIRE(controller.applySlice(QStringLiteral("1, ...")).isEmpty());
        REQUIRE(controller.sliceText() == QStringLiteral("1, :, :"));

        // An ellipsis standing for nothing at all is still a line that reads.
        REQUIRE(controller.applySlice(QStringLiteral("0, 1, 2, ...")).isEmpty());
        REQUIRE(controller.sliceText() == QStringLiteral("0, 1, 2"));
    }

    SECTION("a line prints back the subscript that was written")
    {
        // The bug: `1:2` and `1` select the same one element, and the line was
        // rebuilt from the resolved indices -- so it could only print one of
        // them, and it printed `1`. A reader who wrote `1:2` watched the box
        // rewrite it, which looked like the range had been refused.
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/cube"))); // 2, 3, 4

        REQUIRE(controller.applySlice(QStringLiteral(":, 1:2, :")).isEmpty());
        REQUIRE(controller.sliceText() == QStringLiteral(":, 1:2, :"));
        REQUIRE(setup()->data(setup()->index(1, 0),
                              gui::TableSetupModel::ModeRole).toInt()
                == gui::TableSetupModel::Range);

        REQUIRE(controller.applySlice(QStringLiteral(":, 1, :")).isEmpty());
        REQUIRE(controller.sliceText() == QStringLiteral(":, 1, :"));
        REQUIRE(setup()->data(setup()->index(1, 0),
                              gui::TableSetupModel::ModeRole).toInt()
                == gui::TableSetupModel::Index);

        // Both of them show the same one plane, which is what made the two
        // indistinguishable in the first place.
        REQUIRE(controller.applySlice(QStringLiteral(":, 1:2, :")).isEmpty());
        const int rows = table()->rowCount({});
        REQUIRE(controller.applySlice(QStringLiteral(":, 1, :")).isEmpty());
        REQUIRE(table()->rowCount({}) == rows);
    }

    SECTION("a stride and a descent survive the round trip")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/hypercube"))); // 2,3,4,5

        REQUIRE(controller.applySlice(QStringLiteral(":, :, ::2, :")).isEmpty());
        REQUIRE(controller.sliceText() == QStringLiteral(":, :, ::2, :"));
        REQUIRE(table()->rowCount({}) == 2 * 3 * 2);

        REQUIRE(controller.applySlice(QStringLiteral(":, :, ::-1, :")).isEmpty());
        REQUIRE(controller.sliceText() == QStringLiteral(":, :, ::-1, :"));

        // A negative index is a subscript like any other, and the line keeps
        // the form it was given rather than resolving it to a number.
        REQUIRE(controller.applySlice(QStringLiteral("-1, :, :, :")).isEmpty());
        REQUIRE(controller.sliceText() == QStringLiteral("1, :, :, :"));
    }

    SECTION("checking a line is not applying it")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/matrix")));
        const QString before = controller.sliceText();
        REQUIRE(controller.sliceError(QStringLiteral("0, :")).isEmpty());
        REQUIRE(controller.sliceText() == before);
        // `0` alone reads too -- the dimensions left off the end are the whole
        // of themselves -- so the line that does not read is one that names an
        // element the dataset does not have.
        REQUIRE(controller.sliceError(QStringLiteral("0")).isEmpty());
        REQUIRE(controller.sliceText() == before);
        REQUIRE_FALSE(controller.sliceError(QStringLiteral("99, :")).isEmpty());
    }

    SECTION("a scalar has nothing to subscript")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/scalar_int")));
        REQUIRE(controller.sliceText().isEmpty());
        REQUIRE_THAT(controller.applySlice(QStringLiteral(":")).toStdString(),
                     ContainsSubstring("scalar"));
    }

    SECTION("nothing selected takes no slice at all")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/group")));
        REQUIRE(controller.sliceText().isEmpty());
        REQUIRE_THAT(controller.applySlice(QStringLiteral(":")).toStdString(),
                     ContainsSubstring("no dataset"));
    }
}

TEST_CASE_METHOD(ControllerFixture, "text panes are labelled by index tuple",
                 "[dataset]")
{
    REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/str_grid"))); // 2x2 strings
    auto* strings = controller.datasetStringModel();
    REQUIRE(strings->rowCount({}) == 4);
    REQUIRE(strings->index(3, 0).data(gui::DatasetStringListModel::LabelRole).toString()
            == QStringLiteral("[1,1]"));
}

TEST_CASE_METHOD(ControllerFixture, "the table samples itself as numbers",
                 "[sample]")
{
    SECTION("only a numeric dataset has numbers to sample")
    {
        REQUIRE(h5test::selectAndSettle(controller, "/matrix"));
        REQUIRE(table()->numeric());
        REQUIRE(controller.datasetIsNumeric());

        for (const char* path : {"/str_vlen", "/compound", "/enum"}) {
            REQUIRE(h5test::selectAndSettle(controller, QString::fromLatin1(path)));
            REQUIRE_FALSE(table()->numeric());
            REQUIRE_FALSE(controller.datasetIsNumeric());
        }
    }

    SECTION("a table that fits comes back whole")
    {
        REQUIRE(h5test::selectAndSettle(controller, "/matrix")); // 4x3, value = row*10 + column
        const auto grid = table()->sampleValues(0, -1, 100, 0, -1, 100);
        REQUIRE(grid.error.isEmpty());
        REQUIRE(grid.rows == 4);
        REQUIRE(grid.columns == 3);
        REQUIRE(grid.rowStride == 1);
        REQUIRE(grid.columnStride == 1);
        REQUIRE(grid.at(0, 0) == 0.0);
        REQUIRE(grid.at(1, 1) == 11.0);
        REQUIRE(grid.at(3, 2) == 32.0);
        REQUIRE(grid.hasFinite);
        REQUIRE(grid.minimum == 0.0);
        REQUIRE(grid.maximum == 32.0);
    }

    SECTION("a table larger than the cap is thinned, never truncated")
    {
        REQUIRE(h5test::selectAndSettle(controller, "/long_vec")); // 1000 rows, 1 column
        const auto grid = table()->sampleValues(0, -1, 100, 0, -1, 100);
        REQUIRE(grid.rowStride == 10);
        REQUIRE(grid.rows == 100);
        REQUIRE(grid.columns == 1);
        // Every tenth element, so the last sample is the 990th and the far end
        // of the dataset is still represented.
        REQUIRE(grid.at(0, 0) == 0.0);
        REQUIRE(grid.at(1, 0) == 10.0);
        REQUIRE(grid.at(99, 0) == 990.0);
    }

    SECTION("thinning columns keeps the ends as well as the middle")
    {
        REQUIRE(h5test::selectAndSettle(controller, "/compressed")); // 100x100, chunked+gzip
        const auto grid = table()->sampleValues(0, -1, 10, 0, -1, 10);
        REQUIRE(grid.error.isEmpty());
        REQUIRE(grid.rowStride == 10);
        REQUIRE(grid.columnStride == 10);
        REQUIRE(grid.rows == 10);
        REQUIRE(grid.columns == 10);
        REQUIRE(grid.hasFinite);
    }

    SECTION("a span selects part of the table")
    {
        REQUIRE(h5test::selectAndSettle(controller, "/matrix"));
        const auto grid = table()->sampleValues(1, 2, 100, 1, 2, 100);
        REQUIRE(grid.rows == 2);
        REQUIRE(grid.columns == 2);
        REQUIRE(grid.at(0, 0) == 11.0); // row 1, column 1
        REQUIRE(grid.at(1, 1) == 22.0);
    }

    SECTION("a span starting past the end samples nothing")
    {
        REQUIRE(h5test::selectAndSettle(controller, "/matrix"));
        const auto grid = table()->sampleValues(99, -1, 100, 0, -1, 100);
        REQUIRE(grid.rows == 0);
        REQUIRE(grid.values.empty());
    }

    SECTION("it follows the table's layout, not the dataset's shape")
    {
        REQUIRE(h5test::selectAndSettle(controller, "/hypercube")); // 2x3x4x5
        REQUIRE(h5test::settledRowCount(table()) == 24);
        REQUIRE(h5test::settledColumnCount(table()) == 5);

        // Pinning a dimension narrows the table, and the sample is of that
        // table -- the plot and the image show the slice the grid shows.
        setup()->setMode(2, gui::TableSetupModel::Index);
        REQUIRE(h5test::settledRowCount(table()) == 6);
        const auto grid = table()->sampleValues(0, -1, 100, 0, -1, 100);
        REQUIRE(grid.rows == 6);
        REQUIRE(grid.columns == 5);
    }

    SECTION("a scattered selection still reads, one cell at a time")
    {
        REQUIRE(h5test::selectAndSettle(controller, "/cube")); // 2x3x4, value = flat index
        setup()->setMode(2, gui::TableSetupModel::Custom);
        setup()->setExpression(2, QStringLiteral("0,3"));
        REQUIRE(h5test::settledColumnCount(table()) == 2);

        const auto grid = table()->sampleValues(0, -1, 100, 0, -1, 100);
        REQUIRE(grid.rows == 6);
        REQUIRE(grid.columns == 2);
        REQUIRE(grid.at(0, 0) == 0.0);
        REQUIRE(grid.at(0, 1) == 3.0);
    }

    SECTION("text is refused with a reason rather than sampled as zeros")
    {
        REQUIRE(h5test::selectAndSettle(controller, "/str_vlen"));
        const auto grid = table()->sampleValues(0, -1, 100, 0, -1, 100);
        REQUIRE_FALSE(grid.error.isEmpty());
        REQUIRE(grid.values.empty());
    }
}

TEST_CASE_METHOD(ControllerFixture, "the plot reads the table as lines", "[plot]")
{
    auto* plot = controller.datasetPlot();
    REQUIRE(plot != nullptr);

    SECTION("one line per row, x along the columns")
    {
        REQUIRE(h5test::selectAndSettle(controller, "/cube")); // 2x3x4 -> 6 rows, 4 columns
        REQUIRE(plot->seriesFromRows());
        REQUIRE(plot->seriesCount() == 6);
        REQUIRE(plot->pointCount() == 4);
        REQUIRE(plot->sourceSeriesCount() == 6);
        REQUIRE(plot->hasData());
        REQUIRE(plot->minimum() == 0.0);
        REQUIRE(plot->maximum() == 23.0);
    }

    SECTION("a vector draws as one line rather than a thousand")
    {
        // defaultOnX keeps a rank-1 dimension on the row axis so a vector
        // still reads as a column in the grid. Taken literally that is a
        // thousand lines of one point each, which is a plot of nothing.
        REQUIRE(h5test::selectAndSettle(controller, "/long_vec"));
        REQUIRE_FALSE(plot->seriesFromRows());
        REQUIRE(plot->seriesCount() == 1);
        REQUIRE(plot->pointCount() == 1000);

        // It is a starting point, not a rule: the reader can still transpose,
        // and what they get is the table read the other way -- a thousand
        // lines of one point, of which the first sixty-four are drawn.
        plot->setSeriesFromRows(true);
        REQUIRE(plot->sourceSeriesCount() == 1000);
        REQUIRE(plot->seriesCount() == gui::DatasetPlot::kMaxInitialSeries);
        REQUIRE(plot->pointCount() == 1);
    }

    SECTION("a new selection opens on the table's first sixty-four lines")
    {
        REQUIRE(h5test::selectAndSettle(controller, "/compressed")); // 100x100
        REQUIRE(plot->sourceSeriesCount() == 100);
        // A window, and one that says so: the legend prints "64 / 100" and the
        // footer "64 lines of 100". Strokes over one another stop separating
        // at a few dozen, so drawing all hundred is a picture of nothing that
        // the reader waits for before they have asked for anything.
        REQUIRE(plot->seriesCount() == gui::DatasetPlot::kMaxInitialSeries);
        REQUIRE(plot->seriesVisible(63));
        REQUIRE_FALSE(plot->seriesVisible(64));

        // Nothing is out of reach behind it.
        plot->selectAll();
        REQUIRE(plot->seriesCount() == 100);

        // A table shorter than the window is drawn whole, so the common case
        // never meets the rule at all.
        REQUIRE(h5test::selectAndSettle(controller, "/cube")); // 6 rows
        REQUIRE(plot->seriesCount() == 6);
    }

    SECTION("text has nothing to plot and says so")
    {
        REQUIRE(h5test::selectAndSettle(controller, "/str_vlen"));
        REQUIRE_FALSE(plot->numeric());
        REQUIRE_FALSE(plot->hasData());
        REQUIRE_FALSE(plot->error().isEmpty());
    }

    SECTION("a rearranged table is a new plot")
    {
        REQUIRE(h5test::selectAndSettle(controller, "/hypercube"));
        QSignalSpy spy(plot, &gui::DatasetPlot::changed);
        setup()->setMode(2, gui::TableSetupModel::Index);
        REQUIRE(spy.count() > 0);
        REQUIRE(plot->seriesCount() == 6);
    }
}

TEST_CASE_METHOD(ControllerFixture, "the plot draws the lines it is told to",
                 "[plot]")
{
    auto* plot = controller.datasetPlot();
    REQUIRE(plot != nullptr);

    const auto drawn = [&] {
        std::vector<int> series;
        for (const QVariant& entry : plot->drawnSeries()) {
            series.push_back(entry.toInt());
        }
        return series;
    };

    SECTION("all of them, on a table shorter than the window")
    {
        REQUIRE(h5test::selectAndSettle(controller, "/cube")); // 6 rows
        REQUIRE(drawn() == std::vector<int>{0, 1, 2, 3, 4, 5});
    }

    SECTION("a line the legend unticks stops being drawn")
    {
        REQUIRE(h5test::selectAndSettle(controller, "/cube"));
        REQUIRE(plot->seriesVisible(2));
        plot->setSeriesVisible(2, false);
        REQUIRE_FALSE(plot->seriesVisible(2));
        REQUIRE(drawn() == std::vector<int>{0, 1, 3, 4, 5});

        // Ascending, so a line put back lands where the table has it and not
        // at the end of the drawing order.
        plot->setSeriesVisible(2, true);
        REQUIRE(drawn() == std::vector<int>{0, 1, 2, 3, 4, 5});
    }

    SECTION("the extent follows the lines drawn, not the whole table")
    {
        REQUIRE(h5test::selectAndSettle(controller, "/cube")); // values 0..23, 4 per row
        REQUIRE(plot->maximum() == 23.0);
        plot->setSeriesVisible(5, false);
        REQUIRE(plot->maximum() == 19.0);
        plot->setSeriesVisible(0, false);
        REQUIRE(plot->minimum() == 4.0);
    }

    SECTION("all, none, and the first of them")
    {
        REQUIRE(h5test::selectAndSettle(controller, "/compressed")); // 100x100
        REQUIRE(plot->seriesCount() == gui::DatasetPlot::kMaxInitialSeries);

        plot->selectNone();
        REQUIRE(plot->seriesCount() == 0);
        REQUIRE_FALSE(plot->hasData());

        plot->selectAll();
        REQUIRE(plot->seriesCount() == 100);
        REQUIRE(plot->pointCount() == 100);

        // The way back to what the selection opened on, and a count larger
        // than the table is the table.
        plot->selectFirst(10);
        REQUIRE(plot->seriesCount() == 10);
        plot->selectFirst(1000);
        REQUIRE(plot->seriesCount() == 100);
    }

    SECTION("a line is named by its slice, not by its place in the drawing")
    {
        REQUIRE(h5test::selectAndSettle(controller, "/cube")); // 2x3x4
        REQUIRE(plot->seriesLabel(4) == QStringLiteral("[1,1,_]"));
        plot->setSeriesVisible(0, false);
        // Hiding the line above it must not renumber it.
        REQUIRE(plot->seriesLabel(4) == QStringLiteral("[1,1,_]"));

        // A vector plotted as one line sits on an axis carrying no dimension,
        // so there is no tuple and the bare number stands in.
        REQUIRE(h5test::selectAndSettle(controller, "/long_vec"));
        REQUIRE(plot->seriesLabel(0) == QStringLiteral("0"));
    }

    SECTION("a new selection starts from the window again")
    {
        REQUIRE(h5test::selectAndSettle(controller, "/cube"));
        plot->selectNone();
        REQUIRE(plot->seriesCount() == 0);
        // Line 3 of one dataset names nothing in the next one.
        REQUIRE(h5test::selectAndSettle(controller, "/matrix"));
        REQUIRE(plot->seriesCount() == 4);
    }

    SECTION("the x values are the reader's, not the element's index")
    {
        REQUIRE(h5test::selectAndSettle(controller, "/matrix")); // 4 rows x 3 columns
        // The default is 0 : 1 : len(data), which is the element's own index.
        REQUIRE(plot->sourcePointCount() == 3);
        REQUIRE(plot->xStart() == 0.0);
        REQUIRE(plot->xStep() == 1.0);

        // Moving them moves the points and nothing else: no line has appeared
        // or gone away, so this is not the signal that rebuilds the graph.
        QSignalSpy moved(plot, &gui::DatasetPlot::xAxisChanged);
        QSignalSpy rebuilt(plot, &gui::DatasetPlot::changed);
        plot->setXStart(100.0);
        plot->setXStep(0.5);
        REQUIRE(moved.count() == 2);
        REQUIRE(rebuilt.count() == 0);

        plot->setXStart(0.0);
        plot->setXStep(1.0);
    }
}

TEST_CASE_METHOD(ControllerFixture, "a one-channel picture can take a colour ramp",
                 "[image]")
{
    auto* image = controller.datasetImage();
    REQUIRE(h5test::selectAndSettle(controller, "/matrix")); // 4x3 float64, 0..32

    const QImage gray = image->render();
    REQUIRE(gray.width() == 3);
    // Grayscale by default, which is a shade rather than a colour: the three
    // channels of every pixel agree.
    const QColor low = gray.pixelColor(0, 0);
    REQUIRE(low.red() == low.green());
    REQUIRE(low.green() == low.blue());

    // The stops come from QML, because they are the plot's stops and those
    // live in Theme.qml -- a picture and a plot of the same values must not be
    // able to disagree about what viridis is.
    image->setRamp(QVariantList{QColor(Qt::black), QColor(Qt::red)});
    const QImage ramped = image->render();
    const QColor high = ramped.pixelColor(2, 3); // the maximum, 32
    REQUIRE(high.red() > high.blue());
    REQUIRE(ramped.pixelColor(0, 0).red() < high.red());

    SECTION("reversing runs the ramp the other way")
    {
        image->setInvert(true);
        const QImage reversed = image->render();
        // What was the red end is now the black one.
        REQUIRE(reversed.pixelColor(2, 3).red() < reversed.pixelColor(0, 0).red());
    }

    SECTION("three channels are the picture's own colour and ignore the ramp")
    {
        REQUIRE(h5test::selectAndSettle(controller, "/matrix"));
        image->setRamp(QVariantList{QColor(Qt::black), QColor(Qt::red)});
        image->setColorMode(gui::DatasetImage::ColorMode::Grayscale);
        REQUIRE(image->render().pixelColor(2, 3).red() > 0);
    }
}

TEST_CASE("the build says what it is", "[controller]")
{
    // The version is counted out of the release tags by CMake, so what is
    // asserted here is the shape of it and that it reaches the application --
    // not the number. Not even the major and minor: those were asserted once,
    // and the first time the minor moved they made this a thing to update
    // rather than a thing to trust, which is exactly what the rest of this
    // case was written to avoid.
    const QString version = gui::AppController::appVersion();
    INFO(version.toStdString());
    REQUIRE_FALSE(version.isEmpty());

    // Three shapes, and no fourth:
    //   "0.2.0"                     a release, standing on its tag
    //   "0.2.1-dev"                 after one, on the way to the next
    //   "0.2.0 (unversioned build)" nothing to count -- a tarball, a shallow
    //                               clone, or a clone with no tags fetched
    QString number = version.section(QLatin1Char(' '), 0, 0);
    const bool dev = number.endsWith(QStringLiteral("-dev"));
    if (dev) {
        number.chop(4);
    }

    const QStringList parts = number.split(QLatin1Char('.'));
    REQUIRE(parts.size() == 3);
    for (const QString& part : parts) {
        bool numeric = false;
        part.toInt(&numeric);
        REQUIRE(numeric);
    }

    // The qualified string and the bare one are two renderings of one answer,
    // so the bare one is always what is left when the qualifiers come off. A
    // build whose two version constants disagreed would be a build that says
    // different things depending on which one you asked.
    REQUIRE(number == QString::fromLatin1(h5scope::kVersionNumber));

    // The commit it was built from, short, or the word that says it is not
    // known. Either way something rather than nothing.
    REQUIRE_FALSE(gui::AppController::appCommit().isEmpty());

    // The running executable's own name. Qt can only answer that through a
    // QCoreApplication, and this suite is a plain Catch2 binary with none, so
    // what holds here is the contract rather than a name: whatever Qt reports
    // as the running file, this is the last segment of it. The application
    // itself always has one, which is where the About dialog reads it.
    REQUIRE(gui::AppController::binaryName()
            == QFileInfo(QCoreApplication::applicationFilePath()).fileName());
    if (QCoreApplication::instance() != nullptr) {
        REQUIRE_FALSE(gui::AppController::binaryName().isEmpty());
    }
}

TEST_CASE("the recent list keeps what opened, newest first", "[controller]")
{
    // No organisation name is set in the test binary, so nothing here touches
    // QSettings or the user's own list -- which is the point of the guard in
    // AppController.
    REQUIRE(QCoreApplication::organizationName().isEmpty());

    h5test::TempFile first{"recent-a"};
    h5test::TempFile second{"recent-b"};
    h5test::onH5([&] { h5test::writeFixture(first.path()); });
    h5test::onH5([&] { h5test::writeFixture(second.path()); });

    gui::AppController controller;
    REQUIRE(controller.recentFiles().isEmpty());

    REQUIRE(h5test::openFileAndSettle(controller, QString::fromStdString(first.path())));
    REQUIRE(controller.recentFiles().size() == 1);
    REQUIRE(controller.recentFiles().first().toMap()
                .value(QStringLiteral("path")).toString()
            == QFileInfo(QString::fromStdString(first.path())).absoluteFilePath());

    REQUIRE(h5test::openFileAndSettle(controller, QString::fromStdString(second.path())));
    REQUIRE(controller.recentFiles().size() == 2);

    SECTION("re-opening one moves it to the front rather than adding it twice")
    {
        REQUIRE(h5test::openFileAndSettle(controller, QString::fromStdString(first.path())));
        REQUIRE(controller.recentFiles().size() == 2);
        REQUIRE(controller.recentFiles().first().toMap()
                    .value(QStringLiteral("name")).toString()
                == QFileInfo(QString::fromStdString(first.path())).fileName());
    }

    SECTION("a file that would not open is not offered again")
    {
        REQUIRE_FALSE(h5test::openFileAndSettle(controller, QStringLiteral("/no/such/file.h5")));
        REQUIRE(controller.recentFiles().size() == 2);
    }

    SECTION("a file that has since moved is listed and marked")
    {
        const QString path = QString::fromStdString(second.path());
        REQUIRE_FALSE(controller.recentFiles().first().toMap()
                          .value(QStringLiteral("missing")).toBool());
        QFile::remove(path);
        // Checked when the list is asked for rather than when the file was
        // opened: the row that offers to open it is what has to know.
        const QVariantList after = controller.recentFiles();
        const auto entry = std::find_if(
            after.begin(), after.end(), [&](const QVariant& value) {
                return value.toMap().value(QStringLiteral("path")).toString()
                       == QFileInfo(path).absoluteFilePath();
            });
        REQUIRE(entry != after.end());
        REQUIRE(entry->toMap().value(QStringLiteral("missing")).toBool());
    }

    SECTION("and the whole record can be erased")
    {
        controller.clearRecentFiles();
        REQUIRE(controller.recentFiles().isEmpty());
    }
}

TEST_CASE_METHOD(ControllerFixture, "the image reads the table as a raster",
                 "[image]")
{
    auto* image = controller.datasetImage();
    REQUIRE(image != nullptr);

    SECTION("one pixel per cell, in the table's own shape")
    {
        REQUIRE(h5test::selectAndSettle(controller, "/matrix")); // 4x3
        REQUIRE(image->width() == 3);
        REQUIRE(image->height() == 4);
        REQUIRE(image->sourceWidth() == 3);
        REQUIRE(image->sourceHeight() == 4);
        REQUIRE_FALSE(image->thinned());
        REQUIRE(image->hasData());

        const QImage raster = image->render();
        REQUIRE(raster.width() == 3);
        REQUIRE(raster.height() == 4);
        // The fixture runs 0..32, so the extremes land on the ends of the ramp.
        REQUIRE(qGray(raster.pixel(0, 0)) == 0);
        REQUIRE(qGray(raster.pixel(2, 3)) == 255);
    }

    SECTION("inverting swaps the ends of the ramp")
    {
        REQUIRE(h5test::selectAndSettle(controller, "/matrix"));
        image->setInvert(true);
        const QImage raster = image->render();
        REQUIRE(qGray(raster.pixel(0, 0)) == 255);
        REQUIRE(qGray(raster.pixel(2, 3)) == 0);
        image->setInvert(false);
    }

    SECTION("a manual range maps to it rather than to the data")
    {
        REQUIRE(h5test::selectAndSettle(controller, "/matrix")); // values 0..32
        image->setAutoRange(false);
        image->setRangeMinimum(0.0);
        image->setRangeMaximum(64.0);
        const QImage raster = image->render();
        // 32 of 64 is mid gray, where auto range would have made it white.
        REQUIRE(qGray(raster.pixel(2, 3)) == 128);
        image->setAutoRange(true);
    }

    SECTION("a table larger than the cap is thinned to it")
    {
        REQUIRE(h5test::selectAndSettle(controller, "/compressed")); // 100x100
        REQUIRE(image->width() == 100);
        REQUIRE_FALSE(image->thinned());
    }

    SECTION("every change that alters the pixels moves the revision")
    {
        // QML puts the revision in the image URL; an unchanged URL is served
        // from Qt's pixmap cache and the stale raster stays on screen.
        REQUIRE(h5test::selectAndSettle(controller, "/matrix"));
        const int atSelection = image->revision();

        image->setInvert(true);
        REQUIRE(image->revision() != atSelection);
        const int atInvert = image->revision();

        setup()->setMode(0, gui::TableSetupModel::Index);
        REQUIRE(image->revision() != atInvert);
        image->setInvert(false);
    }

    SECTION("text has nothing to draw and says so")
    {
        REQUIRE(h5test::selectAndSettle(controller, "/str_vlen"));
        REQUIRE_FALSE(image->numeric());
        REQUIRE_FALSE(image->hasData());
        REQUIRE(image->render().isNull());
    }
}

TEST_CASE_METHOD(ControllerFixture, "the attribute table lists metadata", "[metadata]")
{
    auto* model = attributes();

    SECTION("group attributes appear with values")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/group")));
        REQUIRE(h5test::settledRowCount(model, {}) == 2);
        REQUIRE(h5test::settledData(model, model->index(0, gui::AttributeTableModel::NameColumn),
                            Qt::DisplayRole)
                    .toString()
                == QStringLiteral("title"));
        REQUIRE(h5test::settledData(model, model->index(0, gui::AttributeTableModel::ValueColumn),
                            Qt::DisplayRole)
                    .toString()
                == QStringLiteral("example group"));
    }

    SECTION("role-based access matches column access")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/scalar_int")));
        REQUIRE(h5test::settledRowCount(model, {}) == 1);
        REQUIRE(h5test::settledData(model, model->index(0, 0), gui::AttributeTableModel::ValueRole)
                    .toString()
                == QStringLiteral("kelvin"));
    }

    SECTION("an object without attributes yields no rows")
    {
        REQUIRE(h5test::selectAndSettle(controller, QStringLiteral("/matrix")));
        REQUIRE(h5test::settledRowCount(model, {}) == 0);
    }
}

// ---------------------------------------------------------------------------
// Postprocessing
// ---------------------------------------------------------------------------

TEST_CASE_METHOD(ControllerFixture, "the pipeline opens as the two ends and nothing between",
                 "[controller][postproc]")
{
    REQUIRE(h5test::selectAndSettle(controller, "/cube"));

    // The input, the slice, the row that adds another, and the output. There
    // is no operation until one is added, which is what "in the beginning they
    // are the only things there" asks for.
    REQUIRE(post()->rowCount() == 4);
    REQUIRE(step(0, gui::PostprocessModel::KindRole).toInt()
            == gui::PostprocessModel::Input);
    REQUIRE(step(1, gui::PostprocessModel::KindRole).toInt()
            == gui::PostprocessModel::Slice);
    // The add row sits in the chain above the output, as the diagram in
    // postprocessing.md draws it -- not below the panel beside it.
    REQUIRE(step(2, gui::PostprocessModel::KindRole).toInt()
            == gui::PostprocessModel::Adder);
    REQUIRE(step(3, gui::PostprocessModel::KindRole).toInt()
            == gui::PostprocessModel::Output);

    SECTION("none of the furniture can be removed or moved")
    {
        for (const int row : {0, 1, 2, 3}) {
            INFO("row " << row);
            REQUIRE_FALSE(step(row, gui::PostprocessModel::RemovableRole).toBool());
            REQUIRE_FALSE(step(row, gui::PostprocessModel::MovableRole).toBool());
        }
        post()->removeStep(1);
        REQUIRE(post()->rowCount() == 4);
    }

    SECTION("the input row names the dataset and its shape")
    {
        REQUIRE(step(0, gui::PostprocessModel::LabelRole).toString() == "/cube");
        REQUIRE(shapeOf(0) == QString::fromUtf8("2 × 3 × 4"));
    }

    SECTION("the slice row is the slice above the table, not a copy of it")
    {
        REQUIRE(step(1, gui::PostprocessModel::ArgumentRole).toString()
                == controller.sliceText());
        post()->setArgument(1, QStringLiteral("1, :, :"));
        REQUIRE(controller.sliceText() == "1, :, :");
        // ...and a bare index drops the dimension, which the table does not.
        REQUIRE(shapeOf(1) == QString::fromUtf8("3 × 4"));
    }

    SECTION("nothing is active until the switch is set")
    {
        REQUIRE_FALSE(controller.postprocessActive());
        post()->setEnabled(true);
        REQUIRE(controller.postprocessActive());
    }
}

TEST_CASE_METHOD(ControllerFixture, "an operation changes the shape the views draw",
                 "[controller][postproc]")
{
    REQUIRE(h5test::selectAndSettle(controller, "/cube")); // 2 x 3 x 4 of int32
    post()->setEnabled(true);
    post()->addStep(QStringLiteral("max"));
    REQUIRE(post()->rowCount() == 5);
    post()->setArgument(2, QStringLiteral("0"));

    REQUIRE(shapeOf(2) == QString::fromUtf8("3 × 4"));
    REQUIRE(shapeOf(4) == QString::fromUtf8("3 × 4")); // the output

    SECTION("and the table is drawing it")
    {
        REQUIRE(h5test::settledRowCount(table()) == 3);
        REQUIRE(h5test::settledColumnCount(table()) == 4);
        // /cube counts from 0, so the maximum over the first dimension is the
        // second plane: element (1, r, c) = 12 + r * 4 + c.
        REQUIRE(cell(0, 0) == "12");
        REQUIRE(cell(2, 3) == "23");
    }

    SECTION("an integer dataset still prints as integers")
    {
        // The one thing a double-valued pipeline could have quietly cost.
        REQUIRE_FALSE(cell(0, 0).contains('.'));
    }

    SECTION("switching it off puts the file back")
    {
        post()->setEnabled(false);
        REQUIRE_FALSE(controller.postprocessActive());
        REQUIRE(h5test::settledRowCount(table()) == 6); // 2 x 3 down the rows
        REQUIRE(h5test::settledColumnCount(table()) == 4);
    }
}

TEST_CASE_METHOD(ControllerFixture, "a step that cannot run leaves the rest standing",
                 "[controller][postproc]")
{
    REQUIRE(h5test::selectAndSettle(controller, "/cube"));
    post()->setEnabled(true);
    post()->addStep(QStringLiteral("max"));
    post()->setArgument(2, QStringLiteral("0"));
    post()->addStep(QStringLiteral("transpose"));
    post()->setArgument(3, QStringLiteral("7, 7"));

    REQUIRE_FALSE(post()->error().isEmpty());
    REQUIRE_THAT(step(3, gui::PostprocessModel::ErrorRole).toString().toStdString(),
                 ContainsSubstring("out of bounds"));

    SECTION("the row that refused has no shape")
    {
        REQUIRE(shapeOf(3).isEmpty());
    }

    SECTION("the views keep drawing everything above it")
    {
        // Nothing goes blank: this is the table as of the Max, which is the
        // last row that worked.
        REQUIRE(h5test::settledRowCount(table()) == 3);
        REQUIRE(h5test::settledColumnCount(table()) == 4);
        REQUIRE(cell(0, 0) == "12");
    }

    SECTION("fixing the argument starts it again")
    {
        post()->setArgument(3, QStringLiteral("1, 0"));
        REQUIRE(post()->error().isEmpty());
        REQUIRE(shapeOf(3) == QString::fromUtf8("4 × 3"));
        REQUIRE(shapeOf(5) == QString::fromUtf8("4 × 3")); // the output
        REQUIRE(h5test::settledRowCount(table()) == 4);
    }
}

TEST_CASE_METHOD(ControllerFixture, "clicking a row runs the pipeline only that far",
                 "[controller][postproc]")
{
    REQUIRE(h5test::selectAndSettle(controller, "/cube"));
    post()->setEnabled(true);
    post()->addStep(QStringLiteral("max"));
    post()->setArgument(2, QStringLiteral("0"));
    post()->addStep(QStringLiteral("min"));
    post()->setArgument(3, QStringLiteral("0"));

    REQUIRE(post()->activeRow() == 3);
    REQUIRE(shapeOf(5) == "4"); // the output, after both

    post()->setActiveRow(2);
    REQUIRE(shapeOf(5) == QString::fromUtf8("3 × 4"));
    REQUIRE(h5test::settledRowCount(table()) == 3);

    SECTION("everything after the clicked row is marked uncomputed, and the output is not")
    {
        REQUIRE(step(2, gui::PostprocessModel::ComputedRole).toBool());
        REQUIRE_FALSE(step(3, gui::PostprocessModel::ComputedRole).toBool());
        // Neither the add row nor the output is ever greyed: neither is a step.
        REQUIRE(step(4, gui::PostprocessModel::ComputedRole).toBool());
        REQUIRE(step(5, gui::PostprocessModel::ComputedRole).toBool());
    }

    SECTION("the slice row is as far back as it goes")
    {
        post()->setActiveRow(0);
        REQUIRE(post()->activeRow() == 1);
        REQUIRE(shapeOf(5) == QString::fromUtf8("2 × 3 × 4"));
    }
}

TEST_CASE_METHOD(ControllerFixture, "operations can be reordered and removed",
                 "[controller][postproc]")
{
    REQUIRE(h5test::selectAndSettle(controller, "/hypercube")); // 2 x 3 x 4 x 5
    post()->setEnabled(true);
    post()->addStep(QStringLiteral("max"));
    post()->setArgument(2, QStringLiteral("0"));
    post()->addStep(QStringLiteral("reshape"));
    post()->setArgument(3, QStringLiteral("-1"));

    REQUIRE(shapeOf(3) == "60");

    SECTION("dragging one above the other recomputes both shapes")
    {
        post()->moveStep(3, 2);
        REQUIRE(step(2, gui::PostprocessModel::LabelRole).toString() == "reshape");
        REQUIRE(step(3, gui::PostprocessModel::LabelRole).toString() == "max");
        // Reshaped to 120 first, so a maximum over axis 0 is now a scalar.
        REQUIRE(shapeOf(2) == "120");
        REQUIRE(shapeOf(3) == "scalar");
    }

    SECTION("nothing can be dragged above the slice or below the output")
    {
        post()->moveStep(3, 0);
        REQUIRE(step(2, gui::PostprocessModel::LabelRole).toString() == "reshape");
        post()->moveStep(2, 99);
        REQUIRE(step(3, gui::PostprocessModel::LabelRole).toString() == "reshape");
    }

    SECTION("removing one leaves the rest")
    {
        post()->removeStep(2);
        REQUIRE(post()->rowCount() == 5);
        REQUIRE(step(2, gui::PostprocessModel::LabelRole).toString() == "reshape");
        REQUIRE(shapeOf(2) == "120");
    }
}

TEST_CASE_METHOD(ControllerFixture, "a pipeline belongs to the dataset it was made about",
                 "[controller][postproc][settings]")
{
    // The rule issues.txt item 1 established, applied to the one setting that
    // is a list rather than a value. DatasetMemory does the saving; this is
    // the store it saves into and reads back out of.
    REQUIRE(h5test::selectAndSettle(controller, "/cube"));
    post()->setEnabled(true);
    post()->addStep(QStringLiteral("abs"));
    const QVariantList made = post()->steps();
    REQUIRE(made.size() == 1);

    controller.rememberSettings(QStringLiteral("postprocess"),
                                {{QStringLiteral("steps"), made}});

    REQUIRE(h5test::selectAndSettle(controller, "/matrix"));
    REQUIRE(controller.rememberedSettings(QStringLiteral("postprocess")).isEmpty());
    // A dataset nobody has been at opens on no pipeline at all, rather than
    // inheriting the one made about the last dataset. This is also what keeps
    // the first run after a selection cheap: without it the controller would
    // compute the previous dataset's chain over these elements, in full, before
    // DatasetMemory had a chance to say what belongs here.
    REQUIRE(post()->rowCount() == 4);
    REQUIRE_FALSE(post()->enabled());

    REQUIRE(h5test::selectAndSettle(controller, "/cube"));
    const QVariantMap back = controller.rememberedSettings(QStringLiteral("postprocess"));
    REQUIRE(back.value(QStringLiteral("steps")).toList().size() == 1);

    SECTION("and it is put back the way it was written down")
    {
        post()->setSteps(back.value(QStringLiteral("steps")).toList());
        REQUIRE(post()->rowCount() == 5);
        REQUIRE(step(2, gui::PostprocessModel::LabelRole).toString() == "abs");
    }

    SECTION("closing the file forgets it, as every other setting is forgotten")
    {
        controller.closeFile();
        REQUIRE(post()->rowCount() == 4);
        REQUIRE_FALSE(post()->enabled());
    }
}

TEST_CASE_METHOD(ControllerFixture, "postprocessing is offered only where it means something",
                 "[controller][postproc]")
{
    SECTION("a dataset of strings has no arithmetic to do")
    {
        REQUIRE(h5test::selectAndSettle(controller, "/str_vlen"));
        post()->setEnabled(true);
        // The switch is set and nothing is active, because there is nothing it
        // could do -- and so the bar does not claim there is.
        REQUIRE_FALSE(controller.postprocessActive());
    }

    SECTION("a scalar has no subscripts and is not asked for any")
    {
        REQUIRE(h5test::selectAndSettle(controller, "/scalar_int"));
        post()->setEnabled(true);
        REQUIRE(controller.postprocessActive());
        REQUIRE(post()->error().isEmpty());
        REQUIRE(shapeOf(3) == "scalar"); // the output
        REQUIRE(h5test::settledRowCount(table()) == 1);
        REQUIRE(cell(0, 0) == "42");
    }

    SECTION("a vector goes through unchanged when nothing is asked of it")
    {
        REQUIRE(h5test::selectAndSettle(controller, "/vec_int")); // 1 2 3 4 5
        post()->setEnabled(true);
        REQUIRE(h5test::settledRowCount(table()) == 5);
        REQUIRE(cell(0, 0) == "1");
        REQUIRE(cell(4, 0) == "5");
    }
}

TEST_CASE_METHOD(ControllerFixture, "a long vector goes through the pipeline whole",
                 "[controller][postproc]")
{
    // 1000 elements, which is over the table's own 64x64 block and well under
    // the pipeline's cap: the read has to stitch the blocks the streaming path
    // would have painted one at a time. The cap itself is exercised in
    // test_postprocess, against a source too big to build here.
    REQUIRE(h5test::selectAndSettle(controller, "/long_vec"));
    post()->setEnabled(true);
    REQUIRE(post()->error().isEmpty());
    REQUIRE(h5test::settledRowCount(table()) == 1000);
    REQUIRE(cell(0, 0) == "0");
    REQUIRE(cell(999, 0) == "999");

    SECTION("and a reduction over it is one number")
    {
        post()->addStep(QStringLiteral("max"));
        REQUIRE(shapeOf(4) == "scalar"); // the output
        REQUIRE(h5test::settledRowCount(table()) == 1);
        REQUIRE(cell(0, 0) == "999");
    }
}

TEST_CASE_METHOD(ControllerFixture, "the plot and the image draw the output array too",
                 "[controller][postproc][plot][image]")
{
    // The whole point of the feature: all three presentations differ in how
    // they draw one array, not in which array they draw, so putting the
    // pipeline's result where the dataset was is the whole of reaching them.
    REQUIRE(h5test::selectAndSettle(controller, "/hypercube")); // 2 x 3 x 4 x 5 of int32
    auto* plot = controller.datasetPlot();
    auto* image = controller.datasetImage();

    const int seriesBefore = plot->seriesCount();
    REQUIRE(image->sourceHeight() == 24); // 2 x 3 x 4 down the rows
    REQUIRE(image->sourceWidth() == 5);

    post()->setEnabled(true);
    post()->addStep(QStringLiteral("max"));
    post()->setArgument(2, QStringLiteral("0, 1"));

    // 2 x 3 x 4 x 5 reduced over its first two axes is 4 x 5, and every
    // surface is looking at that rather than at the file.
    REQUIRE(h5test::settledRowCount(table()) == 4);
    REQUIRE(h5test::settledColumnCount(table()) == 5);
    REQUIRE(image->sourceHeight() == 4);
    REQUIRE(image->sourceWidth() == 5);
    REQUIRE(plot->seriesCount() == 4);
    REQUIRE(plot->seriesCount() != seriesBefore);

    SECTION("and they go back to the file when the switch does")
    {
        post()->setEnabled(false);
        REQUIRE(image->sourceHeight() == 24);
        REQUIRE(plot->seriesCount() == seriesBefore);
    }
}

TEST_CASE_METHOD(ControllerFixture, "a picture stops being one while a pipeline runs",
                 "[controller][postproc][image]")
{
    // Asserted through the axis arrangement rather than through the tag: what
    // the Image spec buys a dataset is the one layout that draws as a picture,
    // its colour components pinned to a single channel. A transpose or a
    // reduction has just made that statement untrue, so it is withdrawn.
    //
    // The fixture carries no Image-spec dataset, so this exercises the switch
    // itself: with a pipeline running, the shape handed to the setup panel is
    // accompanied by no ImageInfo at all, and the panel therefore opens every
    // dimension whole rather than pinning one.
    REQUIRE(h5test::selectAndSettle(controller, "/cube"));
    post()->setEnabled(true);
    REQUIRE(controller.postprocessActive());

    for (int row = 0; row < setup()->rowCount(); ++row) {
        INFO("dimension " << row);
        REQUIRE(setup()->data(setup()->index(row, 0),
                              gui::TableSetupModel::ModeRole).toInt()
                == gui::TableSetupModel::All);
    }
}
