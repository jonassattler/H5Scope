// SPDX-FileCopyrightText: 2026 Andreas Martin
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// A line plot on a QWidget, using the same projection the QML plot uses.
//
// PlotItem draws on the Qt Quick scene graph. This is the other host: the
// same PlotLine / PlotView / projectLine path, painted with QPainter -- the
// fallback PlotItem already carries for the software renderer, without the
// scene graph around it. The chrome (gutters, linear ticks, a hairline frame)
// lives here rather than in QML, because a QWidget has nowhere else to put it.
//
// Gestures live in PlotCamera, the Qt-free copy of PlotSurface.qml's zoom
// and pan. This file is the window around that.

#include "gui/PlotProjection.hpp"
#include "plotcore/View.hpp"

#include <QColor>
#include <QWidget>

#include <vector>

class QPainter;

namespace gui {

class LineStore;

class PlotWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PlotWidget(QWidget* parent = nullptr);
    ~PlotWidget() override;

    void setLines(std::vector<PlotLine> lines, const PlotAxis& axis);
    void clear();

    void setStore(LineStore* store);
    [[nodiscard]] LineStore* store() const;

    void setXLog(bool on);
    void setYLog(bool on);
    void setXLogBase(double base);
    void setYLogBase(double base);
    [[nodiscard]] bool xLog() const { return camera_.xLog(); }
    [[nodiscard]] bool yLog() const { return camera_.yLog(); }
    [[nodiscard]] double xLogBase() const { return camera_.xLogBase(); }
    [[nodiscard]] double yLogBase() const { return camera_.yLogBase(); }

    void setDataExtent(double xMin, double xMax, double yMin, double yMax, double xPositiveMin,
                       double yPositiveMin, long long samples);
    void resetView();

    /// Each line into an equal band, the first at the top, scaled to its own
    /// padded span. Y returns to the whole axis. X stays. A line a logarithmic
    /// axis cannot draw is left as it was.
    void stackLines();

    [[nodiscard]] double viewMinX() const { return camera_.viewMinX(); }
    [[nodiscard]] double viewMaxX() const { return camera_.viewMaxX(); }
    [[nodiscard]] double viewMinY() const { return camera_.viewMinY(); }
    [[nodiscard]] double viewMaxY() const { return camera_.viewMaxY(); }

    [[nodiscard]] int paneColumns() const;
    /// One device pixel of the pane, in y units. Zero on a logarithmic y,
    /// where a pixel is not a fixed value.
    [[nodiscard]] double yPerPixel() const;
    [[nodiscard]] int lineCount() const { return static_cast<int>(lines_.size()); }
    [[nodiscard]] int drawnPointCount() const { return drawnPoints_; }
    [[nodiscard]] int drawnRunCount() const { return drawnRuns_; }
    [[nodiscard]] QRect plotArea() const;

Q_SIGNALS:
    void viewChanged();
    void drew();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    [[nodiscard]] PlotView viewForFrame() const;
    void projectAll();
    void refill();
    void applyView();
    void zoomAt(const QPointF& pos, double factor, Qt::KeyboardModifiers modifiers);
    void panBy(double dx, double dy);
    void drawBand(QPainter& painter, const QRect& area) const;
    void syncPoses();
    [[nodiscard]] int ownCount() const;
    void shiftLine(int index, double dx, double dy);
    void drawChrome(QPainter& painter, const QRect& area);
    void drawLines(QPainter& painter);

    std::vector<PlotLine> lines_;
    PlotAxis axis_;
    LineStore* store_ = nullptr;
    PlotCamera camera_;
    bool refilling_ = false;
    bool dragging_ = false;
    QPoint lastDrag_;
    bool banding_ = false;
    QPoint bandOrigin_;
    QPoint bandCurrent_;

    struct LinePose
    {
        double shiftX = 0.0;
        double shiftY = 0.0;
        /// 1 is the common axis. See PlotCamera::lineSpan.
        double scaleY = 1.0;
        bool own = false;
        /// Copied from the camera when the line leaves it. The checkbox does
        /// not change it afterwards.
        bool yLog = false;
        double ySpan = 1.0;
    };
    std::vector<LinePose> poses_;
    double xSpan_ = 1.0;
    int shifting_ = -1;
    QPoint shiftLast_;

    std::vector<QPointF> points_;
    std::vector<PlotRun> runs_;
    std::vector<int> lineRuns_;
    int drawnPoints_ = 0;
    int drawnRuns_ = 0;

    QColor ground_{0, 0, 0};
    QColor pane_{0, 0, 0};
    QColor ink_{220, 220, 220};
    QColor rule_{40, 40, 40};
};

} // namespace gui
