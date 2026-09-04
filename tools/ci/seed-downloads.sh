#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-License-Identifier: GPL-3.0-only
#
# Put a source archive into vcpkg's downloads directory before vcpkg goes
# looking for it, for the cases where the port's own URLs will not serve it.
#
# Why this exists. vcpkg checks $VCPKG_ROOT/downloads for the expected file
# name and verifies its SHA512 before touching the network, so a file placed
# here is used and never fetched. That is the documented mechanism, not a
# trick: it is how vcpkg supports building without internet access.
#
# What made it necessary. gperf is fetched from ftpmirror.gnu.org, which is a
# redirector that hands out a different GNU mirror each time, and some of those
# mirrors are broken. Two consecutive CI runs died the same way:
#
#   error: curl operation failed with response code 502.
#   error: Reached maximum number of attempts, won't retry download from
#          https://ftpmirror.gnu.org/gnu/gperf/gperf-3.3.tar.gz
#   error: building gperf:x64-linux-release failed with: BUILD_FAILED
#
# The file itself is fine -- it fetches from ftp.gnu.org and from
# mirrors.kernel.org, byte for byte the archive the portfile's SHA512 names.
# The runner simply kept being sent to a mirror that was not serving it.
#
# Why so little of this is needed. Each source is downloaded once ever: after a
# port builds, its compiled package goes into the binary cache and the archive
# is never wanted again. So this only has to get a package over the line the
# first time, and the table below should stay short. If it grows, that is a
# sign the binary cache is not accumulating and the problem is there instead.
#
# Adding an entry: one line of "filename|sha512|url url url", with the file
# name and hash copied from the port's own vcpkg_download_distfile call, and
# mirrors that are not the one that failed. The hash is the port's, so a mirror
# serving something else is caught here rather than trusted.

set -euo pipefail

if [ -z "${VCPKG_ROOT:-}" ]; then
    echo "error: VCPKG_ROOT is not set" >&2
    exit 2
fi

# filename | sha512 | space-separated mirrors, tried in order
#
# gperf 3.3: ports/gperf/portfile.cmake at the manifest's builtin-baseline.
readonly SEEDS=(
"gperf-3.3.tar.gz|246b75b8ce7d77d6a8725cd15f1cf2e68da404812573af1d5bf32dbe6ad4228f48757baefc77bcb1f5597c2397043c04d31d8a04ab507bfa7a80f85e1ab6045f|https://mirrors.kernel.org/gnu/gperf/gperf-3.3.tar.gz https://ftp.gnu.org/gnu/gperf/gperf-3.3.tar.gz https://ftpmirror.gnu.org/gnu/gperf/gperf-3.3.tar.gz"
)

downloads="$VCPKG_ROOT/downloads"
mkdir -p "$downloads"

status=0

for seed in "${SEEDS[@]}"; do
    filename="${seed%%|*}"
    rest="${seed#*|}"
    sha512="${rest%%|*}"
    urls="${rest#*|}"

    target="$downloads/$filename"

    if [ -f "$target" ] && echo "$sha512  $target" | sha512sum --check --status; then
        echo "already seeded: $filename"
        continue
    fi

    seeded=false
    for url in $urls; do
        echo "seeding $filename from ${url#https://}"
        if ! curl -sSL --fail --max-time 300 --retry 2 -o "$target.tmp" "$url"; then
            echo "  unavailable"
            rm -f "$target.tmp"
            continue
        fi
        if ! echo "$sha512  $target.tmp" | sha512sum --check --status; then
            # A mirror serving a different file is worse than one serving
            # nothing, so say which one it was rather than moving on quietly.
            echo "  ::warning::$url served the wrong bytes for $filename"
            rm -f "$target.tmp"
            continue
        fi
        mv "$target.tmp" "$target"
        echo "  ok"
        seeded=true
        break
    done

    if [ "$seeded" = false ]; then
        # Not fatal. vcpkg still has the port's own URLs to try, and they may
        # well work -- this script exists to improve the odds, not to be the
        # only way the source can arrive. Failing the build here would turn a
        # mirror wobble into a red run for no reason.
        echo "::warning::could not seed $filename; leaving it to vcpkg"
        status=0
    fi
done

exit "$status"
