# SPDX-FileCopyrightText: 2026 Andreas Martin
# SPDX-License-Identifier: GPL-3.0-only

"""ctypes loader for h5plot.dll. The numpy buffer must stay alive."""

from __future__ import annotations

import ctypes
import sys
from ctypes import POINTER, c_double, c_int, c_longlong, c_void_p
from pathlib import Path


class H5PlotRun(ctypes.Structure):
    _fields_ = [
        ("first", c_int),
        ("count", c_int),
        ("red", ctypes.c_ubyte),
        ("green", ctypes.c_ubyte),
        ("blue", ctypes.c_ubyte),
        ("alpha", ctypes.c_ubyte),
        ("width", ctypes.c_float),
        ("summarised", ctypes.c_ubyte),
    ]


class H5PlotSample(ctypes.Structure):
    _fields_ = [
        ("line", c_int),
        ("x", c_double),
        ("y", c_double),
        ("px", c_double),
        ("py", c_double),
        ("red", ctypes.c_ubyte),
        ("green", ctypes.c_ubyte),
        ("blue", ctypes.c_ubyte),
        ("alpha", ctypes.c_ubyte),
    ]


class H5PlotTick(ctypes.Structure):
    _fields_ = [
        ("x", c_double),
        ("y", c_double),
        ("value", c_double),
        ("axis", c_int),
        ("series", c_int),
        ("red", ctypes.c_ubyte),
        ("green", ctypes.c_ubyte),
        ("blue", ctypes.c_ubyte),
        ("alpha", ctypes.c_ubyte),
        ("labeled", ctypes.c_ubyte),
        ("logarithmic", ctypes.c_ubyte),
    ]


# The same cycle LineStore uses when a line brings no colour of its own.
_CYCLE = (
    (230, 159, 0),
    (86, 180, 233),
    (0, 158, 115),
    (240, 228, 66),
    (0, 114, 178),
    (213, 94, 0),
    (204, 121, 167),
)


def _library_names() -> tuple[str, ...]:
    # One name per platform, the one CMake's prefix and suffix produce:
    # h5plot.dll, libh5plot.so, libh5plot.dylib. A second spelling would
    # load whichever file happened to be left over from another build.
    if sys.platform == "win32":
        return ("h5plot.dll",)
    if sys.platform == "darwin":
        return ("libh5plot.dylib",)
    return ("libh5plot.so",)


def _load() -> ctypes.CDLL:
    here = Path(__file__).resolve().parent
    names = _library_names()
    for name in names:
        path = here / name
        if path.exists():
            return ctypes.CDLL(str(path))
    raise FileNotFoundError(
        f"{names[0]} not next to {here}. Build it with python/h5plot/build.bat, "
        "or install the wheel."
    )


_lib = _load()
_lib.h5plot_create.restype = c_void_p
_lib.h5plot_destroy.argtypes = [c_void_p]
_lib.h5plot_add_line.argtypes = [c_void_p, ctypes.POINTER(c_double), c_longlong, c_int, c_int, c_int]
_lib.h5plot_add_line.restype = c_int
_lib.h5plot_begin_line.argtypes = [c_void_p, c_longlong, c_int, c_int, c_int]
_lib.h5plot_begin_line.restype = c_int
_lib.h5plot_add_samples.argtypes = [c_void_p, c_int, ctypes.POINTER(c_double), c_longlong]
_lib.h5plot_finish_line.argtypes = [c_void_p, c_int]
_READ = ctypes.CFUNCTYPE(c_int, c_void_p, c_longlong, c_longlong, POINTER(c_double))
_lib.h5plot_set_reader.argtypes = [c_void_p, c_int, _READ, c_void_p]
_lib.h5plot_set_axis.argtypes = [c_void_p, POINTER(c_double), c_longlong]
_lib.h5plot_set_line_axis.argtypes = [c_void_p, c_int, POINTER(c_double), c_longlong]
_lib.h5plot_set_line_axis.restype = c_int
_lib.h5plot_begin_line_axis.argtypes = [c_void_p, c_int, c_longlong]
_lib.h5plot_begin_line_axis.restype = c_int
_lib.h5plot_add_line_axis_samples.argtypes = [c_void_p, c_int, POINTER(c_double), c_longlong]
_lib.h5plot_finish_line_axis.argtypes = [c_void_p, c_int]
_lib.h5plot_finish_line_axis.restype = c_int
_lib.h5plot_set_line_axis_reader.argtypes = [c_void_p, c_int, _READ, c_void_p]
_lib.h5plot_begin_axis.argtypes = [c_void_p, c_longlong]
_lib.h5plot_add_axis_samples.argtypes = [c_void_p, POINTER(c_double), c_longlong]
_lib.h5plot_finish_axis.argtypes = [c_void_p]
_lib.h5plot_set_axis_reader.argtypes = [c_void_p, _READ, c_void_p]
_lib.h5plot_clear.argtypes = [c_void_p]
_lib.h5plot_set_pane.argtypes = [c_void_p, c_int, c_int, c_double]
_lib.h5plot_set_ylog.argtypes = [c_void_p, c_int]
_lib.h5plot_set_xlog.argtypes = [c_void_p, c_int]
_lib.h5plot_set_x_log_base.argtypes = [c_void_p, c_double]
_lib.h5plot_set_y_log_base.argtypes = [c_void_p, c_double]
_lib.h5plot_x_log_base.argtypes = [c_void_p]
_lib.h5plot_x_log_base.restype = c_double
_lib.h5plot_y_log_base.argtypes = [c_void_p]
_lib.h5plot_y_log_base.restype = c_double
_lib.h5plot_reset_view.argtypes = [c_void_p]
_lib.h5plot_wheel.argtypes = [c_void_p, c_double, c_double, c_double, c_int, c_int, c_int]
_lib.h5plot_pan.argtypes = [c_void_p, c_double, c_double]
_lib.h5plot_zoom_rect.argtypes = [c_void_p, c_double, c_double, c_double, c_double]
_lib.h5plot_zoom_rect.restype = c_int
_lib.h5plot_set_range.argtypes = [c_void_p, c_double, c_double, c_double, c_double]
_lib.h5plot_stack_lines.argtypes = [c_void_p]
_lib.h5plot_set_own_axis.argtypes = [c_void_p, c_int, c_int]
_lib.h5plot_line_count.argtypes = [c_void_p]
_lib.h5plot_line_count.restype = c_int
_lib.h5plot_own_axis.argtypes = [c_void_p, c_int]
_lib.h5plot_own_axis.restype = c_int
_lib.h5plot_own_count.argtypes = [c_void_p]
_lib.h5plot_own_count.restype = c_int
_lib.h5plot_shared_count.argtypes = [c_void_p]
_lib.h5plot_shared_count.restype = c_int
_lib.h5plot_nearest.argtypes = [c_void_p, c_double, c_double]
_lib.h5plot_nearest.restype = c_int
_lib.h5plot_sample.argtypes = [c_void_p, c_double, c_double, POINTER(H5PlotSample)]
_lib.h5plot_sample.restype = c_int
_lib.h5plot_shift_line.argtypes = [c_void_p, c_int, c_double, c_double]
_lib.h5plot_view_min_x.argtypes = [c_void_p]
_lib.h5plot_view_min_x.restype = c_double
_lib.h5plot_view_max_x.argtypes = [c_void_p]
_lib.h5plot_view_max_x.restype = c_double
_lib.h5plot_view_min_y.argtypes = [c_void_p]
_lib.h5plot_view_min_y.restype = c_double
_lib.h5plot_view_max_y.argtypes = [c_void_p]
_lib.h5plot_view_max_y.restype = c_double
_lib.h5plot_data_x_at.argtypes = [c_void_p, c_double]
_lib.h5plot_data_x_at.restype = c_double
_lib.h5plot_data_y_at.argtypes = [c_void_p, c_double]
_lib.h5plot_data_y_at.restype = c_double
_lib.h5plot_project.argtypes = [c_void_p]
_lib.h5plot_project.restype = c_int
_lib.h5plot_point_count.argtypes = [c_void_p]
_lib.h5plot_point_count.restype = c_int
_lib.h5plot_copy_points.argtypes = [c_void_p, POINTER(c_double)]
_lib.h5plot_run_count.argtypes = [c_void_p]
_lib.h5plot_run_count.restype = c_int
_lib.h5plot_copy_runs.argtypes = [c_void_p, POINTER(H5PlotRun)]
_lib.h5plot_tick_count.argtypes = [c_void_p]
_lib.h5plot_tick_count.restype = c_int
_lib.h5plot_copy_ticks.argtypes = [c_void_p, POINTER(H5PlotTick)]
_lib.h5plot_copy_data.argtypes = [c_void_p, POINTER(c_double)]
_lib.h5plot_copy_run_lines.argtypes = [c_void_p, POINTER(c_int)]
_lib.h5plot_x_log.argtypes = [c_void_p]
_lib.h5plot_x_log.restype = c_int
_lib.h5plot_y_log.argtypes = [c_void_p]
_lib.h5plot_y_log.restype = c_int
_lib.h5plot_set_line_y_log.argtypes = [c_void_p, c_int, c_int]
_lib.h5plot_line_y_log.argtypes = [c_void_p, c_int]
_lib.h5plot_line_y_log.restype = c_int
_lib.h5plot_line_y_range.argtypes = [c_void_p, c_int, POINTER(c_double), POINTER(c_double)]


class Plot:
    def __init__(self) -> None:
        self._handle = _lib.h5plot_create()
        if not self._handle:
            raise RuntimeError("h5plot_create failed")
        self._keep: list = []
        self._names: list = []
        self._colours: list = []
        self._x = None
        self._line_x: dict = {}
        self._sources: dict = {}
        self._tokens: dict = {}
        self._read_cb = _READ(self._read_window)

    def close(self) -> None:
        if self._handle:
            _lib.h5plot_destroy(self._handle)
            self._handle = None
        for handle, _data in self._sources.values():
            handle.close()
        self._sources.clear()
        self._tokens.clear()
        self._keep.clear()
        self._names.clear()
        self._colours.clear()
        self._x = None
        self._line_x.clear()

    def __del__(self) -> None:
        self.close()

    def add_line(self, y, colour=None, name=None) -> int:
        import numpy as np

        if colour is None:
            colour = _CYCLE[len(self._keep) % len(_CYCLE)]
        array = np.ascontiguousarray(y, dtype=np.float64)
        self._keep.append(array)
        self._remember(name, colour)
        ptr = array.ctypes.data_as(POINTER(c_double))
        return int(_lib.h5plot_add_line(self._handle, ptr, array.size, colour[0], colour[1], colour[2]))

    def set_x(self, x) -> None:
        """Borrow one x per sample. None puts the sample index back.

        The array has to go one way. A range of x is then a range of
        positions, and a zoom reads that run. One that doubles back is drawn,
        and a zoom stretches the summary.
        """
        import numpy as np

        if x is None:
            self._drop_x_file()
            self._x = None
            _lib.h5plot_set_axis(self._handle, None, 0)
            return
        self._drop_x_file()
        array = np.ascontiguousarray(x, dtype=np.float64)
        self._x = array
        _lib.h5plot_set_axis(self._handle, array.ctypes.data_as(POINTER(c_double)), array.size)

    def set_x_hdf5(self, path, dataset: str) -> None:
        """Stream a 1-D numeric dataset in as the shared x.

        Folded in the same pieces as a curve, then the chunk is dropped.
        The file stays open so a closer look can read that window back.
        The values have to go one way, as with `set_x`.
        """
        import h5py
        import numpy as np

        self._drop_x_file()
        self._x = None
        handle = h5py.File(path, "r")
        try:
            data = handle[dataset]
            if getattr(data, "ndim", None) != 1 or not np.issubdtype(data.dtype, np.number):
                raise ValueError(f"{dataset} is not a 1-D numeric dataset")
            count = int(data.shape[0])
            if count < 2:
                raise ValueError(f"{dataset} has fewer than two samples")
            _lib.h5plot_begin_axis(self._handle, count)
            step = 1 << 16
            for start in range(0, count, step):
                block = np.ascontiguousarray(data[start:start + step], dtype=np.float64)
                _lib.h5plot_add_axis_samples(self._handle, block.ctypes.data_as(POINTER(c_double)),
                                             int(block.size))
            _lib.h5plot_finish_axis(self._handle)
        except Exception:
            handle.close()
            raise
        self._sources["x"] = (handle, data)
        token = (self, "x")
        self._tokens["x"] = token
        _lib.h5plot_set_axis_reader(self._handle, self._read_cb, c_void_p(id(token)))

    def set_line_x(self, index: int, x) -> None:
        """This line's own time. None puts the line back on the shared x.

        One value per sample of this line, and at least two. A different
        length is refused: index i of the time is sample i. The numbered x
        stays one window, and the line is drawn where its timestamps fall
        in it. The array has to go one way, as with `set_x`.
        """
        import numpy as np

        index = int(index)
        if x is None:
            _lib.h5plot_set_line_axis(self._handle, index, None, 0)
            self._drop_line_x(index)
            return
        array = np.ascontiguousarray(x, dtype=np.float64)
        # The previous buffer stays alive until this returns: the store drops
        # its borrow inside the call, and only then may the old array go.
        if not _lib.h5plot_set_line_axis(
            self._handle, index, array.ctypes.data_as(POINTER(c_double)), array.size
        ):
            raise ValueError("x must have one sample per value of this line, and at least two")
        self._drop_line_x(index)
        self._line_x[index] = array

    def set_line_x_hdf5(self, index: int, path, dataset: str) -> None:
        """Stream a 1-D numeric dataset in as this line's own time.

        Folded in the same pieces as `set_x_hdf5`. The length has to be this
        line's. The file stays open so a closer look can read that window back.
        """
        import h5py
        import numpy as np

        index = int(index)
        handle = h5py.File(path, "r")
        started = False
        try:
            data = handle[dataset]
            if getattr(data, "ndim", None) != 1 or not np.issubdtype(data.dtype, np.number):
                raise ValueError(f"{dataset} is not a 1-D numeric dataset")
            count = int(data.shape[0])
            if not _lib.h5plot_begin_line_axis(self._handle, index, count):
                raise ValueError("x must have one sample per value of this line, and at least two")
            started = True
            step = 1 << 16
            for start in range(0, count, step):
                block = np.ascontiguousarray(data[start:start + step], dtype=np.float64)
                _lib.h5plot_add_line_axis_samples(
                    self._handle, index, block.ctypes.data_as(POINTER(c_double)), int(block.size)
                )
            if not _lib.h5plot_finish_line_axis(self._handle, index):
                raise ValueError("x must have one sample per value of this line, and at least two")
        except Exception:
            handle.close()
            if started:
                self._drop_line_x(index)
            raise
        self._drop_line_x(index)
        key = ("line-x", index)
        self._sources[key] = (handle, data)
        token = (self, key)
        self._tokens[key] = token
        _lib.h5plot_set_line_axis_reader(self._handle, index, self._read_cb, c_void_p(id(token)))

    def _drop_line_x(self, index: int) -> None:
        self._line_x.pop(index, None)
        key = ("line-x", index)
        held = self._sources.pop(key, None)
        self._tokens.pop(key, None)
        if held is not None:
            held[0].close()

    def _drop_x_file(self) -> None:
        held = self._sources.pop("x", None)
        self._tokens.pop("x", None)
        if held is not None:
            held[0].close()

    def add_hdf5(self, path, dataset: str, colour=None, name=None) -> int:
        """Stream a 1-D numeric dataset into the pyramid, one read at a time.

        The chunk is the viewer's read (`kReadRun`, 65536). It is converted
        and folded, then dropped. The file stays open: a closer look finer
        than the pyramid's base reads that window back from it.
        """
        import h5py
        import numpy as np

        if colour is None:
            colour = _CYCLE[len(self._keep) % len(_CYCLE)]
        handle = h5py.File(path, "r")
        try:
            data = handle[dataset]
            if getattr(data, "ndim", None) != 1 or not np.issubdtype(data.dtype, np.number):
                raise ValueError(f"{dataset} is not a 1-D numeric dataset")
            count = int(data.shape[0])
            index = int(_lib.h5plot_begin_line(self._handle, count, colour[0], colour[1], colour[2]))
            if index < 0:
                raise RuntimeError("h5plot_begin_line failed")
            self._keep.append(None)
            self._remember(name, colour)
            step = 1 << 16
            for start in range(0, count, step):
                block = np.ascontiguousarray(data[start:start + step], dtype=np.float64)
                _lib.h5plot_add_samples(self._handle, index, block.ctypes.data_as(POINTER(c_double)),
                                        int(block.size))
            _lib.h5plot_finish_line(self._handle, index)
        except Exception:
            handle.close()
            raise
        self._sources[index] = (handle, data)
        token = (self, index)
        self._tokens[index] = token
        # id() is the PyObject*. The token stays in _tokens, which is the
        # reference that keeps that pointer live for the callback.
        _lib.h5plot_set_reader(self._handle, index, self._read_cb, c_void_p(id(token)))
        return index

    def _read_window(self, user, first: int, count: int, out) -> int:
        import numpy as np

        try:
            _plot, index = ctypes.cast(user, ctypes.py_object).value
            _handle, data = self._sources[index]
            block = np.ascontiguousarray(data[int(first):int(first) + int(count)], dtype=np.float64)
            if block.size != int(count):
                return 0
            ctypes.memmove(out, int(block.ctypes.data), int(count) * 8)
            return 1
        except Exception:
            return 0

    def clear(self) -> None:
        _lib.h5plot_clear(self._handle)
        for handle, _data in self._sources.values():
            handle.close()
        self._sources.clear()
        self._tokens.clear()
        self._keep.clear()
        self._names.clear()
        self._colours.clear()
        self._x = None
        self._line_x.clear()

    def line_name(self, index: int):
        if index < 0 or index >= len(self._names):
            return None
        return self._names[index]

    def line_colour(self, index: int):
        if index < 0 or index >= len(self._colours):
            return None
        return self._colours[index]

    def named_lines(self):
        """Lines that have a name, in the order they were added, with the colour they were drawn in."""
        return [
            (index, self._names[index], self._colours[index])
            for index in range(len(self._names))
            if self._names[index]
        ]

    def _remember(self, name, colour) -> None:
        text = str(name).strip() if name else ""
        self._names.append(text or None)
        self._colours.append((int(colour[0]), int(colour[1]), int(colour[2])))

    def set_pane(self, width: int, height: int, pixel_ratio: float = 1.0) -> None:
        _lib.h5plot_set_pane(self._handle, int(width), int(height), float(pixel_ratio))

    def set_y_log(self, on: bool) -> None:
        _lib.h5plot_set_ylog(self._handle, 1 if on else 0)

    def set_x_log(self, on: bool) -> None:
        _lib.h5plot_set_xlog(self._handle, 1 if on else 0)

    def _base(self, base: float) -> float:
        import math

        value = float(base)
        if not math.isfinite(value) or not value > 1.0:
            raise ValueError("a logarithmic base is a number above one")
        return value

    def set_x_log_base(self, base: float) -> None:
        """Number the powers of a logarithmic x. A base at or below one is refused.

        Where a point sits does not change: it is a ratio of two logarithms,
        and the base cancels. The ticks are the powers of this base.
        """
        _lib.h5plot_set_x_log_base(self._handle, self._base(base))

    def set_y_log_base(self, base: float) -> None:
        _lib.h5plot_set_y_log_base(self._handle, self._base(base))

    def x_log_base(self) -> float:
        return float(_lib.h5plot_x_log_base(self._handle))

    def y_log_base(self) -> float:
        return float(_lib.h5plot_y_log_base(self._handle))

    def reset_view(self) -> None:
        _lib.h5plot_reset_view(self._handle)

    def wheel(self, px: float, py: float, factor: float, shift: bool, control: bool,
              alt: bool = False) -> None:
        """Zoom about a pane-local pixel. A factor above one zooms in.

        Shift alone is x, Ctrl alone is y, and both together is both.
        Alt with Ctrl scales only the curve nearest the pointer, in y,
        and the value under the pointer stays where it is.
        """
        _lib.h5plot_wheel(self._handle, px, py, factor, 1 if shift else 0, 1 if control else 0,
                          1 if alt else 0)

    def pan(self, dx: float, dy: float) -> None:
        _lib.h5plot_pan(self._handle, dx, dy)

    def zoom_rect(self, x0: float, y0: float, x1: float, y1: float) -> bool:
        return bool(_lib.h5plot_zoom_rect(self._handle, x0, y0, x1, y1))

    def set_range(self, x0=None, x1=None, y0=None, y1=None) -> None:
        """Open the window on these values. An axis left out stays as it is.

        On a logarithmic axis a bound at or below zero is not a place, so it
        is clipped to the part of the axis that exists.
        """
        if x0 is None:
            x0 = self.view_min_x()
        if x1 is None:
            x1 = self.view_max_x()
        if y0 is None:
            y0 = self.view_min_y()
        if y1 is None:
            y1 = self.view_max_y()
        _lib.h5plot_set_range(self._handle, float(x0), float(x1), float(y0), float(y1))

    def line_count(self) -> int:
        return int(_lib.h5plot_line_count(self._handle))

    def stack_lines(self) -> None:
        """Lay every line in an equal band, the first at the top.

        Each band is that line's own padded span. Y returns to the whole
        axis; x stays. A line a logarithmic axis cannot draw is left as it
        was. The numbers beside a stacked line are that line's values.
        """
        _lib.h5plot_stack_lines(self._handle)

    def set_own_axis(self, index: int, on: bool) -> None:
        _lib.h5plot_set_own_axis(self._handle, int(index), 1 if on else 0)

    def own_axis(self, index: int) -> bool:
        return bool(_lib.h5plot_own_axis(self._handle, int(index)))

    def own_count(self) -> int:
        return int(_lib.h5plot_own_count(self._handle))

    def shared_count(self) -> int:
        return int(_lib.h5plot_shared_count(self._handle))

    def nearest(self, px: float, py: float) -> int:
        return int(_lib.h5plot_nearest(self._handle, px, py))

    def sample(self, px: float, py: float):
        """The drawn sample closest to a pane-local pixel, or None.

        x and y are that sample's values on the axis the line was drawn
        against. A shifted line answers in its own y.
        """
        found = H5PlotSample()
        if not _lib.h5plot_sample(self._handle, float(px), float(py), found):
            return None
        return found

    def shift_line(self, index: int, dx: float, dy: float) -> None:
        _lib.h5plot_shift_line(self._handle, int(index), dx, dy)

    def view_min_x(self) -> float:
        return float(_lib.h5plot_view_min_x(self._handle))

    def view_max_x(self) -> float:
        return float(_lib.h5plot_view_max_x(self._handle))

    def view_min_y(self) -> float:
        return float(_lib.h5plot_view_min_y(self._handle))

    def view_max_y(self) -> float:
        return float(_lib.h5plot_view_max_y(self._handle))

    def data_x_at(self, px: float) -> float:
        return float(_lib.h5plot_data_x_at(self._handle, px))

    def data_y_at(self, py: float) -> float:
        return float(_lib.h5plot_data_y_at(self._handle, py))

    def project(self):
        n = int(_lib.h5plot_project(self._handle))
        import numpy as np

        xy = np.empty(n * 2, dtype=np.float64)
        if n:
            _lib.h5plot_copy_points(self._handle, xy.ctypes.data_as(POINTER(c_double)))
        runs_n = int(_lib.h5plot_run_count(self._handle))
        runs = (H5PlotRun * max(runs_n, 1))()
        if runs_n:
            _lib.h5plot_copy_runs(self._handle, runs)
        tick_n = int(_lib.h5plot_tick_count(self._handle))
        ticks = (H5PlotTick * max(tick_n, 1))()
        if tick_n:
            _lib.h5plot_copy_ticks(self._handle, ticks)
        points = xy.reshape(n, 2) if n else xy.reshape(0, 2)
        return points, list(runs[:runs_n]), list(ticks[:tick_n])

    def data_points(self):
        """The vertices of the last project(), in the data's own units.

        The same order as the pane pixels, so a run indexes either. y is the
        line's value. A shifted line is not rewritten onto the common axis.
        """
        import numpy as np

        n = int(_lib.h5plot_point_count(self._handle))
        xy = np.empty(n * 2, dtype=np.float64)
        if n:
            _lib.h5plot_copy_data(self._handle, xy.ctypes.data_as(POINTER(c_double)))
        return xy.reshape(n, 2) if n else xy.reshape(0, 2)

    def run_lines(self):
        """Which line each run of the last project() belongs to."""
        import numpy as np

        n = int(_lib.h5plot_run_count(self._handle))
        lines = np.empty(n, dtype=np.int32)
        if n:
            _lib.h5plot_copy_run_lines(self._handle, lines.ctypes.data_as(POINTER(c_int)))
        return lines

    def x_log(self) -> bool:
        return bool(_lib.h5plot_x_log(self._handle))

    def y_log(self) -> bool:
        return bool(_lib.h5plot_y_log(self._handle))

    def set_line_y_log(self, index: int, on: bool) -> None:
        """Logarithmic y for one line that already has its own axis.

        A line still on the common axis is left alone: that scale is the
        checkbox. Zero and negative samples become gaps, as a NaN does.
        """
        _lib.h5plot_set_line_y_log(self._handle, int(index), 1 if on else 0)

    def line_y_log(self, index: int) -> bool:
        """The scale this line is drawn on. The common axis, when it has none of its own."""
        return bool(_lib.h5plot_line_y_log(self._handle, int(index)))

    def line_y_range(self, index: int) -> tuple[float, float]:
        low = c_double()
        high = c_double()
        _lib.h5plot_line_y_range(self._handle, int(index), ctypes.byref(low), ctypes.byref(high))
        return float(low.value), float(high.value)
