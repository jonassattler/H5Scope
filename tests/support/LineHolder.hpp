// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "gui/LineTarget.hpp"
#include "gui/PlotCore.hpp"

#include <QObject>
#include <QString>
#include <QVariantMap>

#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace h5test {

/// Something to lend a plot's lines to, in a case that is about the lending.
///
/// The borrow contract (BorrowedLines, PlotLine::values) is between a plot and
/// whatever it fills, and gui::FigureItem is the holder on screen. The cases
/// that hold the plots to that contract need two things of a holder and
/// nothing of a picture: whether it still has lines, and whether the values
/// behind them can still be read. This is the smallest holder that answers
/// both. It reads what it was lent back the way the crosshair does, through
/// plotcore::nearestSample over a pane and a window set here, so under a
/// sanitiser a freed vector is a use-after-free at the assertion rather than a
/// count that is still right.
///
/// It is what gui::PlotItem was to these cases before the figure replaced it,
/// less the drawing: the same window, the same pixels, the same answers.
class LineHolder : public QObject, public gui::LineTarget
{
public:
    void setLines(std::vector<gui::PlotLine> lines, std::vector<gui::PlotAxis> axes) override
    {
        lines_ = std::move(lines);
        axes_ = std::move(axes);
    }
    void clearLines() override
    {
        lines_.clear();
        axes_.clear();
    }
    [[nodiscard]] QObject* lineObject() override { return this; }

    [[nodiscard]] int lineCount() const { return static_cast<int>(lines_.size()); }
    [[nodiscard]] std::vector<gui::PlotLine>& lines() { return lines_; }

    /// The pane, 400 by 300 unless a case says otherwise, and the window onto
    /// the data it shows.
    void frameOver(double xMin, double xMax, double yMin, double yMax)
    {
        view.xMin = xMin;
        view.xMax = xMax;
        view.yMin = yMin;
        view.yMax = yMax;
    }

    /// The drawn sample nearest a pixel of the pane, as `{ valid, line, x, y,
    /// px, py }`.
    [[nodiscard]] QVariantMap nearestSample(double px, double py) const
    {
        QVariantMap answer;
        answer.insert(QStringLiteral("valid"), false);
        const std::optional<gui::NearestSample> found =
            gui::nearestSample(lines_, std::span<const gui::PlotAxis>(axes_), view, px, py);
        if (!found.has_value()) {
            return answer;
        }
        answer.insert(QStringLiteral("valid"), true);
        answer.insert(QStringLiteral("line"), found->line);
        answer.insert(QStringLiteral("x"), found->x);
        answer.insert(QStringLiteral("y"), found->y);
        answer.insert(QStringLiteral("px"), found->px);
        answer.insert(QStringLiteral("py"), found->py);
        return answer;
    }

    gui::PlotView view = [] {
        gui::PlotView pane;
        pane.width = 400.0;
        pane.height = 300.0;
        return pane;
    }();

private:
    std::vector<gui::PlotLine> lines_;
    std::vector<gui::PlotAxis> axes_;
};

} // namespace h5test
