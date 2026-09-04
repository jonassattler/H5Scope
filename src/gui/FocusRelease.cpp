// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "FocusRelease.hpp"

#include <QEvent>
#include <QMetaObject>
#include <QMouseEvent>
#include <QQuickItem>
#include <QQuickWindow>

namespace gui {

FocusRelease::FocusRelease(QObject* parent) : QObject(parent) {}

void FocusRelease::setWindow(QQuickWindow* window)
{
    if (window_ == window) {
        return;
    }
    if (window_ != nullptr) {
        window_->removeEventFilter(this);
    }
    window_ = window;
    if (window_ != nullptr) {
        window_->installEventFilter(this);
    }
    emit windowChanged();
}

bool FocusRelease::isTextEntry(const QQuickItem* item)
{
    if (item == nullptr) {
        return false;
    }
    const QMetaObject* meta = item->metaObject();
    return meta->indexOfProperty("selectionStart") >= 0
           && meta->indexOfProperty("cursorPosition") >= 0;
}

bool FocusRelease::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() != QEvent::MouseButtonPress || window_.isNull()) {
        return QObject::eventFilter(watched, event);
    }

    QQuickItem* focused = window_->activeFocusItem();
    if (!isTextEntry(focused)) {
        return QObject::eventFilter(watched, event);
    }

    // Inside the box the reader is already editing is not "somewhere else":
    // clicking into the middle of a line one is halfway through writing must
    // move the caret, not end the edit. The stepper arrows of a number box are
    // children of it and so are inside it too, which is what lets one be
    // nudged without losing the keyboard.
    const auto* press = static_cast<QMouseEvent*>(event);
    if (focused->contains(focused->mapFromScene(press->scenePosition()))) {
        return QObject::eventFilter(watched, event);
    }

    // The box's own flag, not merely the window's active focus. Every scope in
    // Qt Quick remembers which of its children last held the keyboard, so
    // forcing focus onto the content item hands it straight back down to the
    // box that had it -- which was this fix's first version, and it changed
    // nothing at all.
    focused->setFocus(false, Qt::MouseFocusReason);
    // With nothing focused the window would deliver keys nowhere, so the
    // content item takes it: that is where the keyboard sits before anything
    // has been clicked into.
    if (window_->activeFocusItem() == nullptr) {
        if (QQuickItem* content = window_->contentItem(); content != nullptr) {
            content->forceActiveFocus(Qt::MouseFocusReason);
        }
    }

    // Nothing is consumed. This is a bystander: the press goes on to whatever
    // the reader actually pressed, and all that has changed by the time it
    // arrives is that the box they left has committed and let go.
    return QObject::eventFilter(watched, event);
}

} // namespace gui
