// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "PlotPyramid.hpp"

#include "PlotLevels.hpp"

#include <QSemaphore>
#include <QThreadPool>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace gui {

namespace {

/// `value` rounded up to a power of two, at least one.
[[nodiscard]] long long roundUpPowerOfTwo(long long value)
{
    long long power = 1;
    while (power < value && power < (1LL << 40)) {
        power <<= 1;
    }
    return power;
}

/// Doubles one piece of a fold is worth spreading over a thread.
///
/// Below this the pool costs more than the fold does: posting a task, waking a
/// thread and waiting on a semaphore is a few microseconds, and a fold of a few
/// thousand doubles is less than that. So a frame's own fold stays on the thread
/// that asked for it -- which is what keeps it microseconds -- and only the
/// passes over a whole line are spread.
constexpr long long kFoldGrain = 1 << 16;

/// Run `body(from, to)` over `[0, units)`, in parallel when it is worth it.
///
/// `work` is how many doubles the whole of it walks, and it is a separate
/// argument because it is not `units`: a fold answers with one bucket per unit
/// and reads `work / units` elements to find each one, so a summary of ten
/// million elements into two thousand buckets is two thousand units and ten
/// million doubles of work. Splitting on the units would leave that one on this
/// thread, which is the one fold large enough that it must not be.
template<typename F>
void overRanges(long long units, long long work, F body)
{
    const int threads = std::max(QThreadPool::globalInstance()->maxThreadCount(), 1);
    const long long pieces = std::min<long long>(std::min<long long>(threads, units),
                                                 std::max<long long>(1, work / kFoldGrain));
    if (pieces <= 1) {
        body(0, units);
        return;
    }
    const long long each = (units + pieces - 1) / pieces;
    QSemaphore done;
    long long started = 0;
    for (long long piece = 0; piece < pieces; ++piece) {
        const long long from = piece * each;
        if (from >= units) {
            break;
        }
        const long long to = std::min(from + each, units);
        ++started;
        QThreadPool::globalInstance()->start([&done, &body, from, to] {
            body(from, to);
            done.release();
        });
    }
    // Acquired rather than left to the pool's own wait: this blocks until every
    // piece is finished, which is what makes `body` safe to capture by
    // reference and the output safe to read on return.
    done.acquire(static_cast<int>(started));
}

} // namespace

std::size_t LinePyramid::doubles() const
{
    std::size_t total = 0;
    for (const PyramidLevel& level : levels) {
        total += level.values.size();
    }
    return total;
}

long long pyramidDoubles(long long length, long long base)
{
    if (length <= 0) {
        return 0;
    }
    const long long bucket = std::max<long long>(base, 1);
    const long long size = bucket == 1 ? length : 2 * ((length + bucket - 1) / bucket);
    // The levels above add a quarter, a sixteenth, a sixty-fourth... which sums
    // to a third. Rounded up, because the top few levels are a handful of
    // doubles each and there are more of them than the series accounts for.
    return size + size / 3 + 64;
}

long long baseBucketFor(long long length, long long budget)
{
    if (length <= 0) {
        return 1;
    }
    if (budget <= 0) {
        return roundUpPowerOfTwo(length); // one bucket: the cheapest pyramid there is
    }
    // Doubled until it fits, rather than solved: a pyramid's cost is not quite
    // a closed form -- the levels above the base are a rounded-up geometric sum
    // -- and there are at most forty steps of this for any line that exists.
    long long base = 1;
    while (pyramidDoubles(length, base) > budget && base < length) {
        base <<= 1;
    }
    return base;
}

bool coarsenTo(LinePyramid& pyramid, long long base)
{
    std::size_t drop = 0;
    while (drop + 1 < pyramid.levels.size() && pyramid.levels[drop].bucket < base) {
        ++drop;
    }
    if (drop == 0) {
        return false;
    }
    pyramid.levels.erase(pyramid.levels.begin(),
                         pyramid.levels.begin() + static_cast<long>(drop));
    // The vectors that went are the whole point, so the capacity goes with
    // them: a std::vector erased out of the front of another leaves the
    // survivors where they were, but the elements removed are destroyed and
    // their buffers freed, which is the memory the reader asked for back.
    pyramid.levels.shrink_to_fit();
    return true;
}

void buildLevels(LinePyramid& pyramid)
{
    if (pyramid.levels.empty()) {
        return;
    }
    constexpr long long factor = 1LL << kPyramidOctaves;

    while (pyramid.levels.back().buckets() > 1) {
        const PyramidLevel& below = pyramid.levels.back();
        PyramidLevel level;
        level.bucket = below.bucket * factor;

        const long long buckets = (below.buckets() + factor - 1) / factor;
        level.values.resize(static_cast<std::size_t>(buckets) * 2);

        const double* source = below.values.data();
        const long long sourceBuckets = below.buckets();
        const bool raw = below.bucket == 1;
        double* out = level.values.data();

        // Every output bucket is a fold of `factor` input buckets, and no two
        // of them share an input -- so the ranges are disjoint and there is
        // nothing to lock.
        overRanges(buckets, static_cast<long long>(below.values.size()),
                   [&](long long from, long long to) {
                       const long long first = from * factor;
                       const long long count = std::min(to * factor, sourceBuckets) - first;
                       if (count <= 0) {
                           return;
                       }
                       if (raw) {
                           reduceBucketsInto(source + first, count, factor, out + from * 2);
                       }
                       else {
                           coarsenEnvelopeInto(source + first * 2, count, factor, out + from * 2);
                       }
                   });

        pyramid.levels.push_back(std::move(level));
        if (pyramid.levels.size() > 64) {
            break; // a line no dataset has; the loop must still end
        }
    }
}

PyramidBuilder::PyramidBuilder(long long length, long long base)
    : base_(std::max<long long>(base, 1))
{
    pyramid_.length = std::max<long long>(length, 0);
    PyramidLevel bottom;
    bottom.bucket = base_;
    // Reserved and appended to rather than sized and overwritten: sizing it
    // writes the whole buffer once before a single element has been read, which
    // on a large line is hundreds of megabytes of zeroes and the page faults
    // that come with first touching them.
    const long long buckets = (pyramid_.length + base_ - 1) / base_;
    bottom.values.reserve(static_cast<std::size_t>(base_ == 1 ? pyramid_.length : buckets * 2));
    pyramid_.levels.push_back(std::move(bottom));
}

void PyramidBuilder::add(const double* values, long long count)
{
    if (values == nullptr || count <= 0) {
        return;
    }
    taken_ += count;
    std::vector<double>& bottom = pyramid_.levels.front().values;

    const auto fold = [&](const double* from, long long many) {
        if (base_ == 1) {
            bottom.insert(bottom.end(), from, from + many);
        }
        else {
            reduceBuckets(from, many, base_, bottom);
        }
    };

    if (carry_.empty() && count % base_ == 0) {
        // Straight out of the read, which is every read but the last: kReadRun
        // is a power of two and so is the base. The carry below is for the ones
        // that are not -- a scattered subscript breaking the run, or the tail
        // of the line -- and going through it otherwise would be a second copy
        // of every element.
        fold(values, count);
    }
    else {
        carry_.insert(carry_.end(), values, values + count);
        const long long whole = (static_cast<long long>(carry_.size()) / base_) * base_;
        if (whole > 0) {
            fold(carry_.data(), whole);
            carry_.erase(carry_.begin(), carry_.begin() + whole);
        }
    }
}

LinePyramid PyramidBuilder::finish()
{
    if (!carry_.empty()) {
        std::vector<double>& bottom = pyramid_.levels.front().values;
        if (base_ == 1) {
            bottom.insert(bottom.end(), carry_.begin(), carry_.end());
        }
        else {
            reduceBuckets(carry_.data(), static_cast<long long>(carry_.size()), base_, bottom);
        }
        carry_.clear();
    }
    // The length is what was handed in, not what was promised. Everything that
    // reads a pyramid -- a fold, a column, the extremes over a run -- indexes
    // its base up to `length` and trusts the base to be that long, so a line
    // whose reads stopped short of the length it was built for would otherwise
    // be read past the end of the buffer holding it. The caller that stops
    // early on purpose used to have to say so by hand afterwards; now nothing
    // can forget to.
    pyramid_.length = std::min(pyramid_.length, taken_);
    // The levels above the base are built here, whole and over the machine,
    // rather than a chunk at a time as each read lands.
    //
    // Folding them chunk by chunk was tried, on the argument that a level made
    // out of a few kilobytes still in cache beats one made out of a re-read of
    // the whole base. It is half again as slow, and measurably: the fold is
    // *compute* bound, not memory bound -- extremesOf() tests every element for
    // being finite and branches on it, which neither vectorises nor prefetches
    // away -- so what the ladder costs is sixteen million of those and what
    // decides it is how many cores are doing them. Chunk by chunk that is one.
    buildLevels(pyramid_);
    return std::move(pyramid_);
}

LinePyramid pyramidOf(const double* values, long long count, long long base)
{
    LinePyramid pyramid;
    if (values == nullptr || count <= 0) {
        return pyramid;
    }
    pyramid.length = count;

    PyramidLevel bottom;
    bottom.bucket = std::max<long long>(base, 1);
    if (bottom.bucket == 1) {
        bottom.values.assign(values, values + count);
    }
    else {
        const long long buckets = (count + bottom.bucket - 1) / bottom.bucket;
        bottom.values.resize(static_cast<std::size_t>(buckets) * 2);
        double* out = bottom.values.data();
        const long long width = bottom.bucket;
        overRanges(buckets, count, [&](long long from, long long to) {
            const long long first = from * width;
            const long long take = std::min(to * width, count) - first;
            if (take > 0) {
                reduceBucketsInto(values + first, take, width, out + from * 2);
            }
        });
    }
    pyramid.levels.push_back(std::move(bottom));

    buildLevels(pyramid);
    return pyramid;
}

namespace {

/// The coarsest level a bucket of `bucket` can be folded out of, or -1.
///
/// Coarsest, because it is the least work: a level four times finer than the
/// one asked for is four times the doubles to walk. But it has to *divide*
/// `bucket` and not merely be no larger than it -- a fold of a level whose
/// buckets straddle the wanted boundaries would answer about the wrong
/// elements and drift further out the longer the line.
///
/// Every bucket the zoom asks for is a power of two and every level is a power
/// of four, so there is always one within a factor of two. The whole-line
/// summary is the case that is not: its stride is `ceil(length / buckets)` and
/// can be any integer at all, so it usually lands on the base and pays a pass
/// over the whole line for it. That is once per pane width, not once per frame.
[[nodiscard]] int levelFor(const LinePyramid& pyramid, long long bucket)
{
    int best = -1;
    for (std::size_t i = 0; i < pyramid.levels.size(); ++i) {
        if (pyramid.levels[i].bucket > bucket) {
            break; // finest first, so everything past this one is coarser still
        }
        if (bucket % pyramid.levels[i].bucket == 0) {
            best = static_cast<int>(i);
        }
    }
    return best;
}

/// Fold `[first, first + span)` of the line at `bucket`, out of the pyramid.
[[nodiscard]] bool foldRun(const LinePyramid& pyramid, long long first, long long span,
                           long long bucket, std::vector<double>& out)
{
    out.clear();
    if (pyramid.empty() || bucket < pyramid.baseBucket() || first < 0 || span <= 0) {
        return false;
    }
    const int at = levelFor(pyramid, bucket);
    if (at < 0) {
        return false;
    }
    const PyramidLevel& level = pyramid.levels[static_cast<std::size_t>(at)];
    if (first % level.bucket != 0) {
        // Nothing in the plot asks for one -- windowFor aligns a run to a
        // multiple of its own bucket, and every level's bucket divides that --
        // but an unaligned fold would silently answer about the wrong elements,
        // so it is refused rather than approximated.
        return false;
    }
    const long long take = std::min(span, pyramid.length - first);
    if (take <= 0) {
        return false;
    }
    const long long factor = bucket / level.bucket;

    if (level.bucket == 1) {
        if (bucket == 1) {
            // Sample for sample. The consecutive path of a read, which answers
            // with the elements themselves rather than with pairs of them.
            out.assign(level.values.data() + first, level.values.data() + first + take);
            return true;
        }
        // Sized and then written in ranges rather than appended to, so that the
        // walk can be spread over the machine. It matters in one place and
        // matters a lot there: the whole-line summary's stride is whatever
        // `ceil(length / buckets)` comes to and is usually odd, so the only
        // level that divides it is the base -- which makes the summary a fold
        // of every element of the line. On ten million that was a third of what
        // the first picture cost. Every *other* fold is a paneful of buckets and
        // stays on this thread, because below the grain a thread pool costs more
        // than the fold does.
        const long long made = (take + bucket - 1) / bucket;
        out.resize(static_cast<std::size_t>(made) * 2);
        double* into = out.data();
        const double* from = level.values.data() + first;
        overRanges(made, take, [&](long long a, long long b) {
            const long long at = a * bucket;
            reduceBucketsInto(from + at, std::min(b * bucket, take) - at, bucket, into + a * 2);
        });
        return true;
    }

    const long long fromBucket = first / level.bucket;
    const long long buckets =
        std::min((take + level.bucket - 1) / level.bucket, level.buckets() - fromBucket);
    if (buckets <= 0) {
        return false;
    }
    const double* pairs = level.values.data() + fromBucket * 2;
    if (factor == 1) {
        out.assign(pairs, pairs + buckets * 2);
        return true;
    }
    const long long made = (buckets + factor - 1) / factor;
    out.resize(static_cast<std::size_t>(made) * 2);
    double* into = out.data();
    overRanges(made, buckets * 2, [&](long long a, long long b) {
        const long long at = a * factor;
        coarsenEnvelopeInto(pairs + at * 2, std::min(b * factor, buckets) - at, factor,
                            into + a * 2);
    });
    return true;
}

} // namespace

bool fillWindow(const LinePyramid& pyramid, const PlotWindow& window, std::vector<double>& out)
{
    return foldRun(pyramid, window.first, window.span, window.bucket, out);
}

bool fillWhole(const LinePyramid& pyramid, int buckets, std::vector<double>& out, long long& stride,
               double& step)
{
    out.clear();
    if (pyramid.empty() || buckets <= 0) {
        return false;
    }
    const long long base = pyramid.baseBucket();
    long long wanted = (pyramid.length + buckets - 1) / buckets;
    // Up to a multiple of the base, which is what makes the fold exact. With a
    // base of one -- every line the budget can hold raw, which is every line in
    // the test suite and most of the ones a reader opens -- this changes nothing
    // at all and the answer is the file's own, element for element.
    //
    // It is not rounded any further than that. A coarser stride would fold out
    // of a coarser level and cost a fraction of the walk, but the summary is
    // what the y axis is drawn against and what the footer counts, and buying a
    // few milliseconds with a resolution nobody asked to give up is the wrong
    // trade. The walk is spread over the machine instead -- see foldRun.
    wanted = ((wanted + base - 1) / base) * base;
    stride = std::max<long long>(wanted, 1);

    if (!foldRun(pyramid, 0, pyramid.length, stride, out)) {
        return false;
    }
    // What sampleFrom reports beside the values: an envelope answers with two
    // per bucket half a bucket apart, and a stride of one is the elements
    // themselves.
    step = stride == 1 ? 1.0 : static_cast<double>(stride) / 2.0;
    return true;
}

Extremes extremesOver(const LinePyramid& pyramid, long long first, long long last)
{
    Extremes found;
    if (pyramid.empty()) {
        return found;
    }
    const long long base = pyramid.baseBucket();
    first = std::max<long long>(first, 0);
    last = std::min(last, pyramid.length);
    first = (first / base) * base;
    if (last < pyramid.length) {
        last = ((last + base - 1) / base) * base;
        last = std::min(last, pyramid.length);
    }

    const auto take = [&found](double value, long long at) {
        if (!std::isfinite(value)) {
            return;
        }
        if (found.lowAt < 0 || value < found.lowest) {
            found.lowest = value;
            found.lowAt = at;
        }
        if (found.highAt < 0 || value > found.highest) {
            found.highest = value;
            found.highAt = at;
        }
    };

    long long at = first;
    while (at < last) {
        // The coarsest level whose bucket starts here and ends inside the run
        // -- or at the end of the line, which a run reaching it is allowed to
        // take whole. The base always qualifies, because `at` is on it.
        std::size_t use = 0;
        for (std::size_t i = 1; i < pyramid.levels.size(); ++i) {
            const long long bucket = pyramid.levels[i].bucket;
            if (at % bucket != 0 || std::min(at + bucket, pyramid.length) > last) {
                break; // coarser still cannot fit where this one did not
            }
            use = i;
        }
        const PyramidLevel& level = pyramid.levels[use];
        const long long index = at / level.bucket;
        if (level.bucket == 1) {
            take(level.values[static_cast<std::size_t>(index)], at);
        }
        else if (index < level.buckets()) {
            // A pair in the order its two occurred, so the first is taken as
            // earlier than the second -- the bucket's start and its middle.
            take(level.values[static_cast<std::size_t>(index) * 2], at);
            take(level.values[static_cast<std::size_t>(index) * 2 + 1], at + level.bucket / 2);
        }
        at += level.bucket;
    }
    return found;
}

void foldColumns(const LinePyramid& pyramid, std::span<const double> edges, ColumnFold& out)
{
    out.values.clear();
    out.positions.clear();
    out.summarised = false;
    if (pyramid.empty() || edges.size() < 2) {
        return;
    }
    const long long base = pyramid.baseBucket();
    const long long length = pyramid.length;
    const PyramidLevel& bottom = pyramid.levels.front();
    out.values.reserve(edges.size() * 2);
    out.positions.reserve(edges.size() * 2);

    // Where the last column stopped. A column never starts before it, which is
    // what keeps an element out of two columns once the ends are rounded to
    // the base.
    long long cursor = 0;
    for (std::size_t c = 0; c + 1 < edges.size(); ++c) {
        const double from = edges[c];
        const double to = edges[c + 1];
        if (!(to > from) || !(to > 0.0) || !(from < static_cast<double>(length))) {
            continue;
        }
        long long first = static_cast<long long>(std::ceil(std::max(from, 0.0)));
        long long last =
            static_cast<long long>(std::min(std::ceil(to), static_cast<double>(length)));
        if (base > 1) {
            first = (first / base) * base;
            last = std::min(((last + base - 1) / base) * base, length);
        }
        first = std::max(first, cursor);
        if (last <= first) {
            continue;
        }
        cursor = last;

        if (base == 1 && last - first <= 2) {
            for (long long i = first; i < last; ++i) {
                out.values.push_back(bottom.values[static_cast<std::size_t>(i)]);
                out.positions.push_back(static_cast<double>(i));
            }
            continue;
        }

        out.summarised = true;
        const Extremes found = extremesOver(pyramid, first, last);
        if (!found.found()) {
            out.values.push_back(std::numeric_limits<double>::quiet_NaN());
            out.positions.push_back(static_cast<double>(first));
            continue;
        }
        out.values.push_back(found.first());
        out.positions.push_back(static_cast<double>(first));
        out.values.push_back(found.second());
        out.positions.push_back(static_cast<double>(first) +
                                static_cast<double>(last - first) / 2.0);
    }
}

bool smallestPositive(const LinePyramid& pyramid, double& out)
{
    if (pyramid.empty()) {
        return false;
    }
    constexpr long long factor = 1LL << kPyramidOctaves;
    bool found = false;
    double best = 0.0;
    const auto offer = [&](double value) {
        if (value > 0.0 && std::isfinite(value) && (!found || value < best)) {
            best = value;
            found = true;
        }
    };

    // Depth first, with the buckets still to open on a stack of (level,
    // bucket). The top level is a handful of buckets, so it seeds the stack.
    std::vector<std::pair<std::size_t, long long>> open;
    const std::size_t top = pyramid.levels.size() - 1;
    for (long long j = pyramid.levels[top].buckets() - 1; j >= 0; --j) {
        open.emplace_back(top, j);
    }
    while (!open.empty()) {
        const auto [at, j] = open.back();
        open.pop_back();
        const PyramidLevel& level = pyramid.levels[at];
        if (level.bucket == 1) {
            offer(level.values[static_cast<std::size_t>(j)]);
            continue;
        }
        const double a = level.values[static_cast<std::size_t>(j) * 2];
        const double b = level.values[static_cast<std::size_t>(j) * 2 + 1];
        if (std::isnan(a) || std::isnan(b)) {
            continue; // nothing finite under it
        }
        const double low = std::min(a, b);
        const double high = std::max(a, b);
        if (!(high > 0.0)) {
            continue; // nothing above zero under it
        }
        if (low > 0.0) {
            // All of it is above zero, so its smallest is its answer.
            offer(low);
            continue;
        }
        if (at == 0) {
            // A bucket of the base that straddles zero: its largest is the
            // nearest thing above zero it can vouch for.
            offer(high);
            continue;
        }
        const PyramidLevel& below = pyramid.levels[at - 1];
        const long long last = std::min((j + 1) * factor, below.buckets());
        for (long long child = last - 1; child >= j * factor; --child) {
            open.emplace_back(at - 1, child);
        }
    }
    out = best;
    return found;
}

} // namespace gui
