// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// The two values the plot hands out that are not doubles.
//
// They were QPointF and QColor, and those were the only reason this library
// needed Qt at all: the projection is arithmetic over doubles, and a point and
// a colour are two and four of them. Its own here, so that whatever draws the
// result -- a scene graph, a QPainter, a mesh in another language -- converts
// at its own edge and the arithmetic answers to none of them.

namespace plotcore {

/// A place, on the pane in pixels or in the data in its own units.
///
/// Double all the way, for the reason Projection.hpp gives at length: a
/// float has a spacing of 128 at an epoch timestamp, and the projection is
/// where that is decided. Spelt as QPointF was, x() and y(), so the code that
/// used one reads the same with the other.
class Point
{
public:
    constexpr Point() = default;
    constexpr Point(double x, double y) : x_(x), y_(y) {}

    [[nodiscard]] constexpr double x() const { return x_; }
    [[nodiscard]] constexpr double y() const { return y_; }
    constexpr void setX(double x) { x_ = x; }
    constexpr void setY(double y) { y_ = y; }

    [[nodiscard]] constexpr bool operator==(const Point&) const = default;

    [[nodiscard]] constexpr Point operator+(const Point& other) const
    {
        return {x_ + other.x_, y_ + other.y_};
    }
    [[nodiscard]] constexpr Point operator-(const Point& other) const
    {
        return {x_ - other.x_, y_ - other.y_};
    }
    [[nodiscard]] constexpr Point operator*(double factor) const
    {
        return {x_ * factor, y_ * factor};
    }

private:
    double x_ = 0.0;
    double y_ = 0.0;
};

/// A colour, each channel from 0 to 1, not premultiplied.
///
/// `valid` is QColor's isValid(): a line nobody has coloured yet is a line
/// without a colour, which is not the same thing as a black one, and the
/// caller asking what colour a line is should be able to tell the two apart.
struct Rgba
{
    double red = 0.0;
    double green = 0.0;
    double blue = 0.0;
    double alpha = 1.0;
    bool valid = false;

    [[nodiscard]] static constexpr Rgba of(double red, double green, double blue,
                                           double alpha = 1.0)
    {
        return Rgba{red, green, blue, alpha, true};
    }

    [[nodiscard]] constexpr bool operator==(const Rgba&) const = default;
};

} // namespace plotcore
