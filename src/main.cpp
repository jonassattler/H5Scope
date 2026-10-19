// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "gui/AppController.hpp"
#include "gui/H5Thread.hpp"
#include "h5scope/Version.hpp"
#include "gui/DatasetImageProvider.hpp"
#include "gui/EmbeddedFonts.hpp"
#include "gui/EmbeddedIcon.hpp"
#include "gui/Rendering.hpp"
#include "h5core/Error.hpp"

#include <QCommandLineParser>
#include <QDebug>
#include <QFile>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QTextStream>

#include <cstring>

#if defined(Q_OS_WIN)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdio>
#endif

namespace {

#if defined(Q_OS_WIN)
/// Give the process somewhere to print, when Windows has given it nowhere.
///
/// H5Scope is built with WIN32_EXECUTABLE, which makes it a GUI-subsystem
/// executable: Windows starts it with no console and with null standard
/// handles, so every write to stdout goes nowhere at all. That is right for
/// the window -- a viewer should not open a black rectangle behind itself --
/// and wrong for the four options that are text: `--license`, `--notices`,
/// `--version` and `--help`.
///
/// It is `--license` that makes this load-bearing rather than a convenience.
/// The described way to use H5Scope is to download one executable and run it,
/// so the recipient of that executable has nowhere but the executable itself
/// to read the terms it is conveyed under; src/gui/CMakeLists.txt compiles the
/// GPL into the binary for exactly that reason. A binary that accepts
/// `--license`, exits 0 and prints nothing satisfies the letter of nothing.
///
/// Two cases, and the order matters:
///
///   Standard output already exists -- `H5Scope --license > terms.txt`, or a
///   pipe. The handle was inherited from the shell and printing already works.
///   Reopening CONOUT$ here would take the output *away* from the file the
///   user asked for and put it on the console instead, so this returns and
///   changes nothing.
///
///   Standard output is null -- run from a console with no redirection, or
///   from Explorer. AttachConsole borrows the console of whatever launched
///   this, if there is one; the CRT streams then have to be pointed at it,
///   because they were bound to nothing at startup and do not notice. Started
///   from Explorer there is no parent console, AttachConsole fails, and the
///   application goes on to open its window with stdout still going nowhere,
///   which is exactly right.
///
/// What this cannot fix: cmd.exe does not wait for a GUI-subsystem process, so
/// it prints its next prompt and the licence then arrives underneath it. The
/// text is all there and the shell is simply no longer looking. `H5Scope
/// --license | more` or a redirect avoids it, and both take the first branch
/// above.
void attachParentConsole()
{
    const HANDLE out = ::GetStdHandle(STD_OUTPUT_HANDLE);
    if (out != nullptr && out != INVALID_HANDLE_VALUE) {
        return;
    }
    if (::AttachConsole(ATTACH_PARENT_PROCESS) == 0) {
        return;
    }
    FILE* stream = nullptr;
    (void)::freopen_s(&stream, "CONOUT$", "w", stdout);
    (void)::freopen_s(&stream, "CONOUT$", "w", stderr);
}
#endif

/// Write a compiled-in text resource to stdout. False only if the resource is
/// absent, which cannot happen to a correctly assembled binary -- the licence
/// texts are linked in by src/gui/CMakeLists.txt -- so the caller treats it as
/// a failure rather than as an empty answer.
bool printResource(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    QTextStream out(stdout);
    out << QString::fromUtf8(file.readAll());
    return true;
}

/// `--license` and `--notices`, answered before a QGuiApplication exists.
///
/// Deliberately before: constructing one needs a display, and on a machine
/// without it the xcb plugin aborts before main() gets its answer back -- the
/// same trap the CI smoke test documents for `--version`. Reading the licence
/// of a GPL binary is the one thing a recipient must be able to do anywhere,
/// including over ssh on a headless box, so it cannot be made to depend on
/// there being a screen.
///
/// This costs a hand-rolled scan of argv, because QCommandLineParser::process
/// needs the application object that is precisely what is being avoided. The
/// options are still declared on the parser below, so `--help` lists them.
///
/// Returns an exit code, or -1 when neither option was given.
int printLicenseOptions(int argc, char* argv[])
{
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        // Everything after a bare `--` is a file name, not an option.
        if (arg == QLatin1String("--")) {
            break;
        }
        if (arg == QLatin1String("--license")) {
            return printResource(QStringLiteral(":/licenses/GPL-3.0.txt")) ? 0 : 1;
        }
        if (arg == QLatin1String("--notices")) {
            // The inventory, then the texts those licences require, then the
            // OFL -- which lives beside the faces it covers rather than with
            // the rest, and so is fetched from its own place.
            const bool ok =
                printResource(QStringLiteral(":/licenses/THIRD-PARTY-NOTICES.md"))
                && printResource(QStringLiteral(":/licenses/THIRD-PARTY-LICENSES.txt"))
                && printResource(QStringLiteral(":/fonts/LICENSE.txt"));
            return ok ? 0 : 1;
        }
    }
    return -1;
}

#if defined(Q_OS_LINUX)
/// Ask for the xdg-desktop-portal platform theme, which is how a native file
/// dialog reaches this binary on Linux.
///
/// A dynamic Qt finds the desktop's own dialog through a theme plugin --
/// KDE's, GTK's -- that it loads from the host. A static one carries the
/// plugins it was linked with and no others, and the portal theme is the one
/// linked here (see vcpkg.json): it hands the dialog to the desktop over D-Bus
/// and wraps whichever theme the desktop would otherwise have had for
/// everything else, so naming it changes the file dialog and nothing a reader
/// can see in this window. Whether the dialog is actually *used* is the
/// reader's choice under Settings; this only makes it possible, and has to
/// happen before QGuiApplication because that is when Qt picks its theme.
///
/// An explicit QT_QPA_PLATFORMTHEME is replaced rather than respected, and
/// that is deliberate. Whatever it names is a plugin of the desktop's own Qt
/// -- qt5ct, qt6ct, gtk3 -- which no static binary has; Qt passes over a name
/// it cannot load in silence, and the reader would get the generic theme,
/// which has no native dialog at all. What those names ask for is the
/// desktop's integration, and the portal is the form of it this binary can
/// have. `-platformtheme` on the command line still wins over the environment,
/// which is the way round a portal that misbehaves.
///
/// Only for the desktop platforms. Offscreen and minimal have no desktop to
/// ask, and the portal theme makes a D-Bus call while Qt is starting -- which
/// on a CI runner with no session bus is a warning on every `--version`.
void askForDesktopPortal(int argc, char* argv[])
{
    // The platform as Qt will settle it: the environment, then the argument,
    // the last of which wins. Unset means Qt's own default, which on Linux is
    // xcb or wayland.
    QByteArray platform = qgetenv("QT_QPA_PLATFORM");
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "-platform") == 0 || std::strcmp(argv[i], "--platform") == 0) {
            platform = argv[i + 1];
        }
    }
    if (!platform.isEmpty() && !platform.startsWith("xcb") && !platform.startsWith("wayland")) {
        return;
    }
    qputenv("QT_QPA_PLATFORMTHEME", "xdgdesktopportal");
}
#endif

} // namespace

int main(int argc, char* argv[])
{
#if defined(Q_OS_WIN)
    // First, because everything below that prints depends on it -- including
    // the licence options on the next line, which is the whole point.
    attachParentConsole();
#endif

    if (const int status = printLicenseOptions(argc, argv); status >= 0) {
        return status;
    }

#if defined(Q_OS_LINUX)
    askForDesktopPortal(argc, argv);
#endif

    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("H5Scope"));
    // Counted out of the history by CMake rather than typed here; `--version`
    // and the About dialog both read it back off the application object.
    QCoreApplication::setApplicationVersion(
        QString::fromLatin1(h5scope::kVersion));
    // The one thing this program remembers between runs is which files were
    // opened, and QSettings needs somewhere to file that. Set here rather than
    // in AppController so the tests, which construct controllers freely, write
    // nowhere at all unless a host application has named itself first.
    QCoreApplication::setOrganizationName(QStringLiteral("H5Scope"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("h5scope.local"));

    // What the window is called by the desktop it is running on.
    //
    // Wayland does not let a client hand the compositor a picture: the icon a
    // taskbar draws is the one in the .desktop file whose base name matches
    // this, so without it a Wayland session shows the generic placeholder
    // however many pixmaps the process is holding. It costs nothing where
    // there is no such file -- the name simply matches nothing -- and it is
    // the only way the installed icon and the running window are connected.
    QGuiApplication::setDesktopFileName(QStringLiteral("h5scope"));

    // ...and the picture itself, for everywhere that does take one: X11 puts
    // it in _NET_WM_ICON, and every Qt window falls back on it.
    //
    // Compiled into the binary rather than looked for beside it. A Windows
    // executable already carries this icon in its own resource and an AppImage
    // carries one in the AppDir, but an ELF has no slot for it: the described
    // way to use this program on Linux is to download one file and run it, and
    // that file had no icon at all until it was put here. See EmbeddedIcon.
    if (const QIcon icon = gui::applicationIcon(); icon.isNull()) {
        qWarning() << "H5Scope: the bundled application icon failed to load;"
                   << "the window will take whatever the desktop draws for an"
                   << "application it does not recognise.";
    } else {
        QGuiApplication::setWindowIcon(icon);
    }

    // Four samples, for the one thing in here that is not a rectangle or a
    // glyph: the plot's strokes are triangles and are rasterised by coverage,
    // so a hairline across a true-black ground is a staircase without this.
    // See gui::askForMultisampling. Before any window exists.
    gui::askForMultisampling();

    // Basic is the only style with no platform-specific behaviour: it renders
    // identically on every OS, which is what makes the Theme singleton the
    // single source of truth for the look. Set before any QML loads.
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    // The design system's typefaces are compiled into the binary; register
    // them before any QML loads, or the first frame renders in a fallback.
    // A failure here is not fatal -- the UI is still usable in a substitute
    // face -- but it is never silent, because a silent fallback is precisely
    // the system dependency this project exists to avoid.
    if (const auto fonts = gui::loadEmbeddedFonts(); !fonts.ok()) {
        qWarning() << "H5Scope: bundled fonts failed to load:"
                   << fonts.missing.join(QStringLiteral(", "))
                   << "- falling back to host fonts.";
    }

    // Stop HDF5 writing its error stack to stderr; failures surface as
    // exceptions and are reported in the UI instead.
    h5core::initErrorHandling();

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("A viewer for HDF5 files."));
    parser.addHelpOption();
    parser.addVersionOption();
    // Declared so `--help` lists them; answered by printLicenseOptions above,
    // before there is an application object to parse against.
    parser.addOption({QStringLiteral("license"),
                      QStringLiteral("Print the GNU General Public License "
                                     "version 3 and exit.")});
    parser.addOption({QStringLiteral("notices"),
                      QStringLiteral("Print the third-party notices and the "
                                     "licence texts they require, and exit.")});
    parser.addPositionalArgument(QStringLiteral("file"),
                                 QStringLiteral("HDF5 file to open."));
    parser.process(app);

    QQmlApplicationEngine engine;
    // Before the QML loads: the Data Viewer's image mode resolves its source
    // through this provider, and an engine without one renders a broken image
    // rather than saying anything useful.
    gui::installImageProvider(engine);
    engine.loadFromModule("H5Scope", "Main");
    if (engine.rootObjects().isEmpty()) {
        return 1;
    }

    const QStringList args = parser.positionalArguments();
    if (!args.isEmpty()) {
        // The controller is a QML singleton, so the engine owns the instance;
        // reach it rather than constructing a second one.
        auto* controller = engine.singletonInstance<gui::AppController*>(
            "H5Scope.Backend", "AppController");
        if (controller != nullptr) {
            controller->openFile(args.first());
        }
    }

    const int result = app.exec();

    // Before the engine and the application go. The HDF5 thread closes the file
    // and hands back its claim as jobs of its own, and a job needs the event
    // loop that is about to be torn down; leaving it to static destruction
    // would call H5Fclose from the wrong thread on the way out of a program
    // that did everything else right.
    gui::H5Thread::shutdown();
    return result;
}
