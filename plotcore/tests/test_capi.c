/* SPDX-FileCopyrightText: 2026 Jonas Sattler
 * SPDX-License-Identifier: GPL-3.0-only */

/* plotcore.h, from C.
 *
 * Compiled as C on purpose: a header that only a C++ compiler accepts is not a
 * C interface, and Python's ctypes and Rust's FFI both read it as one. Two
 * halves. The first holds the interface to its own rules -- a status for every
 * failure, nothing borrowed, a context let go exactly once. The second runs
 * tests/golden/script.txt and writes down every scene it asks for, which is
 * the digest the Python and Rust bindings are held to.
 *
 *     test_capi                              the contract
 *     test_capi SCRIPT EXPECTED              ...and the golden script
 *     test_capi SCRIPT EXPECTED --write      ...rewriting EXPECTED */

#include "plotcore.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_MSC_VER)
#define strtok_r strtok_s
#endif

static int failures = 0;

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "%s:%d: CHECK(%s) failed: %s\n", __FILE__, __LINE__, #condition,       \
                    plotcore_last_error());                                                        \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)

/* --- the contract ------------------------------------------------------------ */

static int reads = 0;
static int released = 0;

static int readRamp(void* context, int64_t first, int64_t count, double* out)
{
    (void)context;
    ++reads;
    for (int64_t i = 0; i < count; ++i) {
        out[i] = (double)(first + i);
    }
    return 1;
}

static void releaseContext(void* context)
{
    (void)context;
    ++released;
}

static int widths = 0;

static double measureWidth(void* context, const char* text, int32_t role)
{
    (void)context;
    (void)role;
    ++widths;
    return 7.0 * (double)strlen(text);
}

static double measureHeight(void* context, int32_t role)
{
    (void)context;
    return role == PLOTCORE_TEXT_TITLE ? 18.0 : 13.0;
}

static void contract(void)
{
    CHECK(plotcore_abi_version() == PLOTCORE_ABI_VERSION);
    CHECK(strlen(plotcore_version()) > 0);

    /* A null figure is a status, not a crash. */
    CHECK(plotcore_add_line(NULL, NULL, 0, NULL, NULL) == PLOTCORE_ERROR_ARGUMENT);
    CHECK(strlen(plotcore_last_error()) > 0);
    CHECK(plotcore_scene_new(NULL) == NULL);

    plotcore_figure* figure = plotcore_figure_new();
    CHECK(figure != NULL);
    CHECK(plotcore_set_size(figure, 640, 400, 1) == PLOTCORE_OK);
    CHECK(strlen(plotcore_last_error()) == 0);

    /* Values are copied in: the caller's buffer may go at once. */
    double* values = malloc(1000 * sizeof(double));
    for (int i = 0; i < 1000; ++i) {
        values[i] = (double)(i % 100);
    }
    int32_t line = -1;
    CHECK(plotcore_add_line(figure, values, 1000, "teeth", &line) == PLOTCORE_OK);
    CHECK(line == 0);
    memset(values, 0, 1000 * sizeof(double));
    free(values);
    double minimum = 0;
    double maximum = 0;
    CHECK(plotcore_line_extent(figure, 0, &minimum, &maximum) == PLOTCORE_OK);
    CHECK(minimum == 0.0 && maximum == 99.0);
    CHECK(plotcore_line_length(figure, 0) == 1000);

    /* A line that does not exist is a range, a setting that does not exist an
     * argument, and a call out of order a state. */
    CHECK(plotcore_set_line_bool(figure, 7, "visible", 1) == PLOTCORE_ERROR_RANGE);
    CHECK(plotcore_set_bool(figure, "noSuchThing", 1) == PLOTCORE_ERROR_ARGUMENT);
    CHECK(plotcore_set_text(figure, "grid", "tartan") == PLOTCORE_ERROR_ARGUMENT);
    CHECK(plotcore_set_number(figure, "xLogBase", 1.0) == PLOTCORE_ERROR_ARGUMENT);
    CHECK(plotcore_append_line(figure, 0, &minimum, 1) == PLOTCORE_ERROR_STATE);

    /* Settings read back as they were written. */
    CHECK(plotcore_set_bool(figure, "xLog", 1) == PLOTCORE_OK);
    int32_t on = 0;
    CHECK(plotcore_get_bool(figure, "xLog", &on) == PLOTCORE_OK && on == 1);
    CHECK(plotcore_set_bool(figure, "xLog", 0) == PLOTCORE_OK);
    CHECK(plotcore_set_text(figure, "title", "a title") == PLOTCORE_OK);
    char small[4];
    int64_t length = 0;
    CHECK(plotcore_get_text(figure, "title", small, sizeof small, &length) == PLOTCORE_OK);
    CHECK(length == 7 && strcmp(small, "a t") == 0);
    CHECK(plotcore_set_text(figure, "colours.mode", "viridis") == PLOTCORE_OK);
    plotcore_rgba blue = {0.1, 0.2, 0.9, 1.0};
    CHECK(plotcore_set_line_colour(figure, 0, blue) == PLOTCORE_OK);
    int32_t has = 0;
    plotcore_rgba back = {0, 0, 0, 0};
    CHECK(plotcore_get_line_colour(figure, 0, &has, &back) == PLOTCORE_OK);
    CHECK(has == 1 && back.blue == 0.9);
    CHECK(plotcore_clear_line_colour(figure, 0) == PLOTCORE_OK);
    CHECK(plotcore_get_line_colour(figure, 0, &has, &back) == PLOTCORE_OK && has == 0);

    /* The palettes are named and listed. */
    CHECK(plotcore_palette_count() == 5);
    CHECK(strcmp(plotcore_palette_name(0), "okabe-ito") == 0);
    CHECK(plotcore_palette_name(99) == NULL);
    plotcore_rgba stops[32];
    CHECK(plotcore_palette("okabe-ito", 1, stops, 32) == 8);
    CHECK(plotcore_palette("viridis", 1, stops, 32) == 5);
    CHECK(plotcore_palette("plaid", 1, stops, 32) == -1);

    /* A streamed line is not drawn until it is finished. */
    CHECK(plotcore_begin_line(figure, 300, "streamed", &line) == PLOTCORE_OK && line == 1);
    double piece[100];
    for (int i = 0; i < 100; ++i) {
        piece[i] = i;
    }
    for (int i = 0; i < 3; ++i) {
        CHECK(plotcore_append_line(figure, 1, piece, 100) == PLOTCORE_OK);
    }
    plotcore_scene* scene = plotcore_scene_new(figure);
    CHECK(scene != NULL && plotcore_scene_line_count(scene) == 1);
    plotcore_scene_free(scene);
    CHECK(plotcore_finish_line(figure, 1) == PLOTCORE_OK);
    scene = plotcore_scene_new(figure);
    CHECK(scene != NULL && plotcore_scene_line_count(scene) == 2);
    plotcore_line_info info;
    CHECK(plotcore_scene_line(scene, 1, &info) == PLOTCORE_OK);
    CHECK(info.line == 1 && info.run_count == 1);
    CHECK(plotcore_scene_line_run_length(scene, 1, 0) == 300);
    const double* run = plotcore_scene_line_run(scene, 1, 0);
    CHECK(run != NULL && run[0] < run[2]);
    CHECK(info.triangle_points % 3 == 0 && info.triangle_points > 0);
    CHECK(plotcore_scene_text_count(scene) > 0);
    plotcore_text text;
    CHECK(plotcore_scene_text(scene, 0, &text) == PLOTCORE_OK && text.text != NULL);
    CHECK(plotcore_scene_text(scene, -1, &text) == PLOTCORE_ERROR_RANGE);
    plotcore_scene_free(scene);

    /* A reader's context is let go once: when it is replaced, here. */
    reads = 0;
    released = 0;
    CHECK(plotcore_set_line_reader(figure, 0, readRamp, &reads, releaseContext) == PLOTCORE_OK);
    CHECK(plotcore_set_line_reader(figure, 0, readRamp, &reads, releaseContext) == PLOTCORE_OK);
    CHECK(released == 1);
    CHECK(plotcore_remove_line(figure, 0) == PLOTCORE_OK);
    CHECK(released == 2);
    CHECK(plotcore_line_count(figure) == 1);

    /* The host's measure is what the frame is laid out by. */
    widths = 0;
    CHECK(plotcore_set_text_measure(figure, measureWidth, measureHeight, NULL) == PLOTCORE_OK);
    scene = plotcore_scene_new(figure);
    CHECK(widths > 0);
    plotcore_scene_free(scene);
    CHECK(plotcore_set_text_measure(figure, measureWidth, NULL, NULL) == PLOTCORE_ERROR_ARGUMENT);
    CHECK(plotcore_set_text_measure(figure, NULL, NULL, NULL) == PLOTCORE_OK);

    /* A view written down reads back, and what is not one is refused. */
    CHECK(plotcore_wheel(figure, 320, 200, 360, PLOTCORE_SHIFT) == PLOTCORE_OK);
    double x0 = 0;
    double x1 = 0;
    CHECK(plotcore_view(figure, &x0, &x1, NULL, NULL) == PLOTCORE_OK);
    CHECK(plotcore_state(figure, NULL, 0, &length) == PLOTCORE_OK && length > 0);
    char* state = malloc((size_t)length + 1);
    CHECK(plotcore_state(figure, state, length + 1, NULL) == PLOTCORE_OK);
    CHECK(plotcore_reset_view(figure) == PLOTCORE_OK);
    CHECK(plotcore_restore(figure, state) == PLOTCORE_OK);
    double y0 = 0;
    double again = 0;
    CHECK(plotcore_view(figure, &y0, &again, NULL, NULL) == PLOTCORE_OK);
    CHECK(y0 == x0 && again == x1);
    CHECK(plotcore_restore(figure, "{]") == PLOTCORE_ERROR_ARGUMENT);
    free(state);

    plotcore_figure_free(figure);

    /* ...and when the figure goes. */
    figure = plotcore_figure_new();
    double one = 1.0;
    CHECK(plotcore_add_line(figure, &one, 1, NULL, &line) == PLOTCORE_OK);
    released = 0;
    CHECK(plotcore_set_line_reader(figure, 0, readRamp, NULL, releaseContext) == PLOTCORE_OK);
    plotcore_figure_free(figure);
    CHECK(released == 1);
}

/* --- the golden script ----------------------------------------------------------- */

typedef struct Out
{
    char* text;
    size_t size;
    size_t capacity;
} Out;

static void put(Out* out, const char* format, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;

static void put(Out* out, const char* format, ...)
{
    for (;;) {
        va_list args;
        va_start(args, format);
        const size_t room = out->capacity - out->size;
        const int wrote = vsnprintf(out->text + out->size, room, format, args);
        va_end(args);
        if (wrote >= 0 && (size_t)wrote < room) {
            out->size += (size_t)wrote;
            return;
        }
        out->capacity = out->capacity * 2 + (size_t)(wrote > 0 ? wrote : 256) + 1;
        out->text = realloc(out->text, out->capacity);
    }
}

/* A number as %.17g, which is a round trip; the comparison is numeric. */
#define G "%.17g"

static void colour(Out* out, plotcore_rgba c)
{
    put(out, " " G " " G " " G " " G, c.red, c.green, c.blue, c.alpha);
}

static void segments(Out* out, const char* what, const plotcore_segment* s, int64_t count)
{
    double x = 0;
    double y = 0;
    for (int64_t i = 0; i < count; ++i) {
        x += s[i].x0;
        x += s[i].x1;
        y += s[i].y0;
        y += s[i].y1;
    }
    put(out, "%s %lld " G " " G "\n", what, (long long)count, x, y);
}

static void digest(Out* out, plotcore_scene* scene, int index)
{
    double w = 0;
    double h = 0;
    plotcore_scene_size(scene, &w, &h);
    put(out, "scene %d\nsize " G " " G "\n", index, w, h);
    const plotcore_rect area = plotcore_scene_area(scene);
    put(out, "area " G " " G " " G " " G "\n", area.x, area.y, area.width, area.height);
    double x0 = 0;
    double x1 = 0;
    double y0 = 0;
    double y1 = 0;
    plotcore_scene_view(scene, &x0, &x1, &y0, &y1);
    put(out, "view " G " " G " " G " " G "\n", x0, x1, y0, y1);
    put(out, "ground");
    colour(out, plotcore_scene_ground(scene));
    put(out, "\nmessage |%s\n", plotcore_scene_message(scene));
    for (int64_t i = 0; i < plotcore_scene_line_count(scene); ++i) {
        plotcore_line_info info;
        plotcore_scene_line(scene, i, &info);
        long long points = 0;
        double sx = 0;
        double sy = 0;
        double vx = 0;
        double vy = 0;
        for (int64_t r = 0; r < info.run_count; ++r) {
            const int64_t n = plotcore_scene_line_run_length(scene, i, r);
            const double* xy = plotcore_scene_line_run(scene, i, r);
            const double* values = plotcore_scene_line_run_values(scene, i, r);
            for (int64_t k = 0; k < n; ++k) {
                sx += xy[2 * k];
                sy += xy[2 * k + 1];
                vx += values[2 * k];
                vy += values[2 * k + 1];
            }
            points += n;
        }
        put(out, "line %d", info.line);
        colour(out, info.colour);
        put(out, " " G " %lld %lld %lld %lld " G " " G "\n", info.width, (long long)info.run_count,
            points, (long long)info.triangle_points, (long long)info.marker_count, sx, sy);
        put(out, "window " G " " G " %d %d %d " G " " G "\n", info.y_low, info.y_high, info.y_log,
            info.own, info.numbered, vx, vy);
    }
    segments(out, "rules", plotcore_scene_rules(scene), plotcore_scene_rule_count(scene));
    segments(out, "strokes", plotcore_scene_strokes(scene), plotcore_scene_stroke_count(scene));
    for (int64_t i = 0; i < plotcore_scene_text_count(scene); ++i) {
        plotcore_text t;
        plotcore_scene_text(scene, i, &t);
        put(out, "text %d %d %d %d " G " " G, t.role, t.across, t.down, t.vertical, t.x, t.y);
        colour(out, t.colour);
        put(out, " |%s\n", t.text);
    }
    const int64_t legend = plotcore_scene_legend_count(scene);
    for (int64_t i = 0; i < legend; ++i) {
        plotcore_legend_entry e;
        plotcore_scene_legend(scene, i, &e);
        put(out, "legend %d", e.line);
        colour(out, e.colour);
        put(out, " " G " " G " " G " " G " " G " " G " |%s\n", e.x0, e.y0, e.x1, e.y1, e.text_x,
            e.text_y, e.name);
    }
    if (legend > 0) {
        const plotcore_rect r = plotcore_scene_legend_rect(scene);
        put(out, "legend_rect " G " " G " " G " " G "\n", r.x, r.y, r.width, r.height);
    }
    const plotcore_reading reading = plotcore_scene_reading(scene);
    put(out, "reading %d %d " G " " G " " G " " G " |%s|%s\n", reading.valid, reading.line,
        reading.x, reading.y, reading.px, reading.py, reading.x_text, reading.y_text);
    plotcore_rect band = {0, 0, 0, 0};
    const int present = plotcore_scene_band(scene, &band);
    put(out, "band %d " G " " G " " G " " G "\n", present, band.x, band.y, band.width, band.height);
}

/* The generators of README.md. */
static double* generate(char** words, int count, int64_t* length, int* used)
{
    if (count < 2) {
        return NULL;
    }
    const char* kind = words[0];
    const int64_t n = atoll(words[1]);
    double* v = malloc((size_t)(n > 0 ? n : 1) * sizeof(double));
    *length = n;
    if (strcmp(kind, "ramp") == 0 && count >= 4) {
        const double from = atof(words[2]);
        const double step = atof(words[3]);
        for (int64_t i = 0; i < n; ++i) {
            v[i] = from + (double)i * step;
        }
        *used = 4;
    }
    else if (strcmp(kind, "quadratic") == 0 && count >= 3) {
        const double scale = atof(words[2]);
        for (int64_t i = 0; i < n; ++i) {
            v[i] = scale * (double)(i + 1) * (double)(i + 1);
        }
        *used = 3;
    }
    else if (strcmp(kind, "square") == 0 && count >= 5) {
        const int64_t period = atoll(words[2]);
        const double low = atof(words[3]);
        const double high = atof(words[4]);
        for (int64_t i = 0; i < n; ++i) {
            v[i] = (i % period) < period / 2 ? low : high;
        }
        *used = 5;
    }
    else if (strcmp(kind, "holes") == 0 && count >= 3) {
        const int64_t every = atoll(words[2]);
        for (int64_t i = 0; i < n; ++i) {
            v[i] = (i % every) < 3 ? NAN : (double)i;
        }
        *used = 3;
    }
    else if (strcmp(kind, "walk") == 0 && count >= 3) {
        uint32_t state = (uint32_t)strtoul(words[2], NULL, 10);
        double at = 0.0;
        for (int64_t i = 0; i < n; ++i) {
            state = state * 1103515245u + 12345u;
            at += (double)((state >> 16) & 0x7fffu) / 32767.0 - 0.5;
            v[i] = (i % 997 == 996) ? at + 500.0 : at;
        }
        *used = 3;
    }
    else {
        free(v);
        return NULL;
    }
    return v;
}

static int32_t modifiersOf(const char* word)
{
    int32_t bits = 0;
    if (strstr(word, "shift") != NULL) {
        bits |= PLOTCORE_SHIFT;
    }
    if (strstr(word, "control") != NULL) {
        bits |= PLOTCORE_CONTROL;
    }
    if (strstr(word, "alt") != NULL) {
        bits |= PLOTCORE_ALT;
    }
    return bits;
}

static plotcore_button buttonOf(const char* word)
{
    if (strcmp(word, "middle") == 0) {
        return PLOTCORE_BUTTON_MIDDLE;
    }
    if (strcmp(word, "right") == 0) {
        return PLOTCORE_BUTTON_RIGHT;
    }
    return PLOTCORE_BUTTON_LEFT;
}

/* The text after the first `skip` words of `line`. */
static const char* rest(const char* line, int skip)
{
    const char* at = line;
    for (int i = 0; i < skip && at != NULL; ++i) {
        at = strchr(at, ' ');
        if (at != NULL) {
            ++at;
        }
    }
    return at != NULL ? at : "";
}

/* Where sample `sample` of drawn line `line`'s first run is, in `last`. */
static int sampleAt(plotcore_scene* last, int line, int64_t sample, double* x, double* y)
{
    for (int64_t i = 0; last != NULL && i < plotcore_scene_line_count(last); ++i) {
        plotcore_line_info info;
        plotcore_scene_line(last, i, &info);
        if (info.line != line || info.run_count < 1) {
            continue;
        }
        if (sample >= plotcore_scene_line_run_length(last, i, 0)) {
            return 0;
        }
        const double* xy = plotcore_scene_line_run(last, i, 0);
        *x = xy[2 * sample];
        *y = xy[2 * sample + 1];
        return 1;
    }
    return 0;
}

static int runScript(const char* path, Out* out)
{
    FILE* file = fopen(path, "r");
    if (file == NULL) {
        fprintf(stderr, "cannot open %s\n", path);
        return 0;
    }
    plotcore_figure* figure = plotcore_figure_new();
    plotcore_scene* last = NULL;
    char* kept = NULL;
    double pointerX = 0;
    double pointerY = 0;
    int scenes = 0;
    int number = 0;
    char line[4096];
    int ok = 1;
    while (fgets(line, sizeof line, file) != NULL) {
        ++number;
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }
        char copy[4096];
        strcpy(copy, line);
        char* words[64];
        int count = 0;
        for (char* word = strtok(copy, " "); word != NULL && count < 64; word = strtok(NULL, " ")) {
            words[count++] = word;
        }
        const char* command = words[0];
        plotcore_status status = PLOTCORE_OK;
        if (strcmp(command, "size") == 0) {
            status = plotcore_set_size(figure, atof(words[1]), atof(words[2]), atof(words[3]));
        }
        else if (strcmp(command, "line") == 0) {
            int64_t n = 0;
            int used = 0;
            double* values = generate(words + 1, count - 1, &n, &used);
            if (values == NULL) {
                status = PLOTCORE_ERROR_ARGUMENT;
            }
            else {
                status = plotcore_add_line(figure, values, n, rest(line, 1 + used), NULL);
                free(values);
            }
        }
        else if (strcmp(command, "remove") == 0) {
            status = plotcore_remove_line(figure, atoi(words[1]));
        }
        else if (strcmp(command, "clear") == 0) {
            status = plotcore_clear(figure);
        }
        else if (strcmp(command, "time") == 0 || strcmp(command, "line_time") == 0) {
            const int own = strcmp(command, "line_time") == 0;
            if (!own && strcmp(words[1], "none") == 0) {
                status = plotcore_set_time(figure, NULL, 0);
            }
            else {
                int64_t n = 0;
                int used = 0;
                double* values = generate(words + 1 + own, count - 1 - own, &n, &used);
                status = values == NULL ? PLOTCORE_ERROR_ARGUMENT
                         : own          ? plotcore_set_line_time(figure, atoi(words[1]), values, n)
                                        : plotcore_set_time(figure, values, n);
                free(values);
            }
        }
        else if (strcmp(command, "budget") == 0) {
            status = plotcore_set_budget_bytes(figure, atoll(words[1]));
        }
        else if (strcmp(command, "set_bool") == 0) {
            status = plotcore_set_bool(figure, words[1], atoi(words[2]));
        }
        else if (strcmp(command, "set_number") == 0) {
            status = plotcore_set_number(figure, words[1], atof(words[2]));
        }
        else if (strcmp(command, "set_text") == 0) {
            status = plotcore_set_text(figure, words[1], rest(line, 2));
        }
        else if (strcmp(command, "set_colour") == 0) {
            const plotcore_rgba c = {atof(words[2]), atof(words[3]), atof(words[4]),
                                     atof(words[5])};
            status = plotcore_set_colour(figure, words[1], c);
        }
        else if (strcmp(command, "line_bool") == 0) {
            status = plotcore_set_line_bool(figure, atoi(words[1]), words[2], atoi(words[3]));
        }
        else if (strcmp(command, "line_number") == 0) {
            status = plotcore_set_line_number(figure, atoi(words[1]), words[2], atof(words[3]));
        }
        else if (strcmp(command, "line_text") == 0) {
            status = plotcore_set_line_text(figure, atoi(words[1]), words[2], rest(line, 3));
        }
        else if (strcmp(command, "line_colour") == 0) {
            const plotcore_rgba c = {atof(words[2]), atof(words[3]), atof(words[4]),
                                     atof(words[5])};
            status = plotcore_set_line_colour(figure, atoi(words[1]), c);
        }
        else if (strcmp(command, "wheel") == 0) {
            pointerX = atof(words[1]);
            pointerY = atof(words[2]);
            status =
                plotcore_wheel(figure, pointerX, pointerY, atof(words[3]), modifiersOf(words[4]));
        }
        else if (strcmp(command, "press") == 0) {
            pointerX = atof(words[1]);
            pointerY = atof(words[2]);
            status = plotcore_press(figure, pointerX, pointerY, buttonOf(words[3]),
                                    modifiersOf(words[4]));
        }
        else if (strcmp(command, "move") == 0) {
            pointerX = atof(words[1]);
            pointerY = atof(words[2]);
            status = plotcore_move(figure, pointerX, pointerY, modifiersOf(words[3]));
        }
        else if (strcmp(command, "release") == 0) {
            pointerX = atof(words[1]);
            pointerY = atof(words[2]);
            status = plotcore_release(figure, pointerX, pointerY, buttonOf(words[3]),
                                      modifiersOf(words[4]));
        }
        else if (strcmp(command, "double") == 0) {
            status =
                plotcore_double_click(figure, atof(words[1]), atof(words[2]), buttonOf(words[3]));
        }
        else if (strcmp(command, "hover") == 0) {
            pointerX = atof(words[1]);
            pointerY = atof(words[2]);
            status = plotcore_hover(figure, pointerX, pointerY);
        }
        else if (strcmp(command, "leave") == 0) {
            status = plotcore_leave(figure);
        }
        else if (strcmp(command, "reset") == 0) {
            status = plotcore_reset_view(figure);
        }
        else if (strcmp(command, "view") == 0) {
            status = plotcore_set_view(figure, atof(words[1]), atof(words[2]), atof(words[3]),
                                       atof(words[4]));
        }
        else if (strcmp(command, "press_line") == 0 || strcmp(command, "wheel_line") == 0 ||
                 strcmp(command, "click_line") == 0) {
            if (!sampleAt(last, atoi(words[1]), atoll(words[2]), &pointerX, &pointerY)) {
                status = PLOTCORE_ERROR_RANGE;
            }
            else if (command[0] == 'p') {
                status = plotcore_press(figure, pointerX, pointerY, PLOTCORE_BUTTON_LEFT,
                                        modifiersOf(words[3]));
            }
            else if (command[0] == 'w') {
                status = plotcore_wheel(figure, pointerX, pointerY, atof(words[3]),
                                        modifiersOf(words[4]));
            }
            else {
                const int32_t mods = modifiersOf(words[3]);
                status = plotcore_press(figure, pointerX, pointerY, PLOTCORE_BUTTON_LEFT, mods);
                if (status == PLOTCORE_OK) {
                    status =
                        plotcore_release(figure, pointerX, pointerY, PLOTCORE_BUTTON_LEFT, mods);
                }
            }
        }
        else if (strcmp(command, "move_by") == 0) {
            pointerX += atof(words[1]);
            pointerY += atof(words[2]);
            status = plotcore_move(figure, pointerX, pointerY, modifiersOf(words[3]));
        }
        else if (strcmp(command, "release_here") == 0) {
            status = plotcore_release(figure, pointerX, pointerY, buttonOf(words[1]),
                                      modifiersOf(words[2]));
        }
        else if (strcmp(command, "state") == 0) {
            int64_t length = 0;
            status = plotcore_state(figure, NULL, 0, &length);
            free(kept);
            kept = malloc((size_t)length + 1);
            if (status == PLOTCORE_OK) {
                status = plotcore_state(figure, kept, length + 1, NULL);
            }
            put(out, "state |%s\n", kept);
        }
        else if (strcmp(command, "restore") == 0) {
            status = kept != NULL ? plotcore_restore(figure, kept) : PLOTCORE_ERROR_STATE;
        }
        else if (strcmp(command, "scene") == 0) {
            plotcore_scene_free(last);
            last = plotcore_scene_new(figure);
            if (last == NULL) {
                status = PLOTCORE_ERROR_INTERNAL;
            }
            else {
                digest(out, last, scenes++);
            }
        }
        else {
            fprintf(stderr, "%s:%d: no command %s\n", path, number, command);
            ok = 0;
            continue;
        }
        if (status != PLOTCORE_OK) {
            fprintf(stderr, "%s:%d: %s: status %d: %s\n", path, number, line, (int)status,
                    plotcore_last_error());
            ok = 0;
        }
    }
    fclose(file);
    free(kept);
    plotcore_scene_free(last);
    plotcore_figure_free(figure);
    return ok;
}

/* Token by token: numbers within a relative 1e-9, the rest of a line after
 * its first '|' exactly. */
static int sameNumbers(const char* a, const char* b)
{
    char* endA = NULL;
    char* endB = NULL;
    const double x = strtod(a, &endA);
    const double y = strtod(b, &endB);
    if (endA == a || endB == b || *endA != '\0' || *endB != '\0') {
        return strcmp(a, b) == 0;
    }
    if (isnan(x) || isnan(y)) {
        return isnan(x) && isnan(y);
    }
    const double scale = fmax(1.0, fmax(fabs(x), fabs(y)));
    return fabs(x - y) <= 1e-9 * scale;
}

static int sameLine(const char* a, const char* b)
{
    const char* barA = strchr(a, '|');
    const char* barB = strchr(b, '|');
    if ((barA == NULL) != (barB == NULL)) {
        return 0;
    }
    if (barA != NULL && strcmp(barA, barB) != 0) {
        return 0;
    }
    char headA[4096];
    char headB[4096];
    const size_t lengthA = barA != NULL ? (size_t)(barA - a) : strlen(a);
    const size_t lengthB = barB != NULL ? (size_t)(barB - b) : strlen(b);
    if (lengthA >= sizeof headA || lengthB >= sizeof headB) {
        return strcmp(a, b) == 0;
    }
    memcpy(headA, a, lengthA);
    headA[lengthA] = '\0';
    memcpy(headB, b, lengthB);
    headB[lengthB] = '\0';
    char* saveA = NULL;
    char* saveB = NULL;
    char* wordA = strtok_r(headA, " ", &saveA);
    char* wordB = strtok_r(headB, " ", &saveB);
    while (wordA != NULL && wordB != NULL) {
        if (!sameNumbers(wordA, wordB)) {
            return 0;
        }
        wordA = strtok_r(NULL, " ", &saveA);
        wordB = strtok_r(NULL, " ", &saveB);
    }
    return wordA == NULL && wordB == NULL;
}

static char* readAll(const char* path)
{
    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        return NULL;
    }
    fseek(file, 0, SEEK_END);
    const long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    char* text = malloc((size_t)size + 1);
    const size_t read = fread(text, 1, (size_t)size, file);
    text[read] = '\0';
    fclose(file);
    return text;
}

static int compareDigests(char* expected, char* actual)
{
    char* saveE = NULL;
    char* saveA = NULL;
    char* e = strtok_r(expected, "\n", &saveE);
    char* a = strtok_r(actual, "\n", &saveA);
    int line = 1;
    while (e != NULL && a != NULL) {
        size_t n = strlen(e);
        if (n > 0 && e[n - 1] == '\r') {
            e[n - 1] = '\0';
        }
        if (!sameLine(e, a)) {
            fprintf(stderr, "golden line %d differs\n  expected: %s\n  actual:   %s\n", line, e, a);
            return 0;
        }
        e = strtok_r(NULL, "\n", &saveE);
        a = strtok_r(NULL, "\n", &saveA);
        ++line;
    }
    if (e != NULL || a != NULL) {
        fprintf(stderr, "golden digest is %s than expected at line %d\n",
                e != NULL ? "shorter" : "longer", line);
        return 0;
    }
    return 1;
}

int main(int argc, char** argv)
{
    contract();
    if (argc >= 3) {
        Out out = {NULL, 0, 0};
        put(&out, "%s", "");
        if (!runScript(argv[1], &out)) {
            ++failures;
        }
        if (argc >= 4 && strcmp(argv[3], "--write") == 0) {
            FILE* file = fopen(argv[2], "wb");
            if (file == NULL) {
                fprintf(stderr, "cannot write %s\n", argv[2]);
                return 1;
            }
            fwrite(out.text, 1, out.size, file);
            fclose(file);
            printf("wrote %s\n", argv[2]);
        }
        else {
            char* expected = readAll(argv[2]);
            if (expected == NULL) {
                fprintf(stderr, "cannot read %s\n", argv[2]);
                ++failures;
            }
            else if (!compareDigests(expected, out.text)) {
                ++failures;
            }
            free(expected);
        }
        free(out.text);
    }
    if (failures > 0) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("all checks passed\n");
    return 0;
}
