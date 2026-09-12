// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "H5Thread.hpp"

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>

#include <hdf5.h>

#include <functional>
#include <vector>

namespace h5core {
class File;
}

namespace gui {

/// A path and a subscript, as a reader writes the two of them on one line.
///
/// `/committed/morning[0:24]`. This is the notation the slice bar above the
/// table already prints -- `AppController::sliceExpression` builds exactly this
/// string -- and the custom plot's entry boxes are the first place it is typed
/// back in rather than only read, so it needs a parser.
struct Expression {
    QString path;
    /// The body, with the outer brackets taken off. Empty means the whole of
    /// the dataset, which is what a bare path selects.
    QString subscript;
    QString error; ///< empty when the line reads

    [[nodiscard]] bool valid() const { return error.isEmpty(); }
};

/// Take a typed line apart into the object named and the subscript applied.
///
/// Splitting on the *last* `[` at bracket depth zero rather than on the first
/// one, because both halves can contain a bracket. An HDF5 link name may hold
/// one -- the example file has a `/stress/awkward_names` group precisely to
/// keep that honest -- and the subscript holds its own for numpy's fancy
/// indexing, `[0:4,5,7:10]`. Depth is what tells those apart, and it is the
/// same counter `postproc::splitSubscripts` uses on the half after the split.
///
/// An unbalanced bracket is reported in the words the subscript parser already
/// uses for it, so a reader does not learn two vocabularies for one mistake.
[[nodiscard]] Expression splitExpression(const QString& text);

/// Everything the custom plot needs to know about one path, gathered once.
struct PathFacts {
    bool isDataset = false;
    /// Whether it can be drawn at all: a readable, numeric, non-null dataset.
    bool usable = false;
    std::vector<hsize_t> shape;
    /// Why it cannot be used, when it cannot. Empty when `usable`.
    QString problem;
};

/// What is known about the paths the custom plots name, and how to find out.
///
/// This exists because of one asymmetry. Every other validator in this
/// application is pure arithmetic over a shape already in hand --
/// `TableSetupModel::sliceError` and `PostprocessModel::argumentError` both
/// check a subscript against the selected dataset, which the controller
/// described when the reader clicked it -- so both are free and both answer on
/// every keystroke. A custom plot's entry names a dataset nobody has selected,
/// and whether `/does/not/exist[:,2]` is wrong cannot be known without asking
/// the file.
///
/// So the answers are cached, and the cache is what makes typing feel like the
/// rest of the application: the common edit is fixing a subscript on a dataset
/// the plot is already drawing, and that path is already known, so it checks
/// as you type. A path this has never seen is not reported as wrong -- it is
/// not known to be anything -- and the reader finds out when the read comes
/// back and the row states its reason.
///
/// Cleared whenever the file changes. Two files can hold a `/data` that have
/// nothing to do with each other.
class DatasetLookup : public QObject
{
    Q_OBJECT

public:
    explicit DatasetLookup(QObject* parent = nullptr);

    [[nodiscard]] bool knows(const QString& path) const;
    /// The facts, or null when this has never resolved that path.
    [[nodiscard]] const PathFacts* facts(const QString& path) const;

    /// Forget everything. Called when a file is opened or closed.
    void clear();

    /// Resolve every path in `paths` not already known, in one crossing, and
    /// run `then` on this thread once they are all in.
    ///
    /// `then` runs even when there was nothing to resolve, so a caller can use
    /// this as "make sure, then carry on" without a special case.
    void resolve(const QStringList& paths, std::function<void()> then);

    /// Record what a read already found out. The refresh job opens each
    /// dataset anyway, so the facts come back with the values and nothing has
    /// to be asked twice.
    void remember(const QString& path, PathFacts facts);

signals:
    /// Something that was unknown is now known. The entry rows re-check
    /// themselves on it.
    void changed();

private:
    QHash<QString, PathFacts> known_;
    /// Never reset except by clear(): a fact about a path in the open file
    /// does not go stale, so there is never a resolve worth disowning, and
    /// disowning one would drop the continuation waiting on it.
    H5Requests requests_;
};

/// Everything the plots need to know about one path, read from the file.
///
/// On the HDF5 thread, and only there: it opens the dataset. Declared here
/// rather than kept private because the refresh job that reads the entries
/// opens each of them anyway and hands the answer straight to the cache, so
/// nothing is ever asked twice.
[[nodiscard]] PathFacts lookupFacts(h5core::File* file, const QString& path);

/// Why `text` cannot be drawn as one line, or empty when it can be -- judged
/// from what `lookup` already knows and nothing else. See the note above.
[[nodiscard]] QString expressionProblem(const QString& text,
                                        const DatasetLookup& lookup);

/// Resolve a subscript against a known shape into the per-dimension index
/// lists a read takes, and say whether what it selects is one line.
///
/// `drop` marks the dimensions written as a bare index, which are the ones
/// that go away -- the same rule `postproc::run` applies to the pipeline's
/// first step, and the reason `[:, 2]` of a 48 x 3 dataset is a line of 48 and
/// `[:, 2:3]` is a 48 x 1 block that is not one.
[[nodiscard]] bool resolveLine(const QString& subscript,
                               const std::vector<hsize_t>& shape,
                               std::vector<std::vector<hsize_t>>& indices,
                               std::vector<bool>& drop, QString& error);

/// Thin the surviving dimension of `indices` to at most `maxPoints`, and
/// return the stride that was applied.
///
/// Thinning the *index list* rather than the values is what keeps the read
/// proportional to what is drawn: a line of a million points taken every 489th
/// element reads two thousand elements, not a million. Plain stride sampling,
/// as the table's own thinning is -- a spike narrower than one stride is not
/// drawn, which is the honest cost of never reading more than the screen can
/// show.
int thinToPoints(std::vector<std::vector<hsize_t>>& indices,
                 const std::vector<bool>& drop, int maxPoints);

} // namespace gui
