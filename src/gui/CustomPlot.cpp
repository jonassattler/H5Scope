// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "CustomPlot.hpp"

#include "H5Session.hpp"
#include "PlotBudget.hpp"
#include "PlotLevels.hpp"
#include "PlotPyramid.hpp"
#include "h5core/Dataset.hpp"
#include "h5core/Error.hpp"
#include "h5core/File.hpp"
#include "postproc/Array.hpp"
#include "postproc/ComputedDataset.hpp"
#include "postproc/MemberPath.hpp"
#include "postproc/Operations.hpp"
#include "postproc/Pipeline.hpp"
#include "postproc/Script.hpp"

#include <QPointF>
#include <QPointer>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <utility>

namespace gui {

namespace {

/// See CustomPlot::hyperslabs(). Relaxed because it is a count for a test to
/// difference, never a thing another thread waits on.
std::atomic<long long> gHyperslabs{0};

/// Which drawn point of one reading of a time base `x` falls on, as an axis
/// position -- where that reading's first value sits at `start` and its values
/// are `step` positions apart.
///
/// **A time base that only ever goes one way is a map that can be run
/// backwards**, and that is the whole of what makes a time series zoomable: a
/// range of x becomes a range of positions, and everything the index and range
/// axes already do -- narrow the run, fold it finer, read towards the pointer --
/// follows without another line. One that doubles back is not such a map and
/// gets none of it, which is what `false` says here; the tab then draws what it
/// always drew and a zoom stretches it.
///
/// The order is asked of the very array about to be searched rather than of the
/// time base as a whole, because a summary can ascend while the elements under
/// one of its buckets do not, and it is the array being bisected whose order
/// has to hold.
[[nodiscard]] bool positionOfX(const std::vector<double>& values, double start, double step,
                               double x, double& position)
{
    if (values.empty() || !(step > 0.0) || !std::isfinite(x)) {
        return false;
    }
    std::size_t at = 0;
    if (std::is_sorted(values.begin(), values.end())) {
        at = static_cast<std::size_t>(
            std::lower_bound(values.begin(), values.end(), x) - values.begin());
    }
    else if (std::is_sorted(values.rbegin(), values.rend())) {
        // A descending time base is a time base. It is drawn right to left and
        // is inverted the same way round; nothing above here cares which.
        at = static_cast<std::size_t>(
            std::lower_bound(values.begin(), values.end(), x, std::greater<>{}) - values.begin());
    }
    else {
        return false;
    }
    position = start + static_cast<double>(std::min(at, values.size() - 1)) * step;
    return std::isfinite(position);
}

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
    /// Doubles this line's pyramid may spend, or zero for no pyramid.
    ///
    /// Set on the whole-line read and never on a closer look: the point of the
    /// pyramid is that after the whole-line read there are no closer looks to
    /// make. See PlotPyramid.hpp, and DatasetPlot, which reads the same way for
    /// the same reason.
    long long budget = 0;
    /// Whether `expression` is a postproc::Script rather than a slice.
    bool postprocess = false;
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
    /// Whether it *was* read as an envelope. Carried rather than derived from
    /// the step above, which cannot tell a bucket of two from the elements
    /// themselves -- see PlotLine::summarised.
    bool summarised = false;
    int sourceLength = 0;
    /// The line itself, kept at every resolution, when one was asked for.
    LinePyramid pyramid;
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

    // A line is a slice, or a pipeline written as a script. Both come down to
    // a path, a member chain and a slice line; a script may also carry steps,
    // and only a script that does is read any differently from here on.
    const QString& expression = ask.expression;
    Expression parts;
    std::optional<postproc::Script> script;
    if (ask.postprocess) {
        script = postproc::parseScript(expression);
        if (!script->ok()) {
            answer.problem = script->error;
            return answer;
        }
        parts.path = script->path;
        parts.member = script->member;
    }
    else {
        parts = splitExpression(expression);
        if (!parts.valid()) {
            answer.problem = parts.error;
            return answer;
        }
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

    // The chain, before the subscript: it decides what shape the subscript is
    // against, because the axes a member appends are axes of the line.
    const postproc::MemberChain chain =
        postproc::resolveMemberChain(parts.member, known->second.type);
    if (!chain.valid()) {
        answer.problem = chain.error;
        return answer;
    }
    // The same sentence the entry box gives while it is being typed, said here
    // because this is where a reader who typed a path and pressed Return finds
    // out -- and "name one of its members" is a more useful answer than the
    // read's own "which has no numeric value".
    answer.problem = undrawableReason(chain.selection.type, known->second.type,
                                      !chain.empty());
    if (!answer.problem.isEmpty()) {
        return answer;
    }

    std::vector<hsize_t> shape = known->second.shape;
    shape.insert(shape.end(), chain.selection.dims.begin(),
                 chain.selection.dims.end());
    const std::size_t originRank = known->second.shape.size();

    h5core::Dataset* open =
        session.held(parts.path.toStdString(), chain.selection);
    if (open == nullptr) {
        answer.problem = known->second.problem.isEmpty()
                             ? QStringLiteral("cannot open %1").arg(parts.path)
                             : known->second.problem;
        return answer;
    }

    std::vector<std::vector<hsize_t>> indices;
    std::vector<bool> drop;
    // What the reduction below reads from: the dataset, or -- for a pipeline
    // with steps in it -- the pipeline's output, held in memory. It is handed
    // the second as a DataSource, because a ComputedDataset is one, so nothing
    // below this point has a branch for which it got.
    const h5core::DataSource* source = open;
    std::shared_ptr<const postproc::ComputedDataset> computed;

    if (script.has_value() && !script->steps.empty()) {
        // A pipeline has to materialise -- an axis cannot be reduced a block
        // at a time -- so it is run whole, under the cap the panel runs under,
        // and the line it leaves is folded exactly as a line read from the
        // file would be. Checked first, because the check costs no read and
        // says which step is at fault.
        const postproc::ScriptCheck check =
            postproc::checkScript(*script, known->second.shape, known->second.type);
        if (!check.ok()) {
            answer.problem = check.error;
            return answer;
        }
        if (check.output.size() != 1) {
            answer.problem = notOneLine(check.output);
            return answer;
        }
        const postproc::RunResult run = postproc::run(*open, check.pipeline, check.pipeline.size());
        if (!run.error.isEmpty() || run.array.rank() != 1) {
            answer.problem = run.error.isEmpty()
                                 ? QStringLiteral("the pipeline did not leave a line")
                                 : run.error;
            return answer;
        }
        computed = std::make_shared<const postproc::ComputedDataset>(
            run.array, open->info(), open->path(), "(postprocessed)");
        source = computed.get();
        indices.emplace_back(static_cast<std::size_t>(run.array.size()));
        std::iota(indices.front().begin(), indices.front().end(), hsize_t{0});
        drop.push_back(false);
    }
    else {
        // A script with no steps is a slice written another way, and is read
        // as one -- streamed, never materialised -- so ticking the box costs a
        // line nothing. pipelineOf is what makes its slice line: over the
        // whole derived shape unless the chain carries subscripts of its own.
        const QString line =
            script.has_value()
                ? postproc::pipelineOf(*script, chain.folded, originRank).front().argument
                : postproc::sliceLineFor(parts.subscript, chain.folded, originRank);
        if (!resolveLine(line, shape, indices, drop, answer.problem)) {
            return answer;
        }
    }
    // Reads out of memory are not reads of the file, and the count is of the
    // file's.
    const bool fromFile = computed == nullptr;

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
    //
    // For a pipeline, the run is of its output, and asking for one runs the
    // pipeline again. That is only ever below the pyramid's base, which for a
    // line under the pipeline's cap means a budget too small to hold it at
    // bucket one -- rare, bounded by kMaxElements, and correct.
    if (ask.window.has_value()) {
        windowLine(indices, drop, ask.window->first, ask.window->span);
        answer.start = static_cast<double>(ask.window->first);
    }

    const h5core::DataSource& dataset = *source;

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

    if (ask.budget > 0 && !ask.window.has_value()) {
        // The whole line, kept.
        //
        // One pass, at the finest bucket the budget affords, and every closer
        // look afterwards is a fold of it in memory rather than another read --
        // which is the arrangement DatasetPlot reads by and the reason the two
        // tabs feel alike on a ten-million-element slice. See PlotPyramid.hpp.
        //
        // The walk is the envelope walk below, handing each hyperslab to the
        // builder instead of folding it here. Whatever a read leaves that does
        // not fill a whole bucket is the builder's to carry into the next one:
        // a read stops where kReadRun and the index list let it and none of
        // those boundaries has to fall on a bucket, and a bucket summarised
        // from half of itself is simply wrong.
        const long long base = baseBucketFor(length, ask.budget);
        PyramidBuilder builder(length, base);

        const std::vector<hsize_t> whole = std::move(indices[along]);
        long long read = 0;
        while (read < length) {
            const long long run = std::min<long long>(kReadRun, length - read);
            indices[along].assign(whole.begin() + static_cast<std::ptrdiff_t>(read),
                                  whole.begin() + static_cast<std::ptrdiff_t>(read + run));
            if (fromFile) {
                gHyperslabs.fetch_add(1, std::memory_order_relaxed);
            }
            const postproc::ArrayResult got = postproc::read(dataset, indices, drop);
            if (!got.ok()) {
                answer.problem = got.error;
                return answer;
            }
            // Contiguous, because the fold walks it with a pointer. A read of a
            // run of a line is already contiguous, so this is the buffer itself.
            const std::vector<double> buffer = got.array.values();
            builder.add(buffer.data(), static_cast<long long>(buffer.size()));
            read += run;
        }
        LinePyramid pyramid = builder.finish();

        // ...and the whole-line summary out of it, which is exactly what the
        // walk below would have answered with: coarsening an envelope is the
        // same question as reading at that bucket. See PlotLevels.hpp.
        long long stride = 1;
        if (!fillWhole(pyramid, static_cast<int>(buckets), answer.values, stride, answer.step)) {
            answer.problem = QStringLiteral("could not summarise %1").arg(ask.expression);
            return answer;
        }
        answer.summarised = stride > 1;
        answer.pyramid = std::move(pyramid);
        return answer;
    }

    if (bucket <= 1) {
        // Short enough to draw sample for sample. One read of the lot.
        if (fromFile) {
            gHyperslabs.fetch_add(1, std::memory_order_relaxed);
        }
        const postproc::ArrayResult read = postproc::read(dataset, indices, drop);
        if (!read.ok()) {
            answer.problem = read.error;
            return answer;
        }
        const postproc::Array& array = read.array;
        const hsize_t count = array.size();
        answer.step = 1.0;
        answer.summarised = false;
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
    answer.summarised = true;
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

        if (fromFile) {
            gHyperslabs.fetch_add(1, std::memory_order_relaxed);
        }
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

CustomPlot::~CustomPlot()
{
    // Disconnected first, and this is not tidiness. leave() tells every other
    // plot that its share just grew, and a signal emitted from a destructor
    // reaches this object's own slot as well -- ~QObject, which is what breaks
    // the connections, runs after this body. applyBudget() then asks the model
    // how long the line is, through a table that is already gone.
    disconnect(&PlotBudget::instance(), nullptr, this, nullptr);
    PlotBudget::instance().leave();
}

void CustomPlot::applyBudget()
{
    // The pyramids are where the memory is. This used to trim the run ladder
    // and nothing else, under a comment saying nothing was re-read -- true when
    // the only held thing was a handful of runs, and misleading from the moment
    // a whole line was held beside them: a reader who turned the budget down
    // because this program was holding three gigabytes got none of it back
    // until they selected another dataset.
    //
    // Coarsening is exact and free, so turning the budget down is honoured
    // here. Refining is not arithmetic -- a finer base is elements this no
    // longer has -- so it costs the pass that read the line, and refresh() is
    // how that is asked for. Unlike the Plot tab's, this one is asynchronous:
    // the window stays live while it lands.
    const long long budget = pyramidBudget();
    bool refine = false;
    const auto resize = [&](Entry& entry) {
        if (entry.pyramid.empty()) {
            return;
        }
        const long long wanted = baseBucketFor(entry.pyramid.length, budget);
        if (wanted < entry.pyramid.baseBucket()) {
            refine = true;
            return;
        }
        coarsenTo(entry.pyramid, wanted);
    };
    resize(axis_);
    for (Entry& entry : entries_) {
        resize(entry);
        trimLevels(entry);
    }
    trimLevels(axis_);
    if (refine) {
        refresh();
        return;
    }
    refreshCloser();
}

long long CustomPlot::hyperslabs()
{
    return gHyperslabs.load(std::memory_order_relaxed);
}

CustomPlot::CustomPlot(QString name, DatasetLookup* lookup, QObject* parent)
    : QAbstractListModel(parent), name_(std::move(name)), lookup_(lookup)
{
    // One budget, shared out between this tab, every other tab and the Plot
    // tab. See PlotBudget: a per-object constant times the number of objects is
    // how a generous number becomes an unbounded one.
    PlotBudget::instance().join();
    connect(&PlotBudget::instance(), &PlotBudget::changed, this, &CustomPlot::applyBudget);

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
    case ColourRole:
        // Nothing rather than an invalid colour, for the reason
        // seriesOverride gives: in QML the two are not the same answer.
        return entry.colour.isValid() ? QVariant(entry.colour) : QVariant();
    case SeparateAxisRole:
        return entry.separateAxis;
    case AxisFixedRole:
        return entry.axisFixed;
    case AxisLabelRole:
        return entry.axisLabel;
    case PostprocessRole:
        return entry.postprocess;
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
            {DrawnRole, "drawn"},
            {ColourRole, "colour"},
            {SeparateAxisRole, "separateAxis"},
            {AxisFixedRole, "axisFixed"},
            {AxisLabelRole, "axisLabel"},
            {PostprocessRole, "postprocess"}};
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

int CustomPlot::addScript(const QString& text)
{
    const int row = static_cast<int>(entries_.size());
    beginInsertRows({}, row, row);
    Entry entry;
    entry.postprocess = true;
    const postproc::Script script = postproc::parseScript(text);
    entry.expression = script.ok() ? postproc::writeScript(script) : text.trimmed();
    entries_.push_back(std::move(entry));
    endInsertRows();
    invalidate();
    return row;
}

void CustomPlot::setPostprocess(int row, bool on)
{
    if (row < 0 || row >= static_cast<int>(entries_.size())) {
        return;
    }
    Entry& entry = entries_[static_cast<std::size_t>(row)];
    if (entry.postprocess == on) {
        return;
    }
    QString rewritten;
    if (on) {
        // The script put aside when the box was last unticked, if the slice
        // is still the one it was put aside as -- otherwise the slice, written
        // as a script.
        const bool untouched =
            !entry.unticked.isEmpty() && lookup_ != nullptr
            && expressionFromScript(entry.unticked, *lookup_) == entry.expression;
        rewritten = untouched ? entry.unticked : scriptFromExpression(entry.expression);
        entry.unticked.clear();
    }
    else {
        const postproc::Script script = postproc::parseScript(entry.expression);
        if (script.ok() && !script.steps.empty()) {
            entry.unticked = entry.expression;
        }
        if (lookup_ != nullptr) {
            rewritten = expressionFromScript(entry.expression, *lookup_);
        }
    }
    entry.postprocess = on;
    touch(row, {PostprocessRole});
    // A line that did not parse either way keeps its text, and says what is
    // wrong with it in the other grammar.
    if (!rewritten.isEmpty() && rewritten != entry.expression) {
        const QString text = rewritten;
        entry.expression.clear(); // so that setExpression sees a change
        setExpression(row, text);
        return;
    }
    touch(row, {ErrorRole});
    discard();
}

void CustomPlot::addDataset(const QString& path, bool confirmed)
{
    if (lookup_ == nullptr) {
        return;
    }
    // The shape decides how many lines this is, so it has to be known first.
    // resolve() runs the continuation immediately when it already is, which is
    // the usual case: the tree had to describe the row to draw it.
    //
    // When it is not, the answer comes back a turn or more later, and it comes
    // back through the *set's* lookup -- whose requests outlive this tab. A tab
    // closed in between is deleted by then, so `this` is guarded rather than
    // trusted: a reply for a tab nobody has any more is dropped, not written
    // into freed memory.
    lookup_->resolve({path}, [self = QPointer<CustomPlot>(this), path, confirmed] {
        if (self.isNull()) {
            return;
        }
        self->addLinesOf(path, confirmed);
    });
}

void CustomPlot::addLinesOf(const QString& path, bool confirmed)
{
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
    if (shape.empty()) {
        return; // a scalar is not usable, so this is only ever a guard
    }
    const std::size_t last = shape.size() - 1;
    // Saturating, for the reason postproc::elementCount gives.
    const hsize_t lines = postproc::elementCount(
        std::vector<hsize_t>(shape.begin(), shape.begin() + static_cast<std::ptrdiff_t>(last)));

    // An empty leading dimension leaves no lines at all, and an insertion of
    // none is a range whose last row comes before its first -- which Qt's
    // views are entitled to assert on, and a debug build of Qt does.
    if (lines == 0) {
        emit notice(tr("%1 has an empty dimension, so there is no line in it to draw").arg(path));
        return;
    }
    // A row is an int, however many lines were confirmed.
    const auto room = static_cast<hsize_t>(std::numeric_limits<int>::max()) - entries_.size();
    if (lines > room) {
        emit notice(tr("%1 is %2 lines, more than one plot can hold").arg(path).arg(lines));
        return;
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
}

void CustomPlot::removeEntry(int row)
{
    if (row < 0 || row >= static_cast<int>(entries_.size())) {
        return;
    }
    beginRemoveRows({}, row, row);
    entries_.erase(entries_.begin() + row);
    endRemoveRows();
    // Now, rather than when the re-read lands: the lines left are already in
    // hand, and one of them may just have become the only line -- whose own
    // axis is then no longer in force.
    recount();
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
    QString trimmed = text.trimmed();
    if (entry.postprocess) {
        // Formatted a step to a line, which is what Return in a DATA box
        // promises; a script that does not read is kept as it was typed, with
        // its reason, as every box here keeps a line it cannot use.
        if (const postproc::Script script = postproc::parseScript(trimmed); script.ok()) {
            trimmed = postproc::writeScript(script);
        }
    }
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
    entry.summarised = false;
    touch(row, {ExpressionRole, PointsRole, SourcePointsRole, ScalableRole});
    discard();
}

QString CustomPlot::entryError(int row, const QString& text) const
{
    if (lookup_ == nullptr) {
        return {};
    }
    const bool postprocess = row >= 0 && row < static_cast<int>(entries_.size())
                             && entries_[static_cast<std::size_t>(row)].postprocess;
    return lineProblem(text, postprocess, *lookup_);
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

void CustomPlot::setEntryColor(int row, const QColor& colour)
{
    if (row < 0 || row >= static_cast<int>(entries_.size())) {
        return;
    }
    Entry& entry = entries_[static_cast<std::size_t>(row)];
    if (entry.colour == colour) {
        return;
    }
    entry.colour = colour;
    touch(row, {ColourRole});
    // Nothing is re-read and no point moves; the same line is drawn in another
    // colour. `changed` is what the surface and the legend listen to, and the
    // surface answers it by restyling rather than refilling.
    announce();
}

void CustomPlot::clearEntryColor(int row)
{
    // An invalid QColor, which is what "no override" is here rather than a
    // separate flag: one thing to store, one thing to serialise, and no state
    // in which a line both has a colour and does not.
    setEntryColor(row, QColor());
}

QVariant CustomPlot::seriesOverride(int series) const
{
    if (series < 0 || series >= static_cast<int>(entries_.size())) {
        return {};
    }
    const QColor& colour = entries_[static_cast<std::size_t>(series)].colour;
    return colour.isValid() ? QVariant(colour) : QVariant();
}

void CustomPlot::setSeparateAxis(int row, bool on)
{
    if (row < 0 || row >= static_cast<int>(entries_.size())) {
        return;
    }
    Entry& entry = entries_[static_cast<std::size_t>(row)];
    if (entry.separateAxis == on) {
        return;
    }
    entry.separateAxis = on;
    touch(row, {SeparateAxisRole});
    // The same line, under another map. Nothing is re-read; what moves is the
    // common axis, which no longer spans this line (or spans it again), and
    // that is a recount of values already in hand.
    recount();
    announce();
}

void CustomPlot::setAxisFixed(int row, bool on)
{
    if (row < 0 || row >= static_cast<int>(entries_.size())) {
        return;
    }
    Entry& entry = entries_[static_cast<std::size_t>(row)];
    if (entry.axisFixed == on) {
        return;
    }
    entry.axisFixed = on;
    touch(row, {AxisFixedRole});
    announce();
}

void CustomPlot::setAxisLabel(int row, const QString& text)
{
    if (row < 0 || row >= static_cast<int>(entries_.size())) {
        return;
    }
    Entry& entry = entries_[static_cast<std::size_t>(row)];
    const QString trimmed = text.trimmed();
    if (entry.axisLabel == trimmed) {
        return;
    }
    entry.axisLabel = trimmed;
    touch(row, {AxisLabelRole});
    // A word beside an axis. Nothing is re-read, no point moves and no extent
    // changes; `changed` is what the surface asks seriesAxis() again on.
    announce();
}

QVariantMap CustomPlot::seriesAxis(int series) const
{
    if (series < 0 || series >= static_cast<int>(entries_.size())) {
        return {{QStringLiteral("separate"), false}};
    }
    const Entry& entry = entries_[static_cast<std::size_t>(series)];
    return {{QStringLiteral("separate"), entry.ownAxis},
            {QStringLiteral("fixed"), entry.ownAxis && entry.axisFixed},
            {QStringLiteral("finite"), entry.finite},
            {QStringLiteral("low"), entry.low},
            {QStringLiteral("high"), entry.high},
            {QStringLiteral("label"), entry.axisLabel}};
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

double CustomPlot::positiveMinimum() const
{
    return hasPositive_ ? positiveMinimum_ : 0.0;
}

int CustomPlot::sourcePointCount() const
{
    if (xMode_ == Dataset) {
        return std::max(axis_.sourceLength, 1);
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

void CustomPlot::setXLog(bool logarithmic)
{
    if (xLog_ == logarithmic) {
        return;
    }
    xLog_ = logarithmic;
    dropFold();
    emit xAxisChanged();
    refreshCloser();
}

std::optional<LogColumns> CustomPlot::foldWanted() const
{
    const int drawn = seriesCount();
    if (!xLog_ || drawn == 0 || drawn > kCrowdedLines) {
        return {};
    }
    if (xMode_ == Dataset) {
        // Folded against only while it runs one way, which is the rule every
        // closer look at a time base keeps: one that doubles back puts an x in
        // two places and there is no column to put it in.
        bool ascending = true;
        if (axis_.pyramid.empty() || !timeSorted(ascending)) {
            return {};
        }
    }
    else if (!std::isfinite(xStart_) || !std::isfinite(xStep_) || !(std::abs(xStep_) > 0.0)) {
        return {};
    }
    return logColumnsFor(viewMin_, viewMax_, bucketBudget());
}

bool CustomPlot::foldServes() const
{
    return foldColumns_.has_value() && foldStart_ == xStart_ && foldStep_ == xStep_ &&
           foldMode_ == static_cast<int>(xMode_) && foldBuckets_ == bucketBudget() &&
           logColumnsServe(*foldColumns_, viewMin_, viewMax_, bucketBudget());
}

void CustomPlot::dropFold() const
{
    for (const Entry& entry : entries_) {
        retire(entry.foldValues);
        retire(entry.foldXs);
        entry.foldGeneration = -1;
    }
    foldColumns_.reset();
    ++foldGeneration_;
}

double CustomPlot::timeAt(long long at) const
{
    const LinePyramid& time = axis_.pyramid;
    if (time.empty() || at < 0 || at >= time.length) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const PyramidLevel& bottom = time.levels.front();
    if (bottom.bucket == 1) {
        return bottom.values[static_cast<std::size_t>(at)];
    }
    return bottom.values[static_cast<std::size_t>(at / bottom.bucket) * 2];
}

bool CustomPlot::timeSorted(bool& ascending) const
{
    const LinePyramid& time = axis_.pyramid;
    if (time.empty()) {
        return false;
    }
    const std::vector<double>& bottom = time.levels.front().values;
    if (sortedFor_ != bottom.data() || sortedSize_ != bottom.size()) {
        sortedFor_ = bottom.data();
        sortedSize_ = bottom.size();
        // A pair per bucket above a base of one, in the order the two
        // occurred, which on a time base running one way is its first element
        // and its last -- so the pairs laid end to end run the same way the
        // elements do, and one walk answers for both.
        sortedAnswer_ = std::is_sorted(bottom.begin(), bottom.end())     ? 1
                        : std::is_sorted(bottom.rbegin(), bottom.rend()) ? -1
                                                                         : 0;
    }
    ascending = sortedAnswer_ >= 0;
    return sortedAnswer_ != 0;
}

void CustomPlot::timeEdges(const LogColumns& columns, double scale, std::vector<double>& out) const
{
    out.clear();
    bool ascending = true;
    if (!timeSorted(ascending) || !(scale > 0.0)) {
        return;
    }
    const long long length = axis_.pyramid.length;
    // How many elements lie before `x` in the order the time base runs:
    // those below it when it ascends, those above it when it descends. A
    // column's elements are then a run between two of these, which is what an
    // edge is.
    const auto before = [&](double x) {
        long long low = 0;
        long long high = length;
        while (low < high) {
            const long long mid = low + (high - low) / 2;
            const double t = timeAt(mid);
            if (ascending ? t < x : t >= x) {
                low = mid + 1;
            }
            else {
                high = mid;
            }
        }
        return low;
    };
    out.reserve(static_cast<std::size_t>(columns.last - columns.first + 1));
    for (long long k = columns.first; k <= columns.last; ++k) {
        out.push_back(static_cast<double>(before(columns.edge(k))) / scale);
    }
    if (!ascending) {
        std::reverse(out.begin(), out.end());
    }
}

bool CustomPlot::foldedLine(const Entry& entry, PlotLine& line) const
{
    if (entry.pyramid.empty() || !foldWanted().has_value()) {
        return false;
    }
    if (!foldServes()) {
        // Another grid. Every entry's fold goes with the one it was made on --
        // retired rather than freed, because the renderer is drawing them
        // until it is handed these.
        dropFold();
        foldColumns_ = logColumnsFor(viewMin_, viewMax_, bucketBudget());
        foldStart_ = xStart_;
        foldStep_ = xStep_;
        foldMode_ = static_cast<int>(xMode_);
        foldBuckets_ = bucketBudget();
    }
    if (entry.foldGeneration != foldGeneration_) {
        retire(entry.foldValues);
        retire(entry.foldXs);
        // An element of this entry sits at `scale` axis positions per
        // element: one under Align, the axis over the line under Stretch.
        const double scale = stretchScale(entry);
        std::vector<double> edges;
        if (xMode_ == Dataset) {
            timeEdges(*foldColumns_, scale, edges);
        }
        else {
            edgesAlong(*foldColumns_, xStart_, xStep_ * scale, edges);
        }
        ColumnFold folded;
        foldColumns(entry.pyramid, edges, folded);
        std::vector<double> xs(folded.positions.size());
        for (std::size_t i = 0; i < xs.size(); ++i) {
            const double at = folded.positions[i] * scale;
            xs[i] = xMode_ == Dataset ? timeAt(std::llround(at)) : xStart_ + at * xStep_;
        }
        entry.foldValues = std::move(folded.values);
        entry.foldXs = std::move(xs);
        entry.foldSummarised = folded.summarised;
        entry.foldGeneration = foldGeneration_;
    }
    line.values = entry.foldValues.data();
    line.xs = entry.foldXs.data();
    line.count = static_cast<qsizetype>(entry.foldValues.size());
    line.summarised = entry.foldSummarised;
    return true;
}

bool CustomPlot::hasData() const
{
    return hasFinite_;
}

QString CustomPlot::error() const
{
    // The time base first: without one, in Dataset mode, nothing has an x and
    // every entry would otherwise report the same silence.
    if (xMode_ == Dataset && !axis_.problem.isEmpty()) {
        return axis_.problem;
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
    if (!entry.alias.isEmpty()) {
        return entry.alias;
    }
    if (entry.postprocess) {
        // On one line: a label that breaks across lines is not a label.
        const postproc::Script script = postproc::parseScript(entry.expression);
        return script.ok() ? postproc::writeScript(script, postproc::ScriptLayout::OneLine)
                           : entry.expression;
    }
    return entry.expression;
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
    // A logarithmic axis wide enough to be folded per column is its own path;
    // see DatasetPlot::setVisibleRange, which is this written for one line.
    const bool wasFolded = foldWanted().has_value();
    const double oldMin = viewMin_;
    const double oldMax = viewMax_;
    viewMin_ = xMin;
    viewMax_ = xMax;
    if (foldWanted().has_value()) {
        recomputeView();
        if (!wasFolded || !foldServes()) {
            announce();
        }
        return;
    }
    viewMin_ = oldMin;
    viewMax_ = oldMax;
    if (wasFolded) {
        viewMin_ = xMin;
        viewMax_ = xMax;
        refreshCloser();
        announce();
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

bool CustomPlot::axisPositionOf(double x, double& position, double& resolution) const
{
    // The whole of the time base first: it is the one reading that answers for
    // every position, so it is where the bracket starts.
    if (!positionOfX(axis_.values, 0.0, axis_.step, x, position)) {
        return false;
    }
    resolution = axis_.step;
    // ...and then again in whatever finer run of it covers that answer. Each
    // pass is a strictly finer reading than the last, so this ends -- and it
    // ends at the pyramid's base, which on any time base a reader has is the
    // elements themselves. Without it the bracket stays as wide as one drawn
    // point of the summary, which on a ten-million-element log is five thousand
    // elements, and the run that comes back is three octaves coarser than the
    // pane asked for however far the reader zooms.
    for (std::size_t pass = 0; pass < axis_.levels.size(); ++pass) {
        const Level* finer = nullptr;
        for (const Level& level : axis_.levels) {
            if (level.values.empty() || !(level.step > 0.0) || !(level.step < resolution)) {
                continue;
            }
            if (!level.window.covers(position, position)) {
                continue;
            }
            if (finer == nullptr || level.step < finer->step) {
                finer = &level;
            }
        }
        if (finer == nullptr) {
            break;
        }
        double closer = 0.0;
        if (!positionOfX(finer->values, static_cast<double>(finer->window.first), finer->step, x,
                         closer)) {
            break;
        }
        position = closer;
        resolution = finer->step;
    }
    return true;
}

void CustomPlot::recomputeView()
{
    viewUsable_ = false;
    focusUsable_ = false;
    // A range of no width is what "nothing has been pushed" looks like: the two
    // are zero until a surface says otherwise. Refused here rather than left to
    // fall out of the arithmetic, because under Dataset the bracket is opened
    // by the resolution it was inverted at -- so a degenerate range would come
    // back a bucket wide and take a closer look at the *start* of every line,
    // which is a tab drawn from the first two thousand elements of itself.
    if (!std::isfinite(viewMin_) || !std::isfinite(viewMax_) || !(viewMax_ > viewMin_)) {
        return;
    }

    if (xMode_ == Dataset) {
        double low = 0.0;
        double high = 0.0;
        double lowResolution = 1.0;
        double highResolution = 1.0;
        if (!axisPositionOf(viewMin_, low, lowResolution) ||
            !axisPositionOf(viewMax_, high, highResolution)) {
            return;
        }
        if (low > high) {
            std::swap(low, high);
            std::swap(lowResolution, highResolution);
        }
        // Opened by the resolution each end was settled at. The inversion is
        // only as sharp as the reading it was made against, and a run that
        // stops short of what is drawn is a line clipped in the middle of the
        // pane -- so the bracket errs outwards, which costs at most one bucket
        // of span and cannot cost a stroke.
        viewLow_ = low - lowResolution;
        viewHigh_ = high + highResolution;
        viewUsable_ = std::isfinite(viewLow_) && std::isfinite(viewHigh_) && viewHigh_ > viewLow_;
        if (focusActive_) {
            double at = 0.0;
            double ignored = 1.0;
            focusUsable_ = axisPositionOf(focusX_, at, ignored);
            focusPosition_ = at;
        }
        return;
    }

    if (!std::isfinite(xStart_) || !std::isfinite(xStep_) || !(std::abs(xStep_) > 0.0)) {
        return;
    }
    double low = (viewMin_ - xStart_) / xStep_;
    double high = (viewMax_ - xStart_) / xStep_;
    if (low > high) {
        std::swap(low, high);
    }
    if (!std::isfinite(low) || !std::isfinite(high) || !(high > low)) {
        return;
    }
    viewLow_ = low;
    viewHigh_ = high;
    viewUsable_ = true;
    if (focusActive_) {
        const double at = (focusX_ - xStart_) / xStep_;
        focusUsable_ = std::isfinite(at);
        focusPosition_ = at;
    }
}

bool CustomPlot::lineRange(const Entry& entry, double& first, double& last) const
{
    if (!viewUsable_) {
        return false;
    }
    const double scale = stretchScale(entry);
    if (!(std::abs(scale) > 0.0)) {
        return false;
    }
    const double low = viewLow_ / scale;
    const double high = viewHigh_ / scale;
    if (!std::isfinite(low) || !std::isfinite(high) || !(high > low)) {
        return false;
    }
    first = low;
    last = high;
    return true;
}

void CustomPlot::setZoomFocus(double x, double factor)
{
    if (!std::isfinite(x) || !std::isfinite(factor) || !(factor > 0.0)) {
        clearZoomFocus();
        return;
    }
    focusX_ = x;
    focusInward_ = factor > 1.0;
    focusActive_ = true;
}

void CustomPlot::clearZoomFocus()
{
    focusActive_ = false;
}

PlotFocus CustomPlot::focusFor(const Entry& entry) const
{
    PlotFocus focus;
    if (!focusActive_ || !focusUsable_) {
        return focus;
    }
    const double scale = stretchScale(entry);
    if (!(std::abs(scale) > 0.0)) {
        return focus;
    }
    const double position = focusPosition_ / scale;
    if (!std::isfinite(position)) {
        return focus;
    }
    focus.position = position;
    focus.inward = focusInward_;
    focus.active = true;
    return focus;
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

long long CustomPlot::pyramidBudget() const
{
    // The share this tab may hold, split between its entries -- and the time
    // base, which is one more line held at every resolution and has to be paid
    // for out of the same share rather than beside it. Twice, for the reason
    // DatasetPlot::pyramidBudget gives: what is held has to leave room for the
    // runs a bucket below the base still reads.
    const int lines = std::max(seriesCount(), 1) + (xMode_ == Dataset ? 1 : 0);
    return PlotBudget::instance().share() / (2LL * lines);
}

bool CustomPlot::fillCloser(Entry& entry, const PlotWindow& window)
{
    if (entry.pyramid.empty() || window.bucket < entry.pyramid.baseBucket()) {
        return false;
    }
    const auto at = std::find_if(entry.levels.begin(), entry.levels.end(),
                                 [&window](const Level& level) { return level.window == window; });
    if (at != entry.levels.end()) {
        return false; // already held at this run
    }
    std::vector<double> folded;
    if (!fillWindow(entry.pyramid, window, folded)) {
        return false;
    }
    // What a read of this run would have reported: a pair per bucket half a
    // bucket apart, or the elements themselves when the bucket is one.
    const double step = window.bucket == 1 ? 1.0 : static_cast<double>(window.bucket) / 2.0;
    // The same hazard DatasetPlot::takeDetail names: this push_back may
    // reallocate `levels` while the renderer is reading a run already in it, so
    // a Level is move-only-by-noexcept and the relocation cannot be a copy.
    entry.levels.push_back(Level{window, step, std::move(folded)});
    return true;
}

long long CustomPlot::retiredDoubles() const
{
    long long held = 0;
    for (const std::vector<double>& values : retired_) {
        held += static_cast<long long>(values.size());
    }
    return held;
}

long long CustomPlot::heldDoubles() const
{
    long long held = static_cast<long long>(axis_.pyramid.doubles());
    for (const Entry& entry : entries_) {
        held += static_cast<long long>(entry.pyramid.doubles());
    }
    return held;
}

int CustomPlot::heldLevels() const
{
    // Each run is about `bucketBudget()` buckets of two values, per entry, so
    // how many there is room for is the budget divided by what one costs. A tab
    // of a few entries gets all of them; one carrying hundreds keeps the run it
    // is on and nothing else.
    //
    // Out of what is *left* of the share once the pyramids have been paid for,
    // rather than out of the whole of it -- see DatasetPlot::heldLevels, which
    // carries the argument.
    const long long spare =
        std::max<long long>(PlotBudget::instance().share() - heldDoubles(), 0);
    const int entries = std::max(seriesCount(), 1);
    const long long affordable =
        spare / std::max<long long>(entries * 4LL * bucketBudget(), 1);
    return static_cast<int>(std::clamp<long long>(affordable, 1, kHeldLevels));
}

void CustomPlot::trimLevels(Entry& entry)
{
    const int allowed = heldLevels();
    while (static_cast<int>(entry.levels.size()) > allowed) {
        // Rebuilt each time round, because erasing one changes which of the
        // rest is coldest.
        const std::size_t worst = gui::coldestLevel(ladder(entry), levelView(entry), focusFor(entry));
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
    return gui::wantedLevel(ladder(entry), levelView(entry), focusFor(entry));
}

void CustomPlot::refreshCloser()
{
    bool wanted = false;
    bool dropped = false;
    bool filled = false;

    recomputeView();

    if (foldWanted().has_value()) {
        // What is drawn is the fold, and no run below would be looked at. See
        // DatasetPlot::refreshDetail.
        settle_.stop();
        return;
    }

    // The time base first, and the order is the whole of why it works.
    //
    // Every run below is a run of *positions*, and under Dataset a position is
    // whatever the time base says it is -- so folding the axis finer tightens
    // the bracket every entry is then measured against. That is why this loops:
    // each pass inverts the view against a sharper reading than the last and so
    // asks for a sharper one still, and it settles at the pyramid's base. Out
    // of a held line every one of those folds is arithmetic over a buffer
    // already in hand, so the loop costs the pane and not the file.
    if (xMode_ == Dataset) {
        std::optional<PlotWindow> own;
        for (int step = 0; step <= kHeldLevels; ++step) {
            own = closerFor(axis_, closerBuckets());
            if (!own.has_value() || !fillCloser(axis_, *own)) {
                break;
            }
            filled = true;
            trimLevels(axis_);
            recomputeView(); // finer, now that a finer reading is in hand
        }
        if (!closerFor(axis_, bucketBudget()).has_value()) {
            if (!axis_.levels.empty()) {
                // Zoomed back out past every run of the axis. Retired rather
                // than freed: the renderer may be drawing against one of them,
                // and the whole-line summary covers every position by
                // construction, so the frame after this is a correct picture
                // either way.
                for (Level& level : axis_.levels) {
                    retire(level.values);
                }
                axis_.levels.clear();
                dropped = true;
                recomputeView();
            }
        }
        else if (own.has_value() && !closerCovers(axis_)) {
            // The one run of the axis the pyramid could not answer, which is a
            // zoom below its base bucket. Asked of the file, as row -1.
            //
            // Only that one. The octaves an entry reads ahead are there because
            // the *next* view would otherwise cost a round trip; a fold costs
            // nothing, so an axis run held against a zoom the reader has not
            // made yet is memory spent on arithmetic that is already free.
            wanted = true;
        }
    }

    // Everything the pyramids can answer, now, in this call.
    //
    // The same loop DatasetPlot::refreshDetail runs and for the same reason: a
    // run at or above an entry's base bucket is a fold of a buffer in hand, so
    // there is nothing to wait for and no reply to arm the next step with. The
    // pane's own preferred run goes in first whether or not a coarser one in
    // hand would have covered it -- see the note there; out of a held line the
    // finer fold has nothing to weigh against it.
    for (Entry& entry : entries_) {
        if (const std::optional<PlotWindow> own = closerFor(entry, closerBuckets());
            own.has_value() && fillCloser(entry, *own)) {
            filled = true;
        }
        int step = 0;
        for (; step <= heldLevels(); ++step) {
            const std::optional<PlotWindow> next = closerWanted(entry);
            if (!next.has_value() || !fillCloser(entry, *next)) {
                break;
            }
            filled = true;
            trimLevels(entry);
        }
        trimLevels(entry);
        // A loop that ran to its bound stopped because it ran out of turns
        // rather than because the ladder was full: closerWanted() and
        // fillCloser() have stopped agreeing about what is held. The bound
        // keeps that from hanging; this is what keeps it from being invisible,
        // because in a release build the symptom would not look like a bug, it
        // would look like the plot had become slow again.
        Q_ASSERT(step <= heldLevels());
    }
    if (filled) {
        announce();
    }

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
    if (wanted && focusActive_) {
        // A zoom reads at once rather than waiting the gesture out. See
        // DatasetPlot::refreshDetail, which argues it: what bounds the cost is
        // one read at a time, not a wait. A pan has no focus and still settles.
        settle_.stop();
        askForCloser();
    }
    else if (wanted) {
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
    if (closerInFlight_) {
        // One read out at a time; the reply arms the next. See
        // DatasetPlot::askForDetail.
        return;
    }
    std::vector<int> rows;
    std::vector<Ask> asks;
    // The time base is row -1: a line too long to hold at bucket one still has
    // octaves below its pyramid's base, and an axis stuck three octaves coarser
    // than the entries drawn against it is a curve laid on a staircase. Asked
    // for on the same terms as any other line, because it is one.
    if (xMode_ == Dataset && !xExpression_.trimmed().isEmpty() && !closerCovers(axis_)) {
        if (const std::optional<PlotWindow> want = closerFor(axis_, closerBuckets());
            want.has_value()) {
            rows.push_back(-1);
            asks.push_back(Ask{xExpression_, want, closerBuckets(), 0});
        }
    }
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        const Entry& entry = entries_[i];
        const std::optional<PlotWindow> want = closerWanted(entry);
        if (!want.has_value()) {
            continue;
        }
        rows.push_back(static_cast<int>(i));
        asks.push_back(Ask{entry.expression, want, closerBuckets(), 0, entry.postprocess});
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
    closerInFlight_ = true;
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
            closerInFlight_ = false;
            const std::size_t count = std::min(rows.size(), reply.lines.size());
            for (std::size_t i = 0; i < count; ++i) {
                Entry* into = nullptr;
                if (rows[i] < 0) {
                    // The time base. Still the one being drawn against, or the
                    // next settle asks about whatever replaced it.
                    if (xMode_ == Dataset && xExpression_ == asks[i].expression) {
                        into = &axis_;
                    }
                }
                else if (const auto row = static_cast<std::size_t>(rows[i]);
                         row < entries_.size() &&
                         entries_[row].expression == asks[i].expression &&
                         entries_[row].postprocess == asks[i].postprocess) {
                    into = &entries_[row];
                }
                if (into == nullptr) {
                    continue; // removed or retyped while this was out
                }
                Entry& entry = *into;
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
    // A ticket reset means the reply in flight will never call its
    // continuation, so the flag it would have cleared is cleared here instead.
    // See DatasetPlot::clearDetail.
    closerRequests_.reset();
    closerInFlight_ = false;
    for (Entry& entry : entries_) {
        for (Level& level : entry.levels) {
            retire(level.values);
        }
        entry.levels.clear();
    }
    // The axis's own runs go with them. They are runs of the time base as it
    // was before whatever is clearing this, which is exactly what a stale run
    // is.
    for (Level& level : axis_.levels) {
        retire(level.values);
    }
    axis_.levels.clear();
    // And every fold, for the same reason: it is a fold of the lines, and of
    // the time base, as they were.
    dropFold();
    recomputeView();
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

void CustomPlot::retire(std::vector<double>& values) const
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
    if (xMode_ == Dataset && axis_.values.empty()) {
        return line;
    }

    // On a logarithmic axis spanning an octave or more, the line folded onto
    // the pane's own columns, and nothing below is asked. See LogColumns.
    if (foldedLine(entry, line)) {
        return line;
    }

    // The closer look, while it covers what is on screen. A second summary of
    // the same entry rather than a replacement for the first: zoom out past the
    // run in hand and the whole-line summary below is what is drawn, in the
    // same frame, because it covers everything by construction.
    if (closerCovers(entry)) {
        const Level& level = entry.levels[static_cast<std::size_t>(drawnLevel(entry))];
        // The run's own start and step, put back on the axis by the same map
        // the whole line is drawn with. Under Align an element is a position;
        // under Stretch the line is spread over the axis, so both the offset
        // and the step are scaled by the same factor.
        const double scale = stretchScale(entry);
        line.positionStart = static_cast<double>(level.window.first) * scale;

        line.values = level.values.data();
        line.count = static_cast<qsizetype>(level.values.size());
        line.positionStep = level.step * scale;
        // A run at bucket one is that run's own elements; anything coarser is
        // an envelope of them, whatever the stretch does to the step.
        line.summarised = level.window.bucket > 1;
        return line;
    }

    line.values = entry.values.data();
    line.count = static_cast<qsizetype>(entry.values.size());
    line.summarised = entry.summarised;
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
        axis.values = axis_.values.data();
        axis.count = static_cast<qsizetype>(axis_.values.size());
        axis.valueStep = axis_.step;
        // ...and the run of it the reader is looking at, which is the same
        // time base folded finer over what is on screen. Handed over *beside*
        // the whole rather than instead of it, for the reason PlotAxis says: a
        // line whose own closer look has not landed yet is still drawn against
        // the positions the whole answers for.
        if (const int at = drawnLevel(axis_); at >= 0) {
            const Level& level = axis_.levels[static_cast<std::size_t>(at)];
            axis.closerValues = level.values.data();
            axis.closerCount = static_cast<qsizetype>(level.values.size());
            axis.closerStart = static_cast<double>(level.window.first);
            axis.closerStep = level.step;
        }
    }
    return axis;
}

void CustomPlot::fill(PlotItem* target)
{
    if (target == nullptr) {
        return;
    }
    // A different item is being handed the lines, so the one that had them is
    // emptied first. Only one item is ever recorded as reading this plot, and
    // everything below frees on that record: the retired store at the end of
    // this call, and every later retire() and releaseDrawing(). An item left
    // holding pointers it was given before would go on drawing through them
    // after they were freed -- a detached custom tab is drawn by a second
    // PlotSurface, and the one it left behind in the tab bar was exactly that.
    if (drawing_ != nullptr && drawing_ != target) {
        drawing_->clear();
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
        const QString path = linePath(entry.expression, entry.postprocess);
        if (!path.isEmpty() && !named.contains(path)) {
            named.append(path);
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
    positiveMinimum_ = 0.0;
    hasFinite_ = false;
    hasPositive_ = false;
    sharedSeries_ = 0;
    bool hasShared = false;
    // A separate axis is a second axis beside the common one, so it is in
    // force only where there is something for it to be separate *from*: a
    // plot of one line has one axis however that line's box is set, and the
    // request is kept so a second line brings it back.
    const int drawnLines = seriesCount();
    for (Entry& entry : entries_) {
        entry.ownAxis = entry.drawn && entry.separateAxis && drawnLines > 1;
        entry.finite = false;
        entry.low = 0.0;
        entry.high = 0.0;
        if (!entry.drawn) {
            continue;
        }
        if (!entry.ownAxis) {
            ++sharedSeries_;
        }
        for (const double value : entry.values) {
            if (!std::isfinite(value)) {
                continue;
            }
            // The line's own extent, for its own axis -- and counted for every
            // line rather than only the separate ones, because it is two
            // comparisons in a loop that is already here.
            entry.low = entry.finite ? std::min(entry.low, value) : value;
            entry.high = entry.finite ? std::max(entry.high, value) : value;
            entry.finite = true;
            ++points_;
            // Whether anything at all is drawable is a question about every
            // line; the extent below is the common axis's and spans only the
            // lines left on it.
            if (entry.ownAxis) {
                hasFinite_ = true;
                continue;
            }
            if (!hasShared) {
                minimum_ = value;
                maximum_ = value;
                hasShared = true;
                hasFinite_ = true;
            }
            else {
                minimum_ = std::min(minimum_, value);
                maximum_ = std::max(maximum_, value);
            }
            // The other end a logarithmic axis needs, taken in the pass that
            // is already touching every value -- when the line has nothing
            // better to answer with. A summary cannot say it: see below.
            if (value > 0.0 && entry.pyramid.empty()) {
                positiveMinimum_ = hasPositive_ ? std::min(positiveMinimum_, value) : value;
                hasPositive_ = true;
            }
        }
        // ...and when it has the line itself, out of that. An envelope keeps
        // a bucket's smallest, so a bucket holding a zero and a thousandth
        // answers with the zero and the thousandth is not in the summary at
        // all -- which on a logarithmic axis is the bottom of the axis gone.
        // See gui::smallestPositive.
        double smallest = 0.0;
        if (!entry.ownAxis && smallestPositive(entry.pyramid, smallest)) {
            positiveMinimum_ = hasPositive_ ? std::min(positiveMinimum_, smallest) : smallest;
            hasPositive_ = true;
        }
    }
    // In Dataset mode a line with no time base to draw against is not drawable
    // however many finite values it holds.
    if (xMode_ == Dataset && axis_.values.empty()) {
        hasFinite_ = false;
    }

    // The time base's own extent, which is what the axis is drawn between. Its
    // ends rather than its first and last value: a time base is usually
    // ascending and is not required to be, and an axis drawn from the first to
    // the last of a run that doubles back would leave half the points outside
    // the frame.
    xMinimum_ = 0.0;
    xMaximum_ = 1.0;
    xPositiveMinimum_ = 0.0;
    bool seen = false;
    bool seenPositive = false;
    for (const double value : axis_.values) {
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
        // Where a logarithmic x axis starts. The ends of the time base are the
        // whole of the axis on a linear one; on a logarithmic one the end below
        // zero is not an end it has.
        if (value > 0.0) {
            xPositiveMinimum_ = seenPositive ? std::min(xPositiveMinimum_, value) : value;
            seenPositive = true;
        }
    }
    // Out of the time base itself where it is held, for the reason the lines
    // above take theirs from their pyramids -- and it matters more here. The
    // summary is pairs of extremes, and the first pair of a time base from
    // zero is the zero and the end of the first bucket, so a logarithmic axis
    // began a bucket along and every element before that was off the pane.
    if (double smallest = 0.0; smallestPositive(axis_.pyramid, smallest)) {
        xPositiveMinimum_ = smallest;
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

    // The time base is held at every resolution too, and that is the change
    // that made a time series zoomable.
    //
    // It used to be asked for with no budget, under "a time base is read to
    // turn an axis position into a printed x, one value per drawn point -- it
    // is not a line anybody zooms into". The first half is true and the second
    // does not follow: an axis summarised to a pane's worth of points has one x
    // per column, so a reader zoomed past that is handed the same x for every
    // sample in the column and the curve collapses onto a staircase. The axis
    // has to resolve with the lines it carries, so it is read like one.
    const long long budget = pyramidBudget();

    std::vector<Ask> asks;
    asks.reserve(entries_.size() + 1);
    if (wantsX) {
        asks.push_back(Ask{xExpression_, {}, bucketBudget(), budget});
    }
    for (const Entry& entry : entries_) {
        asks.push_back(Ask{entry.expression, {}, bucketBudget(), budget, entry.postprocess});
    }

    if (asks.empty()) {
        axis_.problem.clear();
        retire(axis_.values);
        axis_.step = 1.0;
        axis_.sourceLength = 0;
        axis_.pyramid = {};
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
    closerInFlight_ = false;
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
                    axis_.problem = answer.problem;
                    retire(axis_.values);
                    axis_.values = std::move(answer.values);
                    // The time base's own thinning, which is what turns an
                    // axis position back into one of these values. It is not
                    // the entries' -- they are thinned against the same pane
                    // and need not be the same length as it.
                    axis_.step = answer.step;
                    axis_.sourceLength = answer.sourceLength;
                    axis_.pyramid = std::move(answer.pyramid);
                }
                at = 1;
            }
            else {
                axis_.problem.clear();
                retire(axis_.values);
                axis_.step = 1.0;
                axis_.sourceLength = 0;
                axis_.pyramid = {};
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
                entry.summarised = answer.summarised;
                entry.sourceLength = answer.sourceLength;
                // The elements the summary above was folded out of. Every
                // closer look at this entry from here on is a fold of these
                // rather than a second reading of the file.
                entry.pyramid = std::move(answer.pyramid);
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
        QVariantMap fields{{QStringLiteral("expression"), entry.expression},
                           {QStringLiteral("alias"), entry.alias},
                           {QStringLiteral("scaling"), entry.scaling == Stretch
                                                           ? QStringLiteral("stretch")
                                                           : QStringLiteral("align")},
                           {QStringLiteral("drawn"), entry.drawn}};
        // Written only where there is one, so that a view saved before this
        // existed and a view whose lines take the cycle are the same
        // document. CustomPlotSet::jsonOf turns a QColor into "#aarrggbb" on
        // the way out and a QML `color` reads that back without being asked,
        // so the round trip needs nothing on the way in.
        if (entry.colour.isValid()) {
            fields.insert(QStringLiteral("colour"), entry.colour);
        }
        // The same rule for the same reason: written only where it says
        // something, so every view saved before separate axes existed is a
        // view whose lines share one.
        if (entry.separateAxis) {
            fields.insert(QStringLiteral("separateAxis"), true);
        }
        if (entry.axisFixed) {
            fields.insert(QStringLiteral("axisFixed"), true);
        }
        if (!entry.axisLabel.isEmpty()) {
            fields.insert(QStringLiteral("axisLabel"), entry.axisLabel);
        }
        // Again only where it says something, so every view saved before a
        // line could be a pipeline is a view of slices, which it is.
        if (entry.postprocess) {
            fields.insert(QStringLiteral("postprocess"), true);
        }
        rows.append(fields);
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
        // Absent means "no colour of its own", which is what every view saved
        // before 0.6.3 says and is why they migrate for nothing. A value that
        // is not a colour is absent as well: QColor's string constructor
        // leaves it invalid, which is exactly the state wanted.
        const QVariant colour = fields.value(QStringLiteral("colour"));
        if (colour.isValid()) {
            entry.colour =
                colour.canConvert<QColor>() ? colour.value<QColor>() : QColor(colour.toString());
        }
        entry.separateAxis = fields.value(QStringLiteral("separateAxis"), false).toBool();
        entry.axisFixed = fields.value(QStringLiteral("axisFixed"), false).toBool();
        entry.axisLabel = fields.value(QStringLiteral("axisLabel")).toString().trimmed();
        entry.postprocess = fields.value(QStringLiteral("postprocess"), false).toBool();
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
