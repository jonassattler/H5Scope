// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "TableLayout.hpp"

#include <QAbstractListModel>
#include <QString>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

#include <vector>

namespace gui {

/// One row per dimension of the selected dataset: how that dimension is
/// subset, and which axis of the table it lands on.
///
/// This is the state behind the Data Viewer's "table setup" panel, kept in C++
/// rather than in QML so the expression parsing, the axis invariant and the
/// resolved index lists are all testable without a QML engine.
class TableSetupModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Obtained from AppController.tableSetupModel")

public:
    /// Mirrors AxisMode so QML can name the modes. Kept in lockstep by the
    /// static_asserts in the .cpp.
    enum Mode {
        All = 0,
        Index = 1,
        Range = 2,
        Custom = 3,
    };
    Q_ENUM(Mode)

    enum Roles {
        ExtentRole = Qt::UserRole + 1,
        ModeRole,
        IndexValueRole,
        RangeFirstRole,  ///< inclusive
        RangeLastRole,   ///< inclusive
        ExpressionRole,
        ExpressionErrorRole, ///< empty when the expression parsed
        OnXRole,
        SelectedCountRole,
        SummaryRole, ///< this dimension as it appears in the slice line
    };
    Q_ENUM(Roles)

    explicit TableSetupModel(QObject* parent = nullptr);

    /// Rebuild for a dataset of `shape`, applying the defaults: every
    /// dimension All, the last dimension on x and the rest on y -- except at
    /// rank 1, where the single dimension stays on y so a vector keeps
    /// reading as one column rather than one very long row, and except on a
    /// dataset that declares itself an image, where `image` says which
    /// dimension is the height, which the width, and which the colour
    /// components to pin to one index. See defaultAxes.
    void setShape(const std::vector<hsize_t>& shape,
                  const std::optional<h5core::ImageInfo>& image = {});

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    /// The resolved selection, ready for DatasetTableModel::setLayout.
    [[nodiscard]] TableLayout layout() const;
    /// Each dimension as it appears between the brackets of the slice line.
    [[nodiscard]] QStringList summaries() const;
    /// The whole of what stands between those brackets: `summaries()` joined
    /// by the comma the slice line separates subscripts with. This is the one
    /// string the slice bar lets the reader edit.
    [[nodiscard]] QString sliceText() const;

    /// Read a slice body back in -- one subscript per dimension, written the
    /// way `sliceText()` writes it -- and set every dimension to what it
    /// names.
    ///
    /// Returns the reason it could not be read, or an empty string once it has
    /// been applied. Nothing changes unless every subscript reads: a slice is
    /// one statement about the whole dataset, and half of one applied to half
    /// the dimensions is a selection the reader never asked for.
    ///
    /// The shorthands are Python's, because the subscripts are:
    ///
    ///   * **Trailing dimensions may be left out.** On a rank-4 dataset `0` is
    ///     `0, :, :, :`, exactly as it is in numpy.
    ///   * **`...` stands for the dimensions nobody wrote.** `..., 0` is
    ///     `:, :, :, 0`. One of them per line, since two would not say how many
    ///     each stood for.
    ///
    /// Each dimension takes the mode that matches how its subscript was
    /// *written* rather than what it resolved to -- All for `:`, Index for a
    /// bare number, Range for a run, Custom for anything else. That is the
    /// difference between `1` and `1:2`, which select the same element and are
    /// still not the same subscript; reading the mode off the resolved indices
    /// made them indistinguishable, so a reader who typed `1:2` watched the
    /// line rewrite itself as `1`.
    ///
    /// Which axis a dimension sits on is not part of a slice, and is left
    /// alone.
    QString applySlice(const QString& text);
    /// Why `text` cannot be read as a slice of this dataset, or an empty
    /// string when it can. Changes nothing: the slice bar checks every
    /// keystroke and applies only what the reader commits, so that a line
    /// halfway to being typed reports itself without rebuilding the table.
    [[nodiscard]] QString sliceError(const QString& text) const;

    Q_INVOKABLE void setMode(int dimension, int mode);
    Q_INVOKABLE void setIndex(int dimension, int value);
    Q_INVOKABLE void setRange(int dimension, int first, int last);
    Q_INVOKABLE void setExpression(int dimension, const QString& text);
    /// Put `dimension` on the column axis (`onX`) or the row axis. There is no
    /// way to say "both" or "neither": the two checkboxes in the panel are
    /// bound to this one flag.
    Q_INVOKABLE void setAxis(int dimension, bool onX);

signals:
    /// The effective selection changed. Deliberately not `layoutChanged`,
    /// which QAbstractItemModel already owns and means something else.
    void tableLayoutChanged();

private:
    struct Dimension {
        hsize_t extent = 0;
        AxisMode mode = AxisMode::All;
        hsize_t index = 0;
        hsize_t first = 0; ///< inclusive
        hsize_t last = 0;  ///< inclusive
        QString expression;
        QString expressionError;
        /// The last selection the expression box parsed to. Held so a
        /// half-typed expression highlights the box instead of blanking the
        /// grid underneath it.
        std::vector<hsize_t> custom;
        /// ...and the text that produced it. The slice line prints this, so a
        /// stride or a descent comes back written the way it was asked for
        /// rather than expanded into the indices it stands for -- and so that
        /// a box halfway through being retyped does not put a line the reader
        /// never wrote onto the bar above.
        QString customText;
        bool onX = false;
    };

    [[nodiscard]] bool valid(int dimension) const;
    /// This dimension as it appears between the brackets of the slice line:
    /// the subscript that was written, not the indices it resolved to.
    [[nodiscard]] QString summaryFor(const Dimension& dimension) const;
    /// Read a slice body into one parsed subscript per dimension, and the text
    /// each of them was written as -- which is what a Custom selection keeps,
    /// so the line prints back what was typed. False and a filled `error` when
    /// any part of it does not name a selection of this dataset.
    [[nodiscard]] bool readSlice(const QString& text,
                                 std::vector<IndexExpression>& chosen,
                                 QStringList& written, QString& error) const;
    [[nodiscard]] std::vector<hsize_t> indicesFor(const Dimension& dimension) const;
    /// Announce a change to one row, and the layout change behind it.
    void touch(int dimension, const std::vector<int>& roles);

    std::vector<Dimension> dimensions_;
};

} // namespace gui
