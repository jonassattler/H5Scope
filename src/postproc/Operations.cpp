// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "postproc/Operations.hpp"

#include "postproc/Subscripts.hpp"

#include <QLocale>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace postproc {
namespace {

/// The terms of a comma-separated argument, with the surrounding brackets or
/// parentheses taken off. `(2, 0, 1)`, `[2,0,1]` and `2 0 1` are all the same
/// list, because all three are what somebody types after reading numpy's
/// documentation of it.
QStringList terms(const QString& text)
{
    QString body = text.trimmed();
    if ((body.startsWith(u'(') && body.endsWith(u')'))
        || (body.startsWith(u'[') && body.endsWith(u']'))) {
        body = body.mid(1, body.size() - 2).trimmed();
    }
    if (body.isEmpty()) {
        return {};
    }
    QStringList out;
    for (const QString& term : body.split(u',', Qt::KeepEmptyParts)) {
        for (const QString& word : term.split(u' ', Qt::SkipEmptyParts)) {
            out << word.trimmed();
        }
        if (term.trimmed().isEmpty()) {
            out << QString{};
        }
    }
    return out;
}

/// A call's arguments, split the way Python splits them.
///
/// The six operations this panel started with each took one thing, so one
/// string was one argument and `terms()` above was the whole of the reading.
/// The ones after them take two -- `diff(n, axis)`, `clip(min, max)`,
/// `sum(axis, initial=...)` -- and a reader who has used numpy writes those
/// with keywords as often as without, so they are read as numpy reads them:
/// positional terms in order, then `name=value`, `None` for "not given".
///
/// A term is split off at a comma only outside brackets, because an axis tuple
/// is one argument: `sum((0, 1), initial=2)` is two terms, not three.
struct Arguments {
    /// In order. `std::nullopt` for a term written as `None` or left empty.
    std::vector<std::optional<QString>> positional;
    std::vector<std::pair<QString, std::optional<QString>>> named;

    [[nodiscard]] bool empty() const { return positional.empty() && named.empty(); }
};

bool splitArguments(const QString& text, Arguments& out, QString& error)
{
    out = {};
    const QString body = text.trimmed();
    if (body.isEmpty()) {
        return true;
    }

    QStringList pieces;
    int depth = 0;
    qsizetype start = 0;
    for (qsizetype at = 0; at < body.size(); ++at) {
        const QChar c = body.at(at);
        if (c == u'(' || c == u'[') {
            ++depth;
        } else if (c == u')' || c == u']') {
            if (--depth < 0) {
                error = QStringLiteral("this has a '%1' with nothing to close").arg(c);
                return false;
            }
        } else if (c == u',' && depth == 0) {
            pieces << body.mid(start, at - start);
            start = at + 1;
        }
    }
    if (depth != 0) {
        error = QStringLiteral("this has a bracket that is never closed");
        return false;
    }
    pieces << body.mid(start);
    // A trailing comma is allowed, as it is in Python.
    if (pieces.size() > 1 && pieces.back().trimmed().isEmpty()) {
        pieces.removeLast();
    }

    static const QRegularExpression kKeyword(
        QStringLiteral(R"(^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=(?!=)(.*)$)"),
        QRegularExpression::DotMatchesEverythingOption);
    const auto value = [](const QString& written) -> std::optional<QString> {
        const QString trimmed = written.trimmed();
        if (trimmed.isEmpty() || trimmed == QStringLiteral("None")) {
            return std::nullopt;
        }
        return trimmed;
    };

    for (const QString& piece : pieces) {
        const QRegularExpressionMatch keyword = kKeyword.match(piece);
        if (keyword.hasMatch()) {
            out.named.emplace_back(keyword.captured(1), value(keyword.captured(2)));
            continue;
        }
        if (!out.named.empty()) {
            error = QStringLiteral("'%1' comes after a keyword argument — write the "
                                   "positional ones first, as Python does")
                        .arg(piece.trimmed());
            return false;
        }
        out.positional.push_back(value(piece));
    }
    return true;
}

/// One parameter of an operation: the names it may be given by (the first is
/// the one numpy documents) and what was written for it.
struct Parameter {
    QStringList names;
    std::optional<QString> value;
    bool given = false;
};

/// Hand the arguments to the parameters, positionally and then by name, and
/// refuse what Python would refuse: too many, one it does not have, the same
/// one twice.
bool bindArguments(const Arguments& arguments, std::vector<Parameter>& parameters,
                   QString& error)
{
    if (arguments.positional.size() > parameters.size()) {
        error = parameters.empty()
                    ? QStringLiteral("this takes no arguments")
                    : QStringLiteral("this takes at most %1 argument%2, and %3 were "
                                     "given")
                          .arg(parameters.size())
                          .arg(parameters.size() == 1 ? QString{} : QStringLiteral("s"))
                          .arg(arguments.positional.size());
        return false;
    }
    for (std::size_t i = 0; i < arguments.positional.size(); ++i) {
        parameters[i].value = arguments.positional[i];
        parameters[i].given = true;
    }
    for (const auto& [name, value] : arguments.named) {
        auto match = std::find_if(parameters.begin(), parameters.end(),
                                  [&name](const Parameter& parameter) {
                                      return parameter.names.contains(name);
                                  });
        if (match == parameters.end()) {
            QStringList known;
            for (const Parameter& parameter : parameters) {
                known << parameter.names.front();
            }
            error = known.isEmpty()
                        ? QStringLiteral("this takes no arguments")
                        : QStringLiteral("'%1' is not an argument here — this takes "
                                         "%2")
                              .arg(name, known.join(QStringLiteral(", ")));
            return false;
        }
        if (match->given) {
            error = QStringLiteral("'%1' is given twice").arg(match->names.front());
            return false;
        }
        match->value = value;
        match->given = true;
    }
    return true;
}

/// A number, in the spellings Python prints: `2`, `-0.5`, `1e-3`, `inf`, `nan`.
bool readNumber(const QString& text, const QString& what, double& out, QString& error)
{
    const QString trimmed = text.trimmed();
    const QString lower = trimmed.toLower();
    if (lower == QStringLiteral("inf") || lower == QStringLiteral("+inf")) {
        out = std::numeric_limits<double>::infinity();
        return true;
    }
    if (lower == QStringLiteral("-inf")) {
        out = -std::numeric_limits<double>::infinity();
        return true;
    }
    if (lower == QStringLiteral("nan")) {
        out = std::numeric_limits<double>::quiet_NaN();
        return true;
    }
    bool converted = false;
    out = QLocale::c().toDouble(trimmed, &converted);
    if (!converted) {
        error = QStringLiteral("'%1' is not a number, and %2 has to be one")
                    .arg(trimmed, what);
        return false;
    }
    return true;
}

/// A whole number, for the arguments that count something.
bool readWhole(const QString& text, const QString& what, qint64& out, QString& error)
{
    bool converted = false;
    out = text.trimmed().toLongLong(&converted);
    if (!converted) {
        error = QStringLiteral("'%1' is not a whole number, and %2 has to be one")
                    .arg(text.trimmed(), what);
        return false;
    }
    return true;
}

/// Whether a number as written is a whole one. For preservesIntegers, which
/// asks of the text rather than of the data.
bool wholeText(const QString& text)
{
    double value = 0.0;
    QString ignored;
    return readNumber(text, QString{}, value, ignored) && std::isfinite(value)
           && value == std::trunc(value);
}

/// One axis number, resolved against a rank. Negative counts from the end, as
/// every axis argument in numpy does.
bool readAxis(const QString& text, std::size_t rank, std::size_t& out,
              QString& error)
{
    bool converted = false;
    const qint64 value = text.toLongLong(&converted);
    if (!converted) {
        error = QStringLiteral("'%1' is not an axis number").arg(text);
        return false;
    }
    const auto extent = static_cast<qint64>(rank);
    const qint64 resolved = value < 0 ? value + extent : value;
    if (resolved < 0 || resolved >= extent) {
        error = QStringLiteral("axis %1 is out of bounds for an array of "
                               "rank %2")
                    .arg(value)
                    .arg(rank);
        return false;
    }
    out = static_cast<std::size_t>(resolved);
    return true;
}

/// The axes an argument names, in the order it names them. An empty argument
/// names none, which each caller reads its own way.
bool readAxes(const QString& text, std::size_t rank,
              std::vector<std::size_t>& out, QString& error)
{
    out.clear();
    for (const QString& term : terms(text)) {
        if (term.isEmpty()) {
            error = QStringLiteral("empty axis between commas");
            return false;
        }
        std::size_t axis = 0;
        if (!readAxis(term, rank, axis, error)) {
            return false;
        }
        out.push_back(axis);
    }
    return true;
}

/// The axes a reduction removes: what was written, or every one of them when
/// nothing was. Rejects a repeat, as numpy does -- reducing twice over the same
/// axis is not a thing that can be meant.
bool readReductionAxes(const QString& text, std::size_t rank,
                       std::vector<std::size_t>& out, QString& error)
{
    if (text.trimmed().isEmpty()) {
        out.resize(rank);
        for (std::size_t d = 0; d < rank; ++d) {
            out[d] = d;
        }
        return true;
    }
    if (rank == 0) {
        // numpy accepts a *single* axis 0 or -1 on a 0-d array and reduces
        // nothing -- a back-compatibility allowance rather than a principle,
        // and one that does not extend to a list: `axis=0` is taken and
        // `axis=(0,0)` is out of bounds. It is numpy's answer either way, and
        // matching it is the whole contract here.
        const QStringList named = terms(text);
        if (named.size() == 1
            && (named.front() == QStringLiteral("0")
                || named.front() == QStringLiteral("-1"))) {
            out.clear();
            return true;
        }
        error = QStringLiteral("axis %1 is out of bounds for an array of rank 0")
                    .arg(named.join(QStringLiteral(", ")));
        return false;
    }
    if (!readAxes(text, rank, out, error)) {
        return false;
    }
    std::vector<std::size_t> seen = out;
    std::sort(seen.begin(), seen.end());
    if (std::adjacent_find(seen.begin(), seen.end()) != seen.end()) {
        error = QStringLiteral("the same axis is named twice");
        return false;
    }
    return true;
}

/// The permutation a transpose was given, or the reversal it means when it was
/// given nothing.
bool readPermutation(const QString& text, std::size_t rank,
                     std::vector<std::size_t>& out, QString& error)
{
    if (text.trimmed().isEmpty()) {
        out.resize(rank);
        for (std::size_t d = 0; d < rank; ++d) {
            out[d] = rank - 1 - d; // numpy reverses the axes by default
        }
        return true;
    }
    if (!readAxes(text, rank, out, error)) {
        return false;
    }
    if (out.size() != rank) {
        error = QStringLiteral("%1 axes for an array of rank %2 — a transpose "
                               "names every axis or none of them")
                    .arg(out.size())
                    .arg(rank);
        return false;
    }
    std::vector<std::size_t> seen = out;
    std::sort(seen.begin(), seen.end());
    if (std::adjacent_find(seen.begin(), seen.end()) != seen.end()) {
        error = QStringLiteral("the same axis is named twice");
        return false;
    }
    return true;
}

/// The shape a reshape was given, with a single -1 worked out from the size.
bool readShape(const QString& text, hsize_t size, std::vector<hsize_t>& out,
               QString& error)
{
    const QStringList written = terms(text);
    if (written.isEmpty()) {
        error = QStringLiteral("enter a shape, e.g. 2, 3 — or -1 for a "
                               "dimension to work out");
        return false;
    }

    out.clear();
    out.reserve(static_cast<std::size_t>(written.size()));
    std::optional<std::size_t> unknown;
    hsize_t known = 1;
    for (const QString& term : written) {
        bool converted = false;
        const qint64 value = term.toLongLong(&converted);
        if (!converted) {
            error = QStringLiteral("'%1' is not an extent").arg(term);
            return false;
        }
        if (value < -1) {
            error = QStringLiteral("%1 is not an extent — only -1 stands for "
                                   "a dimension to work out")
                        .arg(value);
            return false;
        }
        if (value == -1) {
            if (unknown.has_value()) {
                error = QStringLiteral("only one dimension can be left as -1 — "
                                       "two of them do not say how to divide "
                                       "the elements between them");
                return false;
            }
            unknown = out.size();
            out.push_back(0);
            continue;
        }
        out.push_back(static_cast<hsize_t>(value));
        known = elementCount({known, static_cast<hsize_t>(value)});
    }

    if (unknown.has_value()) {
        if (known == 0 || size % known != 0) {
            // Either the dimensions that were written multiply to nothing, so
            // there is nothing to divide by, or they do not divide the
            // elements evenly and no whole extent would finish the shape.
            error = QStringLiteral("cannot work out the -1: %1 element%2 do "
                                   "not divide evenly by the %3 the rest of "
                                   "the shape asks for")
                        .arg(size)
                        .arg(size == 1 ? QString{} : QStringLiteral("s"))
                        .arg(known);
            return false;
        }
        out[*unknown] = size / known;
        return true;
    }

    if (elementCount(out) != size) {
        error = QStringLiteral("%1 element%2 cannot be reshaped into %3, which "
                               "holds %4")
                    .arg(size)
                    .arg(size == 1 ? QString{} : QStringLiteral("s"))
                    .arg(describeShape(out))
                    .arg(elementCount(out));
        return false;
    }
    return true;
}

/// The shape a slice leaves, and the selection that produces it. `drop[d]`
/// marks a dimension written as a bare index, which Python removes.
bool readSlice(const QString& text, const std::vector<hsize_t>& shape,
               std::vector<std::vector<hsize_t>>& indices,
               std::vector<bool>& drop, QString& error)
{
    std::vector<IndexExpression> chosen;
    QStringList written;
    if (!readSubscripts(text, shape, chosen, written, error)) {
        return false;
    }
    indices.clear();
    drop.clear();
    indices.reserve(chosen.size());
    drop.reserve(chosen.size());
    for (const IndexExpression& subscript : chosen) {
        indices.push_back(subscript.indices);
        // The one distinction the whole grammar is built to keep: `1` and `1:2`
        // select the same element, and only the first of them drops the
        // dimension. Form records how it was written, which is why it is there.
        drop.push_back(subscript.form == IndexExpression::Form::Single);
    }
    return true;
}

/// The shape left after `axes` are taken out of `shape`.
std::vector<hsize_t> withoutAxes(const std::vector<hsize_t>& shape,
                                 const std::vector<std::size_t>& axes)
{
    std::vector<hsize_t> out;
    out.reserve(shape.size());
    for (std::size_t d = 0; d < shape.size(); ++d) {
        if (std::find(axes.begin(), axes.end(), d) == axes.end()) {
            out.push_back(shape[d]);
        }
    }
    return out;
}

/// Whether a reduction over `axes` has anything to reduce.
///
/// numpy refuses `min` over an empty axis and it is right to: the minimum of no
/// numbers is not a number, and an identity would be an infinity nobody asked
/// for. An *output* with no cells is fine -- reducing (0, 3) along axis 1 gives
/// an empty result and never has to fold anything -- so the question is only
/// ever about the axes being folded.
///
/// A sum and a product are asked nothing: they have an identity, and numpy
/// answers an empty sum with it.
bool reducible(const std::vector<hsize_t>& shape,
               const std::vector<std::size_t>& axes)
{
    for (const std::size_t axis : axes) {
        if (shape[axis] == 0) {
            return false;
        }
    }
    return !axes.empty() || elementCount(shape) != 0;
}

QString reductionRefusal(bool minimum)
{
    return QStringLiteral("nothing to reduce: %1 over an axis of no elements "
                          "has no answer")
        .arg(minimum ? QStringLiteral("a minimum") : QStringLiteral("a maximum"));
}

/// The four ways a set of axes can be folded away.
enum class Fold { Min, Max, Sum, Prod };

/// What a fold was asked for: which axes, and what the fold starts from.
struct FoldPlan {
    std::vector<std::size_t> axes;
    double initial = 0.0;
};

/// Read a fold's arguments.
///
/// `min` and `max` take the axes and nothing else, and read exactly as they
/// always have -- `0, 1` is a tuple of axes -- unless a keyword is written, in
/// which case `axis=` is the one there is. `sum` and `prod` read their axes the
/// same way, so `max(0, 1)` and `sum(0, 1)` fold the same axes, and take
/// `initial=` as a keyword only. That is numpy's own arrangement rather than a
/// choice made here: the second positional parameter of `np.sum` is `dtype`,
/// so a positional `initial` is not a thing numpy has either.
bool planFold(const Step& step, const std::vector<hsize_t>& shape, FoldPlan& plan,
              QString& error)
{
    const std::size_t rank = shape.size();
    const bool additive = step.kind == OperationKind::Sum;
    const bool identity = additive || step.kind == OperationKind::Prod;
    plan = {};
    plan.initial = step.kind == OperationKind::Min ? std::numeric_limits<double>::infinity()
                   : step.kind == OperationKind::Max
                       ? -std::numeric_limits<double>::infinity()
                   : additive ? 0.0
                              : 1.0;

    Arguments arguments;
    if (!splitArguments(step.argument, arguments, error)) {
        return false;
    }

    QString axesText;
    if (!identity && arguments.named.empty()) {
        // The spelling these two have always read, untouched.
        axesText = step.argument;
    } else {
        // Every positional term is an axis, so they are gathered into one
        // parameter before the keywords are matched against it.
        std::vector<Parameter> parameters{{{QStringLiteral("axis")}, {}, false}};
        if (identity) {
            parameters.push_back({{QStringLiteral("initial")}, {}, false});
        }
        Arguments gathered;
        gathered.named = arguments.named;
        if (!arguments.positional.empty()) {
            QStringList axes;
            for (const std::optional<QString>& term : arguments.positional) {
                if (!term.has_value()) {
                    if (arguments.positional.size() == 1) {
                        continue; // `sum(None)`: every axis
                    }
                    error = QStringLiteral("None cannot be one of several axes");
                    return false;
                }
                if (arguments.positional.size() > 1 && term->startsWith(u'(')) {
                    error = QStringLiteral("write several axes as one tuple, (0, 1), "
                                           "or as a list, 0, 1 — not both");
                    return false;
                }
                axes << *term;
            }
            gathered.positional.push_back(
                axes.isEmpty() ? std::nullopt
                               : std::optional<QString>(axes.join(QStringLiteral(", "))));
        }
        if (!bindArguments(gathered, parameters, error)) {
            return false;
        }
        axesText = parameters[0].value.value_or(QString{});
        if (identity && parameters[1].value.has_value()
            && !readNumber(*parameters[1].value, QStringLiteral("initial"), plan.initial,
                           error)) {
            return false;
        }
    }

    if (!readReductionAxes(axesText, rank, plan.axes, error)) {
        return false;
    }
    if (!identity && !reducible(shape, plan.axes)) {
        error = reductionRefusal(step.kind == OperationKind::Min);
        return false;
    }
    return true;
}

/// numpy's pairwise summation, ported rather than approximated.
///
/// A sum is the one fold whose answer depends on the order it is done in, and
/// numpy does not add left to right: along a contiguous run it keeps eight
/// partial sums and splits anything longer than 128 in two, which is why its
/// error grows with the logarithm of the length rather than with the length.
/// A plain loop is a better sum than some and a worse one than others, and in
/// every case a *different* one -- the last bit of a mean over a row would
/// disagree with h5py's, and the golden suite asserts bit for bit. So this is
/// `pairwise_sum` from numpy's loops_utils.h.src, including the -0.0 it starts
/// a short run from, which is what keeps a sum of negative zeros negative.
double pairwiseSum(const double* values, std::size_t count)
{
    if (count < 8) {
        double sum = -0.0;
        for (std::size_t i = 0; i < count; ++i) {
            sum += values[i];
        }
        return sum;
    }
    if (count <= 128) {
        std::array<double, 8> partial{};
        for (std::size_t j = 0; j < 8; ++j) {
            partial[j] = values[j];
        }
        std::size_t i = 8;
        for (; i < count - (count % 8); i += 8) {
            for (std::size_t j = 0; j < 8; ++j) {
                partial[j] += values[i + j];
            }
        }
        double sum = ((partial[0] + partial[1]) + (partial[2] + partial[3]))
                     + ((partial[4] + partial[5]) + (partial[6] + partial[7]));
        for (; i < count; ++i) {
            sum += values[i];
        }
        return sum;
    }
    std::size_t half = count / 2;
    half -= half % 8;
    return pairwiseSum(values, half) + pairwiseSum(values + half, count - half);
}

/// Fold `input` along `axes`.
///
/// For a minimum and a maximum NaN wins, whichever way the comparison runs,
/// because that is what numpy's `min` and `max` do: a NaN anywhere in what is
/// being folded comes out of it. This application already writes NaN into a
/// cell it could not read, so an unreadable element poisons the reduction over
/// it -- which is the honest answer and the same one h5py would give. A sum and
/// a product need no such rule; arithmetic already carries a NaN through.
///
/// The walk goes a run at a time rather than an element at a time, where a run
/// is the axes folded away at the *end* of the shape: those elements are
/// contiguous and all land in one cell, which is exactly where numpy's inner
/// loop is a pairwise sum rather than one addition per element. Where the last
/// axis survives, the run is one element and every fold is the plain loop it
/// always was.
Array reduce(const Array& input, const std::vector<std::size_t>& axes, Fold fold,
             double initial)
{
    const std::vector<hsize_t>& shape = input.shape();
    const std::vector<hsize_t> outShape = withoutAxes(shape, axes);
    const hsize_t outSize = elementCount(outShape);

    std::vector<double> out(static_cast<std::size_t>(outSize), initial);
    if (outSize == 0 || elementCount(shape) == 0) {
        return Array(outShape, std::move(out));
    }

    const auto folded = [&axes](std::size_t d) {
        return std::find(axes.begin(), axes.end(), d) != axes.end();
    };
    std::size_t lead = shape.size();
    std::size_t run = 1;
    while (lead > 0 && folded(lead - 1)) {
        run *= static_cast<std::size_t>(shape[lead - 1]);
        --lead;
    }

    // How far the output position moves when each leading input dimension
    // advances: zero for an axis being folded away, and the output's own
    // row-major stride for one that survives. Carried alongside the walk so
    // neither the input nor the output index has to be unravelled per run.
    std::vector<std::ptrdiff_t> outStride(lead, 0);
    std::ptrdiff_t running = 1;
    for (std::size_t d = lead; d-- > 0;) {
        if (folded(d)) {
            continue;
        }
        outStride[d] = running;
        running *= static_cast<std::ptrdiff_t>(shape[d]);
    }

    const std::vector<double> values = input.values();
    std::vector<hsize_t> index(lead, 0);
    std::ptrdiff_t position = 0;
    for (std::size_t at = 0; at < values.size(); at += run) {
        const double* from = values.data() + at;
        double& target = out[static_cast<std::size_t>(position)];
        switch (fold) {
        case Fold::Sum:
            target += pairwiseSum(from, run);
            break;
        case Fold::Prod:
            for (std::size_t i = 0; i < run; ++i) {
                target *= from[i];
            }
            break;
        case Fold::Min:
        case Fold::Max:
            for (std::size_t i = 0; i < run; ++i) {
                const double value = from[i];
                if (std::isnan(value)) {
                    target = value;
                } else if (!std::isnan(target)) {
                    target = fold == Fold::Min ? std::min(target, value)
                                               : std::max(target, value);
                }
            }
            break;
        }
        for (std::size_t d = lead; d-- > 0;) {
            position += outStride[d];
            if (++index[d] < shape[d]) {
                break;
            }
            position -= outStride[d] * static_cast<std::ptrdiff_t>(shape[d]);
            index[d] = 0;
        }
    }
    return Array(outShape, std::move(out));
}

/// The axis an accumulation runs along, or nothing for numpy's default, which
/// is the array flattened. A 0-d array is read as the one-element line it
/// flattens to, which is also numpy's answer: `cumsum(x, axis=0)` of a scalar
/// is `[x]`.
bool planScan(const Step& step, const std::vector<hsize_t>& shape,
              std::optional<std::size_t>& axis, QString& error)
{
    Arguments arguments;
    if (!splitArguments(step.argument, arguments, error)) {
        return false;
    }
    std::vector<Parameter> parameters{{{QStringLiteral("axis")}, {}, false}};
    if (!bindArguments(arguments, parameters, error)) {
        return false;
    }
    axis.reset();
    if (!parameters[0].value.has_value()) {
        return true;
    }
    std::size_t resolved = 0;
    if (!readAxis(*parameters[0].value, std::max<std::size_t>(shape.size(), 1), resolved,
                  error)) {
        return false;
    }
    axis = resolved;
    return true;
}

std::vector<hsize_t> scannedShape(const std::vector<hsize_t>& shape,
                                  const std::optional<std::size_t>& axis)
{
    if (!axis.has_value() || shape.empty()) {
        return {elementCount(shape)};
    }
    return shape;
}

/// A running sum or product along one axis, as numpy's `accumulate`: the first
/// element as it is, and each after it folded onto the one before. Sequential,
/// not pairwise -- a running total has to pass through every partial sum, and
/// numpy's does.
Array scan(const Array& input, const std::optional<std::size_t>& axis, bool product)
{
    const std::vector<hsize_t> shape = scannedShape(input.shape(), axis);
    std::vector<double> values = input.values();
    const std::size_t along = axis.has_value() && !input.shape().empty() ? *axis : 0;

    const auto extent = static_cast<std::size_t>(shape[along]);
    std::size_t inner = 1;
    for (std::size_t d = along + 1; d < shape.size(); ++d) {
        inner *= static_cast<std::size_t>(shape[d]);
    }
    const std::size_t block = extent * inner;
    if (block == 0) {
        return Array(shape, std::move(values));
    }
    for (std::size_t base = 0; base < values.size(); base += block) {
        for (std::size_t i = 0; i < inner; ++i) {
            double running = values[base + i];
            for (std::size_t k = 1; k < extent; ++k) {
                double& value = values[base + k * inner + i];
                running = product ? running * value : running + value;
                value = running;
            }
        }
    }
    return Array(shape, std::move(values));
}

/// What a difference was asked for: how many times, and along which axis.
struct DiffPlan {
    qint64 n = 1;
    std::size_t axis = 0;
};

/// numpy's `diff(a, n=1, axis=-1)`, refused where numpy refuses it and in
/// numpy's order -- which includes answering `n=0` with the array as it is
/// before anything else is asked, a scalar and a nonsense axis included.
bool planDiff(const Step& step, const std::vector<hsize_t>& shape, DiffPlan& plan,
              QString& error)
{
    Arguments arguments;
    if (!splitArguments(step.argument, arguments, error)) {
        return false;
    }
    std::vector<Parameter> parameters{{{QStringLiteral("n")}, {}, false},
                                      {{QStringLiteral("axis")}, {}, false}};
    if (!bindArguments(arguments, parameters, error)) {
        return false;
    }
    plan = {};
    if (parameters[0].value.has_value()
        && !readWhole(*parameters[0].value, QStringLiteral("n"), plan.n, error)) {
        return false;
    }
    if (plan.n == 0) {
        return true;
    }
    if (plan.n < 0) {
        error = QStringLiteral("order must be non-negative but got %1").arg(plan.n);
        return false;
    }
    if (shape.empty()) {
        error = QStringLiteral("a difference needs at least one dimension to run "
                               "along, and this is a single value");
        return false;
    }
    return readAxis(parameters[1].value.value_or(QStringLiteral("-1")), shape.size(),
                    plan.axis, error);
}

std::vector<hsize_t> differencedShape(std::vector<hsize_t> shape, const DiffPlan& plan)
{
    if (plan.n == 0) {
        return shape;
    }
    const auto n = static_cast<hsize_t>(plan.n);
    shape[plan.axis] = shape[plan.axis] > n ? shape[plan.axis] - n : 0;
    return shape;
}

/// `a[1:] - a[:-1]` along the axis, `n` times over. Done as the passes numpy
/// does rather than as one binomial sum, because the passes are what decide
/// how it rounds.
Array difference(const Array& input, const DiffPlan& plan)
{
    std::vector<hsize_t> shape = input.shape();
    std::vector<double> values = input.values();
    if (plan.n == 0) {
        return Array(shape, std::move(values));
    }
    std::size_t outer = 1;
    for (std::size_t d = 0; d < plan.axis; ++d) {
        outer *= static_cast<std::size_t>(shape[d]);
    }
    std::size_t inner = 1;
    for (std::size_t d = plan.axis + 1; d < shape.size(); ++d) {
        inner *= static_cast<std::size_t>(shape[d]);
    }
    for (qint64 pass = 0; pass < plan.n && shape[plan.axis] > 0; ++pass) {
        const auto extent = static_cast<std::size_t>(shape[plan.axis]);
        std::vector<double> next(outer * (extent - 1) * inner);
        for (std::size_t o = 0; o < outer; ++o) {
            for (std::size_t k = 0; k + 1 < extent; ++k) {
                for (std::size_t i = 0; i < inner; ++i) {
                    next[(o * (extent - 1) + k) * inner + i] =
                        values[(o * extent + k + 1) * inner + i]
                        - values[(o * extent + k) * inner + i];
                }
            }
        }
        values = std::move(next);
        shape[plan.axis] = extent - 1;
    }
    return Array(differencedShape(input.shape(), plan), std::move(values));
}

/// Read the parameters of an operation that takes numbers, each optional
/// unless `required` says otherwise, into `out` (left as it is where nothing
/// was written).
bool readNumbers(const QString& text, std::vector<Parameter> parameters,
                 const std::vector<bool>& required, std::vector<std::optional<double>>& out,
                 QString& error)
{
    Arguments arguments;
    if (!splitArguments(text, arguments, error)
        || !bindArguments(arguments, parameters, error)) {
        return false;
    }
    out.assign(parameters.size(), std::nullopt);
    for (std::size_t i = 0; i < parameters.size(); ++i) {
        if (!parameters[i].value.has_value()) {
            if (i < required.size() && required[i]) {
                error = QStringLiteral("enter a number for %1")
                            .arg(parameters[i].names.front());
                return false;
            }
            continue;
        }
        double value = 0.0;
        if (!readNumber(*parameters[i].value, parameters[i].names.front(), value, error)) {
            return false;
        }
        out[i] = value;
    }
    return true;
}

/// The parameters each element-wise operation takes, by numpy's names where
/// numpy has the operation and by the obvious ones where it does not.
std::vector<Parameter> scalarParameters(OperationKind kind)
{
    switch (kind) {
    case OperationKind::Clip:
        // numpy 2.1 added `min=` and `max=` beside the older `a_min=`/`a_max=`.
        return {{{QStringLiteral("min"), QStringLiteral("a_min")}, {}, false},
                {{QStringLiteral("max"), QStringLiteral("a_max")}, {}, false}};
    case OperationKind::Pow:
        return {{{QStringLiteral("exponent"), QStringLiteral("x2")}, {}, false}};
    case OperationKind::Add:
        return {{{QStringLiteral("value"), QStringLiteral("x2")}, {}, false}};
    case OperationKind::Multiply:
        return {{{QStringLiteral("factor"), QStringLiteral("x2")}, {}, false}};
    case OperationKind::Normalize:
        return {{{QStringLiteral("min")}, {}, false}, {{QStringLiteral("max")}, {}, false}};
    default:
        return {};
    }
}

std::vector<bool> scalarRequired(OperationKind kind)
{
    switch (kind) {
    case OperationKind::Pow:
    case OperationKind::Add:
    case OperationKind::Multiply:
        return {true};
    default:
        return {};
    }
}

bool planScalars(const Step& step, std::vector<std::optional<double>>& values,
                 QString& error)
{
    return readNumbers(step.argument, scalarParameters(step.kind),
                       scalarRequired(step.kind), values, error);
}

/// Refuse an argument to an operation that takes none, rather than ignoring
/// it: `sqrt(2)` looks like a request for something, and quietly taking the
/// square root instead is an answer to a question nobody asked.
bool noArguments(const Step& step, QString& error)
{
    Arguments arguments;
    if (!splitArguments(step.argument, arguments, error)) {
        return false;
    }
    std::vector<Parameter> none;
    return bindArguments(arguments, none, error);
}

/// Every element through `f`, in the array's shape.
template <typename F>
Array mapped(const Array& input, F f)
{
    std::vector<double> values = input.values();
    for (double& value : values) {
        value = f(value);
    }
    return Array(input.shape(), std::move(values));
}

/// numpy's clip, which is three functions depending on which bounds are given,
/// and differs between them exactly where a bound and an element tie with
/// opposite signs of zero. Found by asking numpy about every pairing of -0.0,
/// 0.0, NaN, None and an ordinary number rather than by reading its source:
///
/// - Only a floor is `np.maximum(x, min)`: a tie takes the floor, and a NaN on
///   either side is NaN.
/// - Only a ceiling is `np.minimum(x, max)`: a tie takes the ceiling, and a NaN
///   on either side is NaN.
/// - Both are the clip ufunc itself: a tie keeps the element, and a NaN bound
///   makes everything NaN. A floor above the ceiling clips everything to the
///   ceiling, which falls out of doing the floor first.
///
/// A NaN element stays NaN in all three.
double clipped(double value, const std::optional<double>& low,
               const std::optional<double>& high)
{
    constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
    if (std::isnan(value)) {
        return value;
    }
    if (low.has_value() && high.has_value()) {
        if (std::isnan(*low) || std::isnan(*high)) {
            return kNaN;
        }
        value = value < *low ? *low : value;
        return value > *high ? *high : value;
    }
    if (low.has_value()) {
        return std::isnan(*low) ? kNaN : (value > *low ? value : *low);
    }
    if (high.has_value()) {
        return std::isnan(*high) ? kNaN : (value < *high ? value : *high);
    }
    return value;
}

/// The linear map that puts the smallest finite element at `low` and the
/// largest at `high`.
///
/// Not numpy's, because numpy has none; the edges are taken from where a
/// reader would otherwise have got them. The extent is over the *finite*
/// elements, as `nanmin`/`nanmax` would have it with the infinities left out
/// too -- one infinity would otherwise make every other element the same
/// point. NaN and the infinities are then left as they are rather than mapped,
/// because there is no place in `[low, high]` that is a true reading of them.
/// An array whose finite elements are all one value maps to `low`, which is
/// scikit-learn's MinMaxScaler's answer to the same division by zero.
Array normalized(const Array& input, double low, double high)
{
    std::vector<double> values = input.values();
    double smallest = std::numeric_limits<double>::infinity();
    double largest = -std::numeric_limits<double>::infinity();
    for (const double value : values) {
        if (std::isfinite(value)) {
            smallest = std::min(smallest, value);
            largest = std::max(largest, value);
        }
    }
    if (smallest > largest) {
        return Array(input.shape(), std::move(values)); // nothing finite
    }
    // Interpolated as (1 - t)·low + t·high rather than low + t·(high - low):
    // the second misses `high` by a rounding whenever the two do not subtract
    // exactly -- 0.1 and 0.3 give 0.30000000000000004 -- and a reader who asked
    // for the largest element at 0.3 should find it there.
    const double span = largest - smallest;
    for (double& value : values) {
        if (!std::isfinite(value)) {
            continue;
        }
        const double t = span > 0.0 ? (value - smallest) / span : 0.0;
        value = (1.0 - t) * low + t * high;
    }
    return Array(input.shape(), std::move(values));
}

} // namespace

QString describeShape(const std::vector<hsize_t>& shape)
{
    if (shape.empty()) {
        return QStringLiteral("scalar");
    }
    QStringList parts;
    parts.reserve(static_cast<qsizetype>(shape.size()));
    for (const hsize_t extent : shape) {
        parts << QString::number(extent);
    }
    return parts.join(QStringLiteral(" × "));
}

const std::vector<OperationInfo>& operations()
{
    // Slice first because it is the one the reader already knows from the bar,
    // then the two that rearrange, then the folds, then the running folds and
    // the difference -- which keep an axis but change what is along it -- and
    // last the ones that touch each element on its own, numpy's before the
    // three that are not.
    //
    // Lower case throughout, and it is the name rather than a presentation of
    // it: these are numpy's own words, they are set beside "slice" and
    // "output" -- which the panel writes itself and has always written this
    // way -- and a chain that read "slice / Max / Reshape" was capitalising
    // four of its seven rows and no others. operationNamed() ignores case, so a
    // pipeline remembered when they were capitalised still reads back.
    //
    // The argument's name is numpy's parameter where there is one parameter,
    // and the list of them where there are two, so the column beside the box
    // says what `diff(1, 0)` means without a trip to the documentation. The
    // placeholders are what an empty box means, which is numpy's default.
    static const std::vector<OperationInfo> kOperations = {
        {OperationKind::Slice, QStringLiteral("slice"), QStringLiteral("subscripts"),
         QStringLiteral(":, 0, ::2")},
        {OperationKind::Transpose, QStringLiteral("transpose"),
         QStringLiteral("axes"), QStringLiteral("reversed")},
        {OperationKind::Reshape, QStringLiteral("reshape"), QStringLiteral("shape"),
         QStringLiteral("2, -1")},
        {OperationKind::Min, QStringLiteral("min"), QStringLiteral("axis"),
         QStringLiteral("all")},
        {OperationKind::Max, QStringLiteral("max"), QStringLiteral("axis"),
         QStringLiteral("all")},
        {OperationKind::Sum, QStringLiteral("sum"), QStringLiteral("axis"),
         QStringLiteral("all, initial=0")},
        {OperationKind::Prod, QStringLiteral("prod"), QStringLiteral("axis"),
         QStringLiteral("all, initial=1")},
        {OperationKind::CumSum, QStringLiteral("cumsum"), QStringLiteral("axis"),
         QStringLiteral("flattened")},
        {OperationKind::CumProd, QStringLiteral("cumprod"), QStringLiteral("axis"),
         QStringLiteral("flattened")},
        {OperationKind::Diff, QStringLiteral("diff"), QStringLiteral("n, axis"),
         QStringLiteral("1, -1")},
        {OperationKind::Abs, QStringLiteral("abs"), QString{}, QString{}},
        {OperationKind::Sqrt, QStringLiteral("sqrt"), QString{}, QString{}},
        {OperationKind::Pow, QStringLiteral("pow"), QStringLiteral("exponent"),
         QStringLiteral("2")},
        {OperationKind::Clip, QStringLiteral("clip"), QStringLiteral("min, max"),
         QStringLiteral("0, None")},
        {OperationKind::Add, QStringLiteral("add"), QStringLiteral("value"),
         QStringLiteral("1.5")},
        {OperationKind::Multiply, QStringLiteral("multiply"), QStringLiteral("factor"),
         QStringLiteral("2")},
        {OperationKind::Normalize, QStringLiteral("normalize"), QStringLiteral("min, max"),
         QStringLiteral("0, 1")},
    };
    return kOperations;
}

const OperationInfo& operationInfo(OperationKind kind)
{
    for (const OperationInfo& info : operations()) {
        if (info.kind == kind) {
            return info;
        }
    }
    return operations().front();
}

std::optional<OperationKind> operationNamed(const QString& name)
{
    for (const OperationInfo& info : operations()) {
        if (info.name.compare(name, Qt::CaseInsensitive) == 0) {
            return info.kind;
        }
    }
    return std::nullopt;
}

ShapeResult shapeAfter(const Step& step, const std::vector<hsize_t>& shape)
{
    ShapeResult result;
    const std::size_t rank = shape.size();

    switch (step.kind) {
    case OperationKind::Slice: {
        // Counted, not resolved. A shape needs how many each subscript names
        // and nothing else, and `:` on a dimension of ten million resolves to
        // ten million indices -- eighty megabytes built, measured and freed.
        // This is asked of every step on every keystroke and on every change of
        // selection, which is what made picking a large dataset slow: a quarter
        // of a second spent describing a table before anything was read.
        std::vector<SubscriptCount> counted;
        if (!countSubscripts(step.argument, shape, counted, result.error)) {
            return result;
        }
        for (const SubscriptCount& subscript : counted) {
            if (!subscript.drop) {
                result.shape.push_back(subscript.count);
            }
        }
        return result;
    }
    case OperationKind::Transpose: {
        std::vector<std::size_t> axes;
        if (!readPermutation(step.argument, rank, axes, result.error)) {
            return result;
        }
        result.shape.reserve(rank);
        for (const std::size_t axis : axes) {
            result.shape.push_back(shape[axis]);
        }
        return result;
    }
    case OperationKind::Min:
    case OperationKind::Max:
    case OperationKind::Sum:
    case OperationKind::Prod: {
        FoldPlan plan;
        if (!planFold(step, shape, plan, result.error)) {
            return result;
        }
        result.shape = withoutAxes(shape, plan.axes);
        return result;
    }
    case OperationKind::CumSum:
    case OperationKind::CumProd: {
        std::optional<std::size_t> axis;
        if (!planScan(step, shape, axis, result.error)) {
            return result;
        }
        result.shape = scannedShape(shape, axis);
        return result;
    }
    case OperationKind::Diff: {
        DiffPlan plan;
        if (!planDiff(step, shape, plan, result.error)) {
            return result;
        }
        result.shape = differencedShape(shape, plan);
        return result;
    }
    case OperationKind::Abs:
    case OperationKind::Sqrt:
        if (!noArguments(step, result.error)) {
            return result;
        }
        result.shape = shape;
        return result;
    case OperationKind::Clip:
    case OperationKind::Pow:
    case OperationKind::Add:
    case OperationKind::Multiply:
    case OperationKind::Normalize: {
        std::vector<std::optional<double>> values;
        if (!planScalars(step, values, result.error)) {
            return result;
        }
        result.shape = shape;
        return result;
    }
    case OperationKind::Reshape:
        static_cast<void>(
            readShape(step.argument, elementCount(shape), result.shape, result.error));
        if (!result.error.isEmpty()) {
            result.shape.clear();
        }
        return result;
    }
    return result;
}

ArrayResult apply(const Step& step, const Array& input)
{
    ArrayResult result;
    const std::vector<hsize_t>& shape = input.shape();
    const std::size_t rank = shape.size();

    switch (step.kind) {
    case OperationKind::Slice: {
        std::vector<std::vector<hsize_t>> indices;
        std::vector<bool> drop;
        if (!readSlice(step.argument, shape, indices, drop, result.error)) {
            return result;
        }
        result.array = input.selected(indices, drop);
        return result;
    }
    case OperationKind::Transpose: {
        std::vector<std::size_t> axes;
        if (!readPermutation(step.argument, rank, axes, result.error)) {
            return result;
        }
        result.array = input.transposed(axes);
        return result;
    }
    case OperationKind::Min:
    case OperationKind::Max:
    case OperationKind::Sum:
    case OperationKind::Prod: {
        FoldPlan plan;
        if (!planFold(step, shape, plan, result.error)) {
            return result;
        }
        const Fold fold = step.kind == OperationKind::Min   ? Fold::Min
                          : step.kind == OperationKind::Max ? Fold::Max
                          : step.kind == OperationKind::Sum ? Fold::Sum
                                                            : Fold::Prod;
        result.array = reduce(input, plan.axes, fold, plan.initial);
        return result;
    }
    case OperationKind::CumSum:
    case OperationKind::CumProd: {
        std::optional<std::size_t> axis;
        if (!planScan(step, shape, axis, result.error)) {
            return result;
        }
        result.array = scan(input, axis, step.kind == OperationKind::CumProd);
        return result;
    }
    case OperationKind::Diff: {
        DiffPlan plan;
        if (!planDiff(step, shape, plan, result.error)) {
            return result;
        }
        result.array = difference(input, plan);
        return result;
    }
    case OperationKind::Abs:
    case OperationKind::Sqrt: {
        if (!noArguments(step, result.error)) {
            return result;
        }
        if (step.kind == OperationKind::Abs) {
            result.array = mapped(input, [](double value) { return std::abs(value); });
        } else {
            // A negative number has no real square root, and numpy answers
            // NaN for it rather than refusing -- as std::sqrt does.
            result.array = mapped(input, [](double value) { return std::sqrt(value); });
        }
        return result;
    }
    case OperationKind::Clip:
    case OperationKind::Pow:
    case OperationKind::Add:
    case OperationKind::Multiply:
    case OperationKind::Normalize: {
        std::vector<std::optional<double>> values;
        if (!planScalars(step, values, result.error)) {
            return result;
        }
        switch (step.kind) {
        case OperationKind::Clip:
            result.array = mapped(input, [low = values[0], high = values[1]](double value) {
                return clipped(value, low, high);
            });
            break;
        case OperationKind::Pow:
            // numpy's float power is C's pow, so its NaN for a negative base
            // under a fractional exponent, and its 1 for anything to the
            // zeroth, NaN included, are this one's too -- with one exception
            // numpy makes and this copies: a power of one half is a square
            // root. The two differ at the edges, where pow says √-∞ is +∞ and
            // √-0 is +0, and sqrt says NaN and -0.
            if (*values[0] == 0.5) {
                result.array = mapped(input, [](double value) { return std::sqrt(value); });
                break;
            }
            result.array = mapped(input, [exponent = *values[0]](double value) {
                return std::pow(value, exponent);
            });
            break;
        case OperationKind::Add:
            result.array = mapped(input, [term = *values[0]](double value) {
                return value + term;
            });
            break;
        case OperationKind::Multiply:
            result.array = mapped(input, [factor = *values[0]](double value) {
                return value * factor;
            });
            break;
        default:
            result.array = normalized(input, values[0].value_or(0.0), values[1].value_or(1.0));
            break;
        }
        return result;
    }
    case OperationKind::Reshape: {
        std::vector<hsize_t> target;
        if (!readShape(step.argument, elementCount(shape), target, result.error)) {
            return result;
        }
        result.array = input.reshaped(std::move(target));
        return result;
    }
    }
    return result;
}

bool preservesIntegers(const Step& step)
{
    switch (step.kind) {
    case OperationKind::Slice:
    case OperationKind::Transpose:
    case OperationKind::Reshape:
    case OperationKind::Min:
    case OperationKind::Max:
    case OperationKind::Abs:
    case OperationKind::CumSum:
    case OperationKind::CumProd:
    case OperationKind::Diff:
        return true;
    case OperationKind::Sqrt:
    case OperationKind::Normalize:
        return false;
    case OperationKind::Sum:
    case OperationKind::Prod: {
        // The fold of whole numbers is whole; what it starts from has to be.
        Arguments arguments;
        QString ignored;
        if (!splitArguments(step.argument, arguments, ignored)) {
            return true; // it will not run, so it will not print either
        }
        for (const auto& [name, value] : arguments.named) {
            if (name == QStringLiteral("initial") && value.has_value()) {
                return wholeText(*value);
            }
        }
        return true;
    }
    case OperationKind::Pow: {
        std::vector<std::optional<double>> values;
        QString ignored;
        if (!planScalars(step, values, ignored) || !values[0].has_value()) {
            return true;
        }
        // A whole power of a whole number is whole; a negative one is a
        // reciprocal and a fractional one a root, and neither is.
        return std::isfinite(*values[0]) && *values[0] >= 0.0
               && *values[0] == std::trunc(*values[0]);
    }
    case OperationKind::Clip:
    case OperationKind::Add:
    case OperationKind::Multiply: {
        std::vector<std::optional<double>> values;
        QString ignored;
        if (!planScalars(step, values, ignored)) {
            return true;
        }
        return std::all_of(values.begin(), values.end(), [](const std::optional<double>& v) {
            return !v.has_value() || (std::isfinite(*v) && *v == std::trunc(*v));
        });
    }
    }
    return true;
}

} // namespace postproc
