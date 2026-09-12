// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "QCustomPlotSurface.hpp"

#include "QCustomPlotItem.hpp"

#include <QtQuick/QQuickWindow>

namespace spike {

QCustomPlotItem* QCustomPlotSurface::item() const
{
    if (item_ == nullptr && window() != nullptr) {
        item_ = window()->findChild<QCustomPlotItem*>(QStringLiteral("plotItem"));
    }
    return item_;
}

void QCustomPlotSurface::setVariant(const QString& variant)
{
    variant_ = variant;
    name_ = variant_.isEmpty() ? "qcustomplot"
                               : "qcustomplot/" + variant_.toStdString();
    if (QCustomPlotItem* plot = item()) {
        plot->setPopulateLegend(variant_ == QLatin1String("with-legend"));
        plot->setAdaptiveSampling(variant_ != QLatin1String("no-adaptive"));
    }
}

void QCustomPlotSurface::setSource(const SyntheticSource* source)
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
    if (QCustomPlotItem* plot = item()) {
        plot->setPopulateLegend(variant_ == QLatin1String("with-legend"));
        plot->setAdaptiveSampling(variant_ != QLatin1String("no-adaptive"));
        plot->setSource(source_);
        plot->setView(view_);
    }
    Q_EMIT changed();
}

void QCustomPlotSurface::setViewWindow(const ViewWindow& window)
{
    view_ = window;
    if (QCustomPlotItem* plot = item()) {
        plot->setView(view_);
    }
    Q_EMIT changed();
}

bool QCustomPlotSurface::setLogY(bool on)
{
    QCustomPlotItem* plot = item();
    return plot != nullptr && plot->setLogY(on);
}

QString QCustomPlotSurface::caption() const
{
    if (source_ == nullptr) {
        return QStringLiteral("nothing loaded");
    }
    return QStringLiteral("QCustomPlot 2.1.1 (%1)  %2 x %3  (%4)")
        .arg(variant_.isEmpty() ? QStringLiteral("adaptive") : variant_)
        .arg(source_->seriesCount())
        .arg(source_->pointCount())
        .arg(QString::fromLatin1(name(source_->shape())));
}

} // namespace spike
