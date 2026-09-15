// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "postproc/Subscripts.hpp"

#include <QLatin1String>
#include <QStringView>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <optional>
#include <unordered_set>

namespace postproc {
namespace {

/// Read one bound or step of a slice term as a signed number.
///
/// `keyword` is the word that stands for the implicit value ("start" or "end"),
/// and an empty text means the same thing. Signed rather than unsigned because
/// a negative bound counts from the end of the dimension, which is the whole
/// point of writing one.
bool parseBound(QStringView text, const QLatin1String keyword, qint64 implicit,
                qint64& out, QString& error)
{
    const QStringView trimmed = text.trimmed();
    if (trimmed.isEmpty() || trimmed.compare(keyword, Qt::CaseInsensitive) == 0) {
        out = implicit;
        return true;
    }

    bool ok = false;
    const qlonglong value = trimmed.toLongLong(&ok);
    if (!ok) {
        error = QStringLiteral("'%1' is not an index").arg(trimmed.toString());
        return false;
    }
    out = static_cast<qint64>(value);
    return true;
}

/// CPython's own slice arithmetic, which is the contract this grammar is
/// borrowing: PySlice_AdjustIndices, written out.
///
/// The asymmetry is deliberate and is Python's. Going forwards a bound may
/// reach `extent`, because it is exclusive; going backwards it may reach -1,
/// because that is the position before the first element and a descent has to
/// be able to stop after it. Both ends are *clamped* rather than refused: in
/// Python `a[0:100]` on a list of ten is the whole list, and only a bare index
/// past the end is an error.
void adjustSlice(qint64 extent, qint64 step, std::optional<qint64> start,
                 std::optional<qint64> stop, qint64& from, qint64& to)
{
    const qint64 lower = (step < 0) ? -1 : 0;
    const qint64 upper = (step < 0) ? extent - 1 : extent;

    if (!start.has_value()) {
        from = (step < 0) ? upper : lower;
    } else {
        from = *start;
        if (from < 0) {
            from += extent;
            from = std::max(from, lower);
        } else {
            from = std::min(from, upper);
        }
    }

    if (!stop.has_value()) {
        to = (step < 0) ? lower : upper;
    } else {
        to = *stop;
        if (to < 0) {
            to += extent;
            to = std::max(to, lower);
        } else {
            to = std::min(to, upper);
        }
    }
}

/// One term of an expression as arithmetic, before any index is written down:
/// where it starts, how far it steps, and how many it names.
///
/// Split out of parseTerm so that the same grammar can answer "how many" without
/// producing them. See countSubscripts for why that question is worth its own
/// path: `:` on a dimension of ten million is ten million indices, and a shape
/// needs only the number.
struct Run
{
    qint64 from = 0;
    qint64 step = 1;
    hsize_t count = 0;
    IndexExpression::Form form = IndexExpression::Form::Scattered;
};

/// One term resolved to a Run. The whole of the term grammar lives here; the
/// two callers below differ only in what they do with the answer.
bool resolveTerm(QStringView term, hsize_t extent, Run& run, QString& error)
{
    const auto signedExtent = static_cast<qint64>(extent);

    const qsizetype firstColon = term.indexOf(u':');
    if (firstColon < 0) {
        qint64 index = 0;
        if (!parseBound(term, QLatin1String("start"), 0, index, error)) {
            return false;
        }
        // Python's own rule for a bare subscript: it may count from the end,
        // and it may not name an element that is not there.
        const qint64 resolved = (index < 0) ? index + signedExtent : index;
        if (resolved < 0 || resolved >= signedExtent) {
            error = QStringLiteral("%1 is past the end of this dimension "
                                   "(0 to %2, or -1 to -%3 from the end)")
                        .arg(index)
                        .arg(signedExtent - 1)
                        .arg(signedExtent);
            return false;
        }
        run = Run{resolved, 1, 1, IndexExpression::Form::Single};
        return true;
    }

    const QStringView lowerText = term.left(firstColon);
    QStringView rest = term.mid(firstColon + 1);
    const qsizetype secondColon = rest.indexOf(u':');
    QStringView upperText = rest;
    QStringView stepText;
    if (secondColon >= 0) {
        upperText = rest.left(secondColon);
        stepText = rest.mid(secondColon + 1);
        if (stepText.contains(u':')) {
            error = QStringLiteral("'%1' has more than one step").arg(term.toString());
            return false;
        }
    }

    qint64 step = 1;
    if (!parseBound(stepText, QLatin1String("step"), 1, step, error)) {
        return false;
    }
    if (step == 0) {
        error = QStringLiteral("a step of zero selects nothing and never ends");
        return false;
    }

    // An omitted bound and one written as its keyword are the same thing, and
    // both are "whichever end the step is coming from" rather than a number.
    const auto stated = [](QStringView text, QLatin1String keyword,
                           qint64 value) -> std::optional<qint64> {
        const QStringView trimmed = text.trimmed();
        if (trimmed.isEmpty() || trimmed.compare(keyword, Qt::CaseInsensitive) == 0) {
            return std::nullopt;
        }
        return value;
    };

    qint64 lowerValue = 0;
    qint64 upperValue = 0;
    if (!parseBound(lowerText, QLatin1String("start"), 0, lowerValue, error)
        || !parseBound(upperText, QLatin1String("end"), 0, upperValue, error)) {
        return false;
    }

    qint64 from = 0;
    qint64 to = 0;
    adjustSlice(signedExtent, step,
                stated(lowerText, QLatin1String("start"), lowerValue),
                stated(upperText, QLatin1String("end"), upperValue), from, to);

    // How many the run names, by arithmetic rather than by counting them out:
    // ceil((to - from) / step) in the direction of travel, and none at all when
    // the bounds are the wrong way round for it.
    const qint64 reach = (step > 0) ? to - from : from - to;
    const qint64 size = std::abs(step);
    const hsize_t taken =
        reach > 0 ? static_cast<hsize_t>((reach + size - 1) / size) : hsize_t{0};

    // A step-1 slice is a run, and a run is something the data settings panel
    // can draw with its own two boxes. Anything else -- a stride, a descent --
    // is only expressible as the expression that produced it.
    IndexExpression::Form form = IndexExpression::Form::Scattered;
    if (step == 1 && taken > 0) {
        form = (taken == extent) ? IndexExpression::Form::Whole
                                 : IndexExpression::Form::Span;
    }
    run = Run{from, step, taken, form};
    return true;
}

/// One term of an expression, appended to `out`, and how it was written.
bool parseTerm(QStringView term, hsize_t extent, std::vector<hsize_t>& out,
               IndexExpression::Form& form, QString& error)
{
    Run run;
    if (!resolveTerm(term, extent, run, error)) {
        return false;
    }
    out.reserve(out.size() + static_cast<std::size_t>(run.count));
    qint64 at = run.from;
    for (hsize_t i = 0; i < run.count; ++i, at += run.step) {
        out.push_back(static_cast<hsize_t>(at));
    }
    form = run.form;
    return true;
}

/// Split a slice body at the commas that separate one dimension's subscript
/// from the next -- which is not every comma, because a Custom selection of
/// scattered indices is itself a bracketed, comma-separated list.
///
/// Returns false and sets `error` on a bracket that does not pair up, rather
/// than silently reading "[0,2" as two subscripts.
bool splitSubscripts(const QString& text, QStringList& out, QString& error)
{
    int depth = 0;
    qsizetype start = 0;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar character = text.at(i);
        if (character == u'[') {
            ++depth;
        } else if (character == u']') {
            if (depth == 0) {
                error = QStringLiteral("a ']' here closes a '[' that was never "
                                       "opened");
                return false;
            }
            --depth;
        } else if (character == u',' && depth == 0) {
            out << text.mid(start, i - start);
            start = i + 1;
        }
    }
    if (depth > 0) {
        error = QStringLiteral("a '[' here is never closed");
        return false;
    }
    out << text.mid(start);
    return true;
}

/// "1 subscript", "3 subscripts". The count is the point of the sentence this
/// goes into, so it is never written as a bare number.
QString countedSubscripts(qsizetype count)
{
    return QStringLiteral("%1 subscript%2")
        .arg(count)
        .arg(count == 1 ? QString{} : QStringLiteral("s"));
}

} // namespace

IndexExpression parseIndexExpression(const QString& text, hsize_t extent)
{
    IndexExpression result;

    if (text.trimmed().isEmpty()) {
        result.error = QStringLiteral("enter indices, e.g. 0,2,5:9");
        return result;
    }
    if (extent == 0) {
        result.error = QStringLiteral("this dimension is empty");
        return result;
    }

    // A bracketed list is accepted so the slice line pastes straight back in.
    QStringView body = QStringView(text).trimmed();
    bool bracketed = false;
    if (body.startsWith(u'[') && body.endsWith(u']')) {
        body = body.mid(1, body.size() - 2).trimmed();
        bracketed = true;
        if (body.isEmpty()) {
            result.error = QStringLiteral("enter indices, e.g. 0,2,5:9");
            return result;
        }
    }

    const QList<QStringView> terms = body.split(u',');
    IndexExpression::Form form = IndexExpression::Form::Scattered;
    for (const QStringView rawTerm : terms) {
        const QStringView term = rawTerm.trimmed();
        if (term.isEmpty()) {
            result.error = QStringLiteral("empty term between commas");
            return result;
        }
        IndexExpression::Form termForm = IndexExpression::Form::Scattered;
        if (!parseTerm(term, extent, result.indices, termForm, result.error)) {
            return result;
        }
        form = termForm;
    }

    // Duplicates go, order stays. A table cannot show one element twice, and
    // the order a descent or a scattered list names its indices in is part of
    // what was asked for.
    //
    // One term cannot repeat itself: a bare index is one element and a slice
    // counts by a fixed non-zero step, so it visits each index at most once.
    // Only a list can say the same thing twice, and only a list pays for the
    // check -- which matters, because `:` on a dimension of two million is one
    // term and asking whether each of those two million indices had been seen
    // before is where this used to spend the afternoon. It was never noticed
    // while only a *custom* subscript reached the parser; the postprocessing
    // pipeline parses every subscript of every dimension, including the plain
    // ones the panel used to resolve for itself.
    bool deduplicated = false;
    if (terms.size() > 1) {
        std::vector<hsize_t> unique;
        unique.reserve(result.indices.size());
        std::unordered_set<hsize_t> seen;
        seen.reserve(result.indices.size());
        for (const hsize_t index : result.indices) {
            if (seen.insert(index).second) {
                unique.push_back(index);
            }
        }
        deduplicated = unique.size() != result.indices.size();
        result.indices = std::move(unique);
    }

    if (result.indices.empty()) {
        result.error = QStringLiteral("selects no indices");
        return result;
    }

    // The written form survives only when the expression was one plain term.
    // A list, a bracketed run or anything that lost a duplicate is a selection
    // that only its own text describes.
    if (terms.size() != 1 || bracketed || deduplicated) {
        form = IndexExpression::Form::Scattered;
    }
    result.form = form;
    if (form == IndexExpression::Form::Single) {
        result.first = result.indices.front();
        result.last = result.indices.front();
    } else if (form == IndexExpression::Form::Span) {
        result.first = result.indices.front();
        result.last = result.indices.back();
    }
    return result;
}

namespace {

/// What a scalar's slice line resolves to: nothing at all, or a refusal.
///
/// Its own function because both readers of a slice line have to answer it the
/// same way and neither has any dimensions to loop over afterwards.
enum class Scalar
{
    NotScalar,
    Whole,
    Refused,
};

Scalar scalarSubscript(const QString& text, qsizetype rank, QString& error)
{
    if (rank != 0) {
        return Scalar::NotScalar;
    }
    // A scalar has no dimensions to subscript, and Python says so: `a[0]` and
    // `a[:]` on a 0-d array are both "too many indices". `a[...]` is not,
    // because the dimensions the ellipsis stands for are the ones nobody wrote,
    // and on a scalar there are none of them -- so it is the array itself,
    // which is a thing a reader may well want a step to be.
    const QString line = text.trimmed();
    if (line.isEmpty() || line == QStringLiteral("...")) {
        return Scalar::Whole;
    }
    error = QStringLiteral("a scalar has no dimensions to subscript — "
                           "'...' is the whole of it");
    return Scalar::Refused;
}

/// The slice line split into exactly one subscript per dimension, with the
/// shorthands filled in.
///
/// Both readers of a line go through this, which is what keeps them agreeing
/// about the two rules that are easy to get subtly different: `...` stands for
/// however many dimensions nobody wrote a subscript for, and so does the end of
/// the line.
bool expandSubscripts(const QString& text, qsizetype rank, QStringList& expanded,
                      QString& error)
{
    QStringList terms;
    if (!splitSubscripts(text, terms, error)) {
        return false;
    }

    // One ellipsis: two would not say how many dimensions each was covering.
    const QString ellipsis = QStringLiteral("...");
    qsizetype gap = -1;
    for (qsizetype i = 0; i < terms.size(); ++i) {
        if (terms.at(i).trimmed() != ellipsis) {
            continue;
        }
        if (gap >= 0) {
            error = QStringLiteral("only one '...' can stand for the dimensions "
                                   "left — two of them do not say how many "
                                   "each is covering");
            return false;
        }
        gap = i;
    }

    const qsizetype stated = (gap >= 0) ? terms.size() - 1 : terms.size();
    if (stated > rank) {
        // Too many subscripts is nearly always one scattered selection written
        // without its brackets -- "0,2,4" is three subscripts and "[0,2,4]" is
        // one -- so the line says which of the two it read.
        error = QStringLiteral("%1 for %2 dimension%3 — bracket a scattered "
                               "selection as [0,2,4]")
                    .arg(countedSubscripts(stated))
                    .arg(rank)
                    .arg(rank == 1 ? QString{} : QStringLiteral("s"));
        return false;
    }

    // Whatever was not written is the whole of its dimension, whether the gap
    // was marked with "..." or simply left off the end. Both are Python's.
    const QString whole = QStringLiteral(":");
    const qsizetype missing = rank - stated;
    expanded.clear();
    expanded.reserve(rank);
    for (qsizetype i = 0; i < terms.size(); ++i) {
        if (i == gap) {
            for (qsizetype fill = 0; fill < missing; ++fill) {
                expanded << whole;
            }
        } else {
            expanded << terms.at(i);
        }
    }
    if (gap < 0) {
        for (qsizetype fill = 0; fill < missing; ++fill) {
            expanded << whole;
        }
    }
    return true;
}

/// One subscript, counted rather than resolved.
///
/// A single term is pure arithmetic, and a single term is what nearly every
/// subscript is -- `:` above all, which is what every dimension of a freshly
/// selected dataset carries. Several terms are resolved after all, because
/// duplicates have to go and only the indices say which; that is affordable for
/// the same reason the deduplication itself is, a list being something written
/// by hand and therefore short.
bool countIndexExpression(const QString& text, hsize_t extent, SubscriptCount& out)
{
    if (text.trimmed().isEmpty()) {
        out.error = QStringLiteral("enter indices, e.g. 0,2,5:9");
        return false;
    }
    if (extent == 0) {
        out.error = QStringLiteral("this dimension is empty");
        return false;
    }

    QStringView body = QStringView(text).trimmed();
    bool bracketed = false;
    if (body.startsWith(u'[') && body.endsWith(u']')) {
        body = body.mid(1, body.size() - 2).trimmed();
        bracketed = true;
        if (body.isEmpty()) {
            out.error = QStringLiteral("enter indices, e.g. 0,2,5:9");
            return false;
        }
    }

    const QList<QStringView> terms = body.split(u',');
    if (terms.size() != 1) {
        const IndexExpression resolved = parseIndexExpression(text, extent);
        if (!resolved.valid()) {
            out.error = resolved.error;
            return false;
        }
        out.count = static_cast<hsize_t>(resolved.indices.size());
        out.drop = resolved.form == IndexExpression::Form::Single;
        return true;
    }

    const QStringView term = terms.front().trimmed();
    if (term.isEmpty()) {
        out.error = QStringLiteral("empty term between commas");
        return false;
    }
    Run run;
    if (!resolveTerm(term, extent, run, out.error)) {
        return false;
    }
    if (run.count == 0) {
        out.error = QStringLiteral("selects no indices");
        return false;
    }
    out.count = run.count;
    // Only a bare index drops its dimension, and only where it was written as
    // one: `[1]` is a list of a single element and keeps it, which is the same
    // distinction parseIndexExpression makes by taking the form away from a
    // bracketed subscript.
    out.drop = !bracketed && run.form == IndexExpression::Form::Single;
    return true;
}

/// "dim 2 has no subscript", as both readers have to say it.
bool missingSubscript(qsizetype dimension, QString& error)
{
    // parseIndexExpression would say "enter indices" here, which is advice for
    // a box the reader is typing into rather than a report about a line they
    // have already written.
    error = QStringLiteral("dim %1 has no subscript — write ':' for the whole "
                           "of it")
                .arg(dimension);
    return false;
}

} // namespace

bool countSubscripts(const QString& text, const std::vector<hsize_t>& shape,
                     std::vector<SubscriptCount>& chosen, QString& error)
{
    const auto rank = static_cast<qsizetype>(shape.size());
    switch (scalarSubscript(text, rank, error)) {
    case Scalar::Whole:
        chosen.clear();
        return true;
    case Scalar::Refused:
        return false;
    case Scalar::NotScalar:
        break;
    }

    QStringList expanded;
    if (!expandSubscripts(text, rank, expanded, error)) {
        return false;
    }

    chosen.clear();
    chosen.reserve(shape.size());
    for (qsizetype d = 0; d < rank; ++d) {
        const auto slot = static_cast<std::size_t>(d);
        const QString term = expanded.at(d).trimmed();
        if (term.isEmpty()) {
            return missingSubscript(d, error);
        }
        SubscriptCount counted;
        if (!countIndexExpression(term, shape[slot], counted)) {
            // Which dimension, first: the parser's own messages describe "this
            // dimension", and on a rank-4 line there are four of those.
            error = QStringLiteral("dim %1: %2").arg(d).arg(counted.error);
            return false;
        }
        chosen.push_back(std::move(counted));
    }
    return true;
}

bool readSubscripts(const QString& text, const std::vector<hsize_t>& shape,
                    std::vector<IndexExpression>& chosen, QStringList& written,
                    QString& error)
{
    const auto rank = static_cast<qsizetype>(shape.size());
    switch (scalarSubscript(text, rank, error)) {
    case Scalar::Whole:
        chosen.clear();
        written.clear();
        return true;
    case Scalar::Refused:
        return false;
    case Scalar::NotScalar:
        break;
    }

    QStringList expanded;
    if (!expandSubscripts(text, rank, expanded, error)) {
        return false;
    }

    chosen.clear();
    chosen.reserve(shape.size());
    written.clear();
    written.reserve(rank);
    for (qsizetype d = 0; d < rank; ++d) {
        const auto slot = static_cast<std::size_t>(d);
        const QString term = expanded.at(d).trimmed();
        if (term.isEmpty()) {
            return missingSubscript(d, error);
        }
        IndexExpression subscript = parseIndexExpression(term, shape[slot]);
        if (!subscript.valid()) {
            // Which dimension, first: the parser's own messages describe "this
            // dimension", and on a rank-4 line there are four of those.
            error = QStringLiteral("dim %1: %2").arg(d).arg(subscript.error);
            return false;
        }
        chosen.push_back(std::move(subscript));
        written << term;
    }
    return true;
}

QString bracketedIfListed(const QString& text)
{
    const QString trimmed = text.trimmed();
    if (!trimmed.contains(u',')) {
        return trimmed;
    }
    if (trimmed.startsWith(u'[') && trimmed.endsWith(u']')) {
        return trimmed;
    }
    return QStringLiteral("[%1]").arg(trimmed);
}

} // namespace postproc
