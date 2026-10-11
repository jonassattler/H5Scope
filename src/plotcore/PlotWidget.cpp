// SPDX-FileCopyrightText: 2026 Andreas Martin
// SPDX-License-Identifier: GPL-3.0-only

#include "plotcore/PlotWidget.hpp"

#include "plotcore/LineStore.hpp"

#include "gui/PlotLevels.hpp"

#include <QtCore/QMetaObject>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>
#include <QtGui/QWheelEvent>

#include <algorithm>
#include <cmath>
#include <vector>

namespace gui {
namespace {

constexpr int kGutterBottom = 28;
constexpr int kGutterTop = 8;
constexpr int kGutterRight = 8;
constexpr int kOwnColumn = 56;
constexpr int kTickTarget = 6;

double niceStep(double span, int target)
{
    if (!(span > 0.0)) {
        return 0.0;
    }
    const double raw = span / static_cast<double>(std::max(1, target));
    const double magnitude = std::pow(10.0, std::floor(std::log10(raw)));
    const double scaled = raw / magnitude;
    const double factor = scaled <= 1.0 ? 1.0 : scaled <= 2.0 ? 2.0 : scaled <= 5.0 ? 5.0 : 10.0;
    return magnitude * factor;
}

} // namespace

PlotWidget::PlotWidget(QWidget* parent) : QWidget(parent)
{
    setMouseTracking(true);
    setMinimumSize(160, 120);
    setAutoFillBackground(false);
}

PlotWidget::~PlotWidget()
{
    if (store_ != nullptr) {
        store_->onChanged = nullptr;
    }
}

void PlotWidget::setLines(std::vector<PlotLine> lines, const PlotAxis& axis)
{
    lines_ = std::move(lines);
    axis_ = axis;
    update();
}

void PlotWidget::clear()
{
    lines_.clear();
    poses_.clear();
    axis_ = PlotAxis{};
    update();
}

void PlotWidget::syncPoses()
{
    if (poses_.size() != lines_.size()) {
        poses_.resize(lines_.size());
    }
}

int PlotWidget::ownCount() const
{
    int count = 0;
    for (const LinePose& pose : poses_) {
        if (pose.own) {
            ++count;
        }
    }
    return count;
}

void PlotWidget::setStore(LineStore* store)
{
    if (store_ == store) {
        return;
    }
    if (store_ != nullptr) {
        store_->onChanged = nullptr;
    }
    store_ = store;
    if (store_ != nullptr) {
        store_->onChanged = [this] {
            if (!refilling_) {
                refill();
            }
        };
        refill();
    }
}

LineStore* PlotWidget::store() const
{
    return store_;
}

void PlotWidget::setXLog(bool on)
{
    camera_.setXLog(on);
    applyView();
}

void PlotWidget::setYLog(bool on)
{
    if (camera_.yLog() == on) {
        return;
    }
    // The shift is in the common axis's units. Take each own window in the
    // data's units first, or the bands jump when those units change.
    struct Held
    {
        std::size_t index;
        double low;
        double high;
        double lineLow;
        double lineHigh;
        bool yLog;
    };
    std::vector<Held> held;
    if (store_ != nullptr) {
        for (std::size_t i = 0; i < poses_.size(); ++i) {
            if (!poses_[i].own) {
                continue;
            }
            double rawLow = 0.0;
            double rawHigh = 0.0;
            if (!store_->lineExtent(static_cast<int>(i), rawLow, rawHigh)) {
                continue;
            }
            double positive = 0.0;
            const bool hasPositive = store_->linePositiveMinimum(static_cast<int>(i), positive);
            double lineLow = 0.0;
            double lineHigh = 0.0;
            if (!PlotCamera::bandSpan(poses_[i].yLog, rawLow, rawHigh, positive, hasPositive, lineLow,
                                      lineHigh)) {
                continue;
            }
            const PlotCamera::Span window = camera_.lineSpan(
                poses_[i].scaleY, poses_[i].shiftY, poses_[i].yLog, lineLow, lineHigh);
            held.push_back(Held{i, window.low, window.high, lineLow, lineHigh, poses_[i].yLog});
        }
    }
    camera_.setYLog(on);
    if (store_ != nullptr) {
        store_->setYLog(on);
    }
    for (const Held& item : held) {
        double scale = poses_[item.index].scaleY;
        double shift = poses_[item.index].shiftY;
        if (camera_.fitLine(scale, shift, item.yLog, item.lineLow, item.lineHigh, item.low,
                            item.high)) {
            poses_[item.index].scaleY = scale;
            poses_[item.index].shiftY = shift;
        }
    }
    applyView();
}

void PlotWidget::setXLogBase(double base)
{
    camera_.setXLogBase(base);
    applyView();
}

void PlotWidget::setYLogBase(double base)
{
    camera_.setYLogBase(base);
    applyView();
}

void PlotWidget::setDataExtent(double xMin, double xMax, double yMin, double yMax,
                              double xPositiveMin, double yPositiveMin, long long samples)
{
    camera_.setDataExtent(xMin, xMax, yMin, yMax, xPositiveMin, yPositiveMin, samples);
}

void PlotWidget::resetView()
{
    camera_.reset();
    applyView();
}

void PlotWidget::stackLines()
{
    if (store_ == nullptr) {
        return;
    }
    setDataExtent(store_->xMin(), store_->xMax(), store_->minimum(), store_->maximum(),
                  store_->xPositiveMinimum(), store_->positiveMinimum(), store_->length());
    syncPoses();
    const int count = store_->lineCount();
    struct Item
    {
        int index;
        double low;
        double high;
    };
    std::vector<Item> items;
    items.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count && i < lineCount(); ++i) {
        double low = 0.0;
        double high = 0.0;
        if (!store_->lineExtent(i, low, high)) {
            continue;
        }
        double positive = 0.0;
        const bool hasPositive = store_->linePositiveMinimum(i, positive);
        double from = 0.0;
        double to = 0.0;
        if (!PlotCamera::bandSpan(camera_.yLog(), low, high, positive, hasPositive, from, to)) {
            continue;
        }
        items.push_back(Item{i, from, to});
    }
    if (items.empty()) {
        return;
    }
    camera_.resetY();
    const double bands = static_cast<double>(items.size());
    for (std::size_t band = 0; band < items.size(); ++band) {
        const Item& item = items[band];
        const double at = static_cast<double>(band);
        double scale = 1.0;
        double shift = 0.0;
        if (!camera_.placeLine(scale, shift, item.low, item.high, (bands - 1.0 - at) / bands,
                               (bands - at) / bands)) {
            continue;
        }
        LinePose& pose = poses_[static_cast<std::size_t>(item.index)];
        pose.scaleY = scale;
        pose.shiftY = shift;
        pose.yLog = camera_.yLog();
        pose.own = true;
    }
    applyView();
}

double PlotWidget::yPerPixel() const
{
    if (camera_.yLog()) {
        return 0.0;
    }
    const double span = viewMaxY() - viewMinY();
    const double pixels = std::max(1.0, plotArea().height() * devicePixelRatioF());
    if (!(span > 0.0) || !std::isfinite(span)) {
        return 0.0;
    }
    return span / pixels;
}

int PlotWidget::paneColumns() const
{
    const qreal ratio = devicePixelRatioF();
    const int pixels = std::max(1, static_cast<int>(std::lround(plotArea().width() * ratio)));
    return std::max(kColumnQuantum, (pixels / kColumnQuantum) * kColumnQuantum);
}

QRect PlotWidget::plotArea() const
{
    const bool common = lineCount() == 0 || ownCount() < lineCount();
    const int left = kOwnColumn * (ownCount() + (common ? 1 : 0));
    const int top = kGutterTop;
    const int right = std::max(left + 1, width() - kGutterRight);
    const int bottom = std::max(top + 1, height() - kGutterBottom);
    return QRect(QPoint(left, top), QPoint(right - 1, bottom - 1));
}

PlotView PlotWidget::viewForFrame() const
{
    const QRect area = plotArea();
    return camera_.frame(area.width(), area.height(), devicePixelRatioF(), lineCount());
}

void PlotWidget::projectAll()
{
    points_.clear();
    runs_.clear();
    const auto lines = static_cast<std::size_t>(lineCount());
    syncPoses();
    xSpan_ = camera_.xSpan();
    const double commonY = camera_.ySpan();
    const PlotView view = viewForFrame();
    lineRuns_.assign(lines + 1, 0);
    for (std::size_t line = 0; line < lines; ++line) {
        LinePose& pose = poses_[line];
        pose.ySpan = commonY;
        lines_[line].ownY = pose.own;
        lines_[line].yMin = view.yMin;
        lines_[line].yMax = view.yMax;
        PlotView drawn = view;
        if (pose.own) {
            double lineLow = 0.0;
            double lineHigh = 0.0;
            bool have = false;
            if (store_ != nullptr) {
                double rawLow = 0.0;
                double rawHigh = 0.0;
                if (store_->lineExtent(static_cast<int>(line), rawLow, rawHigh)) {
                    double positive = 0.0;
                    const bool hasPositive =
                        store_->linePositiveMinimum(static_cast<int>(line), positive);
                    have = PlotCamera::bandSpan(pose.yLog, rawLow, rawHigh, positive, hasPositive,
                                                lineLow, lineHigh);
                }
            }
            const PlotCamera::Span span =
                have ? camera_.lineSpan(pose.scaleY, pose.shiftY, pose.yLog, lineLow, lineHigh)
                     : camera_.lineSpan(pose.scaleY, pose.shiftY);
            drawn.yMin = span.low;
            drawn.yMax = span.high;
            drawn.yLog = pose.yLog;
            drawn.yLogBase = camera_.yLogBase();
            lines_[line].yMin = span.low;
            lines_[line].yMax = span.high;
            pose.ySpan = camera_.shiftUnits(pose.scaleY, pose.yLog, span.low, span.high);
        }
        lineRuns_[line] = static_cast<int>(runs_.size());
        projectLine(lines_[line], axis_, drawn, points_, runs_);
    }
    lineRuns_[lines] = static_cast<int>(runs_.size());
    drawnPoints_ = static_cast<int>(points_.size());
    drawnRuns_ = static_cast<int>(runs_.size());
}

void PlotWidget::refill()
{
    if (store_ == nullptr || refilling_) {
        return;
    }
    refilling_ = true;
    store_->setPaneColumns(paneColumns());
    store_->setYPerPixel(yPerPixel());
    store_->setVisibleRange(viewMinX(), viewMaxX());
    std::vector<PlotLine> lines;
    PlotAxis axis;
    store_->fillInto(lines, axis);
    setLines(std::move(lines), axis);
    setDataExtent(store_->xMin(), store_->xMax(), store_->minimum(), store_->maximum(),
                  store_->xPositiveMinimum(), store_->positiveMinimum(), store_->length());
    store_->releaseRetired();
    refilling_ = false;
}

void PlotWidget::applyView()
{
    if (store_ != nullptr) {
        store_->setYPerPixel(yPerPixel());
        store_->setVisibleRange(viewMinX(), viewMaxX());
    }
    update();
    Q_EMIT viewChanged();
}

void PlotWidget::zoomAt(const QPointF& pos, double factor, Qt::KeyboardModifiers modifiers)
{
    const QRect area = plotArea();
    if (area.width() <= 0 || area.height() <= 0) {
        return;
    }
    const bool shift = modifiers.testFlag(Qt::ShiftModifier);
    const bool control = modifiers.testFlag(Qt::ControlModifier);
    const bool alt = modifiers.testFlag(Qt::AltModifier);
    // Alt+Ctrl scales the curve under the pointer and leaves every other
    // axis where it is. A miss zooms nothing, for the reason h5plot_wheel
    // gives: the keys named one curve.
    if (alt && control && !shift) {
        syncPoses();
        const QPointF local = pos - QPointF(area.topLeft());
        const int index = nearestLine(points_, runs_, lineRuns_, local.x(), local.y(), 14.0);
        if (index < 0 || index >= static_cast<int>(poses_.size())) {
            return;
        }
        LinePose& pose = poses_[static_cast<std::size_t>(index)];
        if (!pose.own) {
            pose.yLog = camera_.yLog();
        }
        pose.own = true;
        const double along = 1.0 - local.y() / static_cast<double>(area.height());
        double lineLow = 0.0;
        double lineHigh = 0.0;
        bool have = false;
        if (store_ != nullptr) {
            double rawLow = 0.0;
            double rawHigh = 0.0;
            if (store_->lineExtent(index, rawLow, rawHigh)) {
                double positive = 0.0;
                const bool hasPositive = store_->linePositiveMinimum(index, positive);
                have = PlotCamera::bandSpan(pose.yLog, rawLow, rawHigh, positive, hasPositive, lineLow,
                                            lineHigh);
            }
        }
        if (have) {
            camera_.scaleLine(pose.scaleY, pose.shiftY, along, factor, pose.yLog, lineLow, lineHigh);
        } else {
            camera_.scaleLine(pose.scaleY, pose.shiftY, along, factor);
        }
        applyView();
        return;
    }
    const double fx = (pos.x() - area.x()) / static_cast<double>(area.width());
    const double fy = 1.0 - (pos.y() - area.y()) / static_cast<double>(area.height());
    camera_.zoomAt(fx, fy, factor, shift && !control, control && !shift);
    applyView();
}

void PlotWidget::panBy(double dx, double dy)
{
    const QRect area = plotArea();
    camera_.panBy(dx, dy, area.width(), area.height());
    applyView();
}

void PlotWidget::paintEvent(QPaintEvent*)
{
    projectAll();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), ground_);
    const QRect area = plotArea();
    painter.fillRect(area, pane_);
    drawChrome(painter, area);
    painter.save();
    painter.setClipRect(area);
    painter.translate(area.topLeft());
    drawLines(painter);
    painter.restore();
    drawBand(painter, area);
    QMetaObject::invokeMethod(this, &PlotWidget::drew, Qt::QueuedConnection);
}

void PlotWidget::drawBand(QPainter& painter, const QRect& area) const
{
    if (!banding_) {
        return;
    }
    const QRect band = QRect(bandOrigin_, bandCurrent_).normalized().intersected(area);
    if (band.isEmpty()) {
        return;
    }
    painter.setPen(QPen(ink_, 1.0));
    painter.setBrush(QColor(ink_.red(), ink_.green(), ink_.blue(), 48));
    painter.drawRect(band);
}

void PlotWidget::drawChrome(QPainter& painter, const QRect& area)
{
    painter.setPen(QPen(rule_, 1.0));
    const PlotView view = viewForFrame();
    const double xSpan = view.xMax - view.xMin;
    const double ySpan = view.yMax - view.yMin;
    const double xStep = niceStep(xSpan, kTickTarget);
    const double yStep = niceStep(ySpan, kTickTarget);

    if (xStep > 0.0 && std::isfinite(view.xMin) && std::isfinite(view.xMax)) {
        const double first = std::ceil(view.xMin / xStep) * xStep;
        for (double x = first; x <= view.xMax + xStep * 0.5; x += xStep) {
            const double fraction = xFractionOf(x, view);
            if (!std::isfinite(fraction)) {
                continue;
            }
            const int px = area.left() + static_cast<int>(std::lround(fraction * area.width()));
            painter.drawLine(px, area.top(), px, area.bottom());
            painter.setPen(ink_);
            painter.drawText(QRect(px - 40, area.bottom() + 4, 80, kGutterBottom - 6),
                             Qt::AlignHCenter | Qt::AlignTop, QString::number(x, 'g', 6));
            painter.setPen(QPen(rule_, 1.0));
        }
    }
    const bool shared = lineCount() == 0 || ownCount() < lineCount();
    if (shared && yStep > 0.0 && std::isfinite(view.yMin) && std::isfinite(view.yMax)) {
        const double first = std::ceil(view.yMin / yStep) * yStep;
        for (double y = first; y <= view.yMax + yStep * 0.5; y += yStep) {
            const double fraction = yFractionOf(y, view);
            if (!std::isfinite(fraction)) {
                continue;
            }
            const int py = area.bottom() - static_cast<int>(std::lround(fraction * area.height()));
            painter.drawLine(area.left(), py, area.right(), py);
            painter.setPen(ink_);
            painter.drawText(QRect(area.left() - kOwnColumn, py - 8, kOwnColumn - 6, 16),
                             Qt::AlignRight | Qt::AlignVCenter, QString::number(y, 'g', 6));
            painter.setPen(QPen(rule_, 1.0));
        }
    }
    painter.setPen(QPen(ink_, 1.0));
    painter.drawRect(area.adjusted(0, 0, -1, -1));
    int slot = 0;
    for (std::size_t index = 0; index < lines_.size(); ++index) {
        const PlotLine& line = lines_[index];
        if (!line.ownY || !(line.yMax > line.yMin)) {
            continue;
        }
        painter.setPen(line.colour);
        const int column = slot * kOwnColumn;
        PlotView own = view;
        own.yMin = line.yMin;
        own.yMax = line.yMax;
        own.yLog = index < poses_.size() && poses_[index].yLog;
        own.yLogBase = camera_.yLogBase();
        if (own.yLog && own.yLogBase > 1.0 && line.yMin > 0.0 && line.yMax > line.yMin) {
            const double firstExp = std::ceil(std::log(line.yMin) / std::log(own.yLogBase) - 1e-9);
            for (int step = 0; step < 12; ++step) {
                const double y = std::pow(own.yLogBase, firstExp + step);
                if (y > line.yMax * (1.0 + 1e-9)) {
                    break;
                }
                const double fraction = yFractionOf(y, own);
                if (!std::isfinite(fraction)) {
                    continue;
                }
                const int py = area.bottom() - static_cast<int>(std::lround(fraction * area.height()));
                painter.drawText(QRect(column, py - 8, kOwnColumn - 6, 16),
                                 Qt::AlignRight | Qt::AlignVCenter, QString::number(y, 'g', 6));
            }
        } else {
            const double step = niceStep(line.yMax - line.yMin, kTickTarget);
            if (!(step > 0.0)) {
                ++slot;
                continue;
            }
            const double first = std::ceil(line.yMin / step) * step;
            for (double y = first; y <= line.yMax + step * 0.5; y += step) {
                const double fraction = yFractionOf(y, own);
                const int py =
                    area.bottom() - static_cast<int>(std::lround(fraction * area.height()));
                painter.drawText(QRect(column, py - 8, kOwnColumn - 6, 16),
                                 Qt::AlignRight | Qt::AlignVCenter, QString::number(y, 'g', 6));
            }
        }
        ++slot;
    }
}

void PlotWidget::drawLines(QPainter& painter)
{
    for (std::size_t line = 0; line < lines_.size(); ++line) {
        QColor colour = lines_[line].colour;
        colour.setAlphaF(std::clamp(colour.alphaF() * lines_[line].opacity, 0.0, 1.0));
        painter.setPen(QPen(colour, lines_[line].width, Qt::SolidLine, Qt::FlatCap, Qt::RoundJoin));
        for (int r = lineRuns_[line]; r < lineRuns_[line + 1]; ++r) {
            const PlotRun& run = runs_[static_cast<std::size_t>(r)];
            painter.drawPolyline(&points_[static_cast<std::size_t>(run.first)], run.count);
        }
    }
}

void PlotWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (store_ != nullptr) {
        store_->setPaneColumns(paneColumns());
    }
}

void PlotWidget::wheelEvent(QWheelEvent* event)
{
    const int turned =
        event->angleDelta().y() != 0 ? event->angleDelta().y() : event->angleDelta().x();
    if (turned == 0) {
        return;
    }
    zoomAt(event->position(), std::pow(1.25, static_cast<double>(turned) / 120.0),
           event->modifiers());
    event->accept();
}

void PlotWidget::shiftLine(int index, double /*dx*/, double dy)
{
    syncPoses();
    if (index < 0 || index >= static_cast<int>(poses_.size())) {
        return;
    }
    LinePose& pose = poses_[static_cast<std::size_t>(index)];
    const QRect area = plotArea();
    if (!pose.own) {
        pose.yLog = camera_.yLog();
    }
    pose.own = true;
    const double scale = pose.scaleY > 0.0 ? pose.scaleY : 1.0;
    if (area.height() > 0 && pose.ySpan != 0.0) {
        pose.shiftY += dy / static_cast<double>(area.height()) * pose.ySpan / scale;
    }
    update();
}

void PlotWidget::mousePressEvent(QMouseEvent* event)
{
    const bool alt = event->modifiers().testFlag(Qt::AltModifier);
    if ((event->button() == Qt::MiddleButton || (event->button() == Qt::LeftButton && alt)) &&
        !banding_) {
        const QRect area = plotArea();
        const QPoint local = event->pos() - area.topLeft();
        shifting_ = nearestLine(points_, runs_, lineRuns_, local.x(), local.y(), 14.0);
        if (shifting_ >= 0) {
            shiftLast_ = event->pos();
            setCursor(Qt::SizeAllCursor);
        }
        event->accept();
        return;
    }
    if (event->button() == Qt::RightButton && !dragging_ && shifting_ < 0) {
        banding_ = true;
        bandOrigin_ = event->pos();
        bandCurrent_ = bandOrigin_;
        setCursor(Qt::CrossCursor);
        update();
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && !banding_ && shifting_ < 0) {
        dragging_ = true;
        lastDrag_ = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void PlotWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (shifting_ >= 0) {
        const QPoint delta = event->pos() - shiftLast_;
        shiftLast_ = event->pos();
        shiftLine(shifting_, delta.x(), delta.y());
        event->accept();
        return;
    }
    if (banding_) {
        bandCurrent_ = event->pos();
        update();
        event->accept();
        return;
    }
    if (dragging_) {
        const QPoint delta = event->pos() - lastDrag_;
        lastDrag_ = event->pos();
        panBy(delta.x(), delta.y());
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void PlotWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (shifting_ >= 0 && (event->button() == Qt::MiddleButton || event->button() == Qt::LeftButton)) {
        shifting_ = -1;
        unsetCursor();
        event->accept();
        return;
    }
    if (event->button() == Qt::RightButton && banding_) {
        banding_ = false;
        unsetCursor();
        const QRect area = plotArea();
        const QPoint from = bandOrigin_ - area.topLeft();
        const QPoint to = bandCurrent_ - area.topLeft();
        if (camera_.zoomToRegion(from.x(), from.y(), to.x(), to.y(), area.width(), area.height())) {
            applyView();
        } else {
            update();
        }
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && dragging_) {
        dragging_ = false;
        unsetCursor();
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void PlotWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        resetView();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

} // namespace gui
