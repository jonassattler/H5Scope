// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

// Prints, for every object in an HDF5 file, what H5Scope would put on
// screen for it: the Information panels, the attribute table, the slice the
// Data Viewer would show and its first cells, and what the plot and image
// presentations make of it.
//
// It drives the application's own models rather than the HDF5 API, so its
// output is the application's answer, and diffing it against h5ls or h5dump is
// what turns "the file has this in it" into "the viewer says so".
//
// Usage: inspect-file <file.h5> [path-prefix]

#include "gui/AppController.hpp"
#include "gui/DatasetImage.hpp"
#include "gui/DatasetPlot.hpp"
#include "gui/H5Thread.hpp"
#include "gui/H5TreeModel.hpp"

#include <QAbstractItemModel>
#include <QGuiApplication>
#include <QStringList>
#include <QTextStream>
#include <QVariantMap>

#include <cstdio>

namespace {

/// Wait until the HDF5 thread has answered everything asked of it.
///
/// This program prints what the window would show, and the window is filled in
/// from another thread a moment after it asks. A dump that did not wait would
/// be a dump of the instant before the file answered, which is to say of
/// nothing at all.
void settle()
{
    gui::H5Thread::instance().drain();
}

/// A row count, waited for: asking is what starts the read.
int settledRowCount(QAbstractItemModel* model, const QModelIndex& parent = {})
{
    int previous = -1;
    for (int attempt = 0; attempt < 8; ++attempt) {
        const int count = model->rowCount(parent);
        settle();
        if (count == previous) {
            break;
        }
        previous = count;
    }
    return model->rowCount(parent);
}

/// A role, waited for.
QVariant settledData(const QAbstractItemModel* model, const QModelIndex& index, int role)
{
    (void)model->data(index, role);
    settle();
    return model->data(index, role);
}


QTextStream& out()
{
    static QTextStream stream(stdout);
    return stream;
}

QString roleString(const QAbstractItemModel* model, const QModelIndex& index,
                   const char* role)
{
    const auto roles = model->roleNames();
    for (auto it = roles.constBegin(); it != roles.constEnd(); ++it) {
        if (it.value() == role) {
            return settledData(model, index, it.key()).toString();
        }
    }
    return {};
}

/// Every path in the file, in the order the tree would show them.
void collectPaths(QAbstractItemModel* tree, const QModelIndex& parent, QStringList& paths)
{
    const int rows = settledRowCount(tree, parent);
    for (int row = 0; row < rows; ++row) {
        const QModelIndex index = tree->index(row, 0, parent);
        paths << roleString(tree, index, "path");
        // A cyclic node is shown but never descended into; asking the model
        // for its children is what the view does, so ask it here too.
        collectPaths(tree, index, paths);
    }
}

void printTree(QAbstractItemModel* tree, const QModelIndex& parent, int depth)
{
    const int rows = settledRowCount(tree, parent);
    for (int row = 0; row < rows; ++row) {
        const QModelIndex index = tree->index(row, 0, parent);
        out() << QString(depth * 2, u' ') << roleString(tree, index, "name") << "  ["
              << roleString(tree, index, "kindText") << "]";
        const QString meta = roleString(tree, index, "meta");
        if (!meta.isEmpty()) {
            out() << "  " << meta;
        }
        if (settledData(tree, index, gui::H5TreeModel::IsImageRole).toBool()) {
            out() << "  [img]";
        }
        if (settledData(tree, index, gui::H5TreeModel::IsCyclicRole).toBool()) {
            out() << "  <cyclic>";
        }
        out() << "\n";
        printTree(tree, index, depth + 1);
    }
}

void printPanels(const gui::AppController& controller)
{
    for (const QVariant& panel : controller.infoPanels()) {
        const QVariantMap map = panel.toMap();
        out() << "  [" << map.value("title").toString() << "] "
              << map.value("meta").toString() << "\n";
        for (const QVariant& row : map.value("rows").toList()) {
            const QVariantMap fields = row.toMap();
            out() << "      " << fields.value("label").toString().leftJustified(16)
                  << " : " << fields.value("value").toString()
                  << (fields.value("isWarning").toBool() ? "   <warning>" : "") << "\n";
        }
    }
}

void printAttributes(QAbstractItemModel* model)
{
    const int rows = settledRowCount(model);
    if (rows == 0) {
        return;
    }
    out() << "  attributes:\n";
    for (int row = 0; row < rows; ++row) {
        const QModelIndex index = model->index(row, 0);
        out() << "      " << roleString(model, index, "name").leftJustified(20) << " "
              << roleString(model, index, "type").leftJustified(28) << " "
              << roleString(model, index, "shape").leftJustified(10) << " "
              << roleString(model, index, "value").left(160) << "\n";
    }
}

void printTable(gui::AppController& controller)
{
    QAbstractItemModel* table = controller.datasetModel();
    const int rows = settledRowCount(table);
    const int columns = table->columnCount();
    out() << "  table: " << rows << " rows x " << columns
          << " cols   slice: " << controller.sliceExpression() << "\n";

    const int shownRows = std::min(rows, 4);
    const int shownColumns = std::min(columns, 6);
    for (int row = 0; row < shownRows; ++row) {
        QStringList cells;
        for (int column = 0; column < shownColumns; ++column) {
            cells << settledData(table, table->index(row, column), Qt::DisplayRole)
                         .toString()
                         .left(40);
        }
        if (columns > shownColumns) {
            cells << QStringLiteral("...");
        }
        out() << "      "
              << table->headerData(row, Qt::Vertical).toString().leftJustified(14) << " "
              << cells.join(QStringLiteral(" | ")) << "\n";
    }
    if (rows > shownRows) {
        out() << "      ...\n";
    }
}

void printPresentations(gui::AppController& controller)
{
    const auto* plot = controller.datasetPlot();
    out() << "  plot:  numeric=" << plot->property("numeric").toBool()
          << " hasData=" << plot->property("hasData").toBool()
          << " series=" << plot->property("seriesCount").toInt() << "/"
          << plot->property("sourceSeriesCount").toInt()
          << " points=" << plot->property("pointCount").toInt()
          << " thinned=" << plot->property("thinned").toBool() << " range=["
          << plot->property("minimum").toDouble() << ", "
          << plot->property("maximum").toDouble() << "]";
    const QString plotError = plot->property("error").toString();
    if (!plotError.isEmpty()) {
        out() << " error=\"" << plotError << "\"";
    }
    out() << "\n";

    const auto* image = controller.datasetImage();
    out() << "  image: numeric=" << image->property("numeric").toBool()
          << " hasData=" << image->property("hasData").toBool()
          << " size=" << image->property("width").toInt() << "x"
          << image->property("height").toInt()
          << " source=" << image->property("sourceWidth").toInt() << "x"
          << image->property("sourceHeight").toInt()
          << " thinned=" << image->property("thinned").toBool() << " range=["
          << image->property("minimum").toDouble() << ", "
          << image->property("maximum").toDouble() << "]";
    const QString imageError = image->property("error").toString();
    if (!imageError.isEmpty()) {
        out() << " error=\"" << imageError << "\"";
    }
    out() << "\n";
}

} // namespace

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    if (argc < 2) {
        std::fprintf(stderr, "usage: inspect-file <file.h5> [path-prefix]\n");
        return 2;
    }
    const QString prefix = (argc > 2) ? QString::fromUtf8(argv[2]) : QString{};

    gui::AppController controller;
    // Opening is asked of the HDF5 thread and answered a moment later, so
    // whether it worked is `hasFile` once it has answered rather than what the
    // call returned.
    controller.openFile(QString::fromUtf8(argv[1]));
    settle();
    settle();
    if (!controller.hasFile()) {
        out() << "open failed: " << controller.errorText() << "\n";
        out().flush();
        gui::H5Thread::shutdown();
        return 1;
    }

    out() << "file:    " << controller.filePath() << "\n";
    out() << "size:    " << controller.fileSize() << "\n";
    out() << "hdf5:    " << controller.hdf5Version() << "\n\n";

    out() << "=== tree ===\n";
    printTree(controller.treeModel(), QModelIndex{}, 0);

    QStringList paths{QStringLiteral("/")};
    collectPaths(controller.treeModel(), QModelIndex{}, paths);

    out() << "\n=== objects ===\n";
    for (const QString& path : paths) {
        if (!prefix.isEmpty() && !path.startsWith(prefix)) {
            continue;
        }
        out() << "\n--- " << path << "\n";
        controller.selectPath(path);
        // Twice: describing the object is one round trip, and installing what
        // the views draw is the next.
        settle();
        settle();
        if (controller.currentPath() != path) {
            out() << "  selectPath refused this path\n";
            continue;
        }
        out() << "  flags: dataTab=" << controller.datasetTabVisible()
              << " metaTab=" << controller.metadataTabVisible()
              << " rank=" << controller.datasetRank()
              << " numeric=" << controller.datasetIsNumeric()
              << " string=" << controller.datasetIsString()
              << " elements=" << controller.datasetElementCount() << "\n";
        if (!controller.datasetMessage().isEmpty()) {
            out() << "  message: " << controller.datasetMessage() << "\n";
        }
        printPanels(controller);
        printAttributes(controller.attributeModel());
        if (controller.datasetTabVisible()) {
            printTable(controller);
            printPresentations(controller);
        }
        out() << "  status: " << controller.statusLeft().join(QStringLiteral(" | "))
              << "   ///   " << controller.statusRight().join(QStringLiteral(" | "))
              << "\n";
    }

    out().flush();
    // Before the application goes: closing the file and handing back the claim
    // are jobs, and a job needs the event loop that is about to be torn down.
    gui::H5Thread::shutdown();
    return 0;
}
