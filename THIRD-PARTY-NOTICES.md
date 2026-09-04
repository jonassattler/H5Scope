# Third-party notices

H5Scope links every dependency statically, so a single executable contains
the code of everything listed here. This file is the *inventory* of what is in
that executable and under which licence. It is compiled from the link line of
the release binary, not from the dependency manifest: `vcpkg.json` names five
packages, and those five pull in everything below.

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

The Qt modules linked into H5Scope fall into two groups.

**Qt Graphs and Qt Quick 3D** are available under `LicenseRef-Qt-Commercial OR
GPL-3.0-only`. There is no LGPL option for either module — Qt's own licensing
page lists both under "Modules available under GNU General Public License v3".
`libQt6Graphs.a`, `libgraphsplugin.a`, `libQt6Quick3D.a`,
`libQt6Quick3DRuntimeRender.a`, `libQt6Quick3DUtils.a` and `libqquick3dplugin.a`
are all linked into the binary, and that is what makes the combined work GPLv3.

**Every other Qt module** is available under `LicenseRef-Qt-Commercial OR
LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only`. They are conveyed here under
GPL-3.0-only, which the LGPL permits: LGPLv3 is GPLv3 plus additional
permissions, and GPLv3 section 7 allows those additional permissions to be
removed from a covered work.

Linked Qt modules: Core, Gui, Qml, QmlMeta, QmlModels, QmlWorkerScript, Quick,
QuickControls2 (with the Basic, Fusion, Imagine, Material, Universal and
FluentWinUI3 styles and their implementation modules), QuickTemplates2,
QuickLayouts, QuickShapes, QuickEffects, QuickDialogs2, QuickDialogs2QuickImpl,
QuickDialogs2Utils, **Graphs**, **Quick3D**, **Quick3DRuntimeRender**,
**Quick3DUtils**, Svg, Network, Concurrent, OpenGL, ShaderTools, PacketProtocol,
LabsFolderListModel, XcbQpa, WaylandClient, WlShellIntegration,
DeviceDiscoverySupport, EglFSDeviceIntegration, FbSupport, InputSupport, and
the platform, image-format, QML debug and Wayland decoration plugins that
accompany them.

Bold above marks the GPL-only modules. Qt Quick 3D is linked because the
`qtgraphs` package requires it; H5Scope uses only the 2D half of Qt Graphs
and renders nothing in 3D. It arrives with Graphs and would leave with it, so
it adds no obligation that Graphs did not already impose — but it is a second
GPL-only module, not an LGPL one, and belongs in this group rather than the
one below.

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
| meshoptimizer | 1.2 | MIT |

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
- **X11** — `libX11`, `libX11-xcb`, the libxcb family, `libSM`, `libICE`,
  `libXau`, `libXdmcp`.
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

H5Scope's own source could be released under a permissive licence — it is
original work with no vendored third-party code. It is not, because Qt Graphs
is GPL-3.0-only and is statically linked into the binary. A permissive licence
on the source would promise something the releases cannot deliver.

Qt Quick 3D is GPL-3.0-only as well, so strictly there are two GPL-only
dependencies rather than one. It is not a second reason, though: it is here
only because the `qtgraphs` port depends on it, so it arrives with Graphs and
leaves with Graphs.

The plot view is the only part that depends on Qt Graphs
(`src/gui/DatasetPlot.{hpp,cpp}` and `src/qml/PlotSurface.qml`). Replacing it
with Qt Quick Shapes, which is LGPL, would remove both GPL-only dependencies at
once — Graphs directly, and the `qtquick3d` build along with it.

## Corresponding Source

Every release attaches a source bundle containing the complete Corresponding
Source for the binary released with it: this repository at the released commit,
the upstream source archive of every library above, and the vcpkg `ports/` tree
at the pinned baseline, which carries the patches vcpkg applies (23 to
`qtbase`, 2 to `qtquick3d`, 5 to `hdf5`) and the scripts that apply them.

See the *Building from the source bundle* section of the README.
