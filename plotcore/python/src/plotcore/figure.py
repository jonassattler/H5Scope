# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-FileCopyrightText: 2026 Andreas Martin
# SPDX-License-Identifier: GPL-3.0-only

"""A whole plot, as a Python object.

A Figure is plotcore::Figure across the C interface: lines copied in, settings
named, gestures forwarded, a Scene out. Nothing here decides anything about the
picture -- that is what makes a figure drawn from Python the figure drawn from
C++ or Rust, and tests/test_golden.py holds the three to one digest.
"""

from __future__ import annotations

import ctypes
import enum
import itertools
import threading
from typing import Callable, Dict, Iterator, Optional, Sequence, Tuple

import numpy as np

from . import _native
from .scene import Colour, Scene, TextRole, copy_scene

_lib = _native.lib


class Error(RuntimeError):
    """A call plotcore refused, with the status it gave and why."""

    def __init__(self, status: int, message: str):
        super().__init__(message or f"plotcore status {status}")
        self.status = status


class Button(enum.IntEnum):
    LEFT = 0
    MIDDLE = 1
    RIGHT = 2


class Modifiers(enum.IntFlag):
    NONE = 0
    SHIFT = 1
    CONTROL = 2
    ALT = 4


_APPETITES = {"low": 0, "medium": 1, "greedy": 2}


def _check(status: int) -> None:
    if status != 0:
        message = _lib.plotcore_last_error() or b""
        raise Error(status, message.decode("utf-8", "replace"))


def _copied(call) -> str:
    """A string the C side copies out: asked its length, then asked again."""
    length = ctypes.c_int64()
    _check(call(None, 0, ctypes.byref(length)))
    buffer = ctypes.create_string_buffer(length.value + 1)
    _check(call(buffer, length.value + 1, None))
    return buffer.value.decode("utf-8")


def _encode(text: Optional[str]) -> bytes:
    return (text or "").encode("utf-8")


def _array(values) -> np.ndarray:
    return np.ascontiguousarray(np.asarray(values, dtype=np.float64).ravel())


def _pointer(array: np.ndarray):
    return array.ctypes.data_as(ctypes.POINTER(ctypes.c_double))


def _rgba(colour) -> _native.Rgba:
    parts = tuple(float(c) for c in colour)
    if len(parts) == 3:
        parts = parts + (1.0,)
    if len(parts) != 4:
        raise ValueError(f"a colour is three or four numbers from 0 to 1, not {colour!r}")
    return _native.Rgba(*parts)


def _is_colour(value) -> bool:
    return isinstance(value, (tuple, list)) and len(value) in (3, 4) and all(
        isinstance(c, (int, float)) and not isinstance(c, bool) for c in value
    )


# Readers live here, keyed by the token the C side holds as their context, and
# leave when the C side says it has let them go -- which is the only moment it
# is safe to, and the moment index bookkeeping in Python would get wrong.
_readers: Dict[int, Callable] = {}
_tokens = itertools.count(1)
_readers_lock = threading.Lock()


@_native.FREE_CONTEXT
def _release_reader(context):
    with _readers_lock:
        _readers.pop(context or 0, None)


@_native.READER
def _call_reader(context, first, count, out):
    with _readers_lock:
        reader = _readers.get(context or 0)
    if reader is None:
        return 0
    try:
        values = _array(reader(int(first), int(count)))
    except Exception:  # noqa: BLE001 -- an exception must not cross into C
        return 0
    if values.size != count:
        return 0
    ctypes.memmove(out, values.ctypes.data, int(count) * 8)
    return 1


class _Named:
    """Settings by name, as `figure.settings["xLog"] = True` -- the names a
    saved view writes; see plotcore.h for the list. A value's Python type says
    which kind of setting it is: a bool, a number, a string, or a colour as a
    tuple of three or four numbers from 0 to 1."""

    def __init__(self, figure: "Figure", line: Optional[int] = None):
        self._figure = figure
        self._line = line

    def __setitem__(self, name: str, value) -> None:
        handle = self._figure._handle
        key = _encode(name)
        line = self._line
        if isinstance(value, bool):
            _check(_lib.plotcore_set_bool(handle, key, int(value)) if line is None
                   else _lib.plotcore_set_line_bool(handle, line, key, int(value)))
        elif isinstance(value, (int, float)):
            _check(_lib.plotcore_set_number(handle, key, float(value)) if line is None
                   else _lib.plotcore_set_line_number(handle, line, key, float(value)))
        elif isinstance(value, str):
            _check(_lib.plotcore_set_text(handle, key, _encode(value)) if line is None
                   else _lib.plotcore_set_line_text(handle, line, key, _encode(value)))
        elif _is_colour(value):
            if line is not None:
                raise KeyError("a line's colour is Line.colour")
            _check(_lib.plotcore_set_colour(handle, key, _rgba(value)))
        else:
            raise TypeError(f"{name}: a setting is a bool, a number, a string or a colour")

    def __getitem__(self, name: str):
        handle = self._figure._handle
        key = _encode(name)
        line = self._line
        flag = ctypes.c_int32()
        if (_lib.plotcore_get_bool(handle, key, ctypes.byref(flag)) if line is None
                else _lib.plotcore_get_line_bool(handle, line, key, ctypes.byref(flag))) == 0:
            return bool(flag.value)
        number = ctypes.c_double()
        if (_lib.plotcore_get_number(handle, key, ctypes.byref(number)) if line is None
                else _lib.plotcore_get_line_number(handle, line, key, ctypes.byref(number))) == 0:
            return number.value
        length = ctypes.c_int64()
        if (_lib.plotcore_get_text(handle, key, None, 0, ctypes.byref(length)) if line is None
                else _lib.plotcore_get_line_text(handle, line, key, None, 0,
                                                 ctypes.byref(length))) == 0:
            return _copied(
                (lambda b, c, n: _lib.plotcore_get_text(handle, key, b, c, n)) if line is None
                else (lambda b, c, n: _lib.plotcore_get_line_text(handle, line, key, b, c, n)))
        if line is None:
            colour = _native.Rgba()
            if _lib.plotcore_get_colour(handle, key, ctypes.byref(colour)) == 0:
                return (colour.red, colour.green, colour.blue, colour.alpha)
        raise KeyError(name)


class _Style:
    """The frame's measurements by name, as `figure.style["tickPixels"] = 12`."""

    def __init__(self, figure: "Figure"):
        self._figure = figure

    def __setitem__(self, name: str, value: float) -> None:
        _check(_lib.plotcore_set_style(self._figure._handle, _encode(name), float(value)))

    def __getitem__(self, name: str) -> float:
        number = ctypes.c_double()
        _check(_lib.plotcore_get_number(self._figure._handle, _encode(name), ctypes.byref(number)))
        return number.value


class Line:
    """One line of a figure, by index."""

    def __init__(self, figure: "Figure", index: int):
        self._figure = figure
        self.index = index
        self.settings = _Named(figure, index)

    @property
    def length(self) -> int:
        return int(_lib.plotcore_line_length(self._figure._handle, self.index))

    @property
    def base_bucket(self) -> int:
        """The finest bucket the line is held at; one is every element."""
        return int(_lib.plotcore_line_base_bucket(self._figure._handle, self.index))

    @property
    def extent(self) -> Tuple[float, float]:
        low = ctypes.c_double()
        high = ctypes.c_double()
        _check(_lib.plotcore_line_extent(self._figure._handle, self.index, ctypes.byref(low),
                                         ctypes.byref(high)))
        return (low.value, high.value)

    @property
    def name(self) -> str:
        return _copied(lambda buffer, capacity, length: _lib.plotcore_get_line_name(
            self._figure._handle, self.index, buffer, capacity, length))

    @property
    def ready(self) -> bool:
        """Whether a line streamed in has finished arriving."""
        return _lib.plotcore_line_ready(self._figure._handle, self.index) == 1

    @property
    def has_time(self) -> bool:
        return _lib.plotcore_line_has_time(self._figure._handle, self.index) == 1

    @name.setter
    def name(self, value: str) -> None:
        _check(_lib.plotcore_set_line_name(self._figure._handle, self.index, _encode(value)))

    @property
    def colour(self) -> Optional[Colour]:
        """A colour of its own over whatever cycle is in force, or None."""
        has = ctypes.c_int32()
        colour = _native.Rgba()
        _check(_lib.plotcore_get_line_colour(self._figure._handle, self.index, ctypes.byref(has),
                                             ctypes.byref(colour)))
        return (colour.red, colour.green, colour.blue, colour.alpha) if has.value else None

    @colour.setter
    def colour(self, value: Optional[Sequence[float]]) -> None:
        if value is None:
            _check(_lib.plotcore_clear_line_colour(self._figure._handle, self.index))
        else:
            _check(_lib.plotcore_set_line_colour(self._figure._handle, self.index, _rgba(value)))

    def set_time(self, times) -> None:
        """A time base of the line's own, which beats the shared one; None for
        none."""
        self._figure.set_line_time(self.index, times)

    def set_reader(self, reader: Optional[Callable[[int, int], Sequence[float]]]) -> None:
        self._figure.set_line_reader(self.index, reader)


class Figure:
    """A plot: lines, settings, the view, the gestures, and a Scene out.

    Not thread-safe: one thread at a time, as with any widget.
    """

    def __init__(self) -> None:
        handle = _lib.plotcore_figure_new()
        if not handle:
            raise MemoryError("plotcore could not make a figure")
        self._handle = handle
        self._measure = None
        self.settings = _Named(self)
        self.style = _Style(self)

    def close(self) -> None:
        """Let the figure go now rather than when it is collected."""
        if self._handle:
            _lib.plotcore_figure_free(self._handle)
            self._handle = None

    def __del__(self) -> None:
        try:
            self.close()
        except Exception:  # noqa: BLE001 -- the interpreter may be going
            pass

    def __enter__(self) -> "Figure":
        return self

    def __exit__(self, *_exc) -> None:
        self.close()

    # --- lines ------------------------------------------------------------

    def add_line(self, values, name: str = "") -> int:
        """Copy `values` in as a line; its index."""
        array = _array(values)
        line = ctypes.c_int32()
        _check(_lib.plotcore_add_line(self._handle, _pointer(array), array.size, _encode(name),
                                      ctypes.byref(line)))
        return line.value

    def begin_line(self, count: int, name: str = "") -> int:
        """A line that arrives in pieces: `count` elements, appended in order
        and then finished. Not drawn until it is."""
        line = ctypes.c_int32()
        _check(_lib.plotcore_begin_line(self._handle, int(count), _encode(name),
                                        ctypes.byref(line)))
        return line.value

    def append_line(self, line: int, values) -> None:
        array = _array(values)
        _check(_lib.plotcore_append_line(self._handle, line, _pointer(array), array.size))

    def finish_line(self, line: int) -> None:
        _check(_lib.plotcore_finish_line(self._handle, line))

    def set_line_reader(self, line: int,
                        reader: Optional[Callable[[int, int], Sequence[float]]]) -> None:
        """Where a closer look finer than the line is held at reads from:
        `reader(first, count)` returns that many values, or raises."""
        if reader is None:
            _check(_lib.plotcore_set_line_reader(self._handle, line, _native.READER(),
                                                 None, _native.FREE_CONTEXT()))
            return
        with _readers_lock:
            token = next(_tokens)
            _readers[token] = reader
        try:
            _check(_lib.plotcore_set_line_reader(self._handle, line, _call_reader, token,
                                                 _release_reader))
        except Error:
            with _readers_lock:
                _readers.pop(token, None)
            raise

    def remove_line(self, line: int) -> None:
        _check(_lib.plotcore_remove_line(self._handle, line))

    def clear(self) -> None:
        _check(_lib.plotcore_clear(self._handle))

    def __len__(self) -> int:
        return int(_lib.plotcore_line_count(self._handle))

    def line(self, index: int) -> Line:
        if not 0 <= index < len(self):
            raise IndexError(f"no line {index}")
        return Line(self, index)

    def lines(self) -> Iterator[Line]:
        return (Line(self, i) for i in range(len(self)))

    # --- x ------------------------------------------------------------------

    def set_index_axis(self, start: float, step: float) -> None:
        """The x of element i of a line with no time: start + i * step."""
        _check(_lib.plotcore_set_index_axis(self._handle, float(start), float(step)))

    def set_time(self, times) -> None:
        """A time base every line without its own is drawn against; None for
        none."""
        if times is None:
            _check(_lib.plotcore_set_time(self._handle, None, 0))
            return
        array = _array(times)
        _check(_lib.plotcore_set_time(self._handle, _pointer(array), array.size))

    def set_line_time(self, line: int, times) -> None:
        if times is None:
            _check(_lib.plotcore_set_line_time(self._handle, line, None, 0))
            return
        array = _array(times)
        _check(_lib.plotcore_set_line_time(self._handle, line, _pointer(array), array.size))

    # --- holding --------------------------------------------------------------

    def set_appetite(self, appetite: str) -> None:
        """How much of the machine the lines may hold: "low", "medium" or
        "greedy"."""
        if appetite not in _APPETITES:
            raise ValueError(f"an appetite is low, medium or greedy, not {appetite!r}")
        _check(_lib.plotcore_set_appetite(self._handle, _APPETITES[appetite]))

    @property
    def budget_bytes(self) -> int:
        return int(_lib.plotcore_budget_bytes(self._handle))

    @budget_bytes.setter
    def budget_bytes(self, value: int) -> None:
        """Pin what the lines may hold; zero goes back to the appetite."""
        _check(_lib.plotcore_set_budget_bytes(self._handle, int(value)))

    @property
    def held_bytes(self) -> int:
        return int(_lib.plotcore_held_bytes(self._handle))

    # --- the pane -----------------------------------------------------------

    def set_size(self, width: float, height: float, pixel_ratio: float = 1.0) -> None:
        _check(_lib.plotcore_set_size(self._handle, float(width), float(height),
                                      float(pixel_ratio)))

    def set_text_measure(self, width: Optional[Callable[[str, TextRole], float]],
                         height: Optional[Callable[[TextRole], float]] = None) -> None:
        """How the host measures text, in pane pixels; None to go back to an
        approximation that needs no font."""
        if width is None or height is None:
            self._measure = None
            _check(_lib.plotcore_set_text_measure(self._handle, _native.TEXT_WIDTH(),
                                                  _native.TEXT_HEIGHT(), None))
            return

        def measure_width(_context, text, role):
            try:
                return float(width(text.decode("utf-8"), TextRole(role)))
            except Exception:  # noqa: BLE001 -- an exception must not cross into C
                return 0.0

        def measure_height(_context, role):
            try:
                return float(height(TextRole(role)))
            except Exception:  # noqa: BLE001
                return 0.0

        # Held by the figure: the C side calls these on every scene.
        self._measure = (_native.TEXT_WIDTH(measure_width), _native.TEXT_HEIGHT(measure_height))
        _check(_lib.plotcore_set_text_measure(self._handle, self._measure[0], self._measure[1],
                                              None))

    # --- gestures, in pane pixels with y down ------------------------------------

    def wheel(self, x: float, y: float, angle_delta: float,
              modifiers: Modifiers = Modifiers.NONE) -> None:
        """`angle_delta` in eighths of a degree: 120 is one notch. Shift zooms
        across the pane, Control up it; Alt and Control scale the line under
        the pointer."""
        _check(_lib.plotcore_wheel(self._handle, float(x), float(y), float(angle_delta),
                                   int(modifiers)))

    def press(self, x: float, y: float, button: Button = Button.LEFT,
              modifiers: Modifiers = Modifiers.NONE) -> None:
        _check(_lib.plotcore_press(self._handle, float(x), float(y), int(button), int(modifiers)))

    def move(self, x: float, y: float, modifiers: Modifiers = Modifiers.NONE) -> None:
        _check(_lib.plotcore_move(self._handle, float(x), float(y), int(modifiers)))

    def release(self, x: float, y: float, button: Button = Button.LEFT,
                modifiers: Modifiers = Modifiers.NONE) -> None:
        _check(_lib.plotcore_release(self._handle, float(x), float(y), int(button),
                                     int(modifiers)))

    def double_click(self, x: float, y: float, button: Button = Button.LEFT) -> None:
        _check(_lib.plotcore_double_click(self._handle, float(x), float(y), int(button)))

    def hover(self, x: float, y: float) -> None:
        _check(_lib.plotcore_hover(self._handle, float(x), float(y)))

    def leave(self) -> None:
        _check(_lib.plotcore_leave(self._handle))

    def reset_view(self) -> None:
        _check(_lib.plotcore_reset_view(self._handle))

    def set_view(self, x0: float, x1: float, y0: float, y1: float) -> None:
        """The window named in values, clipped to what the axes can draw."""
        _check(_lib.plotcore_set_view(self._handle, float(x0), float(x1), float(y0), float(y1)))

    @property
    def view(self) -> Tuple[float, float, float, float]:
        values = [ctypes.c_double() for _ in range(4)]
        _check(_lib.plotcore_view(self._handle, *(ctypes.byref(v) for v in values)))
        return tuple(v.value for v in values)  # type: ignore[return-value]

    def line_at(self, x: float, y: float, reach: float = 12.0) -> Optional[int]:
        found = _lib.plotcore_line_at(self._handle, float(x), float(y), float(reach))
        return None if found < 0 else int(found)

    # --- a view, written down ------------------------------------------------------

    def state(self) -> str:
        """The settings, every line's settings and the window, as JSON."""
        return _copied(lambda b, c, n: _lib.plotcore_state(self._handle, b, c, n))

    def restore(self, state: str) -> None:
        """Put back what state() wrote. A setting this build does not know is
        passed over; ValueError when `state` is not a view at all."""
        try:
            _check(_lib.plotcore_restore(self._handle, _encode(state)))
        except Error as error:
            raise ValueError(str(error)) from error

    # --- the picture ---------------------------------------------------------------------

    def scene(self) -> Scene:
        handle = _lib.plotcore_scene_new(self._handle)
        if not handle:
            message = _lib.plotcore_last_error() or b""
            raise Error(4, message.decode("utf-8", "replace"))
        try:
            return copy_scene(handle)
        finally:
            _lib.plotcore_scene_free(handle)

