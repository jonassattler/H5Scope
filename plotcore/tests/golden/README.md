<!--
SPDX-FileCopyrightText: 2026 Jonas Sattler
SPDX-License-Identifier: GPL-3.0-only
-->

# One plot, three languages

`script.txt` drives a plot. Each binding runs it through its own interface
and writes a digest of every `scene` it asks for. The C test
(`test_capi`), the Python tests and the Rust tests must all write exactly
`expected.txt`. A digest that matches the C one is a binding that hands the
same calls to plotcore and reads the same picture back.

To regenerate `expected.txt` after a change to the plot, run
`test_capi --write`. Then read the diff, because every changed line is a
changed picture.

## The script

One command a line. `#` starts a comment, and blank lines are passed over.
Words are separated by single spaces. A trailing text argument (a name, a
title, a mode) is the rest of the line.

| Command | What it does |
|---|---|
| `size W H R` | `set_size(W, H, R)` |
| `line GEN ARGS... NAME` | add a line made by `GEN` (below), named `NAME` |
| `remove I` / `clear` | `remove_line(I)` / `clear()` |
| `time GEN ARGS...` / `time none` | the shared time base, or none |
| `line_time I GEN ARGS...` | line `I`'s own time base |
| `budget BYTES` | `set_budget_bytes(BYTES)` |
| `set_bool K V`, `set_number K V`, `set_text K TEXT` | the named setting |
| `set_colour K R G B A` | the named colour |
| `line_bool I K V`, `line_number I K V`, `line_text I K TEXT` | line `I`'s setting |
| `line_colour I R G B A` | line `I`'s own colour |
| `wheel X Y DELTA MODS` | `wheel(X, Y, DELTA, MODS)` |
| `press X Y BUTTON MODS`, `move X Y MODS`, `release X Y BUTTON MODS` | the pointer |
| `double X Y BUTTON`, `hover X Y`, `leave`, `reset` | the same names |
| `view X0 X1 Y0 Y1` | `set_view` |
| `press_line I S MODS` | press the left button on sample `S` of line `I`'s first run, as the last scene drew it |
| `move_by DX DY MODS` | move by that much from where the pointer is |
| `release_here BUTTON MODS` | release where the pointer is |
| `wheel_line I S DELTA MODS` | the wheel over sample `S` of line `I` |
| `click_line I S MODS` | press and release the left button on sample `S` of line `I` |
| `state` | write the state down, and keep it |
| `restore` | restore the state that was kept |
| `scene` | digest a scene |

`MODS` is `none` or names joined by `|`: `shift`, `control` and `alt`.
`BUTTON` is `left`, `middle` or `right`.

The generators build their values in exact arithmetic, or in arithmetic
every language does identically. None of them calls a transcendental
function, because libm is allowed to differ by an ulp between platforms:

| Generator | Element `i` |
|---|---|
| `ramp N FROM STEP` | `FROM + i * STEP` |
| `quadratic N SCALE` | `SCALE * (i + 1) * (i + 1)` |
| `square N PERIOD LOW HIGH` | `LOW` in the first half of each period, `HIGH` in the second |
| `holes N EVERY` | `i`, but NaN wherever `i % EVERY < 3` |
| `walk N SEED` | a random walk: `state = state * 1103515245 + 12345` modulo 2³², then `at += ((state >> 16) & 0x7fff) / 32767 - 0.5`, plus 500 on every element where `i % 997 == 996` |

## The digest

A `scene` writes the lines below. The line that opens it is `scene N`, where
`N` counts from zero. Numbers are written in whatever round-trip form the
language writes. A digest is compared token by token: numbers within a
relative 1e-9, everything after a `|` character for character.

    scene N
    size W H
    area X Y W H
    view X0 X1 Y0 Y1
    ground R G B A
    chrome INK_RGBA AXIS_RULE_RGBA CURSOR_INK_RGBA
    message |TEXT
    line INDEX R G B A WIDTH RUNS POINTS TRIANGLE_POINTS MARKERS SUM_X SUM_Y
    window Y_LOW Y_HIGH Y_LOG OWN NUMBERED SUM_VALUE_X SUM_VALUE_Y
    rules COUNT SUM_X SUM_Y
    strokes COUNT SUM_X SUM_Y
    text ROLE ACROSS DOWN VERTICAL X Y R G B A |TEXT
    legend LINE R G B A X0 Y0 X1 Y1 TEXT_X TEXT_Y |NAME
    legend_rect X Y W H
    reading VALID LINE X Y PX PY |X_TEXT|Y_TEXT
    band PRESENT X Y W H

A `state` command writes `state |JSON`.

`SUM_X` and `SUM_Y` add up every run point's coordinates (or both ends of every
segment) in order, and `SUM_VALUE_X` and `SUM_VALUE_Y` the same points in the
data's own units. A legend appears only when it has entries, and its rect
only then.
