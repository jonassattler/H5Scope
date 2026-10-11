# SPDX-FileCopyrightText: 2026 Andreas Martin
# SPDX-License-Identifier: GPL-3.0-only

"""A matplotlib host for the same fold the Qt window draws.

The axes are matplotlib's: its ticks, its logarithm, its limits taken from
the camera. The samples are not. What is drawn is the envelope the fold
already reduced the line to, in the data's own units, and a gap stays a gap
so matplotlib does not stitch across it.

The navigation toolbar stays off. A scroll, a drag and a rectangle go
through the same camera the window uses. Left to itself, matplotlib would
zoom whatever was last drawn, and that is a pane of the line, not the line.
"""

from __future__ import annotations

import numpy as np

from .lib import Plot

# How far an own axis sits out from the one beside it, in points.
_SPINE = 48


class MplView:
    def __init__(self, plot: Plot | None = None, figure=None):
        try:
            from matplotlib.widgets import Button, CheckButtons
        except ImportError as error:
            raise ImportError(
                "matplotlib is not installed; pip install h5plot[matplotlib]"
            ) from error
        self.plot = plot if plot is not None else Plot()
        if figure is None:
            import matplotlib.pyplot as plt

            figure = plt.figure()
        self.figure = figure
        self.ax = self.figure.add_subplot(111)
        self.ax.set_navigate(False)
        self._own: list = []
        self._artists: list = []
        self._band = None
        self._band_xy = None
        self._band_artist = None
        self._band_labels: list = []
        self._hair_v = None
        self._hair_h = None
        self._hair_mark = None
        self._readout = None
        self._drag = None
        self._shift = None
        self._pane = (800, 400)
        self._drawing = False
        canvas = self.figure.canvas
        canvas.mpl_connect("scroll_event", self._scroll)
        canvas.mpl_connect("button_press_event", self._press)
        canvas.mpl_connect("motion_notify_event", self._motion)
        canvas.mpl_connect("button_release_event", self._release)
        canvas.mpl_connect("draw_event", self._drawn)
        canvas.mpl_connect("resize_event", self._resized)
        # The same two switches the Qt window offers. A scale you cannot reach
        # is a linear axis, whatever the camera was asked for.
        self._scale_ax = self.figure.add_axes([0.12, 0.01, 0.34, 0.07])
        self._scale_ax.set_frame_on(False)
        self._scales = CheckButtons(self._scale_ax, ["Log x", "Log y"], [False, False])
        self._scales.on_clicked(self._scale)
        self._log_bases = (10.0, 2.0, 2.718281828459045)
        self._log_base_i = 0
        self._base_ax = self.figure.add_axes([0.48, 0.01, 0.14, 0.07])
        self._base_button = Button(self._base_ax, "base 10")
        self._base_button.on_clicked(self._cycle_base)
        self._stack_ax = self.figure.add_axes([0.64, 0.01, 0.14, 0.07])
        self._stack_button = Button(self._stack_ax, "Stack")
        self._stack_button.on_clicked(self._stack)

    def _stack(self, _event) -> None:
        self.stack_lines()

    def _scale(self, label: str) -> None:
        status = dict(zip(("Log x", "Log y"), self._scales.get_status()))
        if label == "Log x":
            self.plot.set_x_log(bool(status["Log x"]))
        elif label == "Log y":
            self.plot.set_y_log(bool(status["Log y"]))
        self.redraw()

    def _cycle_base(self, _event) -> None:
        # One control for both axes. The base numbers the powers; the curve
        # stays where it was, because a ratio of logarithms does not contain it.
        self._log_base_i = (self._log_base_i + 1) % len(self._log_bases)
        base = self._log_bases[self._log_base_i]
        self.plot.set_x_log_base(base)
        self.plot.set_y_log_base(base)
        self._base_button.label.set_text("base e" if self._log_base_i == 2 else f"base {int(base)}")
        self.redraw()

    def stack_lines(self) -> None:
        """Lay every line in an equal band, the first at the top."""
        self.plot.stack_lines()
        self.redraw()

    def set_range(self, x0=None, x1=None, y0=None, y1=None) -> None:
        self.plot.set_range(x0, x1, y0, y1)
        self.redraw()

    def set_x_label(self, text: str) -> None:
        self.ax.set_xlabel(text or "")

    def set_y_label(self, text: str) -> None:
        self.ax.set_ylabel(text or "")

    def show_lines(self, fill) -> None:
        """Replace every line, then draw once.

        ``fill(plot)`` writes the lines and returns ``(x label, y label)``.
        The view resets once, after the lines are in, rather than on every
        channel the way ``PlotWidget.add_line`` does.
        """
        labels = fill(self.plot)
        x_label, y_label = ("", "") if not labels else labels
        self.plot.reset_view()
        self.set_x_label(x_label)
        self.set_y_label(y_label)
        self.redraw()

    def show(self) -> None:
        import matplotlib.pyplot as plt

        self.redraw()
        plt.show(block=False)
        self._hide_toolbar()
        plt.show()

    def redraw(self) -> None:
        self._drawing = True
        try:
            self._paint()
        finally:
            self._drawing = False
        self.figure.canvas.draw_idle()

    def _paint(self) -> None:
        self._clear_reading()
        width, height = self._pane
        self.plot.set_pane(int(width), int(height), 1.0)
        _pixels, runs, _ticks = self.plot.project()
        data = self.plot.data_points()
        owners = self.plot.run_lines()
        for artist in self._artists:
            artist.remove()
        self._artists.clear()
        for axes in self._own:
            axes.remove()
        self._own.clear()

        grouped: dict[int, list] = {}
        for run, line in zip(runs, owners):
            grouped.setdefault(int(line), []).append(run)

        shared = any(not self.plot.own_axis(line) for line in grouped)
        self.ax.yaxis.set_visible(shared)
        self.ax.spines["left"].set_visible(shared)
        x0, x1 = self.plot.view_min_x(), self.plot.view_max_x()
        self.ax.set_xscale("linear")
        self.ax.set_xlim(x0, x1)
        if self.plot.x_log() and x0 > 0.0 and x1 > x0:
            self.ax.set_xscale("log", base=self.plot.x_log_base())
        if shared:
            y0, y1 = self.plot.view_min_y(), self.plot.view_max_y()
            self.ax.set_yscale("linear")
            self.ax.set_ylim(y0, y1)
            if self.plot.y_log() and y0 > 0.0 and y1 > y0:
                self.ax.set_yscale("log", base=self.plot.y_log_base())
        # A numbered tick is a rule across the pane, as in the Qt window.
        # An own axis is a second scale, so it does not add a second set.
        # The ground of the axes is painted at zorder 1. A rule at 0 is
        # behind that ground and never shows.
        self.ax.grid(True, which="major", axis="x", color="0.82", linewidth=0.8, zorder=1.5)
        if shared:
            self.ax.grid(True, which="major", axis="y", color="0.82", linewidth=0.8, zorder=1.5)
        else:
            # Line properties on a false grid turn it back on. The call that
            # hides the common y axis has to be the one that only hides it.
            self.ax.grid(False, axis="y")

        from .widget import _mark_samples

        own_slot = 0
        handles = []
        for line, line_runs in grouped.items():
            if not line_runs:
                continue
            xs: list = []
            ys: list = []
            for run in line_runs:
                if xs:
                    xs.append(np.array([np.nan]))
                    ys.append(np.array([np.nan]))
                chunk = data[run.first:run.first + run.count]
                xs.append(chunk[:, 0])
                ys.append(chunk[:, 1])
            x = np.concatenate(xs)
            y = np.concatenate(ys)
            colour = (line_runs[0].red / 255.0, line_runs[0].green / 255.0, line_runs[0].blue / 255.0)
            if self.plot.own_axis(line):
                outward = _SPINE * own_slot
                if shared:
                    outward += _SPINE
                own_slot += 1
                axes = self._own_axes(colour, outward)
                axes.h5_line = line
                self._own.append(axes)
                low, high = self.plot.line_y_range(line)
                axes.set_yscale("linear")
                axes.set_ylim(low, high)
                if self.plot.line_y_log(line) and low > 0.0 and high > low:
                    axes.set_yscale("log", base=self.plot.y_log_base())
                # The column is that curve, so it carries the curve's name.
                name = self.plot.line_name(line)
                if name:
                    axes.set_ylabel(name, color=colour)
            else:
                axes = self.ax
            # The same room as the Qt window. The run is wider than the
            # view, so only the samples the axes are showing are counted.
            # A logarithmic axis leaves the non-positive samples out, and
            # the count then says the rest are a screen apart. They are
            # still a summary until the zoom draws the samples themselves.
            shown = int(np.count_nonzero(np.isfinite(x) & (x >= x0) & (x <= x1)))
            summarised = any(run.summarised for run in line_runs)
            mark = "o" if not summarised and _mark_samples(shown, width) else None
            drawn, = axes.plot(
                x, y, color=colour, linewidth=max(float(line_runs[0].width), 0.8),
                solid_capstyle="butt", marker=mark, markersize=5 if mark else 0,
            )
            self._artists.append(drawn)
            name = self.plot.line_name(line)
            if name:
                handles.append(drawn)
                drawn.set_label(name)
        named = sum(1 for column in self._own if column.get_ylabel())
        # The spines are a fixed number of points apart, and a name sits on
        # its spine. A share of the figure per curve, and again per name,
        # left an empty third once four curves were stacked: the margin grew
        # with the count, and the spines did not fill it.
        self.figure.subplots_adjust(left=self._left_margin(own_slot, shared, bool(named)),
                                    bottom=0.2)
        if handles:
            # On the common axes the legend is drawn before a shifted curve's
            # axes, so that curve runs through the names. A figure legend is
            # drawn after every axes, and the frame is opaque so a line behind
            # it does not show through.
            legend = self.figure.legend(
                handles=handles, loc="upper right", bbox_to_anchor=self.ax.get_position(),
                bbox_transform=self.figure.transFigure, frameon=True, framealpha=1.0,
                borderaxespad=0.4,
            )
            legend.set_zorder(20)
            self._artists.append(legend)

    def _left_margin(self, own_slot: int, shared: bool, named: bool) -> float:
        """Figure fraction that clears the leftmost spine and its labels."""
        if own_slot <= 0:
            return 0.12
        fig_pt = self.figure.get_figwidth() * 72.0
        if not fig_pt > 0.0:
            return 0.12
        # The first own spine lies on the axes when nothing is shared, and
        # one step out when the common spine is still there.
        outermost = _SPINE * (own_slot - 1)
        if shared:
            outermost += _SPINE
        tick_pt = 32.0
        name_pt = 16.0 if named else 0.0
        # The common labels need a gutter even when no own spine sticks out
        # past them. The larger of the two is what the figure has to clear.
        common_pt = (28.0 + tick_pt) if shared else 0.0
        gutter = max(common_pt, outermost + tick_pt + name_pt) + 6.0
        return gutter / fig_pt

    def _own_axes(self, colour, outward: float):
        # twinx is how the x axis is shared. The spine does not stay where
        # twinx puts it: an own y axis sits out to the left of the common one.
        axes = self.ax.twinx()
        axes.set_navigate(False)
        axes.spines["right"].set_visible(False)
        axes.spines["top"].set_visible(False)
        axes.spines["left"].set_position(("outward", outward))
        axes.yaxis.set_label_position("left")
        axes.yaxis.set_ticks_position("left")
        axes.spines["left"].set_color(colour)
        axes.tick_params(axis="y", colors=colour)
        axes.patch.set_visible(False)
        axes.grid(False)
        return axes

    def _drawn(self, _event) -> None:
        if self._drawing:
            return
        bbox = self.ax.get_window_extent()
        pane = (max(1, int(bbox.width)), max(1, int(bbox.height)))
        if pane != self._pane and pane[0] > 1 and pane[1] > 1:
            self._pane = pane
            self.redraw()

    def _resized(self, _event) -> None:
        self._drawn(_event)

    def _own_at_display(self, x, y):
        if x is None or y is None:
            return None
        pane = self.ax.bbox
        if pane.x0 <= x <= pane.x1 and pane.y0 <= y <= pane.y1:
            return None
        renderer = self.figure.canvas.get_renderer()
        for axes in self._own:
            boxes = [axes.yaxis.label.get_window_extent(renderer)]
            ticks = axes.yaxis.get_tightbbox(renderer)
            if ticks is not None:
                boxes.append(ticks)
            for box in boxes:
                if box is None or box.width < 0 or box.height < 0:
                    continue
                if box.x0 - 6 <= x <= box.x1 + 6 and box.y0 - 2 <= y <= box.y1 + 2:
                    return getattr(axes, "h5_line", None)
        return None

    def _axis_menu(self, index: int) -> None:
        was = self.plot.line_y_log(index)

        def apply(on: bool) -> None:
            self.plot.set_line_y_log(index, on)
            self.redraw()

        canvas = self.figure.canvas
        module = type(canvas).__module__.lower()
        if "qt" in module:
            menu_type = cursor = None
            for package in ("PySide6", "PyQt6", "PyQt5"):
                try:
                    widgets = __import__(package + ".QtWidgets", fromlist=["QMenu"])
                    gui = __import__(package + ".QtGui", fromlist=["QCursor"])
                except ImportError:
                    continue
                menu_type = widgets.QMenu
                cursor = gui.QCursor
                break
            if menu_type is not None and cursor is not None:
                menu = menu_type()
                action = menu.addAction("Logarithmisch")
                action.setCheckable(True)
                action.setChecked(was)
                if menu.exec(cursor.pos()) is action:
                    apply(not was)
                return
        if "tk" in module or hasattr(canvas, "get_tk_widget"):
            import tkinter as tk

            widget = canvas.get_tk_widget()
            menu = tk.Menu(widget, tearoff=0)
            var = tk.BooleanVar(value=was)
            menu.add_checkbutton(label="Logarithmisch", variable=var,
                                 command=lambda: apply(bool(var.get())))
            menu.tk_popup(widget.winfo_pointerx(), widget.winfo_pointery())

    def _hide_toolbar(self) -> None:
        manager = getattr(self.figure.canvas, "manager", None)
        toolbar = getattr(manager, "toolbar", None) if manager is not None else None
        if toolbar is None:
            return
        hide = getattr(toolbar, "setVisible", None)
        if hide is not None:
            hide(False)
            return
        forget = getattr(toolbar, "pack_forget", None)
        if forget is not None:
            forget()

    def _at(self, event):
        if event.x is None or event.y is None:
            return None
        bbox = self.ax.bbox
        return event.x - bbox.x0, bbox.y1 - event.y

    def _scroll(self, event) -> None:
        at = self._at(event)
        if at is None or not event.step:
            return
        key = event.key or ""
        shift = "shift" in key
        control = "control" in key or "ctrl" in key
        alt = "alt" in key
        # Raw flags. h5plot_wheel decides, including Alt+Ctrl scaling one curve.
        self.plot.wheel(at[0], at[1], 1.25 ** float(event.step), shift, control, alt)
        self.redraw()

    def _press(self, event) -> None:
        if getattr(event, "dblclick", False) and event.button == 1:
            self._drag = None
            self._band = None
            self._shift = None
            self.plot.reset_view()
            self.redraw()
            return
        at = self._at(event)
        if at is None:
            return
        self._clear_reading()
        key = event.key or ""
        alt = "alt" in key and "control" not in key and "ctrl" not in key
        if event.button == 2 or (event.button == 1 and alt):
            index = self.plot.nearest(at[0], at[1])
            if index >= 0:
                self._shift = (index, at[1])
            return
        if event.button == 1 and ("control" in key or "ctrl" in key):
            index = self.plot.nearest(at[0], at[1])
            if index >= 0 and self.plot.own_axis(index):
                self.plot.set_own_axis(index, False)
                self.redraw()
            return
        if event.button == 3:
            self._band = (at, at)
            self._band_xy = (event.x, event.y)
            return
        if event.button == 1:
            self._drag = at

    def _motion(self, event) -> None:
        at = self._at(event)
        if at is None:
            self._clear_reading(draw=True)
            return
        if self._shift is not None:
            index, last = self._shift
            self.plot.shift_line(index, 0.0, at[1] - last)
            self._shift = (index, at[1])
            self.redraw()
            return
        if self._band is not None:
            self._band = (self._band[0], at)
            self._show_band()
            return
        if self._drag is None:
            self._show_reading(at[0], at[1])
            return
        self.plot.pan(at[0] - self._drag[0], at[1] - self._drag[1])
        self._drag = at
        self.redraw()

    def _release(self, event) -> None:
        if self._shift is not None and event.button in (1, 2):
            self._shift = None
            return
        if event.button == 3 and self._band is not None:
            start, end = self._band
            origin = self._band_xy
            self._band = None
            self._band_xy = None
            self._hide_band()
            # Twelve pixels on either side is a window. Anything shorter was
            # a click, and a click on an own axis opens that axis's menu.
            # A click in the pane is kept for later and opens nothing.
            if self.plot.zoom_rect(start[0], start[1], end[0], end[1]):
                self.redraw()
                return
            if origin is not None:
                index = self._own_at_display(origin[0], origin[1])
                if index is not None:
                    self._axis_menu(index)
            return
        if event.button == 1:
            self._drag = None

    def _show_band(self) -> None:
        from matplotlib.patches import Rectangle

        from .widget import _axis_number

        start, end = self._band
        # The corners are the same mapping the zoom uses on release, written
        # the way the Qt window writes them. A second spelling would name a
        # different window from the one that opens.
        x_log = self.plot.x_log()
        y_log = self.plot.y_log()
        view_x0, view_x1 = self.plot.view_min_x(), self.plot.view_max_x()
        view_y0, view_y1 = self.plot.view_min_y(), self.plot.view_max_y()
        sx = self.plot.data_x_at(start[0])
        ex = self.plot.data_x_at(end[0])
        sy = self.plot.data_y_at(start[1])
        ey = self.plot.data_y_at(end[1])
        common_y = self.plot.line_count() == 0 or self.plot.own_count() < self.plot.line_count()

        def nx(value: float) -> str:
            return _axis_number(value, view_x0, view_x1, x_log)

        def ny(value: float) -> str:
            return _axis_number(value, view_y0, view_y1, y_log)

        if common_y:
            start_text = f"{nx(sx)}, {ny(sy)}"
            end_text = f"{nx(ex)}, {ny(ey)}"
            height_text = "∆" + ny(abs(ey - sy))
        else:
            start_text = nx(sx)
            end_text = nx(ex)
            height_text = ""
        width_text = "∆" + nx(abs(ex - sx))
        if self._band_artist is None:
            self._band_artist = Rectangle(
                (0, 0), 0, 0, facecolor=(0.7, 0.7, 0.7, 0.25), edgecolor=(0.35, 0.35, 0.35),
                linewidth=1, zorder=5,
            )
            self.ax.add_patch(self._band_artist)
        self._band_artist.set_xy((min(sx, ex), min(sy, ey)))
        self._band_artist.set_width(abs(ex - sx))
        self._band_artist.set_height(abs(ey - sy))
        self._band_artist.set_visible(True)
        for text in self._band_labels:
            text.remove()
        self._band_labels.clear()
        ink = {"color": "0.15", "fontsize": 9, "zorder": 6, "clip_on": False}
        self._band_labels.append(self.ax.annotate(
            start_text, xy=(sx, sy), xytext=(8, -8), textcoords="offset points",
            ha="left", va="top", **ink))
        self._band_labels.append(self.ax.annotate(
            end_text, xy=(ex, ey), xytext=(8, 8), textcoords="offset points",
            ha="left", va="bottom", **ink))
        if abs(end[0] - start[0]) >= 48:
            self._band_labels.append(self.ax.annotate(
                width_text, xy=((sx + ex) / 2.0, max(sy, ey)), xytext=(0, 6),
                textcoords="offset points", ha="center", va="bottom", **ink))
        if height_text and abs(end[1] - start[1]) >= 24:
            self._band_labels.append(self.ax.annotate(
                height_text, xy=(min(sx, ex), (sy + ey) / 2.0), xytext=(-6, 0),
                textcoords="offset points", ha="right", va="center", **ink))
        self.figure.canvas.draw_idle()

    def _clear_reading(self, draw: bool = False) -> None:
        had = self._readout is not None
        for artist in (self._hair_v, self._hair_h, self._hair_mark, self._readout):
            if artist is not None:
                artist.remove()
        self._hair_v = None
        self._hair_h = None
        self._hair_mark = None
        self._readout = None
        if draw and had:
            self.figure.canvas.draw_idle()

    def _show_reading(self, px: float, py: float) -> None:
        # The number is the sample that was drawn, on the axis that drew it.
        # A shifted line answers in its own y. The hair is that sample's pixel,
        # so it sits on the stroke rather than on the value under the pointer.
        from .widget import _reading_number, _y_span

        self._clear_reading()
        reading = self.plot.sample(px, py)
        if reading is None:
            self.figure.canvas.draw_idle()
            return
        hx = self.plot.data_x_at(reading.px)
        hy = self.plot.data_y_at(reading.py)
        self._hair_v = self.ax.axvline(hx, color="0.75", linewidth=0.8, zorder=4)
        self._hair_h = self.ax.axhline(hy, color="0.75", linewidth=0.8, zorder=4)
        colour = (reading.red / 255.0, reading.green / 255.0, reading.blue / 255.0)
        self._hair_mark, = self.ax.plot(
            [hx], [hy], marker="+", color=colour, markersize=8, linestyle="none", zorder=6)
        parts = [
            _reading_number(reading.x, self.plot.view_max_x() - self.plot.view_min_x(),
                            self.plot.x_log()),
            _reading_number(reading.y, _y_span(self.plot, reading.line),
                            self.plot.line_y_log(reading.line)),
        ]
        name = self.plot.line_name(reading.line)
        if name and self.plot.line_count() > 1:
            parts.insert(0, name)
        self._readout = self.ax.text(
            0.01, 0.98, "    ".join(parts), transform=self.ax.transAxes,
            ha="left", va="top", color="0.15", fontsize=9, zorder=7)
        self.figure.canvas.draw_idle()

    def _hide_band(self) -> None:
        if self._band_artist is not None:
            self._band_artist.set_visible(False)
        for text in self._band_labels:
            text.remove()
        self._band_labels.clear()
