#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-License-Identifier: GPL-3.0-only

# Assemble the Corresponding Source for a release binary.
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
#   .                     this repository at the released commit
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
echo "fetching upstream sources..."
"$VCPKG_ROOT/vcpkg" install \
  --x-manifest-root="$repo_root" \
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
echo "checking the plan against the downloads..."
fetched=0
for pkg in "${planned[@]}"; do
  data="$staging/vcpkg-ports/$pkg/port.data.cmake"
  [ -f "$data" ] || continue          # not a port that records its source here
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
  data="$staging/vcpkg-ports/$pkg/port.data.cmake"
  [ -f "$data" ] || continue
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

set -euo pipefail
readonly here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [[ -z "${VCPKG_ROOT:-}" || ! -x "$VCPKG_ROOT/vcpkg" ]]; then
  echo "error: set VCPKG_ROOT to a vcpkg checkout (the tool; its ports are here)" >&2
  exit 1
fi

export VCPKG_DOWNLOADS="$here/vcpkg-downloads"
export VCPKG_OVERLAY_PORTS="$here/vcpkg-ports"

# The presets resolve their binary directory relative to the source tree, so
# this runs where CMakePresets.json is rather than wherever it was invoked.
cd "$here"
cmake --preset release
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

This is the complete source of the H5Scope ${version} binary released
alongside it, as GPL-3.0-only section 6 requires. It was cut from commit
\`${commit}\` by \`tools/make-source-bundle.sh\`.

| Path | What it is |
|---|---|
| \`.\` | the H5Scope repository at \`${commit}\` |
| \`vcpkg-downloads/\` | the upstream source archive of every library linked into the binary |
| \`vcpkg-ports/\` | the vcpkg ports tree at baseline \`${baseline}\` — the patches applied to those archives, and the scripts that apply them |
| \`build-from-bundle.sh\` | rebuilds the release from the two above |
| \`SHA256SUMS\` | the archives as they went in |

Run \`./build-from-bundle.sh\` with \`VCPKG_ROOT\` pointing at a vcpkg checkout.
Nothing here is fetched from the network.

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
