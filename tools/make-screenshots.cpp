// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

// Writes the pictures README.md shows, by running the application and taking
// them off its own window.
//
// The README's screenshots used to be taken by hand, which meant they were a
// picture of whatever the window looked like on the day someone remembered to
// take one. Every change to the chrome dated them silently: nothing failed, the
// pictures simply stopped being of this program. This is the mechanism that
// makes them a build artefact instead -- `cmake --build --preset release
// --target screenshots` regenerates all of them into docs/screenshots, and
// what comes out is by construction the window this source builds.
//
// What is captured is the window's own surface, so there are no decorations in
// it: the title bar, the shadow and the desktop behind them belong to the
// machine the picture was taken on rather than to this program, and they were
// the least reproducible thing in the old screenshots.
//
// It runs headless, on the offscreen platform, so it needs no display, no
// compositor and no window manager. That platform has one thing to be told --
// which renderer to draw the scene with; see canRender() below for why and for
// what happens on a machine that cannot.
//
// Usage: make-screenshots [--out DIR] [--size WxH]

#include "ExampleFile.hpp"
#include "gui/AppController.hpp"
#include "gui/DatasetImageProvider.hpp"
#include "gui/EmbeddedFonts.hpp"
#include "gui/H5Thread.hpp"
#include "h5core/Error.hpp"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QList>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSet>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include <QVariant>

#include <cstdio>
#include <cstdlib>
#include <exception>

namespace {

QTextStream& out()
{
    static QTextStream stream(stdout);
    return stream;
}

QTextStream& err()
{
    static QTextStream stream(stderr);
    return stream;
}

// --- what to photograph ----------------------------------------------------

/// One picture: the state the window is put into before it is asked what it
/// looks like.
///
/// Everything here is what a reader would do -- open some branches of the tree,
/// click an object, choose a tab -- and it is driven through the same public
/// entry points their pointer would reach: nothing here reaches behind the
/// window to arrange a view that the application cannot actually be put into.
struct Shot
{
    /// The file this is written to, without its extension, and the name
    /// README.md links to.
    const char* name;
    /// Tree branches to open, shallowest first. The tree reads a group the
    /// first time something asks for its contents, so this is also what
    /// decides how much of the file has been read when the picture is taken.
    QStringList open;
    /// The object to select.
    const char* path;
    /// Which of the four tabs is showing: "info", "table", "plot" or "image".
    const char* tab;
};

/// The three the README shows. Adding one is an entry here and a paragraph
/// there; nothing else in this file knows how many there are.
QList<Shot> shots()
{
    return {
        // The Information tab, on an object that fills every panel it has:
        // a truecolour image has a dataspace, a datatype, chunked storage,
        // the image panel the HDF5 image specification describes, and the four
        // attributes that say so.
        {"information",
         {QStringLiteral("/images")},
         "/images/rgb_planar_3x256x256",
         "info"},
        // The Plot view, on the smallest dataset that still looks like data:
        // 48 float64 readings, which draw as one line with room for its axes.
        {"plot",
         {QStringLiteral("/committed")},
         "/committed/morning",
         "plot"},
        // The Image view, on a dataset with an alpha channel, so the
        // checkerboard behind a transparent picture is in the picture too.
        {"image",
         {QStringLiteral("/images")},
         "/images/rgba_128x128x4",
         "image"},
    };
}

// --- waiting ---------------------------------------------------------------

/// How long one turn of the loop is given. Long enough for a polish and a
/// timer, short enough that the dozen of them a picture costs are not felt.
constexpr int kSettleMilliseconds = 50;

/// Wait until the HDF5 thread has answered everything asked of it, and until
/// the window has acted on the answers.
///
/// The models answer immediately with what they know and ask another thread for
/// the rest, which is what keeps the window alive while a file is read. A
/// picture taken without waiting would be a picture of that moment: a tree with
/// nothing under it and a view still showing the last selection.
///
/// A turn of a real event loop rather than a call to processEvents(), because
/// half of what has to happen here is not a queued call at all: a view that has
/// been given rows polishes itself on a timer, and a loop that only drains the
/// queue never lets that run.
void settle()
{
    gui::H5Thread::instance().drain();
    QEventLoop loop;
    QTimer::singleShot(kSettleMilliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}

/// How many frames are drawn before one is kept. See render().
constexpr int kRenderPasses = 3;

/// Render the window and give back the last thing it drew.
///
/// Several frames rather than one, because a single pass is not always the
/// whole picture. A view that was handed rows while it was laying out lays them
/// out again on the frame after -- the tree, which is fed by another thread,
/// otherwise comes out holding a mixture of the rows it had and the rows it was
/// given -- and Qt Graphs builds its series from what the pass before it
/// delivered, so a plot's first frame after a change of selection can still be
/// the axes without the line. Each pass costs a millisecond and the whole
/// question goes away.
QImage render(QQuickWindow* window)
{
    QImage picture;
    for (int pass = 0; pass < kRenderPasses; ++pass) {
        settle();
        picture = window->grabWindow();
    }
    return picture;
}

// --- driving the window ----------------------------------------------------

/// Run a line of JavaScript in the root window's own scope.
///
/// This is how the ids in Main.qml -- `objectTree`, `dataView` -- are reached
/// from here: they are names in the root component's context and exist nowhere
/// else, so an expression evaluated against that context is the only way to
/// call the tree's own functions without giving the window a second, private
/// interface that exists solely to be photographed.
bool run(QObject* root, const QString& javascript)
{
    QQmlExpression expression(qmlContext(root), root, javascript);
    expression.evaluate();
    if (expression.hasError()) {
        err() << "evaluating " << javascript << ": "
              << expression.error().toString() << "\n";
        return false;
    }
    return true;
}

/// `paths` as a JavaScript array literal. Through QJsonDocument rather than by
/// joining quotes, because an HDF5 name may contain both.
QString jsArray(const QStringList& paths)
{
    const QJsonDocument document{QJsonArray::fromStringList(paths)};
    return QString::fromUtf8(document.toJson(QJsonDocument::Compact));
}

/// Put the window into the state `shot` describes. False, with a reason
/// printed, when it did not get there -- a picture of the wrong tab is worse
/// than no picture, because nothing about it looks wrong.
bool arrange(const Shot& shot, QObject* root, QQuickWindow* window,
             gui::AppController& controller)
{
    // The tree first, because a reader opens a branch and then clicks what is
    // in it. Closed and reopened rather than added to, so that each picture
    // shows the branches it names and no others -- what the shot before it left
    // open is not part of this one.
    if (!run(root, QStringLiteral("objectTree.collapseAll()"))) {
        return false;
    }
    settle();
    // A drawn frame between closing the tree and opening it again. Without one,
    // the rows below the branch that opens keep the names of the rows that were
    // there before it did: TreeView is told about the collapse and the expand
    // in the same turn of the event loop and only maps the second of them onto
    // what it is already showing. The same fault is reachable by hand -- type
    // in the filter box and clear it again -- and is the tree's to fix; a
    // screenshot tool cannot photograph its way around it.
    (void)render(window);

    if (!run(root, QStringLiteral("objectTree.restoreBranches(%1)")
                       .arg(jsArray(shot.open)))) {
        return false;
    }
    settle();

    const QString path = QString::fromUtf8(shot.path);
    controller.selectPath(path);
    // Twice: describing the object is one round trip, and installing what the
    // views draw -- which the description decides -- is the next.
    settle();
    settle();
    if (controller.currentPath() != path) {
        err() << shot.name << ": " << path << " is not in the example file\n";
        return false;
    }

    const QString tab = QString::fromUtf8(shot.tab);
    if (!QMetaObject::invokeMethod(root, "selectTab", Q_ARG(QVariant, tab))) {
        err() << shot.name << ": the window has no selectTab()\n";
        return false;
    }
    settle();

    // A tab the selection does not offer is refused rather than shown empty --
    // the plot and the image are for numbers -- and the refusal is silent,
    // which is exactly the failure this catches.
    const QString showing = root->property("currentTabId").toString();
    if (showing != tab) {
        err() << shot.name << ": asked for the " << tab << " tab and got "
              << showing << "; " << path << " does not offer it\n";
        return false;
    }
    return true;
}

// --- writing ---------------------------------------------------------------

/// Roughly how many colours are in `image`, sampled on a grid.
///
/// A window that never drew -- no scene graph, no platform integration, a
/// grab of a surface that was never rendered -- comes out as one flat colour
/// or as nothing at all, and a generator that writes that file has failed
/// while reporting success. Sampled rather than counted because the answer
/// only has to distinguish "a window" from "a rectangle".
int distinctColours(const QImage& image)
{
    QSet<QRgb> seen;
    for (int y = 0; y < image.height(); y += 8) {
        for (int x = 0; x < image.width(); x += 8) {
            seen.insert(image.pixel(x, y));
        }
    }
    return static_cast<int>(seen.size());
}

/// Whether two pictures are the same picture.
///
/// Not byte for byte: two runs of this program on one machine already differ,
/// by a step of one in the odd channel, because the graphics driver is free to
/// round a blend either way and does. A tolerance of two channel steps is far
/// below anything a person can see and far below any real change -- a rule that
/// moved by a pixel, a colour that was retuned, a panel that grew a row all
/// swing whole pixels between light and dark somewhere.
///
/// It is what keeps the target idempotent: regenerating the screenshots after a
/// change that did not touch the window leaves the files, and the commit, alone.
bool sameAs(const QImage& picture, const QImage& previous)
{
    if (picture.size() != previous.size() || picture.isNull() || previous.isNull()) {
        return false;
    }
    const QImage left = picture.convertToFormat(QImage::Format_ARGB32);
    const QImage right = previous.convertToFormat(QImage::Format_ARGB32);
    constexpr int tolerance = 2;
    for (int y = 0; y < left.height(); ++y) {
        const auto* leftRow = left.constScanLine(y);
        const auto* rightRow = right.constScanLine(y);
        for (qsizetype byte = 0; byte < left.bytesPerLine(); ++byte) {
            const int difference = static_cast<int>(leftRow[byte])
                                   - static_cast<int>(rightRow[byte]);
            if (std::abs(difference) > tolerance) {
                return false;
            }
        }
    }
    return true;
}

/// Write `image` to `path`, and say whether that changed anything.
///
/// The file already there is read back and compared first, so a run that
/// changes nothing leaves the files -- and `git status` -- alone. That is the
/// answer the maintainer is actually after: whether this build's window still
/// looks like the pictures the README is shipping.
bool write(const QImage& image, const QString& path, bool& changed)
{
    changed = !sameAs(image, QImage(path));
    if (!changed) {
        return true;
    }
    if (!image.save(path, "PNG")) {
        err() << path << ": the picture could not be written\n";
        return false;
    }
    return true;
}

/// Whether this machine can give Qt a graphics context to draw through.
///
/// Asked with Qt's own classes rather than assumed from the platform name: a
/// build machine may well have no GPU, no driver and no software rasteriser,
/// and a program that asked for the graphics API anyway would fail to start
/// rather than fall back. See where it is called.
bool canRender()
{
    QOpenGLContext context;
    if (!context.create()) {
        return false;
    }
    QOffscreenSurface surface;
    surface.setFormat(context.format());
    surface.create();
    return surface.isValid() && context.makeCurrent(&surface);
}

// --- the command line ------------------------------------------------------

void usage(const char* program)
{
    std::fprintf(stderr,
                 "usage: %s [--out DIR] [--size WxH]\n"
                 "\n"
                 "  --out DIR   where to write the PNGs (default: the working "
                 "directory)\n"
                 "  --size WxH  the window, in pixels (default 1280x820)\n",
                 program);
}

struct Options
{
    QString directory = QStringLiteral(".");
    /// Main.qml's own opening size, in pixels. A picture of the window as it
    /// first appears, rather than of one dragged to some size that suited the
    /// day.
    ///
    /// One pixel to a pixel, deliberately, and there is no option to ask for
    /// more. A picture at twice the density would be sharper in the README and
    /// this cannot take one: with a scale factor set, the offscreen platform
    /// renders the scene at the full density and then reads back a window-sized
    /// corner of it, so what comes out is a quarter of the window rather than
    /// all of it, whichever backend draws. See the note on the graphics API
    /// below for the other half of what this platform can and cannot do.
    QSize size{1280, 820};
    bool valid = true;
};

/// Read the command line. Hand-rolled, and read before there is an application
/// object, because the platform plugin is settled through the environment
/// before QGuiApplication is constructed -- by then it is too late to tell Qt
/// which one to use.
Options parse(int argc, char* argv[])
{
    Options options;
    for (int i = 1; i < argc; ++i) {
        const QString argument = QString::fromLocal8Bit(argv[i]);
        const bool hasValue = i + 1 < argc;
        if (argument == QLatin1String("--out") && hasValue) {
            options.directory = QString::fromLocal8Bit(argv[++i]);
        } else if (argument == QLatin1String("--size") && hasValue) {
            const QStringList parts = QString::fromLocal8Bit(argv[++i])
                                          .split(QLatin1Char('x'));
            bool wide = false;
            bool tall = false;
            if (parts.size() == 2) {
                options.size = QSize(parts.at(0).toInt(&wide),
                                     parts.at(1).toInt(&tall));
            }
            options.valid = options.valid && wide && tall
                            && !options.size.isEmpty();
        } else {
            options.valid = false;
        }
    }
    return options;
}

} // namespace

int main(int argc, char* argv[])
{
    const Options options = parse(argc, argv);
    if (!options.valid) {
        usage(argv[0]);
        return 2;
    }

    // In the environment before the application is constructed: Qt reads the
    // platform name once, on its way up, and it cannot be changed afterwards.
    // Left overridable so the same program can be pointed at a real display
    // when someone wants to watch it work.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    }

    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("H5Scope"));
    // Deliberately no organization name: that is what switches AppController's
    // QSettings on, and a screenshot of this machine's list of recently opened
    // files is a screenshot of whoever ran it. Without one nothing is read and
    // nothing is written -- see AppController::remember.

    // Draw through the graphics API rather than through the raster fallback.
    //
    // The offscreen platform declares no RhiBasedRendering capability, so Qt
    // Quick picks its software renderer for every window opened on it. That
    // renderer draws most of this application correctly and silently drops what
    // it cannot do -- which here is Qt Graphs' grid, the horizontal and vertical
    // rules the plot's readings are read against. A picture missing them is a
    // picture of a plot this program does not draw.
    //
    // "rhi" is the name for "no adaptation, use the graphics API"; asking for it
    // is what overrides the platform's answer. It needs a context to be had,
    // which is why it is asked for only when one can be, and the run says so
    // when it cannot rather than quietly producing the lesser picture.
    if (canRender()) {
        QQuickWindow::setSceneGraphBackend(QStringLiteral("rhi"));
    } else {
        // On stdout beside the pictures rather than on stderr: it is not a
        // failure, it is what this run produced.
        out() << "note: no OpenGL context on this machine, so the scene graph "
                 "falls back to\n      software and the plot's grid lines will "
                 "be missing from its picture.\n";
    }

    // The rest is what main.cpp does before its first frame, and for the same
    // reasons: a different style is a different window, and a picture in the
    // wrong typeface is a picture of a program nobody has. A failure here is
    // fatal where the application only warns -- it can go on in a substitute
    // face and this cannot, because the substitute would be what the README
    // showed.
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    if (const auto fonts = gui::loadEmbeddedFonts(); !fonts.ok()) {
        err() << "the bundled fonts did not load: "
              << fonts.missing.join(QStringLiteral(", ")) << "\n";
        return 1;
    }
    h5core::initErrorHandling();

    // The file the pictures are of, written here rather than committed: the
    // example files are generated from tools/ExampleFile.cpp by every part of
    // this project that needs them, so what the README shows is the same file
    // the test suite asserts against.
    QTemporaryDir examples;
    if (!examples.isValid()) {
        err() << "nowhere to write the example file: " << examples.errorString()
              << "\n";
        return 1;
    }
    try {
        h5example::writeExampleFiles(examples.path().toStdString());
    }
    catch (const std::exception& error) {
        err() << "the example file could not be written: " << error.what() << "\n";
        return 1;
    }

    QQmlApplicationEngine engine;
    gui::installImageProvider(engine);
    engine.loadFromModule("H5Scope", "Main");
    if (engine.rootObjects().isEmpty()) {
        err() << "H5Scope/Main.qml did not load\n";
        return 1;
    }
    QObject* root = engine.rootObjects().constFirst();
    auto* window = qobject_cast<QQuickWindow*>(root);
    auto* controller = engine.singletonInstance<gui::AppController*>(
        "H5Scope.Backend", "AppController");
    if (window == nullptr || controller == nullptr) {
        err() << "the application's root window is not what it used to be\n";
        return 1;
    }

    // Only when it has to be: a resize on the offscreen platform draws a
    // warning about size hints it cannot propagate, and the size asked for is
    // the size Main.qml opens at unless someone said otherwise.
    if (window->size() != options.size) {
        window->resize(options.size);
    }

    const QString file = QDir(examples.path()).filePath(QStringLiteral("example.h5"));
    controller->openFile(file);
    settle();
    settle();
    if (!controller->hasFile()) {
        err() << "the example file did not open: " << controller->errorText() << "\n";
        gui::H5Thread::shutdown();
        return 1;
    }
    // One frame before anything is arranged. The tree only has rows once it has
    // been rendered, and a branch cannot be opened by a view that has not yet
    // built the row it hangs from.
    (void)render(window);
    settle();

    QDir directory(options.directory);
    if (!directory.mkpath(QStringLiteral("."))) {
        err() << "cannot write to " << directory.path() << "\n";
        gui::H5Thread::shutdown();
        return 1;
    }

    int failures = 0;
    for (const Shot& shot : shots()) {
        const QString path =
            directory.filePath(QLatin1String(shot.name) + QLatin1String(".png"));
        if (!arrange(shot, root, window, *controller)) {
            ++failures;
            continue;
        }

        const QImage picture = render(window);
        // A window that came out at another size was resized by something else
        // -- a window manager, when this is pointed at a real display, will
        // shrink a window that does not fit the screen -- and the picture is
        // then of a layout nobody asked for.
        if (picture.size() != options.size) {
            err() << shot.name << ": the window came out at "
                  << picture.width() << "x" << picture.height() << " rather than "
                  << options.size.width() << "x" << options.size.height() << "\n";
            ++failures;
            continue;
        }
        // Sixteen is far below anything this window can draw -- its chrome
        // alone spends more -- and far above a rectangle of one colour.
        if (const int colours = distinctColours(picture); colours < 16) {
            err() << shot.name << ": the window drew nothing (" << colours
                  << " colours); the platform plugin rendered no scene graph\n";
            ++failures;
            continue;
        }

        bool changed = false;
        if (!write(picture, path, changed)) {
            ++failures;
            continue;
        }
        out() << (changed ? "wrote     " : "unchanged ") << path << "  ("
              << picture.width() << "x" << picture.height() << ")\n";
    }
    out().flush();

    // Before the engine and the application go: the HDF5 thread closes the file
    // as a job of its own, and a job needs an event loop that is about to be
    // torn down.
    gui::H5Thread::shutdown();
    return failures == 0 ? 0 : 1;
}
