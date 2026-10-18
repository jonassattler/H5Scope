# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-FileCopyrightText: 2026 Andreas Martin
# SPDX-License-Identifier: GPL-3.0-only

"""The Python surface over the C interface: what goes in is copied, what is
named is set, what fails says so, and what is lent comes back."""

import gc

import numpy as np
import pytest

import plotcore
from plotcore import Figure
from plotcore.figure import _readers


def figure_of(*lines, width=800, height=500):
    figure = Figure()
    for i, values in enumerate(lines):
        figure.add_line(values, f"line {i}")
    figure.set_size(width, height)
    figure.settings["legend"] = False
    return figure


def test_the_library_is_the_one_this_package_speaks_to():
    assert plotcore.abi_version() == 1
    assert plotcore.version() == "0.1.0"
    assert plotcore.palette_names() == ["okabe-ito", "tol bright", "tol muted", "spectrum", "safe"]
    assert plotcore.ramp_names()[1] == "viridis"
    okabe = plotcore.palette("okabe-ito")
    assert len(okabe) == 8 and okabe[0][:3] == pytest.approx((230 / 255, 159 / 255, 0.0))
    # The achromatic entry is last, and white on the dark ground.
    assert okabe[-1][:3] == (1.0, 1.0, 1.0)
    assert plotcore.palette("okabe-ito", on_dark=False)[-1][:3] == (0.0, 0.0, 0.0)
    assert plotcore.palette("plaid") is None


def test_values_are_copied_in_and_the_caller_may_reuse_them():
    values = np.arange(1000, dtype=np.float64) % 100
    figure = figure_of(values)
    values[:] = -1.0
    assert figure.line(0).extent == (0.0, 99.0)
    assert figure.line(0).length == 1000
    assert figure.line(0).name == "line 0"
    # Not a float64 array at all: converted, then copied.
    figure.add_line([1, 2, 3])
    assert figure.line(1).extent == (1.0, 3.0)


def test_a_line_streamed_in_is_drawn_once_it_is_whole():
    figure = figure_of(np.zeros(10))
    line = figure.begin_line(300_000, "streamed")
    assert not figure.line(line).ready
    for start in range(0, 300_000, 70_000):
        figure.append_line(line, np.arange(start, min(start + 70_000, 300_000)))
    assert len(figure.scene().lines) == 1
    figure.finish_line(line)
    assert figure.line(line).ready
    assert len(figure.scene().lines) == 2
    with pytest.raises(plotcore.Error) as refused:
        figure.append_line(line, [1.0])
    assert refused.value.status == 3


def test_settings_are_named_and_read_back_as_written():
    figure = figure_of(np.arange(10.0))
    figure.settings["xLog"] = True
    figure.settings["xLogBase"] = 2.0
    figure.settings["title"] = "a title"
    figure.settings["grid"] = "dense"
    figure.settings["colours.single"] = (0.1, 0.2, 0.3)
    assert figure.settings["xLog"] is True
    assert figure.settings["xLogBase"] == 2.0
    assert figure.settings["title"] == "a title"
    assert figure.settings["grid"] == "dense"
    assert figure.settings["colours.single"] == pytest.approx((0.1, 0.2, 0.3, 1.0))
    figure.style["tickPixels"] = 12
    assert figure.style["tickPixels"] == 12
    line = figure.line(0)
    line.settings["ownAxis"] = True
    line.settings["axisLabel"] = "volts"
    line.settings["scale"] = 2.0
    assert line.settings["ownAxis"] is True
    assert line.settings["axisLabel"] == "volts"
    assert line.settings["scale"] == 2.0
    line.colour = (1.0, 0.0, 0.0)
    assert line.colour == (1.0, 0.0, 0.0, 1.0)
    line.colour = None
    assert line.colour is None


def test_what_cannot_be_done_is_an_error_that_says_why():
    figure = figure_of(np.arange(10.0))
    with pytest.raises(plotcore.Error, match="no such setting"):
        figure.settings["noSuchThing"] = 1
    with pytest.raises(plotcore.Error, match="grid"):
        figure.settings["grid"] = "tartan"
    with pytest.raises(plotcore.Error, match="base"):
        figure.settings["xLogBase"] = 1.0
    with pytest.raises(IndexError):
        figure.line(5)
    with pytest.raises(KeyError):
        _ = figure.settings["noSuchThing"]
    with pytest.raises(TypeError):
        figure.settings["title"] = object()
    with pytest.raises(ValueError):
        figure.set_appetite("ravenous")


def test_a_reader_is_asked_below_the_base_and_let_go_exactly_once():
    values = np.arange(1 << 20, dtype=np.float64)
    figure = figure_of(values)
    figure.budget_bytes = 1 << 20
    figure.remove_line(0)
    figure.add_line(values, "tight")
    assert figure.line(0).base_bucket > 1
    asked = []

    def reader(first, count):
        asked.append((first, count))
        return values[first:first + count]

    figure.line(0).set_reader(reader)
    assert len(_readers) == 1
    figure.set_view(4000, 4100, 0, 1 << 20)
    scene = figure.scene()
    assert asked, "a window under the base is read"
    assert scene.lines[0].values[0][:, 1] == pytest.approx(scene.lines[0].values[0][:, 0], abs=1e-6)
    figure.remove_line(0)
    assert len(_readers) == 0, "removing the line lets its reader go"

    figure.add_line(values, "again")
    figure.line(0).set_reader(reader)
    figure.close()
    gc.collect()
    assert len(_readers) == 0, "closing the figure lets its readers go"


def test_a_reader_that_fails_is_a_closer_look_not_taken():
    values = np.arange(1 << 20, dtype=np.float64)
    figure = figure_of()
    figure.budget_bytes = 1 << 20
    figure.add_line(values, "tight")

    def broken(first, count):
        raise RuntimeError("the file went away")

    figure.line(0).set_reader(broken)
    figure.set_view(4000, 4100, 0, 1 << 20)
    scene = figure.scene()
    assert scene.lines and scene.lines[0].runs


def test_the_host_s_measure_lays_the_frame_out():
    figure = figure_of(np.arange(1000.0))
    measured = []

    def width(text, role):
        measured.append((text, role))
        return 30.0 * len(text)

    figure.set_text_measure(width, lambda role: 20.0)
    wide = figure.scene()
    figure.set_text_measure(None)
    narrow = figure.scene()
    assert measured and isinstance(measured[0][1], plotcore.TextRole)
    assert wide.area.x > narrow.area.x, "wider numbers take a wider gutter"


def test_a_view_written_down_reads_back():
    figure = figure_of(np.sin(np.arange(20_000) / 100.0), np.arange(20_000.0))
    figure.settings["title"] = "kept"
    figure.line(1).settings["ownAxis"] = True
    figure.wheel(400, 250, 360)
    state = figure.state()
    view = figure.view
    figure.reset_view()
    figure.settings["title"] = "changed"
    figure.restore(state)
    assert figure.view == view
    assert figure.settings["title"] == "kept"
    assert figure.line(1).settings["ownAxis"] is True
    with pytest.raises(ValueError):
        figure.restore("not a view")


def test_a_scene_outlives_its_figure_and_holds_numpy():
    figure = figure_of(np.arange(5000.0))
    scene = figure.scene()
    figure.close()
    gc.collect()
    line = scene.lines[0]
    assert line.runs[0].shape[1] == 2 and line.runs[0].dtype == np.float64
    assert line.triangles.shape[0] % 3 == 0
    assert scene.rules.dtype.names[:4] == ("x0", "y0", "x1", "y1")
    assert any(text.text == "0" for text in scene.texts)
    assert scene.chrome.ink[:3] == (1.0, 1.0, 1.0)
