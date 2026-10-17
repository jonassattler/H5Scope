# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-FileCopyrightText: 2026 Andreas Martin
# SPDX-License-Identifier: GPL-3.0-only

"""The plot H5Scope draws, for Python.

    >>> import numpy as np, plotcore
    >>> figure = plotcore.Figure()
    >>> figure.add_line(np.sin(np.linspace(0, 100, 1_000_000)), "sine")
    0
    >>> figure.set_size(800, 500)
    >>> figure.wheel(400, 250, 360)          # three notches in, about the pointer
    >>> scene = figure.scene()               # strokes, rules and text to paint

A line is held as a pyramid of min/max envelopes, so a spike one sample wide is
never thinned away and a zoom folds what is held rather than reading again. The
axes, the gestures and the colours are the viewer's own, held to it by its
parity tests. `plotcore.qt` paints a figure in a PyQt6 widget and
`plotcore.mpl` draws one with matplotlib.
"""

from __future__ import annotations

from typing import List, Optional

from . import _native
from .figure import Button, Error, Figure, Line, Modifiers
from .scene import Align, LegendEntry, Reading, Rect, Scene, Text, TextRole

__all__ = [
    "Align",
    "Button",
    "Error",
    "Figure",
    "LegendEntry",
    "Line",
    "Modifiers",
    "Reading",
    "Rect",
    "Scene",
    "Text",
    "TextRole",
    "abi_version",
    "palette",
    "palette_names",
    "ramp_names",
    "version",
]

__version__ = "0.1.0"


def version() -> str:
    """The C library's version."""
    return _native.lib.plotcore_version().decode("ascii")


def abi_version() -> int:
    return int(_native.lib.plotcore_abi_version())


def palette_names() -> List[str]:
    """The categorical cycles, in the order a settings panel offers them."""
    lib = _native.lib
    return [lib.plotcore_palette_name(i).decode() for i in range(lib.plotcore_palette_count())]


def ramp_names() -> List[str]:
    """The continuous maps, in the same sense."""
    lib = _native.lib
    return [lib.plotcore_ramp_name(i).decode() for i in range(lib.plotcore_ramp_count())]


def palette(name: str, on_dark: bool = True) -> Optional[List[tuple]]:
    """A cycle's or a map's stops as RGBA tuples, or None for a name that is
    neither."""
    lib = _native.lib
    count = lib.plotcore_palette(name.encode(), int(on_dark), None, 0)
    if count < 0:
        return None
    stops = (_native.Rgba * max(count, 1))()
    lib.plotcore_palette(name.encode(), int(on_dark), stops, count)
    return [(s.red, s.green, s.blue, s.alpha) for s in stops[:count]]
