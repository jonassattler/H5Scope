<!--
SPDX-FileCopyrightText: 2026 Jonas Sattler
SPDX-License-Identifier: GPL-3.0-only
-->

# spikes/ — throwaway measurements, not the product

Nothing in this tree ships. It is a separate CMake project with a separate
vcpkg manifest, and the root `CMakeLists.txt` does not reference it: configuring
or building H5Scope neither reads nor produces anything here.

## Why it exists

`src/qml/PlotSurface.qml` draws through Qt Graphs. The seam is narrow — one
`import QtGraphs`, and one C++ entry point per plot model
(`fill(QAbstractSeries*, int)`, which only ever casts to `QXYSeries` and calls
`replace()`) — so the library underneath is genuinely replaceable, and the
question of whether it *should* be replaced is worth answering with numbers
rather than with taste.

`plotting/` is that answer: the same small application written three times over
three renderers, driven by the same synthetic data through the same harness,
measured on the same grid, and checked by the same eight correctness
predicates.

| spike | renderer | licence |
|---|---|---|
| `spike-qtgraphs` | Qt Graphs 2-D, as the application draws today | GPL-3.0-only |
| `spike-scenegraph` | a `QQuickItem` over `QSGGeometryNode` line strips | none added |
| `spike-qcustomplot` | QCustomPlot 2.1.1 inside a `QQuickPaintedItem` | GPL-3.0-or-later |

The findings are written up in `docs/PLOTTING-EVALUATION.md`, with the feature
comparison in `docs/PLOTTING-FEATURES.md` and the generated tables and images
under `plotting/results/`.

## Building

```sh
export VCPKG_ROOT=/path/to/vcpkg
cd spikes
cmake --preset spike-release
cmake --build --preset spike-release
ctest --preset spike-release        # the correctness checks; needs a display
```

From `spikes/` rather than from the repository root: `cmake --build --preset`
and `ctest --preset` read the preset file in the working directory, and the
root's `CMakePresets.json` knows nothing about these. The build tree still
lands in the repository's own `build/spike-release`, which `.gitignore`
already covers.

`spikes/vcpkg.json` is the application's manifest copied verbatim with
`qcustomplot` added, and that is deliberate rather than lazy. vcpkg hashes each
port by its own feature set and triplet: an identical `qtbase` stanza restores
Qt from the binary cache in minutes, and a stanza that differs by one feature
rebuilds it from source in hours. Before changing anything in it, prove the
cheap path is still the one being taken:

```sh
"$VCPKG_ROOT/vcpkg" install --dry-run --triplet x64-linux \
  --overlay-ports=ports --x-manifest-root=spikes
```

## Running

Each spike opens as an ordinary window, which is the point — the numbers say
whether a renderer can keep up, and only the hand says whether the result feels
like an instrument.

```sh
R=spikes/plotting/results
build/spike-release/bin/spike-qtgraphs                    # interactive
build/spike-release/bin/spike-qtgraphs    --bench  --out $R
build/spike-release/bin/spike-scenegraph  --bench  --out $R --variant batched
build/spike-release/bin/spike-qcustomplot --bench  --out $R
build/spike-release/bin/spike-qtgraphs    --verify --out $R
build/spike-release/bin/plot-gallery               --out $R
```

`--bench` writes one row per cell into `$R/results.tsv` as each finishes,
`--verify` writes `$R/verify.tsv` and an image per check, and `plot-gallery`
assembles both into `$R/gallery.md`. Rows are appended, so delete the two
`.tsv` files before a fresh comparison.

A row is written per cell rather than per run because a run does not always
finish: Qt Graphs segmentation-faults on a single line of ten million points,
inside `QSGCurveStrokeNode::cookGeometry`, and a file written at the end would
lose the twenty cells that had already succeeded along with the one that did
not.

The benchmarks must not be run under `QT_QPA_PLATFORM=offscreen`. That platform
declares no RHI capability, so Qt Quick falls back to its software renderer and
all three spikes are then measured drawing in a way none of them ship. There is
an `--offscreen` flag for machines with no display; it labels its own numbers.
