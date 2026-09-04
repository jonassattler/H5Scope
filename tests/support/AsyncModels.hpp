// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "gui/AppController.hpp"
#include "gui/H5Thread.hpp"
#include "gui/H5TreeModel.hpp"

#include <QAbstractItemModel>
#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QEventLoop>
#include <QModelIndex>
#include <QObject>
#include <QString>
#include <QVariant>

namespace h5test {

/// Wait until the HDF5 thread has answered everything asked of it so far.
///
/// Every one of the helpers below is built on this, and they all exist for the
/// same reason: the models answer immediately with what they know and ask for
/// the rest, which is what keeps the window alive and what makes a test that
/// asserts on the answer have to say when it wants it. Making that explicit is
/// better than a model that quietly blocks so tests can stay short.
inline bool settle(int milliseconds = 30000)
{
    return gui::H5Thread::instance().drain(milliseconds);
}

/// Open a file and wait for it. openFile() returns whether the open was
/// *started*; this returns whether it succeeded.
inline bool openFileAndSettle(gui::AppController& controller, const QString& path)
{
    if (!controller.openFile(path)) {
        return false;
    }
    settle();
    QCoreApplication::processEvents();
    settle();
    return controller.hasFile();
}

/// Select an object and wait for everything the selection rebuilds.
inline bool selectAndSettle(gui::AppController& controller, const QString& path)
{
    if (!controller.selectPath(path)) {
        return false;
    }
    // Twice: describing the object is one round trip, and installing what the
    // views draw -- which the description decides -- is the next.
    settle();
    QCoreApplication::processEvents();
    settle();
    return true;
}

/// A role's value, waited for. Asking is what starts the read, so this asks,
/// waits, and asks again.
inline QVariant settledData(const QAbstractItemModel* model, const QModelIndex& index,
                            int role)
{
    (void)model->data(index, role);
    settle();
    return model->data(index, role);
}

/// A parent's row count, waited for. At most two round trips for a tree node:
/// one to find out it is a group, one to list it.
inline int settledRowCount(QAbstractItemModel* model, const QModelIndex& parent = {})
{
    int previous = -1;
    for (int attempt = 0; attempt < 8; ++attempt) {
        const int count = model->rowCount(parent);
        settle();
        QCoreApplication::processEvents();
        settle();
        if (count == previous) {
            break;
        }
        previous = count;
    }
    return model->rowCount(parent);
}

/// A model's column count, waited for. Nothing reads columns from the file, but
/// the layout that decides how many there are can be a round trip behind.
inline int settledColumnCount(QAbstractItemModel* model, const QModelIndex& parent = {})
{
    settle();
    QCoreApplication::processEvents();
    settle();
    return model->columnCount(parent);
}

/// Whether a node has children, waited for.
inline bool settledHasChildren(QAbstractItemModel* model, const QModelIndex& parent)
{
    (void)model->hasChildren(parent);
    settle();
    return model->hasChildren(parent);
}

/// Walk the tree down to `path`, listing whatever has to be listed, and give
/// back the index. Invalid when the path is not in the file.
inline QModelIndex reveal(gui::H5TreeModel& tree, const QString& path)
{
    QModelIndex found;
    bool answered = false;
    const QMetaObject::Connection connection = QObject::connect(
        &tree, &gui::H5TreeModel::pathRevealed,
        [&](const QModelIndex& index, const QString& which) {
            if (which == path) {
                found = index;
                answered = true;
            }
        });

    tree.revealPath(path);
    QDeadlineTimer deadline(30000);
    while (!answered && !deadline.hasExpired()) {
        QCoreApplication::processEvents(QEventLoop::WaitForMoreEvents, 10);
    }
    QObject::disconnect(connection);
    // The row it landed on has not been described yet; a caller that asks about
    // it wants the answer, and every one of them does.
    settle();
    return found;
}

} // namespace h5test
