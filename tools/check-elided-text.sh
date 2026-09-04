#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-License-Identifier: GPL-3.0-only

#
# Nothing elides without somewhere to read it in full.
#
# The rule this enforces is one sentence: data must never be inaccessible just
# because the UI is too small for it. A Text that sets `elide` has agreed to
# show the reader only part of what it was given, and the only thing that makes
# that acceptable is a way to get the rest -- which in this application is
# AppToolTip.
#
# So: every `elide:` in the QML must have an AppToolTip within the item that
# carries it. The window is a fixed number of lines rather than a brace parse,
# which is crude but has no false negatives that matter -- a tip further away
# than this belongs to a different item anyway, and the check is a reminder
# rather than a proof.
#
# AppToolTip.qml itself is exempt: it is the thing being pointed at.

set -uo pipefail

qml_dir="${1:-$(dirname "$0")/../src/qml}"
window=25

mapfile -t files < <(find "$qml_dir" -name '*.qml' ! -name 'AppToolTip.qml' | sort)

if [ ${#files[@]} -eq 0 ]; then
    echo "check-elided-text: no QML files found under $qml_dir" >&2
    exit 1
fi

errors=0
for file in "${files[@]}"; do
    while IFS=: read -r line _; do
        [ -z "$line" ] && continue
        if ! sed -n "${line},$((line + window))p" "$file" | grep -q 'AppToolTip'; then
            echo "error: text elides with no AppToolTip to read it in full."
            echo "    ${file}:${line}"
            errors=$((errors + 1))
        fi
    done < <(grep -n '^[[:space:]]*elide:' "$file" | cut -d: -f1 | sed 's/$/:/')
done

echo
echo "check-elided-text: ${errors} error(s) across ${#files[@]} file(s)"
[ "$errors" -eq 0 ]
