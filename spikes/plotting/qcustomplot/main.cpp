// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "QCustomPlotSurface.hpp"

#include "common/SpikeMain.hpp"

#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlContext>

int main(int argc, char** argv)
{
    // A QApplication, not a QGuiApplication. QCustomPlot *is* a QWidget, and
    // constructing one without a QApplication aborts with
    //   QWidget: Cannot create a QWidget without QApplication
    // -- so a Qt Quick application that wants this library has the whole Qt
    // Widgets stack inside it, and the widgets event loop under its QML.
    //
    // This is not a detail: it is the exact reason CMakeLists.txt:183-187
    // gives for choosing Qt Graphs over Qt Charts in the first place.
    return spike::runSpike(
        argc, argv, QUrl(QStringLiteral("qrc:/qt/qml/SpikeQCustomPlot/Main.qml")),
        [](QQmlApplicationEngine& engine) -> spike::QmlSurface* {
            auto* surface = new spike::QCustomPlotSurface;
            engine.rootContext()->setContextProperty(QStringLiteral("plot"), surface);
            return surface;
        },
        /*needsWidgets=*/true);
}
