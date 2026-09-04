// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "TableLayout.hpp"
#include "h5core/DataSource.hpp"

#include <QAbstractTableModel>
#include <QString>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

namespace gui {

/// Table over one dataset, reading only the block the view is about to paint.
///
/// The table is not the last two dimensions of the dataset: it is whatever the
/// TableLayout says. Every dimension contributes a list of selected indices and
/// sits on one of the two axes, so rows are the lexicographic product of the
/// y-dimensions' selections and columns the product of the x-dimensions'. A
/// cell therefore always names a complete index tuple, which is what rowLabel()
/// and columnLabel() print.
///
/// A dataset can easily exceed RAM, so nothing is cached beyond the block
/// currently on screen. data() serves from that block and slides it when the
/// view scrolls outside it.
class DatasetTableModel : public QAbstractTableModel
{
    Q_OBJECT
    // Anonymous rather than named: QML never constructs one or imports it by
    // name, but DatasetPlot and DatasetImage are handed to QML as typed
    // pointers, and the engine will not pass a type it has never been told
    // about.
    QML_ANONYMOUS

    /// How a float is written in a cell. Only the presentation changes: the
    /// tooltip role keeps handing back the value the file holds, so a rounded
    /// column never puts the exact number out of reach.
    Q_PROPERTY(FloatFormat floatFormat READ floatFormat WRITE setFloatFormat
                   NOTIFY floatFormatChanged)
    /// Digits after the point, for the two formats that have a point. Ignored
    /// by Shortest.
    Q_PROPERTY(int floatDecimals READ floatDecimals WRITE setFloatDecimals
                   NOTIFY floatFormatChanged)
    /// True when the values are floats and the setting above therefore applies.
    Q_PROPERTY(bool floats READ floats NOTIFY datasetChanged)

public:
    /// Roles beyond Qt's own.
    ///
    /// `Number` is the cell as a `double`, which is what the grid puts on a
    /// colour ramp when the cells are filled by their content. It cannot come
    /// off the display role: that string has been rounded to whatever notation
    /// the reader chose, and a fill computed from a rounded number would band
    /// a column that the file says is smooth. Not a number -- text, a struct,
    /// a cell that would not read -- is NaN rather than an absent QVariant, so
    /// the delegate's required property is always set.
    enum Roles {
        Number = Qt::UserRole,
    };
    Q_ENUM(Roles)

    /// Shortest is what H5Scope has always printed: the fewest digits
    /// that read back as the same double, which is exact and ragged. The other
    /// two trade exactness for a column that lines up.
    enum FloatFormat {
        Shortest = 0,
        Fixed = 1,      ///< 'f': 12.340000
        Scientific = 2, ///< 'e': 1.234000e+01
    };
    Q_ENUM(FloatFormat)

    /// A rectangle of the table read as numbers and thinned to something a
    /// screen can hold: what the plot and the image are both made of.
    struct NumericGrid {
        int rows = 0;    ///< after decimation
        int columns = 0; ///< after decimation
        int rowStride = 1;
        int columnStride = 1;
        /// Row-major, size == rows * columns. NaN marks a cell that could not
        /// be read, so one bad element does not discard the block around it.
        std::vector<double> values;
        double minimum = 0.0; ///< over the finite values only
        double maximum = 0.0;
        bool hasFinite = false;
        QString error;

        [[nodiscard]] double at(int row, int column) const;
    };

    explicit DatasetTableModel(QObject* parent = nullptr);

    /// Show `dataset`, or clear when null. Resets the layout to the default
    /// for the new shape, so the model is coherent on its own.
    void setDataset(std::shared_ptr<const h5core::DataSource> dataset);

    /// The dataset currently shown, or nullptr. Used to size the setup panel.
    [[nodiscard]] const h5core::DataSource* dataset() const
    {
        return dataset_.get();
    }

    /// Choose which indices appear and on which axis. Ignored when its rank
    /// does not match the dataset's.
    void setLayout(const TableLayout& layout);
    [[nodiscard]] const TableLayout& layout() const { return axes_.layout(); }
    /// The table the grid is showing, as geometry. A second reading of the same
    /// dataset -- the image with its colour dimension held at one channel --
    /// starts from this and pins what it needs.
    [[nodiscard]] const TableAxes& axes() const { return axes_; }

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation,
                                      int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    /// Index tuples for the headers, with `_` standing for the dimensions that
    /// belong to the other axis: a cell at row "(4,_,_,2)" and column
    /// "(_,1,3,_)" is element (4,1,3,2). A rank-1 dataset prints the bare
    /// index, having nothing to disambiguate; a scalar prints nothing.
    ///
    /// Q_INVOKABLE because the grid's sticky header and index column are
    /// ListViews of their own and cannot reach headerData().
    Q_INVOKABLE [[nodiscard]] QString rowLabel(int row) const;
    Q_INVOKABLE [[nodiscard]] QString columnLabel(int column) const;
    Q_INVOKABLE [[nodiscard]] QString cellLabel(int row, int column) const;

    /// Whether the values can be read as numbers at all -- false for text, a
    /// compound, an enum, and for no dataset.
    [[nodiscard]] bool numeric() const;

    /// One cell taken apart: its compound members, and the whole of it as
    /// JSON. Returns
    ///
    ///     { label, text, json, fields: [{ name, type, value }] }
    ///
    /// and an empty map for a cell that is not there or will not read. Only
    /// this one element is read, so a compound dataset of any size costs the
    /// same to inspect a struct of.
    ///
    /// Q_INVOKABLE because it answers a click rather than a paint: the grid
    /// says which cell, and one pane below it shows what is in that cell.
    Q_INVOKABLE [[nodiscard]] QVariantMap elementAt(int row, int column) const;

    /// The extent of the table's values, as `{ minimum, maximum, valid }`.
    ///
    /// What a colour ramp over the cells is stretched between, so it has to be
    /// the *table's* extent and not the visible block's: a fill that were
    /// recomputed from whatever is on screen would change a cell's colour as
    /// the reader scrolled past it, which is the one thing a colour that means
    /// a value must not do.
    ///
    /// Sampled the way the plot and the image sample -- at most kExtentSamples
    /// along each axis -- and held until the table changes underneath, so
    /// turning the fill on costs one read and scrolling costs none.
    Q_INVOKABLE [[nodiscard]] QVariantMap valueExtent() const;

    [[nodiscard]] FloatFormat floatFormat() const { return floatFormat_; }
    void setFloatFormat(FloatFormat format);
    [[nodiscard]] int floatDecimals() const { return floatDecimals_; }
    void setFloatDecimals(int decimals);
    [[nodiscard]] bool floats() const;

    /// Length of the longest cell in a rectangle of the table, in characters.
    ///
    /// The grid is set in a fixed-pitch face, so a character count is a width
    /// -- which is what lets a column fit itself to its contents without the
    /// view measuring a thousand strings. Only the block already cached is
    /// walked; a rectangle reaching outside it is answered from what has been
    /// read, because a column width is not worth a read of its own.
    Q_INVOKABLE [[nodiscard]] int widestCell(int firstRow, int rows, int firstColumn,
                                             int columns) const;

    /// A thinned numeric rectangle of the table.
    ///
    /// `rowSpan`/`columnSpan` below zero mean "to the end of the axis". The
    /// result is at most `maxRows` x `maxColumns`, reached by taking every
    /// nth entry -- the plot asks for every row it will draw and as many
    /// points as it has pixels, the image for as much of both as it has
    /// pixels.
    ///
    /// Thinning is plain stride sampling and not a min/max envelope: a spike
    /// narrower than one stride is not drawn. That is the honest cost of never
    /// reading more of the file than the screen can show, and the place to
    /// start if the plot ever needs to be exact at a glance.
    [[nodiscard]] NumericGrid sampleValues(int firstRow, int rowSpan, int maxRows,
                                           int firstColumn, int columnSpan,
                                           int maxColumns) const;

    /// The same read against a table other than the one on screen. The image
    /// presentation uses it to take one colour channel at a time out of a
    /// dimension the grid has spread along an axis; `axes` must describe the
    /// same dataset, which is what starting from axes() and pinning guarantees.
    [[nodiscard]] NumericGrid sampleValues(const TableAxes& axes, int firstRow,
                                           int rowSpan, int maxRows, int firstColumn,
                                           int columnSpan, int maxColumns) const;

    /// Last read error, empty when the dataset reads cleanly.
    [[nodiscard]] const QString& errorText() const { return errorText_; }

signals:
    /// A different dataset is being shown. Narrower than modelReset, which
    /// setLayout emits too: rearranging the table is not a new selection, and
    /// a view that resets its own state on one must not reset it on the other.
    void datasetChanged();
    void floatFormatChanged();

private:
    /// `text` written the way floatFormat says, or unchanged when the values
    /// are not floats or the format is Shortest.
    [[nodiscard]] QString formatted(const QString& text) const;

    /// Rebuild the table's geometry from `layout`, which the caller has already
    /// checked against the dataset.
    void rebuild(TableLayout layout);
    /// Ensure the cached block covers (row, column).
    void ensureBlock(int row, int column) const;
    [[nodiscard]] QString labelFor(int row, int column, bool showX, bool showY) const;

    /// What is being drawn: the file's own dataset, or the result of a
    /// postprocessing pipeline over it. Nothing below this line knows or
    /// needs to know which, because a DataSource answers the same five
    /// questions either way.
    std::shared_ptr<const h5core::DataSource> dataset_;
    TableAxes axes_;

    /// One rectangle of the *table*, not of the dataset: with a scattered
    /// selection the two are no longer the same shape.
    struct Block {
        int rowOrigin = 0;
        int columnOrigin = 0;
        int rows = 0;
        int columns = 0;
        std::vector<QString> cells; ///< row-major, size == rows * columns
        bool valid = false;
    };

    // Mutable: data() is const by Qt's contract but must be able to slide the
    // cached block. Nothing observable outside the model changes.
    mutable Block block_;
    mutable QString errorText_;

    /// What valueExtent() answers with, sampled once per table.
    struct Extent {
        double minimum = 0.0;
        double maximum = 0.0;
        bool valid = false;
    };
    mutable std::optional<Extent> extent_;

    FloatFormat floatFormat_ = Shortest;
    int floatDecimals_ = kDefaultFloatDecimals;

    static constexpr int kBlockRows = 64;
    static constexpr int kBlockColumns = 64;
    /// Cells per axis behind valueExtent(). The same trade the image makes at
    /// 1024: enough of the table that the extremes are the table's, few enough
    /// that asking is one read rather than a walk of a dataset larger than
    /// RAM. Stride sampling can miss a lone spike, and a fill clamps rather
    /// than misreports when it does.
    /// Most values sampleValues() will pull in one read. It walks the columns
    /// it thins away rather than seeking past them -- contiguous is cheap,
    /// seeking is not -- and this is the ceiling on what that buffers.
    static constexpr int kReadRun = 1 << 16;

public:
    /// Six digits is printf's own default and about what a double is worth
    /// reading at a glance; seventeen is what it takes to write any double
    /// back exactly, which is the ceiling worth offering.
    static constexpr int kDefaultFloatDecimals = 6;
    static constexpr int kMaxFloatDecimals = 17;
    static constexpr int kExtentSamples = 256;
};

} // namespace gui
