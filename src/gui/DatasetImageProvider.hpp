// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QPointer>
#include <QQuickImageProvider>

QT_BEGIN_NAMESPACE
class QQmlEngine;
QT_END_NAMESPACE

namespace gui {

class DatasetImage;

/// Serves the Data Viewer's raster to QML under `image://dataset/<revision>`.
///
/// The id is the revision and nothing else: the provider renders whatever the
/// DatasetImage currently describes, and the number is there only to defeat
/// Qt's pixmap cache, which would otherwise keep serving the first raster
/// under an unchanged URL. `requestedSize` is ignored for the same reason the
/// raster is capped in the first place -- it is sampled once per revision, so
/// resizing the window scales what is already in memory instead of re-reading
/// the file.
class DatasetImageProvider : public QQuickImageProvider
{
public:
    explicit DatasetImageProvider(DatasetImage* source);

    QImage requestImage(const QString& id, QSize* size,
                        const QSize& requestedSize) override;

    /// URL scheme this provider answers to.
    static constexpr const char* kProviderId = "dataset";

private:
    QPointer<DatasetImage> source_;
};

/// Attach a provider to `engine`, resolving the DatasetImage through the
/// AppController singleton the engine owns.
///
/// Every entry point that loads this application's QML has to call this --
/// main.cpp and both test harnesses -- because an engine without the provider
/// renders the image view as a broken source rather than as an error.
void installImageProvider(QQmlEngine& engine);

} // namespace gui
