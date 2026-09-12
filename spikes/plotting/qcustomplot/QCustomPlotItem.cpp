// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "QCustomPlotItem.hpp"

#include "common/Palette.hpp"
#include "common/SpikeMain.hpp"

#include <qcustomplot.h>

#include <QtCore/QCoreApplication>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>
#include <QtGui/QWheelEvent>

#include <cmath>

namespace spike {

QCustomPlotItem::QCustomPlotItem(QQuickItem* parent)
    : QQuickPaintedItem(parent)
{
    setAcceptedMouseButtons(Qt::AllButtons);
    setAcceptHoverEvents(true);

    plot_ = std::make_unique<QCustomPlot>();
    // Given a viewport before anything else, because QCustomPlot's constructor
    // ends by queueing a replot of itself. A QWidget that is never shown still
    // runs that cycle, and at a size of zero it allocates zero-sized paint
    // buffers and reports, once per layer:
    //   QPainter::begin: Paint device returned engine == 0
    //   QCPLayer::drawToPaintBuffer() paint buffer returned inactive painter
    //
    // setViewport() rather than resize(): a hidden widget receives no resize
    // event, so resizing it leaves the viewport at zero and changes nothing --
    // which is the shape of this whole bridge in one line. The widget is real
    // enough to run its own machinery and not real enough to be told its size
    // the way a widget normally is.
    //
    // It narrows the complaint and does not silence it. QCustomPlot draws its
    // buffered layers -- the overlay is one -- out of paint buffers that only
    // replot() allocates, and toPainter() is not replot(); calling replot()
    // here as well does not help, because a widget that is never shown never
    // completes one. So the messages stay, the pixels are correct, and what
    // they are really reporting is a library being driven down a path it does
    // not have. Left in the log rather than filtered out of it.
    plot_->setViewport(QRect(0, 0, kWindowWidth, kWindowHeight));
    plot_->setBackground(QBrush(plotGround()));
    plot_->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom | QCP::iSelectPlottables);
    plot_->xAxis->setBasePen(QPen(QColor(0x8b, 0x91, 0x9b)));
    plot_->yAxis->setBasePen(QPen(QColor(0x8b, 0x91, 0x9b)));
    plot_->xAxis->setTickLabelColor(QColor(0x8b, 0x91, 0x9b));
    plot_->yAxis->setTickLabelColor(QColor(0x8b, 0x91, 0x9b));
    plot_->xAxis->grid()->setPen(QPen(QColor(0x2e, 0x32, 0x38)));
    plot_->yAxis->grid()->setPen(QPen(QColor(0x2e, 0x32, 0x38)));
    // The grid draws. Stating the obvious only because the incumbent's does
    // not: PlotSurface.qml:571-578 records that Qt Graphs 6.11 draws neither
    // the grid nor the axis rules whatever the weights are set to.

    connect(plot_.get(), &QCustomPlot::afterReplot, this,
            [this]() { update(); });
}

QCustomPlotItem::~QCustomPlotItem() = default;

void QCustomPlotItem::setPopulateLegend(bool on)
{
    populateLegend_ = on;
    rebuild();
}

void QCustomPlotItem::setAdaptiveSampling(bool on)
{
    adaptiveSampling_ = on;
    for (int i = 0; i < plot_->graphCount(); ++i) {
        plot_->graph(i)->setAdaptiveSampling(on);
    }
    update();
}

bool QCustomPlotItem::setLogY(bool on)
{
    logY_ = on;
    if (on) {
        plot_->yAxis->setScaleType(QCPAxis::stLogarithmic);
        QSharedPointer<QCPAxisTickerLog> ticker(new QCPAxisTickerLog);
        plot_->yAxis->setTicker(ticker);
    } else {
        plot_->yAxis->setScaleType(QCPAxis::stLinear);
        plot_->yAxis->setTicker(QSharedPointer<QCPAxisTicker>(new QCPAxisTicker));
    }
    update();
    return true;
}

void QCustomPlotItem::setSource(const SyntheticSource* source)
{
    source_ = source;
    rebuild();
}

void QCustomPlotItem::rebuild()
{
    plot_->clearGraphs();
    if (source_ == nullptr) {
        update();
        return;
    }

    const int series = source_->seriesCount();
    const int count = source_->pointCount();
    const bool explicitX = source_->hasExplicitX();

    // The x column is shared by every line, so it is built once. QCustomPlot
    // wants it per graph and copies it per graph, which is the equivalent of
    // the QList<QPointF> the Qt Graphs path builds: the library holds its own
    // copy of every value, and the `rss` column is where that shows up.
    QVector<double> keys(count);
    for (int i = 0; i < count; ++i) {
        keys[i] = explicitX ? source_->xValues()[static_cast<std::size_t>(i)]
                            : source_->xStart()
                                  + static_cast<double>(i) * source_->xStep();
    }

    QVector<double> values(count);
    for (int line = 0; line < series; ++line) {
        const std::vector<double>& from = source_->line(line);
        for (int i = 0; i < count; ++i) {
            values[i] = from[static_cast<std::size_t>(i)];
        }
        QCPGraph* graph = plot_->addGraph();
        // Out of QCustomPlot's own legend again, because H5Scope has one of its
        // own -- PlotLegend.qml, virtualised, and already better than anything
        // here offers. QCustomPlot::addGraph() adds a legend item whether the
        // legend is shown or not, and removing a plottable later has to find
        // and remove that item: a linear search of the legend plus a layout
        // simplify, per plottable. Tearing down ten thousand graphs that way
        // is quadratic, and it was measured at 496 seconds.
        //
        // This is a configuration choice and not a fix; --variant with-legend
        // leaves the items in, so the report can say what the default costs.
        if (!populateLegend_) {
            graph->removeFromLegend();
        }
        graph->setPen(QPen(seriesColour(line, series)));
        graph->setAdaptiveSampling(adaptiveSampling_);
        // alreadySorted, because x from an origin and a step is. Telling
        // QCustomPlot so is the difference between a copy and a sort of every
        // line, and at ten thousand lines that is the whole build column.
        graph->setData(keys, values, !explicitX);
    }
    update();
}

void QCustomPlotItem::setView(const ViewWindow& view)
{
    view_ = view;
    plot_->xAxis->setRange(view.xMin, view.xMax);
    plot_->yAxis->setRange(view.yMin, view.yMax);
    update();
}

void QCustomPlotItem::geometryChange(const QRectF& newGeometry,
                                     const QRectF& oldGeometry)
{
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        plot_->setViewport(QRect(0, 0, static_cast<int>(newGeometry.width()),
                                 static_cast<int>(newGeometry.height())));
        update();
    }
}

void QCustomPlotItem::paint(QPainter* painter)
{
    const int w = static_cast<int>(width());
    const int h = static_cast<int>(height());
    if (w <= 0 || h <= 0) {
        return;
    }

    // Rendered into an image of our own and then blitted, rather than straight
    // onto the painter Qt Quick handed us. It is not a choice: QCPPainter is a
    // QPainter, a QPaintDevice may have only one active painter, and the one
    // Qt Quick gave us is already active on it. So a QPainter-based plot in a
    // Qt Quick scene costs one full-surface rasterisation, one full-surface
    // blit and one full-surface texture upload per frame -- 1400 x 900 x 4
    // bytes, three times -- whether or not a single pixel changed.
    //
    // The image is a member so that at least the buffer is not reallocated
    // every frame.
    if (buffer_.size() != QSize(w, h)) {
        buffer_ = QImage(w, h, QImage::Format_ARGB32_Premultiplied);
    }
    // Set here as well as in geometryChange, because QCustomPlot allocates its
    // own internal paint buffers from the viewport and a frame can arrive
    // before the item has ever been resized. When that happens it does not
    // fail, it complains once per layer and draws nothing:
    //   QPainter::begin: Paint device returned engine == 0
    //   QCPLayer::drawToPaintBuffer() paint buffer returned inactive painter
    if (plot_->viewport().size() != QSize(w, h)) {
        plot_->setViewport(QRect(0, 0, w, h));
    }
    QCPPainter plotPainter(&buffer_);
    plot_->toPainter(&plotPainter, w, h);
    plotPainter.end();
    painter->drawImage(0, 0, buffer_);
}

// --- input, forwarded by hand ----------------------------------------------
// A QWidget expects its events delivered to itself. Nothing in Qt Quick does
// that for it, so each one is copied and posted. Every interaction QCustomPlot
// offers -- drag, wheel zoom, selection, item picking, the axis-specific
// variants of all of them -- needs a line here, and anything it reports back
// arrives as a signal on an object with no place in the QML scene.

void QCustomPlotItem::mousePressEvent(QMouseEvent* event)
{
    QMouseEvent copy(event->type(), event->position(), event->globalPosition(),
                     event->button(), event->buttons(), event->modifiers());
    QCoreApplication::sendEvent(plot_.get(), &copy);
    update();
}

void QCustomPlotItem::mouseMoveEvent(QMouseEvent* event)
{
    QMouseEvent copy(event->type(), event->position(), event->globalPosition(),
                     event->button(), event->buttons(), event->modifiers());
    QCoreApplication::sendEvent(plot_.get(), &copy);
    update();
}

void QCustomPlotItem::mouseReleaseEvent(QMouseEvent* event)
{
    QMouseEvent copy(event->type(), event->position(), event->globalPosition(),
                     event->button(), event->buttons(), event->modifiers());
    QCoreApplication::sendEvent(plot_.get(), &copy);
    update();
}

void QCustomPlotItem::wheelEvent(QWheelEvent* event)
{
    QWheelEvent copy(event->position(), event->globalPosition(),
                     event->pixelDelta(), event->angleDelta(), event->buttons(),
                     event->modifiers(), event->phase(), event->inverted());
    QCoreApplication::sendEvent(plot_.get(), &copy);
    update();
}

} // namespace spike
