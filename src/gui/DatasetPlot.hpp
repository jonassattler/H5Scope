// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "DatasetTableModel.hpp"
#include "H5Thread.hpp"
#include "PlotItem.hpp"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <cstddef>
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
/// The values themselves never reach QML. fill() hands the renderer a pointer
/// into the cache this object already holds, together with the arithmetic that
/// puts a sample at an x, and the renderer projects straight from the doubles.
/// The boundary this replaced built a QList<QPointF> per line -- sixteen bytes
/// a point, allocated and copied on every refill -- and it was the refill, not
/// the read, that made recolouring a ten-thousand-line selection slow.
///
/// **Two summaries of a line, not one.** The first is the whole of it, thinned
/// to `cap_` points however long it is; that is what the axes are drawn against
/// and it is never thrown away while the line is drawn. The second is a closer
/// look -- an aligned run of the line, summarised at a finer bucket -- read
/// when the reader has zoomed in far enough for the first to be a stretch of
/// itself rather than a reading of the data. See setVisibleRange() for when,
/// and gui::PlotWindow for where.
///
/// Keeping both is what makes the closer look free of side effects. The extent
/// stays the line's true extent, so the y axis does not rescale under the
/// reader as detail arrives; zooming back out draws immediately from the
/// summary already in hand; and there is always something correct on screen
/// while a read is in flight, so no gesture ever waits and no frame is blank.
class DatasetPlot : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Obtained from AppController.datasetPlot")

    /// True: one line per table row, x running along the columns. False: the
    /// transpose. See the note on setSeriesFromRows for why this is a setting
    /// rather than a constant.
    Q_PROPERTY(bool seriesFromRows READ seriesFromRows WRITE setSeriesFromRows NOTIFY changed)
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

    /// The same line written as a slice of the file: `/cube[1, 2, :]`.
    ///
    /// What the legend's "add to a custom plot" puts into the entry it makes.
    /// Empty when the line is not a slice of one dimension, which is when the
    /// table has spread two dimensions along the axis the lines run down; the
    /// menu offers nothing rather than something close.
    Q_INVOKABLE [[nodiscard]] QString seriesExpression(int series) const;

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

    /// What the reader is looking at, in the x the axis prints.
    ///
    /// The surface pushes this whenever the window onto the data moves. It is
    /// not a setting and nothing is drawn from it: it is what decides whether
    /// the lines are worth reading again at a finer bucket, and the answer is
    /// usually no. A range that resolves to the closer look already held costs
    /// nothing at all -- not a read, not a signal, not even a timer -- which is
    /// what keeps panning inside it free.
    ///
    /// When it does resolve to something else, the read waits
    /// kSettleMilliseconds for the gesture to stop and then goes out
    /// asynchronously. A drag never reads; a drag that ends does, once.
    Q_INVOKABLE void setVisibleRange(double xMin, double xMax);

    /// How wide the pane the lines are drawn in is, in device-independent
    /// pixels.
    ///
    /// What a line is thinned to used to be a constant -- kMaxPoints, two
    /// thousand points whatever was drawing them -- and a constant is wrong in
    /// both directions. On a wide pane it is fewer buckets than there are pixel
    /// columns, so an envelope draws as a hatch of separated teeth instead of a
    /// band; on a narrow one it is more points than can be told apart, read and
    /// held for nothing. A bucket is a column: that is the whole of the rule,
    /// and it is the same rule every plot that summarises server-side follows.
    ///
    /// Quantised, because a resize would otherwise re-read the lines on every
    /// pixel of the drag. See kColumnQuantum.
    ///
    /// And debounced on top of the quantum, because the quantum is not enough:
    /// a window dragged from narrow to wide crosses a dozen of them, and each
    /// crossing was a re-read of every drawn line while the reader was still
    /// holding the mouse down. Nothing happens here but a note of what was
    /// asked for and a timer -- see kResizeMilliseconds -- so a drag costs one
    /// read at the end of it rather than one per sixty-four pixels.
    Q_INVOKABLE void setPaneColumns(int columns);

    /// Hand every drawn line to `target` at once.
    ///
    /// One crossing rather than one per line, and no points built on the way:
    /// the item is given a pointer into the cache and the x arithmetic, and it
    /// projects straight from the doubles this object already holds. What that
    /// replaced built a QList<QPointF> per line -- sixteen bytes a point,
    /// allocated and copied on every refill, and a refill is what recolouring
    /// cost.
    ///
    /// **The values are borrowed.** See PlotLine. This object clears whatever
    /// it last filled before anything can free or prune those vectors, which
    /// is why there are exactly two places in the .cpp that destroy a line and
    /// both of them release first.
    Q_INVOKABLE void fill(gui::PlotItem* target);

    /// Line `series` of the table as a renderer would be given it, drawn or
    /// not, and the axis it is drawn against.
    ///
    /// The seam the suites assert the x arithmetic through, and the only
    /// reason it is public: gui::samplesOf() over these two is what fill()
    /// used to hand a renderer, so a test can read the points without a window,
    /// an engine or a scene graph.
    [[nodiscard]] PlotLine lineOf(int series) const;
    [[nodiscard]] PlotAxis drawingAxis() const;

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
    /// The rectangle of the table one line is: a row of it, or a column when
    /// the lines run the other way.
    [[nodiscard]] DatasetTableModel::SampleRequest requestFor(int series) const;
    /// Read every drawn line that is not already held, in one batch. Asking
    /// per line is a blocking round trip per line, which is what `all` on a
    /// table of thousands used to cost.
    void readMissing() const;
    /// Put the drawn set back to where a new table starts it: its first
    /// kMaxInitialSeries lines.
    void reseed();
    /// Points per line, given how many lines there are and how wide the pane
    /// is.
    ///
    /// Two things bound it. The pane, because a bucket is a column and there is
    /// nothing to be gained from holding more of them than the reader can see
    /// -- nor anything to be gained from holding fewer, which is what a
    /// constant did on a wide screen. And a shared budget, because `all` on a
    /// table of ten thousand at a couple of thousand points each is a hundred
    /// and sixty megabytes held and twenty million doubles scanned on every
    /// frame of a drag, to draw lines that cannot each have a thousand pixels
    /// of a thousand-pixel pane.
    [[nodiscard]] int pointsFor(int lines) const;
    /// Take `cap` as the budget, retiring what was read at the old one. Both
    /// the number of lines and the width of the pane can change it.
    void applyCap(int cap);
    /// Take the pane width the surface last pushed. What the debounce timer
    /// calls; see setPaneColumns.
    void applyColumns();
    /// Stop whatever was last filled from reading `lines_` or `windowLines_`.
    ///
    /// The borrow contract, honoured the blunt way: the renderer is emptied and
    /// draws nothing until it is filled again. That is right when the values
    /// are about to stop being a reading of anything -- invalidate(), which is
    /// a new dataset -- and wrong everywhere else, because "draws nothing" is a
    /// blank pane for however long the replacement takes to arrive. See
    /// retire() for what the rest of them do instead.
    void releaseDrawing() const;

    /// Keep a cache alive that the renderer may still be pointing into.
    ///
    /// The other half of the borrow contract, and the half that keeps a picture
    /// on screen. Nothing may free a vector the item holds a pointer to; the
    /// two honest ways to obey that are to empty the item first, which blanks
    /// the pane, or to let the old values outlive the change. This is the
    /// second. The item goes on drawing what it was given until it is handed
    /// the replacement, and fill() -- the first moment nothing is reading the
    /// old values -- is where they are finally let go.
    ///
    /// It costs nothing to move: std::map relinks nodes, so every mapped vector
    /// stays exactly where it was and every pointer the item holds stays good.
    /// Which is the same property that lets everything else in here insert into
    /// these maps while the renderer is reading them.
    void retire(std::map<int, std::vector<double>>& cache) const;
    /// One line of one, for the path that replaces a single line of a run the
    /// reader is already looking at: a line ticked back on in the legend.
    void retire(std::vector<double>& values) const;

    // --- the closer look ---------------------------------------------------
    /// One resolution of it: an aligned run of the line summarised at one
    /// bucket size, for every line drawn.
    ///
    /// Several of these are held at once, one per octave, which is what makes
    /// zooming a draw rather than a wait in *both* directions. See
    /// kPrefetchOctaves for why the two directions need different things.
    struct Detail
    {
        PlotWindow window;
        /// Table positions between one drawn point of this run and the next,
        /// and how many of them there are. Half a bucket, for the reason
        /// PlotLine::positionStep gives.
        double step = 1.0;
        int points = 0;
        /// One entry per drawn line, keyed as `lines_` is. Borrowed by the
        /// renderer on exactly the same terms.
        std::map<int, std::vector<double>> lines;
    };

    /// The window the view asks for, or nothing when the whole-line summary is
    /// already as good: zoomed out, too many lines drawn to be worth
    /// re-reading, or an axis with no step to divide a position out of.
    [[nodiscard]] std::optional<PlotWindow> detailFor(int buckets) const;
    /// Buckets the pane itself needs, which is a bucket per column.
    [[nodiscard]] int paneBuckets() const;
    /// ...and buckets the run the reader is *on* is read into: an octave finer
    /// than the pane needs, while the drawn set can afford to hold it. See the
    /// definition -- that octave is free in reads and is what makes the next
    /// step in a draw rather than a wait.
    [[nodiscard]] int detailBuckets() const;
    /// How many resolutions may be held at once, given how many lines are
    /// sharing the budget.
    [[nodiscard]] int heldLevels() const;
    /// Where the visible range falls in table positions, ascending. False when
    /// there is no sane answer -- no axis, no data, nothing pushed yet.
    [[nodiscard]] bool visiblePositions(double& first, double& last) const;
    /// The visible range clamped to the data, which is what a run has to cover:
    /// a pane showing the end of a line shows some empty axis past it.
    [[nodiscard]] bool drawnPositions(double& first, double& last) const;
    /// The finest run in hand that covers what is on screen and holds every
    /// drawn line, or -1. What lineOf() draws; the whole-line summary covers
    /// everything by construction and is what is drawn when this finds nothing,
    /// which is why zooming past the runs held is a correct picture immediately
    /// rather than half a line for a moment.
    [[nodiscard]] int drawnLevel() const;
    [[nodiscard]] bool detailCovers() const { return drawnLevel() >= 0; }
    /// The window drawnLevel() names, for the one caller that has to notice it
    /// changing.
    [[nodiscard]] std::optional<PlotWindow> drawnWindow() const;
    /// Where `window` is held, or -1.
    [[nodiscard]] int levelAt(const PlotWindow& window) const;
    /// Whether some run in hand already answers a pane that needs `needed` --
    /// it covers what is on screen at a bucket no coarser. This is where the
    /// octaves are spent: a step in lands on the finer bucket the run it came
    /// from was read at, and a step out lands on a run read ahead of it.
    [[nodiscard]] bool levelServes(const PlotWindow& needed) const;
    /// Whether some run in hand covers `low`..`high` at a bucket no coarser
    /// than `bucket`, holding every drawn line. The question the prefetch asks
    /// about an octave it is thinking of reading: a run the reader zoomed *in*
    /// from is finer than the octave out of where they are now and covers more
    /// of the line, so it answers for that octave and reading it again would
    /// be a round trip spent on nothing.
    [[nodiscard]] bool served(double low, double high, long long bucket) const;
    /// The run to read next: the one the pane is waiting for, or -- when the
    /// pane is already answered -- the nearest octave out that is not held yet.
    /// Nothing when there is nothing left worth reading.
    [[nodiscard]] std::optional<PlotWindow> detailWanted() const;
    /// Work out what to read next and arm it, or drop what is held when the
    /// view wants nothing. Every path that can change the answer ends here: a
    /// new range, a new selection, a moved axis, an answer landing.
    void refreshDetail();
    /// Ask for it. What the settle timer calls, and the one place a read is
    /// submitted rather than waited for.
    void askForDetail();
    /// Install an answer, if it is still the answer that was wanted.
    void takeDetail(const PlotWindow& detail, const std::vector<int>& series,
                    std::vector<DatasetTableModel::NumericGrid> grids);
    /// Drop the runs furthest from the one the pane is on, down to
    /// heldLevels().
    void trimLevels();
    /// Forget every run without saying so. dropDetail() is the same thing plus
    /// the signal, which invalidate() does not want because it emits its own.
    void clearDetail();
    void dropDetail();
    /// The rectangle one line of a run is.
    [[nodiscard]] DatasetTableModel::SampleRequest detailRequestFor(int series,
                                                                    const PlotWindow& detail) const;

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
    /// Table positions between one drawn point and the next. A double because
    /// an envelope puts two points in each bucket, so they sit half a bucket
    /// apart and half of an odd bucket is not a whole number of elements.
    mutable double step_ = 1.0;
    /// Points per line for what is currently held. Recomputed when the size of
    /// the drawn set changes in bulk -- see selectFirst -- and not when a
    /// single line is ticked, so the legend stays cheap.
    int cap_ = 2 * kDefaultColumns;
    /// Columns the pane has, quantised. Until the surface says otherwise this
    /// is kDefaultColumns, which is about the plot area of a window as it first
    /// opens -- so a plot nobody has measured is drawn at the resolution one
    /// would have.
    int columns_ = kDefaultColumns;
    mutable double minimum_ = 0.0;
    mutable double maximum_ = 0.0;
    mutable bool hasFinite_ = false;
    mutable QString error_;
    mutable bool sampled_ = false;
    /// What fill() last handed the lines to, so that it can be emptied before
    /// they are freed. A QPointer because the item belongs to a QML scene that
    /// is torn down and rebuilt without telling this object.
    mutable QPointer<PlotItem> drawing_;
    /// Caches the renderer may still be reading, kept alive until it is handed
    /// their replacement. See retire(); fill() is what empties this.
    mutable std::vector<std::map<int, std::vector<double>>> retired_;
    /// The pane width the surface last pushed, waiting for the drag to stop.
    int wantedColumns_ = kDefaultColumns;
    /// Whether the surface has ever said how wide the pane is. The first time
    /// it does is not a gesture and does not wait; see setPaneColumns.
    bool measured_ = false;
    /// Fires once the pane has stopped changing width. See setPaneColumns.
    QTimer resize_;

    // --- the closer look ---------------------------------------------------
    /// The last range the surface pushed, in the x the axis prints. Kept rather
    /// than resolved on the spot because everything that changes how a position
    /// becomes an x -- the axis moving, the selection changing the budget --
    /// has to work out the window again from the same view.
    double viewMin_ = 0.0;
    double viewMax_ = 0.0;
    /// The runs in hand, one per resolution, in no particular order. Mutable
    /// because ensure() is const by Qt's contract and prunes every cache down
    /// to the drawn set.
    mutable std::vector<Detail> levels_;
    /// What is to be read next, and what is in flight. Two rather than one: a
    /// reply that is no longer wanted is dropped by its ticket, and a run
    /// already being read is not asked for twice.
    std::optional<PlotWindow> wanted_;
    std::optional<PlotWindow> asked_;
    /// Fires once the view has stopped moving. A wheel spin or a drag restarts
    /// it, so a gesture in flight reads nothing at all.
    QTimer settle_;
    /// The reads this object has out. Reset whenever the answer being waited on
    /// stops being the answer that is wanted, which is what stops a window the
    /// reader has already left from being painted.
    H5Requests requests_;

public:
    /// Columns assumed until the surface has measured itself. Two of these --
    /// a bucket answers with a low and a high -- is the two thousand points a
    /// line used to be thinned to unconditionally.
    static constexpr int kDefaultColumns = 1024;
    /// The most points a line is ever thinned to, however wide the pane.
    ///
    /// Eight thousand buckets is a pane eight thousand *device* pixels across
    /// -- see PlotSurface.pushColumns, which measures in those rather than in
    /// logical ones -- so it covers a maximised window on a 5K display and a
    /// 4K one at double scaling. Past that there is nothing left to resolve
    /// for: a bucket would be narrower than a pixel.
    static constexpr int kMaxPoints = 16384;
    /// ...and the fewest, however many lines are sharing the budget. Below
    /// this a line stops being a shape and starts being a sketch of one, and
    /// nothing is saved that was worth the difference.
    static constexpr int kMinPoints = 256;
    /// Doubles held for the drawn set, all lines together. Two million of
    /// them, which is sixteen megabytes -- and, far more to the point, two
    /// million the projection has to walk on every frame of a drag.
    static constexpr int kPointBudget = 1 << 21;
    /// Lines a new selection opens on. A ceiling on what the reader is shown
    /// before they have asked for anything, not on what they may ask for:
    /// the legend ticks any line in the table and `select all` takes them all.
    static constexpr int kMaxInitialSeries = 64;
    /// Lines read per crossing of the HDF5 thread. Large enough that the round
    /// trips stop being what the reader waits for -- the default set of 64 is
    /// one crossing, and `all` on a table of thousands is dozens rather than
    /// thousands -- and small enough that the answers in hand never amount to
    /// a second copy of everything already drawn.
    static constexpr std::size_t kReadBatch = 256;
    /// How long the view has to hold still before a closer look is read.
    ///
    /// Long enough that a wheel spun through six octaves reads once rather than
    /// six times, and short enough that it lands before a reader who has
    /// stopped to look at something has finished looking at it. The picture
    /// does not wait for it: what is already on screen keeps being drawn.
    static constexpr int kSettleMilliseconds = 150;
    /// How far out a run is read before the reader has asked for it.
    ///
    /// The two directions of a zoom are not the same shape, and this is the one
    /// that needs a read. Going *in* is free: a run costs its span, so the run
    /// the reader is on is read an octave finer than the pane needs -- see
    /// detailBuckets() -- and the step down lands on detail that came with it.
    /// Going *out* cannot be free that way, because the next view is wider than
    /// the run in hand and no amount of resolution inside it helps; what is
    /// needed is another run, twice as wide at twice the bucket. That one costs
    /// twice the elements, which is why it is read while the reader is looking
    /// rather than while they are waiting.
    ///
    /// Two of them, so a double tap's worth of zooming out is already drawn.
    /// Each costs one level of memory and its own span in reads; past two, the
    /// spans double again and the whole-line summary is close enough to be the
    /// honest answer.
    static constexpr int kPrefetchOctaves = 2;
    /// Runs held at once, at most.
    ///
    /// The run the pane is on, the two read ahead of it, and two the reader has
    /// already been through -- because a run is not thrown away when the view
    /// leaves it, so zooming back along the way you came costs nothing at all.
    /// Bounded by the budget as well: see heldLevels().
    static constexpr int kHeldLevels = 5;
    /// How long the pane has to hold still before it is re-thinned.
    ///
    /// Longer than kSettleMilliseconds, because a window resize is a slower
    /// gesture than a wheel spin and the cost at the end of it is higher: a
    /// zoom re-reads one run of each line, a resize re-reads all of every one.
    /// What the reader sees while they drag is the line thinned for the pane
    /// they started from, stretched -- which is the right answer, because the
    /// pane they are dragging towards is not one they have chosen yet.
    static constexpr int kResizeMilliseconds = 200;
    /// Lines past which no closer look is read at all.
    ///
    /// One crossing's worth, which is kReadBatch. Past a few hundred strokes
    /// over one another the picture is a distribution rather than a line, and
    /// re-reading every one of them each time the reader zooms would spend the
    /// whole cost of the selection again to sharpen something nobody can
    /// follow. They stretch, as they always did.
    static constexpr int kWindowedSeries = static_cast<int>(kReadBatch);
    /// What the pane's width is rounded down to.
    ///
    /// Down rather than up, and this is not arbitrary: the renderer summarises
    /// again if it is handed more than two points per column, and it does so in
    /// powers of two -- so a cap a hair above twice the pane costs *half* the
    /// horizontal resolution rather than none of it. Staying under the pane
    /// keeps the two in step, and a resize then re-reads once every sixty-four
    /// pixels rather than once a pixel.
    static constexpr int kColumnQuantum = 64;
};

} // namespace gui
