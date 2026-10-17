# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-License-Identifier: GPL-3.0-only

"""What the example plots: signals made to show one thing each, and the two
file formats a reader is most likely to have a line in."""

from __future__ import annotations

import csv
from pathlib import Path
from typing import List, Tuple

import numpy as np

Signal = Tuple[str, np.ndarray]


def spiked_sine(count: int = 200_000) -> Signal:
    """A sine with one sample a hundred times its height -- the spike an
    envelope keeps and a stride throws away."""
    values = np.sin(2 * np.pi * np.arange(count) / 5000.0)
    values[count * 7 // 10] = 100.0
    return "sine with a spike", values


def square_wave(count: int = 20_000) -> Signal:
    return "square wave", np.where((np.arange(count) // 250) % 2 == 0, -1.0, 1.0)


def with_gaps(count: int = 20_000) -> Signal:
    """A ramp with runs of NaN in it: every gap ends a stroke."""
    i = np.arange(count)
    values = np.cos(i / 300.0) + 0.0001 * i
    values[(i // 400) % 5 == 4] = np.nan
    return "with gaps", values


def many_decades(count: int = 100_000) -> Signal:
    """Twelve decades, for a logarithmic axis."""
    at = np.linspace(0.0, 1.0, count)
    return "twelve decades", 10.0 ** (-6 + 12 * at) * (1 + 0.3 * np.sin(40 * at))


def random_walk(count: int = 1_000_000, seed: int = 1) -> Signal:
    rng = np.random.default_rng(seed)
    values = np.cumsum(rng.random(count) - 0.5)
    values[996::997] += 500.0
    return "random walk", values


def ten_million() -> Signal:
    """Ten million samples of a chirp with noise, and two samples far from
    where anyone would think to zoom."""
    count = 10_000_000
    t = np.arange(count) / count
    rng = np.random.default_rng(2026)
    values = np.sin(2 * np.pi * (20 * t + 2000 * t * t)) + 0.2 * (rng.random(count) - 0.5)
    values[7_777_777] = 6.0
    values[123_456] = -6.0
    return "ten million", values


SIGNALS = [
    ("Sine with a one-sample spike", spiked_sine),
    ("Square wave", square_wave),
    ("Gaps (NaN)", with_gaps),
    ("Twelve decades", many_decades),
    ("Random walk", random_walk),
    ("Ten million samples", ten_million),
]


def regular_time(count: int, step: float = 0.001) -> np.ndarray:
    return np.arange(count) * step


def irregular_time(count: int, seed: int = 5) -> np.ndarray:
    """A time base that only goes one way, at a rate that wanders."""
    rng = np.random.default_rng(seed)
    steps = 0.0002 + 0.0018 * rng.random(count) * rng.random(count)
    return np.concatenate([[0.0], np.cumsum(steps[:-1])])


def load(path: str) -> List[Signal]:
    """The lines in a .npy file (1-D, or one per column of 2-D) or a CSV (one
    per numeric column; a first row that is not numbers names them)."""
    file = Path(path)
    if file.suffix.lower() == ".npy":
        array = np.load(file, allow_pickle=False)
        if array.ndim == 1:
            return [(file.stem, array.astype(np.float64))]
        if array.ndim == 2:
            return [(f"{file.stem}[:, {c}]", array[:, c].astype(np.float64))
                    for c in range(array.shape[1])]
        raise ValueError(f"only 1-D and 2-D arrays are plotted, not {array.shape}")
    with open(file, newline="", encoding="utf-8") as handle:
        sample = handle.read(4096)
        handle.seek(0)
        dialect = csv.Sniffer().sniff(sample, delimiters=",;\t")
        rows = list(csv.reader(handle, dialect))
    rows = [row for row in rows if row]
    if not rows:
        raise ValueError(f"no numbers in {path}")

    def number(text: str) -> float:
        try:
            return float(text)
        except ValueError:
            return float("nan")

    header = rows[0]
    named = any(np.isnan(number(cell)) for cell in header)
    body = rows[1:] if named else rows
    names = header if named else [f"column {c}" for c in range(len(header))]
    columns = len(names)
    table = np.full((len(body), columns), np.nan)
    for r, row in enumerate(body):
        for c, cell in enumerate(row[:columns]):
            table[r, c] = number(cell)
    return [(names[c], table[:, c]) for c in range(columns)]
