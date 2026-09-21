// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QColor>
#include <QObject>
#include <QSharedPointer>
#include <QSize>
#include <QString>
#include <QtQml/qqmlregistration.h>

QT_BEGIN_NAMESPACE
class QQuickItem;
class QQuickItemGrabResult;
QT_END_NAMESPACE

namespace gui {

/// Puts a picture of an item on the system clipboard.
///
/// It exists because QML cannot. `QQuickItem::grabToImage()` is reachable from
/// QML and hands back a result whose `image` is a *URL* -- something to point
/// an Image at -- and whose only other offer is `saveToFile`. The QImage itself
/// is C++-side, and QClipboard is not a QML type at all, so the twenty lines
/// below are the whole of what stands between "the plot is drawn" and "the plot
/// can be pasted into a document", which is the form most readers actually want
/// a plot in.
///
/// Deliberately about an *item* rather than about a plot. Nothing here knows
/// what it is copying; PlotSurface decides that the thing worth copying is the
/// frame -- the ground, the rules, the ticks, their labels and the strokes --
/// and not the panel of controls beside it. Since 0.6.3 the item it hands over
/// is usually not the one on screen at all but a twin of it drawn for the
/// picture; that decision is PlotSurface's too, and nothing here changes for
/// it beyond the size argument below.
class ImageClipboard : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit ImageClipboard(QObject* parent = nullptr);
    ~ImageClipboard() override;

    /// Copy `item` as it is drawn.
    ///
    /// Returns whether the grab could be *started*: false is the cases this
    /// can answer at once -- no item, no window, nothing to draw -- and
    /// everything after that is asynchronous, because a grab is one more frame
    /// rendered and there is no frame to render from inside a QML call. So the
    /// answer proper comes back through copied() or failed().
    ///
    /// Grabbed at the window's device pixel ratio rather than at the item's
    /// logical size: what a reader pastes should be the resolution they are
    /// looking at, and on a HiDPI display those differ by a factor of two or
    /// three in each direction.
    ///
    /// `target` overrides that, in pixels, for a reader who asked for a size.
    /// It does **not** scale the on-screen plot to fit: the caller is expected
    /// to have laid an item out at that size already, and this is only how the
    /// grab is told to come back with exactly the pixels that were asked for
    /// rather than with the item's logical size times whatever ratio the
    /// window happens to have. Handing a mismatched size to an item laid out
    /// for another is a stretched picture, which is why nothing in this
    /// application does it.
    ///
    /// `composited` is how a picture with no ground at all is taken, and it is
    /// here rather than in the caller because it is arithmetic over pixels.
    /// **A grab has no alpha channel to rely on**: under Qt Quick's software
    /// renderer -- which is what runs wherever there is no graphics API, and
    /// what the whole test suite runs under -- an item grab comes back as
    /// Format_RGB32 with the untouched pixels at opaque black, so a frame that
    /// draws no ground grabs as a black slab rather than as a picture on
    /// nothing.
    ///
    /// What is passed instead is an item holding the *same picture twice*, the
    /// upper half drawn on white and the lower half on black, and the alpha is
    /// recovered from the pair. That is exact rather than approximate, which a
    /// colour key over a single grab would not be: source-over compositing
    /// says a pixel of colour C at coverage a lands at `C*a + (1-a)` on white
    /// and at `C*a` on black, so their difference **is** `1-a` and what is
    /// left is already the premultiplied colour. Antialiased type and the
    /// feathered edge of a stroke come out with the coverage they were drawn
    /// with, and it answers the same way on both renderers.
    ///
    /// `target` is the size of the *result*, so the item handed over is twice
    /// that tall.
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
    Q_INVOKABLE bool copyItem(QQuickItem* item, const QSize& target = {}, bool composited = false,
                              double dpi = 0.0);

    /// The size of the image on the clipboard now, or an empty size when there
    /// is none.
    ///
    /// Nothing in the application reads this. It is here so that the QML suite
    /// can assert a copy actually landed: what copyItem() does is invisible by
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

Q_SIGNALS:
    /// The picture is on the clipboard.
    void copied();
    /// It is not, and why. A sentence for a reader, not a code.
    void failed(const QString& reason);

private:
    /// The grab in flight.
    ///
    /// Held as a member rather than captured in the lambda that waits for it,
    /// and that is not a style choice: the result is reference-counted and the
    /// connection would be owned by the object being counted, so a lambda
    /// holding the shared pointer would keep alive the very thing whose death
    /// releases it. A member has no such cycle -- and replacing it cancels the
    /// previous grab, which is the right answer to a reader pressing the
    /// button twice.
    QSharedPointer<QQuickItemGrabResult> pending_;
};

} // namespace gui
