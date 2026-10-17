# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-License-Identifier: GPL-3.0-only

"""plotcore/tests/golden/script.txt, run through the Python binding.

The language is golden/README.md's, and so is the digest: test_capi.c writes
it from C, this writes it from Python, and the two must agree token for token
-- numbers within a relative 1e-9, text exactly.
"""

from __future__ import annotations

import math
from pathlib import Path
from typing import List, Optional, Tuple

import numpy as np

import plotcore
from plotcore import Button, Figure, Modifiers

GOLDEN = Path(__file__).resolve().parents[2] / "tests" / "golden"


def generate(words: List[str]) -> Tuple[np.ndarray, int]:
    """A generator of README.md and how many words it took."""
    kind, n = words[0], int(words[1])
    i = np.arange(n, dtype=np.float64)
    if kind == "ramp":
        return float(words[2]) + i * float(words[3]), 4
    if kind == "quadratic":
        k = np.arange(1, n + 1, dtype=np.float64)
        return float(words[2]) * k * k, 3
    if kind == "square":
        period, low, high = int(words[2]), float(words[3]), float(words[4])
        steps = np.arange(n) % period
        return np.where(steps < period // 2, low, high).astype(np.float64), 5
    if kind == "holes":
        every = int(words[2])
        return np.where(np.arange(n) % every < 3, np.nan, i), 3
    if kind == "walk":
        state = int(words[2]) & 0xFFFFFFFF
        at = 0.0
        out = np.empty(n)
        for k in range(n):
            state = (state * 1103515245 + 12345) & 0xFFFFFFFF
            at += ((state >> 16) & 0x7FFF) / 32767.0 - 0.5
            out[k] = at + 500.0 if k % 997 == 996 else at
        return out, 3
    raise ValueError(f"no generator {kind}")


def _modifiers(word: str) -> Modifiers:
    out = Modifiers.NONE
    if "shift" in word:
        out |= Modifiers.SHIFT
    if "control" in word:
        out |= Modifiers.CONTROL
    if "alt" in word:
        out |= Modifiers.ALT
    return out


def _button(word: str) -> Button:
    return {"middle": Button.MIDDLE, "right": Button.RIGHT}.get(word, Button.LEFT)


def _g(value: float) -> str:
    return repr(float(value))


def _colour(colour) -> str:
    return " " + " ".join(_g(c) for c in colour)


def digest(scene: plotcore.Scene, index: int) -> List[str]:
    out = [f"scene {index}", f"size {_g(scene.width)} {_g(scene.height)}"]
    a = scene.area
    out.append(f"area {_g(a.x)} {_g(a.y)} {_g(a.width)} {_g(a.height)}")
    out.append("view " + " ".join(_g(v) for v in scene.view))
    out.append("ground" + _colour(scene.ground))
    c = scene.chrome
    out.append("chrome" + _colour(c.ink) + _colour(c.axis_rule) + _colour(c.cursor_ink))
    out.append(f"message |{scene.message}")
    for line in scene.lines:
        sx = sy = vx = vy = 0.0
        points = 0
        for run, values in zip(line.runs, line.values):
            for (x, y), (u, v) in zip(run.tolist(), values.tolist()):
                sx += x
                sy += y
                vx += u
                vy += v
            points += len(run)
        out.append(f"line {line.line}" + _colour(line.colour)
                   + f" {_g(line.width)} {len(line.runs)} {points} {len(line.triangles)}"
                     f" {len(line.markers)} {_g(sx)} {_g(sy)}")
        out.append(f"window {_g(line.y_low)} {_g(line.y_high)} {int(line.y_log)} {int(line.own)}"
                   f" {int(line.numbered)} {_g(vx)} {_g(vy)}")
    for name, segments in (("rules", scene.rules), ("strokes", scene.strokes)):
        sx = sy = 0.0
        for s in segments.tolist():
            sx += s[0]
            sx += s[2]
            sy += s[1]
            sy += s[3]
        out.append(f"{name} {len(segments)} {_g(sx)} {_g(sy)}")
    for t in scene.texts:
        out.append(f"text {int(t.role)} {int(t.across)} {int(t.down)} {int(t.vertical)}"
                   f" {_g(t.x)} {_g(t.y)}" + _colour(t.colour) + f" |{t.text}")
    for e in scene.legend:
        out.append(f"legend {e.line}" + _colour(e.colour) + " "
                   + " ".join(_g(v) for v in e.swatch + e.text_at) + f" |{e.name}")
    if scene.legend:
        r = scene.legend_rect
        out.append(f"legend_rect {_g(r.x)} {_g(r.y)} {_g(r.width)} {_g(r.height)}")
    r = scene.reading
    out.append(f"reading {int(r.valid)} {r.line} {_g(r.x)} {_g(r.y)} {_g(r.px)} {_g(r.py)}"
               f" |{r.x_text}|{r.y_text}")
    b = scene.band
    out.append("band 0 0 0 0 0" if b is None else
               f"band 1 {_g(b.x)} {_g(b.y)} {_g(b.width)} {_g(b.height)}")
    return out


def _rest(line: str, skip: int) -> str:
    return line.split(" ", skip)[skip] if line.count(" ") >= skip else ""


def run(script: Path) -> List[str]:
    """Every scene the script asks for, digested, as lines."""
    figure = Figure()
    out: List[str] = []
    last: Optional[plotcore.Scene] = None
    kept: Optional[str] = None
    pointer = [0.0, 0.0]
    scenes = 0

    def sample(line: int, index: int) -> Tuple[float, float]:
        for drawn in last.lines if last else []:
            if drawn.line == line and drawn.runs and index < len(drawn.runs[0]):
                x, y = drawn.runs[0][index]
                return float(x), float(y)
        raise IndexError(f"no sample {index} of line {line}")

    for raw in script.read_text(encoding="utf-8").splitlines():
        line = raw.rstrip("\r")
        if not line or line.startswith("#"):
            continue
        w = line.split(" ")
        command = w[0]
        if command == "size":
            figure.set_size(float(w[1]), float(w[2]), float(w[3]))
        elif command == "line":
            values, used = generate(w[1:])
            figure.add_line(values, _rest(line, 1 + used))
        elif command == "remove":
            figure.remove_line(int(w[1]))
        elif command == "clear":
            figure.clear()
        elif command == "time":
            figure.set_time(None if w[1] == "none" else generate(w[1:])[0])
        elif command == "line_time":
            figure.set_line_time(int(w[1]), generate(w[2:])[0])
        elif command == "budget":
            figure.budget_bytes = int(w[1])
        elif command == "set_bool":
            figure.settings[w[1]] = w[2] != "0"
        elif command == "set_number":
            figure.settings[w[1]] = float(w[2])
        elif command == "set_text":
            figure.settings[w[1]] = _rest(line, 2)
        elif command == "set_colour":
            figure.settings[w[1]] = tuple(float(v) for v in w[2:6])
        elif command == "line_bool":
            figure.line(int(w[1])).settings[w[2]] = w[3] != "0"
        elif command == "line_number":
            figure.line(int(w[1])).settings[w[2]] = float(w[3])
        elif command == "line_text":
            figure.line(int(w[1])).settings[w[2]] = _rest(line, 3)
        elif command == "line_colour":
            figure.line(int(w[1])).colour = tuple(float(v) for v in w[2:6])
        elif command == "wheel":
            pointer = [float(w[1]), float(w[2])]
            figure.wheel(pointer[0], pointer[1], float(w[3]), _modifiers(w[4]))
        elif command == "press":
            pointer = [float(w[1]), float(w[2])]
            figure.press(pointer[0], pointer[1], _button(w[3]), _modifiers(w[4]))
        elif command == "move":
            pointer = [float(w[1]), float(w[2])]
            figure.move(pointer[0], pointer[1], _modifiers(w[3]))
        elif command == "release":
            pointer = [float(w[1]), float(w[2])]
            figure.release(pointer[0], pointer[1], _button(w[3]), _modifiers(w[4]))
        elif command == "double":
            figure.double_click(float(w[1]), float(w[2]), _button(w[3]))
        elif command == "hover":
            pointer = [float(w[1]), float(w[2])]
            figure.hover(*pointer)
        elif command == "leave":
            figure.leave()
        elif command == "reset":
            figure.reset_view()
        elif command == "view":
            figure.set_view(*(float(v) for v in w[1:5]))
        elif command in ("press_line", "wheel_line", "click_line"):
            pointer = list(sample(int(w[1]), int(w[2])))
            if command == "press_line":
                figure.press(pointer[0], pointer[1], Button.LEFT, _modifiers(w[3]))
            elif command == "wheel_line":
                figure.wheel(pointer[0], pointer[1], float(w[3]), _modifiers(w[4]))
            else:
                figure.press(pointer[0], pointer[1], Button.LEFT, _modifiers(w[3]))
                figure.release(pointer[0], pointer[1], Button.LEFT, _modifiers(w[3]))
        elif command == "move_by":
            pointer = [pointer[0] + float(w[1]), pointer[1] + float(w[2])]
            figure.move(pointer[0], pointer[1], _modifiers(w[3]))
        elif command == "release_here":
            figure.release(pointer[0], pointer[1], _button(w[1]), _modifiers(w[2]))
        elif command == "state":
            kept = figure.state()
            out.append(f"state |{kept}")
        elif command == "restore":
            figure.restore(kept or "")
        elif command == "scene":
            last = figure.scene()
            out.extend(digest(last, scenes))
            scenes += 1
        else:
            raise ValueError(f"no command {command}")
    figure.close()
    return out


def same_line(expected: str, actual: str) -> bool:
    """README.md's comparison: numbers within a relative 1e-9, everything after
    the first '|' character for character."""
    head_e, bar_e, tail_e = expected.partition("|")
    head_a, bar_a, tail_a = actual.partition("|")
    if bar_e != bar_a or tail_e != tail_a:
        return False
    words_e = head_e.split()
    words_a = head_a.split()
    if len(words_e) != len(words_a):
        return False
    for e, a in zip(words_e, words_a):
        try:
            x, y = float(e), float(a)
        except ValueError:
            if e != a:
                return False
            continue
        if math.isnan(x) or math.isnan(y):
            if not (math.isnan(x) and math.isnan(y)):
                return False
            continue
        if abs(x - y) > 1e-9 * max(1.0, abs(x), abs(y)):
            return False
    return True
