<!--
SPDX-FileCopyrightText: 2026 Jonas Sattler
SPDX-License-Identifier: GPL-3.0-only
-->

# Should H5Scope keep drawing its plot with Qt Graphs?

A bake-off, on the branch `plot-library-evaluation`, between the renderer the
application uses today and two alternatives. The capability inventory is in
`docs/PLOTTING-FEATURES.md`; the generated tables and every image quoted here
are in `spikes/plotting/results/`, and `spikes/README.md` says how to reproduce
them.

Nothing under `src/`, `tests/`, `tools/`, `cmake/` or either manifest was
touched to produce any of it.

## The question, and why it is two questions

The plot is limiting, and the two ways it is limiting have different answers.

It is **missing features** a reader of scientific data expects: no logarithmic
axis, no crosshair or value readout, no export, no point or region selection,
no axis titles, no per-series line style. And it is **not telling the whole
truth** about the data it does draw: it thins by stride, so a spike narrower
than one stride is not drawn, and it drops non-finite values rather than
breaking the line, so missing data is drawn as a straight line across the gap.

Those are not the same problem and they do not have the same fix. Most of the
first list is work somebody has to write whichever library is underneath. All
of the second list happens in `src/gui/`, before any renderer is reached, and
**no library swap fixes any of it**.

So the bake-off measures three things separately: what the renderers cost, what
they get right, and what they save us writing.

## What was built

Three runnable applications, the same in every respect except the renderer:
same synthetic data, same harness, same window size, same gesture, same colour
rule, same eight correctness checks.

| spike | renderer |
|---|---|
| `spike-qtgraphs` | Qt Graphs 6.11.1, arranged as `src/qml/PlotSurface.qml` arranges it — including the `GraphsView` teardown per selection and the re-fill per recolour, because those are real costs the incumbent must be charged for |
| `spike-scenegraph` | a `QQuickItem` over `QSGGeometryNode` line strips, with min/max envelope decimation to the pixel width; `--variant batched` puts every line in one buffer instead of one node each |
| `spike-qcustomplot` | QCustomPlot 2.1.1 inside a `QQuickPaintedItem`; `--variant no-adaptive` turns off its own decimation |

They are driven through one `Surface` interface by one loop (`SpikeRunner.cpp`),
because a benchmark whose loop is copied per candidate lets any result be blamed
on the loop.

### Two candidates surveyed and not built

**QuickGraphLib** (Refeyn, MIT, Qt Quick native, hardware rendered, PNG and SVG
export) is the most interesting thing in this space and is still `0.1.0a6`. It
is not in vcpkg, so it would need an overlay port like `ports/xcb-util-cursor`,
and pinning a pre-release alpha into a manifest that otherwise pins everything
to a vcpkg baseline is a different kind of decision from the one being made
here. Worth revisiting when it reaches 1.0.

**ImPlot, Qwt and JKQTPlotter** were ruled out on shape rather than on merit.
ImPlot (MIT, immediate-mode, genuinely fast) brings its own event loop, its own
font rasteriser and its own look, none of which can be reached from `Theme.qml`;
Qwt (LGPL-2.1 with a static-linking exception) and JKQTPlotter (LGPL, and the
vcpkg port is pinned to a 2023 commit) are `QWidget`-only, so they would arrive
through the same `QQuickPaintedItem` bridge as QCustomPlot while offering less
than it does.

## How it was measured, and four ways it lied first

Every number below is a frame time, an allocation count or a byte count from a
run on this machine. Four of them were wrong in the first pass, and each was
wrong in a way that looked plausible:

- **`afterFrameEnd` comes after the buffer swap**, and on a window the
  compositor is not showing, that swap blocks for one to two seconds. Measured
  through it, all three renderers scored about a thousand milliseconds a frame
  and were indistinguishable. `cpu` now ends at `afterRendering`; the swap is
  reported separately as `wall`.
- **Wayland throttles a client to frame callbacks**, and a window nobody is
  looking at gets none — so the run recorded no frames at all and reported zero
  milliseconds for having drawn nothing. `--bench` asks for `xcb` where there is
  an X server, and says so where there is not.
- **`/proc/self/status` has a size of zero**, so `QFileDevice::atEnd()` is true
  before the first line and the entire `rss` column read `-`.
- **Subpixel-antialiased text is coloured.** The correctness checks tell a curve
  from the chrome around it by chroma, because the curves are saturated hues and
  the grid and labels are grey — and the caption's glyph edges carried fringes
  of chroma 90, which made them the highest "data" ink on the surface. All three
  renderers were reported as having lost a spike that all three had drawn.

A fifth was not a measurement error but a mislabelled one: a QCustomPlot cell
reported a **build of 495 574 ms** for sixty-four lines. It was not building
anything; it was discarding the ten thousand graphs the previous cell left
behind. Teardown is now its own column.

## Is the picture true?

Eight checks, each asserting a property of the pixels rather than comparing
against a reference image — a pixel-exact comparison would fail all three for
antialiasing and line joins, and would say nothing about honesty. The images
are in `spikes/plotting/results/`.

| check | Qt Graphs | QCustomPlot | scene graph |
|---|---|---|---|
| a one-sample spike in a million survives | pass | pass | pass |
| **a run of missing values is a gap** | **fail** | pass | pass |
| the line reaches both ends of the pane | pass | pass | pass |
| x near 1.7e9 at a 1 ms step stays a line | pass | pass | pass |
| y over 1e-30 … 1e30 on a linear axis | pass | pass | pass |
| 1024 lines are drawn as 1024 lines | pass (288 hues) | pass (253) | pass (73) |
| the y axis can be logarithmic | **absent** | pass | pass |
| the grab comes back at device resolution | pass | pass | pass |

### The gap, which is the one that matters

`spikes/plotting/results/qtgraphs-gap.png` is a sine with ten thousand
consecutive samples missing out of a hundred thousand. Qt Graphs draws a clean
straight diagonal across them, rising from −0.6 to +0.6, indistinguishable from
a linear ramp somebody measured.

The cause is in H5Scope, not in Qt Graphs. `DatasetPlot::fill` skips non-finite
values and appends the rest; skipping a point is not drawing a gap, because the
renderer then joins the two points either side of it. The comment above that
loop says "a cell that would not read is a gap in the line, not a zero", and the
first half of that sentence is not what the code achieves.

QCustomPlot breaks the line because `QCPGraph` treats NaN as a discontinuity.
The scene-graph spike breaks it because it ends the strip and starts a new one.
Either would fix it; so would three lines in `fill()` if the series abstraction
could express a break, and Qt Graphs' cannot.

### The spike, and the cap that hides it

All three passed, and the reason is worth stating because it does not carry over
to the application. The check hands the renderer a million points. H5Scope never
does: `DatasetPlot::kMaxPoints` is 2048, and `thinToPoints` strides down to it
before the renderer is reached. `DatasetTableModel.hpp:205-213` already names
the consequence — "a spike narrower than one stride is not drawn" — and calls
itself "the place to start if the plot ever needs to be exact at a glance".

So the application fails this check today while every renderer passes it. The
stride cap is what keeps Qt Graphs viable at all, and replacing it with a
min/max envelope to the pixel width costs the same number of drawn points and
cannot lose an extremum, because the extremum is what it selects.

### The float32 trap, avoided rather than absent

All three pass `bigx`, and the scene-graph spike passes it only because it was
written to. `QSGGeometry` stores float32; an x of 1.7e9 has a float spacing of
128, so a line sampled every millisecond collapses into a staircase of flat
treads. The fix is not to store doubles — it is to project in double and cast
the *pixel coordinate*, which is at most a few thousand. That is why `PlotItem`
does its own projection instead of uploading data coordinates and letting a
matrix in the vertex shader do it, which is the faster arrangement and the one
that loses the data. Anyone writing a GPU plot for this application has to know
this; it is the single easiest way to ship a renderer that is quick and wrong.

## What each one costs to have at all

| | Qt Graphs | QCustomPlot | scene graph |
|---|---|---|---|
| Licence | GPL-3.0-only + Qt-GPL-exception-1.0 | GPL-3.0-or-later | none added |
| Compatible with GPL-3.0-only | yes | yes | n/a |
| Ports it drags in | `qtquick3d`, `meshoptimizer` | `qtbase[widgets]`, `Qt6::PrintSupport` → CUPS on Linux | none |
| Needs `QApplication` | no | **yes** | no |
| Stripped binary, same harness and scene | 67.9 MiB | 46.4 MiB | 44.7 MiB |
| …so the renderer costs | **+23.2 MiB** | +1.7 MiB | — |

Three things follow that the licence table does not say on its own.

**Qt Graphs is the reason H5Scope is GPL-3.0-only.** `share/qtgraphs/copyright`
carries BSD-3-Clause, GFDL-1.3, GPL-3.0-only, the Qt commercial reference and
the GPL exception — and no LGPL text at all. `THIRD-PARTY-NOTICES.md:35-40` says
the same thing, and adds that Qt Quick 3D "arrives with Graphs and would leave
with it". Every other Qt module in this binary is available under LGPL-3.0.

**So the scene-graph route is the only one that frees the licence.** Dropping Qt
Graphs removes both GPL-only modules and leaves nothing in the tree that forces
GPL — `hdf5` is BSD-3-Clause, everything else is LGPL or permissive. That does
not oblige anyone to relicense, and it turns a constraint into a choice.
Adopting QCustomPlot instead swaps one GPL-only dependency for another.

**QCustomPlot needs a `QApplication`.** It derives from `QWidget`, and
constructing one under a bare `QGuiApplication` aborts outright. A Qt Quick
application that adopts it has the Qt Widgets stack inside it and the widgets
event machinery under its QML — which is, in as many words, the reason
`CMakeLists.txt:183-187` gives for having chosen Qt Graphs over Qt Charts in the
first place.

## What each one saves us writing

The full inventory is `docs/PLOTTING-FEATURES.md`. The summary is that counting
capabilities gets the wrong answer, because H5Scope has already written most of
the ones it uses:

- `niceStep()` — because Qt Graphs computes tick spacing from the declared range
  rather than the visible one.
- The zoom arithmetic, the pan clamp, the reset.
- `PlotLegend.qml`, 499 lines, virtualised past half a million rows, with
  per-line visibility, colour swatches, highlight, and "add to custom plot".
  Neither library offers anything close.
- `Theme.qml`'s two categorical palettes and five ramps, solved by
  farthest-point selection in CIELAB against both grounds.

Adopting a library does not recover that work. It competes with it — and in
QCustomPlot's case it competes through a `QQuickPaintedItem`, where the library's
own chrome is rasterised on the CPU and blitted, and cannot take a single value
from `Theme.qml` without being re-pointed at it by hand.

What a library would genuinely save is the part H5Scope has *not* written:
markers and scatter styles, error bars, a crosshair with a snapping readout,
annotations, export. QCustomPlot has all of those. Qt Graphs has almost none of
them, and cannot be given a logarithmic axis at any price.
