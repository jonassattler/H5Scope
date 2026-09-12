// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "CustomPlot.hpp"

#include "H5Session.hpp"
#include "h5core/Dataset.hpp"
#include "h5core/Error.hpp"
#include "h5core/File.hpp"
#include "postproc/Array.hpp"
#include "postproc/Operations.hpp"
#include "postproc/Pipeline.hpp"

#include <QPointF>
#include <QtGraphs/QXYSeries>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <utility>

namespace gui {

namespace {

/// What the job is asked for: one line per expression, the time base first
/// when there is one.
struct Ask {
    QString expression;
};

/// What it hands back, alongside the facts it learned on the way.
struct Answer {
    QString problem;
    std::vector<double> values;
    int stride = 1;
    int sourceLength = 0;
};

struct Reply {
    std::vector<Answer> lines;
    std::vector<std::pair<QString, PathFacts>> learned;
};

/// Read one line. On the HDF5 thread; `opened` is this job's own cache of open
/// datasets, so several entries over one dataset open it once.
[[nodiscard]] Answer
readLine(h5core::File* file, const QString& expression,
         std::map<QString, std::shared_ptr<h5core::Dataset>>& opened,
         std::map<QString, PathFacts>& facts)
{
    Answer answer;

    const Expression parts = splitExpression(expression);
    if (!parts.valid()) {
        answer.problem = parts.error;
        return answer;
    }

    // The facts, once per path per job. lookupFacts opens the dataset to read
    // them, and this then opens it again to read the values -- which is one
    // extra H5Dopen per distinct path per refresh and not per line, and is
    // what buys the session's one open dataset staying the selected one.
    auto known = facts.find(parts.path);
    if (known == facts.end()) {
        known = facts.emplace(parts.path, lookupFacts(file, parts.path)).first;
    }
    if (!known->second.usable) {
        answer.problem = known->second.problem;
        return answer;
    }

    std::vector<std::vector<hsize_t>> indices;
    std::vector<bool> drop;
    if (!resolveLine(parts.subscript, known->second.shape, indices, drop,
                     answer.problem)) {
        return answer;
    }

    for (std::size_t d = 0; d < indices.size(); ++d) {
        if (d >= drop.size() || !drop[d]) {
            answer.sourceLength = static_cast<int>(indices[d].size());
            break;
        }
    }
    answer.stride = thinToPoints(indices, drop, CustomPlot::kMaxPoints);

    auto held = opened.find(parts.path);
    if (held == opened.end()) {
        try {
            held = opened
                       .emplace(parts.path,
                                std::make_shared<h5core::Dataset>(
                                    *file, parts.path.toStdString()))
                       .first;
        } catch (const h5core::H5Error& error) {
            answer.problem = QString::fromStdString(error.summary());
            return answer;
        }
    }

    const postproc::ArrayResult read =
        postproc::read(*held->second, indices, drop);
    if (!read.ok()) {
        answer.problem = read.error;
        return answer;
    }

    const postproc::Array& array = read.array;
    const hsize_t count = array.size();
    answer.values.reserve(static_cast<std::size_t>(count));
    for (hsize_t i = 0; i < count; ++i) {
        answer.values.push_back(array.at({i}));
    }
    return answer;
}

} // namespace

CustomPlot::CustomPlot(QString name, DatasetLookup* lookup, QObject* parent)
    : QAbstractListModel(parent), name_(std::move(name)), lookup_(lookup)
{
    coalesce_.setSingleShot(true);
    coalesce_.setInterval(0);
    connect(&coalesce_, &QTimer::timeout, this, &CustomPlot::refresh);

    // A path that was unknown while the reader was typing is known now, so the
    // rows re-check themselves against it. Nothing is re-read: the error roles
    // are recomputed from the cache.
    if (lookup_ != nullptr) {
        connect(lookup_, &DatasetLookup::changed, this, [this] {
            if (!entries_.empty()) {
                emit dataChanged(index(0, 0),
                                 index(static_cast<int>(entries_.size()) - 1, 0),
                                 {ErrorRole});
            }
        });
    }
}

int CustomPlot::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(entries_.size());
}

QVariant CustomPlot::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0
        || index.row() >= static_cast<int>(entries_.size())) {
        return {};
    }
    const Entry& entry = entries_[static_cast<std::size_t>(index.row())];
    switch (role) {
    case ExpressionRole:
        return entry.expression;
    case AliasRole:
        return entry.alias;
    case ErrorRole:
        return entry.problem;
    case PointsRole:
        return static_cast<int>(entry.values.size());
    case SourcePointsRole:
        return entry.sourceLength;
    case ScalingRole:
        return static_cast<int>(entry.scaling);
    case ScalableRole:
        return scalable(entry);
    case DrawnRole:
        return entry.drawn;
    default:
        return {};
    }
}

QHash<int, QByteArray> CustomPlot::roleNames() const
{
    return {{ExpressionRole, "expression"},
            {AliasRole, "alias"},
            {ErrorRole, "error"},
            {PointsRole, "points"},
            {SourcePointsRole, "sourcePoints"},
            {ScalingRole, "scaling"},
            {ScalableRole, "scalable"},
            {DrawnRole, "drawn"}};
}

void CustomPlot::setName(QString name)
{
    if (name_ == name) {
        return;
    }
    name_ = std::move(name);
    emit nameChanged();
}

void CustomPlot::setXMode(XMode mode)
{
    if (xMode_ == mode) {
        return;
    }
    xMode_ = mode;
    // Index means the element's own position and nothing else: 0, 1, 2 ... A
    // reader who stated a range, looked at it, and went back to the index
    // would otherwise still be looking at that range, because the surface
    // stops pushing a start and a step down here rather than pushing the
    // default ones -- so the last two it pushed would simply stay.
    if (xMode_ == Index) {
        xStart_ = 0.0;
        xStep_ = 1.0;
        emit xAxisChanged();
    }
    emit xSourceChanged();
    // Every entry's x moves, and in Dataset mode there is a line to read that
    // was not being read before.
    invalidate();
}

void CustomPlot::setXExpression(const QString& text)
{
    if (xExpression_ == text) {
        return;
    }
    xExpression_ = text;
    emit xSourceChanged();
    invalidate();
}

QString CustomPlot::xExpressionError(const QString& text) const
{
    if (text.trimmed().isEmpty()) {
        return {};
    }
    return lookup_ == nullptr ? QString() : expressionProblem(text, *lookup_);
}

int CustomPlot::addExpression(const QString& text)
{
    const int row = static_cast<int>(entries_.size());
    beginInsertRows({}, row, row);
    Entry entry;
    entry.expression = text.trimmed();
    entries_.push_back(std::move(entry));
    endInsertRows();
    invalidate();
    return row;
}

void CustomPlot::addDataset(const QString& path, bool confirmed)
{
    if (lookup_ == nullptr) {
        return;
    }
    // The shape decides how many lines this is, so it has to be known first.
    // resolve() runs the continuation immediately when it already is, which is
    // the usual case: the tree had to describe the row to draw it.
    lookup_->resolve({path}, [this, path, confirmed] {
        const PathFacts* facts = lookup_->facts(path);
        if (facts == nullptr) {
            return;
        }
        if (!facts->usable) {
            emit notice(facts->problem);
            return;
        }

        // The lines the plot tab would draw: the last dimension runs along x
        // and every other one is spread over the lines, which is exactly what
        // the default table layout does with a rank-n dataset.
        const std::vector<hsize_t>& shape = facts->shape;
        const std::size_t last = shape.size() - 1;
        hsize_t lines = 1;
        for (std::size_t d = 0; d < last; ++d) {
            lines *= shape[d];
        }

        // Asked about rather than clipped. A dataset of two thousand runs is
        // a thing a reader may genuinely want the shape of; what they must not
        // get is two thousand strokes they did not ask for, or a silent
        // sixty-four out of two thousand, which is the worst of both -- a
        // picture that looks complete and is not.
        if (!confirmed && lines > static_cast<hsize_t>(kCrowdedLines)) {
            emit crowding(path, static_cast<int>(lines));
            return;
        }

        std::vector<QString> written;
        written.reserve(static_cast<std::size_t>(lines));
        std::vector<hsize_t> cursor(last, 0);
        for (hsize_t line = 0; line < lines; ++line) {
            QStringList parts;
            for (std::size_t d = 0; d < last; ++d) {
                parts << QString::number(cursor[d]);
            }
            parts << QStringLiteral(":");
            written.push_back(path + QStringLiteral("[")
                              + parts.join(QStringLiteral(", "))
                              + QStringLiteral("]"));
            // Odometer over the leading dimensions, fastest on the right --
            // row-major, the same order the grid lists its rows in.
            for (std::size_t d = last; d-- > 0;) {
                if (++cursor[d] < shape[d]) {
                    break;
                }
                cursor[d] = 0;
            }
        }

        const int first = static_cast<int>(entries_.size());
        beginInsertRows({}, first, first + static_cast<int>(written.size()) - 1);
        for (QString& text : written) {
            Entry entry;
            entry.expression = std::move(text);
            entries_.push_back(std::move(entry));
        }
        endInsertRows();
        invalidate();
    });
}

void CustomPlot::removeEntry(int row)
{
    if (row < 0 || row >= static_cast<int>(entries_.size())) {
        return;
    }
    beginRemoveRows({}, row, row);
    entries_.erase(entries_.begin() + row);
    endRemoveRows();
    invalidate();
}

void CustomPlot::moveEntry(int from, int to)
{
    const int count = static_cast<int>(entries_.size());
    if (from < 0 || from >= count || to < 0 || to >= count || from == to) {
        return;
    }
    // Qt's destination is measured before the row is taken out, so a move down
    // is one further than the index it lands on. The pipeline panel has the
    // same line in it, for the same reason.
    if (!beginMoveRows({}, from, from, {}, to > from ? to + 1 : to)) {
        return;
    }
    Entry carried = std::move(entries_[static_cast<std::size_t>(from)]);
    entries_.erase(entries_.begin() + from);
    entries_.insert(entries_.begin() + to, std::move(carried));
    endMoveRows();
    // The order is the drawing order and so the colour order, and nothing was
    // re-read: the values travelled with the row.
    emit changed();
}

void CustomPlot::clearEntries()
{
    if (entries_.empty()) {
        return;
    }
    beginResetModel();
    entries_.clear();
    endResetModel();
    invalidate();
}

void CustomPlot::setExpression(int row, const QString& text)
{
    if (row < 0 || row >= static_cast<int>(entries_.size())) {
        return;
    }
    Entry& entry = entries_[static_cast<std::size_t>(row)];
    const QString trimmed = text.trimmed();
    if (entry.expression == trimmed) {
        return;
    }
    entry.expression = trimmed;
    // The values held are the old line's. Dropped rather than kept, because a
    // row whose box says one thing while the stroke on the plot is another is
    // the one state this must never be in.
    entry.values.clear();
    entry.sourceLength = 0;
    entry.stride = 1;
    touch(row, {ExpressionRole, PointsRole, SourcePointsRole, ScalableRole});
    invalidate();
}

QString CustomPlot::entryError(int row, const QString& text) const
{
    Q_UNUSED(row);
    if (lookup_ == nullptr) {
        return {};
    }
    return expressionProblem(text, *lookup_);
}

void CustomPlot::setAlias(int row, const QString& text)
{
    if (row < 0 || row >= static_cast<int>(entries_.size())) {
        return;
    }
    Entry& entry = entries_[static_cast<std::size_t>(row)];
    const QString trimmed = text.trimmed();
    if (entry.alias == trimmed) {
        return;
    }
    entry.alias = trimmed;
    touch(row, {AliasRole});
    // Nothing is re-read and no point moves; the legend simply calls it
    // something else. `changed` is what the legend listens to.
    emit changed();
}

void CustomPlot::setScaling(int row, Scaling scaling)
{
    if (row < 0 || row >= static_cast<int>(entries_.size())) {
        return;
    }
    Entry& entry = entries_[static_cast<std::size_t>(row)];
    if (entry.scaling == scaling) {
        return;
    }
    entry.scaling = scaling;
    touch(row, {ScalingRole});
    // The same points, moved: nothing is re-read and no line has come or gone.
    emit xAxisChanged();
}

QVariantList CustomPlot::drawnSeries() const
{
    QVariantList shown;
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i].drawn) {
            shown.append(static_cast<int>(i));
        }
    }
    return shown;
}

int CustomPlot::seriesCount() const
{
    return static_cast<int>(std::count_if(
        entries_.begin(), entries_.end(),
        [](const Entry& entry) { return entry.drawn; }));
}

int CustomPlot::pointCount() const { return points_; }

int CustomPlot::sourceSeriesCount() const
{
    return static_cast<int>(entries_.size());
}

bool CustomPlot::thinned() const
{
    return std::any_of(entries_.begin(), entries_.end(), [](const Entry& entry) {
        return entry.drawn && entry.stride > 1;
    });
}

double CustomPlot::minimum() const { return minimum_; }
double CustomPlot::maximum() const { return maximum_; }

int CustomPlot::sourcePointCount() const
{
    if (xMode_ == Dataset) {
        return std::max(xSourceLength_, 1);
    }
    int longest = 0;
    for (const Entry& entry : entries_) {
        if (entry.drawn) {
            longest = std::max(longest, entry.sourceLength);
        }
    }
    return std::max(longest, 1);
}

void CustomPlot::setXStart(double value)
{
    if (xStart_ == value) {
        return;
    }
    xStart_ = value;
    emit xAxisChanged();
}

void CustomPlot::setXStep(double value)
{
    if (xStep_ == value) {
        return;
    }
    xStep_ = value;
    emit xAxisChanged();
}

bool CustomPlot::hasData() const { return hasFinite_; }

QString CustomPlot::error() const
{
    // The time base first: without one, in Dataset mode, nothing has an x and
    // every entry would otherwise report the same silence.
    if (xMode_ == Dataset && !xProblem_.isEmpty()) {
        return xProblem_;
    }
    for (const Entry& entry : entries_) {
        if (entry.drawn && !entry.problem.isEmpty()) {
            return entry.problem;
        }
    }
    return {};
}

QString CustomPlot::seriesLabel(int series) const
{
    if (series < 0 || series >= static_cast<int>(entries_.size())) {
        return {};
    }
    const Entry& entry = entries_[static_cast<std::size_t>(series)];
    return entry.alias.isEmpty() ? entry.expression : entry.alias;
}

bool CustomPlot::seriesVisible(int series) const
{
    if (series < 0 || series >= static_cast<int>(entries_.size())) {
        return false;
    }
    return entries_[static_cast<std::size_t>(series)].drawn;
}

void CustomPlot::setSeriesVisible(int series, bool visible)
{
    if (series < 0 || series >= static_cast<int>(entries_.size())) {
        return;
    }
    Entry& entry = entries_[static_cast<std::size_t>(series)];
    if (entry.drawn == visible) {
        return;
    }
    entry.drawn = visible;
    touch(series, {DrawnRole});
    recount();
    emit changed();
}

void CustomPlot::selectAll()
{
    bool moved = false;
    for (Entry& entry : entries_) {
        moved = moved || !entry.drawn;
        entry.drawn = true;
    }
    if (!moved) {
        return;
    }
    touch(-1, {DrawnRole});
    recount();
    emit changed();
}

void CustomPlot::selectNone()
{
    bool moved = false;
    for (Entry& entry : entries_) {
        moved = moved || entry.drawn;
        entry.drawn = false;
    }
    if (!moved) {
        return;
    }
    touch(-1, {DrawnRole});
    recount();
    emit changed();
}

void CustomPlot::selectFirst(int count)
{
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        entries_[i].drawn = static_cast<int>(i) < count;
    }
    touch(-1, {DrawnRole});
    recount();
    emit changed();
}

bool CustomPlot::scalable(const Entry& entry) const
{
    // Align and stretch put the points in the same places when a line is
    // exactly as long as the axis, so there is nothing to choose between.
    return entry.sourceLength > 0 && entry.sourceLength != sourcePointCount();
}

double CustomPlot::positionOf(const Entry& entry, std::size_t at) const
{
    const double drawn = static_cast<double>(entry.values.size());
    if (entry.scaling == Stretch && drawn > 1.0) {
        // Spread over the whole axis: the first point at its start and the
        // last at its end, whatever is in between. The axis is
        // `sourcePointCount` positions long and the last of them is one less
        // than that, which is where the last sample has to land.
        const double fraction = static_cast<double>(at) / (drawn - 1.0);
        return fraction * static_cast<double>(sourcePointCount() - 1);
    }
    // Point for point, allowing for the thinning: a line drawn every stride-th
    // element covers stride axis positions between one drawn point and the
    // next.
    return static_cast<double>(at) * static_cast<double>(entry.stride);
}

void CustomPlot::fill(QAbstractSeries* target, int series)
{
    auto* points = qobject_cast<QXYSeries*>(target);
    if (points == nullptr) {
        return;
    }
    if (series < 0 || series >= static_cast<int>(entries_.size())) {
        return;
    }
    const Entry& entry = entries_[static_cast<std::size_t>(series)];
    if (entry.values.empty()) {
        points->clear();
        return;
    }

    const bool againstDataset = xMode_ == Dataset;
    const auto xCount = static_cast<double>(xValues_.size());

    QList<QPointF> line;
    line.reserve(static_cast<qsizetype>(entry.values.size()));
    for (std::size_t i = 0; i < entry.values.size(); ++i) {
        const double value = entry.values[i];
        // A value that would not read is a gap in the line, not a zero: an
        // invented number at the axis is a reading of the data, and a wrong
        // one. The plot tab's rule, and for its reason.
        if (!std::isfinite(value)) {
            continue;
        }
        const double position = positionOf(entry, i);

        double x = 0.0;
        if (againstDataset) {
            if (xValues_.empty()) {
                continue;
            }
            // The time base has a value at each of its own positions and
            // nowhere in between, so a point that falls between two of them
            // takes the nearer. Interpolating would invent an x, which is the
            // same mistake as inventing a y.
            const double at = std::round(position);
            if (at < 0.0 || at >= xCount) {
                // Past the end of the time base. The line stops here rather
                // than being drawn against an x that does not exist, which is
                // what "align" means when the two are different lengths.
                continue;
            }
            x = xValues_[static_cast<std::size_t>(at)];
            if (!std::isfinite(x)) {
                continue;
            }
        } else {
            x = xStart_ + position * xStep_;
        }
        line.append(QPointF(x, value));
    }
    // One bulk replace, not `count` appends: each append signals, and a series
    // loaded point by point from QML redraws the graph on every one of them.
    points->replace(line);
}

QStringList CustomPlot::paths() const
{
    QStringList named;
    for (const Entry& entry : entries_) {
        const Expression parts = splitExpression(entry.expression);
        if (parts.valid() && !named.contains(parts.path)) {
            named.append(parts.path);
        }
    }
    if (xMode_ == Dataset && !xExpression_.trimmed().isEmpty()) {
        const Expression parts = splitExpression(xExpression_);
        if (parts.valid() && !named.contains(parts.path)) {
            named.append(parts.path);
        }
    }
    return named;
}

void CustomPlot::invalidate()
{
    coalesce_.start();
}

void CustomPlot::touch(int row, const QVector<int>& roles)
{
    if (entries_.empty()) {
        return;
    }
    if (row < 0) {
        emit dataChanged(index(0, 0),
                         index(static_cast<int>(entries_.size()) - 1, 0), roles);
        return;
    }
    emit dataChanged(index(row, 0), index(row, 0), roles);
}

void CustomPlot::recount()
{
    points_ = 0;
    minimum_ = 0.0;
    maximum_ = 0.0;
    hasFinite_ = false;
    for (const Entry& entry : entries_) {
        if (!entry.drawn) {
            continue;
        }
        for (const double value : entry.values) {
            if (!std::isfinite(value)) {
                continue;
            }
            if (!hasFinite_) {
                minimum_ = value;
                maximum_ = value;
                hasFinite_ = true;
            } else {
                minimum_ = std::min(minimum_, value);
                maximum_ = std::max(maximum_, value);
            }
            ++points_;
        }
    }
    // In Dataset mode a line with no time base to draw against is not drawable
    // however many finite values it holds.
    if (xMode_ == Dataset && xValues_.empty()) {
        hasFinite_ = false;
    }

    // The time base's own extent, which is what the axis is drawn between. Its
    // ends rather than its first and last value: a time base is usually
    // ascending and is not required to be, and an axis drawn from the first to
    // the last of a run that doubles back would leave half the points outside
    // the frame.
    xMinimum_ = 0.0;
    xMaximum_ = 1.0;
    bool seen = false;
    for (const double value : xValues_) {
        if (!std::isfinite(value)) {
            continue;
        }
        if (!seen) {
            xMinimum_ = value;
            xMaximum_ = value;
            seen = true;
        } else {
            xMinimum_ = std::min(xMinimum_, value);
            xMaximum_ = std::max(xMaximum_, value);
        }
    }
    if (seen && xMaximum_ <= xMinimum_) {
        // A flat time base has no extent to draw against; give it a unit of
        // room rather than an axis of zero width.
        xMaximum_ = xMinimum_ + 1.0;
    }
}

void CustomPlot::refresh()
{
    // Everything this tab draws, in one job and so in one crossing. The time
    // base goes first when there is one, so its index in the answer is known
    // without carrying a second list.
    const bool wantsX = xMode_ == Dataset && !xExpression_.trimmed().isEmpty();

    std::vector<Ask> asks;
    asks.reserve(entries_.size() + 1);
    if (wantsX) {
        asks.push_back({xExpression_});
    }
    for (const Entry& entry : entries_) {
        asks.push_back({entry.expression});
    }

    if (asks.empty()) {
        xProblem_.clear();
        xValues_.clear();
        xSourceLength_ = 0;
        recount();
        emit changed();
        return;
    }

    // Supersedes whatever was in flight. An answer about the entries as they
    // were two keystrokes ago is exactly the stale answer H5Requests exists to
    // drop.
    requests_.reset();
    H5Thread::instance().submit(
        requests_,
        [asks](H5Session& session) {
            Reply reply;
            reply.lines.reserve(asks.size());
            h5core::File* file = session.file();
            std::map<QString, std::shared_ptr<h5core::Dataset>> opened;
            std::map<QString, PathFacts> facts;
            for (const Ask& ask : asks) {
                reply.lines.push_back(
                    readLine(file, ask.expression, opened, facts));
            }
            reply.learned.reserve(facts.size());
            for (auto& entry : facts) {
                reply.learned.emplace_back(entry.first, std::move(entry.second));
            }
            return reply;
        },
        [this, wantsX](Reply reply) {
            for (auto& learned : reply.learned) {
                lookup_->remember(learned.first, std::move(learned.second));
            }

            std::size_t at = 0;
            if (wantsX) {
                if (!reply.lines.empty()) {
                    Answer& answer = reply.lines.front();
                    xProblem_ = answer.problem;
                    xValues_ = std::move(answer.values);
                    xSourceLength_ = answer.sourceLength;
                }
                at = 1;
            } else {
                xProblem_.clear();
                xValues_.clear();
                xSourceLength_ = 0;
            }

            for (std::size_t i = 0; i < entries_.size(); ++i, ++at) {
                if (at >= reply.lines.size()) {
                    break;
                }
                Answer& answer = reply.lines[at];
                Entry& entry = entries_[i];
                entry.problem = answer.problem;
                entry.values = std::move(answer.values);
                entry.stride = answer.stride;
                entry.sourceLength = answer.sourceLength;
            }

            recount();
            touch(-1, {ErrorRole, PointsRole, SourcePointsRole, ScalableRole});
            emit changed();
        });
}

QVariantMap CustomPlot::state() const
{
    QVariantList rows;
    rows.reserve(static_cast<qsizetype>(entries_.size()));
    for (const Entry& entry : entries_) {
        rows.append(QVariantMap{
            {QStringLiteral("expression"), entry.expression},
            {QStringLiteral("alias"), entry.alias},
            {QStringLiteral("scaling"),
             entry.scaling == Stretch ? QStringLiteral("stretch")
                                      : QStringLiteral("align")},
            {QStringLiteral("drawn"), entry.drawn}});
    }

    QString mode = QStringLiteral("index");
    if (xMode_ == Range) {
        mode = QStringLiteral("range");
    } else if (xMode_ == Dataset) {
        mode = QStringLiteral("dataset");
    }

    return {{QStringLiteral("name"), name_},
            {QStringLiteral("entries"), rows},
            {QStringLiteral("xMode"), mode},
            {QStringLiteral("xExpression"), xExpression_}};
}

void CustomPlot::setState(const QVariantMap& state)
{
    beginResetModel();
    entries_.clear();
    const QVariantList rows =
        state.value(QStringLiteral("entries")).toList();
    for (const QVariant& row : rows) {
        const QVariantMap fields = row.toMap();
        const QString expression =
            fields.value(QStringLiteral("expression")).toString().trimmed();
        if (expression.isEmpty()) {
            // Dropped rather than refused, which is the stance
            // PostprocessModel::setSteps takes for an operation this build does
            // not know: a stored thing that no longer makes sense costs the
            // reader the row it was, not the whole of what they saved.
            continue;
        }
        Entry entry;
        entry.expression = expression;
        entry.alias = fields.value(QStringLiteral("alias")).toString().trimmed();
        entry.scaling =
            fields.value(QStringLiteral("scaling")).toString()
                    == QStringLiteral("stretch")
                ? Stretch
                : Align;
        entry.drawn = fields.value(QStringLiteral("drawn"), true).toBool();
        entries_.push_back(std::move(entry));
    }
    endResetModel();

    const QString mode = state.value(QStringLiteral("xMode")).toString();
    xMode_ = mode == QStringLiteral("range")   ? Range
             : mode == QStringLiteral("dataset") ? Dataset
                                                 : Index;
    xExpression_ = state.value(QStringLiteral("xExpression")).toString();
    emit xSourceChanged();
    invalidate();
}

} // namespace gui
