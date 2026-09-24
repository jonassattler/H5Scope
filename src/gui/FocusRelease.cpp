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

bool FocusRelease::keepsTextFocus(const QQuickItem* item, const QPointF& scene)
{
    // Only down through what is under the pointer, so this costs the depth of
    // the scene at that point and not the size of it. That is enough for what
    // it is looking for: a completion list is a popup, popups live in the
    // overlay, and the overlay covers the whole window.
    for (const QQuickItem* child : item->childItems()) {
        if (!child->isVisible() || !child->contains(child->mapFromScene(scene))) {
            continue;
        }
        if (child->property("keepsTextFocus").toBool() || keepsTextFocus(child, scene)) {
            return true;
        }
    }
    return false;
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

    // Nor is the list of what could be written in it. It hangs under the box
    // and is drawn outside it, and it is only up while the box has the
    // keyboard -- so letting go here closed it on the press, and the row the
    // reader was clicking had gone before the release could reach it. That
    // was the whole of why a click in a completion list did nothing.
    if (QQuickItem* content = window_->contentItem();
        content != nullptr && keepsTextFocus(content, press->scenePosition())) {
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
