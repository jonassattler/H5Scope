// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "CustomPlot.hpp"

#include "H5Session.hpp"
#include "PlotLevels.hpp"
#include "h5core/Dataset.hpp"
#include "h5core/Error.hpp"
#include "h5core/File.hpp"
#include "postproc/Array.hpp"
#include "postproc/Operations.hpp"
#include "postproc/Pipeline.hpp"

#include <QPointF>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <utility>

namespace gui {

namespace {

/// See CustomPlot::hyperslabs(). Relaxed because it is a count for a test to
/// difference, never a thing another thread waits on.
std::atomic<long long> gHyperslabs{0};

/// What the job is asked for: one line per expression, the time base first
/// when there is one.
struct Ask
{
    QString expression;
    /// A closer look rather than the whole line: read only this run of it, at
    /// this bucket. See gui::PlotWindow.
    std::optional<PlotWindow> window;
    /// Buckets to reduce the whole line to, which is the pane's own width in
    /// columns. Ignored when a window says what the bucket is.
    int buckets = CustomPlot::kDefaultColumns;
};

/// What it hands back, alongside the facts it learned on the way.
struct Answer
{
    QString problem;
    std::vector<double> values;
    /// The first element of the line these values start at. Zero for the whole
    /// line; the run's own start for a closer look.
    double start = 0.0;
    /// Axis positions between one drawn point and the next. Half a bucket when
    /// the line was read as an envelope, because a bucket answers with two.
    double step = 1.0;
    int sourceLength = 0;
};

struct Reply
{
    std::vector<Answer> lines;
    std::vector<std::pair<QString, PathFacts>> learned;
};

/// Read one line. On the HDF5 thread.
///
/// The open datasets come from the session rather than from a map of this job's
/// own, so a tab of eight entries edited eight times opens eight datasets and
/// not sixty-four. See H5Session::held().
[[nodiscard]] Answer readLine(H5Session& session, const Ask& ask,
                              std::map<QString, PathFacts>& facts)
{
    Answer answer;
    h5core::File* file = session.file();

    const QString& expression = ask.expression;
    const Expression parts = splitExpression(expression);
    if (!parts.valid()) {
        answer.problem = parts.error;
        return answer;
    }

    // The facts, once per path per job. lookupFacts opens the dataset to read
    // them; the values are then read through the session's own handle, which
    // outlives the job.
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
    if (!resolveLine(parts.subscript, known->second.shape, indices, drop, answer.problem)) {
        return answer;
    }

    for (std::size_t d = 0; d < indices.size(); ++d) {
        if (d >= drop.size() || !drop[d]) {
            answer.sourceLength = static_cast<int>(indices[d].size());
            break;
        }
    }
    // A closer look reads a run of the line rather than the whole of it, and
    // the elements outside that run are never touched -- which is what makes
    // zooming in cost the pane rather than the file. The run's bucket was
    // chosen against the pane by gui::windowFor and is simply obeyed here.
    if (ask.window.has_value()) {
        windowLine(indices, drop, ask.window->first, ask.window->span);
        answer.start = static_cast<double>(ask.window->first);
    }

    h5core::Dataset* open = session.held(parts.path.toStdString());
    if (open == nullptr) {
        answer.problem = known->second.problem.isEmpty()
                             ? QStringLiteral("cannot open %1").arg(parts.path)
                             : known->second.problem;
        return answer;
    }
    h5core::Dataset& dataset = *open;

    // How what is left is reduced to something a screen can show.
    //
    // Always the extremes of each bucket, never every nth element. A stride
    // selects by position and reaches an extremum only by luck -- on
    // /plotting/adc_10M it lands on none of the seventeen impulses and beats
    // against the trace's own oscillation, so what it draws is a wave that is
    // not in the file. An envelope selects the extremum *because* it is
    // extreme, draws the same number of points, and cannot lose one.
    //
    // This used to give up past kEnvelopeElements and fall back to stride,
    // because the reduction was done in memory and the read had to be
    // materialised first. That was not a compromise, it was a second picture of
    // the same data: the Plot tab drew adc_10M as a band of +/-32000 with every
    // impulse in it, and a custom tab drew the same slice as an aliased sine of
    // +/-13000 with none of them.
    const std::size_t along = lineDimension(indices, drop);
    const auto length = static_cast<long long>(indices[along].size());
    const long long buckets = std::max<long long>(1, ask.buckets);
    const long long bucket = ask.window.has_value()
                                 ? ask.window->bucket
                                 : std::max<long long>(1, (length + buckets - 1) / buckets);

    if (bucket <= 1) {
        // Short enough to draw sample for sample. One read of the lot.
        gHyperslabs.fetch_add(1, std::memory_order_relaxed);
        const postproc::ArrayResult read = postproc::read(dataset, indices, drop);
        if (!read.ok()) {
            answer.problem = read.error;
            return answer;
        }
        const postproc::Array& array = read.array;
        const hsize_t count = array.size();
        answer.step = 1.0;
        answer.values.reserve(static_cast<std::size_t>(count));
        for (hsize_t i = 0; i < count; ++i) {
            answer.values.push_back(array.at({i}));
        }
        return answer;
    }

    // The envelope, in reads of up to kReadRun elements -- not one read per
    // bucket, which is what this did and what it cost.
    //
    // A bucket at a time was defended above as "the same round trips as the
    // strided read it replaces, one per drawn point either way", and against a
    // stride that was true. It is the wrong comparison. It is the arrangement
    // DatasetTableModel abandoned for exactly this reason: a pane two thousand
    // columns wide asked HDF5 for a bucket two thousand times per entry per
    // refresh, and essentially all of it was per-call overhead rather than I/O
    // -- twenty megabytes moved in 1.28 million reads, two seconds of a frozen
    // window (DatasetTableModel.cpp, the same note over the same loop).
    //
    // So the round trips follow the *length of the line* and not the number of
    // buckets it is folded into: read as many whole buckets as one hyperslab
    // can reach, fold them out of the buffer, and go on. The values are
    // identical -- gui::reduceBuckets is the arithmetic both plots now use --
    // which is what lets test_customplot go on comparing the two element for
    // element while the reads underneath change by three orders of magnitude.
    //
    // A bucket wider than a whole read is the one case that cannot be cut back;
    // it is read whole, which is what every bucket used to get.
    const std::vector<hsize_t> line = std::move(indices[along]);
    const long long taken = (length + bucket - 1) / bucket;
    answer.step = static_cast<double>(bucket) / 2.0;
    answer.values.reserve(static_cast<std::size_t>(taken) * 2);

    long long done = 0;
    while (done < length) {
        const long long remaining = length - done;
        long long run = wholeBuckets(std::min<long long>(kReadRun, remaining), bucket);
        if (run < bucket) {
            run = std::min(bucket, remaining);
        }
        indices[along].assign(line.begin() + static_cast<std::ptrdiff_t>(done),
                              line.begin() + static_cast<std::ptrdiff_t>(done + run));

        gHyperslabs.fetch_add(1, std::memory_order_relaxed);
        const postproc::ArrayResult read = postproc::read(dataset, indices, drop);
        if (!read.ok()) {
            answer.problem = read.error;
            answer.values.clear();
            return answer;
        }
        // Contiguous, because the fold walks it with a pointer. A read of a run
        // of a line is already contiguous, so this is the buffer itself.
        const std::vector<double> buffer = read.array.values();
        reduceBuckets(buffer.data(), static_cast<long long>(buffer.size()), bucket, answer.values);
        done += run;
    }
    return answer;
}

} // namespace

long long CustomPlot::hyperslabs()
{
    return gHyperslabs.load(std::memory_order_relaxed);
}

CustomPlot::CustomPlot(QString name, DatasetLookup* lookup, QObject* parent)
    : QAbstractListModel(parent), name_(std::move(name)), lookup_(lookup)
{
    coalesce_.setSingleShot(true);
    coalesce_.setInterval(0);
    connect(&coalesce_, &QTimer::timeout, this, &CustomPlot::refresh);

    // The closer look waits for the gesture to stop, where the coalescing above
    // waits only for the turn of the event loop. Two timers because they are
    // two different waits: one is "several edits are one read", the other is
    // "a reader still moving has not asked for anything yet".
    settle_.setSingleShot(true);
    settle_.setInterval(kSettleMilliseconds);
    connect(&settle_, &QTimer::timeout, this, &CustomPlot::askForCloser);

    // And a third wait, for the pane's width. A window dragged from narrow to
    // wide crosses a dozen column quanta and each crossing is every entry read
    // again -- which the reader saw as the tab flickering under their hand
    // while they were still deciding how wide they wanted it.
    resize_.setSingleShot(true);
    resize_.setInterval(kResizeMilliseconds);
    connect(&resize_, &QTimer::timeout, this, &CustomPlot::applyColumns);

    // A path that was unknown while the reader was typing is known now, so the
    // rows re-check themselves against it. Nothing is re-read: the error roles
    // are recomputed from the cache.
    if (lookup_ != nullptr) {
        connect(lookup_, &DatasetLookup::changed, this, [this] {
            if (!entries_.empty()) {
                emit dataChanged(index(0, 0), index(static_cast<int>(entries_.size()) - 1, 0),
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
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(entries_.size())) {
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
            written.push_back(path + QStringLiteral("[") + parts.join(QStringLiteral(", ")) +
                              QStringLiteral("]"));
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
    discard(); // the row's values went with it
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
    announce();
}

void CustomPlot::clearEntries()
{
    if (entries_.empty()) {
        return;
    }
    beginResetModel();
    entries_.clear();
    endResetModel();
    discard(); // every row's values went with them
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
    // the one state this must never be in -- so the renderer is emptied here
    // rather than left drawing them, which is what discard() below is for.
    releaseDrawing();
    entry.values.clear();
    entry.sourceLength = 0;
    entry.step = 1.0;
    touch(row, {ExpressionRole, PointsRole, SourcePointsRole, ScalableRole});
    discard();
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
    announce();
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
    // Which elements are on screen has moved with them, though -- stretch and
    // align put different parts of the line under the pane.
    refreshCloser();
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
    return static_cast<int>(std::count_if(entries_.begin(), entries_.end(),
                                          [](const Entry& entry) { return entry.drawn; }));
}

int CustomPlot::pointCount() const
{
    return points_;
}

int CustomPlot::sourceSeriesCount() const
{
    return static_cast<int>(entries_.size());
}

bool CustomPlot::thinned() const
{
    return std::any_of(entries_.begin(), entries_.end(),
                       [](const Entry& entry) { return entry.drawn && entry.step > 1.0; });
}

double CustomPlot::minimum() const
{
    return minimum_;
}
double CustomPlot::maximum() const
{
    return maximum_;
}

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
    // The axis decides what a position is worth in x, so moving it moves which
    // elements the reader is looking at without the view having moved at all.
    refreshCloser();
}

void CustomPlot::setXStep(double value)
{
    if (xStep_ == value) {
        return;
    }
    xStep_ = value;
    emit xAxisChanged();
    refreshCloser();
}

bool CustomPlot::hasData() const
{
    return hasFinite_;
}

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
    announce();
    // An entry ticked while the reader is looking closely has no closer look of
    // its own yet. It draws from the whole-line summary -- correct, and coarse
    // -- until the next settle reads the one run it is missing.
    refreshCloser();
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
    announce();
    refreshCloser();
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
    announce();
    refreshCloser();
}

void CustomPlot::selectFirst(int count)
{
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        entries_[i].drawn = static_cast<int>(i) < count;
    }
    touch(-1, {DrawnRole});
    recount();
    announce();
    refreshCloser();
}

// ---------------------------------------------------------------------------
// The closer look
// ---------------------------------------------------------------------------

int CustomPlot::bucketBudget() const
{
    return std::clamp(columns_, kMinPoints / 2, kMaxPoints / 2);
}

int CustomPlot::closerBuckets() const
{
    // One octave finer than the pane needs, which is the cheapest prefetch
    // there is and is what the plot tab does -- see DatasetPlot::detailBuckets
    // for the argument. Here every entry keeps its own run rather than sharing
    // a budget with the others, so the guard is the number of entries drawn
    // rather than a division: past a few dozen runs held at twice the
    // resolution the tab is spending memory on detail nobody asked for.
    const int pane = bucketBudget();
    return seriesCount() <= kCrowdedLines ? std::min(2 * pane, kMaxPoints) : pane;
}

void CustomPlot::setPaneColumns(int columns)
{
    // Down to the quantum, for the reason DatasetPlot::kColumnQuantum gives:
    // handing the renderer more than two points per column makes it summarise
    // again, in powers of two, and a hair too many costs half the resolution.
    const int quantised = std::clamp((std::max(columns, 0) / kColumnQuantum) * kColumnQuantum,
                                     kMinPoints / 2, kMaxPoints / 2);
    if (quantised == wantedColumns_) {
        return;
    }
    wantedColumns_ = quantised;
    if (wantedColumns_ == columns_) {
        // Dragged out and back again inside one gesture. Nothing to do, and
        // nothing to wait for either.
        resize_.stop();
        return;
    }
    if (!measured_) {
        // The surface measuring itself for the first time. There is no gesture
        // to wait out and nothing for the wait to protect -- whatever has been
        // read so far was read at an assumed width.
        measured_ = true;
        resize_.stop();
        applyColumns();
        return;
    }
    // Otherwise nothing happens here. See the note on the declaration: the read
    // is at the end of the drag, not once per sixty-four pixels of it.
    resize_.start();
}

void CustomPlot::applyColumns()
{
    if (wantedColumns_ == columns_) {
        return;
    }
    columns_ = wantedColumns_;
    // Every entry was reduced against the old width, so every entry is read
    // again -- and what is on screen goes on being drawn until the answer
    // lands.
    invalidate();
}

void CustomPlot::setVisibleRange(double xMin, double xMax)
{
    if (viewMin_ == xMin && viewMax_ == xMax) {
        return;
    }
    // Which run in hand covers the pane is a property of the view, so it can
    // change without anything being read -- and when it does, a different set
    // of values has to reach the renderer. See closerDrawing(): this compares
    // *which* run each entry is drawn from, because an entry stepping from one
    // held run to another is the case that has to be noticed and the one a
    // count of them cannot see.
    const std::vector<std::optional<PlotWindow>> drawing = closerDrawing();
    viewMin_ = xMin;
    viewMax_ = xMax;
    refreshCloser();
    if (closerDrawing() != drawing) {
        announce();
    }
}

double CustomPlot::stretchScale(const Entry& entry) const
{
    if (entry.scaling != Stretch || entry.sourceLength <= 1) {
        return 1.0;
    }
    // The line spread over the whole axis: its first element at the start and
    // its last at the end, whatever their number. The same map lineOf() draws
    // the whole line with, written per element rather than per drawn point,
    // because a run of the line has to land where its elements would have.
    return static_cast<double>(sourcePointCount() - 1) /
           static_cast<double>(entry.sourceLength - 1);
}

bool CustomPlot::lineRange(const Entry& entry, double& first, double& last) const
{
    if (xMode_ == Dataset) {
        // A time base is a lookup table, not an affine map. It need not be
        // monotonic, so a range of x is not a range of indices and there is
        // nothing here to run backwards -- the same reason projectLine()
        // refuses to envelope that path.
        return false;
    }
    if (!std::isfinite(viewMin_) || !std::isfinite(viewMax_) || !std::isfinite(xStart_) ||
        !std::isfinite(xStep_) || !(std::abs(xStep_) > 0.0)) {
        return false;
    }
    const double scale = stretchScale(entry);
    if (!(std::abs(scale) > 0.0)) {
        return false;
    }
    double low = (viewMin_ - xStart_) / xStep_ / scale;
    double high = (viewMax_ - xStart_) / xStep_ / scale;
    if (low > high) {
        std::swap(low, high);
    }
    if (!std::isfinite(low) || !std::isfinite(high) || !(high > low)) {
        return false;
    }
    first = low;
    last = high;
    return true;
}

LevelView CustomPlot::levelView(const Entry& entry) const
{
    LevelView view;
    if (!entry.drawn || entry.sourceLength <= 0 || !entry.problem.isEmpty()) {
        return view;
    }
    if (entry.step <= 1.0) {
        // Already drawn sample for sample. There is nothing a second read could
        // add, whatever the reader does with the wheel.
        return view;
    }
    if (!lineRange(entry, view.low, view.high)) {
        return view;
    }
    view.length = entry.sourceLength;
    // An envelope answers with two values for each bucket and a bucket is a
    // column, so these counts are already what they mean.
    view.paneBuckets = bucketBudget();
    view.detailBuckets = closerBuckets();
    view.prefetchOctaves = kPrefetchOctaves;
    return view;
}

std::span<const HeldLevel> CustomPlot::ladder(const Entry& entry) const
{
    // One run per entry rather than one for the whole drawn set, so the only
    // question about a run is whether it came back -- see HeldLevel::complete,
    // where the plot tab has the harder half of the same question.
    ladder_.clear();
    ladder_.reserve(entry.levels.size());
    for (const Level& level : entry.levels) {
        ladder_.push_back(HeldLevel{level.window, !level.values.empty()});
    }
    return ladder_;
}

std::optional<PlotWindow> CustomPlot::closerFor(const Entry& entry, int buckets) const
{
    const LevelView view = levelView(entry);
    if (!view.usable()) {
        return {};
    }
    return windowFor(view.low, view.high, view.length, buckets);
}

int CustomPlot::drawnLevel(const Entry& entry) const
{
    return gui::drawnLevel(ladder(entry), levelView(entry));
}

int CustomPlot::heldLevels() const
{
    // Each run is about `bucketBudget()` buckets of two values, per entry, so
    // how many there is room for is the budget divided by what one costs. A tab
    // of a few entries gets all of them; one carrying hundreds keeps the run it
    // is on and nothing else.
    const int entries = std::max(seriesCount(), 1);
    const int affordable = kPointBudget / std::max(entries * 4 * bucketBudget(), 1);
    return std::clamp(affordable, 1, kHeldLevels);
}

void CustomPlot::trimLevels(Entry& entry)
{
    const int allowed = heldLevels();
    while (static_cast<int>(entry.levels.size()) > allowed) {
        // Rebuilt each time round, because erasing one changes which of the
        // rest is coldest.
        const std::size_t worst = gui::coldestLevel(ladder(entry), levelView(entry), focus_);
        retire(entry.levels[worst].values);
        entry.levels.erase(entry.levels.begin() + static_cast<long>(worst));
    }
}

std::vector<std::optional<PlotWindow>> CustomPlot::closerDrawing() const
{
    std::vector<std::optional<PlotWindow>> drawing;
    drawing.reserve(entries_.size());
    for (const Entry& entry : entries_) {
        if (!entry.drawn) {
            continue;
        }
        const int at = drawnLevel(entry);
        drawing.push_back(at < 0
                              ? std::optional<PlotWindow>{}
                              : entry.levels[static_cast<std::size_t>(at)].window);
    }
    return drawing;
}

std::optional<PlotWindow> CustomPlot::closerWanted(const Entry& entry) const
{
    return gui::wantedLevel(ladder(entry), levelView(entry), focus_);
}

void CustomPlot::refreshCloser()
{
    bool wanted = false;
    bool dropped = false;
    for (Entry& entry : entries_) {
        if (!closerFor(entry, bucketBudget()).has_value()) {
            if (!entry.levels.empty()) {
                // The borrow contract: these values outlive the change rather
                // than being freed under a renderer that is reading them. The
                // whole-line summary covers everything by construction, so the
                // frame after this one is a correct picture either way.
                for (Level& level : entry.levels) {
                    retire(level.values);
                }
                entry.levels.clear();
                dropped = true;
            }
            continue;
        }
        if (closerWanted(entry).has_value()) {
            wanted = true;
        }
    }
    if (wanted) {
        settle_.start();
    }
    else {
        // The same runs, already in hand. Not a read, not a signal, not even a
        // timer -- which is what makes panning inside them free.
        settle_.stop();
    }
    if (dropped) {
        announce();
    }
}

void CustomPlot::askForCloser()
{
    if (lookup_ == nullptr) {
        return;
    }
    std::vector<int> rows;
    std::vector<Ask> asks;
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        const Entry& entry = entries_[i];
        const std::optional<PlotWindow> want = closerWanted(entry);
        if (!want.has_value()) {
            continue;
        }
        rows.push_back(static_cast<int>(i));
        asks.push_back(Ask{entry.expression, want, closerBuckets()});
    }
    if (asks.empty()) {
        return;
    }

    // Its own tickets, so that this and a refresh cannot cancel one another:
    // a refresh supersedes whatever closer look was being read, and a closer
    // look must never supersede a refresh.
    //
    // Submitted rather than waited for, as everything on this path is. There is
    // already a correct picture on screen; blocking the window to replace it
    // with a sharper one would spend the reader's attention to save them
    // nothing.
    closerRequests_.reset();
    H5Thread::instance().submit(
        closerRequests_,
        [asks](H5Session& session) {
            Reply reply;
            reply.lines.reserve(asks.size());
            std::map<QString, PathFacts> facts;
            for (const Ask& ask : asks) {
                reply.lines.push_back(readLine(session, ask, facts));
            }
            return reply;
        },
        [this, rows, asks](Reply reply) {
            const std::size_t count = std::min(rows.size(), reply.lines.size());
            for (std::size_t i = 0; i < count; ++i) {
                const auto row = static_cast<std::size_t>(rows[i]);
                if (row >= entries_.size()) {
                    continue; // a row removed while this was out
                }
                Entry& entry = entries_[row];
                if (entry.expression != asks[i].expression) {
                    continue; // retyped while this was out; the next settle asks again
                }
                Answer& answer = reply.lines[i];
                const PlotWindow window = *asks[i].window;
                // Remembered even when it read nothing, so a line that will not
                // read closely is not asked for over and over. lineOf() falls
                // back to a coarser run, or to the whole-line summary, which is
                // what such a line should draw.
                const auto at = std::find_if(
                    entry.levels.begin(), entry.levels.end(),
                    [&window](const Level& level) { return level.window == window; });
                if (at == entry.levels.end()) {
                    entry.levels.push_back(Level{window, answer.step,
                                                 answer.problem.isEmpty()
                                                     ? std::move(answer.values)
                                                     : std::vector<double>{}});
                }
                else {
                    at->step = answer.step;
                    // Retired, not overwritten: a renderer may be holding a
                    // pointer into what is there now, and it goes on drawing it
                    // until fill() hands over what replaces it.
                    retire(at->values);
                    at->values = answer.problem.isEmpty() ? std::move(answer.values)
                                                          : std::vector<double>{};
                }
                trimLevels(entry);
            }
            announce();
            // And on to the next octave, if there is one worth reading. The
            // chain is what keeps the prefetch idle work: each run waits out its
            // own settle, so a reader who starts moving again cancels the rest.
            refreshCloser();
        });
}

void CustomPlot::clearCloser()
{
    settle_.stop();
    closerRequests_.reset();
    for (Entry& entry : entries_) {
        for (Level& level : entry.levels) {
            retire(level.values);
        }
        entry.levels.clear();
    }
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
    // next -- and half a bucket when it was read as an envelope, because the
    // two values of a bucket are its extremes and they occurred somewhere
    // inside it.
    return static_cast<double>(at) * entry.step;
}

void CustomPlot::releaseDrawing()
{
    if (drawing_ != nullptr) {
        drawing_->clear();
        drawing_ = nullptr;
    }
}

void CustomPlot::retire(std::vector<double>& values)
{
    if (values.empty()) {
        return;
    }
    if (drawing_ == nullptr) {
        // Nothing is reading it, so there is nothing to keep it alive for --
        // and this is what bounds the store: a tab nobody is drawing would
        // otherwise accumulate one copy per read until something filled a
        // renderer.
        values.clear();
        return;
    }
    // Moved rather than copied: a std::vector move takes the buffer with it,
    // so the pointer the renderer was given goes on naming the same doubles.
    // `drawing_` is deliberately left alone -- the item is still reading these
    // values and is still what has to be emptied if they ever do have to go.
    retired_.push_back(std::move(values));
    values.clear();
}

void CustomPlot::announce()
{
    emit changed();
}

PlotLine CustomPlot::lineOf(int series) const
{
    PlotLine line;
    if (series < 0 || series >= static_cast<int>(entries_.size())) {
        return line;
    }
    const Entry& entry = entries_[static_cast<std::size_t>(series)];
    if (entry.values.empty()) {
        return line;
    }
    // A tab drawn against a time base that has not read draws nothing at all,
    // rather than falling back to positions: the reader asked for these values
    // against *those* x, and an axis of indices with the same line on it is a
    // different plot wearing the same label.
    if (xMode_ == Dataset && xValues_.empty()) {
        return line;
    }

    // The closer look, while it covers what is on screen. A second summary of
    // the same entry rather than a replacement for the first: zoom out past the
    // run in hand and the whole-line summary below is what is drawn, in the
    // same frame, because it covers everything by construction.
    if (closerCovers(entry)) {
        const Level& level = entry.levels[static_cast<std::size_t>(drawnLevel(entry))];
        line.values = level.values.data();
        line.count = static_cast<qsizetype>(level.values.size());
        // The run's own start and step, put back on the axis by the same map
        // the whole line is drawn with. Under Align an element is a position;
        // under Stretch the line is spread over the axis, so both the offset
        // and the step are scaled by the same factor.
        const double scale = stretchScale(entry);
        line.positionStart = static_cast<double>(level.window.first) * scale;
        line.positionStep = level.step * scale;
        return line;
    }

    line.values = entry.values.data();
    line.count = static_cast<qsizetype>(entry.values.size());
    // The same affine map positionOf() applies, written once as a start and a
    // step. Stretch spreads the line over the whole axis -- its first sample at
    // the start and its last at the end, whatever is in between -- and align
    // covers the elements the thinning skipped.
    const auto drawn = static_cast<double>(entry.values.size());
    if (entry.scaling == Stretch && drawn > 1.0) {
        line.positionStep = static_cast<double>(sourcePointCount() - 1) / (drawn - 1.0);
    }
    else {
        line.positionStep = entry.step;
    }
    return line;
}

PlotAxis CustomPlot::drawingAxis() const
{
    PlotAxis axis;
    axis.start = xStart_;
    axis.step = xStep_;
    if (xMode_ == Dataset) {
        axis.values = xValues_.data();
        axis.count = static_cast<qsizetype>(xValues_.size());
        axis.valueStep = xValueStep_;
    }
    return axis;
}

void CustomPlot::fill(PlotItem* target)
{
    if (target == nullptr) {
        return;
    }
    std::vector<PlotLine> lines;
    lines.reserve(entries_.size());
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i].drawn) {
            lines.push_back(lineOf(static_cast<int>(i)));
        }
    }
    target->setLines(std::move(lines), drawingAxis());
    drawing_ = target;
    // ...and now, and only now, is nothing reading what was retired. This is
    // the one place those vectors are freed, because it is the one place a
    // renderer that was borrowing them has just been given something else.
    retired_.clear();
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
    // Nothing is emptied. Whatever is drawing goes on drawing what it was
    // given, and the reply is what replaces it -- at which point the values it
    // is reading are retired rather than freed. This used to release here, and
    // the reader saw it: every row added, every slider dragged and every change
    // of the pane's width blanked the tab until the answer came back.
    coalesce_.start();
}

void CustomPlot::discard()
{
    // ...and this is the version for when the line on screen is about to become
    // the wrong line rather than a coarser one: a row removed, a row retyped,
    // or the whole tab replaced. There the old stroke is not a worse reading of
    // the data, it is a reading of something the reader has just said they are
    // not looking at.
    releaseDrawing();
    retired_.clear(); // nothing is reading them now
    invalidate();
}

void CustomPlot::touch(int row, const QVector<int>& roles)
{
    if (entries_.empty()) {
        return;
    }
    if (row < 0) {
        emit dataChanged(index(0, 0), index(static_cast<int>(entries_.size()) - 1, 0), roles);
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
            }
            else {
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
        }
        else {
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
        asks.push_back(Ask{xExpression_, {}, bucketBudget()});
    }
    for (const Entry& entry : entries_) {
        asks.push_back(Ask{entry.expression, {}, bucketBudget()});
    }

    if (asks.empty()) {
        xProblem_.clear();
        retire(xValues_);
        xValueStep_ = 1.0;
        xSourceLength_ = 0;
        clearCloser();
        recount();
        announce();
        return;
    }

    // Supersedes whatever was in flight. An answer about the entries as they
    // were two keystrokes ago is exactly the stale answer H5Requests exists to
    // drop -- and so is a closer look at them, which is why both sets of
    // tickets go. What is *held* stays until this read lands, so the picture
    // does not coarsen while the reader types.
    requests_.reset();
    closerRequests_.reset();
    H5Thread::instance().submit(
        requests_,
        [asks](H5Session& session) {
            Reply reply;
            reply.lines.reserve(asks.size());
            std::map<QString, PathFacts> facts;
            for (const Ask& ask : asks) {
                reply.lines.push_back(readLine(session, ask, facts));
            }
            reply.learned.reserve(facts.size());
            for (auto& entry : facts) {
                reply.learned.emplace_back(entry.first, std::move(entry.second));
            }
            return reply;
        },
        [this, wantsX](Reply reply) {
            // Every entry's values are about to be replaced. Retired rather
            // than freed, so the renderer goes on drawing what it has until
            // fill() hands it the answer -- see retire().
            for (auto& learned : reply.learned) {
                lookup_->remember(learned.first, std::move(learned.second));
            }

            std::size_t at = 0;
            if (wantsX) {
                if (!reply.lines.empty()) {
                    Answer& answer = reply.lines.front();
                    xProblem_ = answer.problem;
                    retire(xValues_);
                    xValues_ = std::move(answer.values);
                    // The time base's own thinning, which is what turns an
                    // axis position back into one of these values. It is not
                    // the entries' -- they are thinned against the same pane
                    // and need not be the same length as it.
                    xValueStep_ = answer.step;
                    xSourceLength_ = answer.sourceLength;
                }
                at = 1;
            }
            else {
                xProblem_.clear();
                retire(xValues_);
                xValueStep_ = 1.0;
                xSourceLength_ = 0;
            }

            for (std::size_t i = 0; i < entries_.size(); ++i, ++at) {
                if (at >= reply.lines.size()) {
                    break;
                }
                Answer& answer = reply.lines[at];
                Entry& entry = entries_[i];
                entry.problem = answer.problem;
                retire(entry.values);
                entry.values = std::move(answer.values);
                entry.step = answer.step;
                entry.sourceLength = answer.sourceLength;
            }
            // Whatever was being looked at closely was a run of the lines as
            // they were before this read. The expression may have been retyped
            // under it, so none of it is kept; refreshCloser() below asks for
            // the run the reader is on now.
            clearCloser();

            recount();
            touch(-1, {ErrorRole, PointsRole, SourcePointsRole, ScalableRole});
            announce();
            refreshCloser();
        });
}

QVariantMap CustomPlot::state() const
{
    QVariantList rows;
    rows.reserve(static_cast<qsizetype>(entries_.size()));
    for (const Entry& entry : entries_) {
        rows.append(QVariantMap{{QStringLiteral("expression"), entry.expression},
                                {QStringLiteral("alias"), entry.alias},
                                {QStringLiteral("scaling"), entry.scaling == Stretch
                                                                ? QStringLiteral("stretch")
                                                                : QStringLiteral("align")},
                                {QStringLiteral("drawn"), entry.drawn}});
    }

    QString mode = QStringLiteral("index");
    if (xMode_ == Range) {
        mode = QStringLiteral("range");
    }
    else if (xMode_ == Dataset) {
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
    const QVariantList rows = state.value(QStringLiteral("entries")).toList();
    for (const QVariant& row : rows) {
        const QVariantMap fields = row.toMap();
        const QString expression = fields.value(QStringLiteral("expression")).toString().trimmed();
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
            fields.value(QStringLiteral("scaling")).toString() == QStringLiteral("stretch")
                ? Stretch
                : Align;
        entry.drawn = fields.value(QStringLiteral("drawn"), true).toBool();
        entries_.push_back(std::move(entry));
    }
    endResetModel();

    const QString mode = state.value(QStringLiteral("xMode")).toString();
    xMode_ = mode == QStringLiteral("range")     ? Range
             : mode == QStringLiteral("dataset") ? Dataset
                                                 : Index;
    xExpression_ = state.value(QStringLiteral("xExpression")).toString();
    emit xSourceChanged();
    discard(); // a different tab entirely; nothing on screen is its
}

} // namespace gui
