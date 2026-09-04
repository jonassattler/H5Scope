#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-License-Identifier: GPL-3.0-only
#
# Everything that has to be true of the AppImage. An AppImage that was produced
# is not an AppImage that runs, and the ways it can be built wrong are all
# quiet ones: a payload compiled against the wrong glibc, a library that was
# supposed to be bundled and was not, a launcher that points at a name the
# AppDir does not contain.
#
# Usage: tools/ci/verify-appimage.sh <appimage>

set -euo pipefail

if [ "$#" -ne 1 ]; then
    echo "usage: $0 <appimage>" >&2
    exit 2
fi

appimage="$(readlink -f "$1")"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

[ -f "$appimage" ] || { echo "error: no such file: $appimage" >&2; exit 1; }
chmod +x "$appimage"

echo "appimage: $appimage ($(du -h "$appimage" | cut -f1))"
echo

# There is no FUSE inside a build container, so the AppImage is exercised
# through its own extract-and-run path. That is not a workaround for the test:
# it is the same fallback a user without FUSE has, so exercising it is worth
# doing on every commit regardless.
export APPIMAGE_EXTRACT_AND_RUN=1

echo "== it runs"
QT_QPA_PLATFORM=offscreen "$appimage" --version
echo

echo "== it carries its own licences"

# Captured rather than piped into grep -q: under `set -o pipefail` an early
# -q exit kills the writer with SIGPIPE and the pipeline reports 141, so the
# check would fail exactly when it passed. See verify-binary.sh.
license="$("$appimage" --license)"
notices="$("$appimage" --notices)"

grep -q 'GNU GENERAL PUBLIC LICENSE' <<<"$license"
grep -q 'Copyright 2006 by The HDF Group' <<<"$notices"
grep -q 'SIL OPEN FONT LICENSE' <<<"$notices"
echo "OK: --license and --notices answer from inside the AppImage too"
echo

# --- the payload ------------------------------------------------------------
workdir="$(mktemp -d)"
trap 'rm -rf "$workdir"' EXIT
cd "$workdir"
"$appimage" --appimage-extract >/dev/null

echo "== what it bundles"
ls squashfs-root/usr/lib
echo

# libxcb-cursor used to be bundled here, and used to be most of the reason this
# AppImage existed: RHEL 8 ships it in neither BaseOS nor AppStream, only EPEL.
# It is now linked into the executable instead (ports/xcb-util-cursor), so the
# bare binary needs it from nowhere and the AppImage has nothing to carry. The
# check is therefore inverted rather than deleted -- if the dependency ever
# comes back, it comes back in both artefacts at once, and the payload is where
# it can be seen.
echo "== the library RHEL 8 does not have is not asked for"
if objdump -p squashfs-root/usr/bin/H5Scope-* | grep -q 'NEEDED.*libxcb-cursor'; then
    echo "::error::the payload needs libxcb-cursor.so.0; it must be linked"\
         "statically from ports/xcb-util-cursor. See verify-binary.sh."
    exit 1
fi
if ls squashfs-root/usr/lib/libxcb-cursor.so* >/dev/null 2>&1; then
    echo "::error::libxcb-cursor is bundled but nothing needs it; the payload"\
         "should be linking it statically."
    exit 1
fi
echo "OK: libxcb-cursor is inside the payload, neither bundled nor asked for"
echo

# The mirror image of the check above: bundling any of these would override the
# host's GPU driver stack or its X server connection with a copy from the build
# machine, which is the classic way to make an AppImage that works everywhere
# except on the machines people actually have.
echo "== what it must not bundle"
for forbidden in libGL.so libGLX.so libEGL.so libGLdispatch.so libOpenGL.so \
                 libX11.so libxcb.so libwayland-client.so libc.so libstdc++.so; do
    if ls "squashfs-root/usr/lib/$forbidden"* >/dev/null 2>&1; then
        echo "::error::$forbidden is bundled; it must come from the host"
        exit 1
    fi
done
echo "OK: the driver, X11 and glibc stacks are left to the host"
echo

# The payload is the same executable the bare release publishes, so it faces
# the same floor. The bundled libraries face it too: they were copied off the
# build machine, and a bundle built anywhere newer than RHEL 8 would carry the
# glibc problem straight back in through the side door.
echo "== the RHEL 8 glibc floor, inside the wrapper"
bash "$repo_root/tools/check-glibc-floor.sh" 2.28 \
    squashfs-root/usr/bin/H5Scope-* \
    squashfs-root/usr/lib/*.so*
echo

# The desktop entry is what gives the thing a name, an icon and its association
# with .h5 files once a user integrates it. appimagetool validates the file
# itself; what it cannot check is that the icon it names is actually there.
echo "== the desktop entry and its icon"
test -f squashfs-root/h5scope.desktop
test -f squashfs-root/h5scope.png
test -f squashfs-root/.DirIcon
grep -q '^Icon=h5scope$' squashfs-root/h5scope.desktop
grep -q '^MimeType=.*application/x-hdf5' squashfs-root/h5scope.desktop
grep -q '^X-AppImage-Version=' squashfs-root/h5scope.desktop
echo "OK: desktop entry, icon and .h5 association are present"

echo
echo "OK: $(basename "$appimage") is fit to publish"
