// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "QtGraphsSurface.hpp"

#include "common/SpikeMain.hpp"

#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlContext>

int main(int argc, char** argv)
{
    return spike::runSpike(
        argc, argv, QUrl(QStringLiteral("qrc:/qt/qml/SpikeQtGraphs/Main.qml")),
        [](QQmlApplicationEngine& engine) -> spike::QmlSurface* {
            auto* surface = new spike::QtGraphsSurface;
            // Under the name the QML expects, and before the scene loads, so
            // no binding in it ever evaluates against an undefined `plot`.
            engine.rootContext()->setContextProperty(QStringLiteral("plot"), surface);
            return surface;
        });
}
