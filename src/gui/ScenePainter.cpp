// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "ScenePainter.hpp"

#include "plotcore/Figure.hpp"

#include <QFontMetricsF>
#include <QPainter>
#include <QPolygonF>

#include <algorithm>

namespace gui {
namespace {

QColor colourOf(const plotcore::Rgba& colour)
{
    return QColor::fromRgbF(static_cast<float>(colour.red), static_cast<float>(colour.green),
                            static_cast<float>(colour.blue), static_cast<float>(colour.alpha));
}

QPointF pointOf(const plotcore::Point& point)
{
    return {point.x(), point.y()};
}

double offsetOf(plotcore::TextItem::Align align, double extent)
{
    switch (align) {
    case plotcore::TextItem::Align::Middle:
        return -extent / 2.0;
    case plotcore::TextItem::Align::End:
        return -extent;
    case plotcore::TextItem::Align::Start:
        break;
    }
    return 0.0;
}

} // namespace

const QFont& SceneFonts::of(plotcore::TextRole role) const
{
    const auto slot = static_cast<std::size_t>(role);
    return faces[slot < faces.size() ? slot : 0];
}

TextPlacement placeText(const plotcore::TextItem& text, const SceneFonts& fonts)
{
    const QFontMetricsF base(fonts.of(text.role));
    const QFontMetricsF raised(fonts.of(plotcore::TextRole::TickMinor));
    const QString written = QString::fromStdString(text.base.empty() ? text.text : text.base);
    const QString exponent = QString::fromStdString(text.exponent);
    const double baseWidth = base.horizontalAdvance(written);
    const double width =
        baseWidth + (exponent.isEmpty() ? 0.0 : raised.horizontalAdvance(exponent));
    TextPlacement placed;
    placed.baseLeft = offsetOf(text.across, width);
    placed.baseTop = offsetOf(text.down, base.height());
    // The exponent stands on the base's baseline, raised: its own top is that
    // baseline, less the raise, less its own ascent.
    placed.exponentLeft = placed.baseLeft + baseWidth;
    placed.exponentTop = placed.baseTop + base.ascent() - text.raise - raised.ascent();
    return placed;
}

void paintMark(QPainter& painter, const plotcore::Mark& mark, const SceneFonts& fonts,
               const QRectF& area)
{
    painter.save();
    if (mark.clipped) {
        painter.setClipRect(area);
    }
    switch (mark.kind) {
    case plotcore::Mark::Kind::Text: {
        const plotcore::TextItem& text = mark.text;
        const TextPlacement placed = placeText(text, fonts);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.translate(pointOf(text.at));
        painter.rotate(text.turn);
        painter.setPen(colourOf(text.colour));
        const QFont& face = fonts.of(text.role);
        painter.setFont(face);
        const QString written = QString::fromStdString(text.base.empty() ? text.text : text.base);
        painter.drawText(QPointF(placed.baseLeft, placed.baseTop + QFontMetricsF(face).ascent()),
                         written);
        if (!text.exponent.empty()) {
            const QFont& raised = fonts.of(plotcore::TextRole::TickMinor);
            painter.setFont(raised);
            painter.drawText(
                QPointF(placed.exponentLeft, placed.exponentTop + QFontMetricsF(raised).ascent()),
                QString::fromStdString(text.exponent));
        }
        break;
    }
    case plotcore::Mark::Kind::Box: {
        // Filled to its bounds, and edged inside them.
        const QRectF box(pointOf(mark.from), pointOf(mark.to));
        painter.setRenderHint(QPainter::Antialiasing, mark.radius > 0.0);
        if (mark.fill.alpha > 0.0) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(colourOf(mark.fill));
            painter.drawRoundedRect(box, mark.radius, mark.radius);
        }
        if (mark.width > 0.0) {
            const double half = mark.width / 2.0;
            const double radius = std::max(0.0, mark.radius - half);
            QPen edge(colourOf(mark.stroke), mark.width);
            edge.setJoinStyle(Qt::MiterJoin);
            if (mark.dashed) {
                edge.setStyle(Qt::DashLine);
            }
            painter.setPen(edge);
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(box.adjusted(half, half, -half, -half), radius, radius);
        }
        break;
    }
    case plotcore::Mark::Kind::Circle: {
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(mark.width > 0.0 ? QPen(colourOf(mark.stroke), mark.width)
                                        : QPen(Qt::NoPen));
        painter.setBrush(mark.fill.alpha > 0.0 ? QBrush(colourOf(mark.fill)) : QBrush(Qt::NoBrush));
        painter.drawEllipse(pointOf(mark.from), mark.radius, mark.radius);
        break;
    }
    case plotcore::Mark::Kind::Segment: {
        if (mark.width > 0.0) {
            QPen pen(colourOf(mark.stroke), mark.width);
            pen.setCapStyle(Qt::FlatCap);
            painter.setPen(pen);
            painter.drawLine(pointOf(mark.from), pointOf(mark.to));
        }
        break;
    }
    }
    painter.restore();
}

void paintScene(QPainter& painter, const plotcore::Scene& scene, const SceneFonts& fonts)
{
    if (scene.ground.alpha > 0.0) {
        painter.fillRect(QRectF(0.0, 0.0, scene.width, scene.height), colourOf(scene.ground));
    }
    const QRectF area(scene.area.x, scene.area.y, scene.area.width, scene.area.height);
    for (const plotcore::Mark& mark : scene.under) {
        paintMark(painter, mark, fonts, area);
    }

    painter.save();
    painter.setClipRect(area);
    painter.setRenderHint(QPainter::Antialiasing, true);
    for (const plotcore::DrawnLine& line : scene.lines) {
        const QColor colour = colourOf(line.colour);
        // Round joins rather than the mitred ones the geometry path builds.
        // QPainter's miter limit is its own, and the two only have to agree
        // about where the line goes, not about how a corner is finished.
        painter.setPen(QPen(colour, line.width, Qt::SolidLine, Qt::FlatCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        for (const std::vector<plotcore::Point>& run : line.runs) {
            if (run.size() == 1) {
                painter.drawPoint(pointOf(run.front()));
                continue;
            }
            QPolygonF polyline;
            polyline.reserve(static_cast<qsizetype>(run.size()));
            for (const plotcore::Point& point : run) {
                polyline.append(pointOf(point));
            }
            painter.drawPolyline(polyline);
        }
        if (!line.markers.empty()) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(colour);
            for (const plotcore::Point& point : line.markers) {
                painter.drawEllipse(pointOf(point), line.markerRadius, line.markerRadius);
            }
        }
    }
    painter.restore();

    for (const plotcore::Mark& mark : scene.over) {
        paintMark(painter, mark, fonts, area);
    }
}

} // namespace gui
