# SPDX-FileCopyrightText: 2026 Andreas Martin
# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-License-Identifier: GPL-3.0-only

"""A figure drawn by matplotlib.

The axes are matplotlib's: its ticks, its logarithm, its fonts, with the limits
taken from the figure's view. The lines are not -- they are the folds plotcore
reduced each line to, handed over in the data's own units, so a spike one
sample wide is still there and a gap is still a gap. A line on an axis of its
own is drawn on a twin axis with that axis's window; one that is only moved or
scaled is drawn on a twin axis with no spine, for the reason the figure gives
it none.

`draw()` paints once, for a picture. `View` keeps a matplotlib figure live: a
scroll, a drag and a band go through the plotcore figure's own gestures, so
matplotlib zooms the line rather than the pane of it that was last drawn.

    pip install plotcore[matplotlib]
"""

from __future__ import annotations

from typing import List, Optional, Tuple

import numpy as np

try:
    from matplotlib.patches import Rectangle
    from matplotlib.ticker import MultipleLocator
except ImportError as error:  # pragma: no cover
    raise ImportError("plotcore.mpl needs matplotlib: pip install plotcore[matplotlib]") from error

from .figure import Button, Figure, Modifiers
from .scene import Scene

# How far apart the spines of axes of their own sit, in points.
_SPINE = 48

_CORNERS = {
    "top-left": "upper left",
    "top-right": "upper right",
    "bottom-left": "lower left",
    "bottom-right": "lower right",
}


def _joined(runs: List[np.ndarray]) -> Tuple[np.ndarray, np.ndarray]:
    """Every run of a line in one array, with a NaN between two runs so
    matplotlib does not stitch across a gap."""
    if not runs:
        return np.zeros(0), np.zeros(0)
    parts = []
    for i, run in enumerate(runs):
        if i:
            parts.append(np.array([[np.nan, np.nan]]))
        parts.append(run)
    joined = np.concatenate(parts)
    return joined[:, 0], joined[:, 1]


def _scale(axis_setter, logarithmic: bool, base: float, low: float, high: float) -> None:
    if logarithmic and low > 0.0 and high > low:
        axis_setter("log", base=base)
    else:
        axis_setter("linear")


def _paint(figure: Figure, scene: Scene, ax, extra: list) -> None:
    """Draw `scene` onto `ax`, adding any twin axes made to `extra`."""
    settings = figure.settings
    flipped = settings["flipped"]
    chrome = scene.chrome
    x0, x1, y0, y1 = scene.view
    x_log, y_log = settings["xLog"], settings["yLog"]
    x_base, y_base = settings["xLogBase"], settings["yLogBase"]

    ax.set_facecolor(chrome.ground[:3])
    ax.figure.set_facecolor(chrome.ground[:3])
    for spine in ax.spines.values():
        spine.set_color(chrome.axis_rule[:3])
    ax.tick_params(colors=chrome.ink[:3], which="both")

    # The common axes: x along the foot, or up the side when flipped.
    along, across = (ax.set_ylim, ax.set_xlim) if flipped else (ax.set_xlim, ax.set_ylim)
    along_scale, across_scale = ((ax.set_yscale, ax.set_xscale) if flipped
                                 else (ax.set_xscale, ax.set_yscale))
    along_scale("linear")
    across_scale("linear")
    along(x0, x1)
    across(y0, y1)
    _scale(along_scale, x_log, x_base, x0, x1)
    common = any(not line.own for line in scene.lines) or not scene.lines
    if common:
        _scale(across_scale, y_log, y_base, y0, y1)
    (ax.xaxis if flipped else ax.yaxis).set_visible(common)

    grid = settings["grid"]
    ax.grid(False)
    if grid != "none":
        rule = dict(color=chrome.rule_major[:3], linewidth=0.8)
        ax.grid(True, which="both" if grid == "dense" else "major", **rule)
        if grid == "dense":
            ax.minorticks_on()
            ax.grid(True, which="minor", color=chrome.rule_minor[:3], linewidth=0.5)
        if grid == "custom":
            for step, axis in ((settings["gridStepX"], ax.yaxis if flipped else ax.xaxis),
                               (settings["gridStepY"], ax.xaxis if flipped else ax.yaxis)):
                if step > 0 and axis.get_scale() == "linear":
                    axis.set_major_locator(MultipleLocator(step))

    handles = []
    slot = 0
    markers = settings["markers"]
    names = {entry.line: entry.name for entry in scene.legend}
    for line in scene.lines:
        xs, ys = _joined(line.values)
        if flipped:
            xs, ys = ys, xs
        if line.own:
            twin = ax.twiny() if flipped else ax.twinx()
            extra.append(twin)
            limits = twin.set_xlim if flipped else twin.set_ylim
            setter = twin.set_xscale if flipped else twin.set_yscale
            setter("linear")
            limits(line.y_low, line.y_high)
            _scale(setter, line.y_log, y_base, line.y_low, line.y_high)
            side = "top" if flipped else "right"
            spine = twin.spines[side]
            if line.numbered:
                spine.set_position(("outward", _SPINE * slot))
                spine.set_color(line.colour[:3])
                twin.tick_params(axis="x" if flipped else "y", colors=line.colour[:3])
                label = figure.line(line.line).settings["axisLabel"]
                if label:
                    (twin.set_xlabel if flipped else twin.set_ylabel)(label,
                                                                      color=line.colour[:3])
                slot += 1
            else:
                spine.set_visible(False)
                (twin.xaxis if flipped else twin.yaxis).set_visible(False)
            target = twin
        else:
            target = ax
        drawn, = target.plot(
            xs, ys, color=line.colour[:3], alpha=line.colour[3], linewidth=line.width,
            solid_capstyle="butt",
            marker="o" if markers and len(line.markers) else None,
            markersize=1.5 * line.marker_radius,
        )
        if line.line in names:
            drawn.set_label(names[line.line])
            handles.append(drawn)

    if settings["title"]:
        ax.set_title(settings["title"], color=chrome.ink[:3])
    x_label, y_label = settings["xLabel"], settings["yLabel"]
    (ax.set_ylabel if flipped else ax.set_xlabel)(x_label, color=chrome.ink[:3])
    if common:
        (ax.set_xlabel if flipped else ax.set_ylabel)(y_label, color=chrome.ink[:3])
    if handles and settings["legend"]:
        legend = ax.legend(handles=handles, loc=_CORNERS.get(settings["legendCorner"], "best"),
                           facecolor=chrome.ground[:3], edgecolor=chrome.axis_rule[:3],
                           labelcolor=chrome.ink[:3])
        legend.set_zorder(10)
    if scene.message:
        ax.text(0.5, 0.5, scene.message, transform=ax.transAxes, ha="center", va="center",
                wrap=True, color=chrome.ink[:3])


def _pixels_of(ax) -> Tuple[int, int]:
    """The axes' own box, in display pixels."""
    box = ax.get_window_extent()
    return max(1, int(round(box.width))), max(1, int(round(box.height)))


def draw(figure: Figure, ax=None, *, size: Optional[Tuple[int, int]] = None):
    """Draw `figure` once onto the matplotlib axes `ax` (a new figure's when
    None) and return the axes. The lines are folded for `size` pixels, or for
    the axes' own size."""
    if ax is None:
        import matplotlib.pyplot as plt

        _, ax = plt.subplots()
    width, height = size if size is not None else _pixels_of(ax)
    figure.set_size(width, height)
    scene = figure.scene()
    _paint(figure, scene, ax, [])
    return ax


class View:
    """A live matplotlib figure over a plotcore figure.

    Matplotlib's own navigation is off: a scroll, a drag and a band are the
    plotcore figure's gestures, mapped from the axes' box onto its pane."""

    def __init__(self, figure: Figure, mpl_figure=None):
        if mpl_figure is None:
            import matplotlib.pyplot as plt

            mpl_figure = plt.figure()
        self.figure = figure
        self.mpl = mpl_figure
        self.ax = mpl_figure.add_subplot(111)
        self.ax.set_navigate(False)
        self._extra: list = []
        self._scene: Optional[Scene] = None
        self._band = None
        self._readout = None
        canvas = mpl_figure.canvas
        canvas.mpl_connect("scroll_event", self._scroll)
        canvas.mpl_connect("button_press_event", self._press)
        canvas.mpl_connect("motion_notify_event", self._motion)
        canvas.mpl_connect("button_release_event", self._release)
        canvas.mpl_connect("axes_leave_event", self._leave)
        canvas.mpl_connect("resize_event", lambda _event: self.redraw())

    # --- drawing ------------------------------------------------------------

    def redraw(self) -> None:
        for twin in self._extra:
            twin.remove()
        self._extra.clear()
        self.ax.cla()
        self.ax.set_navigate(False)
        self.mpl.canvas.draw_idle()
        width, height = _pixels_of(self.ax)
        # The figure's pane is laid out at the axes' size, so a pixel of a
        # gesture is a pixel of the line and a band's minimum is a band's.
        self.figure.set_size(width, height)
        self._scene = self.figure.scene()
        _paint(self.figure, self._scene, self.ax, self._extra)
        self._decorate()
        self.mpl.canvas.draw_idle()

    def _decorate(self) -> None:
        scene = self._scene
        if scene is None:
            return
        area = scene.area
        if scene.band is not None:
            band = scene.band
            left = (band.x - area.x) / area.width
            top = (band.y - area.y) / area.height
            self.ax.add_patch(Rectangle(
                (left, 1.0 - top - band.height / area.height), band.width / area.width,
                band.height / area.height, transform=self.ax.transAxes, fill=True,
                alpha=0.12, color=scene.chrome.cursor_ink[:3], zorder=20))
        if scene.reading.valid:
            reading = scene.reading
            self.ax.text(0.01, 0.99, f"x {reading.x_text}   y {reading.y_text}",
                         transform=self.ax.transAxes, ha="left", va="top",
                         color=scene.chrome.ink[:3], zorder=21,
                         bbox=dict(facecolor=scene.ground[:3], edgecolor="none", alpha=0.85))
            self.ax.plot([(reading.px - area.x) / area.width],
                         [1.0 - (reading.py - area.y) / area.height], "o",
                         transform=self.ax.transAxes, markerfacecolor="none",
                         color=scene.chrome.cursor_ink[:3], zorder=21)

    # --- gestures ---------------------------------------------------------------

    def _pane(self, event) -> Optional[Tuple[float, float]]:
        """Where a matplotlib event falls on the figure's pane, or None when
        it is outside the axes."""
        if self._scene is None or event.x is None or event.y is None:
            return None
        # The figure may be drawn elsewhere too -- a Qt window over the same
        # one -- and a gesture is read against the pane it was laid out at.
        self.figure.set_size(*_pixels_of(self.ax))
        box = self.ax.get_window_extent()
        across = (event.x - box.x0) / box.width
        down = (box.y1 - event.y) / box.height
        area = self._scene.area
        return area.x + across * area.width, area.y + down * area.height

    @staticmethod
    def _modifiers(event) -> Modifiers:
        held = getattr(event, "modifiers", None)
        words = set(held) if held else set((event.key or "").replace("+", " ").split())
        out = Modifiers.NONE
        if "shift" in words:
            out |= Modifiers.SHIFT
        if words & {"ctrl", "control"}:
            out |= Modifiers.CONTROL
        if "alt" in words:
            out |= Modifiers.ALT
        return out

    @staticmethod
    def _button(event) -> Optional[Button]:
        return {1: Button.LEFT, 2: Button.MIDDLE, 3: Button.RIGHT}.get(int(event.button or 0))

    def _scroll(self, event) -> None:
        at = self._pane(event)
        if at is None:
            return
        self.figure.wheel(at[0], at[1], 120.0 * event.step, self._modifiers(event))
        self.redraw()

    def _press(self, event) -> None:
        at = self._pane(event)
        button = self._button(event)
        if at is None or button is None:
            return
        if event.dblclick:
            self.figure.double_click(at[0], at[1], button)
        else:
            self.figure.press(at[0], at[1], button, self._modifiers(event))
        self.redraw()

    def _motion(self, event) -> None:
        at = self._pane(event)
        if at is None:
            return
        self.figure.move(at[0], at[1], self._modifiers(event))
        self.redraw()

    def _release(self, event) -> None:
        at = self._pane(event)
        button = self._button(event)
        if at is None or button is None:
            return
        self.figure.release(at[0], at[1], button, self._modifiers(event))
        self.redraw()

    def _leave(self, _event) -> None:
        self.figure.leave()
        self.redraw()

    def show(self) -> None:
        import matplotlib.pyplot as plt

        self.redraw()
        plt.show()


__all__ = ["View", "draw"]
