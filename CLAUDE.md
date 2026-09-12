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
| `src/h5core/` | The HDF5 backend. **No Qt at all** — links only `HDF5::HDF5`. Keep it that way; it is what makes the layer testable headless. |
| `src/postproc/` | The numpy-shaped pipeline (slice, transpose, reshape, reduce…). Links `Qt6::Core` for `QString` only; no `QObject`, AUTOMOC off. |
| `src/gui/` | `QAbstractItemModel`s, `AppController`, and the HDF5 thread. QML module URI `H5Scope.Backend`. |
| `src/qml/` | The UI. QML module URI `H5Scope`, target `appqml`. `Theme.qml` is the singleton every visual value resolves through. |
| `src/main.cpp` | Command line (`--version/--help/--license/--notices`), fonts, icon, engine. |
| `tools/` | `make-example-file`, `inspect-file`, `bench-tree`, `bench-data`, `make-screenshots`, the CI scripts and the two design checks. |
| `tests/` | Catch2 suites (`test_h5core`, `test_postprocess`, `test_h5thread`, `test_models`, `test_example`, `test_cost`, `test_customplot`) plus the Qt Quick Test QML suites under `tests/qml/`. |
| `cmake/`, `ports/`, `packaging/` | Version counting, licence collection, the `xcb-util-cursor` overlay port, icons and the Windows resource. |

QML talks to exactly one object: `AppController` (`QML_SINGLETON`). The models
hang off it as `CONSTANT` properties; `DatasetPlot`, `DatasetImage`,
`TableSetupModel`, `PostprocessModel`, `CustomPlotSet` and `CustomPlot` are
`QML_UNCREATABLE` and obtained from it. `FileSystem` and `FocusRelease` are the
other registered types.

The custom plot tabs are the one part of the UI that is **not** about the
selection: `CustomPlotSet` holds the reader's own tabs, each a `CustomPlot` of
1-D slices named as `path[subscript]` and drawn together. `CustomPlot` is
deliberately shaped like `DatasetPlot` from the outside, which is what lets
`PlotSurface.qml`, `PlotLegend.qml` and `PlotSettingsPanel.qml` draw either one
without being forked. The tabs answer to the *file* rather than to the tree, so
`AppController::openFile`/`closeFile` empty them — but the **saved views do
not**: they are written to `QSettings` and outlive both the tabs and the file,
and each reports how much of whatever is open it can still draw. That and the
recent-files list are the only two things this program remembers between runs,
and both are guarded by `QCoreApplication::organizationName().isEmpty()` so the
tests and `make-screenshots` never touch the user's settings.

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
  which cannot draw Qt Graphs' grid — `make-screenshots` asks for the `rhi`
  backend explicitly for that reason.
- Windows: keep the checkout and vcpkg at short paths (`C:\src\H5Scope`,
  `C:\v`). `MAX_PATH` bites during the Qt link and reports it as
  `LNK1181: cannot open input file` naming a file that exists.
- Warnings are on (`-Wall -Wextra`, `/W4`) but not `-Werror`; the CI build log
  is where a new one is meant to be noticed.
