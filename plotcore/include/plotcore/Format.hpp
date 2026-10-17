// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// How a number is written, exactly the way the viewer's QML writes it -- which
// is ECMAScript's way except where Qt's engine departs from it (toPrecision).
//
// The ticks and readouts of the viewer are written by JavaScript, and the
// libraries built on plotcore have to write the same numbers: a figure taken
// from the application and one drawn from Python should carry the same labels
// at the same places, and a parity test is how that is held rather than
// claimed. JavaScript's own formatting is not C's. toFixed() and friends round
// a tie *up*, where printf rounds it to even; an exponent is written "e+7",
// not "e+07"; and the shortest form of a number switches to an exponent at
// 1e21 and below 1e-6, which no printf conversion does. So these are the
// ECMAScript algorithms, written out, over the exact value of the double.

#include <string>

namespace plotcore {

/// `Number.prototype.toFixed(digits)`. A magnitude of 1e21 or more is written
/// as numberText() writes it, as the specification says.
[[nodiscard]] std::string toFixed(double value, int digits);

/// `Number.prototype.toExponential(digits)`.
[[nodiscard]] std::string toExponential(double value, int digits);

/// `Number.prototype.toPrecision(precision)`, as Qt's JavaScript engine
/// answers it.
///
/// Which is not the specification's answer, and the difference is the one
/// place this file follows the engine rather than the standard: V4 writes
/// toPrecision through QLocale's 'g' format, which turns to an exponent once
/// the power falls below -4 -- C's %g rule -- where ECMAScript keeps fixed
/// notation down to -6, and which pads a number to the precision counting the
/// zeros in front of its first digit. So (0.000001234).toPrecision(1) is
/// "1e-6" in the viewer and "0.000001" in a browser, and (0.0005).toPrecision(2)
/// is "0.0005" and "0.00050". The viewer's is the one a label has to match.
/// Everything else here is both at once.
[[nodiscard]] std::string toPrecision(double value, int precision);

/// `String(value)`: the shortest digits that read back as this double.
[[nodiscard]] std::string numberText(double value);

/// `Math.round`: halves go up, towards positive infinity, where std::round
/// takes them away from zero -- so -2.5 is -2, not -3.
[[nodiscard]] double jsRound(double value);

/// What a string is written in, counted in UTF-16 code units: what `.length`
/// answers in JavaScript, which the labels' spacing is computed from.
[[nodiscard]] int textLength(const std::string& utf8);

} // namespace plotcore
