#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-License-Identifier: GPL-3.0-only
#
# Prove that a binary will start on the oldest distribution this project
# supports, without having that distribution to hand.
#
# The claim "runs on RHEL 8" is not something a successful build establishes.
# A build on Ubuntu 24.04 produces an executable that references glibc symbols
# at versions RHEL 8's glibc 2.28 never defined -- the loader then refuses it
# with "version `GLIBC_2.34' not found", at startup, on the user's machine,
# with nothing in CI having gone red. The only way to catch that at build time
# is to read the versioned symbol references back out of the ELF and compare
# them against the floor.
#
# Three things are checked, and each of them has failed a real project:
#
#   GLIBC_*   The symbol versions glibc must define. 2.34 is the usual
#             offender -- pthread and dl were folded into libc there, so a
#             build on any modern distribution picks it up without asking.
#             2.29 (the maths functions) and 2.32/2.33 are the others.
#
#   GLIBCXX_/CXXABI_
#             The same question for libstdc++. gcc-toolset-13 compiles C++20
#             on RHEL 8 but its libstdc++ is far newer than the 8.5 that RHEL
#             8 ships, so a binary that links it dynamically needs a
#             libstdc++.so.6 the target does not have. The root CMakeLists
#             links it statically to avoid that; if that ever stops working,
#             these symbols reappear and this is where it shows.
#
#   NEEDED    What must exist on the target at all. Listed rather than
#             asserted -- which libraries are acceptable is a judgement about
#             the distribution, not something a script should guess -- but
#             printed on every run so a new entry is visible in the log.
#
# Usage: tools/check-glibc-floor.sh <max-glibc-version> <file>...
#    eg: tools/check-glibc-floor.sh 2.28 build/release/bin/H5Scope

set -euo pipefail

if [ "$#" -lt 2 ]; then
    echo "usage: $0 <max-glibc-version> <file>..." >&2
    exit 2
fi

floor="$1"
shift

if ! command -v objdump >/dev/null 2>&1; then
    echo "error: objdump is required (binutils)" >&2
    exit 2
fi

# sort -V orders 2.9 before 2.10, which a lexical sort gets backwards and
# which is exactly the range these versions live in.
version_gt() {
    [ "$1" != "$2" ] && [ "$(printf '%s\n%s\n' "$1" "$2" | sort -V | tail -1)" = "$1" ]
}

status=0

for file in "$@"; do
    if [ ! -f "$file" ]; then
        echo "error: no such file: $file" >&2
        status=1
        continue
    fi

    echo "== $file"

    # Static executables have no dynamic symbol table at all, and objdump says
    # so on stderr rather than failing. That is a pass, not an error: nothing
    # is being asked of the target's glibc.
    symbols="$(objdump -T "$file" 2>/dev/null || true)"

    glibc_versions="$(printf '%s\n' "$symbols" \
        | grep -oE 'GLIBC_[0-9]+(\.[0-9]+)+' | sed 's/^GLIBC_//' | sort -uV || true)"

    if [ -z "$glibc_versions" ]; then
        echo "   glibc:     no versioned references (static, or not an ELF)"
    else
        highest="$(printf '%s\n' "$glibc_versions" | tail -1)"
        echo "   glibc:     needs up to $highest (floor is $floor)"
        echo "              all: $(printf '%s ' $glibc_versions)"
        if version_gt "$highest" "$floor"; then
            echo "::error::$file requires GLIBC_$highest but the floor is $floor"
            echo "   ^ built against too new a glibc; it will not start on the target"
            status=1
        fi
    fi

    cxx="$(printf '%s\n' "$symbols" \
        | grep -oE '(GLIBCXX|CXXABI)_[0-9]+(\.[0-9]+)+' | sort -uV || true)"

    if [ -n "$cxx" ]; then
        echo "::error::$file links libstdc++ dynamically: $(printf '%s ' $cxx)"
        echo "   ^ expected -static-libstdc++; RHEL 8's libstdc++ is far older"
        status=1
    else
        echo "   libstdc++: not referenced (linked statically, or a C library)"
    fi

    needed="$(objdump -p "$file" 2>/dev/null | awk '/NEEDED/ {print $2}' | sort || true)"
    if [ -n "$needed" ]; then
        echo "   needs:     $(printf '%s ' $needed)"
    fi
done

if [ "$status" -eq 0 ]; then
    echo
    echo "OK: everything checked starts on glibc $floor"
fi

exit "$status"
