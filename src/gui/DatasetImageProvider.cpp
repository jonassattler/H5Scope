// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "DatasetImageProvider.hpp"

#include "AppController.hpp"
#include "DatasetImage.hpp"

#include <QImage>
#include <QQmlEngine>

namespace gui {

DatasetImageProvider::DatasetImageProvider(DatasetImage* source)
    : QQuickImageProvider(QQuickImageProvider::Image), source_(source)
{
}

QImage DatasetImageProvider::requestImage(const QString& id, QSize* size,
                                          const QSize& requestedSize)
{
    Q_UNUSED(id)
    Q_UNUSED(requestedSize)

    QImage image = source_.isNull() ? QImage{} : source_->render();
    if (size != nullptr) {
        *size = image.size();
    }
    return image;
}

void installImageProvider(QQmlEngine& engine)
{
    // The controller is a QML singleton, so the engine owns the instance;
    // reach it rather than constructing a second one, exactly as main.cpp
    // does for the command-line file argument.
    auto* controller = engine.singletonInstance<AppController*>(
        QStringLiteral("H5Scope.Backend"), QStringLiteral("AppController"));
    if (controller == nullptr) {
        return;
    }
    // The engine takes ownership of the provider.
    engine.addImageProvider(QString::fromUtf8(DatasetImageProvider::kProviderId),
                            new DatasetImageProvider(controller->datasetImage()));
}

} // namespace gui
