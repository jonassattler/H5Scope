# SPDX-FileCopyrightText: 2026 Andreas Martin
# SPDX-License-Identifier: GPL-3.0-only

"""PySide widget: C++ folds the line, QPainter strokes the runs."""

from __future__ import annotations

import ctypes
import math

import numpy as np
import shiboken6
from PySide6.QtCore import Qt, QRect
from PySide6.QtGui import (
    QColor,
    QFontMetrics,
    QImage,
    QMouseEvent,
    QPaintEvent,
    QPainter,
    QPen,
    QPolygonF,
    QResizeEvent,
    QWheelEvent,
)
from PySide6.QtWidgets import QMenu, QToolTip, QWidget

from .lib import Plot

# Room for the numbers. The curve is projected into what is left.
_TOP = 8
_RIGHT = 12
_BOTTOM = 28
_OWN_COLUMN = 64


def _label(value: float) -> str:
    if value == 0.0 or abs(value) < 1e-12:
        return "0"
    return format(value, ".6g")


_SUP = "⁰¹²³⁴⁵⁶⁷⁸⁹"


def _raised(exponent: int) -> str:
    sign = "⁻" if exponent < 0 else ""
    return sign + "".join(_SUP[int(digit)] for digit in str(abs(exponent)))


def _log_label(value: float, base: float = 10.0) -> str:
    """10³ at a power, 2×10³ at a numbered multiple of one."""
    if not (value > 0.0) or not (base > 1.0):
        return _label(value)
    fx = math.log(value) / math.log(base)
    decade = abs(fx - round(fx)) < 1e-10
    exponent = int(round(fx) if decade else math.floor(fx))
    power = "e" if abs(base - math.e) < 1e-12 else format(base, ".6g")
    if decade:
        return power + _raised(exponent)
    coefficient = value / (base ** exponent)
    if abs(coefficient - round(coefficient)) < 1e-8:
        coefficient = int(round(coefficient))
        lead = str(coefficient)
    else:
        lead = format(coefficient, ".6g")
    return lead + "×" + power + _raised(exponent)


def _tick_text(tick, x_base: float, y_base: float, x_span: float, y_span: float) -> str:
    # The value is already a power of the axis base. Writing it in base ten
    # turns 16 into 1.6×10¹, which is not the power the tick is standing on.
    # A linear tick is judged by the span on screen. Six figures turn two
    # marks around 100 into the same "100" once the window is narrower than
    # those figures can say.
    if tick.logarithmic:
        return _log_label(tick.value, x_base if tick.axis == 0 else y_base)
    return _label_for(tick.value, x_span if tick.axis == 0 else y_span)


def _trimmed(text: str) -> str:
    """Drop trailing zeros, and the point with them. An exponent keeps its part."""
    at = text.find("e")
    mantissa = text if at < 0 else text[:at]
    if "." not in mantissa:
        return text
    mantissa = mantissa.rstrip("0").rstrip(".")
    if at < 0:
        return mantissa
    exponent = text[at + 1 :]
    sign = ""
    if exponent[:1] in "+-":
        sign = exponent[0]
        exponent = exponent[1:]
    return mantissa + "e" + sign + (exponent.lstrip("0") or "0")


def _label_for(value: float, span: float) -> str:
    """A linear tick: enough figures to tell it from the next, judged by the span."""
    width = abs(span)
    # A padded axis starts a hair below zero. That hair formats as "-0"
    # once the other ticks are whole numbers, and "-0" is not a reading.
    if width > 0.0 and abs(value) <= width * 1e-9:
        value = 0.0
    if width >= 1e6 or (0.0 < width < 1e-4):
        if value == 0.0:
            return "0"
        figures = 1
        if value != 0.0 and width > 0.0:
            figures = math.ceil(math.log10(abs(value) / width)) + 2
        figures = max(1, min(15, figures))
        return _trimmed(format(value, f".{figures}e"))
    places = 0
    if width > 0.0:
        places = max(0, min(6, math.ceil(-math.log10(width)) + 2))
    # The zeros stay. They are the place the next tick differs in, and
    # taking them off made the round one a coarser number than the ticks
    # beside it: 24.999, 25, 25.001.
    text = format(value, f".{places}f")
    if text.startswith("-") and float(text) == 0.0:
        text = text[1:]
    return text


def _log_label_for(value: float) -> str:
    """A value between logarithmic ticks: three figures, zeros taken off."""
    if not (value > 0.0) or not math.isfinite(value):
        return _label(value)
    exponent = math.floor(math.log10(value))
    if exponent >= 6 or exponent < -4:
        return _trimmed(format(value, ".2e"))
    places = max(0, min(8, 2 - exponent))
    return _trimmed(format(value, f".{places}f"))


def _axis_number(value: float, low: float, high: float, logarithmic: bool) -> str:
    if logarithmic:
        return _log_label_for(value)
    return _label_for(value, high - low)


def _miss(text: str, value: float) -> float:
    try:
        return abs(float(text) - value)
    except ValueError:
        return math.inf


def _reading_number(value: float, span: float, logarithmic: bool) -> str:
    """The sample, at least as fine as the ticks around it.

    Six figures turn 100.00002 into "100" once the window is narrower than
    that. On a wide window the sample keeps its own figures, so 1.25 does
    not become the "1" a tick at that scale would be.
    """
    fine = _label_for(value, span)
    coarse = _log_label_for(value) if logarithmic else _label(value)
    return fine if _miss(fine, value) < _miss(coarse, value) else coarse


def _y_span(plot, line: int) -> float:
    if plot.own_axis(line):
        low, high = plot.line_y_range(line)
        return high - low
    return plot.view_max_y() - plot.view_min_y()


# The air a band's number keeps from the rectangle, the pane's edge and the
# pointer. One distance, because they are one rule.
_CLEAR = 6
_POINTER = 16


def _clears(a, b) -> bool:
    return (a[0] + a[2] + _CLEAR <= b[0] or b[0] + b[2] + _CLEAR <= a[0]
            or a[1] + a[3] + _CLEAR <= b[1] or b[1] + b[3] + _CLEAR <= a[1])


def _inside(box, pane_w: float, pane_h: float) -> bool:
    return (box[0] >= 0 and box[1] >= 0
            and box[0] + box[2] <= pane_w and box[1] + box[3] <= pane_h)


def _place_corner(cx, cy, away_x, away_y, tw, th, pane_w, pane_h, avoid):
    """Push a corner's number off the band, and off the pointer when that is where it would land."""
    out_x = _POINTER + _CLEAR if away_x > 0 and away_y > 0 else _CLEAR
    want_x = cx - _CLEAR - tw if away_x < 0 else cx + out_x
    want_y = cy - _CLEAR - th if away_y < 0 else cy + _CLEAR
    back_x = cx + _CLEAR if away_x < 0 else cx - _CLEAR - tw
    back_y = cy + _CLEAR if away_y < 0 else cy - _CLEAR - th

    def clamp(value, size, extent):
        return max(0.0, min(extent - size, value))

    def at(x, y):
        return (x, y, tw, th)

    tries = (
        at(clamp(want_x, tw, pane_w), want_y),
        at(want_x, clamp(want_y, th, pane_h)),
        at(clamp(want_x, tw, pane_w), back_y),
        at(back_x, clamp(want_y, th, pane_h)),
    )
    for box in tries:
        if _inside(box, pane_w, pane_h) and all(_clears(box, other) for other in avoid):
            return box
    return at(clamp(want_x, tw, pane_w), clamp(want_y, th, pane_h))


def _place_beside(box, pane_w, pane_h, avoid):
    if _inside(box, pane_w, pane_h) and all(_clears(box, other) for other in avoid):
        return box
    return None


def _mark_samples(on_pane: int, width: int) -> bool:
    """A sample on the pane gets a mark once it has room of its own.

    The run keeps the samples off either side, so the stroke can enter
    the pane. Counting those says the samples are packed when the ones
    on the pane are a screen apart, and the marks never appear.
    """
    return on_pane > 0 and width > 0 and on_pane * 8 <= width


def _polygon(xy, first: int, count: int, scale: float) -> QPolygonF:
    """Copy a run straight into a polygon, in the image's own pixels.

    A Python QPointF per sample costs more than the fold. A scale on the
    painter costs more than that: stroking an envelope through one takes
    hundreds of milliseconds, and the same stroke into an unscaled image does
    not.
    """
    poly = QPolygonF()
    if count <= 0:
        return poly
    poly.resize(count)
    block = np.ascontiguousarray(xy[first:first + count], dtype=np.float64)
    if scale != 1.0:
        block = block * scale
    ptr = shiboken6.getCppPointer(poly.data())[0]
    ctypes.memmove(ptr, int(block.ctypes.data), count * 16)
    return poly


class PlotWidget(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self._plot = Plot()
        self._xy = None
        self._runs = []
        self._ticks = []
        self._drag = None
        self._band = None
        self._shift = None
        self._reading = None
        self._legend_tips = []
        self._legend_rect = None
        self._x_label = ""
        self._y_label = ""
        self._stroke = QImage()
        self.setMinimumSize(240, 160)
        self.setMouseTracking(True)

    def _columns(self) -> tuple[int, int]:
        """Own-axis columns, then whether the common y axis still has a column."""
        own = self._plot.own_count()
        lines = self._plot.line_count()
        common = 0 if lines > 0 and own >= lines else 1
        return own, common

    def _name_extent(self) -> int:
        return QFontMetrics(self.font()).height() + 4

    def _draw_turned(self, painter: QPainter, text: str, x: int, top: int, height: int,
                     colour: QColor, metrics: QFontMetrics) -> None:
        extent = metrics.height() + 4
        shown = metrics.elidedText(text, Qt.TextElideMode.ElideRight, height)
        band = QRect(x, top, extent, height)
        painter.save()
        painter.translate(band.center())
        painter.rotate(-90)
        painter.setPen(colour)
        painter.drawText(QRect(-height // 2, -extent // 2, height, extent),
                         Qt.AlignmentFlag.AlignCenter, shown)
        painter.restore()
        if shown != text:
            self._legend_tips.append((band, text))

    def _y_layout(self):
        """Columns from the outside in: each own axis, then the common one.

        A name sits outside that axis's numbers, and only when there is one
        to write. The common name is absent once every line has left that
        axis. An own axis takes the curve's name: the column is that curve.
        """
        extent = self._name_extent()
        columns = []
        x = 0
        for index in range(self._plot.line_count()):
            if not self._plot.own_axis(index):
                continue
            name = self._plot.line_name(index)
            name_w = extent if name else 0
            columns.append({
                "kind": "own",
                "series": index,
                "name": name,
                "colour": self._plot.line_colour(index),
                "name_x": x,
                "num_x": x + name_w,
            })
            x += name_w + _OWN_COLUMN
        _own, common = self._columns()
        if common:
            name = self._y_label or None
            name_w = extent if name else 0
            columns.append({
                "kind": "common",
                "series": None,
                "name": name,
                "colour": None,
                "name_x": x,
                "num_x": x + name_w,
            })
            x += name_w + _OWN_COLUMN
        return columns, x

    def _pane(self) -> tuple[int, int, int, int]:
        _columns, left = self._y_layout()
        bottom = _BOTTOM + (self._name_extent() if self._x_label else 0)
        width = max(1, self.width() - left - _RIGHT)
        height = max(1, self.height() - _TOP - bottom)
        return left, _TOP, width, height

    def set_x(self, x):
        self._plot.set_x(x)
        self._plot.reset_view()
        self._reproject()

    def set_x_hdf5(self, path, dataset: str):
        self._plot.set_x_hdf5(path, dataset)
        self._plot.reset_view()
        self._reproject()

    def set_line_x(self, index, x):
        self._plot.set_line_x(index, x)
        self._plot.reset_view()
        self._reproject()

    def set_line_x_hdf5(self, index, path, dataset: str):
        self._plot.set_line_x_hdf5(index, path, dataset)
        self._plot.reset_view()
        self._reproject()

    def add_line(self, y, colour=None, name=None):
        index = self._plot.add_line(y, colour, name)
        self._plot.reset_view()
        self._reproject()
        return index

    def add_hdf5(self, path, dataset: str, colour=None, name=None):
        self._plot.add_hdf5(path, dataset, colour, name)
        self._plot.reset_view()
        self._reproject()

    def stack_lines(self) -> None:
        self._plot.stack_lines()
        self._reproject()

    def set_y_log(self, on: bool):
        self._plot.set_y_log(on)
        self._reproject()

    def set_x_log(self, on: bool):
        self._plot.set_x_log(on)
        self._reproject()

    def set_x_log_base(self, base: float):
        self._plot.set_x_log_base(base)
        self._reproject()

    def set_y_log_base(self, base: float):
        self._plot.set_y_log_base(base)
        self._reproject()

    def set_range(self, x0=None, x1=None, y0=None, y1=None):
        self._plot.set_range(x0, x1, y0, y1)
        self._reproject()

    def set_x_label(self, text: str):
        self._x_label = str(text).strip() if text else ""
        self._reproject()

    def set_y_label(self, text: str):
        self._y_label = str(text).strip() if text else ""
        self._reproject()

    def show_lines(self, fill) -> None:
        """Replace every line, then draw once.

        ``fill(plot)`` writes the lines and returns ``(x label, y label)``.
        ``add_line`` and ``set_line_x`` reset the view and repaint on every
        call, which is one picture per channel when a selection changes.
        """
        labels = fill(self._plot)
        x_label, y_label = ("", "") if not labels else labels
        self._plot.reset_view()
        self._x_label = str(x_label).strip() if x_label else ""
        self._y_label = str(y_label).strip() if y_label else ""
        self._reproject()

    def _reproject(self):
        _left, _top, width, height = self._pane()
        ratio = float(self.devicePixelRatioF())
        self._plot.set_pane(width, height, ratio)
        self._xy, self._runs, self._ticks = self._plot.project()
        self.update()

    def paintEvent(self, event: QPaintEvent):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing, True)
        painter.fillRect(self.rect(), QColor(0, 0, 0))
        left, top, width, height = self._pane()
        area = QRect(left, top, width, height)
        rule = QColor(40, 40, 40)
        ink = QColor(220, 220, 220)
        painter.setPen(QPen(rule, 1.0))
        columns, _margin = self._y_layout()
        by_series = {column["series"]: column for column in columns if column["kind"] == "own"}
        for tick in self._ticks:
            if tick.axis == 2:
                continue
            # A minor on a logarithmic axis is a mark on the spine. A full
            # rule at every multiple of a power hatches the pane.
            minor = tick.logarithmic and not tick.labeled
            if tick.axis == 0:
                x = left + int(round(tick.x))
                if minor:
                    painter.drawLine(x, top + height - 6, x, top + height)
                else:
                    painter.drawLine(x, top, x, top + height)
            else:
                y = top + int(round(tick.y))
                if minor:
                    painter.drawLine(left, y, left + 6, y)
                else:
                    painter.drawLine(left, y, left + width, y)
        # An own axis is a second scale. Its rules do not cross the pane:
        # they would meet the other curves at values those curves do not have.
        # The spine is the right edge of that curve's column, in its colour,
        # and the marks sit in the gap the numbers already leave. A one-pixel
        # stroke with smoothing on lands half on each of two columns, so the
        # colour the curve was drawn in is not the colour on the spine.
        painter.setRenderHint(QPainter.RenderHint.Antialiasing, False)
        for column in by_series.values():
            spine = column["num_x"] + _OWN_COLUMN - 1
            painter.setPen(QPen(QColor(*column["colour"]), 1.0))
            painter.drawLine(spine, top, spine, top + height)
        for tick in self._ticks:
            if tick.axis != 2 or tick.series not in by_series:
                continue
            column = by_series[tick.series]
            spine = column["num_x"] + _OWN_COLUMN - 1
            y = top + int(round(tick.y))
            mark = 3 if tick.logarithmic and not tick.labeled else 5
            painter.setPen(QPen(QColor(tick.red, tick.green, tick.blue), 1.0))
            painter.drawLine(spine - mark, y, spine, y)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing, True)
        painter.setPen(QPen(ink, 1.0))
        painter.drawRect(area.adjusted(0, 0, -1, -1))
        common_col = next((column for column in columns if column["kind"] == "common"), None)
        for tick in self._ticks:
            if not tick.labeled:
                continue
            text = _tick_text(
                tick, self._plot.x_log_base(), self._plot.y_log_base(),
                self._plot.view_max_x() - self._plot.view_min_x(),
                self._plot.view_max_y() - self._plot.view_min_y(),
            )
            if tick.axis == 0:
                x = left + int(round(tick.x))
                painter.drawText(QRect(x - 48, top + height + 2, 96, _BOTTOM - 4),
                                 Qt.AlignmentFlag.AlignHCenter | Qt.AlignmentFlag.AlignTop, text)
            elif tick.axis == 1 and common_col is not None:
                y = top + int(round(tick.y))
                painter.drawText(QRect(common_col["num_x"], y - 8, _OWN_COLUMN - 6, 16),
                                 Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter, text)
            elif tick.axis == 2 and tick.series in by_series:
                y = top + int(round(tick.y))
                column = by_series[tick.series]
                painter.setPen(QColor(tick.red, tick.green, tick.blue))
                painter.drawText(QRect(column["num_x"], y - 8, _OWN_COLUMN - 6, 16),
                                 Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter, text)
                painter.setPen(QPen(ink, 1.0))
        if self._xy is not None and len(self._runs) != 0:
            painter.drawImage(QRect(left, top, width, height), self._curves(width, height))
        self._paint_legend(painter, left, top, width, height, ink)
        self._paint_axis_names(painter, left, top, width, height, ink)
        if self._band is not None:
            start, end = self._band
            band = QRect(int(min(start.x(), end.x())), int(min(start.y(), end.y())),
                         int(abs(end.x() - start.x())), int(abs(end.y() - start.y()))).intersected(area)
            painter.setPen(QPen(ink, 1.0))
            painter.setBrush(QColor(ink.red(), ink.green(), ink.blue(), 48))
            painter.drawRect(band)
            self._paint_band_readout(painter, left, top, width, height, ink)
        if self._reading is not None:
            reading = self._reading
            hx = left + reading.px
            hy = top + reading.py
            painter.setPen(QPen(QColor(70, 70, 70), 1.0))
            painter.drawLine(left, int(round(hy)), left + width, int(round(hy)))
            painter.drawLine(int(round(hx)), top, int(round(hx)), top + height)
            painter.setPen(QColor(reading.red, reading.green, reading.blue))
            painter.drawLine(int(round(hx)) - 4, int(round(hy)), int(round(hx)) + 4, int(round(hy)))
            painter.drawLine(int(round(hx)), int(round(hy)) - 4, int(round(hx)), int(round(hy)) + 4)
            painter.setPen(ink)
            name = self._plot.line_name(reading.line)
            parts = [
                _reading_number(reading.x, self._plot.view_max_x() - self._plot.view_min_x(),
                                self._plot.x_log()),
                _reading_number(reading.y, _y_span(self._plot, reading.line),
                                self._plot.line_y_log(reading.line)),
            ]
            if name and self._plot.line_count() > 1:
                parts.insert(0, name)
            read_right = left + width - 8
            if self._legend_rect is not None:
                read_right = min(read_right, self._legend_rect.left() - 8)
            painter.drawText(QRect(left + 8, top + 4, max(1, read_right - left - 8), 18),
                             Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignVCenter,
                             "    ".join(parts))

    def _paint_legend(self, painter: QPainter, left: int, top: int, width: int, height: int,
                       ink: QColor) -> None:
        # A caption on the picture, not a control. The swatch is the colour the
        # line was drawn in. Past twelve names it says how many are left,
        # because a list that silently stopped would be wrong about the picture.
        named = self._plot.named_lines()
        self._legend_tips = []
        self._legend_rect = None
        if not named or width <= 0 or height <= 0:
            return
        shown = named[:12]
        extra = len(named) - len(shown)
        metrics = QFontMetrics(painter.font())
        swatch = 14
        gap = 6
        pad = 8
        row_h = metrics.height() + 2
        labels = [name for _index, name, _colour in shown]
        if extra:
            labels.append(f"+{extra} more")
        longest = max(metrics.horizontalAdvance(text) for text in labels)
        box_w = min(pad * 2 + swatch + gap + longest, max(1, width // 2))
        text_w = max(1, box_w - pad * 2 - swatch - gap)
        rows = len(shown) + (1 if extra else 0)
        box_h = pad * 2 + rows * row_h
        box_x = left + width - box_w - 8
        box_y = top + 8
        if box_x < left:
            box_x = left
        self._legend_rect = QRect(box_x, box_y, box_w, box_h)
        painter.fillRect(QRect(box_x, box_y, box_w, box_h), QColor(16, 16, 16, 230))
        painter.setPen(QPen(QColor(70, 70, 70), 1.0))
        painter.setBrush(Qt.BrushStyle.NoBrush)
        painter.drawRect(QRect(box_x, box_y, box_w, box_h).adjusted(0, 0, -1, -1))
        for row, (index, name, colour) in enumerate(shown):
            y = box_y + pad + row * row_h
            painter.fillRect(QRect(box_x + pad, y + (row_h - 8) // 2, swatch, 8),
                             QColor(*colour))
            shown_text = metrics.elidedText(name, Qt.TextElideMode.ElideMiddle, text_w)
            painter.setPen(ink)
            text_rect = QRect(box_x + pad + swatch + gap, y, text_w, row_h)
            painter.drawText(text_rect, Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignVCenter,
                             shown_text)
            if shown_text != name:
                self._legend_tips.append((text_rect, name))
        if extra:
            y = box_y + pad + len(shown) * row_h
            painter.setPen(QColor(140, 140, 140))
            painter.drawText(QRect(box_x + pad, y, box_w - pad * 2, row_h),
                             Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignVCenter,
                             f"+{extra} more")

    def _paint_axis_names(self, painter: QPainter, left: int, top: int, width: int, height: int,
                           ink: QColor) -> None:
        # Under the numbers, and up the side outside every y column. A name
        # that does not fit keeps the part that does and shows the rest when
        # the pointer is on it. The common y name is absent once every line
        # has left that axis: there is no column left for it to name. An own
        # axis takes the curve's name, in the colour the curve is drawn.
        metrics = QFontMetrics(painter.font())
        extent = metrics.height() + 4
        if self._x_label and width > 0:
            shown = metrics.elidedText(self._x_label, Qt.TextElideMode.ElideRight, width)
            rect = QRect(left, top + height + _BOTTOM, width, extent)
            painter.setPen(ink)
            painter.drawText(rect, Qt.AlignmentFlag.AlignHCenter | Qt.AlignmentFlag.AlignVCenter,
                             shown)
            if shown != self._x_label:
                self._legend_tips.append((rect, self._x_label))
        if height <= 0:
            return
        for column in self._y_layout()[0]:
            if not column["name"]:
                continue
            colour = ink if column["colour"] is None else QColor(*column["colour"])
            self._draw_turned(painter, column["name"], column["name_x"], top, height, colour, metrics)

    def _paint_band_readout(self, painter: QPainter, left: int, top: int, width: int,
                             height: int, ink: QColor) -> None:
        # The corners are asked of the same mapping the zoom uses on release,
        # and written the way the ticks beside them are written. A second copy
        # of either would be a number that can disagree with the window it names.
        if width <= 0 or height <= 0 or self._band is None:
            return
        start, end = self._band

        def pane(pos):
            return (min(max(pos.x() - left, 0.0), float(width)),
                    min(max(pos.y() - top, 0.0), float(height)))

        sx, sy = pane(start)
        ex, ey = pane(end)
        x_log = any(tick.axis == 0 and tick.logarithmic for tick in self._ticks)
        y_log = any(tick.axis == 1 and tick.logarithmic for tick in self._ticks)
        view_x0 = self._plot.view_min_x()
        view_x1 = self._plot.view_max_x()
        view_y0 = self._plot.view_min_y()
        view_y1 = self._plot.view_max_y()
        common_y = self._plot.line_count() == 0 or self._plot.own_count() < self._plot.line_count()

        def nx(value: float) -> str:
            return _axis_number(value, view_x0, view_x1, x_log)

        def ny(value: float) -> str:
            return _axis_number(value, view_y0, view_y1, y_log)

        start_x = self._plot.data_x_at(sx)
        end_x = self._plot.data_x_at(ex)
        if common_y:
            start_text = f"{nx(start_x)}, {ny(self._plot.data_y_at(sy))}"
            end_text = f"{nx(end_x)}, {ny(self._plot.data_y_at(ey))}"
            height_text = "∆" + ny(abs(self._plot.data_y_at(ey) - self._plot.data_y_at(sy)))
        else:
            start_text = nx(start_x)
            end_text = nx(end_x)
            height_text = ""
        width_text = "∆" + nx(abs(end_x - start_x))

        metrics = QFontMetrics(painter.font())

        def box_size(text: str):
            return metrics.horizontalAdvance(text) + 8, metrics.height() + 4

        band = (min(sx, ex), min(sy, ey), abs(ex - sx), abs(ey - sy))
        pointer = (ex, ey, float(_POINTER), float(_POINTER))
        runs_right = 1 if ex >= sx else -1
        runs_down = 1 if ey >= sy else -1
        start_w, start_h = box_size(start_text)
        end_w, end_h = box_size(end_text)
        start_box = _place_corner(sx, sy, -runs_right, -runs_down, start_w, start_h,
                                   width, height, [band, pointer])
        end_box = _place_corner(ex, ey, runs_right, runs_down, end_w, end_h,
                                 width, height, [band, pointer])
        labels = [(start_box, start_text), (end_box, end_text)]
        occupied = [band, pointer, start_box, end_box]

        width_w, width_h = box_size(width_text)
        if width_w <= band[2]:
            centered = band[0] + (band[2] - width_w) / 2.0
            above = (centered, band[1] - _CLEAR - width_h, width_w, width_h)
            below = (centered, band[1] + band[3] + _CLEAR, width_w, width_h)
            placed = _place_beside(above, width, height, occupied) or _place_beside(
                below, width, height, occupied)
            if placed is not None:
                labels.append((placed, width_text))
                occupied.append(placed)
        if height_text:
            height_w, height_h = box_size(height_text)
            if height_h <= band[3]:
                centered = band[1] + (band[3] - height_h) / 2.0
                west = (band[0] - _CLEAR - height_w, centered, height_w, height_h)
                east = (band[0] + band[2] + _CLEAR, centered, height_w, height_h)
                placed = _place_beside(west, width, height, occupied) or _place_beside(
                    east, width, height, occupied)
                if placed is not None:
                    labels.append((placed, height_text))

        painter.setBrush(Qt.BrushStyle.NoBrush)
        for box, text in labels:
            rect = QRect(left + int(round(box[0])), top + int(round(box[1])),
                         int(round(box[2])), int(round(box[3])))
            painter.fillRect(rect, QColor(0, 0, 0, 210))
            painter.setPen(ink)
            painter.drawText(rect, Qt.AlignmentFlag.AlignCenter, text)

    def _curves(self, width: int, height: int) -> QImage:
        # Stroke in device pixels, with an integer pen width and no painter
        # transform. A whole-line envelope is thousands of segments. Qt's
        # raster engine draws that in a few milliseconds on a plain image and
        # in hundreds when the painter carries a scale — a device-pixel-ratio
        # on the image installs one — or when the pen width is not a whole
        # number of pixels. The finished image is copied into the pane.
        ratio = float(self.devicePixelRatioF())
        if ratio <= 0.0:
            ratio = 1.0
        pixels_w = max(1, int(round(width * ratio)))
        pixels_h = max(1, int(round(height * ratio)))
        image = self._stroke
        if image.width() != pixels_w or image.height() != pixels_h:
            image = QImage(pixels_w, pixels_h, QImage.Format.Format_ARGB32_Premultiplied)
            self._stroke = image
        image.fill(0)
        stroke = QPainter(image)
        for run in self._runs:
            colour = QColor(run.red, run.green, run.blue, run.alpha)
            dense = run.count > width * 1.5
            stroke.setRenderHint(QPainter.RenderHint.Antialiasing, not dense)
            stroke.setPen(QPen(colour, max(1, int(round(run.width * ratio))), Qt.PenStyle.SolidLine,
                               Qt.PenCapStyle.FlatCap,
                               Qt.PenJoinStyle.MiterJoin if dense else Qt.PenJoinStyle.RoundJoin))
            stroke.drawPolyline(_polygon(self._xy, run.first, run.count, ratio))
            pts = self._xy[run.first:run.first + run.count]
            on_pane = int(np.count_nonzero((pts[:, 0] >= 0.0) & (pts[:, 0] <= width)))
            # A summary stays a summary when a logarithmic axis drops the
            # values it cannot draw. Fewer vertices is not a closer look.
            if not run.summarised and _mark_samples(on_pane, width):
                # The stations off the pane are how the stroke enters it.
                # A mark belongs on a sample the pane is showing.
                stroke.setRenderHint(QPainter.RenderHint.Antialiasing, True)
                stroke.setBrush(colour)
                stroke.setPen(Qt.PenStyle.NoPen)
                radius = max(2, int(round(3 * ratio)))
                for px, py in pts:
                    if px < -4.0 or px > width + 4.0 or py < -4.0 or py > height + 4.0:
                        continue
                    x = int(round(float(px) * ratio))
                    y = int(round(float(py) * ratio))
                    stroke.drawEllipse(x - radius, y - radius, radius * 2, radius * 2)
        stroke.end()
        return image

    def resizeEvent(self, event: QResizeEvent):
        super().resizeEvent(event)
        self._reproject()

    def wheelEvent(self, event: QWheelEvent):
        turned = event.angleDelta().y() or event.angleDelta().x()
        if turned == 0:
            return
        factor = 1.25 ** (turned / 120.0)
        mods = event.modifiers()
        left, top, _width, _height = self._pane()
        # The flags are raw. h5plot_wheel decides: Shift is x, Ctrl is y,
        # and Alt with Ctrl scales only the curve under the pointer in y.
        self._plot.wheel(event.position().x() - left, event.position().y() - top, factor,
                         bool(mods & Qt.KeyboardModifier.ShiftModifier),
                         bool(mods & Qt.KeyboardModifier.ControlModifier),
                         bool(mods & Qt.KeyboardModifier.AltModifier))
        self._reproject()

    def _pane_point(self, pos):
        left, top, _width, _height = self._pane()
        return pos.x() - left, pos.y() - top

    def mousePressEvent(self, event: QMouseEvent):
        alt = bool(event.modifiers() & Qt.KeyboardModifier.AltModifier)
        if (event.button() == Qt.MouseButton.MiddleButton or
                (event.button() == Qt.MouseButton.LeftButton and alt)) and self._band is None:
            index = self._plot.nearest(*self._pane_point(event.position()))
            if index >= 0:
                self._reading = None
                self._shift = (index, event.position())
                self.setCursor(Qt.CursorShape.SizeAllCursor)
            return
        if event.button() == Qt.MouseButton.RightButton and self._drag is None and self._shift is None:
            self._reading = None
            self._band = (event.position(), event.position())
            self.setCursor(Qt.CursorShape.CrossCursor)
            self.update()
            return
        ctrl = bool(event.modifiers() & Qt.KeyboardModifier.ControlModifier)
        if event.button() == Qt.MouseButton.LeftButton and ctrl and not alt and self._band is None:
            # A double-click puts the window back and leaves the shift and the
            # y scale. This is the way back for the curve: its axis goes, and
            # so do the offset and the scale, because the common numbers would
            # otherwise name a place it has left.
            index = self._plot.nearest(*self._pane_point(event.position()))
            if index >= 0 and self._plot.own_axis(index):
                self._reading = None
                self._plot.set_own_axis(index, False)
                self._reproject()
            return
        if event.button() == Qt.MouseButton.LeftButton and self._band is None and self._shift is None:
            self._reading = None
            self._drag = event.position()
            self.setCursor(Qt.CursorShape.ClosedHandCursor)

    def _read(self, pos) -> None:
        px, py = self._pane_point(pos)
        self._reading = self._plot.sample(px, py)
        self.update()

    def _legend_tip(self, event: QMouseEvent) -> None:
        pos = event.position().toPoint()
        for rect, name in self._legend_tips:
            if rect.contains(pos):
                QToolTip.showText(event.globalPosition().toPoint(), name, self)
                return
        QToolTip.hideText()

    def leaveEvent(self, event):
        self._reading = None
        QToolTip.hideText()
        self.update()
        super().leaveEvent(event)

    def mouseMoveEvent(self, event: QMouseEvent):
        if self._shift is not None:
            index, last = self._shift
            pos = event.position()
            self._plot.shift_line(index, 0.0, pos.y() - last.y())
            self._shift = (index, pos)
            self._reproject()
            return
        if self._band is not None:
            self._band = (self._band[0], event.position())
            self.update()
            return
        if self._drag is None:
            self._read(event.position())
            self._legend_tip(event)
            return
        self._reading = None
        pos = event.position()
        self._plot.pan(pos.x() - self._drag.x(), pos.y() - self._drag.y())
        self._drag = pos
        self._reproject()

    def mouseReleaseEvent(self, event: QMouseEvent):
        if self._shift is not None and event.button() in (
                Qt.MouseButton.MiddleButton, Qt.MouseButton.LeftButton):
            self._shift = None
            self.unsetCursor()
            return
        if event.button() == Qt.MouseButton.RightButton and self._band is not None:
            start, end = self._band
            self._band = None
            self.unsetCursor()
            left, top, _width, _height = self._pane()
            moved = self._plot.zoom_rect(start.x() - left, start.y() - top, end.x() - left,
                                         end.y() - top)
            if moved:
                self._reproject()
            else:
                # A drag opened a window. A click did not: under twelve pixels
                # on either side zoom_rect refuses it. That click is the
                # axis menu. One in the pane is kept for later and opens nothing.
                self._axis_menu(start)
                self.update()
            return
        if event.button() == Qt.MouseButton.LeftButton:
            self._drag = None
            self.unsetCursor()

    def _own_column_at(self, pos) -> int | None:
        left, top, _width, height = self._pane()
        if pos.y() < top or pos.y() > top + height or pos.x() >= left:
            return None
        for column in self._y_layout()[0]:
            if column["kind"] != "own":
                continue
            right = column["num_x"] + _OWN_COLUMN
            if column["name_x"] <= pos.x() < right:
                return column["series"]
        return None

    def _axis_menu(self, pos) -> None:
        index = self._own_column_at(pos)
        if index is None:
            return
        was = self._plot.line_y_log(index)
        menu = QMenu(self)
        action = menu.addAction("Logarithmisch")
        action.setCheckable(True)
        action.setChecked(was)
        if menu.exec(self.mapToGlobal(pos.toPoint())) is not action:
            return
        self._plot.set_line_y_log(index, not was)
        self._reproject()

    def mouseDoubleClickEvent(self, event: QMouseEvent):
        if event.button() == Qt.MouseButton.LeftButton:
            self._plot.reset_view()
            self._reproject()
