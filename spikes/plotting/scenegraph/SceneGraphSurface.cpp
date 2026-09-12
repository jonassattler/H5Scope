// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "SceneGraphSurface.hpp"

#include "PlotItem.hpp"

#include <QtQuick/QQuickWindow>

namespace spike {

const char* SceneGraphSurface::rendererName() const { return name_.c_str(); }

PlotItem* SceneGraphSurface::item() const
{
    if (item_ == nullptr && window() != nullptr) {
        item_ = window()->findChild<PlotItem*>(QStringLiteral("plotItem"));
        if (item_ != nullptr) {
            connect(item_, &PlotItem::drew, this, &SceneGraphSurface::changed);
        }
    }
    return item_;
}

void SceneGraphSurface::setVariant(const QString& variant)
{
    variant_ = variant;
    // The variant travels in the renderer's name, so two runs written into the
    // same results file cannot be mistaken for one another.
    if (variant_.isEmpty() || variant_ == QLatin1String("per-series")) {
        name_ = "scenegraph";
    } else {
        name_ = "scenegraph/" + variant_.toStdString();
    }
    if (PlotItem* plot = item()) {
        plot->setBatching(variant_ == QLatin1String("batched")
                              ? PlotItem::Batching::Batched
                              : PlotItem::Batching::PerSeries);
    }
}

void SceneGraphSurface::setSource(const SyntheticSource* source)
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
    if (PlotItem* plot = item()) {
        // The variant may have been set before the scene existed.
        plot->setBatching(variant_ == QLatin1String("batched")
                              ? PlotItem::Batching::Batched
                              : PlotItem::Batching::PerSeries);
        plot->setSource(source_);
        plot->setView(view_);
    }
    Q_EMIT changed();
}

void SceneGraphSurface::setViewWindow(const ViewWindow& window)
{
    view_ = window;
    if (PlotItem* plot = item()) {
        plot->setView(view_);
    }
    Q_EMIT changed();
}

bool SceneGraphSurface::setLogY(bool on)
{
    PlotItem* plot = item();
    if (plot == nullptr || !plot->setLogY(on)) {
        return false;
    }
    logY_ = on;
    Q_EMIT changed();
    return true;
}

QString SceneGraphSurface::caption() const
{
    if (source_ == nullptr) {
        return QStringLiteral("nothing loaded");
    }
    const PlotItem* plot = item();
    return QStringLiteral("scene graph (%1)  %2 x %3  (%4)  %5 vertices drawn")
        .arg(variant_.isEmpty() ? QStringLiteral("per-series") : variant_)
        .arg(source_->seriesCount())
        .arg(source_->pointCount())
        .arg(QString::fromLatin1(name(source_->shape())))
        .arg(plot == nullptr ? 0 : plot->verticesLastFrame());
}

} // namespace spike
