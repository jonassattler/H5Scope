#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-License-Identifier: GPL-3.0-only
#
# Everything that has to be true of the release executable before it is
# published, checked against the executable itself rather than against the
# build that produced it.
#
# A script rather than shell inside the workflow, because it runs inside the
# RHEL 8 container and would otherwise be a heredoc inside a `docker run`
# inside a YAML block scalar -- three sets of quoting rules over the same
# apostrophes, which is a way to lose a check silently rather than loudly.
#
# It is also runnable by hand, which is the point:
#   tools/ci/el8.sh bash tools/ci/verify-binary.sh build/release/bin/H5Scope
#
# Usage: tools/ci/verify-binary.sh <executable>

set -euo pipefail

if [ "$#" -ne 1 ]; then
    echo "usage: $0 <executable>" >&2
    exit 2
fi

binary="$1"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
bindir="$(cd "$(dirname "$binary")" && pwd)"

[ -x "$binary" ] || { echo "error: not an executable: $binary" >&2; exit 1; }

echo "binary: $(readlink -f "$binary")"
echo

# --- it starts, and says what it is ----------------------------------------
# Offscreen, like every other invocation here. --version is parsed after
# QGuiApplication is constructed, and constructing one on a machine with no
# display aborts in the xcb plugin before the version is ever printed -- which
# would fail this script for a reason that has nothing to do with what it
# checks.
echo "== it runs"
QT_QPA_PLATFORM=offscreen "$binary" --version
echo

# --- nothing of Qt or HDF5 is asked of the host ----------------------------
# The load-bearing check for the "no system libraries" requirement. If Qt or
# HDF5 ever leak in as shared objects, this fails the build.
echo "== no Qt or HDF5 runtime dependency"
ldd "$binary" || true
if ldd "$binary" | grep -E 'libQt6|libhdf5'; then
    echo "::error::binary links Qt or HDF5 dynamically; it must be static"
    exit 1
fi
echo "OK: no Qt or HDF5 runtime dependency"
echo

# --- nor libxcb-cursor, which is the one X library RHEL 8 lacks ------------
# Qt 6.5 and newer link the xcb platform plugin against libxcb-cursor
# unconditionally, and RHEL 8 ships it in neither BaseOS nor AppStream -- EPEL
# only. A binary that needs it does not start on a stock RHEL 8 desktop at all:
# the loader refuses it before main. ports/xcb-util-cursor builds the library
# static so it travels inside the executable instead, and this is what proves
# it stayed that way.
#
# objdump rather than ldd, deliberately: ldd on a machine that does have the
# library reports it resolved and says nothing about whether it was needed.
# The NEEDED entry is the fact being checked.
echo "== no libxcb-cursor runtime dependency"
if objdump -p "$binary" | grep -q 'NEEDED.*libxcb-cursor'; then
    echo "::error::binary needs libxcb-cursor.so.0, which RHEL 8 has only"\
         "through EPEL. XCB::CURSOR must resolve to the static archive from"\
         "ports/xcb-util-cursor; the root CMakeLists asserts that at configure"\
         "time, so reaching here means something bypassed it."
    exit 1
fi
echo "OK: libxcb-cursor is inside the binary, not asked of the host"
echo

# --- it will start on RHEL 8 -----------------------------------------------
echo "== the RHEL 8 glibc floor"
bash "$repo_root/tools/check-glibc-floor.sh" 2.28 "$binary"
echo

# --- it carries its own licences -------------------------------------------
# GPL-3.0-only section 6 conveys object code under sections 4 and 5, and
# section 4 wants a copy of the License given to every recipient along with the
# Program -- and the BSD, MIT, Zlib and libpng dependencies each want their
# notice reproduced in the materials accompanying a binary. All of that is
# compiled in; this proves it comes out again.
#
# No QT_QPA_PLATFORM here, and that is the point: these two options are
# answered before a QGuiApplication is constructed, so they work on a machine
# with no display. If that ever regresses, this is where it shows up.
echo "== it carries its own licences"

# Captured once and matched as strings afterwards, rather than piped into grep
# a phrase at a time. Two reasons, and the second one bites.
#
# The licence texts run to about a megabyte together, so this is six fewer
# process launches. More to the point, `"$binary" --notices | grep -q ...`
# under `set -o pipefail` is a trap: grep -q exits the moment it matches, the
# binary upstream is killed by SIGPIPE, and the pipeline reports 141. The check
# then fails precisely because it succeeded, which is a very confusing way to
# lose an hour.
license="$("$binary" --license)"
notices="$("$binary" --notices)"

sed -n '1,3p' <<<"$license"
test "$(wc -l <<<"$license")" -gt 600
test "$(grep -c 'END OF TERMS\|LICENCE\|LICENSE\|Copyright' <<<"$notices")" -gt 20

# The four the notices would be worthless without: this project's own licence,
# the inventory, HDF5's required copyright notice, and the font licence.
grep -q 'GNU GENERAL PUBLIC LICENSE' <<<"$license"
grep -q 'Third-party notices' <<<"$notices"
grep -q 'Copyright 2006 by The HDF Group' <<<"$notices"
grep -q 'SIL OPEN FONT LICENSE' <<<"$notices"
echo "OK: --license and --notices both answer without a display"

# And beside the binary, laid down by the build itself (see
# src/CMakeLists.txt), which is what the release publishes.
ls -la "$bindir/LICENSE" \
       "$bindir/THIRD-PARTY-NOTICES.md" \
       "$bindir/THIRD-PARTY-LICENSES.txt"

echo
echo "OK: $binary is fit to publish"
