// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "gui/DatasetImageProvider.hpp"
#include "gui/EmbeddedFonts.hpp"
#include "support/TestFile.hpp"

#include <QQmlContext>
#include <QQmlEngine>
#include <QString>
#include <QtQuickTest>

#include <memory>

namespace {

/// Force the offscreen platform before QGuiApplication exists. Namespace-scope
/// initialisation runs before main(), which the QUICK_TEST_MAIN_WITH_SETUP
/// macro generates -- setting this inside the Setup object would be too late,
/// as the application is constructed first. Left overridable so the suite can
/// be run against a real display.
const bool kForceOffscreen = [] {
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    }
    return true;
}();

/// Exposed to QML as `EmbeddedFonts`, so the suite can assert that the bundled
/// typefaces really came out of the binary. Checking Qt.fontFamilies() would
/// not do: on a developer machine with IBM Plex installed system-wide that
/// passes whether or not the resource works, which is the failure this is meant
/// to catch.
class FontProbe : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QStringList families READ families CONSTANT)
    Q_PROPERTY(QStringList missing READ missing CONSTANT)

public:
    explicit FontProbe(QObject* parent = nullptr)
        : QObject(parent), result_(gui::loadEmbeddedFonts())
    {
    }

    [[nodiscard]] QStringList families() const { return result_.families; }
    [[nodiscard]] QStringList missing() const { return result_.missing; }

private:
    gui::EmbeddedFontResult result_;
};

/// Exposed to QML as `TestFixture`, so the .qml suites know which file to open.
class TestFixture : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString path READ path CONSTANT)

public:
    explicit TestFixture(QString path, QObject* parent = nullptr)
        : QObject(parent), path_(std::move(path))
    {
    }

    [[nodiscard]] QString path() const { return path_; }

private:
    QString path_;
};

} // namespace

/// Generates a fixture file that outlives the whole QML run and publishes its
/// path into the engine's root context.
class Setup : public QObject
{
    Q_OBJECT

public:
    Setup()
    {
        h5test::writeFixture(temp_.path());
        fixture_ = std::make_unique<TestFixture>(QString::fromStdString(temp_.path()));
    }

public slots:
    void qmlEngineAvailable(QQmlEngine* engine)
    {
        // Built here rather than in the constructor: QUICK_TEST_MAIN_WITH_SETUP
        // instantiates Setup before it constructs the QGuiApplication, and the
        // font database cannot be touched until that exists.
        fonts_ = std::make_unique<FontProbe>();

        // The image view resolves its source through the provider, exactly
        // as it does under main.cpp; without it the suite would exercise a
        // view that cannot load its own pixels.
        gui::installImageProvider(*engine);

        engine->rootContext()->setContextProperty(QStringLiteral("TestFixture"),
                                                  fixture_.get());
        engine->rootContext()->setContextProperty(QStringLiteral("EmbeddedFonts"),
                                                  fonts_.get());
    }

private:
    // TempFile removes the file in its destructor, so it must outlive the run.
    h5test::TempFile temp_{"qml"};
    std::unique_ptr<TestFixture> fixture_;
    std::unique_ptr<FontProbe> fonts_;
};

QUICK_TEST_MAIN_WITH_SETUP(qml, Setup)

#include "tst_qml.moc"
