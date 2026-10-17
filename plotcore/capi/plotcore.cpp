// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-FileCopyrightText: 2026 Andreas Martin
// SPDX-License-Identifier: GPL-3.0-only

// plotcore.h, over plotcore::Figure. Every entry point catches: an exception
// that crossed into C, Python or Rust would end the process rather than fail
// the call.

#define PLOTCORE_C_BUILDING
#include "plotcore.h"

#include "plotcore/Figure.hpp"
#include "plotcore/Palettes.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <vector>

using namespace plotcore;

struct plotcore_figure
{
    Figure figure;
};

struct plotcore_scene
{
    Scene scene;
    std::vector<plotcore_segment> rules;
    std::vector<plotcore_segment> strokes;
    /// Each drawn line's runs, triangles and markers as x, y pairs.
    struct Line
    {
        std::vector<std::vector<double>> runs;
        std::vector<double> triangles;
        std::vector<double> markers;
    };
    std::vector<Line> lines;
};

namespace {

thread_local std::string lastError;

plotcore_status fail(plotcore_status status, std::string why)
{
    lastError = std::move(why);
    return status;
}

/// Run `body`, which returns a status, with every exception turned into one.
template<typename Body>
plotcore_status guarded(Body&& body)
{
    lastError.clear();
    try {
        return body();
    }
    catch (const std::bad_alloc&) {
        return fail(PLOTCORE_ERROR_INTERNAL, "out of memory");
    }
    catch (const std::exception& error) {
        return fail(PLOTCORE_ERROR_INTERNAL, error.what());
    }
    catch (...) {
        return fail(PLOTCORE_ERROR_INTERNAL, "unknown failure");
    }
}

plotcore_rgba rgbaOf(const Rgba& colour)
{
    return plotcore_rgba{colour.red, colour.green, colour.blue, colour.alpha};
}

Rgba colourOf(const plotcore_rgba& colour)
{
    return Rgba::of(colour.red, colour.green, colour.blue, colour.alpha);
}

plotcore_segment segmentOf(const Segment& segment)
{
    return plotcore_segment{segment.from.x(), segment.from.y(),       segment.to.x(),
                            segment.to.y(),   rgbaOf(segment.colour), segment.width};
}

plotcore_rect rectOf(const PaneRect& rect)
{
    return plotcore_rect{rect.x, rect.y, rect.width, rect.height};
}

Modifiers modifiersOf(int32_t bits)
{
    return Modifiers{(bits & PLOTCORE_SHIFT) != 0, (bits & PLOTCORE_CONTROL) != 0,
                     (bits & PLOTCORE_ALT) != 0};
}

Button buttonOf(plotcore_button button)
{
    switch (button) {
    case PLOTCORE_BUTTON_MIDDLE:
        return Button::Middle;
    case PLOTCORE_BUTTON_RIGHT:
        return Button::Right;
    case PLOTCORE_BUTTON_LEFT:
        break;
    }
    return Button::Left;
}

bool hasLine(const plotcore_figure* figure, int32_t line)
{
    return line >= 0 && line < figure->figure.plot().lineCount();
}

plotcore_status noLine(int32_t line)
{
    return fail(PLOTCORE_ERROR_RANGE, "no line " + std::to_string(line));
}

plotcore_status noSetting(const char* name)
{
    return fail(PLOTCORE_ERROR_ARGUMENT,
                std::string("no such setting: ") + (name != nullptr ? name : "(null)"));
}

plotcore_status copyText(const std::string& text, char* buffer, int64_t capacity, int64_t* length)
{
    if (length != nullptr) {
        *length = static_cast<int64_t>(text.size());
    }
    if (buffer != nullptr && capacity > 0) {
        const auto count =
            std::min<std::size_t>(text.size(), static_cast<std::size_t>(capacity - 1));
        std::memcpy(buffer, text.data(), count);
        buffer[count] = '\0';
    }
    return PLOTCORE_OK;
}

// --- the names a setting goes by ------------------------------------------------

template<typename Member>
struct Field
{
    const char* name;
    Member member;
};

const Field<bool FigureSettings::*> kBools[] = {
    {"xLog", &FigureSettings::xLog},       {"yLog", &FigureSettings::yLog},
    {"flipped", &FigureSettings::flipped}, {"minorNumbers", &FigureSettings::minorNumbers},
    {"markers", &FigureSettings::markers}, {"cursor", &FigureSettings::cursor},
    {"legend", &FigureSettings::legend},   {"bundle", &FigureSettings::bundle},
    {"dark", &FigureSettings::dark},       {"paper", &FigureSettings::paper},
    {"stacked", &FigureSettings::stacked},
};

const Field<double FigureSettings::*> kNumbers[] = {
    {"gridStepX", &FigureSettings::gridStepX},
    {"gridStepY", &FigureSettings::gridStepY},
    {"xLogBase", &FigureSettings::xLogBase},
    {"yLogBase", &FigureSettings::yLogBase},
};

const Field<std::string FigureSettings::*> kTexts[] = {
    {"title", &FigureSettings::title},
    {"xLabel", &FigureSettings::xLabel},
    {"yLabel", &FigureSettings::yLabel},
};

const Field<Rgba LineColours::*> kColours[] = {
    {"colours.single", &LineColours::single},
    {"colours.rangeFrom", &LineColours::rangeFrom},
    {"colours.rangeTo", &LineColours::rangeTo},
};

const Field<double FigureStyle::*> kStyle[] = {
    {"tickPixels", &FigureStyle::tickPixels},
    {"tickMinorPixels", &FigureStyle::tickMinorPixels},
    {"titlePixels", &FigureStyle::titlePixels},
    {"axisNamePixels", &FigureStyle::axisNamePixels},
    {"legendPixels", &FigureStyle::legendPixels},
    {"gapXS", &FigureStyle::gapXS},
    {"gapS", &FigureStyle::gapS},
    {"gapM", &FigureStyle::gapM},
    {"gapL", &FigureStyle::gapL},
    {"s3", &FigureStyle::s3},
    {"s9", &FigureStyle::s9},
    {"labelMargin", &FigureStyle::labelMargin},
    {"accentWidth", &FigureStyle::accentWidth},
    {"ruleWidth", &FigureStyle::ruleWidth},
    {"tickLength", &FigureStyle::tickLength},
    {"exponentRaise", &FigureStyle::exponentRaise},
    {"lineWidth", &FigureStyle::lineWidth},
    {"markerSize", &FigureStyle::markerSize},
};

template<typename Member, std::size_t N>
const Field<Member>* find(const Field<Member> (&table)[N], const char* name)
{
    if (name == nullptr) {
        return nullptr;
    }
    const std::string_view wanted(name);
    for (const Field<Member>& field : table) {
        if (wanted == field.name) {
            return &field;
        }
    }
    return nullptr;
}

const char* const kGrids[] = {"none", "loose", "dense", "custom"};
const char* const kCorners[] = {"top-left", "top-right", "bottom-left", "bottom-right"};

bool gridOf(std::string_view name, GridMode& mode)
{
    const GridMode modes[] = {GridMode::None, GridMode::Loose, GridMode::Dense, GridMode::Custom};
    for (std::size_t i = 0; i < 4; ++i) {
        if (name == kGrids[i]) {
            mode = modes[i];
            return true;
        }
    }
    return false;
}

const char* gridName(GridMode mode)
{
    switch (mode) {
    case GridMode::None:
        return kGrids[0];
    case GridMode::Dense:
        return kGrids[2];
    case GridMode::Custom:
        return kGrids[3];
    case GridMode::Loose:
        break;
    }
    return kGrids[1];
}

bool cornerOf(std::string_view name, Corner& corner)
{
    const Corner corners[] = {Corner::TopLeft, Corner::TopRight, Corner::BottomLeft,
                              Corner::BottomRight};
    for (std::size_t i = 0; i < 4; ++i) {
        if (name == kCorners[i]) {
            corner = corners[i];
            return true;
        }
    }
    return false;
}

const char* cornerName(Corner corner)
{
    return kCorners[static_cast<int>(corner)];
}

bool isColourMode(std::string_view mode)
{
    if (mode == "same" || mode == "range") {
        return true;
    }
    const auto& palettes = paletteNames();
    const auto& ramps = rampNames();
    return std::find(palettes.begin(), palettes.end(), mode) != palettes.end() ||
           std::find(ramps.begin(), ramps.end(), mode) != ramps.end();
}

} // namespace

// --- the library ------------------------------------------------------------------------

int32_t plotcore_abi_version(void)
{
    return PLOTCORE_ABI_VERSION;
}

const char* plotcore_version(void)
{
    return PLOTCORE_VERSION_STRING;
}

const char* plotcore_last_error(void)
{
    return lastError.c_str();
}

int32_t plotcore_palette_count(void)
{
    return static_cast<int32_t>(paletteNames().size());
}

const char* plotcore_palette_name(int32_t index)
{
    const auto& names = paletteNames();
    return index >= 0 && static_cast<std::size_t>(index) < names.size()
               ? names[static_cast<std::size_t>(index)].c_str()
               : nullptr;
}

int32_t plotcore_ramp_count(void)
{
    return static_cast<int32_t>(rampNames().size());
}

const char* plotcore_ramp_name(int32_t index)
{
    const auto& names = rampNames();
    return index >= 0 && static_cast<std::size_t>(index) < names.size()
               ? names[static_cast<std::size_t>(index)].c_str()
               : nullptr;
}

int32_t plotcore_palette(const char* name, int32_t on_dark, plotcore_rgba* out, int32_t capacity)
{
    if (name == nullptr) {
        return -1;
    }
    std::optional<std::vector<Rgba>> stops = palette(name, on_dark != 0);
    if (!stops.has_value()) {
        stops = ramp(name);
    }
    if (!stops.has_value()) {
        return -1;
    }
    for (int32_t i = 0;
         out != nullptr && i < capacity && static_cast<std::size_t>(i) < stops->size(); ++i) {
        out[i] = rgbaOf((*stops)[static_cast<std::size_t>(i)]);
    }
    return static_cast<int32_t>(stops->size());
}

// --- a figure ---------------------------------------------------------------------------

plotcore_figure* plotcore_figure_new(void)
{
    try {
        return new plotcore_figure;
    }
    catch (...) {
        lastError = "out of memory";
        return nullptr;
    }
}

void plotcore_figure_free(plotcore_figure* figure)
{
    delete figure;
}

// Every call that returns a status opens with this, which is what lets the
// last error be empty after one that did not fail.
#define PLOTCORE_NEED(figure)                                                                      \
    lastError.clear();                                                                             \
    if ((figure) == nullptr) {                                                                     \
        return fail(PLOTCORE_ERROR_ARGUMENT, "no figure");                                         \
    }

#define PLOTCORE_NEED_LINE(figure, line)                                                           \
    PLOTCORE_NEED(figure)                                                                          \
    if (!hasLine((figure), (line))) {                                                              \
        return noLine(line);                                                                       \
    }

// --- lines ----------------------------------------------------------------------------------

plotcore_status plotcore_add_line(plotcore_figure* figure, const double* values, int64_t count,
                                  const char* name, int32_t* line)
{
    PLOTCORE_NEED(figure)
    if (count < 0 || (values == nullptr && count > 0)) {
        return fail(PLOTCORE_ERROR_ARGUMENT, "no values");
    }
    return guarded([&] {
        const int index = figure->figure.plot().addLine(values, count, name != nullptr ? name : "");
        if (line != nullptr) {
            *line = index;
        }
        return PLOTCORE_OK;
    });
}

plotcore_status plotcore_begin_line(plotcore_figure* figure, int64_t count, const char* name,
                                    int32_t* line)
{
    PLOTCORE_NEED(figure)
    if (count < 0) {
        return fail(PLOTCORE_ERROR_ARGUMENT, "a line cannot be shorter than nothing");
    }
    return guarded([&] {
        const int index = figure->figure.plot().beginLine(count, name != nullptr ? name : "");
        if (line != nullptr) {
            *line = index;
        }
        return PLOTCORE_OK;
    });
}

plotcore_status plotcore_append_line(plotcore_figure* figure, int32_t line, const double* values,
                                     int64_t count)
{
    PLOTCORE_NEED_LINE(figure, line)
    if (count < 0 || (values == nullptr && count > 0)) {
        return fail(PLOTCORE_ERROR_ARGUMENT, "no values");
    }
    if (figure->figure.plot().ready(line)) {
        return fail(PLOTCORE_ERROR_STATE, "the line is finished");
    }
    return guarded([&] {
        figure->figure.plot().appendToLine(line, values, count);
        return PLOTCORE_OK;
    });
}

plotcore_status plotcore_finish_line(plotcore_figure* figure, int32_t line)
{
    PLOTCORE_NEED_LINE(figure, line)
    if (figure->figure.plot().ready(line)) {
        return fail(PLOTCORE_ERROR_STATE, "the line is finished");
    }
    return guarded([&] {
        figure->figure.plot().finishLine(line);
        return PLOTCORE_OK;
    });
}

plotcore_status plotcore_set_line_reader(plotcore_figure* figure, int32_t line,
                                         plotcore_reader reader, void* context,
                                         plotcore_free_context release)
{
    PLOTCORE_NEED_LINE(figure, line)
    return guarded([&] {
        if (reader == nullptr) {
            if (release != nullptr) {
                release(context);
            }
            figure->figure.plot().setLineReader(line, {});
            return PLOTCORE_OK;
        }
        // Held by a shared pointer so the context is let go exactly once,
        // however many copies of the function the plot makes.
        std::shared_ptr<void> held(context, [release](void* c) {
            if (release != nullptr) {
                release(c);
            }
        });
        figure->figure.plot().setLineReader(
            line, [reader, held](long long first, long long count, double* out) {
                return reader(held.get(), first, count, out) != 0;
            });
        return PLOTCORE_OK;
    });
}

plotcore_status plotcore_remove_line(plotcore_figure* figure, int32_t line)
{
    PLOTCORE_NEED_LINE(figure, line)
    return guarded([&] {
        figure->figure.removeLine(line);
        return PLOTCORE_OK;
    });
}

plotcore_status plotcore_clear(plotcore_figure* figure)
{
    PLOTCORE_NEED(figure)
    return guarded([&] {
        figure->figure.clear();
        return PLOTCORE_OK;
    });
}

int32_t plotcore_line_count(const plotcore_figure* figure)
{
    return figure != nullptr ? figure->figure.plot().lineCount() : 0;
}

int64_t plotcore_line_length(const plotcore_figure* figure, int32_t line)
{
    return figure != nullptr && hasLine(figure, line) ? figure->figure.plot().length(line) : -1;
}

plotcore_status plotcore_set_line_name(plotcore_figure* figure, int32_t line, const char* name)
{
    PLOTCORE_NEED_LINE(figure, line)
    return guarded([&] {
        figure->figure.plot().setName(line, name != nullptr ? name : "");
        return PLOTCORE_OK;
    });
}

plotcore_status plotcore_line_extent(const plotcore_figure* figure, int32_t line, double* minimum,
                                     double* maximum)
{
    PLOTCORE_NEED_LINE(figure, line)
    return guarded([&] {
        const LineExtent extent = figure->figure.plot().extent(line);
        if (!extent.finite) {
            return fail(PLOTCORE_ERROR_STATE, "the line has no finite value");
        }
        if (minimum != nullptr) {
            *minimum = extent.minimum;
        }
        if (maximum != nullptr) {
            *maximum = extent.maximum;
        }
        return PLOTCORE_OK;
    });
}

int64_t plotcore_line_base_bucket(const plotcore_figure* figure, int32_t line)
{
    return figure != nullptr && hasLine(figure, line) ? figure->figure.plot().baseBucket(line) : -1;
}

// --- x ----------------------------------------------------------------------------------------

plotcore_status plotcore_set_index_axis(plotcore_figure* figure, double start, double step)
{
    PLOTCORE_NEED(figure)
    if (!std::isfinite(start) || !std::isfinite(step) || step == 0.0) {
        return fail(PLOTCORE_ERROR_ARGUMENT, "an index axis needs a finite start and step");
    }
    return guarded([&] {
        figure->figure.plot().setIndexAxis(start, step);
        return PLOTCORE_OK;
    });
}

plotcore_status plotcore_set_time(plotcore_figure* figure, const double* values, int64_t count)
{
    PLOTCORE_NEED(figure)
    if (count < 0 || (values == nullptr && count > 0)) {
        return fail(PLOTCORE_ERROR_ARGUMENT, "no times");
    }
    return guarded([&] {
        figure->figure.plot().setTime(values, count);
        return PLOTCORE_OK;
    });
}

plotcore_status plotcore_set_line_time(plotcore_figure* figure, int32_t line, const double* values,
                                       int64_t count)
{
    PLOTCORE_NEED_LINE(figure, line)
    if (count < 0 || (values == nullptr && count > 0)) {
        return fail(PLOTCORE_ERROR_ARGUMENT, "no times");
    }
    return guarded([&] {
        figure->figure.plot().setLineTime(line, values, count);
        return PLOTCORE_OK;
    });
}

// --- holding ----------------------------------------------------------------------------------

plotcore_status plotcore_set_appetite(plotcore_figure* figure, plotcore_appetite appetite)
{
    PLOTCORE_NEED(figure)
    if (appetite < PLOTCORE_APPETITE_LOW || appetite > PLOTCORE_APPETITE_GREEDY) {
        return fail(PLOTCORE_ERROR_ARGUMENT, "no such appetite");
    }
    return guarded([&] {
        figure->figure.plot().setAppetite(static_cast<Appetite>(appetite));
        return PLOTCORE_OK;
    });
}

plotcore_status plotcore_set_budget_bytes(plotcore_figure* figure, int64_t bytes)
{
    PLOTCORE_NEED(figure)
    if (bytes < 0) {
        return fail(PLOTCORE_ERROR_ARGUMENT, "a budget cannot be less than nothing");
    }
    return guarded([&] {
        figure->figure.plot().setBudgetBytes(bytes);
        return PLOTCORE_OK;
    });
}

int64_t plotcore_budget_bytes(const plotcore_figure* figure)
{
    return figure != nullptr
               ? figure->figure.plot().budgetDoubles() * static_cast<int64_t>(sizeof(double))
               : 0;
}

int64_t plotcore_held_bytes(const plotcore_figure* figure)
{
    return figure != nullptr
               ? figure->figure.plot().heldDoubles() * static_cast<int64_t>(sizeof(double))
               : 0;
}

// --- settings -----------------------------------------------------------------------------------

plotcore_status plotcore_set_bool(plotcore_figure* figure, const char* name, int32_t value)
{
    PLOTCORE_NEED(figure)
    return guarded([&] {
        FigureSettings settings = figure->figure.settings();
        if (const auto* field = find(kBools, name)) {
            settings.*(field->member) = value != 0;
        }
        else if (name != nullptr && std::string_view(name) == "colours.reversed") {
            settings.colours.reversed = value != 0;
        }
        else {
            return noSetting(name);
        }
        figure->figure.setSettings(settings);
        return PLOTCORE_OK;
    });
}

plotcore_status plotcore_set_number(plotcore_figure* figure, const char* name, double value)
{
    PLOTCORE_NEED(figure)
    if (!std::isfinite(value)) {
        return fail(PLOTCORE_ERROR_ARGUMENT, "a setting is a finite number");
    }
    return guarded([&] {
        FigureSettings settings = figure->figure.settings();
        const std::string_view key = name != nullptr ? name : "";
        if (const auto* field = find(kNumbers, name)) {
            if ((key == "xLogBase" || key == "yLogBase") && !usableBase(value)) {
                return fail(PLOTCORE_ERROR_ARGUMENT, "a base is a number above one");
            }
            if ((key == "gridStepX" || key == "gridStepY") && value < 0.0) {
                return fail(PLOTCORE_ERROR_ARGUMENT, "a grid step is not negative");
            }
            settings.*(field->member) = value;
        }
        else if (key == "highlighted") {
            settings.highlighted = value < 0.0 ? -1 : static_cast<int>(value);
        }
        else if (key == "colours.from") {
            settings.colours.from = std::clamp(value, 0.0, 1.0);
        }
        else if (key == "colours.to") {
            settings.colours.to = std::clamp(value, 0.0, 1.0);
        }
        else {
            return noSetting(name);
        }
        figure->figure.setSettings(settings);
        return PLOTCORE_OK;
    });
}

plotcore_status plotcore_set_text(plotcore_figure* figure, const char* name, const char* value)
{
    PLOTCORE_NEED(figure)
    if (value == nullptr) {
        return fail(PLOTCORE_ERROR_ARGUMENT, "no text");
    }
    return guarded([&] {
        FigureSettings settings = figure->figure.settings();
        const std::string_view key = name != nullptr ? name : "";
        if (const auto* field = find(kTexts, name)) {
            settings.*(field->member) = value;
        }
        else if (key == "grid") {
            if (!gridOf(value, settings.grid)) {
                return fail(PLOTCORE_ERROR_ARGUMENT, std::string("no such grid: ") + value);
            }
        }
        else if (key == "legendCorner") {
            if (!cornerOf(value, settings.legendCorner)) {
                return fail(PLOTCORE_ERROR_ARGUMENT, std::string("no such corner: ") + value);
            }
        }
        else if (key == "colours.mode") {
            if (!isColourMode(value)) {
                return fail(PLOTCORE_ERROR_ARGUMENT, std::string("no such colours: ") + value);
            }
            settings.colours.mode = value;
        }
        else {
            return noSetting(name);
        }
        figure->figure.setSettings(settings);
        return PLOTCORE_OK;
    });
}

plotcore_status plotcore_set_colour(plotcore_figure* figure, const char* name, plotcore_rgba value)
{
    PLOTCORE_NEED(figure)
    return guarded([&] {
        FigureSettings settings = figure->figure.settings();
        const auto* field = find(kColours, name);
        if (field == nullptr) {
            return noSetting(name);
        }
        settings.colours.*(field->member) = colourOf(value);
        figure->figure.setSettings(settings);
        return PLOTCORE_OK;
    });
}

plotcore_status plotcore_get_bool(const plotcore_figure* figure, const char* name, int32_t* value)
{
    PLOTCORE_NEED(figure)
    const FigureSettings& settings = figure->figure.settings();
    bool answer = false;
    if (const auto* field = find(kBools, name)) {
        answer = settings.*(field->member);
    }
    else if (name != nullptr && std::string_view(name) == "colours.reversed") {
        answer = settings.colours.reversed;
    }
    else {
        return noSetting(name);
    }
    if (value != nullptr) {
        *value = answer ? 1 : 0;
    }
    return PLOTCORE_OK;
}

plotcore_status plotcore_get_number(const plotcore_figure* figure, const char* name, double* value)
{
    PLOTCORE_NEED(figure)
    const FigureSettings& settings = figure->figure.settings();
    const std::string_view key = name != nullptr ? name : "";
    double answer = 0.0;
    if (const auto* field = find(kNumbers, name)) {
        answer = settings.*(field->member);
    }
    else if (key == "highlighted") {
        answer = settings.highlighted;
    }
    else if (key == "colours.from") {
        answer = settings.colours.from;
    }
    else if (key == "colours.to") {
        answer = settings.colours.to;
    }
    else if (const auto* style = find(kStyle, name)) {
        answer = figure->figure.style().*(style->member);
    }
    else if (key == "tickTarget") {
        answer = figure->figure.style().tickTarget;
    }
    else if (key == "denseFactor") {
        answer = figure->figure.style().denseFactor;
    }
    else {
        return noSetting(name);
    }
    if (value != nullptr) {
        *value = answer;
    }
    return PLOTCORE_OK;
}

plotcore_status plotcore_get_text(const plotcore_figure* figure, const char* name, char* buffer,
                                  int64_t capacity, int64_t* length)
{
    PLOTCORE_NEED(figure)
    return guarded([&] {
        const FigureSettings& settings = figure->figure.settings();
        const std::string_view key = name != nullptr ? name : "";
        if (const auto* field = find(kTexts, name)) {
            return copyText(settings.*(field->member), buffer, capacity, length);
        }
        if (key == "grid") {
            return copyText(gridName(settings.grid), buffer, capacity, length);
        }
        if (key == "legendCorner") {
            return copyText(cornerName(settings.legendCorner), buffer, capacity, length);
        }
        if (key == "colours.mode") {
            return copyText(settings.colours.mode, buffer, capacity, length);
        }
        return noSetting(name);
    });
}

plotcore_status plotcore_get_colour(const plotcore_figure* figure, const char* name,
                                    plotcore_rgba* value)
{
    PLOTCORE_NEED(figure)
    const auto* field = find(kColours, name);
    if (field == nullptr) {
        return noSetting(name);
    }
    if (value != nullptr) {
        *value = rgbaOf(figure->figure.settings().colours.*(field->member));
    }
    return PLOTCORE_OK;
}

plotcore_status plotcore_set_line_bool(plotcore_figure* figure, int32_t line, const char* name,
                                       int32_t value)
{
    PLOTCORE_NEED_LINE(figure, line)
    return guarded([&] {
        LineSettings settings = figure->figure.lineSettings(line);
        const std::string_view key = name != nullptr ? name : "";
        if (key == "visible") {
            settings.visible = value != 0;
        }
        else if (key == "ownAxis") {
            settings.ownAxis = value != 0;
        }
        else if (key == "axisFixed") {
            settings.axisFixed = value != 0;
        }
        else {
            return noSetting(name);
        }
        figure->figure.setLineSettings(line, settings);
        return PLOTCORE_OK;
    });
}

plotcore_status plotcore_set_line_number(plotcore_figure* figure, int32_t line, const char* name,
                                         double value)
{
    PLOTCORE_NEED_LINE(figure, line)
    if (!std::isfinite(value)) {
        return fail(PLOTCORE_ERROR_ARGUMENT, "a setting is a finite number");
    }
    return guarded([&] {
        LineSettings settings = figure->figure.lineSettings(line);
        const std::string_view key = name != nullptr ? name : "";
        if (key == "scale") {
            if (!(value > 0.0)) {
                return fail(PLOTCORE_ERROR_ARGUMENT, "a scale is above zero");
            }
            settings.scale = value;
        }
        else if (key == "shift") {
            settings.shift = value;
        }
        else {
            return noSetting(name);
        }
        figure->figure.setLineSettings(line, settings);
        return PLOTCORE_OK;
    });
}

plotcore_status plotcore_set_line_text(plotcore_figure* figure, int32_t line, const char* name,
                                       const char* value)
{
    PLOTCORE_NEED_LINE(figure, line)
    if (value == nullptr) {
        return fail(PLOTCORE_ERROR_ARGUMENT, "no text");
    }
    if (name == nullptr || std::string_view(name) != "axisLabel") {
        return noSetting(name);
    }
    return guarded([&] {
        LineSettings settings = figure->figure.lineSettings(line);
        settings.axisLabel = value;
        figure->figure.setLineSettings(line, settings);
        return PLOTCORE_OK;
    });
}

plotcore_status plotcore_set_line_colour(plotcore_figure* figure, int32_t line, plotcore_rgba value)
{
    PLOTCORE_NEED_LINE(figure, line)
    return guarded([&] {
        LineSettings settings = figure->figure.lineSettings(line);
        settings.colour = colourOf(value);
        figure->figure.setLineSettings(line, settings);
        return PLOTCORE_OK;
    });
}

plotcore_status plotcore_clear_line_colour(plotcore_figure* figure, int32_t line)
{
    PLOTCORE_NEED_LINE(figure, line)
    return guarded([&] {
        LineSettings settings = figure->figure.lineSettings(line);
        settings.colour.reset();
        figure->figure.setLineSettings(line, settings);
        return PLOTCORE_OK;
    });
}

plotcore_status plotcore_get_line_bool(const plotcore_figure* figure, int32_t line,
                                       const char* name, int32_t* value)
{
    PLOTCORE_NEED_LINE(figure, line)
    const LineSettings settings = figure->figure.lineSettings(line);
    const std::string_view key = name != nullptr ? name : "";
    bool answer = false;
    if (key == "visible") {
        answer = settings.visible;
    }
    else if (key == "ownAxis") {
        answer = settings.ownAxis;
    }
    else if (key == "axisFixed") {
        answer = settings.axisFixed;
    }
    else {
        return noSetting(name);
    }
    if (value != nullptr) {
        *value = answer ? 1 : 0;
    }
    return PLOTCORE_OK;
}

plotcore_status plotcore_get_line_number(const plotcore_figure* figure, int32_t line,
                                         const char* name, double* value)
{
    PLOTCORE_NEED_LINE(figure, line)
    const LineSettings settings = figure->figure.lineSettings(line);
    const std::string_view key = name != nullptr ? name : "";
    double answer = 0.0;
    if (key == "scale") {
        answer = settings.scale;
    }
    else if (key == "shift") {
        answer = settings.shift;
    }
    else {
        return noSetting(name);
    }
    if (value != nullptr) {
        *value = answer;
    }
    return PLOTCORE_OK;
}

plotcore_status plotcore_get_line_colour(const plotcore_figure* figure, int32_t line, int32_t* has,
                                         plotcore_rgba* value)
{
    PLOTCORE_NEED_LINE(figure, line)
    const LineSettings settings = figure->figure.lineSettings(line);
    if (has != nullptr) {
        *has = settings.colour.has_value() ? 1 : 0;
    }
    if (value != nullptr && settings.colour.has_value()) {
        *value = rgbaOf(*settings.colour);
    }
    return PLOTCORE_OK;
}

plotcore_status plotcore_set_style(plotcore_figure* figure, const char* name, double value)
{
    PLOTCORE_NEED(figure)
    if (!std::isfinite(value) || value < 0.0) {
        return fail(PLOTCORE_ERROR_ARGUMENT, "a measurement is a finite number, not negative");
    }
    return guarded([&] {
        FigureStyle style = figure->figure.style();
        const std::string_view key = name != nullptr ? name : "";
        if (const auto* field = find(kStyle, name)) {
            style.*(field->member) = value;
        }
        else if (key == "tickTarget") {
            style.tickTarget = std::max(1, static_cast<int>(value));
        }
        else if (key == "denseFactor") {
            style.denseFactor = std::max(1, static_cast<int>(value));
        }
        else {
            return noSetting(name);
        }
        figure->figure.setStyle(style);
        return PLOTCORE_OK;
    });
}

plotcore_status plotcore_set_text_measure(plotcore_figure* figure, plotcore_text_width width,
                                          plotcore_text_height height, void* context)
{
    PLOTCORE_NEED(figure)
    if ((width == nullptr) != (height == nullptr)) {
        return fail(PLOTCORE_ERROR_ARGUMENT, "a measure is both a width and a height");
    }
    return guarded([&] {
        TextMeasure measure;
        if (width != nullptr) {
            measure.width = [width, context](const std::string& text, TextRole role) {
                return width(context, text.c_str(), static_cast<int32_t>(role));
            };
            measure.height = [height, context](TextRole role) {
                return height(context, static_cast<int32_t>(role));
            };
        }
        figure->figure.setMeasure(std::move(measure));
        return PLOTCORE_OK;
    });
}

// --- the pane and the gestures
// ---------------------------------------------------------------------

plotcore_status plotcore_set_size(plotcore_figure* figure, double width, double height,
                                  double pixel_ratio)
{
    PLOTCORE_NEED(figure)
    if (!std::isfinite(width) || !std::isfinite(height) || !std::isfinite(pixel_ratio)) {
        return fail(PLOTCORE_ERROR_ARGUMENT, "a size is finite");
    }
    figure->figure.setSize(width, height, pixel_ratio);
    return PLOTCORE_OK;
}

plotcore_status plotcore_wheel(plotcore_figure* figure, double px, double py, double angle_delta,
                               int32_t modifiers)
{
    PLOTCORE_NEED(figure)
    return guarded([&] {
        figure->figure.wheel(px, py, angle_delta, modifiersOf(modifiers));
        return PLOTCORE_OK;
    });
}

plotcore_status plotcore_press(plotcore_figure* figure, double px, double py,
                               plotcore_button button, int32_t modifiers)
{
    PLOTCORE_NEED(figure)
    return guarded([&] {
        figure->figure.press(px, py, buttonOf(button), modifiersOf(modifiers));
        return PLOTCORE_OK;
    });
}

plotcore_status plotcore_move(plotcore_figure* figure, double px, double py, int32_t modifiers)
{
    PLOTCORE_NEED(figure)
    return guarded([&] {
        figure->figure.move(px, py, modifiersOf(modifiers));
        return PLOTCORE_OK;
    });
}

plotcore_status plotcore_release(plotcore_figure* figure, double px, double py,
                                 plotcore_button button, int32_t modifiers)
{
    PLOTCORE_NEED(figure)
    return guarded([&] {
        figure->figure.release(px, py, buttonOf(button), modifiersOf(modifiers));
        return PLOTCORE_OK;
    });
}

plotcore_status plotcore_double_click(plotcore_figure* figure, double px, double py,
                                      plotcore_button button)
{
    PLOTCORE_NEED(figure)
    return guarded([&] {
        figure->figure.doubleClick(px, py, buttonOf(button));
        return PLOTCORE_OK;
    });
}

plotcore_status plotcore_hover(plotcore_figure* figure, double px, double py)
{
    PLOTCORE_NEED(figure)
    figure->figure.hover(px, py);
    return PLOTCORE_OK;
}

plotcore_status plotcore_leave(plotcore_figure* figure)
{
    PLOTCORE_NEED(figure)
    figure->figure.leave();
    return PLOTCORE_OK;
}

plotcore_status plotcore_reset_view(plotcore_figure* figure)
{
    PLOTCORE_NEED(figure)
    figure->figure.resetView();
    return PLOTCORE_OK;
}

plotcore_status plotcore_set_view(plotcore_figure* figure, double x0, double x1, double y0,
                                  double y1)
{
    PLOTCORE_NEED(figure)
    if (!std::isfinite(x0) || !std::isfinite(x1) || !std::isfinite(y0) || !std::isfinite(y1)) {
        return fail(PLOTCORE_ERROR_ARGUMENT, "a window is finite");
    }
    return guarded([&] {
        figure->figure.setViewRange(x0, x1, y0, y1);
        return PLOTCORE_OK;
    });
}

plotcore_status plotcore_view(const plotcore_figure* figure, double* x0, double* x1, double* y0,
                              double* y1)
{
    PLOTCORE_NEED(figure)
    const Camera& camera = figure->figure.camera();
    if (x0 != nullptr) {
        *x0 = camera.viewMinX();
    }
    if (x1 != nullptr) {
        *x1 = camera.viewMaxX();
    }
    if (y0 != nullptr) {
        *y0 = camera.viewMinY();
    }
    if (y1 != nullptr) {
        *y1 = camera.viewMaxY();
    }
    return PLOTCORE_OK;
}

int32_t plotcore_line_at(plotcore_figure* figure, double px, double py, double reach)
{
    if (figure == nullptr) {
        return -1;
    }
    try {
        return figure->figure.lineAt(px, py, reach);
    }
    catch (...) {
        return -1;
    }
}

// --- a view, written down
// ---------------------------------------------------------------------------

plotcore_status plotcore_state(const plotcore_figure* figure, char* buffer, int64_t capacity,
                               int64_t* length)
{
    PLOTCORE_NEED(figure)
    return guarded([&] { return copyText(figure->figure.state(), buffer, capacity, length); });
}

plotcore_status plotcore_restore(plotcore_figure* figure, const char* json)
{
    PLOTCORE_NEED(figure)
    if (json == nullptr) {
        return fail(PLOTCORE_ERROR_ARGUMENT, "no view");
    }
    return guarded([&] {
        return figure->figure.restore(json) ? PLOTCORE_OK
                                            : fail(PLOTCORE_ERROR_ARGUMENT, "not a saved view");
    });
}

// --- one frame
// -------------------------------------------------------------------------------------

plotcore_scene* plotcore_scene_new(plotcore_figure* figure)
{
    if (figure == nullptr) {
        lastError = "no figure";
        return nullptr;
    }
    lastError.clear();
    try {
        auto out = std::make_unique<plotcore_scene>();
        out->scene = figure->figure.scene();
        const Scene& scene = out->scene;
        out->rules.reserve(scene.rules.size());
        for (const Segment& segment : scene.rules) {
            out->rules.push_back(segmentOf(segment));
        }
        out->strokes.reserve(scene.strokes.size());
        for (const Segment& segment : scene.strokes) {
            out->strokes.push_back(segmentOf(segment));
        }
        const auto flat = [](const std::vector<Point>& points) {
            std::vector<double> xy;
            xy.reserve(points.size() * 2);
            for (const Point& point : points) {
                xy.push_back(point.x());
                xy.push_back(point.y());
            }
            return xy;
        };
        out->lines.reserve(scene.lines.size());
        for (const DrawnLine& line : scene.lines) {
            plotcore_scene::Line flattened;
            flattened.runs.reserve(line.runs.size());
            for (const std::vector<Point>& run : line.runs) {
                flattened.runs.push_back(flat(run));
            }
            flattened.triangles = flat(line.triangles);
            flattened.markers = flat(line.markers);
            out->lines.push_back(std::move(flattened));
        }
        return out.release();
    }
    catch (const std::exception& error) {
        lastError = error.what();
    }
    catch (...) {
        lastError = "unknown failure";
    }
    return nullptr;
}

void plotcore_scene_free(plotcore_scene* scene)
{
    delete scene;
}

void plotcore_scene_size(const plotcore_scene* scene, double* width, double* height)
{
    if (width != nullptr) {
        *width = scene != nullptr ? scene->scene.width : 0.0;
    }
    if (height != nullptr) {
        *height = scene != nullptr ? scene->scene.height : 0.0;
    }
}

plotcore_rgba plotcore_scene_ground(const plotcore_scene* scene)
{
    return scene != nullptr ? rgbaOf(scene->scene.ground) : plotcore_rgba{0.0, 0.0, 0.0, 0.0};
}

plotcore_rect plotcore_scene_area(const plotcore_scene* scene)
{
    return scene != nullptr ? rectOf(scene->scene.area) : plotcore_rect{0.0, 0.0, 0.0, 0.0};
}

void plotcore_scene_view(const plotcore_scene* scene, double* x0, double* x1, double* y0,
                         double* y1)
{
    const Scene empty;
    const Scene& s = scene != nullptr ? scene->scene : empty;
    if (x0 != nullptr) {
        *x0 = s.x.low;
    }
    if (x1 != nullptr) {
        *x1 = s.x.high;
    }
    if (y0 != nullptr) {
        *y0 = s.y.low;
    }
    if (y1 != nullptr) {
        *y1 = s.y.high;
    }
}

const char* plotcore_scene_message(const plotcore_scene* scene)
{
    return scene != nullptr ? scene->scene.message.c_str() : "";
}

int64_t plotcore_scene_line_count(const plotcore_scene* scene)
{
    return scene != nullptr ? static_cast<int64_t>(scene->scene.lines.size()) : 0;
}

namespace {

bool hasDrawn(const plotcore_scene* scene, int64_t index)
{
    return scene != nullptr && index >= 0 &&
           static_cast<std::size_t>(index) < scene->scene.lines.size();
}

} // namespace

plotcore_status plotcore_scene_line(const plotcore_scene* scene, int64_t index,
                                    plotcore_line_info* out)
{
    if (!hasDrawn(scene, index) || out == nullptr) {
        return fail(PLOTCORE_ERROR_RANGE, "no drawn line " + std::to_string(index));
    }
    const DrawnLine& line = scene->scene.lines[static_cast<std::size_t>(index)];
    out->line = line.line;
    out->colour = rgbaOf(line.colour);
    out->width = line.width;
    out->marker_radius = line.markerRadius;
    out->run_count = static_cast<int64_t>(line.runs.size());
    out->triangle_points = static_cast<int64_t>(line.triangles.size());
    out->marker_count = static_cast<int64_t>(line.markers.size());
    return PLOTCORE_OK;
}

int64_t plotcore_scene_line_run_length(const plotcore_scene* scene, int64_t index, int64_t run)
{
    if (!hasDrawn(scene, index)) {
        return 0;
    }
    const auto& runs = scene->lines[static_cast<std::size_t>(index)].runs;
    return run >= 0 && static_cast<std::size_t>(run) < runs.size()
               ? static_cast<int64_t>(runs[static_cast<std::size_t>(run)].size() / 2)
               : 0;
}

const double* plotcore_scene_line_run(const plotcore_scene* scene, int64_t index, int64_t run)
{
    if (!hasDrawn(scene, index)) {
        return nullptr;
    }
    const auto& runs = scene->lines[static_cast<std::size_t>(index)].runs;
    return run >= 0 && static_cast<std::size_t>(run) < runs.size()
               ? runs[static_cast<std::size_t>(run)].data()
               : nullptr;
}

const double* plotcore_scene_line_triangles(const plotcore_scene* scene, int64_t index)
{
    return hasDrawn(scene, index) ? scene->lines[static_cast<std::size_t>(index)].triangles.data()
                                  : nullptr;
}

const double* plotcore_scene_line_markers(const plotcore_scene* scene, int64_t index)
{
    return hasDrawn(scene, index) ? scene->lines[static_cast<std::size_t>(index)].markers.data()
                                  : nullptr;
}

int64_t plotcore_scene_rule_count(const plotcore_scene* scene)
{
    return scene != nullptr ? static_cast<int64_t>(scene->rules.size()) : 0;
}

const plotcore_segment* plotcore_scene_rules(const plotcore_scene* scene)
{
    return scene != nullptr ? scene->rules.data() : nullptr;
}

int64_t plotcore_scene_stroke_count(const plotcore_scene* scene)
{
    return scene != nullptr ? static_cast<int64_t>(scene->strokes.size()) : 0;
}

const plotcore_segment* plotcore_scene_strokes(const plotcore_scene* scene)
{
    return scene != nullptr ? scene->strokes.data() : nullptr;
}

int64_t plotcore_scene_text_count(const plotcore_scene* scene)
{
    return scene != nullptr ? static_cast<int64_t>(scene->scene.texts.size()) : 0;
}

plotcore_status plotcore_scene_text(const plotcore_scene* scene, int64_t index, plotcore_text* out)
{
    if (scene == nullptr || out == nullptr || index < 0 ||
        static_cast<std::size_t>(index) >= scene->scene.texts.size()) {
        return fail(PLOTCORE_ERROR_RANGE, "no text " + std::to_string(index));
    }
    const TextItem& text = scene->scene.texts[static_cast<std::size_t>(index)];
    out->x = text.at.x();
    out->y = text.at.y();
    out->across = static_cast<int32_t>(text.across);
    out->down = static_cast<int32_t>(text.down);
    out->role = static_cast<int32_t>(text.role);
    out->vertical = text.vertical ? 1 : 0;
    out->colour = rgbaOf(text.colour);
    out->text = text.text.c_str();
    out->base = text.base.c_str();
    out->exponent = text.exponent.c_str();
    return PLOTCORE_OK;
}

int64_t plotcore_scene_legend_count(const plotcore_scene* scene)
{
    return scene != nullptr ? static_cast<int64_t>(scene->scene.legend.size()) : 0;
}

plotcore_status plotcore_scene_legend(const plotcore_scene* scene, int64_t index,
                                      plotcore_legend_entry* out)
{
    if (scene == nullptr || out == nullptr || index < 0 ||
        static_cast<std::size_t>(index) >= scene->scene.legend.size()) {
        return fail(PLOTCORE_ERROR_RANGE, "no legend entry " + std::to_string(index));
    }
    const LegendEntry& entry = scene->scene.legend[static_cast<std::size_t>(index)];
    out->line = entry.line;
    out->colour = rgbaOf(entry.colour);
    out->x0 = entry.from.x();
    out->y0 = entry.from.y();
    out->x1 = entry.to.x();
    out->y1 = entry.to.y();
    out->text_x = entry.textAt.x();
    out->text_y = entry.textAt.y();
    out->name = entry.name.c_str();
    return PLOTCORE_OK;
}

plotcore_rect plotcore_scene_legend_rect(const plotcore_scene* scene)
{
    return scene != nullptr ? rectOf(scene->scene.legendRect) : plotcore_rect{0.0, 0.0, 0.0, 0.0};
}

plotcore_reading plotcore_scene_reading(const plotcore_scene* scene)
{
    plotcore_reading out{0, -1, 0.0, 0.0, 0.0, 0.0, "", ""};
    if (scene == nullptr || !scene->scene.reading.valid) {
        return out;
    }
    const Reading& reading = scene->scene.reading;
    out.valid = 1;
    out.line = reading.line;
    out.x = reading.x;
    out.y = reading.y;
    out.px = reading.at.x();
    out.py = reading.at.y();
    out.x_text = reading.xText.c_str();
    out.y_text = reading.yText.c_str();
    return out;
}

int32_t plotcore_scene_band(const plotcore_scene* scene, plotcore_rect* band)
{
    if (scene == nullptr || !scene->scene.band.has_value()) {
        return 0;
    }
    if (band != nullptr) {
        *band = rectOf(*scene->scene.band);
    }
    return 1;
}
