// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "plotcore/Ticks.hpp"

#include "plotcore/Format.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <numbers>

// Every function here is the one of the same name in src/qml/PlotFrame.qml,
// with JavaScript's arithmetic spelt out where C++'s differs: Math.round is
// jsRound, `%` on numbers is fmod, `.length` of a label is textLength, and a
// number is written by Format.hpp rather than by printf. The arguments for
// each rule are written over the QML; they are not repeated here.

namespace plotcore {
namespace {

constexpr double kInfinity = std::numeric_limits<double>::infinity();

/// `Number(text)`, for the strings toPrecision() and toFixed() write.
double numberOf(const std::string& text)
{
    double value = std::numeric_limits<double>::quiet_NaN();
    const char* first = text.data();
    std::from_chars(first, first + text.size(), value);
    return value;
}

/// `Number.isInteger`.
bool isInteger(double x)
{
    return std::isfinite(x) && std::floor(x) == x;
}

} // namespace

bool usableBase(double base)
{
    return std::isfinite(base) && base > 1.0;
}


double fractionOn(double value, double low, double high, bool logarithmic, double base)
{
    if (logarithmic && (!(low > 0.0) || !(high > 0.0) || !usableBase(base))) {
        return 0.0;
    }
    const double from = logarithmic ? logOf(low, base) : low;
    const double span = (logarithmic ? logOf(high, base) : high) - from;
    if (!std::isfinite(span) || !(span > 0.0)) {
        return 0.0;
    }
    return ((logarithmic ? logOf(value, base) : value) - from) / span;
}

double niceStep(double span, double target)
{
    if (!(span > 0.0)) {
        return 0.0;
    }
    const double raw = span / std::max(1.0, target);
    const double magnitude = std::pow(10.0, std::floor(std::log(raw) / std::numbers::ln10));
    const double scaled = raw / magnitude;
    return magnitude * (scaled <= 1.0 ? 1.0 : scaled <= 2.0 ? 2.0 : scaled <= 5.0 ? 5.0 : 10.0);
}

int decimalsFor(double span)
{
    const double width = std::abs(span);
    if (!(width > 0.0)) {
        return 0;
    }
    const double wanted = std::ceil(-std::log(width) / std::numbers::ln10) + 2.0;
    return static_cast<int>(std::max(0.0, std::min(6.0, wanted)));
}

std::string labelFor(double value, double span)
{
    const double width = std::abs(span);
    if (width >= 1e6 || (width > 0.0 && width < 1e-4)) {
        if (value == 0.0) {
            return "0";
        }
        const double figures = std::ceil(std::log10(std::abs(value) / width)) + 2.0;
        return toExponential(value, static_cast<int>(std::max(1.0, std::min(15.0, figures))));
    }
    return toFixed(value, decimalsFor(span));
}

std::string trimmed(const std::string& text)
{
    const std::size_t at = text.find('e');
    std::string mantissa = at == std::string::npos ? text : text.substr(0, at);
    if (mantissa.find('.') == std::string::npos) {
        return text;
    }
    // `.replace(/\.?0+$/, "")`: a run of trailing zeros, and the point before
    // it if that is what they leave at the end.
    const std::size_t kept = mantissa.find_last_not_of('0');
    if (kept + 1 < mantissa.size()) {
        mantissa.resize(kept + 1);
        if (!mantissa.empty() && mantissa.back() == '.') {
            mantissa.pop_back();
        }
    }
    return mantissa + (at == std::string::npos ? std::string() : text.substr(at));
}

std::string logLabelFor(double value)
{
    if (!(value > 0.0) || !std::isfinite(value)) {
        return numberText(value);
    }
    const double exponent = std::floor(std::log10(value));
    if (exponent >= 6.0 || exponent < -4.0) {
        return trimmed(toExponential(value, 2));
    }
    return trimmed(toFixed(value, static_cast<int>(std::max(0.0, std::min(8.0, 2.0 - exponent)))));
}

std::string axisLabel(double value, double low, double high, bool logarithmic)
{
    return logarithmic ? logLabelFor(value) : labelFor(value, high - low);
}

std::vector<double> ticksBetween(double low, double high, double step)
{
    std::vector<double> found;
    if (!(step > 0.0) || !(high > low)) {
        return found;
    }
    const double first = std::ceil(low / step - 1e-9);
    for (int i = 0; i < 512; ++i) {
        const double value = (first + i) * step;
        if (value > high + step * 1e-9) {
            break;
        }
        found.push_back(value);
    }
    return found;
}

int logTickRoom(double extent, bool vertical, double fontPixels)
{
    const double per = (vertical ? 2.0 : 3.0) * fontPixels;
    const double room = std::floor(std::max(0.0, extent) / per);
    return static_cast<int>(std::max(2.0, std::min(9.0, std::isnan(room) ? 9.0 : room)));
}

bool nearInteger(double x)
{
    return std::abs(x - jsRound(x)) < 1e-10;
}

std::vector<double> logSubs(double base)
{
    std::vector<double> subs;
    if (base < 3.0) {
        return subs;
    }
    for (double m = 2.0; m < base && subs.size() < 64; ++m) {
        subs.push_back(m);
    }
    return subs;
}

LogLocated logLocate(double low, double high, double base, int request, int target)
{
    if (!(low > 0.0) || !(high > low) || !usableBase(base)) {
        return {};
    }
    const double efmin = logOf(low, base);
    const double efmax = logOf(high, base);
    const double emin = std::ceil(efmin - 1e-10);
    const double emax = std::floor(efmax + 1e-10);
    const double avail = emax - emin + 1.0;

    double wanted = std::max(2.0, static_cast<double>(request));
    double stride = std::floor(avail / (wanted + 1.0)) + 1.0;
    const double got = std::ceil(avail / stride);
    if (got <= wanted) {
        wanted = got;
    }
    std::vector<double> decades;
    if (wanted <= 0.0) {
        decades = {emin - 1.0, emax + 1.0};
        stride = decades[1] - decades[0];
    }
    else if (wanted == 1.0) {
        const double mid = jsRound((efmin + efmax) / 2.0);
        stride = std::max(mid - (emin - 1.0), (emax + 1.0) - mid);
        decades = {mid - stride, mid, mid + stride};
    }
    else {
        stride = std::floor((avail - 1.0) / (wanted - 1.0));
        if (stride < avail / wanted) {
            stride = std::floor(avail / wanted);
        }
        const double olo = std::max(avail - stride * wanted, 0.0);
        const double ohi = std::min(avail - stride * (wanted - 1.0), stride);
        double offset = std::fmod(std::fmod(-emin, stride) + stride, stride);
        if (!(olo <= offset && offset < ohi)) {
            offset = olo;
        }
        for (double e = emin + offset - stride; e <= emax + stride && decades.size() < 1024;
             e += stride) {
            decades.push_back(e);
        }
    }

    const auto inView = [&](double v) {
        return v >= low * (1.0 - 1e-12) && v <= high * (1.0 + 1e-12);
    };
    LogLocated located;
    located.stride = stride;
    for (const double decade : decades) {
        const double v = std::pow(base, decade);
        if (inView(v)) {
            located.majors.push_back(v);
        }
    }

    const std::vector<double> subs = avail >= 10.0 ? std::vector<double>{} : logSubs(base);
    if (!subs.empty() && (stride == 1.0 || avail <= 1.0)) {
        for (double e = emin - 1.0; e <= emax && located.minors.size() < 4096; ++e) {
            const double power = std::pow(base, e);
            for (const double sub : subs) {
                const double v = sub * power;
                if (inView(v)) {
                    located.minors.push_back(v);
                }
            }
        }
    }

    if (stride == 1.0 && located.majors.size() + located.minors.size() <= 1) {
        const double step = niceStep(high - low, target);
        std::vector<double> kept;
        for (const double v : ticksBetween(low, high, step)) {
            const bool taken = std::any_of(located.majors.begin(), located.majors.end(),
                                           [&](double m) { return std::abs(m - v) < step / 2.0; });
            if (!taken) {
                kept.push_back(v);
            }
        }
        located.minors = kept;
        located.linear = true;
    }
    return located;
}

std::vector<double> logSublabels(double low, double high, double base)
{
    if (!(low > 0.0) || !(high > low) || !usableBase(base)) {
        return {1.0};
    }
    const double lmin = std::log(low) / std::log(base);
    const double lmax = std::log(high) / std::log(base);
    const double below = isInteger(lmin) ? lmin - 1.0 : std::floor(lmin);
    const double crossed = std::floor(lmax) - below;
    if (crossed > 1.0) {
        return {1.0};
    }
    std::vector<double> found;
    if (lmax - lmin > 0.4) {
        const double count = std::floor(std::floor(base) / 2.0) + 1.0;
        for (double i = 0.0; i < count; ++i) {
            const double c = jsRound(std::pow(base, count > 1.0 ? i / (count - 1.0) : 0.0));
            if (std::find(found.begin(), found.end(), c) == found.end()) {
                found.push_back(c);
            }
        }
        return found;
    }
    for (double c = 1.0; c <= base; ++c) {
        found.push_back(c);
    }
    return found;
}

std::string baseText(double base)
{
    if (std::abs(base - std::numbers::e) < 1e-12) {
        return "e";
    }
    return numberText(base);
}

std::optional<TickLabel> logLabel(double value, double base, const std::vector<double>& sublabels,
                                  int precision)
{
    const double fx = std::log(value) / std::log(base);
    const bool decade = nearInteger(fx);
    const double exponent = decade ? jsRound(fx) : std::floor(fx);
    const double coeff = jsRound(std::pow(base, fx - exponent));
    if (std::find(sublabels.begin(), sublabels.end(), coeff) == sublabels.end()) {
        return std::nullopt;
    }
    const std::string power = baseText(base);
    const std::string sign = exponent < 0.0 ? "−" : "";
    const std::string digits = numberText(std::abs(exponent));
    std::string lead;
    if (!decade) {
        double c = value / std::pow(base, exponent);
        if (nearInteger(c)) {
            c = jsRound(c);
        }
        lead = numberText(numberOf(toPrecision(c, precision))) + "×";
    }
    static const char* const kSuperscripts[] = {"⁰", "¹", "²", "³", "⁴", "⁵", "⁶", "⁷", "⁸", "⁹"};
    std::string raised = exponent < 0.0 ? "⁻" : "";
    for (const char digit : digits) {
        if (digit >= '0' && digit <= '9') {
            raised += kSuperscripts[digit - '0'];
        }
    }
    return TickLabel{lead + power, sign + digits, lead + power + raised};
}

std::vector<std::optional<TickLabel>> logLabels(const std::vector<double>& values, double base,
                                                const std::vector<double>& sublabels)
{
    std::vector<std::optional<TickLabel>> out;
    for (int precision = 6; precision <= 15; ++precision) {
        out.clear();
        for (const double v : values) {
            out.push_back(logLabel(v, base, sublabels, precision));
        }
        std::vector<std::string> seen;
        bool clash = false;
        for (const std::optional<TickLabel>& label : out) {
            if (!label.has_value()) {
                continue;
            }
            if (std::find(seen.begin(), seen.end(), label->text) != seen.end()) {
                clash = true;
                break;
            }
            seen.push_back(label->text);
        }
        if (!clash) {
            break;
        }
    }
    return out;
}

std::vector<double> factorsBetween(double low, double high, double factor)
{
    std::vector<double> found;
    if (!(low > 0.0) || !(high > low) || !(factor > 1.0)) {
        return found;
    }
    const double step = std::log10(factor);
    const double to = std::log10(high);
    const double first = std::ceil(std::log10(low) / step - 1e-9);
    for (int i = 0; i < 512; ++i) {
        const double exponent = (first + i) * step;
        if (exponent > to + 1e-9) {
            break;
        }
        found.push_back(std::pow(10.0, exponent));
    }
    return found;
}

TickSet tickValues(double low, double high, bool logarithmic, double base, double extent,
                   bool vertical, const TickStyle& style)
{
    TickSet set;
    if (logarithmic) {
        const LogLocated located = logLocate(
            low, high, base, logTickRoom(extent, vertical, style.fontPixels), style.target);
        std::vector<double> all = located.majors;
        all.insert(all.end(), located.minors.begin(), located.minors.end());
        std::sort(all.begin(), all.end());
        const std::vector<std::optional<TickLabel>> labels =
            logLabels(all, base, logSublabels(low, high, base));
        for (std::size_t i = 0; i < all.size(); ++i) {
            if (labels[i].has_value()) {
                set.values.push_back(all[i]);
                set.texts.push_back(*labels[i]);
            }
        }
        set.ticks = all;
        set.logarithmic = !located.linear;
        set.fallback = located.linear;
        return set;
    }
    set.values = ticksBetween(low, high, niceStep(high - low, style.target));
    for (const double v : set.values) {
        const std::string text = labelFor(v, high - low);
        set.texts.push_back(TickLabel{text, "", text});
    }
    set.ticks = set.values;
    return set;
}

bool holds(const std::vector<double>& values, double value)
{
    for (const double v : values) {
        const double scale = std::max(std::abs(v), std::abs(value));
        if (std::abs(v - value) <= scale * 1e-9) {
            return true;
        }
    }
    return false;
}

std::vector<double> gridValues(GridMode mode, double low, double high, double custom,
                               const TickSet& found, const TickStyle& style)
{
    if (mode == GridMode::None) {
        return {};
    }
    if (mode == GridMode::Custom) {
        return found.logarithmic ? factorsBetween(low, high, custom)
                                 : ticksBetween(low, high, custom > 0.0 ? custom : 0.0);
    }
    if (!found.logarithmic) {
        const int target =
            mode == GridMode::Dense ? style.target * style.denseFactor : style.target;
        std::vector<double> linear = ticksBetween(low, high, niceStep(high - low, target));
        if (!found.fallback) {
            return linear;
        }
        std::vector<double> ruled = linear;
        for (const double v : found.ticks) {
            if (!holds(linear, v)) {
                ruled.push_back(v);
            }
        }
        std::sort(ruled.begin(), ruled.end());
        return ruled;
    }
    return mode == GridMode::Dense ? found.ticks : found.values;
}

std::string minorLabel(double value, double reference)
{
    if (!(reference > 0.0)) {
        return "";
    }
    const double mantissa = numberOf(toPrecision(value / reference, 6));
    return isInteger(mantissa) ? numberText(mantissa) : numberText(numberOf(toFixed(mantissa, 3)));
}

double powerBelow(double value, double base)
{
    return std::pow(base, std::floor(logOf(value, base) + 1e-9));
}

std::vector<MinorNumber> minorNumbersOf(bool enabled, GridMode mode,
                                        const std::vector<double>& values, const TickSet& found,
                                        double low, double high, double base, double extent,
                                        double gapPixels, double perChar)
{
    std::vector<MinorNumber> out;
    if (!enabled || !found.logarithmic || !(extent > 0.0)) {
        return out;
    }
    if (mode == GridMode::Custom) {
        return out;
    }
    const std::vector<double>& labels = found.values;
    if (labels.empty() || values.size() <= labels.size()) {
        return out;
    }
    const bool multiples = std::any_of(labels.begin(), labels.end(),
                                       [&](double v) { return !nearInteger(logOf(v, base)); });
    if (multiples) {
        return out;
    }

    const std::size_t n = values.size();
    std::vector<double> spots(n);
    std::vector<std::string> texts(n);
    std::vector<bool> numbered(n);
    for (std::size_t i = 0; i < n; ++i) {
        int at = -1;
        for (std::size_t j = 0; j < labels.size() && at < 0; ++j) {
            if (holds({labels[j]}, values[i])) {
                at = static_cast<int>(j);
            }
        }
        spots[i] = fractionOn(values[i], low, high, true, base) * extent;
        texts[i] = at >= 0 ? found.texts[static_cast<std::size_t>(at)].text
                           : minorLabel(values[i], powerBelow(values[i], base));
        numbered[i] = at >= 0;
    }

    int mostMarks = 0;
    int markRun = 0;
    for (std::size_t i = 0; i < n; ++i) {
        markRun = numbered[i] ? 0 : markRun + 1;
        mostMarks = std::max(mostMarks, markRun);
    }
    if (mostMarks < 2) {
        return out;
    }

    std::vector<double> above(n, kInfinity);
    std::vector<std::string> aboveText(n);
    for (std::size_t k = n; k-- > 0;) {
        if (numbered[k]) {
            above[k] = spots[k];
            aboveText[k] = texts[k];
        }
        else if (k + 1 < n) {
            above[k] = above[k + 1];
            aboveText[k] = aboveText[k + 1];
        }
    }

    const auto roomFor = [&](const std::string& one, const std::string& other) {
        return gapPixels + perChar * (textLength(one) + textLength(other)) / 2.0;
    };

    double lastAt = -kInfinity;
    std::string lastText;
    for (std::size_t i = 0; i < n; ++i) {
        if (numbered[i]) {
            lastAt = spots[i];
            lastText = texts[i];
            continue;
        }
        if (texts[i].empty()) {
            continue;
        }
        if (spots[i] - lastAt < roomFor(lastText, texts[i])) {
            continue;
        }
        if (above[i] - spots[i] < roomFor(aboveText[i], texts[i])) {
            continue;
        }
        out.push_back(MinorNumber{spots[i] / extent, texts[i]});
        lastAt = spots[i];
        lastText = texts[i];
    }
    return out;
}

std::vector<Rule> gridAt(GridMode mode, const std::vector<double>& values,
                         const std::vector<double>& labels, double low, double high,
                         bool logarithmic, double base)
{
    std::vector<Rule> out;
    const bool demote =
        mode != GridMode::Custom && !labels.empty() && values.size() > labels.size();
    for (const double v : values) {
        out.push_back(
            Rule{fractionOn(v, low, high, logarithmic, base), demote ? holds(labels, v) : true});
    }
    return out;
}

} // namespace plotcore
