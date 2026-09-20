// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "ImageClipboard.hpp"

#include <QtGui/QClipboard>
#include <QtGui/QColor>
#include <QtGui/QGuiApplication>
#include <QtGui/QImage>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickItemGrabResult>
#include <QtQuick/QQuickWindow>

#include <algorithm>
#include <cmath>

namespace gui {

ImageClipboard::ImageClipboard(QObject* parent) : QObject(parent) {}

ImageClipboard::~ImageClipboard() = default;

namespace {

/// One picture out of two grabs of it, on white and on black.
///
/// `stacked` is the pair, the white half above the black one. See the header:
/// the difference between the two is the coverage, and the black half is
/// already the premultiplied colour, so this is two subtractions a pixel and
/// no guesswork about what was ground and what was ink.
[[nodiscard]] QImage composeOverNothing(const QImage& stacked)
{
    const int height = stacked.height() / 2;
    if (height <= 0 || stacked.width() <= 0) {
        return {};
    }
    const QImage onWhite =
        stacked.copy(0, 0, stacked.width(), height).convertToFormat(QImage::Format_ARGB32);
    const QImage onBlack =
        stacked.copy(0, height, stacked.width(), height).convertToFormat(QImage::Format_ARGB32);

    QImage out(stacked.width(), height, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < height; ++y) {
        const auto* white = reinterpret_cast<const QRgb*>(onWhite.constScanLine(y));
        const auto* black = reinterpret_cast<const QRgb*>(onBlack.constScanLine(y));
        auto* row = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < stacked.width(); ++x) {
            // The three channels answer the same question and should agree;
            // the largest of them is taken, because a channel the ink happens
            // to leave untouched says "transparent" about a pixel that is not.
            const int clear =
                std::max({qRed(white[x]) - qRed(black[x]), qGreen(white[x]) - qGreen(black[x]),
                          qBlue(white[x]) - qBlue(black[x])});
            const int alpha = std::clamp(255 - clear, 0, 255);
            // Already premultiplied: the black pass is C*a by construction.
            // Clamped to alpha all the same, because rounding in two renders
            // can leave a channel a step above it, and a premultiplied pixel
            // whose colour exceeds its alpha is an invalid one.
            row[x] = qRgba(std::min(qRed(black[x]), alpha), std::min(qGreen(black[x]), alpha),
                           std::min(qBlue(black[x]), alpha), alpha);
        }
    }
    return out;
}

} // namespace

bool ImageClipboard::copyItem(QQuickItem* item, const QSize& target, bool composited)
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
    //
    // A reader who asked for a size is answered with that size and not with
    // this one; see the header for why that is not a scaling.
    const double ratio = std::max(1.0, item->window()->devicePixelRatio());
    const QSize wanted = target.isEmpty()
                             ? QSize(static_cast<int>(std::lround(item->width() * ratio)),
                                     static_cast<int>(std::lround(item->height() * ratio)))
                             : target;
    if (wanted.isEmpty()) {
        emit failed(tr("The plot has no size yet."));
        return false;
    }
    // Twice as tall when the two halves are being grabbed together, because
    // `wanted` is the size of the picture and not of what is grabbed.
    const QSize size = composited ? QSize(wanted.width(), wanted.height() * 2) : wanted;

    // Assigning over a grab still in flight cancels it: the previous result
    // loses its last reference and is destroyed, which severs the connection
    // below with it. That is what a reader pressing the button twice means.
    pending_ = item->grabToImage(size);
    if (pending_.isNull()) {
        emit failed(tr("This display cannot take a picture of the plot."));
        return false;
    }

    connect(pending_.data(), &QQuickItemGrabResult::ready, this, [this, composited]() {
        // Taken out of the member first, so that whatever happens below --
        // including a failure -- leaves nothing in flight behind it.
        const QSharedPointer<QQuickItemGrabResult> result = pending_;
        pending_.clear();
        if (result.isNull()) {
            return;
        }

        const QImage image = composited ? composeOverNothing(result->image()) : result->image();
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

QColor ImageClipboard::pixelOnClipboard(int x, int y) const
{
    const QClipboard* board = QGuiApplication::clipboard();
    if (board == nullptr) {
        return {};
    }
    const QImage image = board->image();
    if (!image.valid(x, y)) {
        return {};
    }
    // pixelColor() un-premultiplies for us, which matters here: a grab comes
    // back as ARGB32_Premultiplied, and the one thing this is asked about is a
    // ground whose alpha is zero. Read raw, every colour under a zero alpha is
    // black and the question cannot be told from its answer.
    return image.pixelColor(x, y);
}

} // namespace gui
