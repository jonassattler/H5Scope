# H5Scope — working notes for Claude

A read-only HDF5 viewer. C++20, Qt 6 Quick/QML, HDF5 2.2.0, everything built
and pinned by vcpkg and linked statically into one self-contained executable
per platform. GPL-3.0-only.

The repository is heavily commented on purpose — most files open with an
argument for why they are the way they are, usually naming the failure that
motivated them. Read those headers before changing the code under them; they
are the design record, and several of them are the only place a non-obvious
constraint is written down.

## Build and test

Requires CMake ≥ 3.26, Ninja, a C++20 compiler, and vcpkg with `VCPKG_ROOT`
set. There is no system-library fallback: without a toolchain file the
configure fails by design.

```sh
export VCPKG_ROOT=/path/to/vcpkg
cmake --preset release          # or debug; windows-release / windows-debug
cmake --build --preset release
ctest --preset release
```

- **The first configure builds Qt and HDF5 from source and takes hours.** Every
  later one reads vcpkg's binary cache and takes seconds. Check whether
  `build/<preset>/` already exists before promising a build, and do not start a
  cold one without saying what it will cost.
- Test presets set `QT_QPA_PLATFORM=offscreen` and run 4 jobs. The tests that
  draw or hit the disk hard (`qml`, `qmlload`, `screenshots`, `benchmarks`)
  share a `RESOURCE_LOCK`, so they serialise against each other while the rest
  parallelise.
- `catch_discover_tests` registers one ctest test per Catch2 case, named after
  the case rather than the binary — `ctest --preset release -N` lists them, and
  `-R '<regex>'` selects. To iterate on one suite, run its binary directly:
  `build/release/bin/test_models "the tree filter narrows the pane"`.
- Two checks need no build at all and are the fastest signal there is:

  ```sh
  bash tools/check-design-tokens.sh src/qml    # no raw hex, fonts or animation
  bash tools/check-elided-text.sh  src/qml     # nothing elides without a tooltip
  ```

  They also run as ctest tests (`design_tokens`, `elided_text`) and as their own
  CI job. Run them after touching QML.
- `cmake --build --preset release --target screenshots` regenerates
  `docs/screenshots/` from the running application. Not part of `all` — it
  writes into the source tree. It rewrites only pictures that actually changed,
  so a clean `git status` afterwards means nothing needs committing.
- Release builds (RHEL 8 floor, AppImage, Windows, source bundle) are in
  `docs/BUILDING.md`. CI is `.github/workflows/ci.yml`: design checks, then a
  full release build and test inside an AlmaLinux 8 container, then Windows.

## Layout

| Path | What it is |
|---|---|
| `src/h5core/` | The HDF5 backend. **No Qt at all** — links only `HDF5::HDF5`. Keep it that way; it is what makes the layer testable headless. `FieldDataset` is here too: one member of a compound, presented as a dataset. |
| `src/postproc/` | The numpy-shaped pipeline (slice, transpose, reshape, reduce…). Links `Qt6::Core` for `QString` only; no `QObject`, AUTOMOC off. |
| `src/gui/` | `QAbstractItemModel`s, `AppController`, the HDF5 thread, and the plot renderer (`PlotItem` + `PlotProjection`) with the cache under it (`PlotLevels` + `PlotPyramid` + `PlotBudget`). QML module URI `H5Scope.Backend`. |
| `src/qml/` | The UI. QML module URI `H5Scope`, target `appqml`. `Theme.qml` is the singleton every visual value resolves through. |
| `src/main.cpp` | Command line (`--version/--help/--license/--notices`), fonts, icon, engine. |
| `tools/` | `make-example-file`, `inspect-file`, `bench-tree`, `bench-data`, `bench-zoom`, `make-screenshots`, the CI scripts and the two design checks. |
| `tests/` | Catch2 suites (`test_h5core`, `test_postprocess`, `test_member`, `test_h5thread`, `test_models`, `test_example`, `test_cost`, `test_customplot`, `test_plotprojection`, `test_plotlevels`) plus the Qt Quick Test QML suites under `tests/qml/`. |
| `cmake/`, `ports/`, `packaging/` | Version counting, licence collection, the `xcb-util-cursor` overlay port, icons and the Windows resource. |

QML talks to exactly one object: `AppController` (`QML_SINGLETON`). The models
hang off it as `CONSTANT` properties; `DatasetPlot`, `DatasetImage`,
`TableSetupModel`, `PostprocessModel`, `CustomPlotSet` and `CustomPlot` are
`QML_UNCREATABLE` and obtained from it. `FileSystem`, `FocusRelease` and
`PlotItem` are the other registered types — `PlotItem` is the only one QML
*instantiates*, because it is an item and has to be placed.

The custom plot tabs are the one part of the UI that is **not** about the
selection: `CustomPlotSet` holds the reader's own tabs, each a `CustomPlot` of
1-D slices named as `path[subscript]` and drawn together. `CustomPlot` is
deliberately shaped like `DatasetPlot` from the outside, which is what lets
`PlotSurface.qml`, `PlotLegend.qml` and `PlotSettingsPanel.qml` draw either one
without being forked. The tabs answer to the *file* rather than to the tree, so
`AppController::openFile`/`closeFile` empty them — but the **saved views do
not**: they are written to `QSettings` and outlive both the tabs and the file,
and each reports how much of whatever is open it can still draw. Those, the
recent-files list and the RAM budget under Settings are the only three things
this program remembers between runs, and all of them are guarded by
`QCoreApplication::organizationName().isEmpty()` so the tests and
`make-screenshots` never touch the user's settings.

The plot is drawn by this program and not by a library. `gui::PlotProjection`
is the arithmetic — where a sample lands, which samples are drawable, where a
gap ends one stroke, how a million samples become two thousand vertices without
losing the one that matters, which run of a line a zoomed-in reader is asking to
have read again, and how a stroke is built out of triangles — and
it has no renderer in it, which is what lets `tests/test_plotprojection.cpp`
assert all of it with no window. `gui::PlotItem` is the part that has one.
`src/qml/PlotFrame.qml` draws the gutters, the rules, the ticks and the labels.
Read the header of `PlotProjection.hpp` before changing any of it: the three
things listed there are why this is ours rather than Qt Graphs', and each of
them is a defect of the thing it replaced.

`gui::PlotLevels` is its sibling and holds everything *above* one picture: the
fold a line is reduced by (`reduceBuckets`) and the policy over the runs it is
reduced into — which one is drawn, which is read next, which is given up. Both
plots call it. They used to carry a copy each, together with nine constants
duplicated between them under comments saying they had to match, which is how a
custom tab came to read one hyperslab per bucket for a release while the Plot
tab read one per sixty-four thousand elements. It is Qt-free for
`PlotProjection`'s reason, and `tests/test_plotlevels.cpp` asserts it with no
file, no thread and no window.

`gui::PlotPyramid` is what those runs are folded *out of*, and it is why they
cost nothing. One property of an envelope carries the whole design:

> **An envelope can be coarsened exactly, and only coarsened.** The smallest and
> the largest of a run are the smallest and the largest of the smallests and
> largests of its parts, so merging adjacent buckets loses nothing at all.
> Splitting one does not work the other way.

So the pass that reads a line for the whole-line summary — which already touches
every element and then throws all but two thousand of them away — keeps them
instead, at the finest bucket `gui::PlotBudget` affords, with every coarser
level derived above it in steps of four. After that pass every run at every
resolution is a fold of a few thousand doubles out of a buffer already in hand:
`O(pane columns)` per frame whether the line is ten million elements or a
billion, and no file at all. `PyramidBuilder` is the streaming form both plots
read through, because a hundred-million-element line is not a buffer anyone
hands over whole.

Two rules hold across that boundary. **A tick is drawn where the curve was
drawn, or it is a lie** — `PlotFrame.yFraction()` and `PlotItem::yFraction()`
are two implementations of one rule and `tst_views` asserts they agree, over the
window on screen and outside it.
And **`PlotLine::values` is borrowed** — the models hand the item a pointer into
their own cache and copy nothing, so no path may free or prune a held line
without saying something about it first. There are two things it can say, and
invariant 6 below is where the difference lives: `PlotItem::clear()`, where the
old line stops being a reading of anything, and `retire()`, where it is the same
data read again and goes on being drawn until the replacement arrives.
`test_models` and `test_customplot` do the dangerous thing both ways and check
what the item is reading afterwards — by dereferencing it, because a line count
would be just as happy over freed memory.

A held line can also be freed by something that never meant to free anything:
**a `std::vector` holding one of those caches must relocate by moving, and it
only does when the element's move constructor is `noexcept` or there is no copy
constructor to fall back on.** `std::vector` reallocates with
`std::move_if_noexcept`, so a copyable element whose move can throw is
*deep-copied* into the new storage and the original freed — with the renderer
still pointing at it. Whether that happens is down to the standard library:
`std::map`'s move is `noexcept` on libstdc++ and libc++ and is not on MSVC's, so
a `push_back` that grew `DatasetPlot::levels_` passed everywhere but segfaulted
on Windows. `DatasetPlot::Detail` therefore has its copy **deleted**, both
retired stores hold bare `std::vector<double>`, and `static_assert`s next to
each of them say so on every platform rather than on the one that noticed.

## Compound data: `.member` indexing

A compound used to be a terminus — `isNumeric` is Integer|Float only, and the
only compound-aware code above `h5core` read one cell and opened it out in
`CompoundPane`. A reader with a ten-million-row event table could look at struct
number four and nothing else.

`.member` is what unlocks it, and the whole of it rests on one rule:

> **A subscript written after `.b` binds to the axes `b` itself contributes, and
> to nothing else.** So `array[i1,i2,i3].b[i4]` and
> `(array[:,:,:].b)[i1,i2,i3,i4]` are the same selection — element for element
> and shape for shape — and under the other reading, where `[:]` after `.b`
> would address the whole result, they are not.

That identity is not preserved by hand; it *is* the implementation. The member
projection happens first and produces one derived shape, `dataset ++ memberDims`,
and after that there is one ordinary slice over the whole of it. The short
spelling is resolved by handing its subscripts back to be written onto that same
slice line (`MemberChain::folded`), so a member subscript is never a second kind
of subscript and the two spellings have nothing to drift between.

Four pieces:

- **`h5core::TypeInfo::members`** — a recursive member tree beside the flat
  `memberNames`, which stays as it was (the Information panel prints it, and an
  enum's symbols live in it). A chain cannot be followed through names alone.
- **`h5core::FieldDataset`** — the third `DataSource`, after `Dataset` and
  `postproc::ComputedDataset`. The table, the plot, the image and the pipeline
  are handed one instead of a `Dataset` and none of them has a branch for which
  it got. **It reads the member and not the struct:** the memory type is a
  compound holding just the named member (`H5Tcreate` + one `H5Tinsert`, nested
  per link), so HDF5 extracts that field during the transfer. The member's own
  axes are then selected *in memory*, because HDF5 cannot hyperslab inside an
  `H5T_ARRAY` member — bounded by the member's extent, which is small by nature.
- **`postproc::MemberPath`** — the grammar, beside the subscript grammar for the
  reason already written over that one. Resolving is arithmetic over a
  `TypeInfo`, so it costs no read and answers on every keystroke.
- **The two entry points.** The slice bar grows a second box after the closing
  bracket, shown only for a compound; `sliceText` keeps its exact meaning, which
  is what leaves the pipeline's slice row alone. A custom tab types the whole
  line at once, and there a chain is recognised **only after a `]`** — a link
  name holds a `.` as freely as it holds a `[`, so `/data/run.3` is a dataset
  and not member 3 of `run`. Every expression without a `].` in it parses
  exactly as it always did, which is why saved views migrate for free.

**The vlen rule.** A vlen's length differs in every record and every view here
is a rectangle, so it contributes no axis: `.tags` keeps the dataset's shape and
reads as the list (not numeric, so it does not plot), `.tags[3]` keeps the shape
and reads as what the list holds (so it does), and `.tags[0:2]` is refused.
A record whose list is too short has no value there — an empty cell, and a NaN
in a line, which is where a stroke ends. Nothing in that needs to know how long
any record's list is, so nothing reads the whole dataset to draw the start of it.

`/plotting/events` in the example file is a hundred thousand records with one of
every member class in the same struct. `/series/pairs` in the test fixture is
`/series/a` and `/series/b` again as structs, which is what lets
`test_customplot` assert that a member and a dataset of its own cost the same —
**read for read**, not only value for value. A member read that fell back to a
round trip per element would draw exactly the right picture.

## Invariants worth knowing before editing

1. **One thread ever calls into HDF5.** The pinned HDF5 is not thread-safe and
   the failure mode is a segfault, not a wrong answer. `h5core::thread::claim()`
   takes the claim and every public entry point in `h5core` calls `check()`,
   which aborts in release as well as debug. Everything HDF5 owns lives in
   `gui::H5Session`, reachable only from inside a job run by `gui::H5Thread` —
   there is no `shared_ptr<File>` for a model to hold, deliberately. Replies use
   `H5Requests` tickets so a stale answer is dropped rather than painted.
2. **Files are opened read-only and never written.** That is the product
   promise, not an implementation detail.
3. **Qt is static**, so QML modules and platform plugins resolve at *link*
   time. A new `import` in a `.qml` file or a new target that loads QML needs
   the module and its plugin named in CMake, or the binary builds cleanly and
   then dies at startup with `module "X" is not installed`. Same for platform
   plugins via `qt_import_plugins`.
4. **The release floor is glibc 2.28 / RHEL 8** on Linux and the static CRT on
   Windows. `-static-libstdc++`/`-static-libgcc` and
   `CMAKE_MSVC_RUNTIME_LIBRARY` in the root `CMakeLists.txt` are what hold it;
   `tools/check-glibc-floor.sh` and `tools/ci/verify-*` prove it. Do not add a
   dependency the target may not have — `libxcb-cursor` is already handled by
   `ports/xcb-util-cursor` and asserted at configure time.
5. **Design tokens are the only source of visual values.** No hex colour, no
   font family, no raw pixel number outside `Theme.qml`, and **no animations
   anywhere** — this application changes state between one frame and the next.
   Anything that elides needs an `AppToolTip` within ~25 lines so the reader can
   still get at the data.
6. **The views stream.** The table reads the block it is about to paint; the
   plot reads a line. Postprocessing is the exception — it must materialise, and
   is capped at `postproc::kMaxElements` (2^24 doubles, 128 MB).

   What the plot reads is a min/max **envelope** rather than every nth element:
   it reads the elements those samples were being chosen from and keeps the
   extremes of each bucket, so a spike one sample wide cannot be thinned away.
   It reads the same elements a stride would have skipped over and no more, in
   hyperslabs of up to `kReadRun` — a read carries as many whole buckets as it
   can reach, so the round trips follow the *length of the line* and not the
   number of buckets it is folded into. They used to follow the bucket count,
   and `all` on a ten-thousand-line table was 1.28 million reads of eight
   values each: two seconds of a frozen window to move twenty megabytes.
   `tests/test_cost.cpp` holds both halves — the elements exactly once, the
   reads bounded by `kReadRun`. **Both** plots reduce a line that way, by the
   same arithmetic and out of one implementation of it (`gui::reduceBuckets`),
   because a reader who puts a dataset on the Plot tab and the same slice in a
   custom tab is looking at one dataset: `test_customplot` compares the two
   value for value *and* read for read. It compared only the values until
   0.5.2, and that is exactly how a custom tab went a release asking HDF5 for
   one bucket at a time — same picture, a thousand times the round trips, no
   assertion anywhere that noticed. `CustomPlot::hyperslabs()` is the count
   that would now.

   **A bucket is a column, and a column is a device pixel.** What a line is
   thinned to follows the pane — `setPaneColumns`, quantised — rather than
   being a constant, because a constant is wrong in both directions: fewer
   buckets than the pane has pixel columns draws an envelope as a hatch of
   separated teeth instead of a band, and more points than can be told apart
   are read and held for nothing. The width is measured in *device* pixels
   (`PlotSurface.pushColumns`, `PlotView::pixelRatio`), because a pane is laid
   out in logical ones and drawn into a framebuffer with `devicePixelRatio` of
   them for each — so a HiDPI display summarising to the logical count throws
   away exactly that factor before anything is drawn. Down to the quantum
   rather than up, because the renderer summarises again in powers of two if it
   is handed more than `kSamplesPerColumn` points per column.

   **A closer look is folded, not read.** The whole-line summary is of the
   *whole* line, so zooming in would stretch it rather than resolve it. The plot
   draws the run on screen at a finer bucket — `gui::windowFor` decides the run,
   `DatasetPlot::setVisibleRange` and `CustomPlot::setVisibleRange` ask for it —
   and keeps the whole-line summary beside it, so the y axis stays the line's
   true extent.

   Until 0.5.2 that run came back from the file, a round trip and a settle
   later, one at a time. It now comes out of `gui::LinePyramid`: the line is
   held whole from the one pass that read it, so `DatasetPlot::fillDetail` and
   `CustomPlot::fillCloser` answer **in the same call that asked**, and the
   whole ladder the policy wants — the run on screen, the octaves in towards
   the pointer, the octaves out — is folded before the frame that asked for it
   is drawn. `refreshDetail` loops rather than returning, because there is no
   reply to arm the next step with.

   The pane gets its *own* preferred bucket on every frame, even when a coarser
   run in hand would have covered it. Settling for that run was right while the
   alternative was a round trip and it cost up to an octave — about one drawn
   station per column where the pane asked for two; out of a held line the finer
   fold has nothing to weigh against.

   `H5Thread::submit` is still the path below the pyramid's base bucket, which
   is the only zoom a line too large to hold at bucket one still reads for. Those
   are the cheapest reads there are: below the base, a run's span is at most a
   paneful of base buckets, which is a single hyperslab however large the
   dataset. `kPrefetchOctaves` and `kFocusOctavesIn` still shape what is asked
   for, and runs are still never thrown away when the view leaves them, up to
   `kHeldLevels` and whatever the budget affords.

   **A zoom says where it is going, and is read that way, at once.** The
   surface has always known where the pointer was — a wheel event carries it,
   and `zoomedAxis` holds that value still under it — and until 0.5.2 only the
   resulting range crossed into the model, so the runs read ahead were centred
   on the middle of the frame and a reader zooming into a corner walked off
   them after a step or two. `setZoomFocus` pushes it, `gui::PlotFocus` carries
   it, and the inward octaves (`kFocusOctavesIn`, four of them) are read
   towards it. They can be four deep where `kPrefetchOctaves` is two because
   each inward run is *half* the span of the one above: the whole inward ladder
   is cheaper than one step out.

   A focused gesture also skips the settle and reads immediately. What bounds
   that is not a wait but **one read out at a time** — `inFlight_`,
   `closerInFlight_`, whose reply arms the next — so a wheel spun through six
   octaves cannot queue six reads of runs the reader has already left on a
   thread that runs them one after another. A **pan** has no focus and still
   settles, because a pan that leaves the run in hand would otherwise read on
   every frame of the drag. Clearing the focus (`clearZoomFocus`, from `panBy`
   and `resetView`) is what puts it back.

   A job a gesture submits must not copy the table it reads. `TableAxes` carries
   one index per element of every dimension, so a copy of one is eighty
   megabytes on a ten-million-element vector and eight hundred on a
   hundred-million one — a memcpy on the GUI thread before the read has even
   been queued, which measured as a single 280 ms frame in the middle of a zoom
   on `bench-zoom`. The axes are therefore *owned* through a `shared_ptr`
   (`DatasetTableModel::sharedAxes`) rather than copied into one, and a job
   still holding the old one is reading the table it was submitted about.

   What `test_cost` holds it to: the **renderer** never reads, the whole line is
   read once in hyperslabs bounded by `kReadRun`, and after that **a zoom reads
   nothing at all** — not a gesture, not a pan inside a run, not a resize, not
   retracing the way back out. The case that says it is *"a zoom from the whole
   line to a single sample reads nothing"*: twenty frames from the whole of a
   ten-million-element line down to one sample per column, each checked against
   the elements themselves, with zero crossings and zero reads across all of
   them. A change that made any of those read would not look like a bug, it
   would look like the plot had become slow again.

   The values are checked as hard as the counts, and deliberately: a cache that
   is fast and subtly wrong counts exactly like one that works. `test_plotlevels`
   asserts a coarsened envelope equals a read at that bucket value for value,
   and `test_cost` asserts every drawn sample of every frame equals the element
   it claims to summarise. `tools/bench-zoom` is the milliseconds beside those
   counts — it prints per-frame min/median/p99 against a 5 ms budget, and how
   many frames still held the one-sample impulse the pointer was put on.

   **What is drawn and what is held are two budgets.** `kDrawBudget` bounds
   what the renderer walks — every drawn line is projected on every frame, so
   that one answers to the frame rate and not to the machine. What may be
   *held* is `gui::PlotBudget`: a fraction of physical memory, detected through
   `sysconf`/`GlobalMemoryStatusEx`, chosen by the reader under **Settings >
   RAM Budget** (low/medium/greedy) and shared out between the Plot tab and
   every custom tab rather than held per object — N tabs each holding their own
   constant is how a generous number becomes an unbounded one. They were one
   number until 0.5.2, which is why that number had to be sixteen megabytes and
   why five held runs was all a hundred-million-element dataset ever got.

   What the held budget buys is the pyramid's **base bucket**
   (`gui::baseBucketFor`), and that is the one number a reader can feel. A
   pyramid costs its base and a third again, so a line the budget can hold at
   bucket one is a line where no zoom reads anything ever; one it cannot is held
   at bucket two, four, sixteen, and the octaves below that base are the only
   ones that still go to the file. On a 16 GB machine at **medium** that is
   bucket one up to about a hundred million elements and bucket sixteen at a
   billion — so a billion-element trace still zooms free for the first ten
   octaves and costs one hyperslab for the rest.

   **Nothing is freed under a renderer that is reading it, and nothing blanks
   the pane to avoid that.** The borrow contract has two halves.
   `releaseDrawing()` empties the item and is right only where the old line
   stops being a reading of anything — a new dataset, a row removed or retyped.
   Everywhere else — re-thinning for a pane of a different width, a closer look
   replaced, the cache pruned to the drawn set — the old values are **retired**
   (`DatasetPlot::retire`, `CustomPlot::retire`) and freed in `fill()`, the one
   moment a renderer that was borrowing them has just been handed something
   else. Releasing everywhere was a blank pane for a frame or more on every one
   of those, which is what the reader saw when a rail opened. A resize is also
   debounced (`kResizeMilliseconds`), so a drag of the window's edge reads once
   at the end rather than once per sixty-four pixels.
7. **The version is counted from release tags**, never typed. Major/minor live
   in `cmake/Version.cmake`; the patch is how many `vMAJOR.MINOR.*` tags exist.
8. **Every tag carries a `CHANGELOG.md` section**, headed `## MAJOR.MINOR.PATCH`
   and short. It *is* the release page: `tools/release-notes.sh` pulls it out
   and CI publishes it. That script refuses a section that is missing, empty,
   duplicated or word for word another version's, and it runs in the
   design-checks job on a tag push — six seconds in, rather than after both
   builds — so notes nobody wrote and notes nobody rewrote both stop the
   release rather than reaching the page. `tools/test-release-notes.sh` is the
   `release_notes` ctest test and covers all four.

## Conventions

- SPDX header on every file: `SPDX-FileCopyrightText: 2026 Jonas Sattler` and
  `SPDX-License-Identifier: GPL-3.0-only`.
- `.clang-format` (LLVM base, 100 columns, 4 spaces, braces on their own line
  for classes/functions) and `.clang-tidy` are checked in — match them.
- Doc comments are `///` on the declaration; `//` blocks above a decision
  explain *why*, and name the failure that prompted it when there was one.
  Match that density. Never strip an existing comment to "clean up" — those
  paragraphs are load-bearing documentation.
- Test names are sentences: `TEST_CASE("the tree filter narrows the pane",
  "[tree]")`, `function test_auto_width_fits_every_dataset_and_not_only_the_first()`.
- Commit messages follow the same voice: a lower-case-ish imperative sentence
  as the subject ("Fit the columns to the dataset that is open, not to a model
  with nothing in it"), then a body that argues the change — what was observed,
  what the measurement was, what was decided against. Wrapped at ~72. Keep the
  `Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>` trailer.
- Assert behaviour in counts, not durations (`tests/test_cost.cpp`): a duration
  measures the machine, a count measures the program.

## Gotchas

- `examples/`, `build/`, `dist/`, `instructions/` and `.claude/` are gitignored.
  `instructions/` is the author's working notes — do not resurrect it into the
  repository, and never `git add -A` blindly.
- Headless anything needs `QT_QPA_PLATFORM=offscreen`. The offscreen platform
  declares no RHI capability, so Qt Quick falls back to the software renderer,
  which draws **no custom `QSGGeometryNode` at all** — it knows rectangles,
  images, nine-patches and glyphs and silently drops the rest. `gui::PlotItem`
  therefore carries a QPainter fallback so the QML suite still sees a line;
  `make-screenshots` asks for the `rhi` backend explicitly so that the pictures
  are of the geometry that ships rather than of the fallback.
- Windows: keep the checkout and vcpkg at short paths (`C:\src\H5Scope`,
  `C:\v`). `MAX_PATH` bites during the Qt link and reports it as
  `LNK1181: cannot open input file` naming a file that exists.
- Warnings are on (`-Wall -Wextra`, `/W4`) but not `-Werror`; the CI build log
  is where a new one is meant to be noticed.
