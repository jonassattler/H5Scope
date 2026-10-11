// SPDX-FileCopyrightText: 2026 Andreas Martin
// SPDX-License-Identifier: GPL-3.0-only

// A QWidget plot of a million-point line, so the store and the host can be
// seen without the HDF5 viewer around them.
//
// Wheel zooms (Shift: x, Ctrl: y). Drag pans. Double-click resets. The line
// is a sine with a one-sample spike at the middle -- the spike is why the
// envelope exists, and it has to stay visible at every zoom.

#include "plotcore/LineStore.hpp"
#include "plotcore/PlotWidget.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QVBoxLayout>
#include <QWidget>

#include <cmath>
#include <vector>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    constexpr int kCount = 1'000'000;
    std::vector<double> values(static_cast<std::size_t>(kCount));
    for (int i = 0; i < kCount; ++i) {
        values[static_cast<std::size_t>(i)] =
            std::sin(2.0 * 3.14159265358979323846 * static_cast<double>(i) / 4000.0);
    }
    values[static_cast<std::size_t>(kCount / 2)] = 8.0;

    // The widget clears its callback in its destructor. The store is a local
    // declared first, so that destructor runs while the store is still alive.
    // The raw samples above outlive the store for the same reason: they are
    // borrowed, and the pyramid is the copy.
    gui::LineStore store;
    store.addLine(values.data(), static_cast<qsizetype>(values.size()));

    QWidget window;
    window.setWindowTitle(QStringLiteral("plotcore demo"));
    window.resize(960, 540);

    auto* plot = new gui::PlotWidget;
    auto* logY = new QCheckBox(QStringLiteral("Log y"));
    QObject::connect(logY, &QCheckBox::toggled, plot, &gui::PlotWidget::setYLog);

    auto* layout = new QVBoxLayout(&window);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(plot, 1);
    layout->addWidget(logY);
    plot->setStore(&store);

    window.show();
    return QApplication::exec();
}
