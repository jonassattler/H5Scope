// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

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
/// and not the panel of controls beside it.
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
    Q_INVOKABLE bool copyItem(QQuickItem* item);

    /// The size of the image on the clipboard now, or an empty size when there
    /// is none.
    ///
    /// Nothing in the application reads this. It is here so that the QML suite
    /// can assert a copy actually landed: what copyItem() does is invisible by
    /// construction -- it succeeds by putting something in another process's
    /// reach -- and a feature whose only witness is a paste into some other
    /// program is a feature no test can hold.
    [[nodiscard]] Q_INVOKABLE QSize imageOnClipboard() const;

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
