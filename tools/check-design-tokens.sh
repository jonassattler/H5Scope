#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-License-Identifier: GPL-3.0-only

#
# Design-system adherence check.
#
# This is the portable half of the TBD design system's adherence rules. The
# system states them as an oxlint config, which lints React/JSX -- something
# this project does not have. Three of the rules do survive the port to QML,
# because they are about the design system rather than about React:
#
#   error  no raw hex colour literals   ("Raw hex color -- use a design-system
#                                         color token via var()")
#   error  no font family named outside the singleton
#                                        ("Font not provided by the design system")
#   warn   no raw pixel numbers          ("Raw px value -- use a design-system
#                                         spacing token via var()")
#
# `var()` is `Theme.` here: src/qml/Theme.qml is the one file allowed to hold
# literal values, and it is exempt from all three rules.
#
# The rules that do not survive are the per-component prop and variant whitelists
# (<Button variant=...>, <Badge tone=...> and so on). Those components are React;
# their QML counterparts are AppToolButton and AppTabButton, whose property sets
# the QML engine already type-checks at load.
#
# One rule is this project's own rather than the system's:
#
#   error  no animations                ("This application animates nothing")
#
# The design system has a motion ladder and this port carries it, but the
# application spends none of it: every hover, press, selection and panel here
# changes state between one frame and the next. See the motion note in Theme.qml
# for why. It is a rule a grep can keep and a reviewer cannot, because a
# `Behavior` is four lines at the bottom of a block nobody is reading.
#
# Severities are the design system's own: hex and font violations fail the
# check, raw pixel values are reported but do not. The animation rule is an
# error, because a single one of them undoes the whole of the property.

set -uo pipefail

qml_dir="${1:-$(dirname "$0")/../src/qml}"
singleton="Theme.qml"

mapfile -t files < <(find "$qml_dir" -name '*.qml' ! -name "$singleton" | sort)

if [ ${#files[@]} -eq 0 ]; then
    echo "check-design-tokens: no QML files found under $qml_dir" >&2
    exit 1
fi

errors=0
warnings=0

report() {
    local severity="$1" message="$2" pattern="$3"
    local hits
    hits=$(grep -nE "$pattern" "${files[@]}" || true)
    [ -z "$hits" ] && return 0

    while IFS= read -r hit; do
        echo "$severity: $message"
        echo "    ${hit}"
        if [ "$severity" = error ]; then
            errors=$((errors + 1))
        else
            warnings=$((warnings + 1))
        fi
    done <<< "$hits"
}

report error \
    "Raw hex color -- use a design-system color token from Theme." \
    '"#[0-9a-fA-F]{3,8}"'

report error \
    "Font not provided by the design system -- name families only in $singleton." \
    '\bfamil(y|ies)[[:space:]]*:'

report error \
    "This application animates nothing -- see the motion note in $singleton." \
    '\b(Behavior[[:space:]]+on[[:space:]]|(Number|Color|Property|Rotation|Vector3d|Anchor|Parent|Path|Sequential|Parallel|Pause|Script|Smoothed|Spring)Animation|(Opacity|Scale|Rotation|X|Y|UniformTo)Animator)\b'

report warning \
    "Raw px value -- prefer a design-system spacing token from Theme." \
    '\b(implicitWidth|implicitHeight|spacing|radius|pixelSize|padding|topMargin|bottomMargin|leftMargin|rightMargin|margins|letterSpacing)[[:space:]]*:[[:space:]]*-?[1-9][0-9]*(\.[0-9]+)?\b'

echo
echo "check-design-tokens: ${errors} error(s), ${warnings} warning(s) across ${#files[@]} file(s)"
[ "$errors" -eq 0 ]
