# Building H5Scope

[The README](../README.md) has the ordinary development build. This covers the
release build, the AppImage and the source bundle.

## Design goals

- **No system libraries.** Qt, HDF5 and Catch2 are built and version-pinned by
  vcpkg. Whatever the developer happens to have installed is irrelevant, and
  there is no fallback — the build fails rather than silently linking whatever
  the machine happens to have.
- **No system fonts either.** The two typefaces the design system specifies are
  compiled into the binary, so the UI looks the same on a machine with no fonts
  installed at all.
- **Static linking.** The resulting binary has zero Qt and zero HDF5 runtime
  dependencies. CI fails the build if `ldd` ever finds one.
- **Modern C++20**, split into a Qt-free backend (`h5core`), Qt model classes,
  and a Qt Quick/QML UI, so everything below the view is testable headless.
- **Consistent styling on every platform.** Qt Quick Controls with the *Basic*
  style, the only style that renders identically everywhere and carries no
  platform behaviour of its own. All visual decisions live in one `Theme`
  singleton of design tokens.

## Building what the release publishes

The README's build targets the machine it runs on, which is what you want while
developing. A *release* build additionally has to start on RHEL 8, and that is a
property of the glibc it was linked against — nothing downstream can add it
later, and no amount of bundling can rescue a binary linked too high. So the
published artefacts are built inside an AlmaLinux 8 container with
`gcc-toolset-13`: RHEL 8's glibc 2.28, and a compiler new enough for C++20.

Only Docker is needed. The rest of the environment is described by
`tools/ci/el8.Dockerfile`, and `tools/ci/el8.sh` runs a command inside it with
the repository bind-mounted at its own path.

```sh
export VCPKG_ROOT=/path/to/vcpkg
tools/ci/el8.sh "$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics
tools/ci/el8.sh cmake --preset release
tools/ci/el8.sh cmake --build --preset release
tools/ci/el8.sh ctest --preset release
```

Then the AppImage, and the checks CI runs over both artefacts:

```sh
tools/ci/el8.sh bash tools/make-appimage.sh \
    "$(readlink -f build/release/bin/H5Scope)" dist

tools/ci/el8.sh bash tools/ci/verify-binary.sh build/release/bin/H5Scope
tools/ci/el8.sh bash tools/ci/verify-appimage.sh dist/H5Scope-*.AppImage
```

`tools/make-appimage.sh` bundles only what the host cannot be relied on to have.
The GL and EGL entry points, core libX11 and libxcb, and `libwayland-client` are
deliberately left out: they bind to the GPU driver, the running X server and the
compositor, and a bundled copy of any of them breaks the machines it was
supposed to help. The script's header lists what is excluded and why.

### The one library that could not be left to the host

Bundling and the glibc floor between them cover almost everything, but not
`libxcb-cursor`. Qt 6.5 and newer link the xcb platform plugin against it
unconditionally — it is in `PUBLIC_LIBRARIES` in qtbase's
`src/plugins/platforms/xcb/CMakeLists.txt` with no feature guarding it, and the
`xcb` feature's own configure test includes `<xcb/xcb_cursor.h>` — while RHEL 8
ships it in neither BaseOS nor AppStream. It is in EPEL and nowhere else.

That made the bare executable unusable on a stock RHEL 8 desktop: not a missing
platform plugin, but the dynamic loader refusing the binary before `main`,
naming a library the user has no ordinary way to get. Stubbing the three
symbols involved is not an out either — `QXcbCursor::createFontCursor()` starts
`if (!m_cursorContext) return XCB_NONE;`, so a context that fails to initialise
does not fall back to the glyph cursors below it, and the application would run
with no mouse cursor at all.

So the library is built statically and linked in, like Qt and HDF5:

- `ports/xcb-util-cursor` is an overlay port that builds `libxcb-cursor.a`.
  `CMakePresets.json` sets `VCPKG_OVERLAY_PORTS` so every preset finds it.
- The root `CMakeLists.txt` sets `USE_XCB_CURSOR_STATIC` before
  `find_package(Qt6)`. Qt does not record where it found the xcb libraries —
  its exported dependencies file re-runs `find_package(XCB COMPONENTS CURSOR)`
  at *this* project's configure time — so that switch is what decides which
  file `XCB::CURSOR` resolves to. It then asserts the answer was an archive.
- `tools/ci/verify-binary.sh` fails the build if `libxcb-cursor.so.0` ever
  reappears in the executable's `NEEDED` list.

`tools/ci/el8.Dockerfile` still enables EPEL, and that is not a contradiction:
`xcb-util-cursor-devel` is needed there for *qtbase's own* configure test at
build time. Nothing in the published binary depends on it.

`tools/check-glibc-floor.sh` is what makes "runs on RHEL 8" checkable rather
than asserted, and it works on any ELF:

```sh
tools/check-glibc-floor.sh 2.28 build/release/bin/H5Scope
```

## Building from the source bundle

The bundle attached to each release contains everything needed to rebuild that
release without contacting any network: this repository at the released commit
— `ports/` included — the upstream source archive of every library linked into
the binary, and the vcpkg `ports/` tree at the pinned baseline, which carries
vcpkg's patches.

```sh
tar --zstd -xf H5Scope-<version>-source.tar.zst
cd H5Scope-<version>-source
./build-from-bundle.sh
```

The script points vcpkg at the bundled `vcpkg-downloads/` directory and at two
overlay port trees — the repository's own `ports/` first, then `vcpkg-ports/`,
the registry at the pinned baseline — and then runs the ordinary release
preset. Because the bundle omits `.git`, it carries
the version it was cut at in `cmake/BundleVersion.cmake`, which
`cmake/Version.cmake` reads when there is no history to count.

## Releases

`main` is always releasable and every push to it is built and tested, but a
release is cut by pushing a tag:

```sh
git tag v0.2.0
git push origin v0.2.0
```

That runs the `release` job in `.github/workflows/ci.yml`, which takes the
binary the build job already tested, assembles the Corresponding Source bundle
with `tools/make-source-bundle.sh`, and publishes both — with the licence, the
notices and their texts — as a GitHub release. Builds of `main` are kept as
short-retention CI artifacts, which is a convenience, not a release.

The job refuses to publish a tag that disagrees with the binary the build
produced. There is nothing to guess: the patch number counts the releases in
this series, so the tag to push is whatever the last build called itself. Only a
new major or minor is a decision, and that is two numbers at the top of
`cmake/Version.cmake`.

## Checks

Two design-token checks run in CI and under CTest, before the build:

- `tools/check-design-tokens.sh` rejects raw hex colours, raw pixel numbers and
  font families named anywhere outside `src/qml/Theme.qml`.
- `tools/check-elided-text.sh` rejects text that can elide without a tooltip to
  read it in full.
