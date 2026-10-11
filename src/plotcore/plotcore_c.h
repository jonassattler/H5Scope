/* SPDX-FileCopyrightText: 2026 Andreas Martin
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef H5PLOT_C_H
#define H5PLOT_C_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32
#ifdef H5PLOT_EXPORT
#define H5PLOT_API __declspec(dllexport)
#else
#define H5PLOT_API __declspec(dllimport)
#endif
#elif defined(__GNUC__)
/* Default visibility exports everything. A toolchain that hides symbols
 * (and a wheel build is free to) would then ship a library whose C API
 * ctypes cannot find. The attribute is the dllexport of ELF and Mach-O. */
#define H5PLOT_API __attribute__((visibility("default")))
#else
#define H5PLOT_API
#endif

typedef struct H5Plot H5Plot;

typedef struct H5PlotRun
{
    int first;
    int count;
    unsigned char red;
    unsigned char green;
    unsigned char blue;
    unsigned char alpha;
    float width;
    /* 1 when the vertices are a summary. A marker belongs on a sample.
     * A logarithmic axis drops the values it cannot draw, and the stroke
     * then has fewer vertices; those are still the extremes of a bucket,
     * and a dot on one marks a reading nobody took. */
    unsigned char summarised;
} H5PlotRun;

/* A tick in the pane set by h5plot_set_pane. y grows downward, as the
 * projected points do. axis is 0 along x, 1 on the common y, 2 on a line's
 * own y. series is that line when axis is 2, and -1 otherwise.
 * logarithmic is set when the log locator placed it. labeled is a number;
 * an unlabeled tick is a minor mark between two of those. */
typedef struct H5PlotTick
{
    double x;
    double y;
    double value;
    int axis;
    int series;
    unsigned char red;
    unsigned char green;
    unsigned char blue;
    unsigned char alpha;
    unsigned char labeled;
    unsigned char logarithmic;
} H5PlotTick;

H5PLOT_API H5Plot* h5plot_create(void);
H5PLOT_API void h5plot_destroy(H5Plot* plot);

/* Borrow `n` doubles. The caller must keep the buffer alive. */
H5PLOT_API int h5plot_add_line(H5Plot* plot, const double* y, long long n, int red, int green,
                               int blue);

/* A line fed in order, one read at a time. It is not drawn until finish.
 * The samples need not outlive add_samples. */
H5PLOT_API int h5plot_begin_line(H5Plot* plot, long long n, int red, int green, int blue);
H5PLOT_API void h5plot_add_samples(H5Plot* plot, int index, const double* y, long long n);
H5PLOT_API void h5plot_finish_line(H5Plot* plot, int index);

/* Called when a closer look is finer than the pyramid. Writes `count`
 * doubles at `out`, the line's elements from `first`. Returns 0 to refuse. */
typedef int (*H5PlotRead)(void* user, long long first, long long count, double* out);
H5PLOT_API void h5plot_set_reader(H5Plot* plot, int index, H5PlotRead read, void* user);

/* Shared x, borrowed. n < 2 clears it and x is the sample index again.
 * A line can name a time of its own; this is the clock the others use. */
H5PLOT_API void h5plot_set_axis(H5Plot* plot, const double* x, long long n);

/* This line's own time, borrowed. The numbered x stays one window, and the
 * line is drawn where its timestamps fall in it. n must be this line's
 * length and at least 2; anything else changes nothing and returns 0.
 * n of 0, or a null buffer, clears it and the line returns to the shared
 * clock. Returns 1 when the time is set or cleared. */
H5PLOT_API int h5plot_set_line_axis(H5Plot* plot, int index, const double* x, long long n);

/* The same time, one read at a time. Not in the picture until finish.
 * begin returns 0 unless n is this line's length and at least 2. finish
 * returns 0 when the time was not accepted. The reader is asked when a
 * closer look is finer than the pyramid, and writes `count` doubles. */
H5PLOT_API int h5plot_begin_line_axis(H5Plot* plot, int index, long long n);
H5PLOT_API void h5plot_add_line_axis_samples(H5Plot* plot, int index, const double* x, long long n);
H5PLOT_API int h5plot_finish_line_axis(H5Plot* plot, int index);
H5PLOT_API void h5plot_set_line_axis_reader(H5Plot* plot, int index, H5PlotRead read, void* user);

/* The same axis, one read at a time. Not in the picture until finish.
 * The samples need not outlive add_axis_samples. The reader is asked when
 * a closer look is finer than the pyramid, and writes `count` doubles. */
H5PLOT_API void h5plot_begin_axis(H5Plot* plot, long long n);
H5PLOT_API void h5plot_add_axis_samples(H5Plot* plot, const double* x, long long n);
H5PLOT_API void h5plot_finish_axis(H5Plot* plot);
H5PLOT_API void h5plot_set_axis_reader(H5Plot* plot, H5PlotRead read, void* user);
H5PLOT_API void h5plot_clear(H5Plot* plot);

H5PLOT_API void h5plot_set_pane(H5Plot* plot, int width, int height, double pixel_ratio);
H5PLOT_API void h5plot_set_ylog(H5Plot* plot, int on);
H5PLOT_API void h5plot_set_xlog(H5Plot* plot, int on);
/* A base at or below one is refused and the axis keeps its base. The base
 * numbers the powers. It does not move a point: where a value sits is a
 * ratio of two logarithms, so the base cancels. */
H5PLOT_API void h5plot_set_x_log_base(H5Plot* plot, double base);
H5PLOT_API void h5plot_set_y_log_base(H5Plot* plot, double base);
H5PLOT_API double h5plot_x_log_base(const H5Plot* plot);
H5PLOT_API double h5plot_y_log_base(const H5Plot* plot);
H5PLOT_API void h5plot_reset_view(H5Plot* plot);
/* Pane-local pixels, y downward. factor above one zooms in.
 * Shift alone is x, Ctrl alone is y, and either together with the other
 * is both. Alt with Ctrl, and without Shift, zooms only the line nearest
 * the pointer, in y, and gives it an axis of its own so the numbers beside
 * it are the values it is drawn at. A pointer on no line zooms nothing:
 * those keys named one curve, and a miss is not a request to move the frame. */
H5PLOT_API void h5plot_wheel(H5Plot* plot, double px, double py, double factor, int shift,
                             int control, int alt);
H5PLOT_API void h5plot_pan(H5Plot* plot, double dx, double dy);

/* Pane-local pixels, y downward. Returns 1 when the window moved.
 * A band under 12 pixels on either side is refused and returns 0.
 * A line scaled in y keeps that scale. The band is one fraction of the
 * pane, and the line gives that fraction of the window it was drawn in,
 * so the samples inside the band fill the pane on every scale. */
H5PLOT_API int h5plot_zoom_rect(H5Plot* plot, double x0, double y0, double x1, double y1);

/* Open the window on these data values. A bound at or below zero on a
 * logarithmic axis is clipped to the part of the axis that exists. */
H5PLOT_API void h5plot_set_range(H5Plot* plot, double x0, double x1, double y0, double y1);

/* A line on its own y axis is drawn in its own window: the common window,
 * shifted, and possibly scaled. The common extent is unchanged, so the
 * other curves stay. Turning the axis off clears the shift and the scale,
 * and the line returns to the common one. */
H5PLOT_API void h5plot_set_own_axis(H5Plot* plot, int index, int on);
H5PLOT_API int h5plot_line_count(const H5Plot* plot);
H5PLOT_API int h5plot_own_axis(const H5Plot* plot, int index);
H5PLOT_API int h5plot_own_count(const H5Plot* plot);
H5PLOT_API int h5plot_shared_count(const H5Plot* plot);

/* The line whose stroke passes closest to a pane-local pixel, or -1. */
H5PLOT_API int h5plot_nearest(const H5Plot* plot, double px, double py);

/* The drawn sample closest to a pane-local pixel. x and y are that sample's
 * values, through the axis the line was drawn on — a shifted line's own y,
 * not the common one. px and py are where it was drawn. Returns 0 when
 * nothing drawable is in hand, and when the pixel is outside the pane:
 * the gutters hold the axes, not the curve. A sample that itself lands
 * outside the pane is not a candidate. */
typedef struct H5PlotSample
{
    int line;
    double x;
    double y;
    double px;
    double py;
    unsigned char red;
    unsigned char green;
    unsigned char blue;
    unsigned char alpha;
} H5PlotSample;
H5PLOT_API int h5plot_sample(const H5Plot* plot, double px, double py, H5PlotSample* out);

/* Move one line in y by a pane-local pixel delta, and give it its own y axis
 * so the numbers beside it are the values it is drawn at. The delta is a
 * share of that line's own window, so a line that has been scaled still
 * follows the pointer. dx is ignored. */
H5PLOT_API void h5plot_shift_line(H5Plot* plot, int index, double dx, double dy);

H5PLOT_API double h5plot_view_min_x(const H5Plot* plot);
H5PLOT_API double h5plot_view_max_x(const H5Plot* plot);
H5PLOT_API double h5plot_view_min_y(const H5Plot* plot);
H5PLOT_API double h5plot_view_max_y(const H5Plot* plot);

/* The data value under a pane-local pixel. The same two questions a rectangle
 * zoom resolves its corners with, so a readout of the band and the window it
 * opens are one statement. y grows downward. */
H5PLOT_API double h5plot_data_x_at(const H5Plot* plot, double px);
H5PLOT_API double h5plot_data_y_at(const H5Plot* plot, double py);

/* Fold + project. Returns the number of points. */
H5PLOT_API int h5plot_project(H5Plot* plot);
H5PLOT_API int h5plot_point_count(const H5Plot* plot);
H5PLOT_API void h5plot_copy_points(const H5Plot* plot, double* xy);
H5PLOT_API int h5plot_run_count(const H5Plot* plot);
H5PLOT_API void h5plot_copy_runs(const H5Plot* plot, H5PlotRun* runs);

H5PLOT_API int h5plot_tick_count(const H5Plot* plot);
H5PLOT_API void h5plot_copy_ticks(const H5Plot* plot, H5PlotTick* ticks);

/* The same vertices as h5plot_copy_points, in the data's own units. A run
 * indexes either buffer. y is the line's value, including a line whose window
 * was shifted off the common axis. */
H5PLOT_API void h5plot_copy_data(const H5Plot* plot, double* xy);

/* One entry per run: the line it belongs to. */
H5PLOT_API void h5plot_copy_run_lines(const H5Plot* plot, int* lines);

H5PLOT_API int h5plot_x_log(const H5Plot* plot);
H5PLOT_API int h5plot_y_log(const H5Plot* plot);
/* This line's own y scale. A line still on the common axis reports that
 * axis, and set refuses it: the checkbox is how the common axis changes.
 * Zero and negative samples become gaps, the same way a NaN already does. */
H5PLOT_API void h5plot_set_line_y_log(H5Plot* plot, int index, int on);
H5PLOT_API int h5plot_line_y_log(const H5Plot* plot, int index);

/* The y window that line is drawn in. The common window, or its own when
 * the line has an axis of its own -- shifted, scaled, or both. */
H5PLOT_API void h5plot_line_y_range(const H5Plot* plot, int index, double* low, double* high);

/* Each line into an equal band, the first at the top, scaled to its own
 * padded span. The common y returns to the whole axis; x stays. A line a
 * logarithmic axis cannot draw is left as it was. The numbers beside a
 * stacked line are that line's values. */
H5PLOT_API void h5plot_stack_lines(H5Plot* plot);

#ifdef __cplusplus
}
#endif

#endif
