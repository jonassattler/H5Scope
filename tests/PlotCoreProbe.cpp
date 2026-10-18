// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "PlotCoreProbe.hpp"

#include "plotcore/Camera.hpp"
#include "plotcore/Format.hpp"
#include "plotcore/Palettes.hpp"
#include "plotcore/Ticks.hpp"

#include <vector>

namespace {

QString textOf(const std::string& utf8)
{
    return QString::fromStdString(utf8);
}

QColor colourOf(const plotcore::Rgba& colour)
{
    return QColor::fromRgbF(static_cast<float>(colour.red), static_cast<float>(colour.green),
                            static_cast<float>(colour.blue), static_cast<float>(colour.alpha));
}

plotcore::Rgba rgbaOf(const QVariant& value)
{
    const QColor colour = value.metaType().id() == QMetaType::QColor
                              ? value.value<QColor>()
                              : QColor::fromString(value.toString());
    return plotcore::Rgba::of(colour.redF(), colour.greenF(), colour.blueF(), colour.alphaF());
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

QStringList namesOf(const std::vector<std::string>& names)
{
    QStringList out;
    for (const std::string& name : names) {
        out.append(textOf(name));
    }
    return out;
}

QVariantList listOf(const std::vector<double>& values)
{
    QVariantList out;
    out.reserve(static_cast<qsizetype>(values.size()));
    for (const double v : values) {
        out.append(v);
    }
    return out;
}

std::vector<double> vectorOf(const QVariantList& values)
{
    std::vector<double> out;
    out.reserve(static_cast<std::size_t>(values.size()));
    for (const QVariant& v : values) {
        out.push_back(v.toDouble());
    }
    return out;
}

QVariantMap labelMap(const plotcore::TickLabel& label)
{
    return {{QStringLiteral("base"), textOf(label.base)},
            {QStringLiteral("exponent"), textOf(label.exponent)},
            {QStringLiteral("text"), textOf(label.text)}};
}

QVariantMap tickMap(const plotcore::TickSet& set)
{
    QVariantList texts;
    for (const plotcore::TickLabel& label : set.texts) {
        texts.append(labelMap(label));
    }
    return {{QStringLiteral("values"), listOf(set.values)},
            {QStringLiteral("texts"), texts},
            {QStringLiteral("ticks"), listOf(set.ticks)},
            {QStringLiteral("logarithmic"), set.logarithmic},
            {QStringLiteral("fallback"), set.fallback}};
}

/// The TickSet a QML `tickValues` answer describes.
plotcore::TickSet tickSetOf(const QVariantMap& found)
{
    plotcore::TickSet set;
    set.values = vectorOf(found.value(QStringLiteral("values")).toList());
    set.ticks = vectorOf(found.value(QStringLiteral("ticks")).toList());
    for (const QVariant& written : found.value(QStringLiteral("texts")).toList()) {
        const QVariantMap map = written.toMap();
        set.texts.push_back(
            plotcore::TickLabel{map.value(QStringLiteral("base")).toString().toStdString(),
                                map.value(QStringLiteral("exponent")).toString().toStdString(),
                                map.value(QStringLiteral("text")).toString().toStdString()});
    }
    set.logarithmic = found.value(QStringLiteral("logarithmic")).toBool();
    set.fallback = found.value(QStringLiteral("fallback")).toBool();
    return set;
}

plotcore::GridMode modeOf(const QString& mode)
{
    if (mode == QStringLiteral("none")) {
        return plotcore::GridMode::None;
    }
    if (mode == QStringLiteral("dense")) {
        return plotcore::GridMode::Dense;
    }
    if (mode == QStringLiteral("custom")) {
        return plotcore::GridMode::Custom;
    }
    return plotcore::GridMode::Loose;
}

} // namespace

QString PlotCoreProbe::toFixed(double value, int digits) const
{
    return textOf(plotcore::toFixed(value, digits));
}

QString PlotCoreProbe::toExponential(double value, int digits) const
{
    return textOf(plotcore::toExponential(value, digits));
}

QString PlotCoreProbe::toPrecision(double value, int precision) const
{
    return textOf(plotcore::toPrecision(value, precision));
}

QString PlotCoreProbe::numberText(double value) const
{
    return textOf(plotcore::numberText(value));
}

double PlotCoreProbe::jsRound(double value) const
{
    return plotcore::jsRound(value);
}

double PlotCoreProbe::niceStep(double span, double target) const
{
    return plotcore::niceStep(span, target);
}

int PlotCoreProbe::decimalsFor(double span) const
{
    return plotcore::decimalsFor(span);
}

QString PlotCoreProbe::labelFor(double value, double low, double high) const
{
    return textOf(plotcore::labelFor(value, low, high));
}

QString PlotCoreProbe::logLabelFor(double value) const
{
    return textOf(plotcore::logLabelFor(value));
}

double PlotCoreProbe::fractionOn(double value, double low, double high, bool logarithmic,
                                 double base) const
{
    return plotcore::fractionOn(value, low, high, logarithmic, base);
}

QVariantMap PlotCoreProbe::logLocate(double low, double high, double base, int request,
                                     int target) const
{
    const plotcore::LogLocated located = plotcore::logLocate(low, high, base, request, target);
    return {{QStringLiteral("majors"), listOf(located.majors)},
            {QStringLiteral("minors"), listOf(located.minors)},
            {QStringLiteral("stride"), located.stride},
            {QStringLiteral("linear"), located.linear}};
}

QVariantList PlotCoreProbe::logSublabels(double low, double high, double base) const
{
    return listOf(plotcore::logSublabels(low, high, base));
}

QVariantMap PlotCoreProbe::tickValues(double low, double high, bool logarithmic, double base,
                                      double extent, bool vertical, int target,
                                      double fontPixels) const
{
    plotcore::TickStyle style;
    style.target = target;
    style.fontPixels = fontPixels;
    return tickMap(plotcore::tickValues(low, high, logarithmic, base, extent, vertical, style));
}

QVariantList PlotCoreProbe::gridValues(const QString& mode, double low, double high, double custom,
                                       const QVariantMap& found, int target, int denseFactor) const
{
    plotcore::TickStyle style;
    style.target = target;
    style.denseFactor = denseFactor;
    return listOf(plotcore::gridValues(modeOf(mode), low, high, custom, tickSetOf(found), style));
}

QVariantList PlotCoreProbe::minorNumbersOf(bool enabled, const QString& mode,
                                           const QVariantList& values, const QVariantMap& found,
                                           double low, double high, double base, double extent,
                                           double gapPixels, double perChar) const
{
    QVariantList out;
    for (const plotcore::MinorNumber& number :
         plotcore::minorNumbersOf(enabled, modeOf(mode), vectorOf(values), tickSetOf(found), low,
                                  high, base, extent, gapPixels, perChar)) {
        out.append(QVariantMap{{QStringLiteral("at"), number.at},
                               {QStringLiteral("text"), textOf(number.text)}});
    }
    return out;
}

QVariantMap PlotCoreProbe::padded(double low, double high, bool logarithmic, double base) const
{
    const plotcore::Span span = plotcore::padded(low, high, logarithmic, base);
    return {{QStringLiteral("low"), span.low}, {QStringLiteral("high"), span.high}};
}

double PlotCoreProbe::valueAlong(double low, double high, double at, bool logarithmic,
                                 double base) const
{
    return plotcore::valueAlong(low, high, at, logarithmic, base);
}

double PlotCoreProbe::visibleLow(double low, double high, double zoom, double pan, bool logarithmic,
                                 double base) const
{
    return plotcore::visibleLow(low, high, zoom, pan, logarithmic, base);
}

double PlotCoreProbe::visibleHigh(double low, double high, double zoom, double pan,
                                  bool logarithmic, double base) const
{
    return plotcore::visibleHigh(low, high, zoom, pan, logarithmic, base);
}

double PlotCoreProbe::clampPan(double pan, double zoom, double low, double high, bool logarithmic,
                               double base, double keep) const
{
    return plotcore::clampPan(pan, zoom, low, high, logarithmic, base, keep);
}

double PlotCoreProbe::logZoomCeiling(double full, double held, double fraction, double minimumSpan,
                                     double base, double maxZoom) const
{
    return plotcore::logZoomCeiling(full, held, fraction, minimumSpan, base, maxZoom);
}

QVariantMap PlotCoreProbe::zoomedAxis(double zoom, double pan, double low, double high,
                                      double fraction, double factor, bool logarithmic, double base,
                                      double minimumSpan, double maxZoom, double keep) const
{
    const plotcore::AxisZoom next = plotcore::zoomedAxis(
        {zoom, pan}, low, high, fraction, factor, logarithmic, base, minimumSpan, maxZoom, keep);
    return {{QStringLiteral("zoom"), next.zoom}, {QStringLiteral("pan"), next.pan}};
}

QVariantMap PlotCoreProbe::viewedAxis(double low, double high, double from, double to,
                                      bool logarithmic, double base, double minimumSpan,
                                      double maxZoom, double keep) const
{
    const plotcore::AxisZoom view =
        plotcore::viewedAxis(low, high, from, to, logarithmic, base, minimumSpan, maxZoom, keep);
    return {{QStringLiteral("zoom"), view.zoom}, {QStringLiteral("pan"), view.pan}};
}

QVariantMap PlotCoreProbe::positiveSpan(double from, double to, double low, double high,
                                        bool logarithmic) const
{
    const plotcore::Span span = plotcore::positiveSpan(from, to, low, high, logarithmic);
    return {{QStringLiteral("from"), span.low}, {QStringLiteral("to"), span.high}};
}

QStringList PlotCoreProbe::paletteNames() const
{
    return namesOf(plotcore::paletteNames());
}

QStringList PlotCoreProbe::rampNames() const
{
    return namesOf(plotcore::rampNames());
}

QVariantList PlotCoreProbe::palette(const QString& name, bool onDark) const
{
    return coloursOf(plotcore::palette(name.toStdString(), onDark));
}

QVariantList PlotCoreProbe::ramp(const QString& name) const
{
    return coloursOf(plotcore::ramp(name.toStdString()));
}

QColor PlotCoreProbe::rampColor(const QString& name, double position) const
{
    const auto stops = plotcore::ramp(name.toStdString());
    return colourOf(plotcore::rampColor(stops.value_or(std::vector<plotcore::Rgba>{}), position));
}

QColor PlotCoreProbe::paperColor(const QColor& colour) const
{
    return colourOf(plotcore::paperColor(rgbaOf(colour)));
}

QColor PlotCoreProbe::seriesColor(const QVariantMap& colours, int series, int position, int count,
                                  bool paper, bool onDark, const QVariant& own) const
{
    plotcore::LineColours line = plotcore::LineColours::defaults(onDark);
    line.mode = colours.value(QStringLiteral("mode")).toString().toStdString();
    line.single = rgbaOf(colours.value(QStringLiteral("single")));
    line.rangeFrom = rgbaOf(colours.value(QStringLiteral("rangeFrom")));
    line.rangeTo = rgbaOf(colours.value(QStringLiteral("rangeTo")));
    line.reversed = colours.value(QStringLiteral("reversed")).toBool();
    line.from = colours.value(QStringLiteral("from")).toDouble();
    line.to = colours.value(QStringLiteral("to")).toDouble();
    line.custom = colours.value(QStringLiteral("custom")).toBool();
    std::optional<plotcore::Rgba> colour;
    if (own.isValid() && !own.isNull()) {
        colour = rgbaOf(own);
    }
    return colourOf(plotcore::seriesColor(line, series, position, count, paper, onDark, colour));
}

QVariantMap PlotCoreProbe::defaultColours(bool onDark) const
{
    const plotcore::LineColours line = plotcore::LineColours::defaults(onDark);
    return {{QStringLiteral("mode"), textOf(line.mode)},
            {QStringLiteral("single"), colourOf(line.single)},
            {QStringLiteral("rangeFrom"), colourOf(line.rangeFrom)},
            {QStringLiteral("rangeTo"), colourOf(line.rangeTo)},
            {QStringLiteral("reversed"), line.reversed},
            {QStringLiteral("from"), line.from},
            {QStringLiteral("to"), line.to}};
}

double PlotCoreProbe::seriesOpacity(int series, int count, int highlighted, bool bundle,
                                    bool onDark) const
{
    return plotcore::seriesOpacity(series, count, highlighted, bundle, onDark);
}

QVariantMap PlotCoreProbe::chrome(bool onDark, bool paper) const
{
    const plotcore::Chrome chrome = plotcore::chromeFor(onDark, paper);
    return {{QStringLiteral("ground"), colourOf(chrome.ground)},
            {QStringLiteral("ink"), colourOf(chrome.ink)},
            {QStringLiteral("ruleMinor"), colourOf(chrome.ruleMinor)},
            {QStringLiteral("ruleMajor"), colourOf(chrome.ruleMajor)},
            {QStringLiteral("axisRule"), colourOf(chrome.axisRule)},
            {QStringLiteral("cursorInk"), colourOf(chrome.cursorInk)}};
}
