// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "BorrowedLines.hpp"

#include "PlotItem.hpp"

#include <utility>

namespace gui {

BorrowedLines::~BorrowedLines()
{
    release();
}

void BorrowedLines::lend(PlotItem* target, std::vector<PlotLine> lines, const PlotAxis& axis)
{
    if (target == nullptr) {
        return;
    }
    // A detached custom tab is drawn by a second surface, and the one it left
    // behind in the tab bar still held what it had last been handed.
    if (drawing_ != nullptr && drawing_ != target) {
        drawing_->clear();
    }
    target->setLines(std::move(lines), axis);
    drawing_ = target;
    retired_.clear();
}

void BorrowedLines::release()
{
    if (drawing_ != nullptr) {
        drawing_->clear();
        drawing_ = nullptr;
    }
    retired_.clear();
}

void BorrowedLines::retire(std::vector<double>& values)
{
    if (values.empty() || drawing_ == nullptr) {
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
