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

## Is it fast enough?

Milliseconds inside one frame, median over a scripted pan-and-zoom of forty
frames — the work the renderer does to turn data into draw calls, with the
buffer swap excluded. One process per cell, so every peak-memory figure is that
cell's own rather than a high-water mark left by the one before it.

| series × points | scene graph | …batched | QCustomPlot | …no adaptive | **Qt Graphs** |
|---|---|---|---|---|---|
| 1 × 1 000 | 0.09 | 0.09 | 0.65 | 0.64 | 0.16 |
| 1 × 100 000 | 0.10 | 0.12 | 0.85 | 0.88 | 36.87 |
| 1 × 1 000 000 | 0.17 | 0.17 | 0.87 | 2.99 | 601.89 |
| 1 × 10 000 000 | 0.90 | 0.92 | 2.35 | 29.19 | **SIGSEGV** |
| 8 × 1 000 | 0.09 | 0.09 | 0.78 | 0.77 | 0.53 |
| 8 × 100 000 | 0.22 | 0.41 | 1.63 | 2.43 | 325.23 |
| 8 × 1 000 000 | 0.80 | 0.92 | 2.71 | 17.79 | **600 s timeout** |
| 64 × 1 000 | 0.24 | 0.14 | 1.29 | 1.32 | 5.61 |
| 64 × 100 000 | 1.06 | 2.06 | 8.71 | 14.95 | 1975.62 |
| 256 × 1 000 | 1.51 | 0.34 | 3.38 | 3.28 | 25.26 |
| 256 × 100 000 | 5.12 | 8.42 | 28.15 | 57.80 | **out of memory** |
| 1024 × 1 000 | 8.20 | 0.96 | 10.68 | 10.81 | 96.77 |
| 10000 × 1 000 | 346.85 | 12.05 | 99.25 | 99.38 | **600 s timeout** |
| **64 × 2048** | **0.26** | **0.19** | **1.47** | 1.46 | **10.78** |
| **10000 × 2048** | **341.65** | **52.38** | **125.87** | 125.08 | **600 s timeout** |

The two bold rows are not hypotheses about scale. `64 × 2048` is the view a
reader gets by clicking a dataset — `kMaxInitialSeries` lines at `kMaxPoints`
points. `10000 × 2048` is what the legend's `all` does to a ten-thousand-row
table, which `PlotLegend.qml` warns about and permits.

### Qt Graphs has a ceiling, and the application is sitting on it

Five of fifteen cells did not produce a number, in three different ways —
a segmentation fault inside `QSGCurveStrokeNode::cookGeometry`, three runs that
could not draw fifty frames in ten minutes, and one killed by the out-of-memory
killer. They are one failure, not three. Qt Graphs holds every point it is
given, in triangulated stroke geometry, and the memory is the story:

| total points on screen | frame | peak resident |
|---|---|---|
| 64 × 1 000 = 64 k | 5.61 ms | 449 MiB |
| 1 × 100 000 = 100 k | 36.87 ms | 831 MiB |
| **64 × 2048 = 131 k** | **10.78 ms** | **616 MiB** |
| 1 × 1 000 000 = 1 M | 601.89 ms | 4 979 MiB |
| 64 × 100 000 = 6.4 M | 1 975.62 ms | **27 789 MiB** |
| 256 × 100 000 = 25.6 M | — | killed |

Twenty-seven gigabytes to draw six and a half million points. Above the process
floor it settles at roughly four kilobytes of resident memory per drawn point,
which is what makes 25.6 M points an impossible request rather than a slow one.

And the application's default view is at 131 072 points — just past the place
where this becomes tens of milliseconds a frame. **`kMaxPoints = 2048` is not a
nicety; it is the cap that keeps the current renderer inside its envelope.** The
same cap is what loses a one-sample spike.

### Where the scene graph wins, and where its spike is naive

Against Qt Graphs it is between 1.8× and 1 800× faster, and it never fails a
cell. Against QCustomPlot it is 5–7× faster in the ordinary range. Neither of
those is the interesting part. Two things are:

**At ten thousand lines, the bottleneck is draw calls.** One node per series is
346.85 ms; one batched buffer is 12.05 ms — 29× — because the first issues ten
thousand draw calls and the second issues one.

**And this spike's batching is the naive kind.** It draws disjoint segments with
20-byte coloured vertices, so it doubles the vertex count to buy the draw-call
saving, and the memory says so: 3 871 MiB at `10000 × 2048` against QCustomPlot's
618 MiB for the same picture. A production version would want batched *strips*
with index buffers. The 29× is real and the way it is currently bought is not
the way to buy it.

### Allocations, which do not depend on this machine

Over the same forty frames, and this is the column `tests/test_cost.cpp` would
recognise — a count, not a duration:

| | 64 × 2048 | 1024 × 1 000 |
|---|---|---|
| scene graph | 54 936 | 64 970 |
| scene graph, batched | 54 953 | 53 195 |
| QCustomPlot | 18 096 | 210 068 |
| **Qt Graphs** | **392 626** | **84 661 844** |

Qt Graphs performs **2.1 million allocations per frame** at a thousand lines,
and about ten thousand per frame at the application's default. It is rebuilding
its geometry from nothing every time the axis range moves — which is exactly
what `PlotSurface.qml:615-622` already works around from the other side, having
found that a recoloured series keeps its old stroke until it is re-filled.

The scene graph's own ~1 300 per frame are almost all the QML chrome: the tick
`Repeater`s rebuild their delegates whenever the view changes. That is
application code, it is the same in every candidate, and it is fixable.

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

## What to do

Three recommendations, in the order they should be acted on. The first is
independent of the other two and should happen whatever is decided about the
renderer.

### 1. Fix what is ours, first

None of this needs a library decision, and two of the three are correctness
rather than taste.

- **Thin by min/max envelope, not by stride.** `DatasetLookup::thinToPoints`
  and `DatasetTableModel::sampleFrom` select every *n*-th index; selecting the
  smallest and largest in each pixel column instead reads the same number of
  elements, draws the same number of points, and cannot lose an extremum,
  because the extremum is what it selects. `DatasetTableModel.hpp:205-213`
  already calls this "the place to start if the plot ever needs to be exact at
  a glance". It is the single largest honesty win available and it is worth
  roughly a day.
- **Draw a gap where the data is missing.** `DatasetPlot::fill` drops
  non-finite values, and the renderer joins what is left — see
  `spikes/plotting/results/qtgraphs-gap.png`, a straight diagonal across ten
  thousand samples nobody measured. This one *is* partly blocked on the
  renderer: `QXYSeries` has no way to express a break, so on Qt Graphs the only
  fix is one series per run.
- **Fix the grid.** With the pinned 6.11.1 a `GraphsView` configured the way
  the application configures it draws its grid; something in H5Scope's own
  setup suppresses it, and the settings panel currently has a checkbox that
  does nothing. See the note in `docs/PLOTTING-FEATURES.md` for a lead.

### 2. Replace the renderer with our own scene-graph item

On the numbers this is not close. At the view a reader actually gets, it is
**40× faster and uses a fifth of the memory**; it is the only candidate that
never failed a cell; it removes 23.2 MiB from the binary along with `qtquick3d`
and `meshoptimizer`; and it is the only option that leaves **no GPL-only
dependency in the tree**, which turns H5Scope's licence from a constraint into
a choice.

It also already does the two things the reader most wants and the incumbent
cannot be given: a logarithmic axis, and a decimator that keeps spikes.

What it costs is honest and should not be minimised: **everything is ours.**
Markers, a crosshair readout, export, axis titles, error bars and annotations
are all work — and so is the maintenance of a renderer, forever. Three things
make that bearable here rather than reckless:

- The spike is about 400 lines of C++ and 170 of QML, and it already covers
  everything the application draws today except markers.
- H5Scope has *already* written most of the chrome a library would supply —
  `niceStep()`, the zoom arithmetic, the pan clamp, and a legend neither
  library comes close to. Adopting a library competes with that work rather
  than recovering it.
- The seam is one function. `fill(QAbstractSeries*, int)` appears exactly
  twice.

Two things in the spike must not be carried over as written: batch into
**strips with index buffers**, not disjoint segments, or the memory at ten
thousand lines is worse than the incumbent's; and keep the double-precision
projection, because a float32 vertex turns an epoch timestamp into a staircase.

### 3. If the priority is features soonest, the answer is different

This is the honest counter-argument and it deserves stating plainly. If what
matters most is having error bars, annotations, a snapping cursor, PDF export
and a dozen axis tickers *this quarter*, **QCustomPlot delivers 32 of the 33
capabilities today** and it is fast enough — 1.47 ms at the default view, and it
completed every cell in the grid.

What you would be buying it with: a `QApplication` and the Qt Widgets stack
under the QML; a full-surface rasterisation, blit and texture upload every
frame whether or not anything changed; every mouse and wheel event forwarded by
hand; chrome that cannot read a single value from `Theme.qml`; and a second
GPL-only dependency in place of the first. For a widgets application it would
be the obvious answer. For this one it is the wrong shape, and the measured
6–8× frame-time gap is the smallest of the reasons.

## What moving would cost

`Qt6::Graphs` is named in six build files and two manifests:

| where | what |
|---|---|
| `CMakeLists.txt:189` | `find_package(... Graphs ...)` |
| `src/gui/CMakeLists.txt:127` | `gui` links it PUBLIC, because `DatasetPlot.hpp` exposes it |
| `src/qml/CMakeLists.txt:103` | `appqml` links it for `PlotSurface.qml`'s import |
| `tests/CMakeLists.txt:53, 86, 117` | three suites |
| `tools/CMakeLists.txt:21, 55` | `inspect-file`, `make-screenshots` |
| `vcpkg.json:74` | the `qtgraphs` dependency |
| `cmake/ThirdPartyLicenses.cmake:48-49` | `qtgraphs` and `qtquick3d`, plus `meshoptimizer` at `:44` |

Plus `THIRD-PARTY-NOTICES.md`, which describes the GPL-only group in four
places and would lose it.

The code is narrower than that list suggests:

- **One QML file.** `PlotSurface.qml` holds the only `import QtGraphs` in the
  repository. `PlotLegend.qml`, `PlotSettingsPanel.qml`, `RangeAxis.qml`,
  `CustomView.qml` and `DataView.qml` never mention it.
- **One C++ signature, twice.** `DatasetPlot.cpp:337` and `CustomPlot.cpp:606`.
  Both cast to `QXYSeries` and call `replace()`. A scene-graph item wants the
  `std::vector<double>` the model already holds, so both get *simpler*.
- **One test seam.** `tests/test_customplot.cpp:93-98` fills a bare
  `QLineSeries` with no QML engine, which is the only reason that suite links
  Qt Graphs. A replacement must keep a fill target that works headless — which
  a plain data handover does more easily than a `QObject` series did.

Everything else survives untouched: the whole read path, `H5Thread`, the
batching, `thinToPoints`, both plot models' property surfaces, the legend, the
settings panel, `RangeAxis.qml`'s two-of-three solver, and every one of
`Theme.qml`'s palettes.

## Reproducing any of this

```sh
cd spikes && cmake --preset spike-release && cmake --build --preset spike-release
cd .. && R=spikes/plotting/results
for b in scenegraph qcustomplot qtgraphs; do
  build/spike-release/bin/spike-$b --verify --out $R
done
build/spike-release/bin/spike-qtgraphs --bench --cell 64x2048 --out $R
build/spike-release/bin/plot-gallery --out $R
```

`spikes/plotting/results/gallery.md` is generated from the two `.tsv` files and
the images; `results.tsv` is one process per cell and `results-sequential.tsv`
is the whole grid in one process, which is where the teardown column means
something. `spikes/README.md` has the rest.
