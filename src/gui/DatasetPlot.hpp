// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "DatasetTableModel.hpp"

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QtGraphs/QAbstractSeries>
#include <QtQml/qqmlregistration.h>

#include <map>
#include <optional>
#include <vector>

namespace gui {

/// The Data Viewer's plot presentation: the table read as lines.
///
/// It decides nothing about *which* values those are. The table setup panel
/// owns that, through TableLayout and DatasetTableModel, and this object is one
/// more reading of exactly the table the grid shows -- so a rank-4 dataset is
/// plottable on the same terms it is browsable.
///
/// Which of those lines are drawn is this object's own question. A new
/// selection opens on the table's first kMaxInitialSeries lines and no more:
/// past a few dozen, strokes over one another stop separating and a plot of
/// ten thousand rows is a picture of nothing that takes a while to draw. What
/// the reader is looking at is stated rather than assumed -- the legend's
/// header prints "64 / 10000" and the footer says "64 lines of 10000" -- and
/// the legend's `all` beside it is the whole of the way to the rest.
///
/// Sampling is therefore per line rather than per rectangle: an arbitrary set
/// of rows is not one, and asking for the bounding rectangle of a set would
/// read everything between its ends. Each line is one
/// DatasetTableModel::sampleValues() of a single row, so the file is read in
/// proportion to what is drawn and nothing else.
///
/// The values themselves never reach QML. QML creates the Qt Graphs series and
/// hands each one back through fill(), which loads it in a single bulk
/// QXYSeries::replace(); appending points one at a time across the QML boundary
/// is what makes a graph of ten thousand points slow.
class DatasetPlot : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Obtained from AppController.datasetPlot")

    /// True: one line per table row, x running along the columns. False: the
    /// transpose. See the note on setSeriesFromRows for why this is a setting
    /// rather than a constant.
    Q_PROPERTY(bool seriesFromRows READ seriesFromRows WRITE setSeriesFromRows
                   NOTIFY changed)
    /// The lines actually drawn, by their index in the table, ascending. This
    /// is what the surface iterates and what fill() is indexed by.
    Q_PROPERTY(QVariantList drawnSeries READ drawnSeries NOTIFY changed)
    Q_PROPERTY(int seriesCount READ seriesCount NOTIFY changed)
    /// How many lines a new selection opens on, so the legend can name the
    /// number rather than carry a second copy of it.
    Q_PROPERTY(int initialSeriesLimit READ initialSeriesLimit CONSTANT)
    Q_PROPERTY(int pointCount READ pointCount NOTIFY changed)
    /// Lines the table has in total, of which seriesCount are drawn.
    Q_PROPERTY(int sourceSeriesCount READ sourceSeriesCount NOTIFY changed)
    /// True when the points shown are fewer than the table has: the plot is
    /// thinned, and the readout should say so.
    Q_PROPERTY(bool thinned READ thinned NOTIFY changed)
    Q_PROPERTY(double minimum READ minimum NOTIFY changed)
    Q_PROPERTY(double maximum READ maximum NOTIFY changed)
    /// How long the data is along x, in table positions rather than in drawn
    /// points: the length the axis below is described against, and the one a
    /// reader means by len(data).
    Q_PROPERTY(int sourcePointCount READ sourcePointCount NOTIFY changed)

    // --- where the points sit along x ------------------------------------
    /// The x of the first element, and the distance from one element to the
    /// next. Point i of the data is at `xStart + i * xStep`, so these two are
    /// not a window onto the plot but the x values themselves; the axis is
    /// drawn against what they produce. PlotSurface resolves them from the
    /// start/step/stop the reader states and pushes them down here, because
    /// the points are built in fill() and never cross into QML.
    ///
    /// Their own signal rather than `changed`: moving the axis moves the
    /// points and nothing else -- no line has appeared or gone away, and
    /// nothing has to be re-read -- so the surface re-fills what it already
    /// has instead of building another graph.
    Q_PROPERTY(double xStart READ xStart WRITE setXStart NOTIFY xAxisChanged)
    Q_PROPERTY(double xStep READ xStep WRITE setXStep NOTIFY xAxisChanged)
    Q_PROPERTY(bool numeric READ numeric NOTIFY changed)
    Q_PROPERTY(bool hasData READ hasData NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)

public:
    explicit DatasetPlot(DatasetTableModel* table, QObject* parent = nullptr);

    [[nodiscard]] bool seriesFromRows() const { return seriesFromRows_; }
    void setSeriesFromRows(bool fromRows);

    [[nodiscard]] QVariantList drawnSeries() const;
    [[nodiscard]] int seriesCount() const;
    [[nodiscard]] static int initialSeriesLimit() { return kMaxInitialSeries; }
    [[nodiscard]] int pointCount() const;
    [[nodiscard]] int sourceSeriesCount() const;
    [[nodiscard]] bool thinned() const;
    [[nodiscard]] double minimum() const;
    [[nodiscard]] double maximum() const;
    [[nodiscard]] int sourcePointCount() const;
    [[nodiscard]] double xStart() const { return xStart_; }
    void setXStart(double value);
    [[nodiscard]] double xStep() const { return xStep_; }
    void setXStep(double value);
    [[nodiscard]] bool numeric() const;
    [[nodiscard]] bool hasData() const;
    [[nodiscard]] QString error() const;

    /// What line `index` of the table is, written the way the grid writes it:
    /// the index tuple of its row, or of its column when the plot is
    /// transposed. This is the name the legend lists it under, and it names the
    /// slice rather than the drawing order -- hiding a line must not renumber
    /// the ones around it.
    Q_INVOKABLE [[nodiscard]] QString seriesLabel(int series) const;

    /// Whether line `series` of the table is drawn.
    Q_INVOKABLE [[nodiscard]] bool seriesVisible(int series) const;
    Q_INVOKABLE void setSeriesVisible(int series, bool visible);
    /// Every line in the table, and none of them. Neither is where a selection
    /// starts -- see the note above and reseed() -- and selectAll() on a table
    /// of half a million rows really does draw half a million lines: the
    /// legend prints what that costs and goes amber before the reader asks
    /// for it, rather than quietly showing fewer.
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void selectNone();
    /// The table's first `count` lines, which is what a new selection opens
    /// on and what the legend's "first %1" puts back.
    Q_INVOKABLE void selectFirst(int count);

    /// Load line `series` -- an index into the table, not into the drawn set --
    /// into `series`, which QML created on its ChartView. A cell that could not
    /// be read leaves a gap rather than a zero.
    Q_INVOKABLE void fill(QAbstractSeries* target, int series);

    /// Drop the cached lines. The next reader re-reads the file.
    void invalidate();

signals:
    /// Anything that changes *which* lines are drawn: a new selection, a
    /// rearranged table, or one of the settings above.
    void changed();
    /// Where along x the points sit. The same points, moved.
    void xAxisChanged();

private:
    /// Sample every line in the drawn set that has not been sampled yet, and
    /// with it the extent and the point count they share.
    void ensure() const;
    /// Read one line of the table as numbers.
    [[nodiscard]] DatasetTableModel::NumericGrid sampleOne(int series) const;
    /// Put the drawn set back to where a new table starts it: its first
    /// kMaxInitialSeries lines.
    void reseed();

    DatasetTableModel* table_ = nullptr;
    bool seriesFromRows_ = true;

    /// The lines to draw, ascending. The table's first kMaxInitialSeries until
    /// the reader says otherwise in the legend.
    std::vector<int> drawn_;

    double xStart_ = 0.0;
    double xStep_ = 1.0;

    /// One entry per drawn line, keyed by its index in the table -- not by its
    /// position in `drawn_`, which changes whenever a line above it is hidden.
    /// Pruned to the drawn set on every sample, so what is held is what is on
    /// screen and a line that goes away stops costing memory.
    mutable std::map<int, std::vector<double>> lines_;
    mutable int points_ = 0;
    mutable int stride_ = 1;
    mutable double minimum_ = 0.0;
    mutable double maximum_ = 0.0;
    mutable bool hasFinite_ = false;
    mutable QString error_;
    mutable bool sampled_ = false;

public:
    /// Points per line. Beyond a couple of thousand a line plot is drawing
    /// more detail than a screen can resolve, and the thinning says so.
    static constexpr int kMaxPoints = 2048;
    /// Lines a new selection opens on. A ceiling on what the reader is shown
    /// before they have asked for anything, not on what they may ask for:
    /// the legend ticks any line in the table and `select all` takes them all.
    static constexpr int kMaxInitialSeries = 64;
};

} // namespace gui
