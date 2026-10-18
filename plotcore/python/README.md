<!--
SPDX-FileCopyrightText: 2026 Jonas Sattler
SPDX-License-Identifier: GPL-3.0-only
-->

# plotcore for Python

This is the plot [H5Scope](https://github.com/jonassattler/H5Scope) draws, as a
Python library.

- **No spike is ever thinned away.** A line is held as a pyramid of min/max
  envelopes, so a one-sample spike survives at any zoom, and a zoom folds what
  is already held instead of reading again.
- **The same axes, gestures and colours.** They are the viewer's own, and its
  parity tests hold the two to each other.
- **The same plot from Python, C++ and Rust.** All three libraries are one
  plot. `plotcore/tests/golden` is the script they all have to draw
  identically.

```python
import numpy as np
import plotcore

figure = plotcore.Figure()
figure.add_line(np.sin(np.linspace(0, 100, 10_000_000)), "sine")
figure.settings["title"] = "ten million samples"
figure.set_size(800, 500)
figure.wheel(400, 250, 360)        # three notches in, about the pointer
scene = figure.scene()             # strokes, rules and text, in pixels
```

`plotcore.qt.PlotWidget` draws a figure in a PyQt6 widget (`pip install
plotcore[qt]`). `plotcore.mpl` draws one on matplotlib's own axes, either
once (`draw`) or live (`View`); install it with `pip install
plotcore[matplotlib]`. `plotcore/examples/pyqt6/demo.py` uses every feature
on one window.

## How it is built

The package loads plotcore's C interface (`plotcore/capi/plotcore.h`) with
ctypes. It links no Python, so one wheel per platform serves every Python 3.

The wheel is built from the repository, because its CMake project is
`plotcore/` one directory up:

    pip install ./plotcore/python

To develop against a library you built yourself, set `PLOTCORE_LIBRARY` to
its path.
