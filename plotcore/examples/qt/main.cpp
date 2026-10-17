// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

// plotcore's Qt example.
//
//     plotcore-qt-demo                         the window
//     plotcore-qt-demo --self-test [out.png]   every control and gesture, checked

#include "DemoWindow.hpp"

#include <QApplication>

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("plotcore-qt-demo"));
    demo::DemoWindow window;
    const QStringList arguments = QApplication::arguments();
    const int test = static_cast<int>(arguments.indexOf(QStringLiteral("--self-test")));
    if (test >= 0) {
        window.resize(1400, 820);
        window.show();
        QApplication::processEvents();
        const QString picture = test + 1 < arguments.size() ? arguments[test + 1] : QString();
        return window.selfTest(picture) == 0 ? 0 : 1;
    }
    window.show();
    return QApplication::exec();
}
