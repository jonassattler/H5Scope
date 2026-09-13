// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "DatasetLookup.hpp"
#include "H5Thread.hpp"
#include "PlotItem.hpp"

#include <QAbstractListModel>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <optional>
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
/// about seventeen hundred lines of tuned drawing, colour and zoom behaviour
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
    Q_PROPERTY(QString xExpression READ xExpression WRITE setXExpression NOTIFY xSourceChanged)
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
    enum XMode
    {
        Index,   ///< the element's own position, 0, 1, 2 ...
        Range,   ///< a stated start, step and stop, as the plot tab's is
        Dataset, ///< another 1-D slice, read as a time base
    };
    Q_ENUM(XMode)

    /// Whether an entry shorter or longer than the axis is laid point for
    /// point along it or spread across the whole of it.
    enum Scaling
    {
        /// Sample i sits at position i. A line shorter than the axis stops
        /// early; one longer than it is cut where the axis ends.
        Align,
        /// The samples are spread evenly over the whole extent of the axis,
        /// whatever their number. Two runs of different length taken over the
        /// same interval line up this way and no other.
        Stretch,
    };
    Q_ENUM(Scaling)

    enum Roles
    {
        ExpressionRole = Qt::UserRole + 1,
        /// What the reader would rather this line were called. Empty means the
        /// expression speaks for itself.
        AliasRole,
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
    /// Give a line a name of its own.
    ///
    /// `/committed/morning[0:24]` says exactly what a line is and nothing
    /// about what it means, which is the right default and the wrong label on
    /// a plot of six of them. An alias replaces it in the legend and nowhere
    /// else: the entry box still holds the slice, because that is what is
    /// actually being read and the reader has to be able to edit it.
    Q_INVOKABLE void setAlias(int row, const QString& text);

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

    /// What the reader is looking at, in the x the axis prints.
    ///
    /// The same question DatasetPlot::setVisibleRange answers, asked of a tab
    /// whose lines come from all over the file: which of them are worth reading
    /// again over the run that is on screen, at a bucket fine enough for
    /// zooming in to mean something. Resolved per entry, because entries have
    /// their own lengths and their own scaling, and refused outright in Dataset
    /// mode -- a time base need not be monotonic, so a range of x is not a range
    /// of indices and there is nothing to narrow a read to.
    Q_INVOKABLE void setVisibleRange(double xMin, double xMax);

    /// How wide the pane the lines are drawn in is, in device-independent
    /// pixels. The same rule the plot tab follows -- a bucket is a column --
    /// and for the same reason; see DatasetPlot::setPaneColumns -- including
    /// the debounce, so a drag of the window's edge costs one read at the end
    /// of it rather than one per sixty-four pixels.
    Q_INVOKABLE void setPaneColumns(int columns);

    /// Hand every drawn entry to `target` at once. See DatasetPlot::fill: one
    /// crossing, no points built on the way, and the values are **borrowed**.
    Q_INVOKABLE void fill(gui::PlotItem* target);

    /// Entry `series` as a renderer would be given it, drawn or not, and the
    /// axis the tab is drawn against -- which is a time base in Dataset mode
    /// and a start and a step otherwise.
    ///
    /// The seam tests/test_customplot.cpp asserts the three x modes through.
    /// gui::samplesOf() over these two is what fill() hands a renderer, so the
    /// suite reads the points with no engine and no graph.
    [[nodiscard]] PlotLine lineOf(int series) const;
    [[nodiscard]] PlotAxis drawingAxis() const;

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
    ///
    /// It does not empty the renderer. What is on screen goes on being drawn
    /// until the answer lands and fill() hands over the replacement, which is
    /// what keeps a tab from flashing blank every time a row is added, a slider
    /// is dragged or the pane changes width. discard() is the version for the
    /// cases where the old line is no longer a reading of anything.
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
    /// One resolution of an entry's closer look: an aligned run of that
    /// entry's own elements, summarised at one bucket size.
    ///
    /// The plot tab's DatasetPlot::Detail holds one of these for every drawn
    /// line at once, because every line of a table is the same length and takes
    /// the same run. Here they are per entry, because these lines come from all
    /// over the file and need not be the same length as one another.
    struct Level
    {
        PlotWindow window;
        /// Elements of this line between one of its drawn points and the next.
        /// Half a bucket, as `Entry::step` is.
        double step = 1.0;
        std::vector<double> values;
    };

    struct Entry
    {
        QString expression;
        /// What the legend calls it, when the expression will not do.
        QString alias;
        Scaling scaling = Align;
        bool drawn = true;

        /// Filled by the last read.
        QString problem;
        std::vector<double> values;
        double step = 1.0;    ///< axis positions between drawn points
        int sourceLength = 0; ///< elements the slice has in the file

        /// The closer look: the same line over an aligned run of itself, read
        /// at a finer bucket because the reader has zoomed into that run.
        ///
        /// Held *beside* the whole-line summary rather than instead of it, and
        /// several at once, for the reasons DatasetPlot::Detail gives: the
        /// extent stays the line's own so the y axis does not rescale as detail
        /// arrives, zooming in either direction draws from what is already in
        /// hand, and there is always something correct on screen while a read
        /// is in flight. Empty when there is none, which is the usual case.
        std::vector<Level> levels;
    };

    /// One line as the job hands it back.
    struct LineData
    {
        QString problem;
        std::vector<double> values;
        double step = 1.0;
        int sourceLength = 0;
    };

    void refresh();

    // --- the closer look ---------------------------------------------------
    /// The run of its own elements `entry` would be read over, or nothing when
    /// there is no point: no window pushed, a time base, a line already drawn
    /// sample for sample, or a reader zoomed out far enough that the whole-line
    /// summary is as fine.
    [[nodiscard]] std::optional<PlotWindow> closerFor(const Entry& entry, int buckets) const;
    /// How many axis positions one element of `entry` covers: one under Align,
    /// and the axis divided by the line under Stretch.
    [[nodiscard]] double stretchScale(const Entry& entry) const;
    /// Where the visible range falls in `entry`'s own element indices,
    /// ascending. This is the inverse of the map lineOf() draws with -- point
    /// for point under Align, and spread over the whole axis under Stretch --
    /// and it is why Dataset mode has no closer look: that map is a lookup
    /// table which need not be monotonic and cannot be run backwards.
    [[nodiscard]] bool lineRange(const Entry& entry, double& first, double& last) const;
    /// The finest run of `entry` that covers what is on screen, or -1. An entry
    /// is drawn from one of its runs only while one covers, and from the
    /// whole-line summary -- which covers everything by construction --
    /// otherwise.
    [[nodiscard]] int drawnLevel(const Entry& entry) const;
    [[nodiscard]] bool closerCovers(const Entry& entry) const { return drawnLevel(entry) >= 0; }
    /// Whether some run of `entry` covers `low`..`high` of its own elements at a
    /// bucket no coarser than `bucket`. What the prefetch asks about an octave
    /// it is thinking of reading; see DatasetPlot::served.
    [[nodiscard]] bool served(const Entry& entry, double low, double high,
                              long long bucket) const;
    /// The run to read next for `entry`: the one the pane is waiting for, or --
    /// when the pane is already answered -- the nearest octave out that nothing
    /// in hand covers. Nothing when there is nothing left worth reading.
    [[nodiscard]] std::optional<PlotWindow> closerWanted(const Entry& entry) const;
    /// Drop the runs of `entry` furthest from the one the pane is on, down to
    /// heldLevels().
    void trimLevels(Entry& entry);
    /// How many resolutions each entry may hold at once.
    [[nodiscard]] int heldLevels() const;
    /// How many entries are being drawn from a closer look. Not interesting in
    /// itself: it changes exactly when a different set of values has to reach
    /// the renderer, which is when the surface has to be told to fill again.
    [[nodiscard]] int closerDrawn() const;
    /// Work out what the view wants of each entry, drop what nobody wants, and
    /// arm the read for the rest. Every path that can change the answer ends
    /// here.
    void refreshCloser();
    /// Ask for it. What the settle timer calls.
    void askForCloser();
    /// Forget every closer look, without saying so.
    void clearCloser();
    void touch(int row, const QVector<int>& roles = {});
    /// The extent, the point total and whether anything is drawable, worked
    /// out once per read rather than per binding.
    void recount();
    /// Where point `at` of `entry` sits along the axis, in axis positions.
    [[nodiscard]] double positionOf(const Entry& entry, std::size_t at) const;
    /// Stop whatever was last filled from reading the entries' values.
    ///
    /// The borrow contract, honoured the blunt way -- see
    /// DatasetPlot::releaseDrawing. The renderer draws nothing until it is
    /// filled again, so this is only right where the values are about to stop
    /// being a reading of anything: a row removed, a row retyped, the whole tab
    /// replaced. Everywhere else retire() is what keeps the contract.
    void releaseDrawing();
    /// Empty the renderer and read everything again. The four places where the
    /// line on screen is about to become the wrong line rather than a coarser
    /// one.
    void discard();
    /// Keep values alive that the renderer may still be pointing into, until
    /// fill() hands it their replacement.
    ///
    /// See DatasetPlot::retire, which is the same thing over a map. A
    /// std::vector move takes the buffer with it, so the pointer the item holds
    /// goes on naming the same doubles.
    void retire(std::vector<double>& values);
    /// Say that the lines changed. It does not touch the renderer: whatever it
    /// is drawing stays on the pane until the surface fills it again, which is
    /// a frame later and is a frame of the old picture rather than of none.
    void announce();
    /// Take the pane width the surface last pushed. What the debounce timer
    /// calls; see setPaneColumns.
    void applyColumns();
    /// Buckets a whole line is reduced to: the pane's own width in columns.
    [[nodiscard]] int bucketBudget() const;
    /// ...and buckets a closer look is read into, which is an octave finer
    /// while the tab can afford to hold it. See the definition.
    [[nodiscard]] int closerBuckets() const;
    /// Whether a run `entry` holds already answers `needed` -- it covers the
    /// pane and its bucket is no coarser. Where the prefetch octave is spent.
    [[nodiscard]] bool closerSuffices(const Entry& entry, const PlotWindow& needed) const;
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

    /// What fill() last handed the entries to, so it can be emptied before
    /// they are freed.
    QPointer<PlotItem> drawing_;
    /// Values the renderer may still be reading, kept alive until it is handed
    /// their replacement. See retire(); fill() is what empties this.
    std::vector<std::vector<double>> retired_;

    /// The last range the surface pushed, in the x the axis prints.
    double viewMin_ = 0.0;
    double viewMax_ = 0.0;
    /// Columns the pane has, quantised, until the surface says otherwise.
    int columns_ = kDefaultColumns;
    /// ...and the width the surface last pushed, waiting for the drag to stop.
    int wantedColumns_ = kDefaultColumns;
    /// Whether the surface has ever said how wide the pane is. The first time
    /// it does is not a gesture and does not wait; see setPaneColumns.
    bool measured_ = false;

    H5Requests requests_;
    /// The closer looks in flight, disowned separately from the reads above: a
    /// refresh supersedes a closer look, and a closer look must not supersede a
    /// refresh.
    H5Requests closerRequests_;
    /// Fires once per turn of the event loop however many edits landed in it.
    /// A reader dragging a slider or holding a key down must not put one job
    /// on the thread per keystroke.
    QTimer coalesce_;
    /// Fires once the view has stopped moving, which is when a closer look is
    /// worth reading. A wheel spin or a drag restarts it, so a gesture in
    /// flight reads nothing at all.
    QTimer settle_;
    /// Fires once the pane has stopped changing width. See setPaneColumns.
    QTimer resize_;

public:
    /// Columns assumed until the surface has measured itself, and the most
    /// points an entry is ever reduced to. The plot tab's numbers, for the plot
    /// tab's reasons -- see DatasetPlot::kDefaultColumns and kMaxPoints.
    ///
    /// They have to be the same numbers. A reader who puts /plotting/adc_10M on
    /// the Plot tab and the same slice in a custom tab is looking at one
    /// dataset, and the two pictures of it differing in any way they can see is
    /// a bug in whichever of them they are not looking at.
    static constexpr int kDefaultColumns = 1024;
    static constexpr int kMaxPoints = 16384;
    static constexpr int kColumnQuantum = 64;
    static constexpr int kMinPoints = 256;
    /// How long the view has to hold still before a closer look is read. The
    /// same tenth of a second the plot tab waits -- see
    /// DatasetPlot::kSettleMilliseconds.
    static constexpr int kSettleMilliseconds = 150;
    /// ...and how long the pane has to hold still before it is re-read. The
    /// same fifth of a second the plot tab waits -- see
    /// DatasetPlot::kResizeMilliseconds.
    static constexpr int kResizeMilliseconds = 200;
    /// How far out a run is read before the reader has asked for it, and how
    /// many resolutions are held at once. The plot tab's numbers, for the plot
    /// tab's reasons -- see DatasetPlot::kPrefetchOctaves and kHeldLevels.
    static constexpr int kPrefetchOctaves = 2;
    static constexpr int kHeldLevels = 5;
    /// Doubles held for everything this tab draws. The plot tab's number, for
    /// the plot tab's reason -- see DatasetPlot::kPointBudget.
    static constexpr int kPointBudget = 1 << 21;
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
