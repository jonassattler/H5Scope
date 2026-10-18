// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "plotcore/Palettes.hpp"

#include "plotcore/Format.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

// The values are Theme.qml's, token for token; the functions are Theme's and
// PlotSurface's of the same names.

namespace plotcore {
namespace {

// The neutral ramp, and the two signal colours paper has to answer for.
constexpr std::string_view kN0 = "#000000";
constexpr std::string_view kN4 = "#1D2124";
constexpr std::string_view kN5 = "#282D31";
constexpr std::string_view kN6 = "#3A4045";
constexpr std::string_view kN7 = "#5A6268";
constexpr std::string_view kN9 = "#B9C2C7";
constexpr std::string_view kN10 = "#E4E8EA";
constexpr std::string_view kN11 = "#FFFFFF";
constexpr std::string_view kSig500 = "#FFFFFF";
constexpr std::string_view kCyan500 = "#4DE1FF";
constexpr std::string_view kCyan900 = "#0B3A47";
constexpr std::string_view kLineLight = "#DFE3E5";

int hexDigit(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return 0;
}

double channel(std::string_view hex, std::size_t at)
{
    return (hexDigit(hex[at]) * 16 + hexDigit(hex[at + 1])) / 255.0;
}

/// Equal as an eight-bit colour, which is what Qt.colorEqual compares.
bool sameColour(const Rgba& a, const Rgba& b)
{
    const auto byte = [](double v) {
        return static_cast<int>(jsRound(std::clamp(v, 0.0, 1.0) * 255.0));
    };
    return byte(a.red) == byte(b.red) && byte(a.green) == byte(b.green) &&
           byte(a.blue) == byte(b.blue) && byte(a.alpha) == byte(b.alpha);
}

std::vector<Rgba> stopsOf(std::initializer_list<std::string_view> hexes)
{
    std::vector<Rgba> out;
    out.reserve(hexes.size());
    for (const std::string_view hex : hexes) {
        out.push_back(colourOf(hex));
    }
    return out;
}

} // namespace

Rgba colourOf(std::string_view hex)
{
    if (!hex.empty() && hex.front() == '#') {
        hex.remove_prefix(1);
    }
    if (hex.size() == 6) {
        return Rgba::of(channel(hex, 0), channel(hex, 2), channel(hex, 4));
    }
    if (hex.size() == 8) {
        return Rgba::of(channel(hex, 2), channel(hex, 4), channel(hex, 6), channel(hex, 0));
    }
    return {};
}

std::string hexOf(const Rgba& colour)
{
    const auto byte = [](double v) {
        return static_cast<unsigned>(jsRound(std::clamp(v, 0.0, 1.0) * 255.0));
    };
    char text[12];
    if (byte(colour.alpha) == 255U) {
        std::snprintf(text, sizeof(text), "#%02x%02x%02x", byte(colour.red), byte(colour.green),
                      byte(colour.blue));
    }
    else {
        std::snprintf(text, sizeof(text), "#%02x%02x%02x%02x", byte(colour.alpha), byte(colour.red),
                      byte(colour.green), byte(colour.blue));
    }
    return text;
}

const std::vector<std::string>& paletteNames()
{
    static const std::vector<std::string> names{"okabe-ito", "tol bright", "tol muted", "spectrum",
                                                "safe"};
    return names;
}

const std::vector<std::string>& rampNames()
{
    static const std::vector<std::string> names{"grayscale", "viridis", "inferno", "hot", "cool"};
    return names;
}

std::optional<std::vector<Rgba>> palette(std::string_view name, bool onDark)
{
    if (name == "okabe-ito") {
        return stopsOf({"#E69F00", "#56B4E9", "#009E73", "#F0E442", "#0072B2", "#D55E00", "#CC79A7",
                        onDark ? kN11 : kN0});
    }
    if (name == "tol bright") {
        return stopsOf(
            {"#4477AA", "#EE6677", "#228833", "#CCBB44", "#66CCEE", "#AA3377", "#BBBBBB"});
    }
    if (name == "tol muted") {
        return stopsOf({"#CC6677", "#332288", "#DDCC77", "#117733", "#88CCEE", "#882255", "#44AA99",
                        "#999933", "#AA4499"});
    }
    if (name == "spectrum") {
        if (onDark) {
            return stopsOf({"#B64EF5", "#F6330C", "#009700", "#0089C4", "#C99400",
                            "#FF9DBB", "#00CFC6", "#ACB5FF", "#64F44D", "#FFA379",
                            "#FF005E", "#F09AFF", "#6173FF", "#808700", "#F200AE",
                            "#009276", "#C76700", "#12C8FF", "#00B46A", "#B3C300"});
        }
        return stopsOf({"#9C33DB", "#D01F00", "#007C00", "#0089C3", "#6C4E00", "#7D003F", "#006A65",
                        "#003989", "#063A00", "#6F2500", "#D4004C", "#6C0081", "#085AFC", "#696F00",
                        "#C90090", "#009276", "#A55500", "#006684", "#006237", "#444B00"});
    }
    if (name == "safe") {
        if (onDark) {
            return stopsOf({"#0088CB", "#B37400", "#FFAE7F", "#CCB5FF", "#FB0080", "#00F3BD",
                            "#FF8798", "#00A98C", "#798DFF", "#FC43E7"});
        }
        return stopsOf({"#007BB8", "#945F00", "#602800", "#001AB4", "#D00069", "#00523E", "#930036",
                        "#008B73", "#0050C5", "#A20094"});
    }
    return std::nullopt;
}

std::optional<std::vector<Rgba>> ramp(std::string_view name)
{
    if (name == "grayscale") {
        return stopsOf({kN5, kN7, kN9, kN10, kN11});
    }
    if (name == "viridis") {
        return stopsOf({"#440154", "#3B528B", "#21918C", "#5EC962", "#FDE725"});
    }
    if (name == "inferno") {
        return stopsOf({"#000004", "#420A68", "#932667", "#DD513A", "#FCA50A", "#FCFFA4"});
    }
    if (name == "hot") {
        return stopsOf({"#0B0000", "#E60000", "#FF7A00", "#FFF34D", "#FFFFFF"});
    }
    if (name == "cool") {
        return stopsOf({"#00FFFF", "#FF00FF"});
    }
    return std::nullopt;
}

Rgba mix(const Rgba& from, const Rgba& to, double amount)
{
    const double t = std::max(0.0, std::min(1.0, amount));
    return Rgba::of(from.red + (to.red - from.red) * t, from.green + (to.green - from.green) * t,
                    from.blue + (to.blue - from.blue) * t,
                    from.alpha + (to.alpha - from.alpha) * t);
}

Rgba rampColor(const std::vector<Rgba>& stops, double position)
{
    if (stops.empty()) {
        return colourOf(kSig500);
    }
    if (stops.size() == 1) {
        return stops.front();
    }
    const double at =
        std::max(0.0, std::min(1.0, position)) * static_cast<double>(stops.size() - 1);
    const auto lower = static_cast<std::size_t>(std::floor(at));
    const std::size_t upper = std::min(lower + 1, stops.size() - 1);
    return mix(stops[lower], stops[upper], at - static_cast<double>(lower));
}

Rgba categoricalColor(const std::vector<Rgba>& stops, double position)
{
    if (stops.empty()) {
        return colourOf(kSig500);
    }
    const auto n = static_cast<long long>(stops.size());
    const auto at = static_cast<long long>(jsRound(position));
    return stops[static_cast<std::size_t>(((at % n) + n) % n)];
}

Rgba paperColor(const Rgba& colour)
{
    if (sameColour(colour, colourOf(kSig500))) {
        return colourOf(kN0);
    }
    if (sameColour(colour, colourOf(kCyan500))) {
        return colourOf(kCyan900);
    }
    return colour;
}

LineColours LineColours::defaults(bool onDark)
{
    LineColours colours;
    colours.single = colourOf(onDark ? kSig500 : kN0);
    colours.rangeFrom = colours.single;
    colours.rangeTo = colourOf(onDark ? kCyan500 : kCyan900);
    return colours;
}

Rgba seriesColor(const LineColours& colours, int series, int position, int count, bool paper,
                 bool onDark, const std::optional<Rgba>& own)
{
    if (own.has_value() && own->valid) {
        return paper ? paperColor(*own) : *own;
    }
    if (colours.mode == "same") {
        return paper ? paperColor(colours.single) : colours.single;
    }
    // A palette is asked which line this is, by `series` and not `position`,
    // so hiding one line does not hand another its colour.
    if (const std::optional<std::vector<Rgba>> stops =
            palette(colours.mode, paper ? false : onDark)) {
        const double at = colours.reversed ? static_cast<double>(stops->size()) - 1.0 - series
                                           : static_cast<double>(series);
        return categoricalColor(*stops, at);
    }
    double at = count > 0 ? (position + 1.0) / (count + 1.0) : 0.5;
    if (colours.reversed) {
        at = 1.0 - at;
    }
    at = colours.from + at * (colours.to - colours.from);
    if (colours.mode == "range") {
        return mix(paper ? paperColor(colours.rangeFrom) : colours.rangeFrom,
                   paper ? paperColor(colours.rangeTo) : colours.rangeTo, at);
    }
    const std::optional<std::vector<Rgba>> stops = ramp(colours.mode);
    return rampColor(stops.value_or(std::vector<Rgba>{}), at);
}

double seriesOpacity(int series, int count, int highlighted, bool bundle, bool onDark)
{
    const double bundled = onDark ? 0.55 : 0.95;
    if (highlighted >= 0) {
        return series == highlighted ? 1.0 : bundled / 2.0;
    }
    return bundle && count > 1 ? bundled : 1.0;
}

double seriesWidth(int series, int highlighted, double lineWidth)
{
    return series == highlighted ? lineWidth * 2.0 : lineWidth;
}

Chrome chromeFor(bool onDark, bool paper)
{
    if (paper) {
        return Chrome{colourOf(kN11), colourOf(kN0), colourOf(kLineLight),
                      colourOf(kN9),  colourOf(kN6), colourOf(kN0)};
    }
    if (onDark) {
        return Chrome{colourOf(kN0), colourOf(kN11), colourOf(kN4),
                      colourOf(kN6), colourOf(kN9),  colourOf(kSig500)};
    }
    return Chrome{colourOf(kN11), colourOf(kN0), colourOf(kLineLight),
                  colourOf(kN9),  colourOf(kN6), colourOf(kN0)};
}

} // namespace plotcore
