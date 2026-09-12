// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "common/ImageChecks.hpp"

#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QHash>
#include <QtCore/QString>

#include <algorithm>
#include <cstdlib>

namespace spike {
namespace {

/// How far apart the strongest and weakest channels are. Zero for any grey.
inline int chroma(QRgb pixel)
{
    const int high = std::max({qRed(pixel), qGreen(pixel), qBlue(pixel)});
    const int low = std::min({qRed(pixel), qGreen(pixel), qBlue(pixel)});
    return high - low;
}

inline bool near(QRgb a, QRgb b, int tolerance)
{
    return std::abs(qRed(a) - qRed(b)) <= tolerance
           && std::abs(qGreen(a) - qGreen(b)) <= tolerance
           && std::abs(qBlue(a) - qBlue(b)) <= tolerance;
}

} // namespace

QRgb dominantColour(const QImage& image)
{
    if (image.isNull()) {
        return qRgb(0, 0, 0);
    }
    const QImage rgb = image.convertToFormat(QImage::Format_RGB32);
    QHash<QRgb, int> counts;
    // Every fourth pixel in each direction. A modal colour does not need every
    // sample, and a grab of a 1400x900 plot is 1.26 million of them.
    for (int y = 0; y < rgb.height(); y += 4) {
        const auto* line = reinterpret_cast<const QRgb*>(rgb.constScanLine(y));
        for (int x = 0; x < rgb.width(); x += 4) {
            ++counts[line[x]];
        }
    }
    QRgb best = qRgb(0, 0, 0);
    int most = -1;
    for (auto it = counts.constBegin(); it != counts.constEnd(); ++it) {
        if (it.value() > most) {
            most = it.value();
            best = it.key();
        }
    }
    return best;
}

InkProfile profile(const QImage& image, int tolerance, int minChroma)
{
    InkProfile ink;
    if (image.isNull()) {
        return ink;
    }
    const QImage rgb = image.convertToFormat(QImage::Format_RGB32);
    ink.width = rgb.width();
    ink.height = rgb.height();
    ink.background = dominantColour(rgb);
    ink.inkPerColumn.assign(static_cast<std::size_t>(ink.width), 0);
    ink.topInkRow.assign(static_cast<std::size_t>(ink.width), -1);
    ink.bottomInkRow.assign(static_cast<std::size_t>(ink.width), -1);

    for (int y = 0; y < ink.height; ++y) {
        const auto* line = reinterpret_cast<const QRgb*>(rgb.constScanLine(y));
        for (int x = 0; x < ink.width; ++x) {
            if (near(line[x], ink.background, tolerance)
                || chroma(line[x]) < minChroma) {
                continue;
            }
            const auto column = static_cast<std::size_t>(x);
            ++ink.inkPerColumn[column];
            if (ink.topInkRow[column] < 0) {
                ink.topInkRow[column] = y;
            }
            ink.bottomInkRow[column] = y;
        }
    }
    return ink;
}

int InkProfile::columnsWithInk() const
{
    return static_cast<int>(
        std::count_if(inkPerColumn.begin(), inkPerColumn.end(),
                      [](int count) { return count > 0; }));
}

int InkProfile::longestEmptyRun() const
{
    int best = 0;
    int run = 0;
    for (const int count : inkPerColumn) {
        run = count > 0 ? 0 : run + 1;
        best = std::max(best, run);
    }
    return best;
}

int InkProfile::longestEmptyRunStart() const
{
    int best = 0;
    int bestStart = -1;
    int run = 0;
    for (std::size_t x = 0; x < inkPerColumn.size(); ++x) {
        run = inkPerColumn[x] > 0 ? 0 : run + 1;
        if (run > best) {
            best = run;
            bestStart = static_cast<int>(x) - run + 1;
        }
    }
    return bestStart;
}

int distinctColours(const QImage& image, int step, int tolerance, int minChroma)
{
    if (image.isNull() || step <= 0) {
        return 0;
    }
    const QImage rgb = image.convertToFormat(QImage::Format_RGB32);
    const QRgb background = dominantColour(rgb);
    QHash<int, int> buckets;
    for (int y = 0; y < rgb.height(); ++y) {
        const auto* line = reinterpret_cast<const QRgb*>(rgb.constScanLine(y));
        for (int x = 0; x < rgb.width(); ++x) {
            const QRgb pixel = line[x];
            if (near(pixel, background, tolerance) || chroma(pixel) < minChroma) {
                continue;
            }
            // Quantised, because antialiasing blends every stroke towards the
            // ground and would otherwise report a distinct colour per pixel.
            const int key = (qRed(pixel) / step) * 4096 + (qGreen(pixel) / step) * 64
                            + (qBlue(pixel) / step);
            ++buckets[key];
        }
    }
    // A bucket holding a handful of pixels is a blend between two strokes, not
    // a stroke. Ten pixels is well under a line's own footprint across a plot
    // and well over what a crossing produces.
    int distinct = 0;
    for (auto it = buckets.constBegin(); it != buckets.constEnd(); ++it) {
        if (it.value() >= 10) {
            ++distinct;
        }
    }
    return distinct;
}

double coverage(const InkProfile& ink)
{
    if (ink.width <= 0 || ink.height <= 0) {
        return 0.0;
    }
    long long inked = 0;
    for (const int count : ink.inkPerColumn) {
        inked += count;
    }
    return static_cast<double>(inked)
           / (static_cast<double>(ink.width) * static_cast<double>(ink.height));
}

int longestFlatRun(const InkProfile& ink, int first, int last)
{
    const int from = std::max(0, first);
    const int to = std::min(static_cast<int>(ink.topInkRow.size()) - 1, last);
    int best = 0;
    int run = 0;
    int previous = -2;
    for (int x = from; x <= to; ++x) {
        const int top = ink.topInkRow[static_cast<std::size_t>(x)];
        if (top < 0) {
            run = 0;
            previous = -2;
            continue;
        }
        run = (top == previous) ? run + 1 : 1;
        previous = top;
        best = std::max(best, run);
    }
    return best;
}

bool writePng(const QImage& image, const QString& path)
{
    if (image.isNull() || path.isEmpty()) {
        return false;
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    return image.save(path, "PNG");
}

} // namespace spike
