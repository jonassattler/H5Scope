// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "BorrowedLines.hpp"

#include <QObject>

#include <utility>

namespace gui {

BorrowedLines::~BorrowedLines()
{
    release();
}

LineTarget* BorrowedLines::held() const
{
    return drawingObject_ != nullptr ? drawing_ : nullptr;
}

void BorrowedLines::lend(LineTarget* target, std::vector<PlotLine> lines, const PlotAxis& axis)
{
    if (target == nullptr) {
        return;
    }
    // A detached custom tab is drawn by a second surface, and the one it left
    // behind in the tab bar still held what it had last been handed.
    if (LineTarget* holding = held(); holding != nullptr && holding != target) {
        holding->clearLines();
    }
    target->setLines(std::move(lines), axis);
    drawing_ = target;
    drawingObject_ = target->lineObject();
    retired_.clear();
}

void BorrowedLines::release()
{
    if (LineTarget* holding = held(); holding != nullptr) {
        holding->clearLines();
    }
    drawing_ = nullptr;
    drawingObject_ = nullptr;
    retired_.clear();
}

void BorrowedLines::retire(std::vector<double>& values)
{
    if (values.empty() || held() == nullptr) {
        values.clear();
        return;
    }
    retired_.push_back(std::move(values));
    values.clear();
}

long long BorrowedLines::retiredDoubles() const
{
    long long held = 0;
    for (const std::vector<double>& values : retired_) {
        held += static_cast<long long>(values.size());
    }
    return held;
}

} // namespace gui
