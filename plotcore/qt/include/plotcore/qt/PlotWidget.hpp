// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// plotcore::Figure in a QWidget.
//
// The adapter does the three things a host does and nothing else: it tells the
// figure how big the widget is and how its fonts measure, it hands every wheel,
// press, move and release to the figure as it arrives, and it paints the Scene
// that comes back. Nothing here decides where a tick goes or what a gesture
// does -- that is what lets the Python and Rust adapters draw the same plot
// from the same figure.
//
// Painted with QPainter rather than the scene's triangles: QPainter strokes a
// polyline as well as plotcore's mesh would and the widget gets antialiasing
// and hairlines for nothing. A host that fills triangles (egui, a GPU) uses
// DrawnLine::triangles instead; the runs and the triangles are one stroke.

#include "plotcore/Figure.hpp"

#include <QFont>
#include <QImage>
#include <QWidget>

#include <array>

class QPainter;

namespace plotcore::qt {

class PlotWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PlotWidget(QWidget* parent = nullptr);

    [[nodiscard]] Figure& figure() { return figure_; }
    [[nodiscard]] const Figure& figure() const { return figure_; }

    /// The face each role is set in. Defaults to the widget's font at the
    /// figure style's pixel sizes.
    void setRoleFont(TextRole role, const QFont& font);
    [[nodiscard]] QFont roleFont(TextRole role) const;

    /// The picture at `size` logical pixels, drawn at `scale` device pixels
    /// each -- for an export, which is the same figure laid out again rather
    /// than the screen magnified.
    [[nodiscard]] QImage render(QSize size, double scale = 1.0);

    /// Paint `scene` with `painter`, which is all a host other than this widget
    /// needs to draw a figure with QPainter.
    void paintScene(QPainter& painter, const Scene& scene) const;

    /// The last scene painted, for a host that wants the reading or the view.
    [[nodiscard]] const Scene& lastScene() const { return scene_; }

    QSize sizeHint() const override { return {800, 500}; }

signals:
    /// Something the reader did moved the view, a line or the reading. A host
    /// with controls bound to the figure refreshes them here.
    void changed();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    void applyMeasure();
    void drawText(QPainter& painter, const TextItem& text) const;

    Figure figure_;
    Scene scene_;
    std::array<QFont, 5> fonts_;
    std::array<bool, 5> fontSet_{};
};

} // namespace plotcore::qt
