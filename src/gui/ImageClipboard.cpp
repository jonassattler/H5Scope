// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "ImageClipboard.hpp"

#include <QtCore/QBuffer>
#include <QtCore/QMimeData>
#include <QtGui/QClipboard>
#include <QtGui/QColor>
#include <QtGui/QGuiApplication>
#include <QtGui/QImage>

#include <cmath>

namespace gui {

ImageClipboard::ImageClipboard(QObject* parent) : QObject(parent) {}

ImageClipboard::~ImageClipboard() = default;

QImage straightOnPaper(const QImage& painted)
{
    // Straight alpha and not premultiplied, and the empty ground white rather
    // than black -- see the header, which is about a paste into Word on
    // Windows that came back as a black slab with the numbers gone.
    QImage out = painted.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < out.height(); ++y) {
        auto* row = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < out.width(); ++x) {
            if (qAlpha(row[x]) == 0) {
                row[x] = qRgba(255, 255, 255, 0);
            }
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

bool ImageClipboard::copyImage(const QImage& image, double dpi)
{
    if (image.isNull()) {
        emit failed(tr("The plot could not be drawn into a picture."));
        return false;
    }
    QImage picture = straightOnPaper(image);
    // What the pixels are worth in inches, where the caller said. QImage
    // holds it per metre and every format that carries it at all -- PNG's
    // pHYs among them -- is written out of these two, so this is what
    // survives the trip through the clipboard into somebody's document.
    //
    // Set after the conversion rather than before: a converted QImage is a new
    // one, and a new QImage carries its own defaults.
    if (dpi > 0.0) {
        constexpr double metresPerInch = 0.0254;
        const int perMetre = static_cast<int>(std::lround(dpi / metresPerInch));
        picture.setDotsPerMeterX(perMetre);
        picture.setDotsPerMeterY(perMetre);
    }
    QClipboard* board = QGuiApplication::clipboard();
    if (board == nullptr) {
        emit failed(tr("This system has no clipboard."));
        return false;
    }
    board->setMimeData(pictureMimeData(picture, nativePngFormat()));
    emit copied();
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
    // pixelColor() un-premultiplies for us, which matters here: a picture can
    // come back as ARGB32_Premultiplied, and the one thing this is asked about is a
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
