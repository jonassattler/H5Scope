# SPDX-FileCopyrightText: 2026 Andreas Martin
# SPDX-License-Identifier: GPL-3.0-only

"""Drop-in package: from h5plot import PlotWidget"""

from .widget import PlotWidget
from .lib import Plot

__all__ = ["PlotWidget", "Plot", "MplView"]


def __getattr__(name):
    # matplotlib is an extra. Importing the window must not require it.
    if name == "MplView":
        from .mpl import MplView

        return MplView
    raise AttributeError(f"module {__name__!r} has no attribute {name!r}")
