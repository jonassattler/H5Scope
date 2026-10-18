// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Which values an axis is numbered at, what each says, and where it is ruled.
//
// A port of the viewer's PlotFrame.qml, function for function and under the
// same names, so that a library drawing a plot numbers it exactly as the
// application does. Exactly is the word: tests/qml/tst_plotcore_parity.qml runs
// these and the QML over the same table of views and compares every value and
// every label, because two ways of numbering one axis are two answers to
// "where is this", and the first time they disagreed would be the first time
// anybody noticed.
//
// The logarithmic half is matplotlib's -- LogLocator and LogFormatterSciNotation
// as they stand in 3.11, ported rather than reasoned out again, with the two
// departures PlotFrame.qml names (a linear fallback on every base, and more
// than %g's figures where six would print one label twice). Read the comments
// there before changing anything here; this file keeps the arithmetic and
// points back at the argument.
//
// What the QML takes from Theme -- a font's pixel size, how many rules a dense
// grid aims for -- is a parameter here, because a library has no Theme.

#include "plotcore/Projection.hpp"

#include <optional>
#include <string>
#include <vector>

namespace plotcore {

/// One tick, written: the base in the axis's own face and the exponent raised
/// in a smaller one, and the whole of it as one string with the exponent in
/// superscript digits. `exponent` is empty on a linear axis.
struct TickLabel
{
    std::string base;
    std::string exponent;
    std::string text;

    [[nodiscard]] bool operator==(const TickLabel&) const = default;
};

/// What an axis is numbered and ticked at. `values` and `texts` are the
/// numbered ticks, `ticks` every tick whether numbered or not. `logarithmic`
/// is whether the logarithmic rule placed them -- false on a linear axis and on
/// a logarithmic one zoomed into its linear fallback, which is `fallback`.
struct TickSet
{
    std::vector<double> values;
    std::vector<TickLabel> texts;
    std::vector<double> ticks;
    bool logarithmic = false;
    bool fallback = false;
};

/// matplotlib's LogLocator, over one view: the majors and minors in view, the
/// stride the majors were taken at, and whether the minors are the linear
/// fallback.
struct LogLocated
{
    std::vector<double> majors;
    std::vector<double> minors;
    double stride = 1.0;
    bool linear = false;
};

/// The densities a grid can be ruled at. See gridValues().
enum class GridMode
{
    None,
    Loose,
    Dense,
    Custom,
};

/// One numbered subdivision: where it is, as a fraction of the axis, and the
/// digit it carries.
struct MinorNumber
{
    double at = 0.0;
    std::string text;
};

/// One rule: where it is, as a fraction of the axis, and whether it lands on a
/// numbered tick.
struct Rule
{
    double at = 0.0;
    bool major = true;
};

/// How the chrome around one axis is measured. The QML reads these off Theme.
struct TickStyle
{
    /// Roughly how many ticks a linear axis carries. PlotSurface's eight.
    int target = 8;
    /// The tick face's pixel size, which logTickRoom() sizes a label by.
    double fontPixels = 10.0;
    /// How many times finer a dense grid is than a loose one.
    int denseFactor = 4;
};

// --- where a value sits ---------------------------------------------------

/// Whether `base` is a base at all: finite and above one. A test, not a clamp.
[[nodiscard]] bool usableBase(double base);
// The logarithm to a base is Projection.hpp's logOf, which every axis here
// places a value through. A second definition beside it was the same function
// twice -- inline there and not here -- which GCC linked by picking one and
// MSVC refused.
/// Where `value` sits on an axis running `low`..`high`, as a fraction from
/// `low`; zero where there is no axis to place it on.
[[nodiscard]] double fractionOn(double value, double low, double high, bool logarithmic,
                                double base);

// --- how a number is spaced and written ----------------------------------

[[nodiscard]] double niceStep(double span, double target);
[[nodiscard]] int decimalsFor(double span);
[[nodiscard]] std::string labelFor(double value, double span);
[[nodiscard]] std::string trimmed(const std::string& text);
[[nodiscard]] std::string logLabelFor(double value);
[[nodiscard]] std::string axisLabel(double value, double low, double high, bool logarithmic);
[[nodiscard]] std::vector<double> ticksBetween(double low, double high, double step);

// --- a logarithmic axis: matplotlib's locator ----------------------------

[[nodiscard]] int logTickRoom(double extent, bool vertical, double fontPixels);
[[nodiscard]] bool nearInteger(double x);
[[nodiscard]] std::vector<double> logSubs(double base);
[[nodiscard]] LogLocated logLocate(double low, double high, double base, int request, int target);
[[nodiscard]] std::vector<double> logSublabels(double low, double high, double base);
[[nodiscard]] std::string baseText(double base);
[[nodiscard]] std::optional<TickLabel>
logLabel(double value, double base, const std::vector<double>& sublabels, int precision);
[[nodiscard]] std::vector<std::optional<TickLabel>>
logLabels(const std::vector<double>& values, double base, const std::vector<double>& sublabels);
[[nodiscard]] std::vector<double> factorsBetween(double low, double high, double factor);

// --- which ticks, which rules, which digits between them ------------------

/// The ticks of an axis running `low`..`high` over `extent` pixels of pane.
[[nodiscard]] TickSet tickValues(double low, double high, bool logarithmic, double base,
                                 double extent, bool vertical, const TickStyle& style = {});

/// Whether `value` is one of `values`, to within what two ways of computing one
/// number differ by.
[[nodiscard]] bool holds(const std::vector<double>& values, double value);

/// The values an axis is ruled at under `mode`, given the reader's `custom`
/// step and the axis's own ticks.
[[nodiscard]] std::vector<double> gridValues(GridMode mode, double low, double high, double custom,
                                             const TickSet& found, const TickStyle& style = {});

/// What a subdivision says: how many times `reference` it is.
[[nodiscard]] std::string minorLabel(double value, double reference);
/// The power of `base` at or below `value`.
[[nodiscard]] double powerBelow(double value, double base);

/// Which of the grid's rules carry a digit -- the viewer's own addition to
/// matplotlib's axis, off unless `enabled`. `gapPixels` is the room two labels
/// need whatever they say and `perChar` what each character adds.
[[nodiscard]] std::vector<MinorNumber> minorNumbersOf(bool enabled, GridMode mode,
                                                      const std::vector<double>& values,
                                                      const TickSet& found, double low, double high,
                                                      double base, double extent, double gapPixels,
                                                      double perChar);

/// One rule per value, with the ones on a numbered tick marked major, at their
/// fractions along an axis running `low`..`high`.
[[nodiscard]] std::vector<Rule> gridAt(GridMode mode, const std::vector<double>& values,
                                       const std::vector<double>& labels, double low, double high,
                                       bool logarithmic, double base);

} // namespace plotcore
