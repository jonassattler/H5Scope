// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// The main() the three spikes share.
//
// Same argument as SpikeRunner: what is being compared is three renderers, and
// three copies of the startup would give three chances for a difference to
// creep in somewhere that is not the renderer. The window is the same size in
// all three, the flags mean the same thing, and the refusal to benchmark
// through the software rasteriser is made once.

#include "common/SpikeRunner.hpp"

#include <QtCore/QObject>
#include <QtCore/QUrl>

#include <functional>
#include <memory>

QT_BEGIN_NAMESPACE
class QQmlApplicationEngine;
class QQuickWindow;
QT_END_NAMESPACE

namespace spike {

/// The plot window every spike opens. Fixed, because a benchmark whose plot is
/// as wide as whatever window manager happened to be running is not comparable
/// with the run before it -- and because decimation to the pixel width is one
/// of the things being measured, so the pixel width has to be a constant.
inline constexpr int kWindowWidth = 1400;
inline constexpr int kWindowHeight = 900;

/// A Surface that lives in a QML scene, which all three of these do.
class QmlSurface : public QObject, public Surface
{
    Q_OBJECT

public:
    using QObject::QObject;

    [[nodiscard]] QQuickWindow* window() const override { return window_; }

    /// Called once, after the scene has loaded and its window exists.
    void attach(QQuickWindow* window) { window_ = window; }

private:
    QQuickWindow* window_ = nullptr;
};

/// Parse the flags, open the scene, and either run the grid or hand the window
/// to the reader. `create` is called before the QML is loaded so the surface
/// can put itself in the root context under the name the QML expects.
///
/// `needsWidgets` constructs a QApplication rather than a QGuiApplication. Only
/// the QCustomPlot spike asks for it, and only because QCustomPlot is a
/// QWidget and constructing one without a QApplication aborts outright. It is
/// recorded here rather than hidden in that spike because it is a finding: a
/// Qt Quick application that adopts a QPainter-era plotting library adopts the
/// whole Qt Widgets stack with it.
int runSpike(int argc, char** argv, const QUrl& qmlEntry,
             const std::function<QmlSurface*(QQmlApplicationEngine&)>& create,
             bool needsWidgets = false);

} // namespace spike
