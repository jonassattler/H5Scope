// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace gui {

/// The handful of filesystem facts the application's own file picker needs.
///
/// It lives outside AppController on purpose: AppController is the facade over
/// one open HDF5 file, and where the user's home directory is has nothing to do
/// with that. Qt.labs.folderlistmodel supplies the directory listing itself;
/// this fills the gaps QML has no answer for -- well-known locations, byte
/// counts in human units, and the URL/path conversions a picker does constantly.
class FileSystem : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// Shortcuts for the picker's left rail: [{ label, url }, ...].
    Q_PROPERTY(QVariantList places READ places CONSTANT)
    Q_PROPERTY(QUrl home READ home CONSTANT)

public:
    explicit FileSystem(QObject* parent = nullptr);

    [[nodiscard]] QVariantList places() const;
    [[nodiscard]] QUrl home() const;

    /// Enclosing directory of `folder`. Returns `folder` unchanged at the root,
    /// so an "up" button simply stops rather than needing to be disabled.
    [[nodiscard]] Q_INVOKABLE QUrl parentOf(const QUrl& folder) const;
    /// The directory holding `path`, for reopening the picker where it left off.
    [[nodiscard]] Q_INVOKABLE QUrl folderOf(const QString& path) const;

    [[nodiscard]] Q_INVOKABLE QString toLocalPath(const QUrl& url) const;
    [[nodiscard]] Q_INVOKABLE QUrl fromLocalPath(const QString& path) const;
    /// True when `path` names a directory that can be listed.
    [[nodiscard]] Q_INVOKABLE bool isFolder(const QString& path) const;
    [[nodiscard]] Q_INVOKABLE bool exists(const QString& path) const;

    /// Byte count in the locale's units, e.g. "4.1 MB". Directories have no
    /// meaningful size, so they are given none.
    [[nodiscard]] Q_INVOKABLE QString formatSize(qint64 bytes) const;
    [[nodiscard]] Q_INVOKABLE QString formatTime(const QDateTime& when) const;
};

} // namespace gui
