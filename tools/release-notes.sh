#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-License-Identifier: GPL-3.0-only

#
# What a release says about itself, and whether anybody wrote it.
#
# The release page used to carry a paragraph this repository's workflow wrote:
# the same sentences every time, about the two binaries and the licence. All of
# it true, none of it about the release -- so a reader who came to find out what
# had changed learnt that Qt is linked statically, which they could have learnt
# from the version before it and the one before that.
#
# What changed is a thing only the person cutting the release knows, so it is
# written down where the change itself is: CHANGELOG.md, one section per
# version. This pulls the section out for the version being published.
#
#     tools/release-notes.sh 0.4.0            # prints the section
#     tools/release-notes.sh 0.4.0 --check    # says nothing; exits non-zero
#
# It is also the enforcement, and the enforcement is the point. A release page
# is read once, by someone deciding whether to take the release, and nobody
# proof-reads it afterwards -- so the ways it goes wrong are the ways nobody
# notices. There are four, and all four are refused here:
#
#   missing     no section for this version at all. The notes were never
#               written, or were written under the previous version's heading
#               and the tag moved on without them.
#   duplicated  two sections for the same version, which is what a merge leaves
#               behind. Publishing either silently is publishing a coin toss.
#   copied      a section identical to another version's. That is the previous
#               release's notes with a new number over them, which is worse
#               than no notes: it is wrong rather than absent, and it reads as
#               deliberate.
#   empty       a heading with nothing under it, or with so little that nobody
#               can have said anything. A placeholder somebody meant to come
#               back to.
#
# What cannot be checked here is whether the notes are *true* -- whether they
# describe everything that went into the release. Nothing mechanical can, so
# nothing here pretends to; that is what writing them at the same time as the
# tag is for, which is why BUILDING.md puts it first of the three steps.
#
# The heading is matched exactly: "## <version>", where anything after the
# version on the same line is ignored, which is where a date would go. A section
# runs to the next "## " or to the end of the file.

set -uo pipefail

version="${1:-}"
mode="${2:-print}"
changelog="${CHANGELOG:-$(dirname "$0")/../CHANGELOG.md}"

# Enough characters of actual writing that a heading cannot be called described
# by a shrug. Two short sentences clear it; "TBD", a dash, or a line of dots do
# not. A number rather than a list of forbidden words, because the words people
# leave behind are not a set anyone can enumerate -- and a rule that tries reads
# as an insult the one time it fires on real prose.
readonly MINIMUM_INK=60

die() {
    printf 'error: %s\n' "$1" >&2
    shift
    for line in "$@"; do
        printf '       %s\n' "$line" >&2
    done
    exit 1
}

if [ -z "$version" ]; then
    echo "usage: $(basename "$0") <version> [--check]" >&2
    exit 2
fi

if [ ! -f "$changelog" ]; then
    die "no $changelog to read the notes out of."
fi

# The version each "## " heading names, in the order they appear.
mapfile -t headings < <(
    awk '/^## /{ h = $2; sub(/[[:space:]]*$/, "", h); print h }' "$changelog"
)

# One section, by heading. awk rather than sed: the end of a section is the
# start of the next one, which is a state machine and not a range of lines --
# the last section in the file has nothing after it to stop at. The blank lines
# around it are trimmed so the published notes start where the writing does.
section_of() {
    awk -v want="$1" '
        /^## / {
            h = $2
            sub(/[[:space:]]*$/, "", h)
            inside = (h == want)
            next
        }
        inside { print }
    ' "$changelog" | sed -e '/./,$!d' | tac | sed -e '/./,$!d' | tac
}

# Everything but whitespace, for comparing two sections without one of them
# losing on a reflowed line.
ink_of() {
    printf '%s' "$1" | tr -d '[:space:]'
}

# --- missing, and out of order -------------------------------------------
found=0
for heading in ${headings[@]+"${headings[@]}"}; do
    [ "$heading" = "$version" ] && found=$((found + 1))
done

if [ "$found" -eq 0 ]; then
    die "CHANGELOG.md has no '## $version' section." \
        "Every tag carries a short description of what changed in it." \
        "Write one at the top of that file, then tag. See its head."
fi

# --- duplicated ----------------------------------------------------------
if [ "$found" -gt 1 ]; then
    die "CHANGELOG.md has $found '## $version' sections." \
        "Publishing one of them silently is publishing a coin toss." \
        "Merge them into one."
fi

# --- empty ---------------------------------------------------------------
notes=$(section_of "$version")
ink=$(ink_of "$notes")

if [ -z "$ink" ]; then
    die "CHANGELOG.md's '## $version' section is empty." \
        "A heading with nothing under it is a release that says nothing."
fi

if [ "${#ink}" -lt "$MINIMUM_INK" ]; then
    die "CHANGELOG.md's '## $version' section is ${#ink} characters long." \
        "That is a placeholder rather than a description of a release." \
        "Say what was added, what changed under it, and what broke."
fi

# --- copied --------------------------------------------------------------
# Against every other version in the file rather than against the one above it:
# notes copied from two releases back are the same mistake and read the same
# way to whoever downloads it.
for heading in ${headings[@]+"${headings[@]}"}; do
    [ "$heading" = "$version" ] && continue
    if [ "$(ink_of "$(section_of "$heading")")" = "$ink" ]; then
        die "CHANGELOG.md's '## $version' section is word for word '## $heading'." \
            "Those are the previous notes with a new number over them, which is" \
            "worse than no notes: it is wrong rather than absent."
    fi
done

# --- out of order --------------------------------------------------------
# Newest first is the file's contract, and a release that is not at the top of
# it is one whose notes were written into the middle of the history -- which is
# what happens when the section that was updated was the one already there.
if [ "${headings[0]}" != "$version" ]; then
    die "CHANGELOG.md's first section is '## ${headings[0]}', not '## $version'." \
        "The newest release goes at the top; a section further down is one that" \
        "was already there when this release was cut."
fi

if [ "$mode" = "--check" ]; then
    exit 0
fi

printf '%s\n' "$notes"
