<!--
SPDX-FileCopyrightText: 2026 Jonas Sattler
SPDX-License-Identifier: GPL-3.0-only
-->

# What each renderer gives you, and what you would write yourself

The companion to `docs/PLOTTING-EVALUATION.md`, which has the measurements.
This file has the inventory: for every capability a reader of scientific data
expects from a plot, which of the three candidates provides it, and — where one
does — the class or property that does, so a cell can be checked rather than
believed.

Three verdicts, and the distinction between the second and the third is the
whole decision:

| | meaning |
|---|---|
| **yes** | the library provides it; the named API is the evidence |
| **ours** | the library does not, and H5Scope would write it — the cell says roughly what that costs |
| **no** | not available, and not reasonably buildable on top of what is there |

Columns: **today** is what `src/qml/PlotSurface.qml` actually does now, which is
not the same as what Qt Graphs can do. **Graphs** is Qt Graphs 6.11.1, read out
of the pinned headers in `build/*/vcpkg_installed/x64-linux/include/Qt6/QtGraphs`.
**QCP** is QCustomPlot 2.1.1 as vcpkg builds it. **SG** is the scene-graph
spike in `spikes/plotting/scenegraph`.

## Axes

| | today | Graphs | QCP | SG |
|---|---|---|---|---|
| Linear value axis | yes | yes `QValueAxis` | yes `QCPAxis` | ours, done — the projection is 4 lines |
| **Logarithmic axis** | **no** | **no** — 2-D ships `QValueAxis`, `QBarCategoryAxis`, `QDateTimeAxis` and nothing else; `QLogValue3DAxisFormatter` is for 3-D | yes `QCPAxis::stLogarithmic` + `QCPAxisTickerLog` | ours, done — `PlotItem.cpp`, and the decade ticks in `Main.qml` |
| Date/time axis | no | yes `QDateTimeAxis` | yes `QCPAxisTickerDateTime` | ours — a tick generator and a label format |
| Multiples of π, fixed step, named ticks | no | no | yes `QCPAxisTickerPi`, `…Fixed`, `…Text` | ours — one function each |
| More than one y axis | no | yes, per series: `QAbstractSeries::axisY` (6.10) | yes `QCPAxisRect::addAxis` | ours — a second projection and a second gutter |
| Axis titles | no (`titleVisible: false`) | yes `QAbstractAxis::titleText` | yes `QCPAxis::setLabel` | ours — a `Text` in the chrome |
| Tick spacing that follows the *visible* range | ours — `niceStep()`, `PlotSurface.qml:407-422`, written because Graphs computes spacing from the declared range | partly | yes | ours, done — the same `niceStep()` |
| Grid, subgrid | see note | yes `gridVisible`, `subGridVisible` | yes `QCPGrid` | ours, done — `Repeater` of `Rectangle` |

> **The grid note.** `PlotSurface.qml:571-578` records that "Qt Graphs 6.11 draws
> neither the grid nor the axis rules whatever these are set to — verified by
> setting them to 3px red, which also does not appear". That is not what the
> pinned 6.11.1 does here. A `GraphsView` carrying the same `clipPlotArea`, the
> same four margins, `gridVisible: true` on both axes and the same
> `grid.mainColor`/`grid.mainWidth` on its `GraphsTheme` draws both; set to 3 px
> red it produced 62 632 red pixels in 23 horizontal and 29 vertical rules. So
> the grid is available, and something in the application's own configuration is
> suppressing it. One lead worth checking first: the application binds
> `colorScheme` to `Theme.dark`, and `QGraphsTheme` re-applies its colour preset
> when that changes — a binding that evaluates after the grouped `grid.*`
> assignments would overwrite them. **This is worth fixing whatever else is
> decided; it is a checkbox in the settings panel that currently does nothing.**

## Series and marks

| | today | Graphs | QCP | SG |
|---|---|---|---|---|
| Line | yes | yes `QLineSeries` | yes `QCPGraph` | ours, done |
| Line through non-monotonic x (a phase portrait) | yes | yes | yes `QCPCurve` — `QCPGraph` alone assumes sorted x | ours, done, and **without the envelope**: columns cannot bucket x that goes backwards |
| Scatter / point markers | one shape, on or off | yes `pointDelegate` — but it is a `QQmlComponent` **per point** | yes `QCPScatterStyle`, 14 shapes | ours — a second geometry node of quads |
| Step / stairs | no | no | yes `QCPGraph::lsStepLeft/Right/Center` | ours — one extra vertex per sample |
| Filled area / band | no | yes `QAreaSeries` | yes `QCPGraph::setChannelFillGraph` | ours — a triangle strip |
| **Error bars** | no | **no** | yes `QCPErrorBars` | ours — a geometry node of segments |
| Bars, box plots, financial | no | bars only | yes `QCPBars`, `QCPStatisticalBox`, `QCPFinancial` | ours, one at a time |
| Colour map / heat map | separate `DatasetImage` | no 2-D equivalent | yes `QCPColorMap` + `QCPColorScale` | n/a — H5Scope already has its own |
| Polar | no | no | yes `QCPPolarAxisAngular` | ours |
| Per-series dash pattern | no | no — colour, width, opacity only | yes, any `QPen` | ours — a shader or a dash expansion |

## Reading the data

| | today | Graphs | QCP | SG |
|---|---|---|---|---|
| Wheel zoom, anchored on the pointer | ours — `PlotSurface.qml:371-389` | yes `zoomStyle`, `zoomSensitivity` (6.9) | yes `QCP::iRangeZoom` | ours, done |
| Drag to pan | ours — `DragHandler` | yes `panStyle` (6.9) | yes `QCP::iRangeDrag` | ours, done |
| Rubber-band box zoom | no | yes `zoomAreaEnabled` + `zoomAreaDelegate` (6.9) | yes `QCPSelectionRect` | ours — a `Rectangle` and an arithmetic |
| Reset view | yes | yes | yes | ours, done |
| **Crosshair with a value readout** | **no** — the footer prints ranges, nothing follows the pointer | ours (`hovered`, 6.10, gives you the hook) | yes `QCPItemTracer` + `QCPItemText`, snapping to a sample | ours — and the same work either way |
| Point selection | no | yes `QXYSeries::selectedPoints` | yes `QCPDataSelection` | ours |
| Range selection with statistics | no | no | partly `QCPDataSelection` | ours |
| Hover feedback on a series | no | yes `hoverable` / `hovered` (6.10) | yes `selectTest()` | ours |
| Annotations: lines, text, arrows, brackets | no | no | yes `QCPItemLine`, `…Text`, `…Bracket`, `…Ellipse`, `…Pixmap` | ours, one at a time |
| Legend | ours — `PlotLegend.qml`, 499 lines, virtualised past half a million rows | `legendData` only | yes `QCPLegend` | ours — **and `PlotLegend.qml` is already better than either library's** |

## Honesty at scale

| | today | Graphs | QCP | SG |
|---|---|---|---|---|
| Decimation before the renderer | stride, capped at `kMaxPoints = 2048` — `DatasetTableModel.hpp:205-213` says in as many words that "a spike narrower than one stride is not drawn" | none — it is given every point and draws every point | yes, `setAdaptiveSampling` — a per-column envelope, on by default | ours, done — min/max envelope to the pixel width |
| A spike one sample wide survives | **no** | yes, by drawing all 10⁶ points | yes | yes |
| A run of missing values is a gap | **no** | **no** — see below | yes | yes |
| x near 1.7e9 at a 1 ms step stays a line | yes | yes | yes | yes — but only because the projection is done in double *before* the cast to the float32 vertex |
| Device pixel ratio honoured | yes | yes | yes | yes |

> **The gap.** `DatasetPlot::fill` drops non-finite values and hands the rest to
> the renderer. Dropping a point is not drawing a gap: the renderer joins the two
> survivors. `spikes/plotting/results/qtgraphs-gap.png` is what that looks like —
> a clean straight diagonal drawn across ten thousand samples that do not exist,
> rising from −0.6 to +0.6 as if it had been measured. QCustomPlot and the scene
> graph both break the line. This is the one correctness check any renderer
> fails, and it is the application's own `fill()` that causes it.

## What it costs to have at all

| | Graphs | QCP | SG |
|---|---|---|---|
| Licence | **GPL-3.0-only** + Qt-GPL-exception-1.0. No LGPL text ships with the module — `share/qtgraphs/copyright` carries BSD-3-Clause, GFDL-1.3, GPL-3.0-only, the Qt commercial reference and the GPL exception, and nothing else | GPL-3.0-or-later | none added |
| Compatible with H5Scope being GPL-3.0-only | yes — and it is *why* H5Scope is GPL-3.0-only rather than LGPL | yes | n/a |
| Extra ports on the link line | `qtquick3d`, `meshoptimizer` — neither used; H5Scope renders nothing in 3D | `qtbase[widgets]`, `Qt6::PrintSupport` (and so CUPS on Linux) | none |
| Needs `QApplication` | no | **yes** — `QCustomPlot` derives from `QWidget`, and constructing one under a bare `QGuiApplication` aborts | no |
| Works inside a Qt Quick scene | natively | through `QQuickPaintedItem`: one full-surface rasterisation, one blit, one texture upload per frame, and every mouse and wheel event forwarded by hand | natively |
| Stripped binary, same harness and scene | 67.9 MiB | 46.4 MiB | 44.7 MiB |
| …so the module costs | **+23.2 MiB** over drawing it ourselves | +1.7 MiB | — |

## The shape of the answer

Counting the rows above: Qt Graphs provides **21** of the 33 capabilities,
QCustomPlot **31**, and the scene-graph route **8 as written, with the rest
ours**. But the count is the wrong summary, because the rows are not equal in
weight and three of them decide it:

- The two capabilities H5Scope most visibly lacks — a **logarithmic axis** and a
  **crosshair readout** — split differently. QCustomPlot has both. Qt Graphs has
  neither and cannot be given the first at all. The scene graph has the first
  already and would write the second, which is the same work Qt Graphs would
  also make us do.
- Everything under *Reading the data* that H5Scope already has, it already
  wrote: the zoom arithmetic, the pan clamp, the tick spacing, the whole legend.
  Adopting a library does not recover that work; it competes with it.
- The one thing no library fixes is the **stride thinning**, because it happens
  before any of them are reached.
