// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "CustomPlotSet.hpp"

#include <QSet>

#include <algorithm>

namespace gui {

CustomPlotSet::CustomPlotSet(QObject* parent)
    : QAbstractListModel(parent), lookup_(this)
{
}

int CustomPlotSet::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(plots_.size());
}

QVariant CustomPlotSet::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0
        || index.row() >= static_cast<int>(plots_.size())) {
        return {};
    }
    const auto at = static_cast<std::size_t>(index.row());
    switch (role) {
    case NameRole:
        return plots_[at]->name();
    case DetachedRole:
        return static_cast<bool>(detached_[at]);
    default:
        return {};
    }
}

QHash<int, QByteArray> CustomPlotSet::roleNames() const
{
    return {{NameRole, "name"}, {DetachedRole, "detached"}};
}

CustomPlot* CustomPlotSet::active() const
{
    return plotAt(activeIndex_);
}

void CustomPlotSet::setActiveIndex(int index)
{
    const int clamped =
        (index >= 0 && index < static_cast<int>(plots_.size())) ? index : -1;
    if (activeIndex_ == clamped) {
        return;
    }
    activeIndex_ = clamped;
    emit activeIndexChanged();
}

bool CustomPlotSet::taken(const QString& name, int except) const
{
    for (std::size_t i = 0; i < plots_.size(); ++i) {
        if (static_cast<int>(i) != except && plots_[i]->name() == name) {
            return true;
        }
    }
    return false;
}

QString CustomPlotSet::freeName() const
{
    // The lowest number not in use rather than one past the highest, so
    // closing "Custom 2" and making another gives "Custom 2" back instead of
    // counting away from the tabs actually on screen.
    for (int n = 1;; ++n) {
        const QString candidate = tr("Custom %1").arg(n);
        if (!taken(candidate, -1)) {
            return candidate;
        }
    }
}

int CustomPlotSet::addPlot()
{
    const int row = static_cast<int>(plots_.size());
    beginInsertRows({}, row, row);
    auto* plot = new CustomPlot(freeName(), &lookup_, this);
    connect(plot, &CustomPlot::nameChanged, this, [this] { emit namesChanged(); });
    connect(plot, &CustomPlot::notice, this,
            [this](const QString& message) { emit notice(message); });
    // Forwarded with the plot's position on it, because what answers the
    // question is addDatasetTo and that takes one.
    connect(plot, &CustomPlot::crowding, this,
            [this, plot](const QString& path, int lines) {
                emit crowdingWarned(indexOfName(plot->name()), path, lines);
            });
    plots_.push_back(plot);
    detached_.push_back(false);
    endInsertRows();
    emit countChanged();
    return row;
}

void CustomPlotSet::removePlot(int index)
{
    if (index < 0 || index >= static_cast<int>(plots_.size())) {
        return;
    }
    beginRemoveRows({}, index, index);
    CustomPlot* going = plots_[static_cast<std::size_t>(index)];
    plots_.erase(plots_.begin() + index);
    detached_.erase(detached_.begin() + index);
    endRemoveRows();
    // Deleted late rather than here: a view still bound to it is torn down on
    // the same turn of the event loop, and a binding that reads a destroyed
    // object first is a warning at best.
    going->deleteLater();

    if (activeIndex_ >= static_cast<int>(plots_.size())) {
        activeIndex_ = static_cast<int>(plots_.size()) - 1;
        emit activeIndexChanged();
    }
    emit countChanged();
}

void CustomPlotSet::movePlot(int from, int to)
{
    const int size = static_cast<int>(plots_.size());
    if (from < 0 || from >= size || to < 0 || to >= size || from == to) {
        return;
    }
    // Qt measures the destination before the row comes out, so a move down is
    // one further than where it lands.
    if (!beginMoveRows({}, from, from, {}, to > from ? to + 1 : to)) {
        return;
    }
    CustomPlot* carried = plots_[static_cast<std::size_t>(from)];
    const bool wasDetached = detached_[static_cast<std::size_t>(from)];
    plots_.erase(plots_.begin() + from);
    detached_.erase(detached_.begin() + from);
    plots_.insert(plots_.begin() + to, carried);
    detached_.insert(detached_.begin() + to, wasDetached);
    endMoveRows();

    // The tab the reader was looking at is the tab they are still looking at,
    // wherever it has landed in the strip.
    if (activeIndex_ == from) {
        activeIndex_ = to;
        emit activeIndexChanged();
    } else if (activeIndex_ > from && activeIndex_ <= to) {
        --activeIndex_;
        emit activeIndexChanged();
    } else if (activeIndex_ < from && activeIndex_ >= to) {
        ++activeIndex_;
        emit activeIndexChanged();
    }
}

CustomPlot* CustomPlotSet::plotAt(int index) const
{
    if (index < 0 || index >= static_cast<int>(plots_.size())) {
        return nullptr;
    }
    return plots_[static_cast<std::size_t>(index)];
}

int CustomPlotSet::indexOfName(const QString& name) const
{
    for (std::size_t i = 0; i < plots_.size(); ++i) {
        if (plots_[i]->name() == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

QString CustomPlotSet::setName(int index, const QString& name)
{
    CustomPlot* plot = plotAt(index);
    if (plot == nullptr) {
        return tr("there is no such plot");
    }
    const QString wanted = name.trimmed();
    if (wanted.isEmpty()) {
        return tr("a plot needs a name");
    }
    if (wanted == plot->name()) {
        return {};
    }
    if (taken(wanted, index)) {
        // Refused rather than made unique behind the reader's back. Two tabs
        // called the same thing would make every menu that lists them by name
        // -- and there are three -- ambiguous, and silently appending a number
        // would be this deciding what they meant.
        return tr("another plot is already called \"%1\"").arg(wanted);
    }
    plot->setName(wanted);
    const QModelIndex row = this->index(index, 0);
    emit dataChanged(row, row, {NameRole});
    return {};
}

void CustomPlotSet::setDetached(int index, bool detached)
{
    if (index < 0 || index >= static_cast<int>(plots_.size())) {
        return;
    }
    const auto at = static_cast<std::size_t>(index);
    if (detached_[at] == detached) {
        return;
    }
    detached_[at] = detached;
    const QModelIndex row = this->index(index, 0);
    emit dataChanged(row, row, {DetachedRole});
    if (detached && activeIndex_ == index) {
        // It has left the strip, so the strip is no longer showing it.
        activeIndex_ = -1;
        emit activeIndexChanged();
    }
}

bool CustomPlotSet::detached(int index) const
{
    if (index < 0 || index >= static_cast<int>(detached_.size())) {
        return false;
    }
    return detached_[static_cast<std::size_t>(index)];
}

void CustomPlotSet::addDatasetTo(int index, const QString& path, bool confirmed)
{
    if (CustomPlot* plot = plotAt(index); plot != nullptr) {
        plot->addDataset(path, confirmed);
    }
}

QString CustomPlotSet::uniqueName(const QString& wanted, int except) const
{
    const QString trimmed = wanted.trimmed();
    if (trimmed.isEmpty() || !taken(trimmed, except)) {
        return trimmed;
    }
    for (int n = 2;; ++n) {
        const QString candidate = QStringLiteral("%1 %2").arg(trimmed).arg(n);
        if (!taken(candidate, except)) {
            return candidate;
        }
    }
}

void CustomPlotSet::setTimeSeriesOf(int index, const QString& path)
{
    CustomPlot* plot = plotAt(index);
    if (plot == nullptr) {
        return;
    }
    lookup_.resolve({path}, [this, plot, path] {
        const PathFacts* facts = lookup_.facts(path);
        if (facts == nullptr) {
            return;
        }
        if (!facts->usable) {
            emit notice(facts->problem);
            return;
        }
        // A vector is its own time base. Anything else is ambiguous, and the
        // line taken is the first one -- which is the line the plot tab would
        // have drawn first, so the reader gets the curve they were already
        // looking at rather than a different one.
        QString written = path;
        if (facts->shape.size() > 1) {
            QStringList parts;
            for (std::size_t d = 0; d + 1 < facts->shape.size(); ++d) {
                parts << QStringLiteral("0");
            }
            parts << QStringLiteral(":");
            written = path + QStringLiteral("[") + parts.join(QStringLiteral(", "))
                      + QStringLiteral("]");
        }
        plot->setXExpression(written);
        plot->setXMode(CustomPlot::Dataset);
    });
}

QString CustomPlotSet::saveView(const QString& name, int index,
                                const QVariantMap& settings)
{
    CustomPlot* plot = plotAt(index);
    if (plot == nullptr) {
        return tr("there is no such plot");
    }
    const QString wanted = name.trimmed();
    if (wanted.isEmpty()) {
        return tr("a view needs a name");
    }

    // Saving over a view of the same name replaces it, which is what a reader
    // typing a name they have used before means. The order is kept: a view
    // updated in place stays where they last saw it in the list.
    if (!views_.contains(wanted)) {
        viewOrder_.append(wanted);
    }
    views_.insert(wanted, View{plot->state(), settings});
    emit viewsChanged();
    return {};
}

void CustomPlotSet::removeView(const QString& name)
{
    if (views_.remove(name) == 0) {
        return;
    }
    viewOrder_.removeAll(name);
    emit viewsChanged();
}

void CustomPlotSet::checkView(const QString& name)
{
    const auto held = views_.constFind(name);
    if (held == views_.constEnd()) {
        emit viewChecked(name, 0, {});
        return;
    }

    // Everything the view names, whether or not this session has ever looked
    // at it. Checking against the cache alone would report a view of a file
    // nobody has browsed as perfect, which is exactly the case the warning is
    // for.
    const QVariantMap state = held.value().plot;
    QStringList expressions;
    for (const QVariant& row : state.value(QStringLiteral("entries")).toList()) {
        expressions.append(
            row.toMap().value(QStringLiteral("expression")).toString());
    }
    if (state.value(QStringLiteral("xMode")).toString()
        == QStringLiteral("dataset")) {
        const QString written =
            state.value(QStringLiteral("xExpression")).toString();
        if (!written.trimmed().isEmpty()) {
            expressions.append(written);
        }
    }

    QStringList paths;
    for (const QString& written : expressions) {
        const Expression parts = splitExpression(written);
        if (parts.valid() && !paths.contains(parts.path)) {
            paths.append(parts.path);
        }
    }

    lookup_.resolve(paths, [this, name, expressions] {
        QStringList reasons;
        for (const QString& written : expressions) {
            const QString problem = expressionProblem(written, lookup_);
            if (!problem.isEmpty()) {
                reasons.append(tr("%1 — %2").arg(written, problem));
            }
        }
        emit viewChecked(name, static_cast<int>(reasons.size()), reasons);
    });
}

void CustomPlotSet::restoreView(const QString& name, int index)
{
    const auto held = views_.constFind(name);
    CustomPlot* plot = plotAt(index);
    if (held == views_.constEnd() || plot == nullptr) {
        return;
    }
    plot->setState(held.value().plot);

    // The title travels with the view, because a view *is* an arrangement and
    // what it is called is part of one. It nearly always collides with the tab
    // it was saved from -- which is still open under that name -- so it takes
    // the first free variant rather than being refused, and says so when it
    // had to.
    const QString wanted =
        held.value().plot.value(QStringLiteral("name")).toString().trimmed();
    if (!wanted.isEmpty() && wanted != plot->name()) {
        const QString given = uniqueName(wanted, index);
        plot->setName(given);
        const QModelIndex row = this->index(index, 0);
        emit dataChanged(row, row, {NameRole});
        if (given != wanted) {
            emit notice(tr("another plot is called \"%1\", so this one is "
                           "\"%2\"")
                            .arg(wanted, given));
        }
    }

    emit viewRestored(index, held.value().settings);
}

void CustomPlotSet::clear()
{
    lookup_.clear();

    const bool hadViews = !viewOrder_.isEmpty();
    views_.clear();
    viewOrder_.clear();
    if (hadViews) {
        emit viewsChanged();
    }

    if (plots_.empty()) {
        return;
    }
    beginResetModel();
    for (CustomPlot* plot : plots_) {
        plot->deleteLater();
    }
    plots_.clear();
    detached_.clear();
    endResetModel();
    activeIndex_ = -1;
    emit activeIndexChanged();
    emit countChanged();
}

} // namespace gui
