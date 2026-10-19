// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QColor>
#include <QObject>
#include <QSize>
#include <QString>
#include <QtQml/qqmlregistration.h>

QT_BEGIN_NAMESPACE
class QImage;
class QMimeData;
QT_END_NAMESPACE

namespace gui {

/// A painted picture as it goes on the clipboard.
///
/// **What comes back is straight ARGB32, and the empty ground is transparent
/// *white*.** Both are for the reader that drops the alpha, and on Windows
/// that is Word. Qt's Windows clipboard offers a picture as CF_DIBV5 and
/// CF_DIB and never as PNG, and writing CF_DIB it converts anything
/// premultiplied to RGB32 and keeps only the three channels. Premultiplied, a
/// pixel with nothing on it is (0, 0, 0, 0) -- so a publication picture
/// pasted into Word from 0.6.3 until 0.6.9 was a black slab with the lines on
/// it and the numbers, which are black, gone into it. It pasted correctly out
/// of a Linux session through a remote desktop, which offers image/png, and
/// that is what made it look like a question of the picture rather than of
/// the platform.
///
/// Straight, the colour of an empty pixel is free, so it is the page's.
/// A reader honouring the alpha sees exactly what it saw before; one dropping
/// it sees black ink on white paper, the edges of the type a little harder
/// than they were drawn. That is the whole of the cost.
[[nodiscard]] QImage straightOnPaper(const QImage& painted);

/// What copyImage() puts on the clipboard: the image, and -- under
/// `pngFormat`, when it is not empty -- the same image as PNG, which carries
/// the alpha and the dpi through any reader that takes it. The caller owns
/// the result until it hands it to QClipboard.
[[nodiscard]] QMimeData* pictureMimeData(const QImage& image, const QString& pngFormat);

/// The clipboard format a PNG must be offered under for this platform's
/// readers to find it, or empty where Qt offers one already. On Windows that
/// is the native "PNG" format Office reads before any bitmap, and which Qt
/// leaves out on purpose; see straightOnPaper for what that cost.
[[nodiscard]] QString nativePngFormat();

/// Puts a picture on the system clipboard.
///
/// It exists because QML cannot: QClipboard is not a QML type at all, so the
/// lines below are the whole of what stands between "the plot is drawn" and
/// "the plot can be pasted into a document", which is the form most readers
/// actually want a plot in.
///
/// Deliberately about a *picture* rather than about a plot. Nothing here knows
/// what it is copying; the plot paints the picture of itself that leaves
/// (FigureItem::picture, at the size and in the colours AppController and
/// PlotSurface decide), and this tags it and hands it over.
class ImageClipboard : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit ImageClipboard(QObject* parent = nullptr);
    ~ImageClipboard() override;

    /// Put `image` on the clipboard, as straightOnPaper makes it, and say so
    /// through copied() or failed().
    ///
    /// A picture painted rather than grabbed, and that is what made a picture
    /// on no ground simple. **A grab has no alpha channel to rely on**: under
    /// Qt Quick's software renderer -- which is what runs wherever there is no
    /// graphics API, and what the whole test suite runs under -- an item grab
    /// comes back as Format_RGB32 with the untouched pixels at opaque black.
    /// Until the plot was plotcore's, a publication picture was therefore drawn
    /// twice, on white and on black, and its alpha recovered from the pair.
    /// The figure paints its picture with QPainter into an image of its own,
    /// whose alpha is what was drawn, so there is one render and nothing to
    /// recover.
    ///
    /// `dpi` is what the picture is *tagged* with, and it changes not one
    /// pixel of it. A number of pixels is not a size until something says how
    /// densely they sit, so a figure asked for at 300 dpi has to carry that
    /// number or it lands on the page at one dot per point -- three times too
    /// large, in a word processor that believes it. Zero says nothing, which
    /// is what a picture taken at the display's own scale should say: "as many
    /// dots as this screen has" is not a claim about inches. See
    /// AppController::plotExportTaggedDpi, which is the one place that decides
    /// which of the two it is.
    Q_INVOKABLE bool copyImage(const QImage& image, double dpi = 0.0);

    /// The size of the image on the clipboard now, or an empty size when there
    /// is none.
    ///
    /// Nothing in the application reads this. It is here so that the QML suite
    /// can assert a copy actually landed: what copyImage() does is invisible by
    /// construction -- it succeeds by putting something in another process's
    /// reach -- and a feature whose only witness is a paste into some other
    /// program is a feature no test can hold.
    [[nodiscard]] Q_INVOKABLE QSize imageOnClipboard() const;

    /// One pixel of it, and an invalid colour where there is no such pixel.
    ///
    /// Here for the reason imageOnClipboard() is, and for a stricter question:
    /// a publication export differs from the picture on screen in its
    /// *colours* -- a transparent ground, the light scope's palette -- and a
    /// size is no witness to either. Nothing in the application reads this one
    /// either.
    [[nodiscard]] Q_INVOKABLE QColor pixelOnClipboard(int x, int y) const;

    /// The same pixel as a reader that ignores alpha gets it: Word pasting
    /// Qt's CF_DIB on Windows, reproduced here by the conversion Qt makes to
    /// write one. Opaque by construction.
    ///
    /// The witness the suite was missing. A publication picture was right in
    /// every pixel pixelOnClipboard() could see -- the ground clear, the ink
    /// black -- and black on black in the one reading most readers' word
    /// processor actually makes. See straightOnPaper.
    [[nodiscard]] Q_INVOKABLE QColor opaquePixelOnClipboard(int x, int y) const;

Q_SIGNALS:
    /// The picture is on the clipboard.
    void copied();
    /// It is not, and why. A sentence for a reader, not a code.
    void failed(const QString& reason);
};

} // namespace gui
