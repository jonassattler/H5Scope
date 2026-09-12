# Third-party notices

H5Scope links every dependency statically, so a single executable contains
the code of everything listed here. This file is the *inventory* of what is in
that executable and under which licence. It is compiled from the link line of
the release binary, not from the dependency manifest: `vcpkg.json` names six
packages, and those six pull in everything below.

An inventory is not the notice most of these licences ask for. BSD-3, MIT,
Zlib, libpng and bzip2 each require the copyright notice, the list of
conditions and the disclaimer to be reproduced in the materials accompanying a
binary distribution — HDF5's second clause says so in as many words — and an
SPDX identifier in a table is none of those three. Those texts are in
**THIRD-PARTY-LICENSES.txt**, published beside the binary and generated at
build time from the licence text vcpkg installed with each port, so every entry
corresponds to the version that was built rather than to whatever upstream
carries today. See `cmake/ThirdPartyLicenses.cmake`.

H5Scope itself is licensed under the GNU General Public License version 3
only; see [LICENSE](LICENSE) and the *Why GPLv3* section at the end of this
file, which explains which dependencies force that choice.

All three documents are compiled into the executable as well, since a single
self-contained binary is the only thing this project distributes and a reader
who has only that has nowhere else to look: `H5Scope --license` prints the
GPL, and `H5Scope --notices` prints this file, the licence texts and the
fonts' OFL. Neither needs a display.

## Qt 6.11.1

Copyright (C) The Qt Company Ltd. and other contributors.

**Every Qt module linked into H5Scope** is available under
`LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only`.
They are conveyed here under GPL-3.0-only, which the LGPL permits: LGPLv3 is
GPLv3 plus additional permissions, and GPLv3 section 7 allows those additional
permissions to be removed from a covered work.

This used to say something different, and the difference is worth recording.
Qt Graphs and Qt Quick 3D were available only under `LicenseRef-Qt-Commercial
OR GPL-3.0-only` — Qt's own licensing page lists both under "Modules available
under GNU General Public License v3" — and H5Scope linked Graphs to draw the
plot, with Quick3D arriving because the `qtgraphs` package required it even
though nothing here rendered in 3D. Those two were the only GPL-only components
in the tree, and they were what made the combined work GPLv3 by obligation
rather than by choice.

The plot is now drawn by the application itself, directly on the Qt Quick scene
graph, and both modules are gone from the manifest. **Nothing in this binary is
GPL-only any more**: every Qt module here is available under LGPL-3.0, HDF5 is
BSD-3-Clause, and the rest of the table below is MIT, Zlib and similar. H5Scope
remains GPL-3.0-only because that is the licence its author chose for it, which
is now the whole of the reason.

Linked Qt modules: Core, Gui, Qml, QmlMeta, QmlModels, QmlWorkerScript, Quick,
QuickControls2 (with the Basic, Fusion, Imagine, Material, Universal and
FluentWinUI3 styles and their implementation modules), QuickTemplates2,
QuickLayouts, QuickShapes, QuickEffects, QuickDialogs2, QuickDialogs2QuickImpl,
QuickDialogs2Utils, Svg, Network, Concurrent, OpenGL, ShaderTools,
PacketProtocol,
LabsFolderListModel, XcbQpa, WaylandClient, WlShellIntegration,
DeviceDiscoverySupport, EglFSDeviceIntegration, FbSupport, InputSupport, and
the platform, image-format, QML debug and Wayland decoration plugins that
accompany them.

That list is the Linux build's. On Windows the platform modules differ rather
than the licensing: XcbQpa, WaylandClient, WlShellIntegration, the Wayland
decoration plugins and the EGL and device-discovery support modules are absent,
and the Windows platform plugin is linked in their place. Every module named is
under the same terms on both.

Qt source: <https://download.qt.io/official_releases/qt/6.11/6.11.1/submodules/>

### Third-party code bundled inside Qt

| Component | Licence |
|---|---|
| Embree | Apache-2.0 |
| glslang | BSD-3-Clause AND Apache-2.0 AND MIT |
| SPIRV-Cross | Apache-2.0 |
| Wayland protocol definitions (`xdg-shell`, `wl-shell`, `fullscreen-shell-v1`) | MIT |

## Libraries built separately by vcpkg

Licence identifiers are the SPDX expressions declared by each vcpkg port at
baseline `00c5775211f45cd08b37fce0484b4cb940e422ab`. Where a port is
dual-licensed, the option H5Scope relies on is marked.

| Library | Version | Licence |
|---|---|---|
| HDF5 | 2.2.0 | BSD-3-Clause |
| libaec / szip | 1.1.7 | BSD-2-Clause |
| FreeType | 2.14.3 | FTL **or** GPL-2.0-or-later — used under the **FTL** |
| HarfBuzz | 14.3.1 | MIT-Modern-Variant AND MIT |
| Fontconfig | 2.17.1 | MIT |
| Expat | 2.8.3 | MIT |
| libpng | 1.6.58 | libpng-2.0 |
| zlib | 1.3.2 | Zlib |
| Zstandard | 1.5.7 | BSD-3-Clause **or** GPL-2.0-only — used under **BSD-3-Clause** |
| Brotli | 1.2.0 | MIT |
| bzip2 | 1.0.8 | bzip2-1.0.6 |
| PCRE2 | 10.47 | BSD-3-Clause WITH PCRE2-exception |
| double-conversion | 3.4.0 | BSD-3-Clause (Copyright 2006-2011, the V8 project authors) |
| libb2 | 0.98.1 | CC0-1.0 |
| md4c | 0.5.3 | MIT |
| xcb-util-cursor | 0.1.5 | MIT (X11-style) |

That table is the Linux build. The Windows executable links a strict subset of
it: every library in it is in the Linux binary too, and four of them are not in
the Windows one.

| Absent from the Windows build | Why |
|---|---|
| Fontconfig | font discovery on X11; Windows asks DirectWrite instead |
| Expat | Fontconfig's XML parser, and nothing else here needs one |
| libb2 | qtbase takes BLAKE2 from the system on Unix and uses its own bundled copy on Windows, so the port is not installed there |
| xcb-util-cursor | there is no X11 to keep the binary off `libxcb-cursor.so.0` — see below for what it is doing in the table at all |

Nothing is in the Windows build that is not in the Linux one, which is what
lets one document be the inventory for both. Each release publishes a
`THIRD-PARTY-LICENSES-<platform>-x64.txt` generated from the ports actually on
that platform's link line, and each binary prints its own with `--notices`;
`cmake/ThirdPartyLicenses.cmake` is where the difference is written down.

xcb-util-cursor appears in that table rather than in *System libraries*
below, and the move is the whole point of it being there. Qt 6.5 and newer
link the xcb platform plugin against `libxcb-cursor` unconditionally, and
RHEL 8 — the oldest system this project supports — ships that library in
neither BaseOS nor AppStream, only in EPEL. A binary that asked the host for it
did not start on a stock RHEL 8 desktop: the loader refused it before `main`.
`ports/xcb-util-cursor` builds it statically, so it is now inside the
executable like everything else in the table, and the release asks nothing of a
third-party repository. See that port's `portfile.cmake`.

Its licence is MIT with the X11 no-advertising clause appended — beyond the
usual MIT terms it adds that "the names of the authors or their institutions
shall not be used in advertising or otherwise to promote the sale, use or other
dealings in this Software without prior written authorization from the
authors". H5Scope names them in no promotional material. The full text, which
the licence requires to accompany the binary, is reproduced in
THIRD-PARTY-LICENSES.txt like every other entry above.

FreeType is used under the FreeType Licence, which requires the following
acknowledgement:

> Portions of this software are copyright © The FreeType Project
> (<https://www.freetype.org>). All rights reserved.

The FTL is compatible with GPLv3 but not with GPLv2, which is one reason this
project is GPL-3.0-only rather than offering a GPLv2 option.

PCRE2's own licence carries an exemption — the binary-redistribution condition
does not travel down a chain of packages — which is why its identifier is
`BSD-3-Clause WITH PCRE2-exception` rather than plain BSD-3-Clause. Its text is
the one entry in THIRD-PARTY-LICENSES.txt not taken from vcpkg: the port
installs PCRE2's `COPYING`, which is four lines pointing at `LICENCE.md`, so
`LICENCE.md` itself is vendored in `licenses/`. The build fails if PCRE2's
version moves away from the one that text was taken from.

## IBM Plex

Copyright © 2017 IBM Corp. with Reserved Font Name "Plex".

IBM Plex Sans (Regular, SemiBold) and IBM Plex Mono (Regular, Medium) are
compiled into the executable as Qt resources. They are licensed under the SIL
Open Font License, Version 1.1. The full licence text ships beside the font
files in `src/gui/fonts/LICENSE.txt` and is compiled into the binary at
`:/fonts/LICENSE.txt`, so a binary-only download carries it too.

## System libraries

The following are dynamically linked and are the host operating system's, not
H5Scope's. GPLv3 section 1 excludes them from the Corresponding Source as
System Libraries. The list is what `ldd` reports for the release binary, not a
summary of it:

- **glibc** — `libc`, `libm`, and the dynamic loader.
- **The C++ and compiler runtime** — `libstdc++.so.6` and `libgcc_s.so.1`,
  which are GPL-3.0-with-GCC-exception. They are excluded as part of the
  compiler, which GPLv3 section 1 names as a Major Component in terms
  ("a compiler used to produce the work"), rather than by the same route as the
  rest of this list.
- **X11** — `libX11`, `libX11-xcb`, the libxcb family, the xcb-util family
  (`libxcb-icccm`, `libxcb-image`, `libxcb-keysyms`, `libxcb-render-util`,
  `libxcb-util`), `libSM`, `libICE`, `libXau`, `libXdmcp`. `libxcb-cursor` was
  in this list until it moved inside the binary; it is in the table above now.
- **Wayland** — `libwayland-client`, `libwayland-cursor`, `libwayland-egl`.
- **Keyboard handling** — `libxkbcommon`, `libxkbcommon-x11`.
- **Graphics** — `libGLX`, `libEGL`, `libOpenGL`, `libGLdispatch`, and the GPU
  drivers behind them.
- **Pulled in transitively by the above** — `libffi`, `libuuid`.

`librt` is *not* in this list, and was wrongly named here before. glibc's
`librt.a` is linked statically into the binary, not dynamically: on a modern
glibc it is a stub whose contents have moved into `libc`, so this changes
nothing in practice, but it is glibc code inside the executable rather than
beside it. glibc is LGPL-2.1-or-later, which the GPL is compatible with, and
the System Library exclusion covers it either way.

## Not shipped

These are used to build or test H5Scope and are not part of any distributed
binary:

| Tool | Licence | Used for |
|---|---|---|
| Catch2 3.15.3 | BSL-1.0 | the C++ test suites |
| NumPy | BSD-3-Clause | `tools/make-numpy-golden.py`, which writes expected values into a committed header so the tests need no interpreter |
| CMake, Ninja, vcpkg | — | general-purpose build tools, excluded from Corresponding Source by GPLv3 section 1 |

The vcpkg `egl-registry` and `opengl-registry` ports are also installed but are
not listed above and carry no entry in THIRD-PARTY-LICENSES.txt. They are the
Khronos registry headers — `khrplatform.h` and the GL headers — which supply
type declarations and preprocessor constants to the compiler and contribute no
code to the executable. Nothing from them is on the link line.

## Why GPLv3

Because that is the licence chosen for this program. Nothing in the binary
requires it.

That is a recent change and the old reasoning is worth keeping, because it says
what the constraint was. H5Scope used to link Qt Graphs to draw its plot, and
Qt Graphs is available only under `LicenseRef-Qt-Commercial OR GPL-3.0-only`;
Qt Quick 3D came with it, under the same terms, for a 3-D renderer nothing here
used. Statically linking either made the combined work GPLv3 whatever this
project's own source said, so a permissive licence on the source would have
promised something the releases could not deliver.

The plot is now drawn by the application itself — `src/gui/PlotItem.{hpp,cpp}`
and `src/gui/PlotProjection.{hpp,cpp}` put the geometry on the Qt Quick scene
graph, and `src/qml/PlotFrame.qml` draws the axes — and both modules have left
the manifest. Every remaining Qt module is available under LGPL-3.0, HDF5 is
BSD-3-Clause, and nothing else in the table above is copyleft at all. So the
licence is now a decision rather than an obligation, and it has not changed:
H5Scope is GPL-3.0-only.

## Corresponding Source

Every release attaches a source bundle containing the complete Corresponding
Source for the binary released with it: this repository at the released commit
— `ports/` included, which is where the overlay ports this project carries
itself live — the upstream source archive of every library above, and the vcpkg
`ports/` tree at the pinned baseline, which carries the patches vcpkg applies
(23 to `qtbase`, 5 to `hdf5`) and the scripts that apply
them.

See the *Building from the source bundle* section of the README.
