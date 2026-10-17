// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "plotcore/qt/PlotWidget.hpp"

#include <QFontMetricsF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

#include <cmath>

namespace plotcore::qt {
namespace {

QColor qcolor(const Rgba& colour)
{
    return QColor::fromRgbF(static_cast<float>(colour.red), static_cast<float>(colour.green),
                            static_cast<float>(colour.blue), static_cast<float>(colour.alpha));
}

QPointF qpoint(const Point& point)
{
    return {point.x(), point.y()};
}

Modifiers modifiersOf(Qt::KeyboardModifiers keys)
{
    return Modifiers{keys.testFlag(Qt::ShiftModifier), keys.testFlag(Qt::ControlModifier),
                     keys.testFlag(Qt::AltModifier)};
}

std::optional<Button> buttonOf(Qt::MouseButton button)
{
    switch (button) {
    case Qt::LeftButton:
        return Button::Left;
    case Qt::MiddleButton:
        return Button::Middle;
    case Qt::RightButton:
        return Button::Right;
    default:
        return std::nullopt;
    }
}

std::size_t slot(TextRole role)
{
    return static_cast<std::size_t>(role);
}

} // namespace

PlotWidget::PlotWidget(QWidget* parent) : QWidget(parent)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::WheelFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMinimumSize(160, 120);
    applyMeasure();
}

void PlotWidget::setRoleFont(TextRole role, const QFont& font)
{
    fonts_[slot(role)] = font;
    fontSet_[slot(role)] = true;
    applyMeasure();
    update();
}

QFont PlotWidget::roleFont(TextRole role) const
{
    if (fontSet_[slot(role)]) {
        return fonts_[slot(role)];
    }
    QFont font = this->font();
    font.setPixelSize(static_cast<int>(std::lround(figure_.style().pixelsOf(role))));
    if (role == TextRole::Title) {
        font.setWeight(QFont::DemiBold);
    }
    return font;
}

void PlotWidget::applyMeasure()
{
    // Read the fonts when asked rather than captured now, so a font changed
    // later is the one the next frame is laid out in.
    TextMeasure measure;
    measure.width = [this](const std::string& text, TextRole role) {
        return QFontMetricsF(roleFont(role)).horizontalAdvance(QString::fromStdString(text));
    };
    measure.height = [this](TextRole role) { return QFontMetricsF(roleFont(role)).height(); };
    figure_.setMeasure(std::move(measure));
}

void PlotWidget::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::FontChange) {
        update();
    }
    QWidget::changeEvent(event);
}

void PlotWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    update();
}

void PlotWidget::paintEvent(QPaintEvent*)
{
    figure_.setSize(width(), height(), devicePixelRatioF());
    scene_ = figure_.scene();
    QPainter painter(this);
    paintScene(painter, scene_);
}

QImage PlotWidget::render(QSize size, double scale)
{
    const double width = std::max(1, size.width());
    const double height = std::max(1, size.height());
    QImage image(static_cast<int>(std::lround(width * scale)),
                 static_cast<int>(std::lround(height * scale)),
                 QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(scale);
    // Laid out at the size asked for, and put back: the pane goes on being
    // the pane.
    figure_.setSize(width, height, scale);
    const Scene picture = figure_.scene();
    {
        QPainter painter(&image);
        paintScene(painter, picture);
    }
    figure_.setSize(this->width(), this->height(), devicePixelRatioF());
    return image;
}

void PlotWidget::drawText(QPainter& painter, const TextItem& text) const
{
    const QFont font = roleFont(text.role);
    const QFont raised = roleFont(TextRole::TickMinor);
    const QFontMetricsF metrics(font);
    const QFontMetricsF raisedMetrics(raised);
    const QString base = QString::fromStdString(text.base.empty() ? text.text : text.base);
    const QString exponent = QString::fromStdString(text.exponent);
    const double baseWidth = metrics.horizontalAdvance(base);
    const double width =
        baseWidth + (exponent.isEmpty() ? 0.0 : raisedMetrics.horizontalAdvance(exponent));
    const double height = metrics.height();

    const auto offset = [](TextItem::Align align, double extent) {
        switch (align) {
        case TextItem::Align::Middle:
            return -extent / 2.0;
        case TextItem::Align::End:
            return -extent;
        case TextItem::Align::Start:
            break;
        }
        return 0.0;
    };

    painter.save();
    painter.translate(qpoint(text.at));
    if (text.vertical) {
        painter.rotate(-90.0);
    }
    const double left = offset(text.across, width);
    const double top = offset(text.down, height);
    painter.setPen(qcolor(text.colour));
    painter.setFont(font);
    painter.drawText(QPointF(left, top + metrics.ascent()), base);
    if (!exponent.isEmpty()) {
        painter.setFont(raised);
        painter.drawText(
            QPointF(left + baseWidth, top + metrics.ascent() - figure_.style().exponentRaise),
            exponent);
    }
    painter.restore();
}

void PlotWidget::paintScene(QPainter& painter, const Scene& scene) const
{
    const FigureStyle& style = figure_.style();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(QRectF(0, 0, scene.width, scene.height), qcolor(scene.ground));

    const auto stroke = [&](const Segment& segment) {
        QPen pen(qcolor(segment.colour), segment.width);
        pen.setCapStyle(Qt::FlatCap);
        painter.setPen(pen);
        painter.drawLine(qpoint(segment.from), qpoint(segment.to));
    };

    // The rules, under everything; crisp rather than smeared over two rows.
    painter.setRenderHint(QPainter::Antialiasing, false);
    for (const Segment& rule : scene.rules) {
        stroke(rule);
    }

    // The lines, inside the pane: a run drawn past either end is clipped here
    // as the viewer's item clips it.
    const QRectF area(scene.area.x, scene.area.y, scene.area.width, scene.area.height);
    painter.save();
    painter.setClipRect(area);
    painter.setRenderHint(QPainter::Antialiasing, true);
    for (const DrawnLine& line : scene.lines) {
        QPen pen(qcolor(line.colour), line.width);
        pen.setJoinStyle(Qt::RoundJoin);
        pen.setCapStyle(Qt::RoundCap);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        for (const std::vector<Point>& run : line.runs) {
            if (run.size() == 1) {
                painter.drawPoint(qpoint(run.front()));
                continue;
            }
            QPolygonF polygon;
            polygon.reserve(static_cast<qsizetype>(run.size()));
            for (const Point& point : run) {
                polygon.append(qpoint(point));
            }
            painter.drawPolyline(polygon);
        }
        if (!line.markers.empty()) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(qcolor(line.colour));
            for (const Point& point : line.markers) {
                painter.drawEllipse(qpoint(point), line.markerRadius, line.markerRadius);
            }
            painter.setBrush(Qt::NoBrush);
        }
    }
    painter.restore();

    // The axes, the ticks and the crosshair.
    painter.setRenderHint(QPainter::Antialiasing, false);
    for (const Segment& segment : scene.strokes) {
        stroke(segment);
    }
    painter.setRenderHint(QPainter::Antialiasing, true);
    for (const TextItem& text : scene.texts) {
        drawText(painter, text);
    }

    const Chrome chrome = chromeFor(figure_.settings().dark, figure_.settings().paper);

    // The legend: a swatch and a name a row.
    if (!scene.legend.empty()) {
        const QRectF box(scene.legendRect.x, scene.legendRect.y, scene.legendRect.width,
                         scene.legendRect.height);
        QColor ground = qcolor(scene.ground);
        ground.setAlphaF(0.85f);
        painter.setPen(QPen(qcolor(chrome.axisRule), style.ruleWidth));
        painter.setBrush(ground);
        painter.drawRect(box);
        painter.setBrush(Qt::NoBrush);
        for (const LegendEntry& entry : scene.legend) {
            painter.setPen(QPen(qcolor(entry.colour), 2.0 * style.lineWidth));
            painter.drawLine(qpoint(entry.from), qpoint(entry.to));
            TextItem name;
            name.text = name.base = entry.name;
            name.at = entry.textAt;
            name.down = TextItem::Align::Middle;
            name.role = TextRole::Legend;
            name.colour = chrome.ink;
            drawText(painter, name);
        }
    }

    // The sample under the pointer, and what it reads.
    if (scene.reading.valid) {
        const Reading& reading = scene.reading;
        painter.setPen(QPen(qcolor(chrome.cursorInk), style.ruleWidth));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(qpoint(reading.at), 3.0, 3.0);
        const QFont font = roleFont(TextRole::Tick);
        const QFontMetricsF metrics(font);
        const QString readout =
            QStringLiteral("x %1   y %2")
                .arg(QString::fromStdString(reading.xText), QString::fromStdString(reading.yText));
        const QRectF box(area.x() + style.gapM, area.y() + style.gapM,
                         metrics.horizontalAdvance(readout) + 2 * style.gapS,
                         metrics.height() + 2 * style.gapS);
        QColor ground = qcolor(scene.ground);
        ground.setAlphaF(0.85f);
        painter.fillRect(box, ground);
        painter.setFont(font);
        painter.setPen(qcolor(chrome.ink));
        painter.drawText(QPointF(box.x() + style.gapS, box.y() + style.gapS + metrics.ascent()),
                         readout);
    }

    if (scene.band.has_value()) {
        const PaneRect& band = *scene.band;
        QColor fill = qcolor(chrome.cursorInk);
        fill.setAlphaF(0.12f);
        painter.setPen(QPen(qcolor(chrome.cursorInk), style.ruleWidth, Qt::DashLine));
        painter.setBrush(fill);
        painter.drawRect(QRectF(band.x, band.y, band.width, band.height));
        painter.setBrush(Qt::NoBrush);
    }

    if (!scene.message.empty()) {
        painter.setFont(roleFont(TextRole::AxisName));
        painter.setPen(qcolor(chrome.ink));
        painter.drawText(area.adjusted(style.gapL, style.gapL, -style.gapL, -style.gapL),
                         Qt::AlignCenter | Qt::TextWordWrap, QString::fromStdString(scene.message));
    }
}

void PlotWidget::wheelEvent(QWheelEvent* event)
{
    const QPointF at = event->position();
    // Qt reports a vertical wheel with Alt held as a horizontal one.
    const QPoint delta = event->angleDelta();
    const double angle = delta.y() != 0 ? delta.y() : delta.x();
    figure_.wheel(at.x(), at.y(), angle, modifiersOf(event->modifiers()));
    event->accept();
    update();
    emit changed();
}

void PlotWidget::mousePressEvent(QMouseEvent* event)
{
    const std::optional<Button> button = buttonOf(event->button());
    if (!button.has_value()) {
        QWidget::mousePressEvent(event);
        return;
    }
    const QPointF at = event->position();
    figure_.press(at.x(), at.y(), *button, modifiersOf(event->modifiers()));
    event->accept();
    update();
}

void PlotWidget::mouseMoveEvent(QMouseEvent* event)
{
    const QPointF at = event->position();
    figure_.move(at.x(), at.y(), modifiersOf(event->modifiers()));
    event->accept();
    update();
    if (event->buttons() != Qt::NoButton) {
        emit changed();
    }
}

void PlotWidget::mouseReleaseEvent(QMouseEvent* event)
{
    const std::optional<Button> button = buttonOf(event->button());
    if (!button.has_value()) {
        QWidget::mouseReleaseEvent(event);
        return;
    }
    const QPointF at = event->position();
    figure_.release(at.x(), at.y(), *button, modifiersOf(event->modifiers()));
    event->accept();
    update();
    emit changed();
}

void PlotWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    const std::optional<Button> button = buttonOf(event->button());
    if (!button.has_value()) {
        QWidget::mouseDoubleClickEvent(event);
        return;
    }
    const QPointF at = event->position();
    figure_.doubleClick(at.x(), at.y(), *button);
    event->accept();
    update();
    emit changed();
}

void PlotWidget::leaveEvent(QEvent* event)
{
    figure_.leave();
    update();
    QWidget::leaveEvent(event);
}

} // namespace plotcore::qt
