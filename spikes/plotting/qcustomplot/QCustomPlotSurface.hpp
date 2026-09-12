// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// The QCustomPlot spike's side of the Surface contract.

#include "common/SpikeMain.hpp"

#include <QtCore/QString>

#include <string>

namespace spike {

class QCustomPlotItem;

class QCustomPlotSurface : public QmlSurface
{
    Q_OBJECT

    Q_PROPERTY(QString caption READ caption NOTIFY changed FINAL)

public:
    using QmlSurface::QmlSurface;

    [[nodiscard]] const char* rendererName() const override { return name_.c_str(); }

    void setSource(const SyntheticSource* source) override;
    void setViewWindow(const ViewWindow& window) override;
    void setVariant(const QString& variant) override;

    /// QCPAxis::stLogarithmic plus a QCPAxisTickerLog, and it has been there
    /// since version 1. This is the single feature the incumbent cannot be
    /// made to have at all.
    [[nodiscard]] bool canDrawLogY() const override { return true; }
    bool setLogY(bool on) override;

    [[nodiscard]] QString caption() const;

Q_SIGNALS:
    void changed();

private:
    [[nodiscard]] QCustomPlotItem* item() const;

    const SyntheticSource* source_ = nullptr;
    ViewWindow view_;
    QString variant_;
    std::string name_ = "qcustomplot";
    mutable QCustomPlotItem* item_ = nullptr;
};

} // namespace spike
