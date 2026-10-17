# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-FileCopyrightText: 2026 Andreas Martin
# SPDX-License-Identifier: GPL-3.0-only

"""The gestures, through the Python surface, checked in the pixels they moved."""

import numpy as np
import pytest

from plotcore import Button, Figure, Modifiers


def figure_of(*lines):
    figure = Figure()
    for i, values in enumerate(lines):
        figure.add_line(values, f"line {i}")
    figure.set_size(800, 500)
    figure.settings["legend"] = False
    return figure


def middle(scene):
    a = scene.area
    return a.x + a.width / 2, a.y + a.height / 2


def test_the_wheel_zooms_about_the_pointer_and_a_double_click_puts_it_back():
    figure = figure_of(np.cumsum(np.random.default_rng(1).normal(size=100_000)))
    before = figure.scene()
    x, y = middle(before)
    figure.wheel(x, y, 360)
    zoomed = figure.scene()
    assert zoomed.view[1] - zoomed.view[0] < before.view[1] - before.view[0]
    figure.wheel(x, y, 120, Modifiers.SHIFT)
    across = figure.scene()
    assert across.view[2:] == zoomed.view[2:], "Shift zooms across alone"
    figure.double_click(x, y, Button.LEFT)
    assert figure.scene().view == before.view


def test_a_drag_pans_and_a_right_drag_zooms_to_its_band():
    figure = figure_of(np.arange(1000.0))
    before = figure.scene()
    x, y = middle(before)
    figure.press(x, y)
    figure.move(x + 50, y)
    figure.release(x + 50, y)
    panned = figure.scene()
    span = before.view[1] - before.view[0]
    assert panned.view[0] == pytest.approx(before.view[0] - 50 / before.area.width * span)

    figure.press(x - 100, y - 50, Button.RIGHT)
    figure.move(x + 100, y + 50)
    assert figure.scene().band is not None
    figure.release(x + 100, y + 50, Button.RIGHT)
    banded = figure.scene()
    assert banded.band is None
    assert banded.view[1] - banded.view[0] == pytest.approx(200 / before.area.width * span)


def test_a_scaled_line_keeps_its_sample_under_the_pointer():
    sine = 1000 * np.sin(np.arange(400) / 30.0) + 5000
    figure = figure_of(np.arange(400.0), sine)
    first = figure.scene()
    x, y = first.lines[1].runs[0][200]
    figure.wheel(x, y, 240, Modifiers.ALT | Modifiers.CONTROL)
    scaled = figure.scene()
    assert figure.line(1).settings["scale"] > 1.0
    assert figure.line(0).settings["scale"] == 1.0
    assert scaled.lines[1].runs[0][200][1] == pytest.approx(y, abs=1e-6)
    # Control and a click put it back.
    figure.press(x, y, Button.LEFT, Modifiers.CONTROL)
    figure.release(x, y, Button.LEFT, Modifiers.CONTROL)
    assert figure.line(1).settings["scale"] == 1.0
    assert figure.scene().lines[1].runs[0][150][1] == pytest.approx(first.lines[1].runs[0][150][1])


def test_alt_and_a_drag_move_one_line_and_the_pane_stays_put():
    figure = figure_of(np.arange(300.0), np.arange(300.0)[::-1].copy())
    first = figure.scene()
    x, y = first.lines[1].runs[0][50]
    figure.press(x, y, Button.LEFT, Modifiers.ALT)
    figure.move(x, y + 40)
    figure.release(x, y + 40)
    moved = figure.scene()
    assert moved.lines[1].runs[0][50][1] == pytest.approx(y + 40)
    assert moved.lines[0].runs[0][50][1] == pytest.approx(first.lines[0].runs[0][50][1])
    assert moved.area == first.area
    assert moved.lines[1].own and not moved.lines[1].numbered


def test_the_crosshair_reads_the_sample_under_the_pointer():
    values = 10 + 0.5 * np.arange(300.0)
    figure = figure_of(values)
    first = figure.scene()
    x, y = first.lines[0].runs[0][120]
    figure.hover(x + 1, y - 1)
    reading = figure.scene().reading
    assert reading.valid and reading.line == 0
    assert (reading.x, reading.y) == (120.0, values[120])
    assert reading.x_text and reading.y_text
    figure.leave()
    assert not figure.scene().reading.valid


def test_a_flipped_figure_names_the_pane_s_directions():
    figure = figure_of(np.arange(200.0) * 2)
    figure.settings["flipped"] = True
    before = figure.scene()
    run = before.lines[0].runs[0]
    assert run[0][1] > run[-1][1], "x runs up the pane"
    x, y = middle(before)
    figure.wheel(x, y, 120, Modifiers.SHIFT)
    after = figure.scene()
    assert after.view[:2] == before.view[:2], "Shift is across, which is y"
    assert after.view[3] - after.view[2] < before.view[3] - before.view[2]


def test_stacked_lines_each_fill_a_band():
    figure = figure_of(np.sin(np.arange(500) / 10), np.arange(500.0), np.arange(500.0) ** 2)
    figure.settings["stacked"] = True
    scene = figure.scene()
    a = scene.area
    for k, line in enumerate(scene.lines):
        ys = line.runs[0][:, 1]
        assert ys.min() >= a.y + k * a.height / 3 - 1e-6
        assert ys.max() <= a.y + (k + 1) * a.height / 3 + 1e-6


def test_a_logarithmic_axis_with_nothing_above_zero_says_so():
    figure = figure_of(np.arange(-100.0, 0.0))
    figure.settings["yLog"] = True
    scene = figure.scene()
    assert scene.message
    x, y = middle(scene)
    figure.wheel(x, y, 600)
    assert figure.scene().view == scene.view
