// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "common/SyntheticSource.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <random>

namespace spike {
namespace {

// A generator per line rather than one stream through all of them, so that
// line 900 of a run of 1000 holds the same values whether or not lines 0..899
// were asked for first. The grid skips cells, and a benchmark whose data
// depends on which cells ran before it is not a benchmark.
std::mt19937 generatorFor(std::uint32_t seed, int series)
{
    return std::mt19937(seed * 2654435761u + static_cast<std::uint32_t>(series));
}

constexpr double kPi = 3.14159265358979323846;

} // namespace

const char* name(Shape shape)
{
    switch (shape) {
    case Shape::Sine: return "sine";
    case Shape::Noise: return "noise";
    case Shape::SpikeInAMillion: return "spike";
    case Shape::NaNRun: return "nan";
    case Shape::BigX: return "bigx";
    case Shape::WideRange: return "range";
    case Shape::NonMonotonicX: return "unsorted";
    case Shape::Constant: return "constant";
    }
    return "?";
}

bool parseShape(const std::string& text, Shape& out)
{
    for (const Shape shape : everyShape()) {
        if (text == name(shape)) {
            out = shape;
            return true;
        }
    }
    return false;
}

const std::vector<Shape>& everyShape()
{
    static const std::vector<Shape> shapes = {
        Shape::Sine,     Shape::Noise,     Shape::SpikeInAMillion,
        Shape::NaNRun,   Shape::BigX,      Shape::WideRange,
        Shape::NonMonotonicX, Shape::Constant,
    };
    return shapes;
}

SyntheticSource::SyntheticSource(Shape shape, int seriesCount, int pointsPerSeries,
                                 std::uint32_t seed)
    : shape_(shape)
    , seriesCount_(std::max(1, seriesCount))
    , pointsPerSeries_(std::max(1, pointsPerSeries))
{
    const int count = pointsPerSeries_;

    // x. Two shapes move it off the default; the rest are an origin and a step
    // because that is what the application carries.
    switch (shape_) {
    case Shape::BigX:
        // Seconds since the epoch at a millisecond step. Nothing exotic -- it
        // is what every logger in the world writes -- and it is enough to break
        // a float32 vertex: 1e9 has a spacing of 64 in float, so a 1e-3 step
        // lands sixty-four thousand consecutive samples on the same x.
        xStart_ = 1.7e9;
        xStep_ = 1e-3;
        break;
    case Shape::NonMonotonicX: {
        // A circle, walked. x returns to where it was, which is the whole
        // point: a renderer that sorts by x draws a lens, not a circle.
        xValues_.resize(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i) {
            const double t = 2.0 * kPi * static_cast<double>(i)
                             / static_cast<double>(count);
            xValues_[static_cast<std::size_t>(i)] = std::cos(t);
        }
        break;
    }
    default:
        xStart_ = 0.0;
        xStep_ = 1.0;
        break;
    }

    lines_.resize(static_cast<std::size_t>(seriesCount_));

    double low = std::numeric_limits<double>::infinity();
    double high = -std::numeric_limits<double>::infinity();

    for (int series = 0; series < seriesCount_; ++series) {
        std::vector<double>& values = lines_[static_cast<std::size_t>(series)];
        values.resize(static_cast<std::size_t>(count));
        std::mt19937 generator = generatorFor(seed, series);
        std::uniform_real_distribution<double> uniform(-1.0, 1.0);

        // A phase per series so a hundred lines are a hundred lines and not one
        // line drawn a hundred times -- which would let a renderer that
        // collapses identical geometry look faster than it is.
        const double phase = 2.0 * kPi * static_cast<double>(series)
                             / static_cast<double>(seriesCount_);
        const double amplitude = 1.0 + 0.5 * static_cast<double>(series % 7);

        for (int i = 0; i < count; ++i) {
            const double t = static_cast<double>(i) / static_cast<double>(count);
            double value = 0.0;
            switch (shape_) {
            case Shape::Sine:
            case Shape::SpikeInAMillion:
            case Shape::NaNRun:
            case Shape::BigX:
                value = amplitude * std::sin(2.0 * kPi * 8.0 * t + phase);
                break;
            case Shape::Noise:
                value = amplitude * uniform(generator);
                break;
            case Shape::WideRange:
                // Six orders of magnitude either side of one, swept.
                value = std::pow(10.0, -30.0 + 60.0 * t)
                        * (std::sin(2.0 * kPi * 4.0 * t + phase) >= 0 ? 1.0 : -1.0);
                break;
            case Shape::NonMonotonicX: {
                const double angle = 2.0 * kPi * t;
                value = amplitude * std::sin(angle + phase);
                break;
            }
            case Shape::Constant:
                value = 1.0 + static_cast<double>(series);
                break;
            }
            values[static_cast<std::size_t>(i)] = value;
        }

        if (shape_ == Shape::SpikeInAMillion) {
            // A prime-ish fraction of the way along, so it does not land on a
            // stride boundary by luck and make stride sampling look correct.
            spikeIndex_ = static_cast<int>(static_cast<long long>(count) * 37 / 100);
            if (spikeIndex_ >= count) {
                spikeIndex_ = count - 1;
            }
            values[static_cast<std::size_t>(spikeIndex_)] = amplitude * 10.0;
        }

        if (shape_ == Shape::NaNRun) {
            // A gap a twentieth of the line wide, centred. Wide enough that no
            // decimation can honestly close it.
            gapFirst_ = count * 9 / 20;
            gapLast_ = count * 11 / 20;
            for (int i = gapFirst_; i < gapLast_ && i < count; ++i) {
                values[static_cast<std::size_t>(i)]
                    = std::numeric_limits<double>::quiet_NaN();
            }
        }

        for (const double value : values) {
            if (std::isfinite(value)) {
                low = std::min(low, value);
                high = std::max(high, value);
            }
        }
    }

    // A constant line has no extent. Every axis here divides by the span
    // somewhere, so widen it by hand rather than leaving each spike to discover
    // the division on its own and disagree about the answer.
    if (!(low < high)) {
        const double centre = std::isfinite(low) ? low : 0.0;
        low = centre - 0.5;
        high = centre + 0.5;
    }
    minimum_ = low;
    maximum_ = high;
}

const std::vector<double>& SyntheticSource::line(int series) const
{
    static const std::vector<double> empty;
    if (series < 0 || series >= seriesCount_) {
        return empty;
    }
    return lines_[static_cast<std::size_t>(series)];
}

std::size_t SyntheticSource::bytes() const
{
    return static_cast<std::size_t>(totalPoints()) * sizeof(double)
           + xValues_.size() * sizeof(double);
}

} // namespace spike
