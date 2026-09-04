// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "FileSystem.hpp"

#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QStandardPaths>
#include <QVariantMap>

namespace gui {
namespace {

QVariantMap place(const QString& label, const QString& path)
{
    return QVariantMap{{QStringLiteral("label"), label},
                       {QStringLiteral("url"), QUrl::fromLocalFile(path)}};
}

} // namespace

FileSystem::FileSystem(QObject* parent) : QObject(parent) {}

QUrl FileSystem::home() const
{
    return QUrl::fromLocalFile(QDir::homePath());
}

QVariantList FileSystem::places() const
{
    QVariantList entries;
    entries.append(place(QStringLiteral("home"), QDir::homePath()));

    // Only offer the standard locations that actually exist. An empty rail
    // entry pointing at a directory the user does not have is worse than a
    // shorter rail.
    const auto standard = {
        std::pair{QStringLiteral("documents"), QStandardPaths::DocumentsLocation},
        std::pair{QStringLiteral("downloads"), QStandardPaths::DownloadLocation},
        std::pair{QStringLiteral("desktop"), QStandardPaths::DesktopLocation},
    };
    for (const auto& [label, location] : standard) {
        const QString path = QStandardPaths::writableLocation(location);
        if (!path.isEmpty() && path != QDir::homePath() && QFileInfo(path).isDir()) {
            entries.append(place(label, path));
        }
    }

    entries.append(place(QStringLiteral("temp"), QDir::tempPath()));
    entries.append(place(QStringLiteral("root"), QDir::rootPath()));
    return entries;
}

QUrl FileSystem::parentOf(const QUrl& folder) const
{
    QDir dir(folder.isLocalFile() ? folder.toLocalFile() : folder.toString());
    if (!dir.cdUp()) {
        return folder;
    }
    return QUrl::fromLocalFile(dir.absolutePath());
}

QUrl FileSystem::folderOf(const QString& path) const
{
    if (path.isEmpty()) {
        return home();
    }
    const QFileInfo info(path);
    return QUrl::fromLocalFile(info.isDir() ? info.absoluteFilePath()
                                            : info.absolutePath());
}

QString FileSystem::toLocalPath(const QUrl& url) const
{
    return url.isLocalFile() ? url.toLocalFile() : url.toString();
}

QUrl FileSystem::fromLocalPath(const QString& path) const
{
    return QUrl::fromLocalFile(QFileInfo(path).absoluteFilePath());
}

bool FileSystem::isFolder(const QString& path) const
{
    return QFileInfo(path).isDir();
}

bool FileSystem::exists(const QString& path) const
{
    return QFileInfo::exists(path);
}

QString FileSystem::formatSize(qint64 bytes) const
{
    if (bytes < 0) {
        return {};
    }
    return QLocale::system().formattedDataSize(bytes);
}

QString FileSystem::formatTime(const QDateTime& when) const
{
    if (!when.isValid()) {
        return {};
    }
    // Sortable and unambiguous, which is what a file list wants; the locale's
    // short format is neither.
    return when.toString(QStringLiteral("yyyy-MM-dd hh:mm"));
}

} // namespace gui
