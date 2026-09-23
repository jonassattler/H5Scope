// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "ImageClipboard.hpp"

#include <QtCore/QBuffer>
#include <QtCore/QMimeData>
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

QImage composeOverNothing(const QImage& stacked)
{
    const int height = stacked.height() / 2;
    if (height <= 0 || stacked.width() <= 0) {
        return {};
    }
    const QImage onWhite =
        stacked.copy(0, 0, stacked.width(), height).convertToFormat(QImage::Format_ARGB32);
    const QImage onBlack =
        stacked.copy(0, height, stacked.width(), height).convertToFormat(QImage::Format_ARGB32);

    // Straight alpha and not premultiplied, and the empty ground white rather
    // than black -- see the header, which is about a paste into Word on
    // Windows that came back as a black slab with the numbers gone.
    QImage out(stacked.width(), height, QImage::Format_ARGB32);
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
            if (alpha == 0) {
                row[x] = qRgba(255, 255, 255, 0);
                continue;
            }
            // The black pass is C*a by construction, so the colour is that
            // over a -- clamped first, because rounding in two renders can
            // leave a channel a step above its alpha, and C*a > a is no colour.
            const auto straight = [alpha](int premultiplied) {
                return (std::min(premultiplied, alpha) * 255 + alpha / 2) / alpha;
            };
            row[x] = qRgba(straight(qRed(black[x])), straight(qGreen(black[x])),
                           straight(qBlue(black[x])), alpha);
        }
    }
    return out;
}

QMimeData* pictureMimeData(const QImage& image, const QString& pngFormat)
{
    auto* data = new QMimeData;
    data->setImageData(image);
    if (!pngFormat.isEmpty()) {
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        if (image.save(&buffer, "PNG")) {
            data->setData(pngFormat, png);
        }
    }
    return data;
}

QString nativePngFormat()
{
#if defined(Q_OS_WIN)
    // The clipboard format registered under the name "PNG", which is the one
    // Office and every browser read. Qt's own name for "a native format
    // called this", since image/png would be registered as a format called
    // image/png, which nothing on Windows looks for.
    return QStringLiteral("application/x-qt-windows-mime;value=\"PNG\"");
#else
    // Offered already: the X11 and Wayland clipboards serve image/png out of
    // the image itself, which is why a copy made on Linux pasted correctly
    // into Word through a remote desktop all along.
    return {};
#endif
}

bool ImageClipboard::copyItem(QQuickItem* item, const QSize& target, bool composited, double dpi)
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

    connect(pending_.data(), &QQuickItemGrabResult::ready, this, [this, composited, dpi]() {
        // Taken out of the member first, so that whatever happens below --
        // including a failure -- leaves nothing in flight behind it.
        const QSharedPointer<QQuickItemGrabResult> result = pending_;
        pending_.clear();
        if (result.isNull()) {
            return;
        }

        QImage image = composited ? composeOverNothing(result->image()) : result->image();
        if (image.isNull()) {
            emit failed(tr("The plot could not be drawn into a picture."));
            return;
        }
        // What the pixels are worth in inches, where the caller said. QImage
        // holds it per metre and every format that carries it at all -- PNG's
        // pHYs among them -- is written out of these two, so this is what
        // survives the trip through the clipboard into somebody's document.
        //
        // Set after the composition rather than before: composeOverNothing
        // builds a new image out of the pair, and a new QImage carries its own
        // defaults, so tagging the grab would tag the half that was discarded.
        if (dpi > 0.0) {
            constexpr double metresPerInch = 0.0254;
            const int perMetre = static_cast<int>(std::lround(dpi / metresPerInch));
            image.setDotsPerMeterX(perMetre);
            image.setDotsPerMeterY(perMetre);
        }

        QClipboard* board = QGuiApplication::clipboard();
        if (board == nullptr) {
            emit failed(tr("This system has no clipboard."));
            return;
        }
        board->setMimeData(pictureMimeData(image, nativePngFormat()));
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

QColor ImageClipboard::opaquePixelOnClipboard(int x, int y) const
{
    const QClipboard* board = QGuiApplication::clipboard();
    if (board == nullptr) {
        return {};
    }
    QImage image = board->image();
    if (!image.valid(x, y)) {
        return {};
    }
    // What Qt's Windows clipboard does to write CF_DIB, step for step
    // (QWindowsMimeImage::convertFromMime): any format past straight ARGB32
    // is converted to RGB32, and anything else is written as its bytes stand.
    // Either way the reader keeps the three channels and drops the fourth.
    if (image.format() > QImage::Format_ARGB32) {
        image = image.convertToFormat(QImage::Format_RGB32);
    }
    const QRgb raw = image.pixel(x, y);
    return QColor(qRed(raw), qGreen(raw), qBlue(raw));
}

} // namespace gui
