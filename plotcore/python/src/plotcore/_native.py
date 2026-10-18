# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-FileCopyrightText: 2026 Andreas Martin
# SPDX-License-Identifier: GPL-3.0-only

"""plotcore.h, declared for ctypes.

Every structure and function the C interface has, and nothing else: the
Pythonic surface is figure.py. The declarations follow the header line for
line, so a change to one is a change to the other, and the ABI version is
checked before anything is called.
"""

from __future__ import annotations

import ctypes
import os
import sys
from ctypes import (
    CFUNCTYPE,
    POINTER,
    Structure,
    c_char_p,
    c_double,
    c_int,
    c_int32,
    c_int64,
    c_void_p,
)
from pathlib import Path

ABI_VERSION = 1


class Rgba(Structure):
    _fields_ = [("red", c_double), ("green", c_double), ("blue", c_double), ("alpha", c_double)]


class Chrome(Structure):
    _fields_ = [
        ("ground", Rgba),
        ("ink", Rgba),
        ("rule_minor", Rgba),
        ("rule_major", Rgba),
        ("axis_rule", Rgba),
        ("cursor_ink", Rgba),
    ]


class Rect(Structure):
    _fields_ = [("x", c_double), ("y", c_double), ("width", c_double), ("height", c_double)]


class Segment(Structure):
    _fields_ = [
        ("x0", c_double),
        ("y0", c_double),
        ("x1", c_double),
        ("y1", c_double),
        ("colour", Rgba),
        ("width", c_double),
    ]


class LineInfo(Structure):
    _fields_ = [
        ("line", c_int32),
        ("colour", Rgba),
        ("width", c_double),
        ("marker_radius", c_double),
        ("run_count", c_int64),
        ("triangle_points", c_int64),
        ("marker_count", c_int64),
        ("y_low", c_double),
        ("y_high", c_double),
        ("y_log", c_int32),
        ("own", c_int32),
        ("numbered", c_int32),
    ]


class Text(Structure):
    _fields_ = [
        ("x", c_double),
        ("y", c_double),
        ("across", c_int32),
        ("down", c_int32),
        ("role", c_int32),
        ("vertical", c_int32),
        ("colour", Rgba),
        ("text", c_char_p),
        ("base", c_char_p),
        ("exponent", c_char_p),
    ]


class LegendEntry(Structure):
    _fields_ = [
        ("line", c_int32),
        ("colour", Rgba),
        ("x0", c_double),
        ("y0", c_double),
        ("x1", c_double),
        ("y1", c_double),
        ("text_x", c_double),
        ("text_y", c_double),
        ("name", c_char_p),
    ]


class Reading(Structure):
    _fields_ = [
        ("valid", c_int32),
        ("line", c_int32),
        ("x", c_double),
        ("y", c_double),
        ("px", c_double),
        ("py", c_double),
        ("x_text", c_char_p),
        ("y_text", c_char_p),
    ]


READER = CFUNCTYPE(c_int, c_void_p, c_int64, c_int64, POINTER(c_double))
FREE_CONTEXT = CFUNCTYPE(None, c_void_p)
TEXT_WIDTH = CFUNCTYPE(c_double, c_void_p, c_char_p, c_int32)
TEXT_HEIGHT = CFUNCTYPE(c_double, c_void_p, c_int32)

_status = c_int
_figure = c_void_p
_scene = c_void_p
_doubles = POINTER(c_double)

# name: (result, arguments)
_DECLARATIONS = {
    "plotcore_abi_version": (c_int32, []),
    "plotcore_version": (c_char_p, []),
    "plotcore_last_error": (c_char_p, []),
    "plotcore_palette_count": (c_int32, []),
    "plotcore_palette_name": (c_char_p, [c_int32]),
    "plotcore_ramp_count": (c_int32, []),
    "plotcore_ramp_name": (c_char_p, [c_int32]),
    "plotcore_palette": (c_int32, [c_char_p, c_int32, POINTER(Rgba), c_int32]),
    "plotcore_figure_new": (_figure, []),
    "plotcore_figure_free": (None, [_figure]),
    "plotcore_add_line": (_status, [_figure, _doubles, c_int64, c_char_p, POINTER(c_int32)]),
    "plotcore_begin_line": (_status, [_figure, c_int64, c_char_p, POINTER(c_int32)]),
    "plotcore_append_line": (_status, [_figure, c_int32, _doubles, c_int64]),
    "plotcore_finish_line": (_status, [_figure, c_int32]),
    "plotcore_set_line_reader": (_status, [_figure, c_int32, READER, c_void_p, FREE_CONTEXT]),
    "plotcore_remove_line": (_status, [_figure, c_int32]),
    "plotcore_clear": (_status, [_figure]),
    "plotcore_line_count": (c_int32, [_figure]),
    "plotcore_line_length": (c_int64, [_figure, c_int32]),
    "plotcore_set_line_name": (_status, [_figure, c_int32, c_char_p]),
    "plotcore_line_extent": (_status, [_figure, c_int32, _doubles, _doubles]),
    "plotcore_line_base_bucket": (c_int64, [_figure, c_int32]),
    "plotcore_get_line_name": (_status, [_figure, c_int32, c_char_p, c_int64, POINTER(c_int64)]),
    "plotcore_line_ready": (c_int32, [_figure, c_int32]),
    "plotcore_line_has_time": (c_int32, [_figure, c_int32]),
    "plotcore_set_index_axis": (_status, [_figure, c_double, c_double]),
    "plotcore_set_time": (_status, [_figure, _doubles, c_int64]),
    "plotcore_set_line_time": (_status, [_figure, c_int32, _doubles, c_int64]),
    "plotcore_set_appetite": (_status, [_figure, c_int]),
    "plotcore_set_budget_bytes": (_status, [_figure, c_int64]),
    "plotcore_budget_bytes": (c_int64, [_figure]),
    "plotcore_held_bytes": (c_int64, [_figure]),
    "plotcore_set_bool": (_status, [_figure, c_char_p, c_int32]),
    "plotcore_set_number": (_status, [_figure, c_char_p, c_double]),
    "plotcore_set_text": (_status, [_figure, c_char_p, c_char_p]),
    "plotcore_set_colour": (_status, [_figure, c_char_p, Rgba]),
    "plotcore_get_bool": (_status, [_figure, c_char_p, POINTER(c_int32)]),
    "plotcore_get_number": (_status, [_figure, c_char_p, _doubles]),
    "plotcore_get_text": (_status, [_figure, c_char_p, c_char_p, c_int64, POINTER(c_int64)]),
    "plotcore_get_colour": (_status, [_figure, c_char_p, POINTER(Rgba)]),
    "plotcore_set_line_bool": (_status, [_figure, c_int32, c_char_p, c_int32]),
    "plotcore_set_line_number": (_status, [_figure, c_int32, c_char_p, c_double]),
    "plotcore_set_line_text": (_status, [_figure, c_int32, c_char_p, c_char_p]),
    "plotcore_set_line_colour": (_status, [_figure, c_int32, Rgba]),
    "plotcore_clear_line_colour": (_status, [_figure, c_int32]),
    "plotcore_get_line_bool": (_status, [_figure, c_int32, c_char_p, POINTER(c_int32)]),
    "plotcore_get_line_number": (_status, [_figure, c_int32, c_char_p, _doubles]),
    "plotcore_get_line_text": (
        _status,
        [_figure, c_int32, c_char_p, c_char_p, c_int64, POINTER(c_int64)],
    ),
    "plotcore_get_line_colour": (_status, [_figure, c_int32, POINTER(c_int32), POINTER(Rgba)]),
    "plotcore_set_style": (_status, [_figure, c_char_p, c_double]),
    "plotcore_set_text_measure": (_status, [_figure, TEXT_WIDTH, TEXT_HEIGHT, c_void_p]),
    "plotcore_set_size": (_status, [_figure, c_double, c_double, c_double]),
    "plotcore_wheel": (_status, [_figure, c_double, c_double, c_double, c_int32]),
    "plotcore_press": (_status, [_figure, c_double, c_double, c_int, c_int32]),
    "plotcore_move": (_status, [_figure, c_double, c_double, c_int32]),
    "plotcore_release": (_status, [_figure, c_double, c_double, c_int, c_int32]),
    "plotcore_double_click": (_status, [_figure, c_double, c_double, c_int]),
    "plotcore_hover": (_status, [_figure, c_double, c_double]),
    "plotcore_leave": (_status, [_figure]),
    "plotcore_reset_view": (_status, [_figure]),
    "plotcore_set_view": (_status, [_figure, c_double, c_double, c_double, c_double]),
    "plotcore_view": (_status, [_figure, _doubles, _doubles, _doubles, _doubles]),
    "plotcore_line_at": (c_int32, [_figure, c_double, c_double, c_double]),
    "plotcore_state": (_status, [_figure, c_char_p, c_int64, POINTER(c_int64)]),
    "plotcore_restore": (_status, [_figure, c_char_p]),
    "plotcore_scene_new": (_scene, [_figure]),
    "plotcore_scene_free": (None, [_scene]),
    "plotcore_scene_size": (None, [_scene, _doubles, _doubles]),
    "plotcore_scene_ground": (Rgba, [_scene]),
    "plotcore_scene_chrome": (Chrome, [_scene]),
    "plotcore_scene_area": (Rect, [_scene]),
    "plotcore_scene_view": (None, [_scene, _doubles, _doubles, _doubles, _doubles]),
    "plotcore_scene_message": (c_char_p, [_scene]),
    "plotcore_scene_line_count": (c_int64, [_scene]),
    "plotcore_scene_line": (_status, [_scene, c_int64, POINTER(LineInfo)]),
    "plotcore_scene_line_run_length": (c_int64, [_scene, c_int64, c_int64]),
    "plotcore_scene_line_run": (_doubles, [_scene, c_int64, c_int64]),
    "plotcore_scene_line_run_values": (_doubles, [_scene, c_int64, c_int64]),
    "plotcore_scene_line_triangles": (_doubles, [_scene, c_int64]),
    "plotcore_scene_line_markers": (_doubles, [_scene, c_int64]),
    "plotcore_scene_rule_count": (c_int64, [_scene]),
    "plotcore_scene_rules": (POINTER(Segment), [_scene]),
    "plotcore_scene_stroke_count": (c_int64, [_scene]),
    "plotcore_scene_strokes": (POINTER(Segment), [_scene]),
    "plotcore_scene_text_count": (c_int64, [_scene]),
    "plotcore_scene_text": (_status, [_scene, c_int64, POINTER(Text)]),
    "plotcore_scene_legend_count": (c_int64, [_scene]),
    "plotcore_scene_legend": (_status, [_scene, c_int64, POINTER(LegendEntry)]),
    "plotcore_scene_legend_rect": (Rect, [_scene]),
    "plotcore_scene_reading": (Reading, [_scene]),
    "plotcore_scene_band": (c_int32, [_scene, POINTER(Rect)]),
}


def _library_path() -> Path:
    """The library: PLOTCORE_LIBRARY when it is set, else the one installed
    beside this file, which is where the wheel puts it."""
    named = os.environ.get("PLOTCORE_LIBRARY")
    if named:
        return Path(named)
    here = Path(__file__).resolve().parent
    if sys.platform == "win32":
        names = ["plotcore_c.dll"]
    elif sys.platform == "darwin":
        names = ["libplotcore_c.dylib"]
    else:
        names = ["libplotcore_c.so"]
    for name in names:
        if (here / name).exists():
            return here / name
    raise ImportError(
        f"plotcore's C library is not beside {here}; install the package, or set "
        "PLOTCORE_LIBRARY to a libplotcore_c built from plotcore/CMakeLists.txt"
    )


def _load() -> ctypes.CDLL:
    path = _library_path()
    lib = ctypes.CDLL(str(path))
    for name, (result, arguments) in _DECLARATIONS.items():
        function = getattr(lib, name)
        function.restype = result
        function.argtypes = arguments
    found = lib.plotcore_abi_version()
    if found != ABI_VERSION:
        raise ImportError(
            f"{path} speaks plotcore ABI {found}, and this package speaks {ABI_VERSION}"
        )
    return lib


lib = _load()
