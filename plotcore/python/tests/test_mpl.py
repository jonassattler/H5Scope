# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-FileCopyrightText: 2026 Andreas Martin
# SPDX-License-Identifier: GPL-3.0-only

"""matplotlib draws the folds plotcore made, on its own axes, and its events
are the figure's gestures."""

import numpy as np
import pytest

matplotlib = pytest.importorskip("matplotlib")
matplotlib.use("Agg")

from matplotlib.backend_bases import MouseEvent  # noqa: E402
from matplotlib.figure import Figure as MplFigure  # noqa: E402

import plotcore  # noqa: E402
from plotcore import mpl  # noqa: E402


def spiked(count=1_000_000, at=777_777):
    values = np.sin(np.arange(count) / 1000.0)
    values[at] = 50.0
    return values


def test_a_spike_one_sample_wide_survives_into_matplotlib():
    figure = plotcore.Figure()
    figure.add_line(spiked(), "spiked")
    ax = mpl.draw(figure, MplFigure().add_subplot(111), size=(640, 400))
    drawn = ax.get_lines()[0]
    assert np.nanmax(drawn.get_ydata()) == 50.0
    assert len(drawn.get_xdata()) < 10_000, "the fold, not the million samples"
    x0, x1, y0, y1 = figure.view
    assert ax.get_xlim() == pytest.approx((x0, x1))
    assert ax.get_ylim() == pytest.approx((y0, y1))


def test_matplotlib_takes_the_scales_the_figure_has():
    figure = plotcore.Figure()
    figure.add_line(10.0 ** np.linspace(-3, 3, 10_000), "decades")
    figure.set_index_axis(1.0, 1.0)
    figure.settings["xLog"] = True
    figure.settings["yLog"] = True
    figure.settings["yLogBase"] = 2.0
    figure.settings["title"] = "both logarithmic"
    ax = mpl.draw(figure, MplFigure().add_subplot(111), size=(640, 400))
    assert ax.get_xscale() == "log" and ax.get_yscale() == "log"
    assert ax.get_title() == "both logarithmic"


def test_an_axis_of_its_own_is_a_twin_with_that_window():
    figure = plotcore.Figure()
    figure.add_line(np.arange(1000.0), "common")
    figure.add_line(1e6 + np.arange(1000.0), "own")
    figure.line(1).settings["ownAxis"] = True
    figure.line(1).settings["axisLabel"] = "big"
    fig = MplFigure()
    ax = mpl.draw(figure, fig.add_subplot(111), size=(640, 400))
    twins = [a for a in fig.axes if a is not ax]
    assert len(twins) == 1
    low, high = twins[0].get_ylim()
    assert low < 1e6 < 1e6 + 999 < high
    assert twins[0].get_ylabel() == "big"
    assert np.nanmax(twins[0].get_lines()[0].get_ydata()) == pytest.approx(1e6 + 999)


def test_the_live_view_is_moved_by_the_figure_s_gestures():
    figure = plotcore.Figure()
    figure.add_line(spiked(100_000, 77_777), "spiked")
    fig = MplFigure(figsize=(8, 5), dpi=100)
    from matplotlib.backends.backend_agg import FigureCanvasAgg

    FigureCanvasAgg(fig)
    view = mpl.View(figure, fig)
    view.redraw()
    before = figure.view
    box = view.ax.get_window_extent()
    middle = ((box.x0 + box.x1) / 2, (box.y0 + box.y1) / 2)
    scroll = MouseEvent("scroll_event", fig.canvas, *middle, step=2)
    fig.canvas.callbacks.process("scroll_event", scroll)
    zoomed = figure.view
    assert zoomed[1] - zoomed[0] < before[1] - before[0]
    assert view.ax.get_xlim() == pytest.approx(zoomed[:2])
    for name, x, button in (("button_press_event", middle[0], 1),
                            ("motion_notify_event", middle[0] + 50, 1),
                            ("button_release_event", middle[0] + 50, 1)):
        fig.canvas.callbacks.process(name, MouseEvent(name, fig.canvas, x, middle[1],
                                                      button=button))
    assert figure.view[0] < zoomed[0], "a drag to the right shows what is to the left"
