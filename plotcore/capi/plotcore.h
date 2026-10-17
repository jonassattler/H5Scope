/* SPDX-FileCopyrightText: 2026 Jonas Sattler
 * SPDX-FileCopyrightText: 2026 Andreas Martin
 * SPDX-License-Identifier: GPL-3.0-only */

#ifndef PLOTCORE_H
#define PLOTCORE_H

/* plotcore as C: what the Python and Rust libraries are built on.
 *
 * One plot is a plotcore_figure. A host copies its lines in, hands over the
 * pane's size and how to measure text, forwards the pointer and the wheel, and
 * asks for a plotcore_scene, which it paints: strokes, rules and text in pane
 * pixels with y down. Every decision between those is plotcore::Figure's, so
 * this header adds none -- it is that class with the C++ taken out.
 *
 * Three rules hold the interface up, each of them about a boundary between
 * two languages:
 *
 * - **Nothing is borrowed across it.** Values, times and names are copied in
 *   when they are handed over, and a scene is a snapshot the caller owns and
 *   frees: no pointer either side keeps can be freed by the other. The one
 *   exception is the strings and arrays a scene hands back, which live as
 *   long as that scene does and say so.
 * - **Settings are named, not laid out.** plotcore_set_bool("xLog", 1) rather
 *   than a struct, so a setting added later is a new name and not a new ABI,
 *   and the names are the keys a saved view writes (plotcore_state).
 * - **A failure is a status, not an exception.** Every call that can fail
 *   returns one, and plotcore_last_error() says why, on the calling thread.
 *
 * A figure is not thread-safe: one thread at a time, as with any widget. */

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#if defined(PLOTCORE_C_BUILDING)
#define PLOTCORE_API __declspec(dllexport)
#else
#define PLOTCORE_API __declspec(dllimport)
#endif
#elif defined(PLOTCORE_C_BUILDING)
#define PLOTCORE_API __attribute__((visibility("default")))
#else
#define PLOTCORE_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Bumped whenever a declaration below changes meaning or layout. A binding
 * compares it with plotcore_abi_version() before it calls anything else. */
#define PLOTCORE_ABI_VERSION 1

typedef enum plotcore_status
{
    PLOTCORE_OK = 0,
    /* A null figure, a null pointer where data was promised, a name that is
     * not a setting, a value the setting cannot take. */
    PLOTCORE_ERROR_ARGUMENT = 1,
    /* A line that does not exist. */
    PLOTCORE_ERROR_RANGE = 2,
    /* A call out of order: appending to a finished line, say. */
    PLOTCORE_ERROR_STATE = 3,
    /* Out of memory, or anything else plotcore threw. */
    PLOTCORE_ERROR_INTERNAL = 4
} plotcore_status;

typedef enum plotcore_button
{
    PLOTCORE_BUTTON_LEFT = 0,
    PLOTCORE_BUTTON_MIDDLE = 1,
    PLOTCORE_BUTTON_RIGHT = 2
} plotcore_button;

/* Modifier keys, or-ed together. */
enum
{
    PLOTCORE_SHIFT = 1,
    PLOTCORE_CONTROL = 2,
    PLOTCORE_ALT = 4
};

/* The faces text is set in; a host maps each to a font. */
typedef enum plotcore_text_role
{
    PLOTCORE_TEXT_TICK = 0,
    PLOTCORE_TEXT_TICK_MINOR = 1,
    PLOTCORE_TEXT_TITLE = 2,
    PLOTCORE_TEXT_AXIS_NAME = 3,
    PLOTCORE_TEXT_LEGEND = 4
} plotcore_text_role;

typedef enum plotcore_align
{
    PLOTCORE_ALIGN_START = 0,
    PLOTCORE_ALIGN_MIDDLE = 1,
    PLOTCORE_ALIGN_END = 2
} plotcore_align;

typedef enum plotcore_appetite
{
    PLOTCORE_APPETITE_LOW = 0,
    PLOTCORE_APPETITE_MEDIUM = 1,
    PLOTCORE_APPETITE_GREEDY = 2
} plotcore_appetite;

typedef struct plotcore_rgba
{
    double red;
    double green;
    double blue;
    double alpha;
} plotcore_rgba;

/* What everything that is not a line is drawn in. */
typedef struct plotcore_chrome
{
    plotcore_rgba ground;
    plotcore_rgba ink;
    plotcore_rgba rule_minor;
    plotcore_rgba rule_major;
    plotcore_rgba axis_rule;
    plotcore_rgba cursor_ink;
} plotcore_chrome;

typedef struct plotcore_rect
{
    double x;
    double y;
    double width;
    double height;
} plotcore_rect;

typedef struct plotcore_segment
{
    double x0;
    double y0;
    double x1;
    double y1;
    plotcore_rgba colour;
    double width;
} plotcore_segment;

/* One drawn line. Its geometry is fetched with plotcore_scene_line_points. */
typedef struct plotcore_line_info
{
    int32_t line;
    plotcore_rgba colour;
    double width;
    double marker_radius;
    int64_t run_count;
    int64_t triangle_points;
    int64_t marker_count;
    /* The window y is read in, and whether it is the line's own and whether
     * that is numbered beside the pane: see plotcore_scene_line_run_values. */
    double y_low;
    double y_high;
    int32_t y_log;
    int32_t own;
    int32_t numbered;
} plotcore_line_info;

/* A piece of text. `base` is set in `role`'s face and `exponent` raised beside
 * it in PLOTCORE_TEXT_TICK_MINOR's; `text` is both in one string, with the
 * exponent in superscript digits, for a host that sets one string. `across`
 * aligns it horizontally about `x` and `down` vertically about `y`; a
 * `vertical` one is turned a quarter anticlockwise about that point. The
 * strings are UTF-8 and live as long as the scene. */
typedef struct plotcore_text
{
    double x;
    double y;
    int32_t across;
    int32_t down;
    int32_t role;
    int32_t vertical;
    plotcore_rgba colour;
    const char* text;
    const char* base;
    const char* exponent;
} plotcore_text;

/* One row of the legend: a swatch from (x0, y0) to (x1, y1), and the name set
 * in PLOTCORE_TEXT_LEGEND from (text_x, text_y), centred on it vertically. */
typedef struct plotcore_legend_entry
{
    int32_t line;
    plotcore_rgba colour;
    double x0;
    double y0;
    double x1;
    double y1;
    double text_x;
    double text_y;
    const char* name;
} plotcore_legend_entry;

/* The sample under the pointer. The strings live as long as the scene. */
typedef struct plotcore_reading
{
    int32_t valid;
    int32_t line;
    double x;
    double y;
    double px;
    double py;
    const char* x_text;
    const char* y_text;
} plotcore_reading;

typedef struct plotcore_figure plotcore_figure;
typedef struct plotcore_scene plotcore_scene;

/* Reads `count` elements of a line from element `first` into `out`, for a
 * closer look finer than the line is held at. Nonzero for success. */
typedef int (*plotcore_reader)(void* context, int64_t first, int64_t count, double* out);
/* Called once when a reader's context is let go. */
typedef void (*plotcore_free_context)(void* context);
/* How wide `text` (UTF-8) is in `role`'s face, and how tall a line of it. */
typedef double (*plotcore_text_width)(void* context, const char* text, int32_t role);
typedef double (*plotcore_text_height)(void* context, int32_t role);

/* --- the library ---------------------------------------------------------- */

PLOTCORE_API int32_t plotcore_abi_version(void);
PLOTCORE_API const char* plotcore_version(void);
/* Why the last call on this thread failed; empty after one that did not. */
PLOTCORE_API const char* plotcore_last_error(void);

/* The categorical cycles and the continuous maps, in the order a settings
 * panel offers them. The names are static. */
PLOTCORE_API int32_t plotcore_palette_count(void);
PLOTCORE_API const char* plotcore_palette_name(int32_t index);
PLOTCORE_API int32_t plotcore_ramp_count(void);
PLOTCORE_API const char* plotcore_ramp_name(int32_t index);
/* A cycle's or a map's stops, up to `capacity` of them; how many there are,
 * or -1 for a name that is neither. */
PLOTCORE_API int32_t plotcore_palette(const char* name, int32_t on_dark, plotcore_rgba* out,
                                      int32_t capacity);

/* --- a figure ------------------------------------------------------------------ */

PLOTCORE_API plotcore_figure* plotcore_figure_new(void);
PLOTCORE_API void plotcore_figure_free(plotcore_figure* figure);

/* --- lines ------------------------------------------------------------------------ */

/* Copy `count` values in as a line, named `name` (UTF-8, or null). The line's
 * index is written to `line`. */
PLOTCORE_API plotcore_status plotcore_add_line(plotcore_figure* figure, const double* values,
                                               int64_t count, const char* name, int32_t* line);
/* A line that arrives in pieces: `count` elements, appended in order, then
 * finished. Not drawn until it is. */
PLOTCORE_API plotcore_status plotcore_begin_line(plotcore_figure* figure, int64_t count,
                                                 const char* name, int32_t* line);
PLOTCORE_API plotcore_status plotcore_append_line(plotcore_figure* figure, int32_t line,
                                                  const double* values, int64_t count);
PLOTCORE_API plotcore_status plotcore_finish_line(plotcore_figure* figure, int32_t line);
/* Where a closer look finer than the line's base reads from; `release` (or
 * null) is called with `context` when the reader is replaced or the line goes. */
PLOTCORE_API plotcore_status plotcore_set_line_reader(plotcore_figure* figure, int32_t line,
                                                      plotcore_reader reader, void* context,
                                                      plotcore_free_context release);
PLOTCORE_API plotcore_status plotcore_remove_line(plotcore_figure* figure, int32_t line);
PLOTCORE_API plotcore_status plotcore_clear(plotcore_figure* figure);
PLOTCORE_API int32_t plotcore_line_count(const plotcore_figure* figure);
PLOTCORE_API int64_t plotcore_line_length(const plotcore_figure* figure, int32_t line);
PLOTCORE_API plotcore_status plotcore_set_line_name(plotcore_figure* figure, int32_t line,
                                                    const char* name);
/* The smallest and largest finite value of a line. */
PLOTCORE_API plotcore_status plotcore_line_extent(const plotcore_figure* figure, int32_t line,
                                                  double* minimum, double* maximum);
/* The finest bucket the line is held at; one is every element. */
PLOTCORE_API int64_t plotcore_line_base_bucket(const plotcore_figure* figure, int32_t line);
/* A line's name, copied as plotcore_get_text copies. */
PLOTCORE_API plotcore_status plotcore_get_line_name(const plotcore_figure* figure, int32_t line,
                                                    char* buffer, int64_t capacity,
                                                    int64_t* length);
/* 1 once a line has finished arriving, 0 while it is streaming in, -1 for no
 * line. */
PLOTCORE_API int32_t plotcore_line_ready(const plotcore_figure* figure, int32_t line);
/* 1 when a line is drawn against a time base, its own or the shared one. */
PLOTCORE_API int32_t plotcore_line_has_time(const plotcore_figure* figure, int32_t line);

/* --- x -------------------------------------------------------------------------------- */

/* The x of element i of a line with no time: start + i * step. */
PLOTCORE_API plotcore_status plotcore_set_index_axis(plotcore_figure* figure, double start,
                                                     double step);
/* A time base every line without its own is drawn against; null or zero for
 * none. */
PLOTCORE_API plotcore_status plotcore_set_time(plotcore_figure* figure, const double* values,
                                               int64_t count);
/* A line's own time base, which beats the shared one. */
PLOTCORE_API plotcore_status plotcore_set_line_time(plotcore_figure* figure, int32_t line,
                                                    const double* values, int64_t count);

/* --- holding ---------------------------------------------------------------------------- */

PLOTCORE_API plotcore_status plotcore_set_appetite(plotcore_figure* figure,
                                                   plotcore_appetite appetite);
/* Pin what the lines may hold between them to `bytes`; zero goes back to the
 * appetite. */
PLOTCORE_API plotcore_status plotcore_set_budget_bytes(plotcore_figure* figure, int64_t bytes);
PLOTCORE_API int64_t plotcore_budget_bytes(const plotcore_figure* figure);
PLOTCORE_API int64_t plotcore_held_bytes(const plotcore_figure* figure);

/* --- settings, by the names a saved view writes ---------------------------------------- */
/*
 * The plot as a whole:
 *   bool    xLog yLog flipped minorNumbers markers cursor legend bundle dark
 *           paper stacked colours.reversed
 *   number  gridStepX gridStepY xLogBase yLogBase highlighted colours.from
 *           colours.to
 *   text    title xLabel yLabel grid ("none" "loose" "dense" "custom")
 *           legendCorner ("top-left" "top-right" "bottom-left" "bottom-right")
 *           colours.mode (a palette, a ramp, "same" or "range")
 *   colour  colours.single colours.rangeFrom colours.rangeTo
 *
 * One line:
 *   bool    visible ownAxis axisFixed
 *   number  scale shift
 *   text    axisLabel
 *   colour  colour (cleared with plotcore_clear_line_colour)
 *
 * The frame's measurements, every one a number: tickPixels tickMinorPixels
 *   titlePixels axisNamePixels legendPixels gapXS gapS gapM gapL s3 s9
 *   labelMargin accentWidth ruleWidth tickLength exponentRaise lineWidth
 *   markerSize tickTarget denseFactor
 *
 * A change of either scale or of the flip puts the view back, as the viewer's
 * does: a pan in decades and a pan in units have no reading that is both.
 */
PLOTCORE_API plotcore_status plotcore_set_bool(plotcore_figure* figure, const char* name,
                                               int32_t value);
PLOTCORE_API plotcore_status plotcore_set_number(plotcore_figure* figure, const char* name,
                                                 double value);
PLOTCORE_API plotcore_status plotcore_set_text(plotcore_figure* figure, const char* name,
                                               const char* value);
PLOTCORE_API plotcore_status plotcore_set_colour(plotcore_figure* figure, const char* name,
                                                 plotcore_rgba value);
PLOTCORE_API plotcore_status plotcore_get_bool(const plotcore_figure* figure, const char* name,
                                               int32_t* value);
PLOTCORE_API plotcore_status plotcore_get_number(const plotcore_figure* figure, const char* name,
                                                 double* value);
/* Copies up to `capacity` bytes and a terminator; `length` (or null) is the
 * whole string's length, so a too-small buffer can be retried. */
PLOTCORE_API plotcore_status plotcore_get_text(const plotcore_figure* figure, const char* name,
                                               char* buffer, int64_t capacity, int64_t* length);
PLOTCORE_API plotcore_status plotcore_get_colour(const plotcore_figure* figure, const char* name,
                                                 plotcore_rgba* value);

PLOTCORE_API plotcore_status plotcore_set_line_bool(plotcore_figure* figure, int32_t line,
                                                    const char* name, int32_t value);
PLOTCORE_API plotcore_status plotcore_set_line_number(plotcore_figure* figure, int32_t line,
                                                      const char* name, double value);
PLOTCORE_API plotcore_status plotcore_set_line_text(plotcore_figure* figure, int32_t line,
                                                    const char* name, const char* value);
PLOTCORE_API plotcore_status plotcore_set_line_colour(plotcore_figure* figure, int32_t line,
                                                      plotcore_rgba value);
PLOTCORE_API plotcore_status plotcore_clear_line_colour(plotcore_figure* figure, int32_t line);
PLOTCORE_API plotcore_status plotcore_get_line_bool(const plotcore_figure* figure, int32_t line,
                                                    const char* name, int32_t* value);
PLOTCORE_API plotcore_status plotcore_get_line_number(const plotcore_figure* figure, int32_t line,
                                                      const char* name, double* value);
PLOTCORE_API plotcore_status plotcore_get_line_text(const plotcore_figure* figure, int32_t line,
                                                    const char* name, char* buffer,
                                                    int64_t capacity, int64_t* length);
/* Writes 1 to `has` and the colour when the line has one of its own. */
PLOTCORE_API plotcore_status plotcore_get_line_colour(const plotcore_figure* figure, int32_t line,
                                                      int32_t* has, plotcore_rgba* value);

PLOTCORE_API plotcore_status plotcore_set_style(plotcore_figure* figure, const char* name,
                                                double value);
/* How the host measures text, or null for both to go back to an
 * approximation that needs no font. */
PLOTCORE_API plotcore_status plotcore_set_text_measure(plotcore_figure* figure,
                                                       plotcore_text_width width,
                                                       plotcore_text_height height, void* context);

/* --- the pane and the gestures, in pane pixels with y down -------------------------------- */

PLOTCORE_API plotcore_status plotcore_set_size(plotcore_figure* figure, double width, double height,
                                               double pixel_ratio);
/* `angle_delta` in eighths of a degree, as Qt counts them: 120 is one notch.
 * Shift zooms across the pane, Control up it; Alt and Control together scale
 * the line under the pointer. */
PLOTCORE_API plotcore_status plotcore_wheel(plotcore_figure* figure, double px, double py,
                                            double angle_delta, int32_t modifiers);
/* Left pans; right draws a band to zoom to; middle, or left with Alt, moves
 * the line under the pointer; Control and a left click puts it back. */
PLOTCORE_API plotcore_status plotcore_press(plotcore_figure* figure, double px, double py,
                                            plotcore_button button, int32_t modifiers);
PLOTCORE_API plotcore_status plotcore_move(plotcore_figure* figure, double px, double py,
                                           int32_t modifiers);
PLOTCORE_API plotcore_status plotcore_release(plotcore_figure* figure, double px, double py,
                                              plotcore_button button, int32_t modifiers);
PLOTCORE_API plotcore_status plotcore_double_click(plotcore_figure* figure, double px, double py,
                                                   plotcore_button button);
PLOTCORE_API plotcore_status plotcore_hover(plotcore_figure* figure, double px, double py);
PLOTCORE_API plotcore_status plotcore_leave(plotcore_figure* figure);
PLOTCORE_API plotcore_status plotcore_reset_view(plotcore_figure* figure);
/* The window named in values -- the viewer's four boxes -- clipped to what
 * the axes can draw. */
PLOTCORE_API plotcore_status plotcore_set_view(plotcore_figure* figure, double x0, double x1,
                                               double y0, double y1);
PLOTCORE_API plotcore_status plotcore_view(const plotcore_figure* figure, double* x0, double* x1,
                                           double* y0, double* y1);
/* The line drawn nearest (px, py) within `reach` pixels, or -1. */
PLOTCORE_API int32_t plotcore_line_at(plotcore_figure* figure, double px, double py, double reach);

/* --- a view, written down ---------------------------------------------------------------- */

/* The settings, every line's settings and the window, as JSON. Copies as
 * plotcore_get_text does. */
PLOTCORE_API plotcore_status plotcore_state(const plotcore_figure* figure, char* buffer,
                                            int64_t capacity, int64_t* length);
/* Put back what plotcore_state wrote. A setting this build does not know is
 * passed over; PLOTCORE_ERROR_ARGUMENT when `json` is not a view at all. */
PLOTCORE_API plotcore_status plotcore_restore(plotcore_figure* figure, const char* json);

/* --- one frame ------------------------------------------------------------------------------ */

/* A snapshot of the picture as it stands, which the caller frees. */
PLOTCORE_API plotcore_scene* plotcore_scene_new(plotcore_figure* figure);
PLOTCORE_API void plotcore_scene_free(plotcore_scene* scene);

PLOTCORE_API void plotcore_scene_size(const plotcore_scene* scene, double* width, double* height);
PLOTCORE_API plotcore_rgba plotcore_scene_ground(const plotcore_scene* scene);
PLOTCORE_API plotcore_chrome plotcore_scene_chrome(const plotcore_scene* scene);
/* The pane inside the gutters, where the lines are. */
PLOTCORE_API plotcore_rect plotcore_scene_area(const plotcore_scene* scene);
/* The window on screen: x from x0 to x1, y from y0 to y1. */
PLOTCORE_API void plotcore_scene_view(const plotcore_scene* scene, double* x0, double* x1,
                                      double* y0, double* y1);
/* Why nothing can be drawn, or empty when it can. Lives as long as the scene. */
PLOTCORE_API const char* plotcore_scene_message(const plotcore_scene* scene);

PLOTCORE_API int64_t plotcore_scene_line_count(const plotcore_scene* scene);
PLOTCORE_API plotcore_status plotcore_scene_line(const plotcore_scene* scene, int64_t index,
                                                 plotcore_line_info* out);
/* A run of a drawn line as a polyline: x, y pairs, from the scene. */
PLOTCORE_API int64_t plotcore_scene_line_run_length(const plotcore_scene* scene, int64_t index,
                                                    int64_t run);
PLOTCORE_API const double* plotcore_scene_line_run(const plotcore_scene* scene, int64_t index,
                                                   int64_t run);
/* The same run in the data's own units, point for point: x as the x axis
 * reads it, y as the line's window reads it. For a host whose axes are its
 * own, such as matplotlib. */
PLOTCORE_API const double* plotcore_scene_line_run_values(const plotcore_scene* scene,
                                                          int64_t index, int64_t run);
/* The line as triangles, three x, y pairs each; and its marker centres. */
PLOTCORE_API const double* plotcore_scene_line_triangles(const plotcore_scene* scene,
                                                         int64_t index);
PLOTCORE_API const double* plotcore_scene_line_markers(const plotcore_scene* scene, int64_t index);

/* The grid, under the lines; the axes, their ticks and the crosshair, over. */
PLOTCORE_API int64_t plotcore_scene_rule_count(const plotcore_scene* scene);
PLOTCORE_API const plotcore_segment* plotcore_scene_rules(const plotcore_scene* scene);
PLOTCORE_API int64_t plotcore_scene_stroke_count(const plotcore_scene* scene);
PLOTCORE_API const plotcore_segment* plotcore_scene_strokes(const plotcore_scene* scene);

PLOTCORE_API int64_t plotcore_scene_text_count(const plotcore_scene* scene);
PLOTCORE_API plotcore_status plotcore_scene_text(const plotcore_scene* scene, int64_t index,
                                                 plotcore_text* out);

PLOTCORE_API int64_t plotcore_scene_legend_count(const plotcore_scene* scene);
PLOTCORE_API plotcore_status plotcore_scene_legend(const plotcore_scene* scene, int64_t index,
                                                   plotcore_legend_entry* out);
PLOTCORE_API plotcore_rect plotcore_scene_legend_rect(const plotcore_scene* scene);

PLOTCORE_API plotcore_reading plotcore_scene_reading(const plotcore_scene* scene);
/* 1 and the band while a right drag is drawing one, else 0. */
PLOTCORE_API int32_t plotcore_scene_band(const plotcore_scene* scene, plotcore_rect* band);

#ifdef __cplusplus
}
#endif

#endif /* PLOTCORE_H */
