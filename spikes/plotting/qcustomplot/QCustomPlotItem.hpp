// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// QCustomPlot inside a Qt Quick scene.
//
// QCustomPlot is a QWidget. It has been the default answer for scientific
// plotting in Qt for more than a decade, it has every feature this application
// is missing -- logarithmic axes, error bars, multiple axes, item annotations,
// selection, PNG/PDF export -- and it has no Qt Quick story at all. The only
// way into a QML scene is a QQuickPaintedItem: QCustomPlot renders through
// QPainter, and QQuickPaintedItem is the one Qt Quick item that hands out one.
//
// Two things about that are worth being precise about before reading the
// numbers, because neither of them is QCustomPlot's fault and both are real:
//
// 1. In Qt 6 a QQuickPaintedItem always paints into a QImage on the CPU and
//    then uploads that image as a texture. The framebuffer-object render
//    target of Qt 5 is gone with the RHI. So every frame is a full software
//    rasterisation of the pane plus a texture upload of the whole thing --
//    1400 x 900 x 4 bytes here -- whatever the plot did or did not change.
//    That cost is a property of the bridge, not of the library, and it is
//    charged to every QPainter-based plotting library equally.
//
// 2. Nothing forwards input. A QWidget expects QMouseEvent and QWheelEvent
//    delivered to itself; a QQuickItem receives its own. The bridge below
//    forwards them by hand, which works and is a page of code that has to
//    exist and be maintained for every interaction the library offers.
//
// 3. QCustomPlot cannot be given the painter Qt Quick hands out. QCPPainter
//    *is* a QPainter -- it has to be constructed on a QPaintDevice, and a
//    QPaintDevice may have only one active painter at a time, which the one
//    from paint() already is. So the plot is rasterised into an image of our
//    own and blitted, and the frame costs a full-surface rasterisation, a
//    full-surface blit and a full-surface texture upload. Every QML wrapper
//    for this library in the wild does the same thing through toPixmap().
//
// And one that is the library's own doing, in its favour: QCustomPlot has
// adaptive sampling, which is a per-pixel-column envelope of exactly the kind
// the application's stride thinning is not. --variant no-adaptive turns it off,
// which is how much of the result is the library and how much is the bridge.

#include "common/ScriptedGesture.hpp"
#include "common/SyntheticSource.hpp"

#include <QtGui/QImage>
#include <QtQml/qqmlregistration.h>
#include <QtQuick/QQuickPaintedItem>

#include <memory>

class QCustomPlot;

namespace spike {

class QCustomPlotItem : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT

public:
    explicit QCustomPlotItem(QQuickItem* parent = nullptr);
    ~QCustomPlotItem() override;

    void paint(QPainter* painter) override;

    void setSource(const SyntheticSource* source);
    void setView(const ViewWindow& view);
    /// QCustomPlot has had a logarithmic axis since before this application
    /// existed: QCPAxis::stLogarithmic plus a QCPAxisTickerLog.
    bool setLogY(bool on);

    /// Whether QCustomPlot's own adaptive sampling is on. It is by default,
    /// and it is the feature that makes a QPainter-based plot of a million
    /// points possible at all -- so the variant that turns it off is worth
    /// measuring, as the answer to "how much of this is the library and how
    /// much is the bridge".
    void setAdaptiveSampling(bool on);

    /// Whether each graph gets an entry in QCustomPlot's own legend. Off by
    /// default: H5Scope draws its own, and the entries are not free to remove.
    void setPopulateLegend(bool on);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;

private:
    void rebuild();

    std::unique_ptr<QCustomPlot> plot_;
    /// Where QCustomPlot rasterises, before the result is blitted onto the
    /// painter Qt Quick handed out. A member so the buffer survives the frame.
    QImage buffer_;
    const SyntheticSource* source_ = nullptr;
    ViewWindow view_;
    bool logY_ = false;
    bool adaptiveSampling_ = true;
    bool populateLegend_ = false;
};

} // namespace spike
