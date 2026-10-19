// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "PlotColours.hpp"

#include "plotcore/Palettes.hpp"

#include <optional>
#include <string>
#include <vector>

namespace gui {
namespace {

QColor colourOf(const plotcore::Rgba& colour)
{
    return QColor::fromRgbF(static_cast<float>(colour.red), static_cast<float>(colour.green),
                            static_cast<float>(colour.blue), static_cast<float>(colour.alpha));
}

QStringList namesOf(const std::vector<std::string>& names)
{
    QStringList out;
    out.reserve(static_cast<qsizetype>(names.size()));
    for (const std::string& name : names) {
        out.append(QString::fromStdString(name));
    }
    return out;
}

QVariantList coloursOf(const std::optional<std::vector<plotcore::Rgba>>& stops)
{
    QVariantList out;
    if (stops.has_value()) {
        for (const plotcore::Rgba& stop : *stops) {
            out.append(colourOf(stop));
        }
    }
    return out;
}

} // namespace

PlotColours::PlotColours(QObject* parent) : QObject(parent) {}

QStringList PlotColours::paletteNames() const
{
    return namesOf(plotcore::paletteNames());
}

QStringList PlotColours::rampNames() const
{
    return namesOf(plotcore::rampNames());
}

QVariantList PlotColours::palette(const QString& name, bool onDark) const
{
    return coloursOf(plotcore::palette(name.toStdString(), onDark));
}

QVariantList PlotColours::ramp(const QString& name) const
{
    return coloursOf(plotcore::ramp(name.toStdString()));
}

QColor PlotColours::rampColor(const QVariantList& stops, double position) const
{
    std::vector<plotcore::Rgba> colours;
    colours.reserve(static_cast<std::size_t>(stops.size()));
    for (const QVariant& stop : stops) {
        const QColor colour = stop.value<QColor>();
        colours.push_back(
            plotcore::Rgba::of(colour.redF(), colour.greenF(), colour.blueF(), colour.alphaF()));
    }
    return colourOf(plotcore::rampColor(colours, position));
}

} // namespace gui
