// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "ImageClipboard.hpp"

#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>
#include <QtGui/QImage>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickItemGrabResult>
#include <QtQuick/QQuickWindow>

#include <cmath>

namespace gui {

ImageClipboard::ImageClipboard(QObject* parent) : QObject(parent) {}

ImageClipboard::~ImageClipboard() = default;

bool ImageClipboard::copyItem(QQuickItem* item)
{
    if (item == nullptr || item->window() == nullptr) {
        emit failed(tr("There is no plot on screen to copy."));
        return false;
    }

    // Device pixels, not logical ones. An item is laid out in logical pixels
    // and drawn into a framebuffer with devicePixelRatio of them for each, so
    // grabbing at the item's own size on a HiDPI display throws away exactly
    // that factor before anything reaches the clipboard -- the same arithmetic
    // PlotSurface.pushColumns does for the same reason, and the same mistake
    // it records having made.
    const double ratio = std::max(1.0, item->window()->devicePixelRatio());
    const QSize size(static_cast<int>(std::lround(item->width() * ratio)),
                     static_cast<int>(std::lround(item->height() * ratio)));
    if (size.isEmpty()) {
        emit failed(tr("The plot has no size yet."));
        return false;
    }

    // Assigning over a grab still in flight cancels it: the previous result
    // loses its last reference and is destroyed, which severs the connection
    // below with it. That is what a reader pressing the button twice means.
    pending_ = item->grabToImage(size);
    if (pending_.isNull()) {
        emit failed(tr("This display cannot take a picture of the plot."));
        return false;
    }

    connect(pending_.data(), &QQuickItemGrabResult::ready, this, [this]() {
        // Taken out of the member first, so that whatever happens below --
        // including a failure -- leaves nothing in flight behind it.
        const QSharedPointer<QQuickItemGrabResult> result = pending_;
        pending_.clear();
        if (result.isNull()) {
            return;
        }

        const QImage image = result->image();
        if (image.isNull()) {
            emit failed(tr("The plot could not be drawn into a picture."));
            return;
        }

        QClipboard* board = QGuiApplication::clipboard();
        if (board == nullptr) {
            emit failed(tr("This system has no clipboard."));
            return;
        }
        board->setImage(image);
        emit copied();
    });

    return true;
}

QSize ImageClipboard::imageOnClipboard() const
{
    const QClipboard* board = QGuiApplication::clipboard();
    if (board == nullptr) {
        return {};
    }
    return board->image().size();
}

} // namespace gui
