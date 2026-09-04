// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QObject>
#include <QPointer>
// The full type rather than a forward declaration: `window` is a Q_PROPERTY of
// pointer type, and the meta-object system will not register a pointer to an
// incomplete class.
#include <QQuickWindow>
#include <QtQml/qqmlregistration.h>

QT_BEGIN_NAMESPACE
class QQuickItem;
QT_END_NAMESPACE

namespace gui {

/// Takes the keyboard off a text box when the pointer presses somewhere else.
///
/// Qt Quick does not do this on its own. A TextInput keeps active focus until
/// something else asks for it, and most of a window never asks: a panel, a
/// plot, the strip of chrome around them. So the slice bar stayed in edit mode
/// with an unapplied line in it while the reader was already looking somewhere
/// else, and the tree's filter kept the keyboard after they had gone back to
/// the tree.
///
/// It has to be an event filter on the window rather than a TapHandler in QML,
/// and that is the whole reason this class exists. A handler is offered a press
/// only until some item accepts it, and the items a reader presses to leave a
/// text box -- a button, a list, a Flickable -- all accept presses. A handler
/// on the window's content item is therefore never told about exactly the
/// events it would need to see. An event filter runs before delivery and sees
/// every one of them, whoever ends up handling it.
///
/// What counts as a text box is asked of the object rather than of its type: an
/// item that carries both a `selectionStart` and a `cursorPosition` is one, and
/// in Qt Quick nothing else is. That keeps this off the private headers the
/// concrete types live behind.
class FocusRelease : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    /// The window to watch. Nothing happens until one is named.
    Q_PROPERTY(QQuickWindow* window READ window WRITE setWindow NOTIFY windowChanged)

public:
    explicit FocusRelease(QObject* parent = nullptr);

    [[nodiscard]] QQuickWindow* window() const { return window_; }
    void setWindow(QQuickWindow* window);

    /// Whether `item` is something a caret can sit in.
    [[nodiscard]] static bool isTextEntry(const QQuickItem* item);

signals:
    void windowChanged();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QPointer<QQuickWindow> window_;
};

} // namespace gui
