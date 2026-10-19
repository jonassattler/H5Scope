// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Whatever a plot lends its lines to.
//
// A plot hands its lines over as pointers into its own vectors (PlotLine
// borrows), and BorrowedLines is the record of who is holding them: the
// FigureItem that draws them, or a test standing in for one. The contract is
// the same for any holder: take the lines, and let go of them when told.

#include "gui/PlotCore.hpp"

#include <vector>

class QObject;

namespace gui {

class LineTarget
{
public:
    LineTarget() = default;
    LineTarget(const LineTarget&) = delete;
    LineTarget& operator=(const LineTarget&) = delete;
    LineTarget(LineTarget&&) = delete;
    LineTarget& operator=(LineTarget&&) = delete;
    virtual ~LineTarget() = default;

    /// Hand over the lines to draw and the x axes they are drawn against: one
    /// for every line, or one each -- a custom tab's line may name a time base
    /// of its own. Both are borrowed: the owner calls clearLines() before it
    /// touches them again.
    virtual void setLines(std::vector<PlotLine> lines, std::vector<PlotAxis> axes) = 0;
    /// Draw nothing, and stop reading whatever was handed over.
    virtual void clearLines() = 0;
    /// The item itself, which BorrowedLines watches: a QML scene tears its
    /// items down without telling the plot.
    [[nodiscard]] virtual QObject* lineObject() = 0;
};

} // namespace gui
