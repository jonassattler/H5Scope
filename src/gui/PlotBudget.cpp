// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "PlotBudget.hpp"

#include <QByteArray>

#include <algorithm>

namespace gui {
namespace {

constexpr long long kMegabyte = 1024LL * 1024LL;

} // namespace

PlotBudget& PlotBudget::instance()
{
    static PlotBudget budget;
    return budget;
}

void PlotBudget::setAppetite(Appetite appetite)
{
    if (appetite_ == appetite) {
        return;
    }
    appetite_ = appetite;
    emit changed();
}

PlotBudget::PlotBudget()
{
    // The environment override, read once here rather than into a static inside
    // total(): the same number is now settable at runtime, and two ways of
    // saying one thing is how they come to disagree.
    const QByteArray asked = qgetenv("H5SCOPE_PLOT_BUDGET_MB");
    if (asked.isEmpty()) {
        return;
    }
    bool ok = false;
    const long long megabytes = asked.toLongLong(&ok);
    pinned_ = ok && megabytes > 0 ? megabytes * kMegabyte : 0LL;
}

void PlotBudget::setPinnedTotal(long long bytes)
{
    const long long wanted = std::max(bytes, 0LL);
    if (pinned_ == wanted) {
        return;
    }
    pinned_ = wanted;
    emit changed();
}

long long PlotBudget::total() const
{
    const long long bytes =
        pinned_ > 0 ? pinned_ : plotcore::allowance(appetite_, physicalMemory());
    return bytes / static_cast<long long>(sizeof(double));
}

long long PlotBudget::share() const
{
    return total() / std::max(plots_, 1);
}

void PlotBudget::join()
{
    ++plots_;
    emit changed();
}

void PlotBudget::leave()
{
    plots_ = std::max(plots_ - 1, 0);
    emit changed();
}

} // namespace gui
