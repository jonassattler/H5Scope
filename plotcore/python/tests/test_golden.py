# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-License-Identifier: GPL-3.0-only

"""One plot in three languages: the golden script, from Python, must write the
digest test_capi.c wrote from C."""

from golden import GOLDEN, run, same_line


def test_the_golden_script_draws_the_c_digest():
    actual = run(GOLDEN / "script.txt")
    expected = (GOLDEN / "expected.txt").read_text(encoding="utf-8").splitlines()
    for number, (e, a) in enumerate(zip(expected, actual), start=1):
        assert same_line(e, a), f"golden line {number}\n  expected: {e}\n  actual:   {a}"
    assert len(actual) == len(expected), "a digest of a different length"
