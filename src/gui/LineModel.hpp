// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// What a plot of the viewer's says to whatever draws it.
//
// DatasetPlot and CustomPlot were built shaped alike from the outside, which
// is what let PlotSurface.qml draw either without knowing which it had; this
// is that shape, named, so that the figure item can ask it in C++ rather than
// by method names through the meta-object. Every member is one both plots
// already had under the same name.

#include "gui/LineTarget.hpp"

#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

namespace gui {

class LineModel
{
public:
    LineModel() = default;
    LineModel(const LineModel&) = delete;
    LineModel& operator=(const LineModel&) = delete;
    LineModel(LineModel&&) = delete;
    LineModel& operator=(LineModel&&) = delete;
    virtual ~LineModel() = default;

    // --- the lines ---------------------------------------------------------------
    /// Which series are drawn, in drawing order.
    [[nodiscard]] virtual QVariantList drawnSeries() const = 0;
    [[nodiscard]] virtual QString seriesLabel(int series) const = 0;
    /// A colour of the series' own, or an invalid QVariant.
    [[nodiscard]] virtual QVariant seriesOverride(int series) const = 0;
    /// `{ separate, fixed, finite, low, high, label }`.
    [[nodiscard]] virtual QVariantMap seriesAxis(int series) const = 0;
    /// Lend the drawn lines to `target`.
    virtual void fill(LineTarget* target) = 0;

    // --- what they span ----------------------------------------------------------
    [[nodiscard]] virtual bool hasData() const = 0;
    [[nodiscard]] virtual double minimum() const = 0;
    [[nodiscard]] virtual double maximum() const = 0;
    [[nodiscard]] virtual double positiveMinimum() const = 0;
    [[nodiscard]] virtual int sourcePointCount() const = 0;
    [[nodiscard]] virtual QString error() const = 0;

    // --- what is being looked at -------------------------------------------------
    virtual void setVisibleRange(double xMin, double xMax) = 0;
    virtual void setPaneColumns(int columns) = 0;
    virtual void setZoomFocus(double x, double factor) = 0;
    virtual void clearZoomFocus() = 0;
    [[nodiscard]] virtual bool xLog() const = 0;
    virtual void setXLog(bool logarithmic) = 0;
};

} // namespace gui
