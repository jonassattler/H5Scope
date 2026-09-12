#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-License-Identifier: GPL-3.0-only

#
# tools/release-notes.sh, against the four ways a release page goes wrong.
#
# The script is a gate: it runs once per release, on a runner, and the only time
# anybody finds out it is broken is the release it fails to stop. There is no
# arrangement of the repository that exercises it -- the working tree has one
# changelog and one version, and what has to be tested is the other five -- so
# the changelogs are written here.
#
# It cannot be a ctest test of the *repository's* notes, which is the obvious
# thing and the wrong one: the version a working tree calls itself is the next
# release, and the next release's notes are written when it is cut. A suite that
# demanded them on every build would fail on every machine every day until the
# afternoon somebody tagged.

set -uo pipefail

script="$(dirname "$0")/release-notes.sh"
home=$(mktemp -d)
trap 'rm -rf "$home"' EXIT

failures=0

# Run the script over `notes` and say whether it did what was wanted.
#
#   expect  ok | refuse
expect() {
    local what="$1" version="$2" expect="$3" notes="$4"
    printf '%s' "$notes" > "$home/CHANGELOG.md"

    local output status
    output=$(CHANGELOG="$home/CHANGELOG.md" bash "$script" "$version" --check 2>&1)
    status=$?

    if [ "$expect" = ok ] && [ "$status" -ne 0 ]; then
        echo "FAIL  $what: refused what it should have taken"
        printf '      %s\n' "$output"
        failures=$((failures + 1))
        return
    fi
    if [ "$expect" = refuse ] && [ "$status" -eq 0 ]; then
        echo "FAIL  $what: took what it should have refused"
        failures=$((failures + 1))
        return
    fi
    echo "ok    $what"
}

readonly REAL='Custom plot tabs: a fifth kind of tab, made with the plus at the
end of the strip, drawing slices from anywhere in the file together.'

readonly OLDER='Fixed the column widths, which were measured against a model
with nothing in it rather than against the dataset that is open.'

expect "a section that says something is published" 0.4.0 ok \
"# Changelog

## 0.4.0

$REAL

## 0.3.3

$OLDER
"

expect "no section for this version is refused" 0.4.1 refuse \
"# Changelog

## 0.4.0

$REAL
"

expect "two sections for one version is refused" 0.4.0 refuse \
"# Changelog

## 0.4.0

$REAL

## 0.4.0

$OLDER
"

expect "an empty section is refused" 0.4.0 refuse \
"# Changelog

## 0.4.0

## 0.3.3

$OLDER
"

expect "a placeholder is refused" 0.4.0 refuse \
"# Changelog

## 0.4.0

TBD
"

expect "the previous notes under a new number are refused" 0.4.0 refuse \
"# Changelog

## 0.4.0

$OLDER

## 0.3.3

$OLDER
"

# Reflowed rather than retyped: the comparison is on the ink, so the same
# sentences wrapped differently are still the same sentences.
expect "and refused even when they have been rewrapped" 0.4.0 refuse \
"# Changelog

## 0.4.0

Fixed the column widths, which were measured
against a model with nothing in it rather than
against the dataset that is open.

## 0.3.3

$OLDER
"

expect "a release below an older one is refused" 0.3.4 refuse \
"# Changelog

## 0.4.0

$REAL

## 0.3.4

$OLDER
"

expect "a date after the version still matches" 0.4.0 ok \
"# Changelog

## 0.4.0 — 2026-09-12

$REAL
"

if [ "$failures" -ne 0 ]; then
    echo
    echo "release-notes: $failures case(s) failed"
    exit 1
fi

echo
echo "release-notes: every case behaved"
