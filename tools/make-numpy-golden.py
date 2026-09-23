#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-License-Identifier: GPL-3.0-only

"""Write the numpy answers the postprocessing suite is checked against.

`postprocessing.md` asks for these operations to work *exactly* as numpy's do,
and the only way to know whether they do is to ask numpy. This runs a few
hundred shape-and-argument combinations through it and writes what came back
into a header the Catch2 suite includes.

Committed rather than generated at build time, because this project builds
every dependency from a pinned manifest and never uses a system library. A
CTest suite that shelled out to whatever Python happened to be installed would
be exactly the thing the design goals rule out -- and it would make the test
results depend on the machine rather than on the code. Re-run it by hand when a
case is added:

    python3 tools/make-numpy-golden.py

The header records what numpy *did*, including the calls it refused. It does not
record what this application does about that, which is the point: the two
documented places where the two deliberately part company are applied in
test_postprocess.cpp, where they can be read as rules rather than as data.
"""

import itertools
import math
import pathlib
import sys

try:
    import numpy as np
except ImportError:  # pragma: no cover - the message is the whole handling
    sys.exit("this needs numpy: pip install numpy")

OUT = pathlib.Path(__file__).resolve().parent.parent / "tests" / "data" / "NumpyGolden.hpp"

# Shapes worth asking about: a scalar, the degenerate extents, the empty ones,
# a vector, a matrix, and enough rank to make an axis argument mean something.
SHAPES = [
    (),
    (1,),
    (5,),
    (0,),
    (2, 3),
    (3, 1),
    (1, 3),
    (0, 3),
    (3, 0),
    (4, 4),
    (2, 3, 4),
    (1, 2, 1, 3),
    (2, 2, 2, 2),
    (2, 3, 4, 5),
    (2, 1, 3, 1, 2),
]

# Subscript lines, tried against every shape whose rank they could apply to.
# Every one of them is something the slice bar above the table already accepts.
SUBSCRIPTS = [
    "...",
    ":",
    "0",
    "-1",
    "0:2",
    "1:",
    ":-1",
    "::2",
    "::-1",
    "1::2",
    "-2:",
    "0:100",
    "[0,2]",
    "[2,0]",
    ":, 0",
    "0, :",
    "-1, -1",
    "::2, ::-1",
    ":, :, 0",
    "0, :, -1",
    "..., 0",
    "0, ...",
    "1, 2, 3",
    ":, 0, :, -1",
]

# Including the ones numpy refuses: an axis named twice, and one past the end.
AXES = ["", "0", "1", "2", "-1", "-2", "0,1", "0,2", "1,2", "0,1,2", "3", "5",
        "0,0", "1,-1", "-1,-1", "0,-2"]

PERMUTATIONS = ["", "0", "1,0", "0,1", "-1,-2", "2,0,1", "0,2,1", "2,1,0",
                "-1,0,1", "3,2,1,0", "1,0,3,2", "0,1,2",
                # Refused by numpy: a repeated axis, and too few of them.
                "0,0", "1,1,1", "0,1,1", "1"]

RESHAPES = ["1", "-1", "5", "0", "2,3", "3,2", "6", "-1,2", "2,-1", "-1,-1",
            "1,-1", "24", "2,12", "4,3,2", "-1,4", "0,-1", "2,3,4", "120",
            "-1,5", "1,1,1"]

def _axes(text):
    """The axis argument a comma-separated list stands for (see axes_argument,
    which is defined further down with the other readers)."""
    if text == "":
        return None
    parts = [int(p) for p in text.split(",")]
    return parts[0] if len(parts) == 1 else tuple(parts)


# The operations added after the first six, each as the argument this
# application reads and the numpy call that argument stands for. Written out
# pair by pair rather than parsed, because the parsing is the thing under test:
# a golden file that derived the call from the text with the same rules as the
# code would agree with the code by construction.
FOLDS = [(text, {"axis": _axes(text)}) for text in
         ["", "0", "1", "2", "-1", "-2", "0,1", "0,2", "1,2", "0,1,2", "3", "5",
          "0,0", "1,-1", "-1,-1", "0,-2"]] + [
    ("None", {"axis": None}),
    ("initial=5", {"initial": 5.0}),
    ("0, initial=2.5", {"axis": 0, "initial": 2.5}),
    ("axis=(0,1)", {"axis": (0, 1)}),
    ("axis=-1, initial=-1", {"axis": -1, "initial": -1.0}),
    ("(0, 1), initial=0.5", {"axis": (0, 1), "initial": 0.5}),
]

SCANS = [
    ("", {}),
    ("None", {"axis": None}),
    ("0", {"axis": 0}),
    ("1", {"axis": 1}),
    ("-1", {"axis": -1}),
    ("-2", {"axis": -2}),
    ("2", {"axis": 2}),
    ("5", {"axis": 5}),
    ("axis=0", {"axis": 0}),
]

DIFFS = [
    ("", {}),
    ("1", {"n": 1}),
    ("2", {"n": 2}),
    ("0", {"n": 0}),
    ("5", {"n": 5}),
    ("-1", {"n": -1}),
    ("1, 0", {"n": 1, "axis": 0}),
    ("1, -2", {"n": 1, "axis": -2}),
    ("2, 1", {"n": 2, "axis": 1}),
    ("3, -1", {"n": 3, "axis": -1}),
    ("1, 5", {"n": 1, "axis": 5}),
    ("axis=0", {"axis": 0}),
    ("n=2, axis=0", {"n": 2, "axis": 0}),
]

CLIPS = [
    ("", (None, None)),
    ("0, 1", (0.0, 1.0)),
    ("-1, None", (-1.0, None)),
    ("None, 2", (None, 2.0)),
    ("2, 1", (2.0, 1.0)),
    ("None, None", (None, None)),
    ("min=0", (0.0, None)),
    ("max=0.5", (None, 0.5)),
    ("a_min=-1, a_max=1", (-1.0, 1.0)),
    ("0, nan", (0.0, math.nan)),
    # The signed zeros, where the three functions numpy's clip is made of
    # part company -- see clipped() in Operations.cpp.
    ("0, 0", (0.0, 0.0)),
    ("-0.0, 0", (-0.0, 0.0)),
    ("0", (0.0, None)),
    ("-0.0", (-0.0, None)),
    ("None, 0", (None, 0.0)),
    ("None, -0.0", (None, -0.0)),
    ("nan, 1", (math.nan, 1.0)),
    ("nan", (math.nan, None)),
]

POWERS = ["2", "0.5", "-1", "0", "3", "exponent=2", ""]
ADDENDS = ["1.5", "-2", "0", "inf", ""]
FACTORS = ["2", "-0.5", "0", "inf", ""]


def payload(shape):
    """Values with something in them: a ramp, a negative half, and the three
    doubles that are not numbers."""
    size = int(np.prod(shape)) if shape else 1
    values = np.arange(size, dtype=np.float64) - size / 3.0
    if size >= 4:
        values[1] = -values[1]
        values[2] = math.nan
        values[3] = math.inf
    if size >= 6:
        values[4] = -math.inf
        values[5] = -0.0
    return values.reshape(shape)


def literal(value):
    if math.isnan(value):
        return "NAN"
    if math.isinf(value):
        return "INFINITY" if value > 0 else "-INFINITY"
    # repr gives the shortest text that reads back as the same double, which is
    # what a C++ literal of it needs to be.
    return repr(float(value))


def escaped(text):
    return text.replace("\\", "\\\\").replace('"', '\\"')


def record(name, operation, argument, data, run):
    try:
        result = run(data)
    except Exception as error:  # noqa: BLE001 - every refusal is a result
        return {
            "name": name,
            "operation": operation,
            "argument": argument,
            "shape": data.shape,
            "input": data,
            "raised": True,
            "why": str(error).splitlines()[0][:120],
            "outShape": (),
            "output": np.array([]),
        }
    result = np.asarray(result, dtype=np.float64)
    return {
        "name": name,
        "operation": operation,
        "argument": argument,
        "shape": data.shape,
        "input": data,
        "raised": False,
        "why": "",
        "outShape": result.shape,
        "output": result.reshape(-1),
    }


def axes_argument(text):
    """The tuple an axis argument stands for: nothing at all when it is empty,
    a bare int when it names one, a tuple when it names several."""
    if text == "":
        return None
    parts = [int(p) for p in text.split(",")]
    return parts[0] if len(parts) == 1 else tuple(parts)


def subscript_argument(text):
    """The slice line as an index expression numpy can be handed."""
    terms = []
    depth = 0
    current = ""
    for character in text:
        if character == "[":
            depth += 1
        elif character == "]":
            depth -= 1
        if character == "," and depth == 0:
            terms.append(current)
            current = ""
            continue
        current += character
    terms.append(current)

    out = []
    for term in terms:
        term = term.strip()
        if term == "...":
            out.append(Ellipsis)
        elif term.startswith("[") and term.endswith("]"):
            out.append([int(p) for p in term[1:-1].split(",")])
        elif ":" in term:
            bounds = [p.strip() for p in term.split(":")]
            bounds += [""] * (3 - len(bounds))
            out.append(slice(*(int(b) if b else None for b in bounds)))
        else:
            out.append(int(term))
    return tuple(out)


def cases():
    out = []
    for shape in SHAPES:
        data = payload(shape)
        rank = len(shape)

        for text in SUBSCRIPTS:
            written = len([t for t in text.split(",") if t.strip() != "..."])
            if written > rank and text != "...":
                continue
            key = subscript_argument(text)
            out.append(record(f"{text} of {shape}", "Slice", text, data,
                              lambda a, k=key: a[k]))

        for text in PERMUTATIONS:
            out.append(record(f"transpose({text}) of {shape}", "Transpose", text,
                              data,
                              lambda a, t=text: np.transpose(
                                  a, None if t == "" else
                                  tuple(int(p) for p in t.split(",")))))

        for text in AXES:
            for name, function in (("Min", np.min), ("Max", np.max)):
                out.append(record(f"{name.lower()}(axis={text or None}) of {shape}",
                                  name, text, data,
                                  lambda a, f=function, t=text: f(
                                      a, axis=axes_argument(t))))

        out.append(record(f"abs of {shape}", "Abs", "", data, np.abs))

        for text in RESHAPES:
            out.append(record(f"reshape({text}) of {shape}", "Reshape", text, data,
                              lambda a, t=text: a.reshape(
                                  tuple(int(p) for p in t.split(",")))))

        for text, keywords in FOLDS:
            for name, function in (("Sum", np.sum), ("Prod", np.prod)):
                out.append(record(f"{name.lower()}({text}) of {shape}", name, text,
                                  data, lambda a, f=function, k=keywords: f(a, **k)))

        for text, keywords in SCANS:
            for name, function in (("CumSum", np.cumsum), ("CumProd", np.cumprod)):
                out.append(record(f"{name.lower()}({text}) of {shape}", name, text,
                                  data, lambda a, f=function, k=keywords: f(a, **k)))

        for text, keywords in DIFFS:
            out.append(record(f"diff({text}) of {shape}", "Diff", text, data,
                              lambda a, k=keywords: np.diff(a, **k)))

        for text, (low, high) in CLIPS:
            out.append(record(f"clip({text}) of {shape}", "Clip", text, data,
                              lambda a, lo=low, hi=high: np.clip(a, lo, hi)))

        out.append(record(f"sqrt of {shape}", "Sqrt", "", data, np.sqrt))

        for text in POWERS:
            out.append(record(f"pow({text}) of {shape}", "Pow", text, data,
                              lambda a, t=text: np.pow(a, float(t.split("=")[-1]))
                              if t else np.pow(a)))

        for text in ADDENDS:
            out.append(record(f"add({text}) of {shape}", "Add", text, data,
                              lambda a, t=text: np.add(a, float(t)) if t else np.add(a)))

        for text in FACTORS:
            out.append(record(f"multiply({text}) of {shape}", "Multiply", text, data,
                              lambda a, t=text: np.multiply(a, float(t))
                              if t else np.multiply(a)))
    return out


def render(entries):
    # Flat arrays of plain numbers, pooled, and the vectors built from them at
    # run time. The header used to spell every case as a brace-initialised
    # struct of four std::vectors, and once the operations after the first six
    # doubled the number of cases GCC spent the better part of ten minutes on
    # that one initialiser: an aggregate of non-trivial types is compiled as
    # code, element by element, where an array of doubles is data. Pooling
    # also means a shape's payload -- the same for every call made on it -- is
    # written once rather than once per call.
    extents = []
    extentAt = {}
    values = []
    valueAt = {}

    def pooled(items, pool, index, text):
        key = tuple(text(v) for v in items)
        if key not in index:
            index[key] = len(pool)
            pool.extend(key)
        return index[key], len(key)

    rows = []
    for entry in entries:
        shape = pooled(entry["shape"], extents, extentAt, lambda e: str(int(e)))
        data = pooled(np.asarray(entry["input"]).reshape(-1), values, valueAt, literal)
        out_shape = pooled(entry["outShape"], extents, extentAt, lambda e: str(int(e)))
        output = pooled(np.asarray(entry["output"]).reshape(-1), values, valueAt, literal)
        rows.append(
            f'    {{"{escaped(entry["name"])}", "{entry["operation"]}", '
            f'"{escaped(entry["argument"])}", {shape[0]}, {shape[1]}, {data[0]}, '
            f'{data[1]}, {"true" if entry["raised"] else "false"}, '
            f'"{escaped(entry["why"])}", {out_shape[0]}, {out_shape[1]}, {output[0]}, '
            f"{output[1]}}},")

    def wrapped(items):
        out = []
        for at in range(0, len(items), 8):
            out.append("    " + ", ".join(items[at:at + 8]) + ",")
        return out

    lines = [
        # Emitted, not inherited: this file is overwritten wholesale on every
        # run, so a header added by hand would survive exactly until the next
        # regeneration.
        "// SPDX-FileCopyrightText: 2026 Jonas Sattler",
        "// SPDX-License-Identifier: GPL-3.0-only",
        "",
        "#pragma once",
        "",
        "// Generated by tools/make-numpy-golden.py against numpy "
        f"{np.__version__}. Do not edit:",
        "// re-run the script, which is what makes this file worth anything.",
        "//",
        "// Each entry is one call and what numpy made of it -- including the",
        "// calls it refused, which are as much a part of the contract as the",
        "// answers. What this application does differently is applied in",
        "// test_postprocess.cpp rather than baked in here.",
        "",
        "#include <hdf5.h>",
        "",
        "#include <cmath>",
        "#include <iterator>",
        "#include <string>",
        "#include <vector>",
        "",
        "namespace golden {",
        "",
        "struct Case {",
        "    std::string name;      ///< the call, for the failure message",
        "    std::string operation; ///< as postproc::operations() names it",
        "    std::string argument;",
        "    std::vector<hsize_t> shape;  ///< of the input",
        "    std::vector<double> input;   ///< row-major",
        "    bool raised = false;         ///< numpy refused the call",
        "    std::string why;             ///< its first line, when it did",
        "    std::vector<hsize_t> outShape;",
        "    std::vector<double> output;  ///< row-major",
        "};",
        "",
        "namespace detail {",
        "",
        "/// One case, as offsets into the two pools below.",
        "struct Raw {",
        "    const char* name;",
        "    const char* operation;",
        "    const char* argument;",
        "    unsigned shape, rank, input, size;",
        "    bool raised;",
        "    const char* why;",
        "    unsigned outShape, outRank, output, outSize;",
        "};",
        "",
        "// clang-format off",
        "inline constexpr hsize_t kExtents[] = {",
        *wrapped(extents or ["0"]),
        "};",
        "",
        "inline constexpr double kValues[] = {",
        *wrapped(values or ["0.0"]),
        "};",
        "",
        "inline constexpr Raw kCases[] = {",
        *rows,
        "};",
        "// clang-format on",
        "",
        "} // namespace detail",
        "",
        "inline const std::vector<Case>& cases()",
        "{",
        "    static const std::vector<Case> kCases = [] {",
        "        std::vector<Case> out;",
        "        out.reserve(std::size(detail::kCases));",
        "        for (const detail::Raw& raw : detail::kCases) {",
        "            out.push_back(Case{",
        "                raw.name, raw.operation, raw.argument,",
        "                {detail::kExtents + raw.shape, detail::kExtents + raw.shape + raw.rank},",
        "                {detail::kValues + raw.input, detail::kValues + raw.input + raw.size},",
        "                raw.raised, raw.why,",
        "                {detail::kExtents + raw.outShape,",
        "                 detail::kExtents + raw.outShape + raw.outRank},",
        "                {detail::kValues + raw.output, detail::kValues + raw.output + raw.outSize},",
        "            });",
        "        }",
        "        return out;",
        "    }();",
        "    return kCases;",
        "}",
        "",
        "} // namespace golden",
        "",
    ]
    return "\n".join(lines)


def main():
    entries = cases()
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(render(entries))
    refused = sum(1 for e in entries if e["raised"])
    print(f"{OUT}: {len(entries)} cases, {refused} of them refused by numpy")


if __name__ == "__main__":
    main()
