// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// The plot's colour cycles and colour maps, as QML asks for them.
//
// They are plotcore's (plotcore/cpp/src/Palettes.cpp, where the argument for
// every one of them is written): a palette says which line is which, a ramp
// how far along something is, and each is a published or a solved table that
// a figure drawn here has to share with one drawn by anything else. The plot
// is drawn by plotcore and needs nothing from here; this is for the rest of
// the window that shows the same tables -- the swatches the plot settings
// offer, and the image and the table, which colour a value with the same
// maps. Theme.qml reads them through this rather than holding a second copy,
// because a second copy is what the viewer kept until a parity suite had to
// hold the two together.

#include <QColor>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace gui {

class PlotColours : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit PlotColours(QObject* parent = nullptr);

    /// The cycles and the maps by name, in the order the plot settings offer
    /// them. The names are what a saved view stores.
    Q_INVOKABLE [[nodiscard]] QStringList paletteNames() const;
    Q_INVOKABLE [[nodiscard]] QStringList rampNames() const;
    /// A cycle's entries as the dark or the light scope draws them, and a
    /// map's stops, which are the same in both; empty for a name that is
    /// neither.
    Q_INVOKABLE [[nodiscard]] QVariantList palette(const QString& name, bool onDark) const;
    Q_INVOKABLE [[nodiscard]] QVariantList ramp(const QString& name) const;
    /// The colour `position` of the way along `stops`, interpolated as the
    /// plot interpolates a map.
    Q_INVOKABLE [[nodiscard]] QColor rampColor(const QVariantList& stops, double position) const;
};

} // namespace gui
