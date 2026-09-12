// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "QtGraphsSurface.hpp"

#include "common/Palette.hpp"

#include <QtGraphs/QXYSeries>

#include <cmath>

namespace spike {

int QtGraphsSurface::seriesCount() const
{
    return source_ == nullptr ? 0 : source_->seriesCount();
}

QString QtGraphsSurface::caption() const
{
    if (source_ == nullptr) {
        return QStringLiteral("nothing loaded");
    }
    return QStringLiteral("Qt Graphs  %1 x %2  (%3)")
        .arg(source_->seriesCount())
        .arg(source_->pointCount())
        .arg(QString::fromLatin1(name(source_->shape())));
}

void QtGraphsSurface::setSource(const SyntheticSource* source)
{
    source_ = source;
    if (source_ != nullptr) {
        view_.xMin = source_->xStart();
        view_.xMax = source_->xStart()
                     + source_->xStep()
                           * static_cast<double>(source_->pointCount() - 1);
        if (source_->hasExplicitX()) {
            view_.xMin = -1.0;
            view_.xMax = 1.0;
        }
        view_.yMin = source_->minimum();
        view_.yMax = source_->maximum();
    }
    // Synchronous: the QML handler tears the GraphsView down and builds a new
    // one inside this emit, calling fill() once per line on the way. That is
    // what makes the build column mean what it says.
    Q_EMIT rebuilt();
    Q_EMIT viewChanged();
}

void QtGraphsSurface::setViewWindow(const ViewWindow& window)
{
    view_ = window;
    Q_EMIT viewChanged();
}

QColor QtGraphsSurface::seriesColor(int index) const
{
    return seriesColour(index, seriesCount());
}

void QtGraphsSurface::fill(QAbstractSeries* target, int series)
{
    auto* points = qobject_cast<QXYSeries*>(target);
    if (points == nullptr || source_ == nullptr) {
        return;
    }
    const std::vector<double>& values = source_->line(series);
    if (values.empty()) {
        points->clear();
        return;
    }

    QList<QPointF> line;
    line.reserve(static_cast<qsizetype>(values.size()));

    const bool explicitX = source_->hasExplicitX();
    const std::vector<double>& xs = source_->xValues();
    const double start = source_->xStart();
    const double step = source_->xStep();

    for (std::size_t i = 0; i < values.size(); ++i) {
        // A value that would not read is a gap in the line, not a zero. Which
        // is exactly what the application does -- and dropping the point is not
        // the same as drawing a gap: Qt Graphs joins whatever it is given, so
        // the two points either side of a dropped run are connected by a
        // straight line across it. The correctness suite is where that shows.
        if (!std::isfinite(values[i])) {
            continue;
        }
        const double x = explicitX ? xs[i]
                                   : start + static_cast<double>(i) * step;
        line.append(QPointF(x, values[i]));
    }

    // One bulk replace, not `count` appends.
    points->replace(line);
}

} // namespace spike
