// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "DatasetLookup.hpp"

#include "H5Session.hpp"
#include "h5core/Dataset.hpp"
#include "h5core/Error.hpp"
#include "h5core/File.hpp"
#include "postproc/Operations.hpp"
#include "postproc/Subscripts.hpp"

#include <algorithm>
#include <utility>

namespace gui {

namespace {

/// The shape a selection leaves, after the bare-index dimensions go.
[[nodiscard]] std::vector<hsize_t>
selectedShape(const std::vector<std::vector<hsize_t>>& indices,
              const std::vector<bool>& drop)
{
    std::vector<hsize_t> shape;
    for (std::size_t d = 0; d < indices.size(); ++d) {
        if (d >= drop.size() || !drop[d]) {
            shape.push_back(static_cast<hsize_t>(indices[d].size()));
        }
    }
    return shape;
}

/// Which dimension of `indices` is the one that survives. Only meaningful once
/// the selection is known to be a line, which is to say exactly one does.
[[nodiscard]] std::size_t survivor(const std::vector<std::vector<hsize_t>>& indices,
                                   const std::vector<bool>& drop)
{
    for (std::size_t d = 0; d < indices.size(); ++d) {
        if (d >= drop.size() || !drop[d]) {
            return d;
        }
    }
    return 0;
}

} // namespace

Expression splitExpression(const QString& text)
{
    Expression result;
    const QString line = text.trimmed();

    if (line.isEmpty()) {
        result.error = QStringLiteral("write a path, as /group/dataset[:]");
        return result;
    }

    // Scan for the outermost bracket pair. Depth is what separates a bracket
    // in a link name from the one that opens the subscript: only a `[` seen at
    // depth zero can be the opening one, and only the last such can be, since
    // the path is everything before the subscript.
    int depth = 0;
    qsizetype opened = -1;
    qsizetype closed = -1;
    for (qsizetype i = 0; i < line.size(); ++i) {
        const QChar c = line.at(i);
        if (c == QLatin1Char('[')) {
            if (depth == 0) {
                opened = i;
                closed = -1;
            }
            ++depth;
        } else if (c == QLatin1Char(']')) {
            --depth;
            if (depth < 0) {
                result.error =
                    QStringLiteral("a ']' here closes a '[' that was never opened");
                return result;
            }
            if (depth == 0) {
                closed = i;
            }
        }
    }
    if (depth > 0) {
        result.error = QStringLiteral("a '[' here is never closed");
        return result;
    }

    if (opened < 0) {
        // A bare path, which selects the whole of the object -- exactly what
        // the slice line prints for a scalar, and what a reader writes for a
        // vector they want all of.
        result.path = line;
    } else {
        if (closed != line.size() - 1) {
            result.error =
                QStringLiteral("there is text after the closing ']' — the "
                               "subscript comes last");
            return result;
        }
        result.path = line.left(opened).trimmed();
        result.subscript = line.mid(opened + 1, closed - opened - 1).trimmed();
    }

    if (result.path.isEmpty()) {
        result.error = QStringLiteral("there is no path in front of the subscript");
        return result;
    }
    if (!result.path.startsWith(QLatin1Char('/'))) {
        result.error = QStringLiteral("a path starts at the root, as /group/dataset");
        return result;
    }
    return result;
}

bool resolveLine(const QString& subscript, const std::vector<hsize_t>& shape,
                 std::vector<std::vector<hsize_t>>& indices,
                 std::vector<bool>& drop, QString& error)
{
    indices.clear();
    drop.clear();
    error.clear();

    if (shape.empty()) {
        error = QStringLiteral("this is a single value, and a line needs several "
                               "— pick a dataset with a dimension in it");
        return false;
    }

    // An empty body is the whole of the object, which is what a bare path
    // means. Spelt out rather than passed through, because readSubscripts
    // reads an empty line as a missing subscript for dimension 0.
    const QString body =
        subscript.isEmpty() ? QStringLiteral("...") : subscript;

    std::vector<postproc::IndexExpression> chosen;
    QStringList written;
    if (!postproc::readSubscripts(body, shape, chosen, written, error)) {
        return false;
    }

    indices.reserve(chosen.size());
    drop.reserve(chosen.size());
    for (const postproc::IndexExpression& part : chosen) {
        indices.push_back(part.indices);
        drop.push_back(part.form == postproc::IndexExpression::Form::Single);
    }

    const std::vector<hsize_t> left = selectedShape(indices, drop);
    if (left.size() == 1) {
        return true;
    }

    // The two ways to miss, each said in terms of what was written rather than
    // in terms of rank -- "this is a 48 x 3 block" is a sentence about the
    // subscript on screen, where "rank 2" is a sentence about a type system.
    if (left.empty()) {
        error = QStringLiteral("this names one element, not a line — write ':' "
                               "on the dimension you want drawn");
        return false;
    }
    error = QStringLiteral("this selects %1, and an entry is one line — hold "
                           "every dimension but one at a single index")
                .arg(postproc::describeShape(left));
    return false;
}

int thinToPoints(std::vector<std::vector<hsize_t>>& indices,
                 const std::vector<bool>& drop, int maxPoints)
{
    if (indices.empty() || maxPoints <= 0) {
        return 1;
    }
    const std::size_t d = survivor(indices, drop);
    std::vector<hsize_t>& along = indices[d];
    const std::size_t length = along.size();
    if (length <= static_cast<std::size_t>(maxPoints)) {
        return 1;
    }

    // Ceiling division, so the result never exceeds the cap: a line of 4097
    // points at a stride of 2 would be 2049, which is one more than was asked
    // for.
    const std::size_t stride =
        (length + static_cast<std::size_t>(maxPoints) - 1)
        / static_cast<std::size_t>(maxPoints);
    std::vector<hsize_t> thinned;
    thinned.reserve(length / stride + 1);
    for (std::size_t i = 0; i < length; i += stride) {
        thinned.push_back(along[i]);
    }
    along = std::move(thinned);
    return static_cast<int>(stride);
}

DatasetLookup::DatasetLookup(QObject* parent) : QObject(parent) {}

bool DatasetLookup::knows(const QString& path) const
{
    return known_.contains(path);
}

const PathFacts* DatasetLookup::facts(const QString& path) const
{
    const auto found = known_.constFind(path);
    return found == known_.constEnd() ? nullptr : &found.value();
}

void DatasetLookup::clear()
{
    if (known_.isEmpty()) {
        return;
    }
    known_.clear();
    // Answers about the file that was open are answers about nothing now.
    requests_.reset();
    emit changed();
}

void DatasetLookup::remember(const QString& path, PathFacts facts)
{
    const auto held = known_.constFind(path);
    if (held != known_.constEnd() && held.value().problem == facts.problem
        && held.value().shape == facts.shape) {
        return;
    }
    known_.insert(path, std::move(facts));
    emit changed();
}

PathFacts lookupFacts(h5core::File* file, const QString& path)
{
    PathFacts facts;
    if (file == nullptr) {
        facts.problem = QStringLiteral("no file is open");
        return facts;
    }

    const std::string native = path.toStdString();
    if (!file->hasLink(native)) {
        facts.problem = QStringLiteral("there is nothing at this path");
        return facts;
    }

    const h5core::NodeInfo node = file->nodeInfo(native);
    if (node.kind == h5core::NodeKind::Unresolved) {
        facts.problem = QStringLiteral("this link does not resolve to an object");
        return facts;
    }
    if (node.kind != h5core::NodeKind::Dataset) {
        facts.problem =
            QStringLiteral("this is a %1, and only a dataset holds values")
                .arg(QString::fromStdString(h5core::toString(node.kind)).toLower());
        return facts;
    }

    facts.isDataset = true;
    try {
        const h5core::Dataset dataset(*file, native);
        const h5core::DatasetInfo& info = dataset.info();
        facts.shape = info.shape;
        if (info.isNull()) {
            facts.problem = QStringLiteral("this dataset holds no elements");
            return facts;
        }
        if (!info.readable()) {
            facts.problem = QString::fromStdString(info.unreadableReason());
            return facts;
        }
        if (!info.isNumeric()) {
            facts.problem = QStringLiteral("this dataset holds %1, and only "
                                           "numbers can be plotted")
                                .arg(QString::fromStdString(info.type.description));
            return facts;
        }
        if (info.isScalar()) {
            facts.problem = QStringLiteral("this dataset is a single value, and "
                                           "a line needs several");
            return facts;
        }
        facts.usable = true;
    } catch (const h5core::H5Error& error) {
        facts.problem = QString::fromStdString(error.summary());
    }
    return facts;
}

void DatasetLookup::resolve(const QStringList& paths, std::function<void()> then)
{
    QStringList wanted;
    for (const QString& path : paths) {
        if (!path.isEmpty() && !known_.contains(path) && !wanted.contains(path)) {
            wanted.append(path);
        }
    }
    if (wanted.isEmpty()) {
        // Nothing to ask, so the caller carries on now rather than next turn.
        // Callers use this as "make sure, then go", and a special case at every
        // one of them would be the same test written several times.
        if (then) {
            then();
        }
        return;
    }

    H5Thread::instance().submit(
        requests_,
        [wanted](H5Session& session) {
            std::vector<std::pair<QString, PathFacts>> answers;
            answers.reserve(static_cast<std::size_t>(wanted.size()));
            h5core::File* file = session.file();
            for (const QString& path : wanted) {
                answers.emplace_back(path, lookupFacts(file, path));
            }
            return answers;
        },
        [this, then = std::move(then)](
            std::vector<std::pair<QString, PathFacts>> answers) {
            for (auto& answer : answers) {
                known_.insert(answer.first, std::move(answer.second));
            }
            emit changed();
            if (then) {
                then();
            }
        });
}

QString expressionProblem(const QString& text, const DatasetLookup& lookup)
{
    const Expression parts = splitExpression(text);
    if (!parts.valid()) {
        return parts.error;
    }

    const PathFacts* facts = lookup.facts(parts.path);
    if (facts == nullptr) {
        // Not known to be anything, which is not the same as known to be
        // wrong. See the note on DatasetLookup: the reader finds out when the
        // read comes back.
        return QString();
    }
    if (!facts->usable) {
        return facts->problem;
    }

    std::vector<std::vector<hsize_t>> indices;
    std::vector<bool> drop;
    QString error;
    // The reason is the answer here, not the verdict: `error` is empty exactly
    // when this returns true.
    (void)resolveLine(parts.subscript, facts->shape, indices, drop, error);
    return error;
}

} // namespace gui
