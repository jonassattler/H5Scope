#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-License-Identifier: GPL-3.0-only

#
# What a release says about itself.
#
# The release page used to carry a paragraph this workflow wrote: the same
# sentences every time, about the two binaries and the licence. All of it true,
# none of it about the release -- so a reader who came to find out what had
# changed learnt that Qt is linked statically, which they could have learnt from
# the version before it and the one before that.
#
# What changed is a thing only the person cutting the release knows, so it is
# written down where the change itself is: CHANGELOG.md, one section per
# version. This pulls the section out for the version being published.
#
# It is also the enforcement. A release whose notes nobody wrote is not a
# release worth publishing, so a missing section is an error rather than an
# empty page -- and CI runs this on a tag push in its first job, six seconds in,
# rather than discovering it after twenty-six minutes of building.
#
#     tools/release-notes.sh 0.4.0            # prints the section
#     tools/release-notes.sh 0.4.0 --check    # says nothing; exits non-zero
#
# The heading is matched exactly: "## <version>", optionally followed by
# anything on the same line, which is where a date goes. The section runs to
# the next "## " or the end of the file, and the blank lines around it are
# trimmed so the published notes start with the first thing that was written.

set -uo pipefail

version="${1:-}"
mode="${2:-print}"
changelog="${CHANGELOG:-$(dirname "$0")/../CHANGELOG.md}"

if [ -z "$version" ]; then
    echo "usage: $(basename "$0") <version> [--check]" >&2
    exit 2
fi

if [ ! -f "$changelog" ]; then
    echo "error: no $changelog to read the notes out of." >&2
    exit 1
fi

# awk rather than sed: the end of a section is the start of the next one, which
# is a state machine and not a range of lines -- the last section has no line
# after it to stop at.
notes=$(awk -v want="$version" '
    /^## / {
        heading = $2
        sub(/[[:space:]]*$/, "", heading)
        inside = (heading == want)
        next
    }
    inside { print }
' "$changelog")

# Leading and trailing blank lines, both gone. The notes are pasted into a
# release page and should start where the writing does.
notes=$(printf '%s\n' "$notes" | sed -e '/./,$!d' | tac | sed -e '/./,$!d' | tac)

if [ -z "$notes" ]; then
    echo "error: CHANGELOG.md has no '## $version' section with anything in it." >&2
    echo "       Every tag carries a short description of what changed in it;" >&2
    echo "       write one there, then tag. See the head of that file." >&2
    exit 1
fi

if [ "$mode" = "--check" ]; then
    exit 0
fi

printf '%s\n' "$notes"
