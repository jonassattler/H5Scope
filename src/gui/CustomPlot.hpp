// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "DatasetLookup.hpp"
#include "H5Thread.hpp"

#include <QAbstractListModel>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QtGraphs/QAbstractSeries>
#include <QtQml/qqmlregistration.h>

#include <vector>

namespace gui {

/// One custom plot tab: a list of 1-D slices drawn together on one pair of
/// axes.
///
/// The Data Viewer's plot is one more reading of the table the grid shows, and
/// so is welded to the tree's selection -- `DatasetPlot` is constructed over
/// `DatasetTableModel` and cannot be anything else. This is the other question:
/// *these* curves, from wherever they are in the file, against one x. Nothing
/// here is about a selection, which is why a custom tab stays put while the
/// reader browses.
///
/// It is deliberately shaped like `DatasetPlot` from the outside --
/// `drawnSeries`, `seriesCount`, `pointCount`, `minimum`, `seriesLabel`,
/// `fill` and the rest -- because that shape is what `PlotSurface.qml`,
/// `PlotLegend.qml` and `PlotSettingsPanel.qml` ask for, and those three are
/// about seventeen hundred lines of tuned Qt Graphs, colour and zoom behaviour
/// that must not be forked to serve a second plot. A custom tab hands them this
/// object instead and they draw it without knowing the difference.
///
/// The list model underneath is the entries, one row each, with the error the
/// entry's last read gave. That part follows `PostprocessModel`: a row the
/// reader types into, an error role beside it, and a check that runs on every
/// keystroke. There is no adder row -- the pipeline has one because the chain
/// guide has to run through it, and an entry list has no chain -- so a row is
/// an entry and the two indexings never have to be told apart.
class CustomPlot : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Obtained from AppController.customPlots")

    /// What the tab is called. Unique across the set, which CustomPlotSet
    /// enforces; written through `CustomPlotSet::setName` rather than here so
    /// that there is one place the uniqueness rule lives.
    Q_PROPERTY(QString name READ name NOTIFY nameChanged)

    // --- where the points sit along x ------------------------------------
    Q_PROPERTY(XMode xMode READ xMode WRITE setXMode NOTIFY xSourceChanged)
    /// The 1-D slice read as the time base, when `xMode` is Dataset. Written
    /// the same way an entry is: `/committed/morning[:]`.
    Q_PROPERTY(QString xExpression READ xExpression WRITE setXExpression
                   NOTIFY xSourceChanged)
    /// Why the time base will not read, or empty. Its own property rather than
    /// a role because it is not one of the rows.
    Q_PROPERTY(QString xError READ xError NOTIFY changed)
    /// Whether a time base has been read and can be drawn against.
    Q_PROPERTY(bool xReady READ xReady NOTIFY changed)
    /// The extent of the time base, when there is one. The axis is drawn
    /// against these rather than against a start and a step, because a time
    /// base need not be evenly spaced and need not start at anything in
    /// particular -- which is most of why a reader reaches for one.
    Q_PROPERTY(double xMinimum READ xMinimum NOTIFY changed)
    Q_PROPERTY(double xMaximum READ xMaximum NOTIFY changed)

    // --- the DatasetPlot-shaped face the surface draws --------------------
    Q_PROPERTY(QVariantList drawnSeries READ drawnSeries NOTIFY changed)
    Q_PROPERTY(int seriesCount READ seriesCount NOTIFY changed)
    Q_PROPERTY(int initialSeriesLimit READ initialSeriesLimit CONSTANT)
    /// Every point drawn, across every line -- which is the number the tab's
    /// footer states. `DatasetPlot` counts points *per line* because its lines
    /// are rows of one table and all the same length; these are slices of
    /// different datasets and need not be.
    Q_PROPERTY(int pointCount READ pointCount NOTIFY changed)
    Q_PROPERTY(int sourceSeriesCount READ sourceSeriesCount NOTIFY changed)
    Q_PROPERTY(bool thinned READ thinned NOTIFY changed)
    Q_PROPERTY(double minimum READ minimum NOTIFY changed)
    Q_PROPERTY(double maximum READ maximum NOTIFY changed)
    /// How long the x axis is, in positions. What `len(data)` means here.
    Q_PROPERTY(int sourcePointCount READ sourcePointCount NOTIFY changed)
    Q_PROPERTY(double xStart READ xStart WRITE setXStart NOTIFY xAxisChanged)
    Q_PROPERTY(double xStep READ xStep WRITE setXStep NOTIFY xAxisChanged)
    Q_PROPERTY(bool hasData READ hasData NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    /// Whether anything has been asked of the file yet for this tab. The view
    /// says "add a dataset" rather than "nothing to plot" while this is false.
    Q_PROPERTY(bool empty READ empty NOTIFY changed)

public:
    /// Where the x of each point comes from.
    enum XMode {
        Index,   ///< the element's own position, 0, 1, 2 ...
        Range,   ///< a stated start, step and stop, as the plot tab's is
        Dataset, ///< another 1-D slice, read as a time base
    };
    Q_ENUM(XMode)

    /// Whether an entry shorter or longer than the axis is laid point for
    /// point along it or spread across the whole of it.
    enum Scaling {
        /// Sample i sits at position i. A line shorter than the axis stops
        /// early; one longer than it is cut where the axis ends.
        Align,
        /// The samples are spread evenly over the whole extent of the axis,
        /// whatever their number. Two runs of different length taken over the
        /// same interval line up this way and no other.
        Stretch,
    };
    Q_ENUM(Scaling)

    enum Roles {
        ExpressionRole = Qt::UserRole + 1,
        /// Why this entry will not draw, or empty. What the row prints in
        /// amber under the box.
        ErrorRole,
        /// Points held for this entry, after thinning.
        PointsRole,
        /// Elements the slice has in the file, before thinning.
        SourcePointsRole,
        ScalingRole,
        /// Whether align and stretch mean different things for this entry.
        /// They do not when it is exactly as long as the axis, and the pair is
        /// shown disabled rather than hidden so the row keeps its shape.
        ScalableRole,
        DrawnRole,
    };
    Q_ENUM(Roles)

    CustomPlot(QString name, DatasetLookup* lookup, QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] QString name() const { return name_; }
    void setName(QString name);

    [[nodiscard]] XMode xMode() const { return xMode_; }
    void setXMode(XMode mode);
    [[nodiscard]] QString xExpression() const { return xExpression_; }
    void setXExpression(const QString& text);
    [[nodiscard]] QString xError() const { return xProblem_; }
    [[nodiscard]] bool xReady() const { return !xValues_.empty(); }
    [[nodiscard]] double xMinimum() const { return xMinimum_; }
    [[nodiscard]] double xMaximum() const { return xMaximum_; }

    /// Why a line typed into the time-base box will not read, checked without
    /// applying it. The counterpart of `entryError` for the one expression
    /// that is not a row.
    Q_INVOKABLE [[nodiscard]] QString xExpressionError(const QString& text) const;

    // --- the entries ------------------------------------------------------
    /// Add a slice, written as a line. Returns the row it landed on.
    Q_INVOKABLE int addExpression(const QString& text);
    /// Add every 1-D line of `path`, as the plot tab would draw them: the last
    /// dimension runs along x and every other one is spread over the lines.
    ///
    /// Asynchronous, because it has to know the shape first. The rows appear
    /// when the answer does.
    ///
    /// A dataset of more than `kCrowdedLines` lines adds nothing and emits
    /// `crowding` instead, unless `confirmed`. Nothing here refuses: the
    /// reader is told what they are about to ask for and asked again, which is
    /// the same stance the legend's `all` takes on a table of ten thousand
    /// rows.
    Q_INVOKABLE void addDataset(const QString& path, bool confirmed = false);
    Q_INVOKABLE void removeEntry(int row);
    Q_INVOKABLE void moveEntry(int from, int to);
    Q_INVOKABLE void clearEntries();
    /// Type into a row's box.
    Q_INVOKABLE void setExpression(int row, const QString& text);
    /// Whether what is in the box right now would read, without applying it --
    /// what the box checks on every keystroke, exactly as the pipeline's
    /// argument box does. Answers only from what has already been resolved;
    /// see DatasetLookup.
    Q_INVOKABLE [[nodiscard]] QString entryError(int row, const QString& text) const;
    Q_INVOKABLE void setScaling(int row, Scaling scaling);

    // --- what the surface and the legend ask -------------------------------
    [[nodiscard]] QVariantList drawnSeries() const;
    [[nodiscard]] int seriesCount() const;
    /// No such thing here, which is what -1 says. The legend's "first N"
    /// button exists to put a table of ten thousand rows back to the window a
    /// *selection* opened on; a custom plot opens on nothing and every entry
    /// in it was put there on purpose, so there is no number to go back to.
    [[nodiscard]] static int initialSeriesLimit() { return -1; }
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
    [[nodiscard]] bool hasData() const;
    [[nodiscard]] QString error() const;
    [[nodiscard]] bool empty() const { return entries_.empty(); }

    Q_INVOKABLE [[nodiscard]] QString seriesLabel(int series) const;
    Q_INVOKABLE [[nodiscard]] bool seriesVisible(int series) const;
    Q_INVOKABLE void setSeriesVisible(int series, bool visible);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void selectNone();
    Q_INVOKABLE void selectFirst(int count);

    /// Load entry `series` into `target`, which QML created on its graph.
    Q_INVOKABLE void fill(QAbstractSeries* target, int series);

    // --- saved views -------------------------------------------------------
    /// Everything about this tab that is not the drawing: the entries, their
    /// scaling and what is drawn, and the x axis. Plain data, so a saved view
    /// is a value and not a second object graph.
    [[nodiscard]] QVariantMap state() const;
    void setState(const QVariantMap& state);

    /// The paths this tab names, including the time base. What has to be
    /// resolved before anything can be said about it.
    [[nodiscard]] QStringList paths() const;

    /// Read everything again. Coalesced: several edits in one turn of the
    /// event loop produce one job and one crossing.
    void invalidate();

signals:
    /// Anything that changes which lines are drawn or what they hold.
    void changed();
    /// Where along x the points sit. The same points, moved.
    void xAxisChanged();
    /// The x source or its expression changed, which is a re-read.
    void xSourceChanged();
    void nameChanged();
    /// Something worth telling the reader that is not an error on a row.
    void notice(const QString& message);
    /// `path` would put `lines` lines in this plot, which is more than are
    /// worth drawing without being asked. Answered by calling addDataset again
    /// with `confirmed`, or by not calling it.
    void crowding(const QString& path, int lines);

private:
    struct Entry {
        QString expression;
        Scaling scaling = Align;
        bool drawn = true;

        /// Filled by the last read.
        QString problem;
        std::vector<double> values;
        int stride = 1;      ///< elements skipped between drawn points
        int sourceLength = 0; ///< elements the slice has in the file
    };

    /// One line as the job hands it back.
    struct LineData {
        QString problem;
        std::vector<double> values;
        int stride = 1;
        int sourceLength = 0;
    };

    void refresh();
    void touch(int row, const QVector<int>& roles = {});
    /// The extent, the point total and whether anything is drawable, worked
    /// out once per read rather than per binding.
    void recount();
    /// Where point `at` of `entry` sits along the axis, in axis positions.
    [[nodiscard]] double positionOf(const Entry& entry, std::size_t at) const;
    /// Whether align and stretch differ for this entry.
    [[nodiscard]] bool scalable(const Entry& entry) const;

    QString name_;
    DatasetLookup* lookup_ = nullptr;

    std::vector<Entry> entries_;

    XMode xMode_ = Index;
    QString xExpression_;
    QString xProblem_;
    std::vector<double> xValues_;
    int xSourceLength_ = 0;
    double xMinimum_ = 0.0;
    double xMaximum_ = 1.0;
    double xStart_ = 0.0;
    double xStep_ = 1.0;

    int points_ = 0;
    double minimum_ = 0.0;
    double maximum_ = 0.0;
    bool hasFinite_ = false;

    H5Requests requests_;
    /// Fires once per turn of the event loop however many edits landed in it.
    /// A reader dragging a slider or holding a key down must not put one job
    /// on the thread per keystroke.
    QTimer coalesce_;

public:
    /// Points per entry. The plot tab's number, for the plot tab's reason:
    /// beyond a couple of thousand a line is drawing more detail than a screen
    /// can resolve.
    static constexpr int kMaxPoints = 2048;
    /// Lines past which adding a whole dataset asks first.
    ///
    /// Not a limit. Past a few dozen, strokes over one another stop separating
    /// and a plot of thousands takes a while to draw -- which is worth saying
    /// before it happens and is not worth refusing over, because a reader who
    /// wants to see the shape of two thousand runs at once has asked for
    /// exactly that. The same number the plot tab opens a selection at, and
    /// the same stance the legend's `all` takes beside it: state the cost,
    /// then do as you are told.
    static constexpr int kCrowdedLines = 64;
};

} // namespace gui
