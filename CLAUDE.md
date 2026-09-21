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
| `src/gui/` | `QAbstractItemModel`s, `AppController`, the HDF5 thread, the plot renderer (`PlotItem` + `PlotProjection`) with the cache under it (`PlotLevels` + `PlotPyramid` + `PlotBudget`), `Completion` — which of the three grammars on a typed line the caret is in — and `NameIndex`, every name in the file in one block of memory so the filter box answers out of RAM. QML module URI `H5Scope.Backend`. |
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
recent-files list, the RAM budget under Settings and the export settings under
Settings > Plot Settings are the only four things this program remembers
between runs, and all of them are guarded by
`QCoreApplication::organizationName().isEmpty()` so the tests and
`make-screenshots` never touch the user's settings. A fifth follows
`ramBudget`'s shape exactly: clamped on the way in from `QSettings` as well as
from QML, written in the setter under the guard, one `NOTIFY` of its own. The
export group has grown that way twice now — the size, then the density — and
both times without a new mechanism.

A line of a custom tab may also carry a **colour of its own**
(`CustomPlot::Entry::colour`, `seriesOverride`), which sits over whatever
cycle is in force and moves nothing else. It travels in `CustomPlot::state()`
and so in a saved view, written only where there is one — which is why every
view saved before it existed reads back unchanged. `DatasetPlot` answers the
same question with "none" for every line, and that is not a stub: the Plot
tab's lines are rows or columns of one dataset, so what identifies a line
there is its place in the table and the cycle already says that. Both plots
are drawn by `PlotSurface.qml`, which asks the question without knowing which
plot it has.

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
- **The three entry points.** Over a compound the slice bar makes *everything
  after the path* one box — `[:, 2].samples`, brackets and all — and keeps the
  bracketed `path[ box ]` form for everything else. `sliceText` and `memberText`
  keep their exact meanings underneath, which is what leaves the pipeline's
  slice row alone; `selectionText` / `applySelection` / `selectionError` are the
  one line the bar reads and writes them through, checked whole and applied
  whole. It began as a second box after the closing bracket, and that was the
  wrong shape: a chain and the subscript over the axes it appends are one
  statement, so rearranging one is usually rearranging both, and two boxes made
  that two commits with a shape nobody asked for in between — and the second box
  was a few characters wide with a grey `.member` in it, which reads as a value
  somebody chose rather than as a box nobody has typed in. A custom tab types the
  whole line at once, and there a chain is recognised **only after a `]`** — a
  link name holds a `.` as freely as it holds a `[`, so `/data/run.3` is a
  dataset and not member 3 of `run`. Every expression without a `].` in it parses
  exactly as it always did, which is why saved views migrate for free.
  The postprocessing panel grows a **Select** row, and it is the same
  relationship the slice row has to the slice bar: not a copy of what the bar
  offers, *it is it*. It sits above the slice and is furniture rather than an added
  operation, because after a transpose or a reduction there is no compound left
  to select from — an operation legal in exactly one position is not an
  operation, it is a property of the input. Its list is
  `postproc::memberChains`, which is what the completer offers too.

Three consequences worth keeping in mind when editing around it:

- **Every row number in `PostprocessModel` is relative.** The Select row moves
  the slice off row 1, so `sliceRow()`, `stepRow()`, `stepIndex()` and
  `stageOf()` are the only places a row is turned into anything — a constant
  that is right in four of those places and wrong in a fifth is how this breaks.
- **`setDataset` clears the pipeline only for a *different* dataset.** Naming a
  member comes back through it, because the shape below the member changed, and
  clearing on that would have the Select row turn its own panel off every time
  it was used. A step the new shape cannot take stops the walk and says so,
  which is the panel working.
- **`applyMember` emits `selectionAboutToChange` as well as
  `selectionChanged`.** Every `DatasetMemory` in the UI reads the second as "put
  back what is filed for the dataset now current", so without the first, naming
  a member reverted the plot's range, the image's colour axis and the pipeline's
  switch to whatever they were when the reader last *left* that dataset. The
  dataset is not changing, so filing and restoring under one name is the
  identity it should be.

**Completion** (`gui::Completion`, `src/qml/CompletionPopup.qml`). Two boxes in
this application are typed into rather than chosen from — a custom plot's entry
and the slice bar's own box — and the names in both come out of the file and
nowhere the reader can see them. `completionRequest` says which of the three grammars on a
line the caret is in; a path offers the children of the group being typed into,
a closed subscript offers the datatype's chains, and an open subscript offers
nothing, because what may be written there is every integer and every range and
that is not a list. A path completes **with the subscript that selects the whole
of the dataset**, of the right rank, which is the half a reader would otherwise
count dimensions for. Tab behaves as a shell's does: it writes as much as every
candidate shares and only then chooses.

One hazard, and it is the sort that locks a window rather than merely misbehaves:
**`DatasetLookup::resolve` runs its continuation synchronously when there is
nothing to ask** — callers use it as "make sure, then go" — and the continuation
here is the signal that makes every box ask again. Asking about a path the cache
already knows and still cannot give a datatype for (a group, a broken link) is
therefore an unbounded loop with nothing between the turns of it. Every call into
`resolve` from the completer is guarded by `knows()` for that reason, and
`test_models` asserts that a question with no answer is asked once.

The constraint under all of it: **nothing reads more than the reader has already
asked to see.** The tree is lazy because a file can hold a million objects, so a
group that is not listed is *asked for* rather than walked, the answer arrives a
moment later, and `completionsChanged` is what tells the box to ask again. A
completer that listed its way down to answer a keystroke would spend exactly
what that laziness saves.

**Searching is the one exception, and `gui::NameIndex` is where it is made.**

> **The tree stays lazy and the search does not.** A name is the one thing about
> a file a reader may want to search the whole of without having looked at any
> of it, and it is small: three hundred thousand paths is eighteen megabytes.
> So every name is read once per file, in the background, and after that a
> keystroke is a linear pass over contiguous memory.

That laziness used to reach the filter, and it was wrong in both directions. It
matched what the reader had expanded, so a search over a file nobody had walked
found *nothing at all* and the only way to make it find something was to open
the tree by hand, which reads. And it was slow where it did work, because the
match happened per node of every subtree on every keystroke, with two QString
conversions and two PCRE2 matches apiece: three hundred thousand objects
measured at 160–330 ms a character.

Four things hold it up, and each is a failure it was built out of:

- **The walk is cut into jobs** (`kGroupsPerPass`). `H5Thread`'s queue is
  strictly ordered and there is one of it, so a single pass over a large file
  would put every listing the reader clicks for behind a second of indexing.
  Each pass lists a bounded number of groups and re-arms at the *back* of the
  queue. It uses `children(path, Resolve::Objects)`, because the kind is what
  says whether there is anything below a name and asking for it during the
  listing is one object-header read per name rather than two — measured at 1.0 s
  against 2.2 s over three hundred thousand objects, which is `H5Lvisit`'s own
  speed out of an interface that can be stopped and resumed.
- **It answers before it is finished, and says when it cannot say.** Marks are
  computed for whatever has arrived. A group the walk has not reached, one it
  declined to descend into, and everything past `kMaxNames` come back `Unknown`,
  and `TreeFilterProxyModel` then falls back to the recursive walk over what the
  model has read — which is exactly what it did before. So the filter is never
  wrong: it is complete where the index is and lazy where it is not. A group is
  only ever `No` once the whole walk is in, because a listed group still has
  unlisted groups under it.
- **A loop is an ancestor repeating itself, not a name seen twice.**
  `/aliases/alias_0000` and `/runs/run_0000` can be one object under two names;
  the tree shows the contents of both and so must this, or the second gets an
  `Opaque` where an answer was available. Only an identity already on the path
  from the root stops the walk.
- **`gui::matchesWildcard` is written out by hand.** PCRE2 is about a
  microsecond a call and there are two calls per name, which is a third of a
  second per character at three hundred thousand objects. A back-tracking glob
  is two orders of magnitude cheaper on the patterns people write, because
  almost every one of them fails on the first character. `test_models` holds it
  against `QRegularExpression::fromWildcard` over a table of patterns and names,
  so the parity is asserted rather than claimed. The same applies to the case
  fold under it: `QChar::toCaseFolded` is an out-of-line call into QtCore, and
  doing ASCII inline first is what takes a keystroke from 23 ns a character to
  under two.
- **...and most patterns never reach it.** A back-tracking glob is cheap when it
  fails on the first character and expensive when it does not, and a leading `*`
  is how it never does: the rest of the pattern is then tried at every offset of
  every name. Measured over 188,000 names, *every* pattern opening with a star
  cost 14–22 ms against 3 ms for a plain substring — `*item*zz*` among them,
  which matches nothing at all in that file, so the cost is the walk and not the
  hits. (In a debug build, which is where a reader's own slow keystroke is
  easiest to reproduce, the same patterns are 210–260 ms against 19 ms.)

  > **A glob of stars and letters is a sequence of `indexOf`s.** Back-tracking
  > is there for `?` and `[...]`; a pattern with neither is "these pieces, in
  > this order", each end pinned only where the pattern has no star to eat what
  > is outside it.

  So `NameQuery` cuts such a pattern into its literal runs once and answers with
  one folded scan per run — the scan plain text already got, and within a
  whisker of its cost: 5.5–6 ms for every pattern above. The answers are the
  same ones: `test_models` holds `NameQuery` itself against
  `QRegularExpression::fromWildcard` through both of the questions the filter
  asks. Two more things fall out of it. A pattern that opens with a star is
  asked only about the *path*, because a name it matches is a path it matches;
  and one that still needs the walk rejects on its longest literal run first,
  which is a necessary condition and one scan.

Three things live above the index rather than in it, and none of them is about
the matching: two are what the *view* costs, and the third is when the search
runs at all.

- **A search starts from a closed tree.** Every row on screen when the filter
  changes has to be taken out of the view one run of adjacent losers at a time,
  and QQuickTreeView pays for each of those over the whole of its flattened row
  list. A reader who had opened a group of sixty-five thousand members and then
  typed `item*7` waited twenty-two seconds for one keystroke. `ObjectTree.qml`
  collapses when a search begins — what was open is already written down, and
  already put back when the box is cleared — and the same keystroke is then a
  few milliseconds. What is left is `QSortFilterProxyModel`'s own list surgery,
  which is `QList::remove` per interval and so quadratic in the width of a
  group; it is a second at sixty-five thousand members and unmeasurable at
  eight thousand. `invalidate()` would replace all of it with one
  `layoutChanged` — and does, in about fifty milliseconds — but it throws the
  mappings away under every `QModelIndex` already handed out, which
  `QQmlTreeModelToTableModel` answers with "Invalid index" warnings and an
  intermittent use-after-free. Do not reach for it.

  It collapses on the *keystroke* rather than on the search, which is the half
  that was missing: a tree closed after the first search has been applied saves
  every search except the one that was expensive. A box emptied before the
  search it armed ever ran is therefore a search the box itself has to end
  (`endSearch`), because nothing filtered and nothing will report that it has
  stopped.
- **A keystroke arms a search rather than running one.** The box settles
  (`filterSettleMilliseconds`) and each character abandons the search the one
  before it armed, so `temperature` is one search and not eleven — the ten in
  between are for prefixes the reader is already abandoning, and on a large file
  each of them is a pause in the middle of their typing. Clearing is not
  settled: an empty box is the file being asked for back, and putting it back
  was never the slow direction.
- **Opening the tree to the results is bounded** (`kRevealLimit`). A result in a
  branch nobody has expanded is still a result, and `H5TreeModel::revealPath`
  will list the way down to it — but a search that matched more rows than a pane
  could show is not a result to be opened, it is a search to be narrowed, so
  past the bound nothing is opened at all rather than the first two hundred of
  a quarter of a million. It is also settled (`kRevealMilliseconds`) rather than
  run per keystroke, because `temperature` typed a character at a time would
  otherwise list the file's way down to the results of eleven prefixes, ten of
  them abandoned by the next character.

**The vlen rule.** A vlen's length differs in every record and every view here
is a rectangle, so it contributes no axis: `.tags` keeps the dataset's shape and
reads as the list (not numeric, so it does not plot), `.tags[3]` keeps the shape
and reads as what the list holds (so it does), and `.tags[0:2]` is refused.
A record whose list is too short has no value there — an empty cell, and a NaN
in a line, which is where a stroke ends. Nothing in that needs to know how long
any record's list is, so nothing reads the whole dataset to draw the start of it.

The Information tab opens a compound out too: the datatype panel carries a
**"Resolves to"** tree under its one-line member list, indented until nothing is
left but base types. An array or a vlen contributes no row of its own — `array[4]
of float64` has resolved itself in the saying of it — and an enum gets a row
listing its symbols, because a nested one has no Members row to print them in.
The indent comes out of the label column rather than pushing the value column
along with it, so the values stay a column however deep the name beside them is.
`h5core::toJson` opens one out as well, and by the same instinct: a struct always
breaks across lines, a list does so only when it holds structs or lists of its
own, so `"samples": [0, 0.25, 0.5, 0.75]` stays where its name is.

`/plotting/events` in the example file is a hundred thousand records with one of
every member class in the same struct, and `/types/compound/tracks` is the one
composition it does not have: an array member whose elements are themselves
structs. `.trail` appends an axis of three and `.trail.x` is that axis with a
name after it — rank 2 out of a rank-1 dataset — which is where "an array member
appends an axis" and "a chain goes on through a compound" have to hold at once.
It is also the only element in the file whose JSON has to open a list out over
lines. `/series/pairs` in the test fixture is
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

   The line palettes are two kinds of thing and are held to two different
   rules. `spectrum` and `safe` were *generated* against this application's two
   grounds, so every entry clears 3:1 on both and `tst_views` asserts it.
   `okabe-ito`, `tol bright` and `tol muted` are the *published* cycles, kept
   at their published values so a figure from here and one drawn in matplotlib
   or R agree about which line is which — the one substitution being
   Okabe-Ito's black, which is the plot's own ground in the dark theme and is
   drawn at signal white there. They were designed for ink on paper and their
   palest entries do **not** clear 3:1 on the light theme's white; that is the
   stated cost of exactness, `tst_views` pins them stop for stop so a
   well-meant deepening fails rather than passing quietly, and a reader who
   wants a cycle solved for a screen has the other two one pick away.

   There is one departure from a published *order*, and it is Okabe-Ito's:
   that achromatic entry is drawn **last** rather than first. A plot opens on
   this cycle and usually has one line in it, and published order made that
   line black — an ink the reader cannot tell from the chrome, and one a
   pixel-counting test looking for a saturated stroke could not see either.
   So the cycle opens on the orange and closes on the black; the colours and
   the cycle are still the published ones, offset by one.
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

   **A time base is a line, and is held like one.** A custom tab drawn against
   another dataset used to be the one axis of the three that could not be
   zoomed, and it failed in both halves at once. The line, because a range of x
   over a lookup table was refused outright; and the axis, because a time base
   was read once at a pane's worth of points and never again — so a reader
   zoomed past that was handed one x per column and the curve collapsed onto a
   staircase of vertical treads. The rule that unlocks it is one sentence:

   > **A time base that only ever goes one way is a map that can be run
   > backwards.** A range of x is then a range of positions, and everything the
   > index and range axes do — narrow the run, fold it finer, read towards the
   > pointer — follows without another line of policy.

   So `CustomPlot::axis_` is an `Entry` like any other, with its own pyramid and
   its own held runs, and `CustomPlot::axisPositionOf` is the way back: a
   bisection of the whole-line summary first, then again in whatever finer run
   of it covers that answer, because a bracket only as sharp as one drawn point
   of the summary is five thousand elements wide on a ten-million-element log
   and would pin every zoom three octaves short. `recomputeView()` inverts the
   view once per change, into axis positions, and every entry divides that by
   its own scaling — under Index and Range the same function is one division.
   `PlotAxis` carries the folded run **beside** the whole rather than instead of
   it, so a line whose own closer look has not landed yet is still drawn against
   positions the whole answers for. A time base that doubles back is not such a
   map, gets none of it, and draws exactly what it always drew;
   `positionOfX` asks the array it is about to search whether it is sorted,
   rather than asking the time base as a whole, because a summary can ascend
   while the elements under one of its buckets do not.

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

   `gui::extremesOf` is the innermost loop of all of it — every element of a
   line on the way to the summary, and every level folded above it. It reads
   **two comparisons an element and nothing else**, which works because *NaN
   fails every comparison*: seeded with the infinities, the first drawable
   element takes both branches and a NaN takes neither, so being drawable needs
   no test of its own. An actual infinity does pass one of them and is the one
   case it cannot decide, so a run holding one is handed to the careful reading
   instead. Worth the paragraph because of where it is: on a line too large to
   hold at bucket one — which is every line where the first draw is slow enough
   to notice — it is 46 ms against 35 ms of a 10M first draw.

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

   **Changing the budget changes what is held, and the two directions are not
   the same operation.** `applyBudget` used to trim the run ladder and nothing
   else, under a comment saying nothing was re-read — true when the only held
   thing was a handful of runs, and false from the moment a whole line was held
   beside them. A reader who noticed this program holding three gigabytes and
   turned the budget down got none of it back until they selected another
   dataset, and one who turned it up got no finer a base either.

   > **Coarsening is exact and free; refining is a read.** So a budget turned
   > down is honoured in the call that turns it down (`gui::coarsenTo`, which
   > drops the levels finer than the new base and leaves every picture at or
   > above it identical), and a budget turned *up* drops what it could improve
   > on and reads it again.

   Those two are also why the budget can be shared honestly at all: a tab
   opening emits `PlotBudget::changed` through `join()`, so the tabs already
   built shrink instead of the sum quietly exceeding the promise Settings
   makes. And what a run ladder may keep is measured against what the pyramids
   actually cost (`LinePyramid::doubles`, `heldDoubles()`) rather than against
   a halving of the share that assumed it — the number existed for a release
   and nothing consulted it, which is exactly how two claims on one share stop
   adding up.

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

   **The picture that leaves is a second frame, and it owns its values.**
   Settings > Plot Settings asks a copied plot to differ from the pane —
   publication colours, a size of the reader's choosing, a resolution of their
   choosing, the crosshair in or out — and none of the first three can be had
   by re-styling the frame on screen, because a grab renders the scene as it
   stands and the picture would be bought with a frame of the application in
   the wrong colours. `PlotPicture.qml` is therefore a whole `PlotFrame` built
   off screen, laid out at the size asked for (so its ticks and its type are
   that size's rather than the pane's, magnified), grabbed, and destroyed when
   the answer lands. Its colours come from properties with Theme defaults —
   `PlotFrame.ground`, `ink`, `ruleMinor`, `ruleMajor`, `axisRule`,
   `cursorInk` — because `Theme` is a singleton and cannot be flipped for the
   length of a grab.

   **Publication is the light scope and not one ink.** It draws the picture
   the light theme would draw: `Theme.paperInk` and its neighbours for the
   chrome, and `Theme.paperPalettes` — `palettesFor(false)`, the same table
   read against the other ground — for the strokes. It was one black stroke
   for every line until 0.6.4, which is the one thing a colour cycle exists to
   not be: six traces went out as six identical strokes with a caption naming
   colours the picture did not contain. `PlotSurface.seriesColor` takes the
   scope as a fourth argument so that every branch answers it — a palette is a
   second table, a ramp is the same map in both scopes, and the three colours
   that are *values* by the time they arrive (`colorSingle`, the ends of a
   `range`, a line the reader coloured) go through `Theme.paperColor`. That
   maps signal white to ink, which is not a nicety: the accent is white on the
   dark theme and a white line on a white page is no line. It is the exact
   mirror of the substitution okabe-ito already makes the other way.

   **The size says how many pixels; the dpi says how big they are.** Two
   settings, and both change the picture — which is what 0.6.4 got wrong by
   reading the dpi as a supersample, so 1920×1080 at 300 dpi came back at
   6000×3375 with the type as small against the figure as ever. A logical
   unit is 1/96 inch (`kExportBaseDpi`); the type, the rules, the gutters and
   the markers are a fixed number of units; so the dpi divides the pixel
   count into *fewer units* and everything in the picture grows against it.
   Three functions, all on `AppController` so that the dialog's readout and
   the surface's grab cannot be two answers: `plotExportScale` (device pixels
   per unit — the stated dpi over 96, else 1 for a stated size, else the
   display's ratio), `plotExportPixels` (**the dpi is not in this one at
   all**: a stated size exactly, else the pane at the display's scale, held
   under `kMaxExportPixels`), and `plotExportLayout` (the pixels over the
   scale, floored at `kMinExportPixels` units so a frame is never all
   gutter). `PlotPicture` is laid out at the layout and grabbed at the
   pixels. The chosen dpi also goes into the image (`ImageClipboard::copyItem`'s
   `dpi`, from `plotExportTaggedDpi`), because a count of pixels is not a size
   until something says how densely they sit; a picture taken at the display's
   scale is tagged with nothing, since "as many dots as this screen has" is
   not a claim about inches.

   What it must **not** do is ask the model to fill it. `fill()` records which
   item it last handed the lines to (`drawing_`), so a second fill moves that
   record and a `releaseDrawing()` arriving between the grab starting and the
   frame it renders on would empty the wrong item and free values the other is
   still pointing at. `PlotItem::adopt` copies instead — every `PlotLine`'s
   values *and* both of `PlotAxis`'s borrowed arrays, the whole time base and
   the finer run of it — into storage the item owns, so the model never learns
   the picture exists. Cheap for a structural reason: a `PlotItem` holds a
   pane's worth of points, not a file's worth.

   **A grab carries no alpha to rely on.** Under Qt Quick's software renderer
   — what runs wherever there is no graphics API, and what the whole suite
   runs under — an item grab comes back `Format_RGB32` with untouched pixels
   at opaque black, so a frame drawing no ground grabs as a black slab. A
   publication picture is therefore drawn *twice* in one item, on white above
   and on black below, and `ImageClipboard::copyItem(…, composited)` recovers
   the alpha from the pair: source-over says a pixel of colour C at coverage a
   lands at `C*a + (1-a)` on white and `C*a` on black, so their difference is
   `1-a` and the black half is already the premultiplied colour. Exact rather
   than a colour key, so antialiased type and the feathered edge of a stroke
   keep the coverage they were drawn with — and the same answer on both
   renderers. It is also why `kMaxExportPixels` is half a texture ceiling
   rather than a whole one.
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
- **Qt Quick Test's `grabImage(item)` takes the item's *size* from the
  **window's** origin**, not from the item's own. So a grab of the plot surface
  — which sits `Theme.sliceBarHeight` down, under the slice bar — opens with 38
  rows of `surfaceRaised` that belong to the bar, and the plot looks as though
  it were standing on a raised surface when it is standing on true black. That
  is why `colouredPixels()` in `tst_views` takes a `firstRow`. When what is
  being measured is *where* something is drawn, grab through
  `ImageClipboard.copyItem` + `pixelOnClipboard` instead: that is a real
  `QQuickItemGrabResult` on the item and its origin is the item's own. The plot
  ground is asserted that way in *"the plot stands on black or on white"*.
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
