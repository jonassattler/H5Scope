// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "postproc/Subscripts.hpp"

#include <QLatin1String>
#include <QStringView>

#include <algorithm>
#include <cstddef>
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

/// One term of an expression, appended to `out`. `single` is set when the term
/// was a bare index and `span` when it was a step-1 slice, so the caller can
/// tell how the subscript was written.
bool parseTerm(QStringView term, hsize_t extent, std::vector<hsize_t>& out,
               IndexExpression::Form& form, QString& error)
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
        out.push_back(static_cast<hsize_t>(resolved));
        form = IndexExpression::Form::Single;
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

    const std::size_t before = out.size();
    if (step > 0) {
        for (qint64 i = from; i < to; i += step) {
            out.push_back(static_cast<hsize_t>(i));
        }
    } else {
        for (qint64 i = from; i > to; i += step) {
            out.push_back(static_cast<hsize_t>(i));
        }
    }

    const std::size_t taken = out.size() - before;
    // A step-1 slice is a run, and a run is something the data settings panel
    // can draw with its own two boxes. Anything else -- a stride, a descent --
    // is only expressible as the expression that produced it.
    if (step == 1 && taken > 0) {
        form = (taken == extent) ? IndexExpression::Form::Whole
                                 : IndexExpression::Form::Span;
    } else {
        form = IndexExpression::Form::Scattered;
    }
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

bool readSubscripts(const QString& text, const std::vector<hsize_t>& shape,
                    std::vector<IndexExpression>& chosen, QStringList& written,
                    QString& error)
{
    const auto rank = static_cast<qsizetype>(shape.size());
    if (rank == 0) {
        // A scalar has no dimensions to subscript, and Python says so: `a[0]`
        // and `a[:]` on a 0-d array are both "too many indices". `a[...]` is
        // not, because the dimensions the ellipsis stands for are the ones
        // nobody wrote, and on a scalar there are none of them -- so it is the
        // array itself, which is a thing a reader may well want a step to be.
        const QString line = text.trimmed();
        if (line.isEmpty() || line == QStringLiteral("...")) {
            chosen.clear();
            written.clear();
            return true;
        }
        error = QStringLiteral("a scalar has no dimensions to subscript — "
                               "'...' is the whole of it");
        return false;
    }

    QStringList terms;
    if (!splitSubscripts(text, terms, error)) {
        return false;
    }

    // `...` stands for however many dimensions nobody wrote a subscript for,
    // which is what it stands for in numpy. One of them: two would not say how
    // many each was covering.
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
    QStringList expanded;
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

    chosen.clear();
    chosen.reserve(shape.size());
    written.clear();
    written.reserve(rank);
    for (qsizetype d = 0; d < rank; ++d) {
        const auto slot = static_cast<std::size_t>(d);
        const QString term = expanded.at(d).trimmed();
        if (term.isEmpty()) {
            // parseIndexExpression would say "enter indices" here, which is
            // advice for a box the reader is typing into rather than a report
            // about a line they have already written.
            error = QStringLiteral("dim %1 has no subscript — write ':' "
                                   "for the whole of it")
                        .arg(d);
            return false;
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
