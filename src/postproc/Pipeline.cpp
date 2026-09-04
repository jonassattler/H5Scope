// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "postproc/Pipeline.hpp"

#include "h5core/Error.hpp"
#include "postproc/Subscripts.hpp"

#include <QStringList>

#include <algorithm>
#include <cstddef>

namespace postproc {
namespace {

/// The shape a selection leaves, before anything is dropped.
std::vector<hsize_t> selectedShape(const std::vector<std::vector<hsize_t>>& indices)
{
    std::vector<hsize_t> shape;
    shape.reserve(indices.size());
    for (const std::vector<hsize_t>& list : indices) {
        shape.push_back(static_cast<hsize_t>(list.size()));
    }
    return shape;
}

/// The same shape with the dimensions an integer subscript named taken out.
/// Each of those holds exactly one index, so this is a reshape of a contiguous
/// buffer and costs nothing.
std::vector<hsize_t> afterDrop(const std::vector<hsize_t>& shape,
                               const std::vector<bool>& drop)
{
    std::vector<hsize_t> out;
    out.reserve(shape.size());
    for (std::size_t d = 0; d < shape.size(); ++d) {
        if (d >= drop.size() || !drop[d]) {
            out.push_back(shape[d]);
        }
    }
    return out;
}

/// Whether every dimension names a consecutive ascending run, which is what
/// `:` and `a:b` and a bare index all produce. When they all do, the whole
/// selection is one hyperslab and one read.
bool oneBox(const std::vector<std::vector<hsize_t>>& indices)
{
    return std::all_of(indices.begin(), indices.end(),
                       [](const std::vector<hsize_t>& list) {
                           const Progression run = asProgression(list);
                           return run.uniform && (list.size() == 1 || run.step == 1);
                       });
}

/// The subscripts the pipeline's first step stands for.
///
/// That step is the slice above the table rather than one the reader added, so
/// it is never empty in practice; when it is -- a pipeline read back before a
/// dataset was chosen -- an omitted line means the whole of everything, which
/// is what "..." says in the grammar. A Slice the reader added later is a step
/// like any other and an empty one is an error, as it should be.
QString sliceArgument(const std::vector<Step>& steps)
{
    if (steps.empty() || steps.front().argument.trimmed().isEmpty()) {
        return QStringLiteral("...");
    }
    return steps.front().argument;
}

/// How many entries from `at` in `list` count up by one, capped at `limit`.
/// The same trick TableAxes::runLength plays on the grid: consecutive indices
/// are one hyperslab, so they are one read.
std::size_t runLength(const std::vector<hsize_t>& list, std::size_t at,
                      std::size_t limit)
{
    std::size_t length = 1;
    while (at + length < list.size() && length < limit
           && list[at + length] == list[at + length - 1] + 1) {
        ++length;
    }
    return length;
}

} // namespace

ArrayResult read(const h5core::DataSource& source,
                 const std::vector<std::vector<hsize_t>>& indices,
                 const std::vector<bool>& drop)
{
    ArrayResult result;
    const std::vector<hsize_t> shape = selectedShape(indices);
    const hsize_t total = elementCount(shape);

    if (total > kMaxElements) {
        result.error =
            QStringLiteral("this selection is %1 elements; postprocessing has "
                           "to read all of them into memory at once, and stops "
                           "at %2 — narrow the slice")
                .arg(total)
                .arg(kMaxElements);
        return result;
    }

    std::vector<double> values;
    values.reserve(static_cast<std::size_t>(total));

    try {
        if (indices.empty()) {
            // A scalar: one element, and the hyperslab that names it is empty.
            const h5core::NumericWindow window = source.readNumericWindow({}, {});
            values = window.values;
        } else if (oneBox(indices)) {
            std::vector<hsize_t> offset;
            offset.reserve(indices.size());
            for (const std::vector<hsize_t>& list : indices) {
                offset.push_back(list.front());
            }
            const h5core::NumericWindow window =
                source.readNumericWindow(offset, shape);
            values = window.values;
        } else {
            // Every position of every dimension but the last, and along the
            // last one the longest run of consecutive indices at a time.
            const std::size_t last = indices.size() - 1;
            const hsize_t rows = elementCount(
                std::vector<hsize_t>(shape.begin(), shape.begin() + static_cast<
                                                        std::ptrdiff_t>(last)));
            std::vector<hsize_t> cursor(last, 0);
            std::vector<hsize_t> offset(indices.size(), 0);
            std::vector<hsize_t> count(indices.size(), 1);

            for (hsize_t row = 0; row < rows; ++row) {
                for (std::size_t d = 0; d < last; ++d) {
                    offset[d] = indices[d][cursor[d]];
                }
                for (std::size_t at = 0; at < indices[last].size();) {
                    const std::size_t length =
                        runLength(indices[last], at, indices[last].size());
                    offset[last] = indices[last][at];
                    count[last] = static_cast<hsize_t>(length);
                    const h5core::NumericWindow window =
                        source.readNumericWindow(offset, count);
                    values.insert(values.end(), window.values.begin(),
                                  window.values.end());
                    at += length;
                }
                for (std::size_t d = last; d-- > 0;) {
                    if (++cursor[d] < indices[d].size()) {
                        break;
                    }
                    cursor[d] = 0;
                }
            }
        }
    } catch (const h5core::H5Error& error) {
        result.error = QString::fromStdString(error.what());
        return result;
    }

    if (values.size() != static_cast<std::size_t>(total)) {
        result.error = QStringLiteral("read %1 of %2 elements")
                           .arg(values.size())
                           .arg(total);
        return result;
    }

    // Dropping is a reshape of a contiguous buffer: every dimension an integer
    // subscript named holds one index, so taking it out moves nothing.
    result.array = Array(afterDrop(shape, drop), std::move(values));
    return result;
}

Trace trace(const std::vector<hsize_t>& shape, const std::vector<Step>& steps,
            std::size_t upTo)
{
    Trace result;
    result.stages.resize(steps.size());
    result.output = shape;

    const std::size_t limit = std::min(upTo, steps.size());
    for (std::size_t i = 0; i < limit; ++i) {
        // A scalar has no subscripts, so the slice this pipeline was handed
        // rather than asked for is passed over rather than refused.
        if (i == 0 && shape.empty() && steps[i].kind == OperationKind::Slice) {
            result.stages[i].shape = shape;
            ++result.ran;
            continue;
        }
        Step step = steps[i];
        if (i == 0 && step.kind == OperationKind::Slice) {
            step.argument = sliceArgument(steps);
        }
        ShapeResult stage = shapeAfter(step, result.output);
        if (!stage.ok()) {
            result.error = stage.error;
            result.stages[i] = std::move(stage);
            return result;
        }
        result.output = stage.shape;
        result.stages[i] = std::move(stage);
        ++result.ran;
    }
    return result;
}

RunResult run(const h5core::DataSource& source, const std::vector<Step>& steps,
              std::size_t upTo)
{
    RunResult result;
    const std::vector<hsize_t>& shape = source.info().shape;

    if (!source.info().isNumeric()) {
        result.error = QStringLiteral("postprocessing works on numbers; this "
                                      "dataset holds %1")
                           .arg(QString::fromStdString(source.info().type.description));
        return result;
    }

    // Step 0 is the slice, and it is not applied to an array -- it *is* the
    // read. Resolving it here rather than after the fact is what keeps a
    // 10^9-element dataset openable: only the elements it names are ever
    // fetched, as hyperslabs, and the cap is checked against them.
    std::vector<std::vector<hsize_t>> indices;
    std::vector<bool> drop;
    if (!shape.empty()) {
        std::vector<IndexExpression> chosen;
        QStringList written;
        QString error;
        if (!readSubscripts(sliceArgument(steps), shape, chosen, written, error)) {
            result.error = error;
            return result;
        }
        for (const IndexExpression& subscript : chosen) {
            indices.push_back(subscript.indices);
            drop.push_back(subscript.form == IndexExpression::Form::Single);
        }
    }

    ArrayResult selected = read(source, indices, drop);
    if (!selected.ok()) {
        result.error = selected.error;
        return result;
    }
    result.array = std::move(selected.array);
    result.ran = 1;

    const std::size_t limit = std::min(upTo, steps.size());
    for (std::size_t i = 1; i < limit; ++i) {
        ArrayResult next = apply(steps[i], result.array);
        if (!next.ok()) {
            // Everything up to here is real and is what the views draw; the
            // reason travels beside it rather than instead of it.
            result.error = next.error;
            return result;
        }
        result.array = std::move(next.array);
        ++result.ran;
    }
    return result;
}

} // namespace postproc
