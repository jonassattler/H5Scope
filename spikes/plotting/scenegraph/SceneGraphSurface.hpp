// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// The scene-graph spike's side of the Surface contract.
//
// Thinner than the Qt Graphs one, and that is the finding rather than a
// shortcut: there is no series object to create, no graph to tear down and
// rebuild, and no bulk replace across a QML boundary. The data stays where the
// application already has it -- a vector of doubles per line -- and what
// crosses into the renderer is a pointer. Everything the other spike spends
// its build column on happens here in the frame, where it is bounded by the
// pixel width instead of by the dataset.

#include "common/SpikeMain.hpp"

#include <QtCore/QString>

#include <string>

namespace spike {

class PlotItem;

class SceneGraphSurface : public QmlSurface
{
    Q_OBJECT

    Q_PROPERTY(QString caption READ caption NOTIFY changed FINAL)
    Q_PROPERTY(double xMin READ xMin NOTIFY changed FINAL)
    Q_PROPERTY(double xMax READ xMax NOTIFY changed FINAL)
    Q_PROPERTY(double yMin READ yMin NOTIFY changed FINAL)
    Q_PROPERTY(double yMax READ yMax NOTIFY changed FINAL)
    Q_PROPERTY(bool logY READ logY NOTIFY changed FINAL)

public:
    using QmlSurface::QmlSurface;

    [[nodiscard]] const char* rendererName() const override;

    void setSource(const SyntheticSource* source) override;
    void setViewWindow(const ViewWindow& window) override;
    void setVariant(const QString& variant) override;

    /// The projection is ours, so a log axis is arithmetic rather than a
    /// feature request.
    [[nodiscard]] bool canDrawLogY() const override { return true; }
    bool setLogY(bool on) override;

    [[nodiscard]] QString caption() const;
    [[nodiscard]] double xMin() const { return view_.xMin; }
    [[nodiscard]] double xMax() const { return view_.xMax; }
    [[nodiscard]] double yMin() const { return view_.yMin; }
    [[nodiscard]] double yMax() const { return view_.yMax; }
    [[nodiscard]] bool logY() const { return logY_; }

Q_SIGNALS:
    void changed();

private:
    /// The item in the loaded scene, found once and cached. The scene declares
    /// exactly one.
    [[nodiscard]] PlotItem* item() const;

    const SyntheticSource* source_ = nullptr;
    ViewWindow view_;
    bool logY_ = false;
    QString variant_;
    std::string name_ = "scenegraph";
    mutable PlotItem* item_ = nullptr;
};

} // namespace spike
