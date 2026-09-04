// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "postproc/Operations.hpp"

#include "postproc/Subscripts.hpp"

#include <QStringList>

#include <algorithm>
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

/// Fold `input` along `axes`, keeping the smaller or the larger.
///
/// NaN wins, whichever way the comparison runs, because that is what numpy's
/// `min` and `max` do: a NaN anywhere in what is being folded comes out of it.
/// This application already writes NaN into a cell it could not read, so an
/// unreadable element poisons the reduction over it -- which is the honest
/// answer and the same one h5py would give.
Array reduce(const Array& input, const std::vector<std::size_t>& axes,
             bool minimum)
{
    const std::vector<hsize_t>& shape = input.shape();
    const std::vector<hsize_t> outShape = withoutAxes(shape, axes);
    const hsize_t outSize = elementCount(outShape);

    std::vector<double> out(static_cast<std::size_t>(outSize),
                            minimum ? std::numeric_limits<double>::infinity()
                                    : -std::numeric_limits<double>::infinity());
    if (outSize == 0 || elementCount(shape) == 0) {
        return Array(outShape, std::move(out));
    }

    // How far the output position moves when each input dimension advances:
    // zero for an axis being folded away, and the output's own row-major
    // stride for one that survives. Carried alongside the walk so neither the
    // input nor the output index has to be unravelled per element.
    std::vector<std::ptrdiff_t> outStride(shape.size(), 0);
    std::ptrdiff_t running = 1;
    for (std::size_t d = shape.size(); d-- > 0;) {
        if (std::find(axes.begin(), axes.end(), d) != axes.end()) {
            continue;
        }
        outStride[d] = running;
        running *= static_cast<std::ptrdiff_t>(shape[d]);
    }

    const std::vector<double> values = input.values();
    std::vector<hsize_t> index(shape.size(), 0);
    std::ptrdiff_t position = 0;
    for (const double value : values) {
        double& target = out[static_cast<std::size_t>(position)];
        if (std::isnan(value)) {
            target = value;
        } else if (!std::isnan(target)) {
            target = minimum ? std::min(target, value) : std::max(target, value);
        }
        for (std::size_t d = shape.size(); d-- > 0;) {
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
    // then the two that rearrange, then the two that fold, then the one that
    // does neither.
    //
    // Lower case throughout, and it is the name rather than a presentation of
    // it: these are numpy's own words, they are set beside "slice", "add" and
    // "output" -- which the panel writes itself and has always written this way
    // -- and a chain that read "slice / Max / Reshape" was capitalising four of
    // its seven rows and no others. operationNamed() ignores case, so a
    // pipeline remembered when they were capitalised still reads back.
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
        {OperationKind::Abs, QStringLiteral("abs"), QString{}, QString{}},
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
        std::vector<std::vector<hsize_t>> indices;
        std::vector<bool> drop;
        if (!readSlice(step.argument, shape, indices, drop, result.error)) {
            return result;
        }
        for (std::size_t d = 0; d < indices.size(); ++d) {
            if (!drop[d]) {
                result.shape.push_back(static_cast<hsize_t>(indices[d].size()));
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
    case OperationKind::Max: {
        std::vector<std::size_t> axes;
        if (!readReductionAxes(step.argument, rank, axes, result.error)) {
            return result;
        }
        if (!reducible(shape, axes)) {
            result.error = reductionRefusal(step.kind == OperationKind::Min);
            return result;
        }
        result.shape = withoutAxes(shape, axes);
        return result;
    }
    case OperationKind::Abs:
        result.shape = shape;
        return result;
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
    case OperationKind::Max: {
        std::vector<std::size_t> axes;
        if (!readReductionAxes(step.argument, rank, axes, result.error)) {
            return result;
        }
        if (!reducible(shape, axes)) {
            result.error = reductionRefusal(step.kind == OperationKind::Min);
            return result;
        }
        result.array = reduce(input, axes, step.kind == OperationKind::Min);
        return result;
    }
    case OperationKind::Abs: {
        std::vector<double> values = input.values();
        for (double& value : values) {
            value = std::abs(value);
        }
        result.array = Array(shape, std::move(values));
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

} // namespace postproc
