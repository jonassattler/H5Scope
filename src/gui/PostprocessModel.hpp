// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "postproc/Operations.hpp"
#include "postproc/Pipeline.hpp"

#include <QAbstractListModel>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <optional>
#include <vector>

namespace gui {

/// The rows of the postprocessing panel, and the pipeline behind them.
///
/// One row per line of the diagram in `postprocessing.md`: the input array, the
/// slice, an operation per added step, and the output. Only the middle ones are
/// steps the reader owns -- the first two and the last are the ends of the
/// chain and cannot be removed, reordered or dragged past.
///
/// The slice row is not a copy of the slice above the table; it *is* it. The
/// model reads `TableSetupModel::sliceText()` for what to show and calls
/// `applySlice()` when the row is edited, so the panel, the bar and the data
/// settings can never disagree about which elements are being read. That is why
/// this holds a pointer to the setup model rather than a slice of its own.
class PostprocessModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Obtained from AppController.postprocessModel")

    /// Nothing happens at all unless this is set: the views read the file the
    /// way they always did, and every row below is drawn greyed. It is the one
    /// switch the whole feature hangs off.
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)

    /// Which row the pipeline is computed up to. Clicking a row sets this, and
    /// everything after it greys out and is not run. Counted in rows, so it
    /// indexes this model directly.
    Q_PROPERTY(int activeRow READ activeRow WRITE setActiveRow NOTIFY changed)

    /// The steps as plain data, for DatasetMemory to write down per dataset and
    /// put back. A list of {operation, argument} maps; the slice is not in it,
    /// because the slice is remembered with the dataset already.
    Q_PROPERTY(QVariantList steps READ steps WRITE setSteps NOTIFY changed)

    /// The operations the Add dropdown offers, as {name, argumentLabel}.
    Q_PROPERTY(QVariantList operations READ operations CONSTANT)

    /// Which of them that dropdown is showing.
    ///
    /// Kept here rather than in the row that draws it because adding a step
    /// rebuilds the list that row lives in, and a box that jumped back to the
    /// first operation every time one was added is a box nobody can add two of
    /// the same from.
    Q_PROPERTY(int chosenOperation READ chosenOperation WRITE setChosenOperation
                   NOTIFY changed)

    /// Why the pipeline stopped, or empty. The bar prints this beside the
    /// slice, the same way it prints a slice that will not read.
    Q_PROPERTY(QString error READ error NOTIFY changed)

    /// Whether a pipeline is actually being applied: enabled, with a dataset
    /// under it that can be postprocessed. The orange label in the bar and the
    /// suppression of the image defaults both hang off this rather than off
    /// `enabled`, because a switch flipped over a dataset of strings changes
    /// nothing and should not claim to.
    Q_PROPERTY(bool active READ active NOTIFY changed)

public:
    /// What a row is. Everything that is not an Operation is furniture: it
    /// carries no argument, cannot be removed and cannot be moved.
    enum Kind {
        Input = 0,  ///< the dataset, before anything
        Slice = 1,  ///< the slice above the table, mirrored
        Operation = 2,
        Adder = 3,  ///< the row that puts another operation in the chain
        Output = 4, ///< the result the views draw
    };
    Q_ENUM(Kind)

    enum Roles {
        KindRole = Qt::UserRole + 1,
        LabelRole,        ///< the path, the slice, the operation's name
        ArgumentRole,     ///< what was typed beside it
        ArgumentLabelRole,///< "axes", "axis", "shape"; empty when it takes none
        PlaceholderRole,
        ShapeRole,        ///< after this row's operation, as "2 × 3 × 4"
        ErrorRole,        ///< why this row could not run
        RemovableRole,
        MovableRole,
        ComputedRole,     ///< false for a row after the active one
    };
    Q_ENUM(Roles)

    explicit PostprocessModel(QObject* parent = nullptr);

    /// The slice this pipeline's second row mirrors. Set once, at construction
    /// time, by the controller that owns both.
    void setSliceSource(class TableSetupModel* slice);

    /// The dataset the pipeline runs on. `path` names it in the input row and
    /// `shape` is where the shape column starts; `numeric` is false for the
    /// datatypes there is no arithmetic for, which greys the panel with a
    /// reason rather than offering operations that cannot run.
    void setDataset(const QString& path, const std::vector<hsize_t>& shape,
                    bool numeric);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] bool enabled() const { return enabled_; }
    void setEnabled(bool enabled);
    [[nodiscard]] int activeRow() const;
    void setActiveRow(int row);
    [[nodiscard]] QVariantList steps() const;
    void setSteps(const QVariantList& steps);
    [[nodiscard]] QVariantList operations() const;
    [[nodiscard]] int chosenOperation() const { return chosenOperation_; }
    void setChosenOperation(int index);
    [[nodiscard]] QString error() const { return trace_.error; }
    [[nodiscard]] bool active() const;

    /// Add an operation at the end, just above the add row.
    Q_INVOKABLE void addStep(const QString& name);
    /// Add whichever one the dropdown is showing. What the add button does.
    Q_INVOKABLE void addChosenStep();
    /// Take one out. Rows that are not operations are refused.
    Q_INVOKABLE void removeStep(int row);
    /// Drag one to another position. Both are clamped into the operations, so
    /// nothing lands above the slice or below the output.
    Q_INVOKABLE void moveStep(int from, int to);
    /// Type into a row's argument box. The slice row writes through to the
    /// slice above the table; every other row keeps its own text.
    Q_INVOKABLE void setArgument(int row, const QString& argument);
    /// Whether an argument would read, without applying it -- what the box
    /// checks on every keystroke, exactly as the slice line does.
    [[nodiscard]] Q_INVOKABLE QString argumentError(int row,
                                                    const QString& argument) const;

    /// The shape the views are drawing, which is the output row's.
    [[nodiscard]] std::vector<hsize_t> outputShape() const { return trace_.output; }
    /// The pipeline as postproc understands it, slice first.
    [[nodiscard]] std::vector<postproc::Step> pipeline() const;
    /// How many of those to run: the active row, counted in steps.
    [[nodiscard]] std::size_t upTo() const;

    /// Forget the steps and the switch. The controller calls this when the
    /// file closes, for the same reason it empties the settings store.
    void reset();

signals:
    /// Anything that changes what the views should be drawing. One signal
    /// rather than one per property: every one of them means "run it again".
    void changed();

public slots:
    /// The slice above the table moved. The second row is that slice, so this
    /// re-reads the shapes from it.
    void sliceChanged();

private:
    /// Re-walk the shapes and tell everyone. Cheap -- it reads no elements --
    /// so it runs on every keystroke that commits.
    void refresh();
    [[nodiscard]] int stepRow(int index) const { return index + 2; }
    [[nodiscard]] int stepIndex(int row) const { return row - 2; }
    [[nodiscard]] bool isStep(int row) const;
    /// The row holding the add control, which is the one above the output.
    [[nodiscard]] int adderRow() const { return rowCount() - 2; }
    [[nodiscard]] QString sliceText() const;

    TableSetupModel* slice_ = nullptr;
    QString path_;
    std::vector<hsize_t> shape_;
    bool numeric_ = false;
    bool enabled_ = false;
    std::vector<postproc::Step> steps_;
    /// The row the pipeline runs to, counted in steps: 1 is the slice alone.
    /// Clamped up to the full pipeline whenever a step is added.
    std::size_t upTo_ = 0;
    int chosenOperation_ = 0;
    postproc::Trace trace_;
};

} // namespace gui
