// SPDX-FileCopyrightText: 2026 Andreas Martin
// SPDX-License-Identifier: GPL-3.0-only

#include "gui/PlotColumns.hpp"

#include "gui/PlotLevels.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace gui {
namespace {

/// Log-y: exact positive-run split (raw scan) only while a column is this
/// short. Wider columns probe the pyramid in O(probes) steps and merge
/// all-positive spans — dense sine lobes stay gapped without an O(n) walk.
constexpr int kLogYExact = 64;
constexpr int kLogYProbes = 48;

/// First and last sample index that landed in a column during a single pass.
struct ColumnInfo
{
    long long first = -1;
    long long last = -1;
    int samples = 0;
};

double sampleX(std::span<const double> times, long long i, double indexStart, double indexStep)
{
    if (!times.empty() && i >= 0 && static_cast<std::size_t>(i) < times.size()) {
        return times[static_cast<std::size_t>(i)];
    }
    return indexStart + static_cast<double>(i) * indexStep;
}

const double* rawRow(const LinePyramid& pyramid, const double* raw, long long count)
{
    if (raw != nullptr && count > 0) {
        return raw;
    }
    if (!pyramid.empty() && pyramid.baseBucket() == 1 &&
        static_cast<long long>(pyramid.levels.front().values.size()) >= pyramid.length) {
        return pyramid.levels.front().values.data();
    }
    return nullptr;
}

double lerpY(double x0, double y0, double x1, double y1, double at)
{
    const double dx = x1 - x0;
    if (!(std::abs(dx) > 0.0) || !std::isfinite(at)) {
        return y0;
    }
    const double t = (at - x0) / dx;
    return y0 + t * (y1 - y0);
}

/// Whether `y` has a place on the y axis the stroke is built for.
bool drawableY(double y, bool yLog)
{
    return std::isfinite(y) && (!yLog || y > 0.0);
}

void emitPoint(ColumnStroke& out, double x, double y)
{
    if (!std::isfinite(x) || !std::isfinite(y)) {
        return;
    }
    if (!out.values.empty()) {
        const double px = out.xs.back();
        const double py = out.values.back();
        if (px == x && py == y) {
            return;
        }
    }
    out.xs.push_back(x);
    out.values.push_back(y);
}

/// Break the stroke so projectLine does not chord across a log-y gap.
void emitGap(ColumnStroke& out)
{
    if (out.values.empty()) {
        return;
    }
    if (!std::isfinite(out.values.back())) {
        return;
    }
    const double nan = std::numeric_limits<double>::quiet_NaN();
    out.xs.push_back(nan);
    out.values.push_back(nan);
}

bool yAt(const LinePyramid& pyramid, const double* raw, long long count, long long i, double& out)
{
    if (i < 0 || i >= (count > 0 ? count : pyramid.length)) {
        return false;
    }
    if (raw != nullptr) {
        out = raw[static_cast<std::size_t>(i)];
        return std::isfinite(out);
    }
    if (pyramid.baseBucket() == 1 && !pyramid.empty()) {
        out = pyramid.levels.front().values[static_cast<std::size_t>(i)];
        return std::isfinite(out);
    }
    const Extremes hit = extremesOver(pyramid, i, i + 1);
    if (!hit.found()) {
        return false;
    }
    out = hit.first();
    return true;
}

/// Linear axis: column index by division. Log keeps a bisect on `edges`.
int columnIndex(std::span<const double> edges, double x, bool xLog, double xMin, double invWidth,
                int columns)
{
    if (edges.size() < 2 || !std::isfinite(x) || x < edges.front() || x > edges.back()) {
        return -1;
    }
    if (x == edges.back()) {
        return columns - 1;
    }
    if (!xLog) {
        const int c = static_cast<int>((x - xMin) * invWidth);
        return std::clamp(c, 0, columns - 1);
    }
    const auto it = std::upper_bound(edges.begin(), edges.end(), x);
    const int c = static_cast<int>(it - edges.begin()) - 1;
    return std::clamp(c, 0, columns - 1);
}

long long firstAtLeast(std::span<const double> times, long long count, double indexStart,
                       double indexStep, double t)
{
    if (!times.empty()) {
        return static_cast<long long>(std::lower_bound(times.begin(), times.end(), t) -
                                      times.begin());
    }
    const double at = (t - indexStart) / indexStep;
    return std::clamp(static_cast<long long>(std::ceil(at - 1e-15)), 0LL, count);
}

long long firstAfter(std::span<const double> times, long long count, double indexStart,
                     double indexStep, double t)
{
    if (!times.empty()) {
        return static_cast<long long>(std::upper_bound(times.begin(), times.end(), t) -
                                      times.begin());
    }
    const double at = (t - indexStart) / indexStep;
    return std::clamp(static_cast<long long>(std::floor(at + 1e-15)) + 1, 0LL, count);
}

/// Raw in hand: sequential scan (Famos-sized lines stay under a millisecond).
/// No raw: pyramid extremesOver — the path that still answers when the base
/// bucket is coarser than one sample.
Extremes extremesFor(const LinePyramid& pyramid, const double* raw, long long first, long long last)
{
    if (first >= last) {
        return {};
    }
    if (raw != nullptr) {
        return extremesOf(raw, first, last);
    }
    return extremesOver(pyramid, first, last);
}

/// Assign every sample whose x falls in the pane to a column. One pass over
/// `[i0, i1)` — used for both ascending and descending index axes so the
/// column spans are the samples that actually land there, not W edge bisects.
void fillColumnSpans(std::span<const double> times, long long i0, long long i1, double indexStart,
                     double indexStep, std::span<const double> edges, bool xLog, double invWidth,
                     int columns, std::vector<ColumnInfo>& cols)
{
    cols.assign(static_cast<std::size_t>(columns), {});
    for (long long i = i0; i < i1; ++i) {
        const double x = sampleX(times, i, indexStart, indexStep);
        const int c = columnIndex(edges, x, xLog, edges.front(), invWidth, columns);
        if (c < 0) {
            continue;
        }
        ColumnInfo& col = cols[static_cast<std::size_t>(c)];
        if (col.first < 0) {
            col.first = i;
        }
        col.last = i;
        ++col.samples;
    }
}

/// Gap when the next drawable sample is not adjacent to the last one — covers
/// whole columns of non-positive y that emit nothing of their own.
void gapIfSkipped(long long nextIndex, long long& lastDrawable, ColumnStroke& out)
{
    if (lastDrawable >= 0 && nextIndex > lastDrawable + 1) {
        emitGap(out);
    }
}

/// Entry → extrema → exit for one drawable run `[first, lastEx)`.
bool emitRun(const LinePyramid& pyramid, const double* row, long long count,
             std::span<const double> times, double indexStart, double indexStep,
             std::span<const double> edges, bool xLog, bool yLog, double invWidth, int columns,
             int c, long long first, long long lastEx, const Extremes& ext, bool allowEntry,
             bool allowExit, long long& lastDrawable, ColumnStroke& out)
{
    if (!ext.found() || !drawableY(ext.first(), yLog)) {
        return false;
    }
    out.summarised = true;
    const double t0 = edges[static_cast<std::size_t>(c)];
    const double t1 = edges[static_cast<std::size_t>(c) + 1];
    const long long at0 = ext.firstAt();
    const long long at1 = ext.secondAt();
    const double y0 = ext.first();
    const double y1 = drawableY(ext.second(), yLog) ? ext.second() : y0;
    const double xExt0 = sampleX(times, at0, indexStart, indexStep);
    const double xExt1 = sampleX(times, at1, indexStart, indexStep);

    gapIfSkipped(first, lastDrawable, out);

    if (allowEntry) {
        const long long prev = first - 1;
        if (prev >= 0) {
            double yPrev = 0.0;
            if (yAt(pyramid, row, count, prev, yPrev) && drawableY(yPrev, yLog)) {
                const double xPrev = sampleX(times, prev, indexStart, indexStep);
                const int cPrev = columnIndex(edges, xPrev, xLog, edges.front(), invWidth, columns);
                if (cPrev < 0 || c - cPrev <= 1) {
                    const double yEdge = lerpY(xPrev, yPrev, xExt0, y0, t0);
                    if (drawableY(yEdge, yLog)) {
                        emitPoint(out, t0, yEdge);
                    }
                }
            }
        }
    }

    emitPoint(out, xExt0, y0);
    if (at0 != at1 && drawableY(y1, yLog)) {
        emitPoint(out, xExt1, y1);
    }
    lastDrawable = lastEx - 1;

    if (allowExit && lastEx < count) {
        double yNext = 0.0;
        if (yAt(pyramid, row, count, lastEx, yNext) && drawableY(yNext, yLog)) {
            const double xNext = sampleX(times, lastEx, indexStart, indexStep);
            const int cNext = columnIndex(edges, xNext, xLog, edges.front(), invWidth, columns);
            if (cNext < 0 || cNext - c <= 1) {
                const double yEdge = lerpY(xExt1, y1, xNext, yNext, t1);
                if (drawableY(yEdge, yLog)) {
                    emitPoint(out, t1, yEdge);
                }
            }
        }
    }
    return true;
}

/// Exact positive-run split from a raw row. Cheap only for short columns.
bool emitLogYExact(const LinePyramid& pyramid, const double* row, long long count,
                   std::span<const double> times, double indexStart, double indexStep,
                   std::span<const double> edges, bool xLog, double invWidth, int columns, int c,
                   long long first, long long lastEx, long long& lastDrawable, ColumnStroke& out)
{
    bool any = false;
    long long i = first;
    while (i < lastEx) {
        while (i < lastEx) {
            const double y = row[static_cast<std::size_t>(i)];
            if (std::isfinite(y) && y > 0.0) {
                break;
            }
            ++i;
        }
        if (i >= lastEx) {
            break;
        }
        const long long runFirst = i;
        Extremes ext;
        for (; i < lastEx; ++i) {
            const double y = row[static_cast<std::size_t>(i)];
            if (!(std::isfinite(y) && y > 0.0)) {
                break;
            }
            if (ext.lowAt < 0 || y < ext.lowest) {
                ext.lowest = y;
                ext.lowAt = i;
            }
            if (ext.highAt < 0 || y > ext.highest) {
                ext.highest = y;
                ext.highAt = i;
            }
        }
        const long long runLastEx = i;
        if (!ext.found()) {
            continue;
        }
        const bool allowEntry = (runFirst == first);
        const bool allowExit = (runLastEx == lastEx);
        if (runLastEx - runFirst == 1) {
            gapIfSkipped(runFirst, lastDrawable, out);
            emitPoint(out, sampleX(times, runFirst, indexStart, indexStep),
                      row[static_cast<std::size_t>(runFirst)]);
            lastDrawable = runFirst;
            any = true;
            continue;
        }
        if (emitRun(pyramid, row, count, times, indexStart, indexStep, edges, xLog, true, invWidth,
                    columns, c, runFirst, runLastEx, ext, allowEntry, allowExit, lastDrawable,
                    out)) {
            any = true;
        }
    }
    return any;
}

/// Dense log-y: classify O(kLogYProbes) pyramid spans, merge all-positive ones.
/// Mixed spans are gaps — lobe edges may truncate by one probe, which is stable
/// under pan and cheap (no sample walk).
bool emitLogYProbed(const LinePyramid& pyramid, const double* row, long long count,
                    std::span<const double> times, double indexStart, double indexStep,
                    std::span<const double> edges, bool xLog, double invWidth, int columns, int c,
                    long long first, long long lastEx, long long& lastDrawable, ColumnStroke& out)
{
    const long long span = lastEx - first;
    const long long step =
        std::max(1LL, (span + static_cast<long long>(kLogYProbes) - 1) /
                          static_cast<long long>(kLogYProbes));

    bool any = false;
    long long i = first;
    while (i < lastEx) {
        // Skip probes that are not strictly all-positive.
        while (i < lastEx) {
            const long long j = std::min(i + step, lastEx);
            const Extremes e = extremesFor(pyramid, row, i, j);
            if (e.found() && e.lowest > 0.0) {
                break;
            }
            i = j;
        }
        if (i >= lastEx) {
            break;
        }
        const long long runFirst = i;
        while (i < lastEx) {
            const long long j = std::min(i + step, lastEx);
            const Extremes e = extremesFor(pyramid, row, i, j);
            if (!(e.found() && e.lowest > 0.0)) {
                break;
            }
            i = j;
        }
        const long long runLastEx = i;
        const Extremes ext = extremesFor(pyramid, row, runFirst, runLastEx);
        if (!ext.found() || !(ext.lowest > 0.0)) {
            continue;
        }
        const bool allowEntry = (runFirst == first);
        const bool allowExit = (runLastEx == lastEx);
        if (emitRun(pyramid, row, count, times, indexStart, indexStep, edges, xLog, true, invWidth,
                    columns, c, runFirst, runLastEx, ext, allowEntry, allowExit, lastDrawable,
                    out)) {
            any = true;
        }
    }
    return any;
}

/// One multi-sample column.
///
/// Linear y: pyramid/raw envelope.
/// Log y: all-positive columns use that envelope; short mixed columns split
/// positive runs exactly (raw); dense mixed columns probe the pyramid so a
/// zoomed-out sine stays gapped without scanning every sample.
bool emitSummarised(const LinePyramid& pyramid, const double* row, long long count,
                    std::span<const double> times, double indexStart, double indexStep,
                    std::span<const double> edges, bool xLog, bool yLog, double invWidth,
                    int columns, int c, long long first, long long lastEx, long long& lastDrawable,
                    ColumnStroke& out)
{
    if (!yLog) {
        const Extremes ext = extremesFor(pyramid, row, first, lastEx);
        return emitRun(pyramid, row, count, times, indexStart, indexStep, edges, xLog, false,
                       invWidth, columns, c, first, lastEx, ext, true, true, lastDrawable, out);
    }

    const Extremes whole = extremesFor(pyramid, row, first, lastEx);
    if (!whole.found()) {
        return false;
    }
    if (whole.highest <= 0.0) {
        return false; // nothing drawable
    }
    if (whole.lowest > 0.0) {
        // Entire column positive: one envelope (raw scan or pyramid).
        return emitRun(pyramid, row, count, times, indexStart, indexStep, edges, xLog, true,
                       invWidth, columns, c, first, lastEx, whole, true, true, lastDrawable, out);
    }

    // Mixed: sign change inside the column.
    const long long span = lastEx - first;
    if (row != nullptr && span <= kLogYExact) {
        return emitLogYExact(pyramid, row, count, times, indexStart, indexStep, edges, xLog,
                             invWidth, columns, c, first, lastEx, lastDrawable, out);
    }
    return emitLogYProbed(pyramid, row, count, times, indexStart, indexStep, edges, xLog, invWidth,
                          columns, c, first, lastEx, lastDrawable, out);
}

} // namespace

void columnEdges(double xMin, double xMax, int columns, bool xLog, std::vector<double>& edges)
{
    edges.clear();
    columns = std::max(columns, 1);
    if (!(xMax > xMin) || !std::isfinite(xMin) || !std::isfinite(xMax)) {
        return;
    }
    edges.resize(static_cast<std::size_t>(columns) + 1);
    if (xLog) {
        if (!(xMin > 0.0) || !(xMax > 0.0)) {
            edges.clear();
            return;
        }
        const double u0 = std::log2(xMin);
        const double u1 = std::log2(xMax);
        for (int i = 0; i <= columns; ++i) {
            const double u = u0 + (u1 - u0) * (static_cast<double>(i) / static_cast<double>(columns));
            edges[static_cast<std::size_t>(i)] = std::exp2(u);
        }
        return;
    }
    for (int i = 0; i <= columns; ++i) {
        edges[static_cast<std::size_t>(i)] =
            xMin + (xMax - xMin) * (static_cast<double>(i) / static_cast<double>(columns));
    }
}

int columnOf(std::span<const double> edges, double x)
{
    if (edges.size() < 2 || !std::isfinite(x) || x < edges.front() || x > edges.back()) {
        return -1;
    }
    if (x == edges.back()) {
        return static_cast<int>(edges.size()) - 2;
    }
    const auto it = std::upper_bound(edges.begin(), edges.end(), x);
    const int c = static_cast<int>(it - edges.begin()) - 1;
    return std::clamp(c, 0, static_cast<int>(edges.size()) - 2);
}

bool rasterColumns(const LinePyramid& pyramid, const double* raw, long long count,
                   std::span<const double> times, double indexStart, double indexStep, double xMin,
                   double xMax, int columns, bool xLog, bool yLog, ColumnStroke& out)
{
    out.values.clear();
    out.xs.clear();
    out.summarised = false;
    if (pyramid.empty() || columns < 1 || !std::isfinite(indexStart) || !std::isfinite(indexStep) ||
        !(std::abs(indexStep) > 0.0)) {
        return false;
    }
    if (count <= 0) {
        count = pyramid.length;
    }
    if (!times.empty() && static_cast<long long>(times.size()) != count) {
        return false;
    }

    std::vector<double> edges;
    columnEdges(xMin, xMax, columns, xLog, edges);
    if (edges.size() != static_cast<std::size_t>(columns) + 1) {
        return false;
    }

    const double* row = rawRow(pyramid, raw, count);
    const double span = edges.back() - edges.front();
    const double invWidth =
        (!xLog && span > 0.0) ? static_cast<double>(columns) / span : 0.0;
    long long lastDrawable = -1;

    auto appendSample = [&](long long i) {
        double y = 0.0;
        if (!yAt(pyramid, row, count, i, y) || !drawableY(y, yLog)) {
            return;
        }
        gapIfSkipped(i, lastDrawable, out);
        emitPoint(out, sampleX(times, i, indexStart, indexStep), y);
        lastDrawable = i;
    };

    auto emitColumn = [&](int c, long long first, long long lastEx) {
        if (lastEx <= first) {
            return;
        }
        if (lastEx - first == 1) {
            appendSample(first);
            return;
        }
        emitSummarised(pyramid, row, count, times, indexStart, indexStep, edges, xLog, yLog,
                       invWidth, columns, c, first, lastEx, lastDrawable, out);
    };

    // One pass over the samples that can hit the pane. Ascending x (index step
    // or sorted times) bounds the walk with two edge lookups; a descending
    // index axis walks the whole line (columnIndex rejects what falls outside).
    long long i0 = 0;
    long long i1 = count;
    if ((!times.empty()) || indexStep > 0.0) {
        i0 = firstAtLeast(times, count, indexStart, indexStep, edges.front());
        i1 = firstAfter(times, count, indexStart, indexStep, edges.back());
    }

    std::vector<ColumnInfo> cols;
    fillColumnSpans(times, i0, i1, indexStart, indexStep, edges, xLog, invWidth, columns, cols);
    for (int c = 0; c < columns; ++c) {
        const ColumnInfo& col = cols[static_cast<std::size_t>(c)];
        if (col.samples <= 0 || col.first < 0) {
            continue;
        }
        emitColumn(c, col.first, col.last + 1);
    }

    while (!out.values.empty() && !std::isfinite(out.values.back())) {
        out.values.pop_back();
        out.xs.pop_back();
    }
    return !out.values.empty();
}

} // namespace gui
