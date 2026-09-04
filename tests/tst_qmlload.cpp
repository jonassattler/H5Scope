// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

// Loads the application's QML root exactly the way main.cpp does.
//
// This exists because the Qt Quick Test suites instantiate the individual view
// components but never Main.qml itself, so a fault reachable only from the
// root -- a missing QML module registration, or a custom component whose name
// collides with a Qt Quick Controls type and is silently shadowed by it --
// passed every test and still broke the real binary on startup.
#include "gui/DatasetImageProvider.hpp"

#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QtTest>

namespace {

const bool kForceOffscreen = [] {
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    }
    return true;
}();

} // namespace

class TestQmlLoad : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void rootLoadsWithoutErrors();
};

void TestQmlLoad::initTestCase()
{
    // Must match main.cpp: a different style would exercise different code.
    QQuickStyle::setStyle(QStringLiteral("Basic"));
}

void TestQmlLoad::rootLoadsWithoutErrors()
{
    QQmlApplicationEngine engine;
    // Must match main.cpp here too: the Data Viewer's image mode reaches for
    // this provider as the root loads.
    gui::installImageProvider(engine);

    QList<QQmlError> warnings;
    QObject::connect(&engine, &QQmlEngine::warnings, this,
                     [&warnings](const QList<QQmlError>& list) { warnings += list; });

    engine.loadFromModule("H5Scope", "Main");

    QVERIFY2(!engine.rootObjects().isEmpty(),
             "H5Scope/Main.qml failed to load");

    // A binding error does not stop the window appearing, but it does mean the
    // UI is quietly broken, so treat it as a failure rather than noise.
    for (const QQmlError& warning : warnings) {
        qWarning("QML warning: %s", qPrintable(warning.toString()));
    }
    QCOMPARE(warnings.size(), 0);
}

QTEST_MAIN(TestQmlLoad)

#include "tst_qmlload.moc"
