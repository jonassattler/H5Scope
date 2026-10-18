// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// plotcore's answers, handed to QML in the shapes PlotFrame.qml and
// PlotSurface.qml use for their own -- so tst_plotcore_parity.qml can ask both
// the same question and compare.
//
// Exposed as `PlotCore` in the QML suite's root context, beside EmbeddedFonts
// and the rest. Nothing here decides anything: every method is one call into
// plotcore and a conversion of what came back.

#include <QColor>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

class PlotCoreProbe : public QObject
{
    Q_OBJECT

public:
    explicit PlotCoreProbe(QObject* parent = nullptr) : QObject(parent) {}

    // --- how a number is written (JavaScript's own, ported) -------------
    [[nodiscard]] Q_INVOKABLE QString toFixed(double value, int digits) const;
    [[nodiscard]] Q_INVOKABLE QString toExponential(double value, int digits) const;
    [[nodiscard]] Q_INVOKABLE QString toPrecision(double value, int precision) const;
    [[nodiscard]] Q_INVOKABLE QString numberText(double value) const;
    [[nodiscard]] Q_INVOKABLE double jsRound(double value) const;

    // --- PlotFrame.qml ---------------------------------------------------
    [[nodiscard]] Q_INVOKABLE double niceStep(double span, double target) const;
    [[nodiscard]] Q_INVOKABLE int decimalsFor(double span) const;
    [[nodiscard]] Q_INVOKABLE QString labelFor(double value, double low, double high) const;
    [[nodiscard]] Q_INVOKABLE QString logLabelFor(double value) const;
    [[nodiscard]] Q_INVOKABLE double fractionOn(double value, double low, double high,
                                                bool logarithmic, double base) const;
    [[nodiscard]] Q_INVOKABLE QVariantMap logLocate(double low, double high, double base,
                                                    int request, int target) const;
    [[nodiscard]] Q_INVOKABLE QVariantList logSublabels(double low, double high, double base) const;
    [[nodiscard]] Q_INVOKABLE QVariantMap tickValues(double low, double high, bool logarithmic,
                                                     double base, double extent, bool vertical,
                                                     int target, double fontPixels) const;
    [[nodiscard]] Q_INVOKABLE QVariantList gridValues(const QString& mode, double low, double high,
                                                      double custom, const QVariantMap& found,
                                                      int target, int denseFactor) const;
    [[nodiscard]] Q_INVOKABLE QVariantList minorNumbersOf(bool enabled, const QString& mode,
                                                          const QVariantList& values,
                                                          const QVariantMap& found, double low,
                                                          double high, double base, double extent,
                                                          double gapPixels, double perChar) const;

    // --- PlotSurface.qml -------------------------------------------------
    [[nodiscard]] Q_INVOKABLE QVariantMap padded(double low, double high, bool logarithmic,
                                                 double base) const;
    [[nodiscard]] Q_INVOKABLE double valueAlong(double low, double high, double at,
                                                bool logarithmic, double base) const;
    [[nodiscard]] Q_INVOKABLE double visibleLow(double low, double high, double zoom, double pan,
                                                bool logarithmic, double base) const;
    [[nodiscard]] Q_INVOKABLE double visibleHigh(double low, double high, double zoom, double pan,
                                                 bool logarithmic, double base) const;
    [[nodiscard]] Q_INVOKABLE double clampPan(double pan, double zoom, double low, double high,
                                              bool logarithmic, double base, double keep) const;
    [[nodiscard]] Q_INVOKABLE double logZoomCeiling(double full, double held, double fraction,
                                                    double minimumSpan, double base,
                                                    double maxZoom) const;
    [[nodiscard]] Q_INVOKABLE QVariantMap zoomedAxis(double zoom, double pan, double low,
                                                     double high, double fraction, double factor,
                                                     bool logarithmic, double base,
                                                     double minimumSpan, double maxZoom,
                                                     double keep) const;
    [[nodiscard]] Q_INVOKABLE QVariantMap viewedAxis(double low, double high, double from,
                                                     double to, bool logarithmic, double base,
                                                     double minimumSpan, double maxZoom,
                                                     double keep) const;
    [[nodiscard]] Q_INVOKABLE QVariantMap positiveSpan(double from, double to, double low,
                                                       double high, bool logarithmic) const;

    // --- Theme.qml and PlotSurface's colours ------------------------------
    [[nodiscard]] Q_INVOKABLE QStringList paletteNames() const;
    [[nodiscard]] Q_INVOKABLE QStringList rampNames() const;
    [[nodiscard]] Q_INVOKABLE QVariantList palette(const QString& name, bool onDark) const;
    [[nodiscard]] Q_INVOKABLE QVariantList ramp(const QString& name) const;
    [[nodiscard]] Q_INVOKABLE QColor rampColor(const QString& name, double position) const;
    [[nodiscard]] Q_INVOKABLE QColor paperColor(const QColor& colour) const;
    /// `colours` holds PlotSurface's seven colour properties under their
    /// LineColours names; `own` is a colour or null.
    [[nodiscard]] Q_INVOKABLE QColor seriesColor(const QVariantMap& colours, int series,
                                                 int position, int count, bool paper, bool onDark,
                                                 const QVariant& own) const;
    [[nodiscard]] Q_INVOKABLE QVariantMap defaultColours(bool onDark) const;
    [[nodiscard]] Q_INVOKABLE double seriesOpacity(int series, int count, int highlighted,
                                                   bool bundle, bool onDark) const;
    [[nodiscard]] Q_INVOKABLE QVariantMap chrome(bool onDark, bool paper) const;
};
