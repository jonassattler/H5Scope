// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "SceneGraphSurface.hpp"

#include "common/SpikeMain.hpp"

#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlContext>

int main(int argc, char** argv)
{
    return spike::runSpike(
        argc, argv, QUrl(QStringLiteral("qrc:/qt/qml/SpikeSceneGraph/Main.qml")),
        [](QQmlApplicationEngine& engine) -> spike::QmlSurface* {
            auto* surface = new spike::SceneGraphSurface;
            engine.rootContext()->setContextProperty(QStringLiteral("plot"), surface);
            return surface;
        });
}
