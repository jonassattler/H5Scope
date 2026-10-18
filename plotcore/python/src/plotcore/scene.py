# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-License-Identifier: GPL-3.0-only

"""One frame of a figure, as plain Python and numpy.

Copied out of the C scene in one go, so nothing here refers to memory the
library owns: a Scene outlives the call that made it, and the figure it was
taken from, and can be handed to another thread.
"""

from __future__ import annotations

import ctypes
import enum
from dataclasses import dataclass, field
from typing import List, Optional, Tuple

import numpy as np

from . import _native

Colour = Tuple[float, float, float, float]


class TextRole(enum.IntEnum):
    """The faces text is set in; a host maps each to a font."""

    TICK = 0
    TICK_MINOR = 1
    TITLE = 2
    AXIS_NAME = 3
    LEGEND = 4


class Align(enum.IntEnum):
    START = 0
    MIDDLE = 1
    END = 2


@dataclass(frozen=True)
class Rect:
    x: float
    y: float
    width: float
    height: float


@dataclass(frozen=True)
class Chrome:
    """What everything that is not a line is drawn in."""

    ground: Colour
    ink: Colour
    rule_minor: Colour
    rule_major: Colour
    axis_rule: Colour
    cursor_ink: Colour


@dataclass
class Line:
    """One drawn line. Points are pane pixels with y down; `values` are the
    same points in the data's own units, for a host whose axes are its own."""

    line: int
    colour: Colour
    width: float
    marker_radius: float
    runs: List[np.ndarray]
    values: List[np.ndarray]
    triangles: np.ndarray
    markers: np.ndarray
    y_low: float
    y_high: float
    y_log: bool
    own: bool
    numbered: bool


@dataclass
class Text:
    """A piece of text. `base` is set in `role`'s face and `exponent` raised
    beside it in TICK_MINOR's; `text` is both in one string."""

    x: float
    y: float
    across: Align
    down: Align
    role: TextRole
    vertical: bool
    colour: Colour
    text: str
    base: str
    exponent: str


@dataclass
class LegendEntry:
    line: int
    colour: Colour
    name: str
    swatch: Tuple[float, float, float, float]
    text_at: Tuple[float, float]


@dataclass
class Reading:
    valid: bool = False
    line: int = -1
    x: float = 0.0
    y: float = 0.0
    px: float = 0.0
    py: float = 0.0
    x_text: str = ""
    y_text: str = ""


#: A rule or a stroke: both ends, a colour and a width.
SEGMENT = np.dtype(
    [
        ("x0", "f8"),
        ("y0", "f8"),
        ("x1", "f8"),
        ("y1", "f8"),
        ("red", "f8"),
        ("green", "f8"),
        ("blue", "f8"),
        ("alpha", "f8"),
        ("width", "f8"),
    ]
)


@dataclass
class Scene:
    width: float
    height: float
    ground: Colour
    chrome: Chrome
    area: Rect
    view: Tuple[float, float, float, float]
    message: str
    lines: List[Line] = field(default_factory=list)
    rules: np.ndarray = field(default_factory=lambda: np.zeros(0, SEGMENT))
    strokes: np.ndarray = field(default_factory=lambda: np.zeros(0, SEGMENT))
    texts: List[Text] = field(default_factory=list)
    legend: List[LegendEntry] = field(default_factory=list)
    legend_rect: Optional[Rect] = None
    reading: Reading = field(default_factory=Reading)
    band: Optional[Rect] = None


def _colour(rgba: _native.Rgba) -> Colour:
    return (rgba.red, rgba.green, rgba.blue, rgba.alpha)


def _chrome(chrome: _native.Chrome) -> Chrome:
    return Chrome(_colour(chrome.ground), _colour(chrome.ink), _colour(chrome.rule_minor),
                  _colour(chrome.rule_major), _colour(chrome.axis_rule),
                  _colour(chrome.cursor_ink))


def _rect(rect: _native.Rect) -> Rect:
    return Rect(rect.x, rect.y, rect.width, rect.height)


def _text(raw: Optional[bytes]) -> str:
    return raw.decode("utf-8") if raw else ""


def _points(pointer, count: int) -> np.ndarray:
    if count <= 0 or not pointer:
        return np.zeros((0, 2))
    return np.ctypeslib.as_array(pointer, shape=(count * 2,)).reshape(count, 2).copy()


def _segments(pointer, count: int) -> np.ndarray:
    if count <= 0 or not pointer:
        return np.zeros(0, SEGMENT)
    raw = ctypes.string_at(pointer, count * ctypes.sizeof(_native.Segment))
    return np.frombuffer(raw, dtype=SEGMENT).copy()


def copy_scene(handle) -> Scene:
    """Everything in the C scene `handle`, which the caller still frees."""
    lib = _native.lib
    width = ctypes.c_double()
    height = ctypes.c_double()
    lib.plotcore_scene_size(handle, ctypes.byref(width), ctypes.byref(height))
    x0, x1, y0, y1 = (ctypes.c_double() for _ in range(4))
    lib.plotcore_scene_view(handle, ctypes.byref(x0), ctypes.byref(x1), ctypes.byref(y0),
                            ctypes.byref(y1))
    scene = Scene(
        width=width.value,
        height=height.value,
        ground=_colour(lib.plotcore_scene_ground(handle)),
        chrome=_chrome(lib.plotcore_scene_chrome(handle)),
        area=_rect(lib.plotcore_scene_area(handle)),
        view=(x0.value, x1.value, y0.value, y1.value),
        message=_text(lib.plotcore_scene_message(handle)),
    )
    info = _native.LineInfo()
    for index in range(lib.plotcore_scene_line_count(handle)):
        lib.plotcore_scene_line(handle, index, ctypes.byref(info))
        runs = []
        values = []
        for run in range(info.run_count):
            count = lib.plotcore_scene_line_run_length(handle, index, run)
            runs.append(_points(lib.plotcore_scene_line_run(handle, index, run), count))
            values.append(_points(lib.plotcore_scene_line_run_values(handle, index, run), count))
        scene.lines.append(
            Line(
                line=info.line,
                colour=_colour(info.colour),
                width=info.width,
                marker_radius=info.marker_radius,
                runs=runs,
                values=values,
                triangles=_points(lib.plotcore_scene_line_triangles(handle, index),
                                  info.triangle_points),
                markers=_points(lib.plotcore_scene_line_markers(handle, index),
                                info.marker_count),
                y_low=info.y_low,
                y_high=info.y_high,
                y_log=bool(info.y_log),
                own=bool(info.own),
                numbered=bool(info.numbered),
            )
        )
    scene.rules = _segments(lib.plotcore_scene_rules(handle), lib.plotcore_scene_rule_count(handle))
    scene.strokes = _segments(lib.plotcore_scene_strokes(handle),
                              lib.plotcore_scene_stroke_count(handle))
    text = _native.Text()
    for index in range(lib.plotcore_scene_text_count(handle)):
        lib.plotcore_scene_text(handle, index, ctypes.byref(text))
        scene.texts.append(
            Text(
                x=text.x,
                y=text.y,
                across=Align(text.across),
                down=Align(text.down),
                role=TextRole(text.role),
                vertical=bool(text.vertical),
                colour=_colour(text.colour),
                text=_text(text.text),
                base=_text(text.base),
                exponent=_text(text.exponent),
            )
        )
    entry = _native.LegendEntry()
    for index in range(lib.plotcore_scene_legend_count(handle)):
        lib.plotcore_scene_legend(handle, index, ctypes.byref(entry))
        scene.legend.append(
            LegendEntry(
                line=entry.line,
                colour=_colour(entry.colour),
                name=_text(entry.name),
                swatch=(entry.x0, entry.y0, entry.x1, entry.y1),
                text_at=(entry.text_x, entry.text_y),
            )
        )
    if scene.legend:
        scene.legend_rect = _rect(lib.plotcore_scene_legend_rect(handle))
    reading = lib.plotcore_scene_reading(handle)
    scene.reading = Reading(
        valid=bool(reading.valid),
        line=reading.line,
        x=reading.x,
        y=reading.y,
        px=reading.px,
        py=reading.py,
        x_text=_text(reading.x_text),
        y_text=_text(reading.y_text),
    )
    band = _native.Rect()
    if lib.plotcore_scene_band(handle, ctypes.byref(band)):
        scene.band = _rect(band)
    return scene
