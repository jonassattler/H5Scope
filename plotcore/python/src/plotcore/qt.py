# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-FileCopyrightText: 2026 Andreas Martin
# SPDX-License-Identifier: GPL-3.0-only

"""A figure in a PyQt6 widget.

What plotcore::qt::PlotWidget does in C++, in Python: tell the figure the
widget's size and how its fonts measure, hand it every wheel, press, move and
release, and paint the Scene that comes back. Nothing here decides where a tick
goes or what a gesture does.

    pip install plotcore[qt]
"""

from __future__ import annotations

from typing import Dict, Optional

try:
    from PyQt6.QtCore import QPointF, QRectF, QSize, Qt, pyqtSignal
    from PyQt6.QtGui import (
        QColor,
        QFont,
        QFontMetricsF,
        QImage,
        QPainter,
        QPen,
        QPolygonF,
    )
    from PyQt6.QtWidgets import QWidget
except ImportError as error:  # pragma: no cover -- said once, plainly
    raise ImportError("plotcore.qt needs PyQt6: pip install plotcore[qt]") from error

from .figure import Button, Figure, Modifiers
from .scene import Align, Scene, Text, TextRole

def _qcolor(colour) -> QColor:
    return QColor.fromRgbF(*(float(c) for c in colour))


def _modifiers(keys) -> Modifiers:
    out = Modifiers.NONE
    if keys & Qt.KeyboardModifier.ShiftModifier:
        out |= Modifiers.SHIFT
    if keys & Qt.KeyboardModifier.ControlModifier:
        out |= Modifiers.CONTROL
    if keys & Qt.KeyboardModifier.AltModifier:
        out |= Modifiers.ALT
    return out


def _button(button) -> Optional[Button]:
    return {
        Qt.MouseButton.LeftButton: Button.LEFT,
        Qt.MouseButton.MiddleButton: Button.MIDDLE,
        Qt.MouseButton.RightButton: Button.RIGHT,
    }.get(button)


class PlotWidget(QWidget):
    """A QWidget over a plotcore Figure. `changed` is emitted after anything
    the reader did moved the view, a line or the reading."""

    changed = pyqtSignal()

    def __init__(self, figure: Optional[Figure] = None, parent: Optional[QWidget] = None):
        super().__init__(parent)
        self.figure = figure if figure is not None else Figure()
        self.last_scene: Optional[Scene] = None
        self._fonts: Dict[TextRole, QFont] = {}
        self.setMouseTracking(True)
        self.setFocusPolicy(Qt.FocusPolicy.WheelFocus)
        self.setAttribute(Qt.WidgetAttribute.WA_OpaquePaintEvent)
        self.setMinimumSize(160, 120)
        self._metrics: Dict[TextRole, QFontMetricsF] = {}
        self.figure.set_text_measure(self._text_width, self._text_height)

    def sizeHint(self) -> QSize:  # noqa: N802 -- Qt's name
        return QSize(800, 500)

    # --- fonts ----------------------------------------------------------------

    def set_role_font(self, role: TextRole, font: QFont) -> None:
        self._fonts[role] = font
        self._metrics.pop(role, None)
        self.update()

    def role_font(self, role: TextRole) -> QFont:
        if role in self._fonts:
            return self._fonts[role]
        font = QFont(self.font())
        sizes = {
            TextRole.TICK: "tickPixels",
            TextRole.TICK_MINOR: "tickMinorPixels",
            TextRole.TITLE: "titlePixels",
            TextRole.AXIS_NAME: "axisNamePixels",
            TextRole.LEGEND: "legendPixels",
        }
        font.setPixelSize(max(1, round(self.figure.style[sizes[role]])))
        if role == TextRole.TITLE:
            font.setWeight(QFont.Weight.DemiBold)
        return font

    def _font_metrics(self, role: TextRole) -> QFontMetricsF:
        if role not in self._metrics:
            self._metrics[role] = QFontMetricsF(self.role_font(role))
        return self._metrics[role]

    def _text_width(self, text: str, role: TextRole) -> float:
        return self._font_metrics(role).horizontalAdvance(text)

    def _text_height(self, role: TextRole) -> float:
        return self._font_metrics(role).height()

    def changeEvent(self, event) -> None:  # noqa: N802
        # Also called while the widget is still being built, before there is
        # anything measured to forget.
        metrics = getattr(self, "_metrics", None)
        if metrics is not None:
            metrics.clear()
        super().changeEvent(event)

    # --- painting ---------------------------------------------------------------

    def paintEvent(self, _event) -> None:  # noqa: N802
        self.figure.set_size(self.width(), self.height(), self.devicePixelRatioF())
        self.last_scene = self.figure.scene()
        painter = QPainter(self)
        try:
            self.paint_scene(painter, self.last_scene)
        finally:
            painter.end()

    def render(self, width: int, height: int, scale: float = 1.0) -> QImage:
        """The picture at `width` x `height` logical pixels, drawn at `scale`
        device pixels each -- laid out again at that size, not magnified."""
        image = QImage(round(width * scale), round(height * scale),
                       QImage.Format.Format_ARGB32_Premultiplied)
        image.setDevicePixelRatio(scale)
        self.figure.set_size(width, height, scale)
        scene = self.figure.scene()
        painter = QPainter(image)
        try:
            self.paint_scene(painter, scene)
        finally:
            painter.end()
        self.figure.set_size(self.width(), self.height(), self.devicePixelRatioF())
        return image

    def _draw_text(self, painter: QPainter, text: Text) -> None:
        font = self.role_font(text.role)
        raised = self.role_font(TextRole.TICK_MINOR)
        metrics = self._font_metrics(text.role)
        raised_metrics = self._font_metrics(TextRole.TICK_MINOR)
        base = text.base or text.text
        base_width = metrics.horizontalAdvance(base)
        width = base_width + (raised_metrics.horizontalAdvance(text.exponent)
                              if text.exponent else 0.0)
        height = metrics.height()

        def offset(align: Align, extent: float) -> float:
            return {Align.START: 0.0, Align.MIDDLE: -extent / 2.0, Align.END: -extent}[align]

        painter.save()
        painter.translate(QPointF(text.x, text.y))
        if text.vertical:
            painter.rotate(-90.0)
        left = offset(text.across, width)
        top = offset(text.down, height)
        painter.setPen(_qcolor(text.colour))
        painter.setFont(font)
        painter.drawText(QPointF(left, top + metrics.ascent()), base)
        if text.exponent:
            painter.setFont(raised)
            painter.drawText(
                QPointF(left + base_width,
                        top + metrics.ascent() - self.figure.style["exponentRaise"]),
                text.exponent)
        painter.restore()

    def paint_scene(self, painter: QPainter, scene: Scene) -> None:
        """Paint `scene`: all a host other than this widget needs to draw a
        figure with QPainter."""
        style = self.figure.style
        painter.setRenderHint(QPainter.RenderHint.Antialiasing, True)
        painter.fillRect(QRectF(0, 0, scene.width, scene.height), _qcolor(scene.ground))

        def stroke(segments) -> None:
            for s in segments:
                pen = QPen(_qcolor((s["red"], s["green"], s["blue"], s["alpha"])), float(s["width"]))
                pen.setCapStyle(Qt.PenCapStyle.FlatCap)
                painter.setPen(pen)
                painter.drawLine(QPointF(s["x0"], s["y0"]), QPointF(s["x1"], s["y1"]))

        painter.setRenderHint(QPainter.RenderHint.Antialiasing, False)
        stroke(scene.rules)

        area = QRectF(scene.area.x, scene.area.y, scene.area.width, scene.area.height)
        painter.save()
        painter.setClipRect(area)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing, True)
        for line in scene.lines:
            pen = QPen(_qcolor(line.colour), line.width)
            pen.setJoinStyle(Qt.PenJoinStyle.RoundJoin)
            pen.setCapStyle(Qt.PenCapStyle.RoundCap)
            painter.setPen(pen)
            painter.setBrush(Qt.BrushStyle.NoBrush)
            for run in line.runs:
                if len(run) == 1:
                    painter.drawPoint(QPointF(run[0, 0], run[0, 1]))
                    continue
                painter.drawPolyline(QPolygonF([QPointF(x, y) for x, y in run]))
            if len(line.markers):
                painter.setPen(Qt.PenStyle.NoPen)
                painter.setBrush(_qcolor(line.colour))
                for x, y in line.markers:
                    painter.drawEllipse(QPointF(x, y), line.marker_radius, line.marker_radius)
                painter.setBrush(Qt.BrushStyle.NoBrush)
        painter.restore()

        painter.setRenderHint(QPainter.RenderHint.Antialiasing, False)
        stroke(scene.strokes)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing, True)
        for text in scene.texts:
            self._draw_text(painter, text)

        chrome = scene.chrome
        ink = chrome.ink
        if scene.legend and scene.legend_rect is not None:
            box = scene.legend_rect
            ground = _qcolor(scene.ground)
            ground.setAlphaF(0.85)
            painter.setPen(QPen(_qcolor(chrome.axis_rule), style["ruleWidth"]))
            painter.setBrush(ground)
            painter.drawRect(QRectF(box.x, box.y, box.width, box.height))
            painter.setBrush(Qt.BrushStyle.NoBrush)
            for entry in scene.legend:
                painter.setPen(QPen(_qcolor(entry.colour), 2.0 * style["lineWidth"]))
                x0, y0, x1, y1 = entry.swatch
                painter.drawLine(QPointF(x0, y0), QPointF(x1, y1))
                self._draw_text(painter, Text(entry.text_at[0], entry.text_at[1], Align.START,
                                              Align.MIDDLE, TextRole.LEGEND, False, ink,
                                              entry.name, entry.name, ""))

        reading = scene.reading
        if reading.valid:
            painter.setPen(QPen(_qcolor(chrome.cursor_ink), style["ruleWidth"]))
            painter.drawEllipse(QPointF(reading.px, reading.py), 3.0, 3.0)
            font = self.role_font(TextRole.TICK)
            metrics = self._font_metrics(TextRole.TICK)
            readout = f"x {reading.x_text}   y {reading.y_text}"
            gap = style["gapS"]
            box = QRectF(area.x() + style["gapM"], area.y() + style["gapM"],
                         metrics.horizontalAdvance(readout) + 2 * gap, metrics.height() + 2 * gap)
            ground = _qcolor(scene.ground)
            ground.setAlphaF(0.85)
            painter.fillRect(box, ground)
            painter.setFont(font)
            painter.setPen(_qcolor(ink))
            painter.drawText(QPointF(box.x() + gap, box.y() + gap + metrics.ascent()), readout)

        if scene.band is not None:
            band = scene.band
            fill = _qcolor(chrome.cursor_ink)
            fill.setAlphaF(0.12)
            painter.setPen(QPen(_qcolor(chrome.cursor_ink), style["ruleWidth"],
                                Qt.PenStyle.DashLine))
            painter.setBrush(fill)
            painter.drawRect(QRectF(band.x, band.y, band.width, band.height))
            painter.setBrush(Qt.BrushStyle.NoBrush)

        if scene.message:
            painter.setFont(self.role_font(TextRole.AXIS_NAME))
            painter.setPen(_qcolor(ink))
            gap = style["gapL"]
            painter.drawText(area.adjusted(gap, gap, -gap, -gap),
                             int(Qt.AlignmentFlag.AlignCenter | Qt.TextFlag.TextWordWrap),
                             scene.message)

    # --- events ---------------------------------------------------------------

    def wheelEvent(self, event) -> None:  # noqa: N802
        at = event.position()
        delta = event.angleDelta()
        # Qt reports a vertical wheel with Alt held as a horizontal one.
        angle = delta.y() if delta.y() != 0 else delta.x()
        self.figure.wheel(at.x(), at.y(), angle, _modifiers(event.modifiers()))
        event.accept()
        self.update()
        self.changed.emit()

    def mousePressEvent(self, event) -> None:  # noqa: N802
        button = _button(event.button())
        if button is None:
            super().mousePressEvent(event)
            return
        at = event.position()
        self.figure.press(at.x(), at.y(), button, _modifiers(event.modifiers()))
        event.accept()
        self.update()

    def mouseMoveEvent(self, event) -> None:  # noqa: N802
        at = event.position()
        self.figure.move(at.x(), at.y(), _modifiers(event.modifiers()))
        event.accept()
        self.update()
        if event.buttons() != Qt.MouseButton.NoButton:
            self.changed.emit()

    def mouseReleaseEvent(self, event) -> None:  # noqa: N802
        button = _button(event.button())
        if button is None:
            super().mouseReleaseEvent(event)
            return
        at = event.position()
        self.figure.release(at.x(), at.y(), button, _modifiers(event.modifiers()))
        event.accept()
        self.update()
        self.changed.emit()

    def mouseDoubleClickEvent(self, event) -> None:  # noqa: N802
        button = _button(event.button())
        if button is None:
            super().mouseDoubleClickEvent(event)
            return
        at = event.position()
        self.figure.double_click(at.x(), at.y(), button)
        event.accept()
        self.update()
        self.changed.emit()

    def leaveEvent(self, event) -> None:  # noqa: N802
        self.figure.leave()
        self.update()
        super().leaveEvent(event)

