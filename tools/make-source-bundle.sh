#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-License-Identifier: GPL-3.0-only

# Assemble the Corresponding Source for the release binaries -- all of them,
# on both platforms, out of one plan.
#
# That is not a shortcut. A source archive is the same file whichever triplet
# builds it: qtbase's tarball is qtbase's tarball, and only which subset of
# them gets compiled differs between Linux and Windows. The Windows package set
# is a strict subset of the Linux one -- see the comment in vcpkg.json about
# why the qtbase feature lists are kept as close as they are -- so the archives
# fetched for x64-linux already contain every source the .exe was built from,
# and vcpkg-ports/ is the whole registry at the baseline regardless. The check
# below is what keeps that true rather than merely asserted: it resolves the
# Windows dependency graph and fails if anything in it is absent from the plan
# this bundle was filled from.
#
# Resolving it is all that can be done from here -- vcpkg refuses to compute a
# Windows *install* plan on a Linux host, wanting a Developer Prompt it cannot
# have -- but `depend-info` needs no compiler and answers the question that
# matters.
#
# H5Scope links Qt, HDF5 and about twenty further libraries statically, and
# is conveyed under the GPL because Qt Graphs is GPL-3.0-only. Section 6 of the
# GPL therefore requires the source of everything inside the executable to
# accompany it. Pointing at upstream would not do: vcpkg patches what it builds
# -- 23 patches to qtbase alone -- so the sources that correspond to this
# binary are the upstream archives *plus* the port files that modify them.
#
# What lands in the bundle:
#
#   .                     this repository at the released commit, `ports/`
#                         included -- the overlay ports this project carries
#                         itself are as much a part of what built the binary
#                         as vcpkg's own
#   cmake/BundleVersion.cmake   the version that commit builds as, since a
#                               bundle has no .git to count
#   vcpkg-downloads/      every upstream source archive vcpkg fetched
#   vcpkg-ports/          the vcpkg ports tree at the pinned baseline: the
#                         patches, and the scripts that apply them
#   build-from-bundle.sh  rebuilds the release from the three above
#   SHA256SUMS            what the archives were when they went in
#
# Deliberately absent: CMake, Ninja, meson, automake and gperf. GPL section 1
# excludes "general-purpose tools or generally available free programs which
# are used unmodified", which those are. Everything that ends up *in* the
# binary is here.
#
# Usage:  tools/make-source-bundle.sh <output-directory>
# Needs:  a full-history checkout, VCPKG_ROOT, and network access on first run.

set -euo pipefail

readonly repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly out_dir="${1:?usage: make-source-bundle.sh <output-directory>}"

if [[ -z "${VCPKG_ROOT:-}" || ! -d "$VCPKG_ROOT/ports" ]]; then
  echo "error: VCPKG_ROOT is not set or does not point at a vcpkg checkout" >&2
  exit 1
fi

cd "$repo_root"

# --- what is being bundled ---------------------------------------------------
# The same rule cmake/Version.cmake applies, against the same tags: the patch
# number is how many releases this commit descends from in this series. Major
# and minor are read out of that file rather than repeated here. The rule itself
# is stated twice -- once in CMake, once here -- because CMake cannot be asked
# for it before it has configured, and configuring needs Qt.
major="$(sed -n 's/^set(H5SCOPE_VERSION_MAJOR \([0-9]*\)).*/\1/p' cmake/Version.cmake)"
minor="$(sed -n 's/^set(H5SCOPE_VERSION_MINOR \([0-9]*\)).*/\1/p' cmake/Version.cmake)"
series="v${major}.${minor}.*"

if [[ "$(git rev-parse --is-shallow-repository)" == "true" ]]; then
  echo "error: shallow clone -- the reachable tags are not the ones there are," >&2
  echo "       so the version would be wrong. Fetch the full history." >&2
  exit 1
fi

reachable=$(git tag --list "$series" --merged HEAD | grep -c . || true)
at_head=$(git tag --list "$series" --points-at HEAD | grep -c . || true)
patch=$(( reachable - at_head ))
version="${major}.${minor}.${patch}"
commit="$(git rev-parse --short HEAD)"
baseline="$(sed -n 's/.*"builtin-baseline": "\([0-9a-f]*\)".*/\1/p' vcpkg.json)"

if [ "$at_head" -gt 0 ]; then
  released=TRUE
else
  released=FALSE
  # Not fatal -- cutting one by hand to look at it is reasonable -- but a
  # bundle is Corresponding Source *for a release*, and this commit is not one.
  echo "warning: HEAD carries no ${series} tag, so this bundle accompanies no" >&2
  echo "         release. It will describe itself as ${version}-dev." >&2
fi

readonly name="H5Scope-${version}-source"
readonly staging="${out_dir}/${name}"

echo "bundling H5Scope ${version} (${commit}), vcpkg baseline ${baseline}"
rm -rf "$staging"
mkdir -p "$staging"

# --- this repository, at this commit -----------------------------------------
# git archive rather than a copy: it takes exactly what is tracked, so a build
# tree, a stale artefact or the untracked instructions/ folder cannot ride
# along into something published.
git archive --format=tar HEAD | tar -x -C "$staging"

cat > "$staging/cmake/BundleVersion.cmake" <<EOF
# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-License-Identifier: GPL-3.0-only

# Written by tools/make-source-bundle.sh. A bundle carries no .git and so no
# tags, and cmake/Version.cmake counts release tags rather than trusting a
# typed number -- so the count is recorded here at the moment the bundle is
# cut. Only a bundle has this file; in a clone the tags are present and are
# counted instead.
set(H5SCOPE_BUNDLE_PATCH ${patch})
set(H5SCOPE_BUNDLE_COMMIT "${commit}")
set(H5SCOPE_BUNDLE_RELEASED ${released})
EOF

# --- the port files, which are what makes those sources correspond -----------
# First, because the download step below needs them: a Qt submodule records the
# URL and hash of its own tarball in its port.data.cmake, and that is the only
# place they exist.
echo "extracting vcpkg ports at ${baseline}..."
if ! git -C "$VCPKG_ROOT" cat-file -e "${baseline}^{commit}" 2>/dev/null; then
  echo "  baseline not present locally, fetching..."
  git -C "$VCPKG_ROOT" fetch --quiet origin "$baseline"
fi
mkdir -p "$staging/vcpkg-ports"
git -C "$VCPKG_ROOT" archive --format=tar "$baseline" ports \
  | tar -x -C "$staging/vcpkg-ports" --strip-components=1
echo "$baseline" > "$staging/vcpkg-ports/BASELINE"

# --- upstream sources --------------------------------------------------------
# --only-downloads fetches the archives without building any of them, which is
# minutes rather than the hours a real Qt build costs. The plan it prints on the
# way is kept: it is the list of what this binary is made of, and the next step
# checks the downloads against it.
#
# --overlay-ports because one of the manifest's dependencies is not in the
# registry: ports/xcb-util-cursor is this project's own, and without the flag
# vcpkg cannot resolve the manifest at all, let alone download its source.
# CMakePresets.json passes the same directory during an ordinary build.
echo "fetching upstream sources..."
"$VCPKG_ROOT/vcpkg" install \
  --x-manifest-root="$repo_root" \
  --overlay-ports="$repo_root/ports" \
  --triplet=x64-linux \
  --only-downloads \
  --downloads-root="$staging/vcpkg-downloads" \
  2>&1 | tee "$staging/vcpkg-plan.log"

# The packages the plan named, which is what the bundle has to account for.
mapfile -t planned < <(
  sed -n '/will be built and installed:/,/^$/p' "$staging/vcpkg-plan.log" \
    | sed -n 's/^ *\*\? *\([a-z0-9][a-z0-9.+-]*\)[][a-z0-9,.+-]*:[a-z0-9-]*@.*/\1/p' \
    | sort -u)
echo "plan: ${#planned[@]} packages"

# The completeness check below is only as good as this parse: an empty list
# would sail through it and produce exactly the silently-incomplete bundle this
# whole section exists to prevent. qtbase is in every plan this project can
# produce, so its absence means the format moved, not that the plan is short.
if [ "${#planned[@]}" -lt 10 ] || ! printf '%s\n' "${planned[@]}" | grep -qx qtbase; then
  echo "error: could not read vcpkg's install plan -- ${#planned[@]} packages" >&2
  echo "       parsed and no qtbase among them. The output format has moved;" >&2
  echo "       fix the parse rather than shipping an unchecked bundle." >&2
  exit 1
fi

# --- and the Windows binary is made of these same sources --------------------
# The bundle is filled from the x64-linux plan above and accompanies the .exe
# as well, on the strength of the Windows package set being a subset of it.
# This is where that is established rather than believed. A Windows-only
# dependency appearing one day -- a port whose Windows branch pulls something
# the Linux one does not -- would make this bundle incomplete Corresponding
# Source for a binary being published beside it, which is a licence failure
# and not a packaging one, so it fails the release.
# 2>&1 because depend-info prints its graph on *stderr*, which is surprising
# enough to be worth stating: redirecting it to /dev/null as noise leaves this
# parsing an empty stream, and an empty stream is what the check below exists
# to refuse.
echo "checking the Windows dependency graph is covered..."
mapfile -t windows_packages < <(
  "$VCPKG_ROOT/vcpkg" depend-info \
    --x-manifest-root="$repo_root" \
    --overlay-ports="$repo_root/ports" \
    --triplet=x64-windows-static \
    2>&1 \
  | sed -n 's/^\([a-z0-9][a-z0-9.+-]*\)\(\[[^]]*\]\)*:\( .*\)\?$/\1/p' \
  | sort -u)

# The same defence the plan parse above has, and for the same reason: an empty
# list satisfies every check below it without checking anything.
if [ "${#windows_packages[@]}" -lt 10 ] \
   || ! printf '%s\n' "${windows_packages[@]}" | grep -qx qtbase; then
  echo "error: could not resolve the Windows dependency graph --" >&2
  echo "       ${#windows_packages[@]} packages parsed and no qtbase among" >&2
  echo "       them. Fix the parse rather than shipping a bundle whose" >&2
  echo "       coverage of the .exe is unverified." >&2
  exit 1
fi

uncovered=0
for pkg in "${windows_packages[@]}"; do
  printf '%s\n' "${planned[@]}" | grep -qx "$pkg" && continue
  echo "error: $pkg is built into the Windows binary but is not in the" >&2
  echo "       x64-linux plan this bundle was filled from" >&2
  uncovered=$((uncovered + 1))
done
if [ "$uncovered" -ne 0 ]; then
  echo "       The Windows dependency set is no longer a subset of the Linux" >&2
  echo "       one, so one bundle no longer covers both. Fetch the Windows" >&2
  echo "       downloads too -- on a Windows host, or by teaching this script" >&2
  echo "       to run the plan there -- before publishing either binary." >&2
  exit 1
fi
echo "windows: ${#windows_packages[@]} packages, all covered"

# --- what --only-downloads could not fetch -----------------------------------
# Every Qt module except qtbase begins its portfile with
#
#     include("${CURRENT_INSTALLED_DIR}/share/qtbase/qt_install_submodule.cmake")
#
# and --only-downloads does not *install* qtbase, so that include fails and the
# port dies before it reaches its own download call. vcpkg calls the flag a
# "best-effort attempt" and exits 0 regardless, so the first bundle cut this way
# was missing qtdeclarative, qtgraphs, qtquick3d, qtsvg, qtshadertools,
# qtlanguageserver and qtquicktimeline -- most of Qt -- and said nothing.
#
# A bundle that is quietly missing what the binary was built from is worse than
# no bundle at all, so the archives are fetched here from the URL and verified
# against the SHA512 that the port itself records, and anything still missing at
# the end is a hard failure rather than a smaller tarball.
#
# Two places to look, because two trees of ports built this binary: vcpkg's, and
# this project's own overlay in `ports/`. ports/xcb-util-cursor writes its
# port.data.cmake in the same shape vcpkg's Qt ports use, precisely so that both
# the fetch below and the completeness check after it treat it like any other
# dependency rather than needing a special case.
port_data() {
  local pkg="$1" candidate
  for candidate in "$staging/vcpkg-ports/$pkg" "$staging/ports/$pkg"; do
    if [ -f "$candidate/port.data.cmake" ]; then
      printf '%s\n' "$candidate/port.data.cmake"
      return 0
    fi
  done
  return 1
}

echo "checking the plan against the downloads..."
fetched=0
for pkg in "${planned[@]}"; do
  data="$(port_data "$pkg")" || continue   # not a port that records its source
  fname=$(sed -n "s/^set(${pkg}_FILENAME \"\(.*\)\")\$/\1/p" "$data")
  hash=$(sed -n "s/^set(${pkg}_HASH \"\(.*\)\")\$/\1/p" "$data")
  urls=$(sed -n "s/^set(${pkg}_URL \"\(.*\)\")\$/\1/p" "$data")
  [ -n "$fname" ] && [ -n "$hash" ] || continue
  dest="$staging/vcpkg-downloads/$fname"
  [ -f "$dest" ] && continue

  echo "  $pkg: $fname"
  ok=0
  # The port lists mirrors; take the first that answers with the right bytes.
  for url in ${urls//;/ }; do
    curl -fsSL --retry 3 -o "$dest.part" "$url" || continue
    if [ "$(sha512sum "$dest.part" | cut -d" " -f1)" = "$hash" ]; then
      mv "$dest.part" "$dest"; ok=1; fetched=$((fetched + 1)); break
    fi
    echo "    hash mismatch from $url" >&2
    rm -f "$dest.part"
  done
  if [ "$ok" -ne 1 ]; then
    echo "error: could not fetch $fname for $pkg from any of its mirrors" >&2
    exit 1
  fi
done
echo "fetched $fetched archive(s) the download step could not"

# Nothing in the plan that records a source file may be absent from the bundle.
# This is the check the first attempt did not have.
absent=0
for pkg in "${planned[@]}"; do
  data="$(port_data "$pkg")" || continue
  fname=$(sed -n "s/^set(${pkg}_FILENAME \"\(.*\)\")\$/\1/p" "$data")
  [ -n "$fname" ] || continue
  if [ ! -f "$staging/vcpkg-downloads/$fname" ]; then
    echo "error: $pkg is in the plan but $fname is not in the bundle" >&2
    absent=$((absent + 1))
  fi
done
[ "$absent" -eq 0 ] || exit 1

# The general-purpose tools GPL section 1 excludes. Removed after the fetch
# rather than avoided during it: vcpkg decides what it needs, and second-
# guessing that by name up front is how a source archive goes missing.
rm -rf "$staging/vcpkg-downloads/tools"
find "$staging/vcpkg-downloads" -maxdepth 1 -type f \
  \( -name 'cmake-*' -o -name 'ninja-*' -o -name 'meson-*' \
     -o -name 'automake-*' -o -name 'autoconf-*' -o -name 'gperf-*' \
     -o -name 'pkgconf-*' \) -delete
# vcpkg's own bookkeeping, not source.
find "$staging/vcpkg-downloads" -maxdepth 1 -name '*.log' -delete
rm -f "$staging/vcpkg-plan.log"

# --- the rebuild script ------------------------------------------------------
cat > "$staging/build-from-bundle.sh" <<'BUILD'
#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-License-Identifier: GPL-3.0-only

# Rebuild the release this bundle accompanies, from the sources in this bundle.
#
# vcpkg is pointed at vcpkg-downloads/ so it takes the archives bundled here
# instead of fetching them, and at vcpkg-ports/ as overlay ports so it applies
# the same patches with the same port files the release was built with. What
# you still need from your own machine is a C++20 compiler, CMake 3.26+, Ninja,
# a vcpkg checkout (for the tool itself, not its ports), and Qt's X11/OpenGL
# development headers -- see the README's Building section.
#
# This rebuilds the Linux binaries. The same bundle is the source for the
# Windows executable published beside them; on Windows the equivalent is
# `cmake --preset windows-release` from this directory, with the same two
# overlay-ports directories passed the same way.

set -euo pipefail
readonly here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [[ -z "${VCPKG_ROOT:-}" || ! -x "$VCPKG_ROOT/vcpkg" ]]; then
  echo "error: set VCPKG_ROOT to a vcpkg checkout (the tool; its ports are here)" >&2
  exit 1
fi

export VCPKG_DOWNLOADS="$here/vcpkg-downloads"

# The presets resolve their binary directory relative to the source tree, so
# this runs where CMakePresets.json is rather than wherever it was invoked.
cd "$here"

# Two overlay directories, passed as one CMake list rather than through the
# environment. `ports/` is this project's own -- CMakePresets.json already
# points at it, and naming it again here keeps that true whatever the preset
# says later. `vcpkg-ports/` is the whole registry at the pinned baseline,
# overlaid so the build uses the patches this release was made with instead of
# whatever the checkout in VCPKG_ROOT happens to be at. `ports/` comes first so
# that it still wins if the registry ever grows a port of the same name.
cmake --preset release -DVCPKG_OVERLAY_PORTS="$here/ports;$here/vcpkg-ports"
cmake --build --preset release
ctest --preset release

echo
echo "built: $here/build/release/bin/"
BUILD
chmod +x "$staging/build-from-bundle.sh"

# --- what went in ------------------------------------------------------------
( cd "$staging/vcpkg-downloads" && find . -maxdepth 1 -type f -printf '%P\n' \
    | sort | xargs -r sha256sum ) > "$staging/SHA256SUMS"

cat > "$staging/README.bundle.md" <<EOF
# H5Scope ${version} — Corresponding Source

This is the complete source of every H5Scope ${version} binary released
alongside it -- the Linux executable, the AppImage and the Windows
executable -- as GPL-3.0-only section 6 requires. It was cut from commit
\`${commit}\` by \`tools/make-source-bundle.sh\`.

One bundle covers both platforms because a source archive is the same file
whichever compiler builds it, and the set of libraries linked into the
Windows executable is a strict subset of the set linked into the Linux one.
The script verifies that before writing this file.

| Path | What it is |
|---|---|
| \`.\` | the H5Scope repository at \`${commit}\` |
| \`ports/\` | the overlay ports H5Scope carries itself, inside the repository above |
| \`vcpkg-downloads/\` | the upstream source archive of every library linked into the binary |
| \`vcpkg-ports/\` | the vcpkg ports tree at baseline \`${baseline}\` — the patches applied to those archives, and the scripts that apply them |
| \`build-from-bundle.sh\` | rebuilds the release from the two above |
| \`SHA256SUMS\` | the archives as they went in |

Run \`./build-from-bundle.sh\` with \`VCPKG_ROOT\` pointing at a vcpkg checkout.
Nothing here is fetched from the network. On Windows the equivalent is
\`cmake --preset windows-release -DVCPKG_OVERLAY_PORTS="<here>/ports;<here>/vcpkg-ports"\`
with \`VCPKG_DOWNLOADS\` set to \`vcpkg-downloads/\`.

CMake, Ninja, meson, automake and gperf are not included. GPL-3.0-only
section 1 excludes "general-purpose tools or generally available free programs
which are used unmodified", which those are; nothing they contain ends up in
the binary.

Licences: H5Scope is GPL-3.0-only (\`LICENSE\`). Every dependency and the
attribution its licence requires is listed in \`THIRD-PARTY-NOTICES.md\`.
EOF

# --- archive -----------------------------------------------------------------
echo "compressing..."
tar --zstd -cf "${out_dir}/${name}.tar.zst" -C "$out_dir" "$name"
rm -rf "$staging"

echo
echo "wrote ${out_dir}/${name}.tar.zst ($(du -h "${out_dir}/${name}.tar.zst" | cut -f1))"
