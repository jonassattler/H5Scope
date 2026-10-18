// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "plotcore/Format.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <string>
#include <system_error>
#include <tuple>
#include <vector>

namespace plotcore {
namespace {

/// A positive, finite double as its exact decimal digits: `digits` with no
/// leading or trailing zeros, the first of them at 10^`point`.
///
/// Exact, not rounded: every double is a finite binary fraction and so a finite
/// decimal one, and the ECMAScript algorithms are all "the integer closest to
/// the true value, the larger on a tie" -- which has to be decided against the
/// true value, because the tie is exactly where printf's rounding and
/// JavaScript's part company.
struct Exact
{
    std::string digits;
    int point = 0;
};

Exact exactOf(double value)
{
    // A double's binary fraction has 53 - exponent digits after the point, and
    // 2^-k is exactly k decimal digits, so that many places is the whole of it.
    int exponent = 0;
    std::frexp(value, &exponent);
    const int places = std::max(0, 53 - exponent);
    std::vector<char> buffer(static_cast<std::size_t>(places) + 400);
    const auto written = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value,
                                       std::chars_format::fixed, places);
    std::string fixed(buffer.data(), written.ptr);

    const std::size_t dot = fixed.find('.');
    const std::string whole = fixed.substr(0, dot);
    const std::string fraction = dot == std::string::npos ? std::string() : fixed.substr(dot + 1);

    Exact exact;
    if (whole != "0") {
        exact.digits = whole + fraction;
        exact.point = static_cast<int>(whole.size()) - 1;
    }
    else {
        const std::size_t first = fraction.find_first_not_of('0');
        exact.digits = fraction.substr(first);
        exact.point = -static_cast<int>(first) - 1;
    }
    const std::size_t last = exact.digits.find_last_not_of('0');
    exact.digits.resize(last + 1);
    return exact;
}

/// `exact` to `count` significant figures, a tie going up: the digits, and the
/// power of ten the first of them now sits at.
std::pair<std::string, int> significant(const Exact& exact, int count)
{
    std::string kept = exact.digits.substr(0, std::min<std::size_t>(exact.digits.size(), count));
    kept.resize(static_cast<std::size_t>(count), '0');
    int point = exact.point;
    if (exact.digits.size() > static_cast<std::size_t>(count) &&
        exact.digits[static_cast<std::size_t>(count)] >= '5') {
        int at = count - 1;
        while (at >= 0 && kept[static_cast<std::size_t>(at)] == '9') {
            kept[static_cast<std::size_t>(at)] = '0';
            --at;
        }
        if (at < 0) {
            kept.insert(kept.begin(), '1');
            kept.pop_back();
            ++point;
        }
        else {
            ++kept[static_cast<std::size_t>(at)];
        }
    }
    return {kept, point};
}

std::string exponentText(int exponent)
{
    return std::string(exponent < 0 ? "e-" : "e+") + std::to_string(std::abs(exponent));
}

} // namespace

std::string numberText(double value)
{
    if (std::isnan(value)) {
        return "NaN";
    }
    if (std::isinf(value)) {
        return value > 0 ? "Infinity" : "-Infinity";
    }
    if (value == 0.0) {
        return "0";
    }
    if (value < 0.0) {
        return "-" + numberText(-value);
    }
    // The shortest digits that read back as this value: to_chars' answer, and
    // the specification's.
    char buffer[64];
    const auto written =
        std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::scientific);
    const std::string text(buffer, written.ptr);
    const std::size_t e = text.find('e');
    std::string digits;
    for (std::size_t i = 0; i < e; ++i) {
        if (text[i] != '.') {
            digits.push_back(text[i]);
        }
    }
    const int k = static_cast<int>(digits.size());
    const int n = std::atoi(text.c_str() + e + 1) + 1;

    if (k <= n && n <= 21) {
        return digits + std::string(static_cast<std::size_t>(n - k), '0');
    }
    if (0 < n && n <= 21) {
        return digits.substr(0, static_cast<std::size_t>(n)) + "." +
               digits.substr(static_cast<std::size_t>(n));
    }
    if (-6 < n && n <= 0) {
        return "0." + std::string(static_cast<std::size_t>(-n), '0') + digits;
    }
    const std::string lead = k == 1 ? digits : digits.substr(0, 1) + "." + digits.substr(1);
    return lead + exponentText(n - 1);
}

std::string toFixed(double value, int digits)
{
    if (std::isnan(value)) {
        return "NaN";
    }
    if (value < 0.0) {
        return "-" + toFixed(-value, digits);
    }
    if (!(value < 1e21)) {
        return numberText(value);
    }
    std::string whole = "0";
    std::string fraction(static_cast<std::size_t>(digits), '0');
    if (value > 0.0) {
        const Exact exact = exactOf(value);
        // The exact digits laid out at their places, as many after the point
        // as asked for and one more to round by.
        std::string all;
        const int integerDigits = std::max(exact.point + 1, 1);
        for (int place = integerDigits - 1; place >= -(digits + 1); --place) {
            const int index = exact.point - place;
            all.push_back(index >= 0 && index < static_cast<int>(exact.digits.size())
                              ? exact.digits[static_cast<std::size_t>(index)]
                              : '0');
        }
        bool up = all.back() >= '5';
        all.pop_back();
        for (int at = static_cast<int>(all.size()) - 1; up && at >= 0; --at) {
            if (all[static_cast<std::size_t>(at)] == '9') {
                all[static_cast<std::size_t>(at)] = '0';
            }
            else {
                ++all[static_cast<std::size_t>(at)];
                up = false;
            }
        }
        if (up) {
            all.insert(all.begin(), '1');
        }
        const std::size_t split = all.size() - static_cast<std::size_t>(digits);
        whole = all.substr(0, split);
        fraction = all.substr(split);
        const std::size_t lead = whole.find_first_not_of('0');
        whole = lead == std::string::npos ? "0" : whole.substr(lead);
    }
    return digits == 0 ? whole : whole + "." + fraction;
}

std::string toExponential(double value, int digits)
{
    if (std::isnan(value)) {
        return "NaN";
    }
    if (value < 0.0) {
        return "-" + toExponential(-value, digits);
    }
    if (std::isinf(value)) {
        return "Infinity";
    }
    std::string mantissa(static_cast<std::size_t>(digits) + 1, '0');
    int exponent = 0;
    if (value > 0.0) {
        std::tie(mantissa, exponent) = significant(exactOf(value), digits + 1);
    }
    const std::string lead =
        digits == 0 ? mantissa : mantissa.substr(0, 1) + "." + mantissa.substr(1);
    return lead + exponentText(exponent);
}

std::string toPrecision(double value, int precision)
{
    if (std::isnan(value)) {
        return "NaN";
    }
    if (value < 0.0) {
        return "-" + toPrecision(-value, precision);
    }
    if (std::isinf(value)) {
        return "Infinity";
    }
    // QLocale's 'g', which is what V4 answers this with -- see Format.hpp. Its
    // digits come without their trailing zeros, its decimal point sits at
    // `decpt` (the first digit at 10^(decpt - 1)), and the two forms below are
    // QLocaleData::decimalForm and exponentForm under PMSignificantDigits.
    std::string digits = "0";
    int decpt = 1;
    if (value > 0.0) {
        int e = 0;
        std::tie(digits, e) = significant(exactOf(value), precision);
        const std::size_t last = digits.find_last_not_of('0');
        digits.resize(last == std::string::npos ? 1 : last + 1);
        decpt = e + 1;
    }
    const auto count = static_cast<std::size_t>(std::max(precision, 1));
    // POSIX's %g rule: fixed while -4 <= X < P, X being decpt - 1.
    if (decpt > -4 && decpt <= std::max(precision, 1)) {
        if (decpt < 0) {
            // The zeros between the point and the first digit, put in front --
            // and counted towards the precision below, which is the one way
            // this differs from the specification: 0.0005 at two figures is
            // "0.0005", where ECMAScript pads it to "0.00050".
            digits.insert(0, static_cast<std::size_t>(-decpt), '0');
            decpt = 0;
        }
        else if (digits.size() < static_cast<std::size_t>(decpt)) {
            digits.resize(static_cast<std::size_t>(decpt), '0');
        }
        if (digits.size() < count) {
            digits.resize(count, '0');
        }
        if (static_cast<std::size_t>(decpt) < digits.size()) {
            digits.insert(static_cast<std::size_t>(decpt), ".");
        }
        if (decpt == 0) {
            digits.insert(0, "0");
        }
        return digits;
    }
    if (digits.size() < count) {
        digits.resize(count, '0');
    }
    if (digits.size() > 1) {
        digits.insert(1, ".");
    }
    return digits + exponentText(decpt - 1);
}

double jsRound(double value)
{
    if (!std::isfinite(value)) {
        return value;
    }
    const double below = std::floor(value);
    return value - below >= 0.5 ? below + 1.0 : below;
}

int textLength(const std::string& utf8)
{
    int units = 0;
    for (std::size_t i = 0; i < utf8.size();) {
        const auto lead = static_cast<unsigned char>(utf8[i]);
        const int bytes = lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
        units += bytes == 4 ? 2 : 1;
        i += static_cast<std::size_t>(bytes);
    }
    return units;
}

} // namespace plotcore
