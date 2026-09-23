// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "h5core/Types.hpp"
#include "postproc/Operations.hpp"
#include "postproc/Pipeline.hpp"
#include "postproc/Script.hpp"

#include <QAbstractListModel>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <optional>
#include <vector>

namespace gui {

class AppController;

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

    /// The whole chain as text, one step to a line: the path, the member on a
    /// compound, the slice, then every operation. What the panel shows in
    /// place of its rows when visual editing is off, and the same pipeline --
    /// see postproc::Script, which is why the two can be mirrored rather than
    /// kept in step. Empty with no dataset.
    Q_PROPERTY(QString script READ script NOTIFY changed)

    /// The shape the views are drawing, as the output row states it. What the
    /// text view prints under its box, because a script has no shape column.
    Q_PROPERTY(QString outputText READ outputText NOTIFY changed)

    /// Whether what the views are drawing can go into a custom plot: a running
    /// pipeline whose output is one-dimensional, because a custom plot draws
    /// lines and a line is one dimension. `customRefusal` says why not.
    Q_PROPERTY(bool canAddToCustom READ canAddToCustom NOTIFY changed)
    Q_PROPERTY(QString customRefusal READ customRefusal NOTIFY changed)

public:
    /// What a row is. Everything that is not an Operation is furniture: it
    /// carries no argument, cannot be removed and cannot be moved.
    enum Kind {
        Input = 0,  ///< the dataset, before anything
        /// The member of a compound the rest of the chain runs on, mirrored
        /// from the box in the slice bar. Present only over a compound, which
        /// is the one case there is anything to select.
        Member = 1,
        Slice = 2,  ///< the slice above the table, mirrored
        Operation = 3,
        Adder = 4,  ///< the row that puts another operation in the chain
        Output = 5, ///< the result the views draw
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
        /// What a row's argument may be chosen from, when it is chosen rather
        /// than typed. Empty for every row but the member's, which is the one
        /// argument in this panel whose whole set of legal values is known
        /// before the reader types anything.
        ChoicesRole,
    };
    Q_ENUM(Roles)

    explicit PostprocessModel(QObject* parent = nullptr);

    /// The slice this pipeline's slice row mirrors. Set once, at construction
    /// time, by the controller that owns both.
    void setSliceSource(class TableSetupModel* slice);

    /// Where the member row's chain lives, which is the controller. Set once,
    /// beside the slice source, and for the same reason: the Select row is not
    /// a copy of the box in the bar, it *is* it, so the panel and the bar can
    /// never disagree about which member is being read. A model with no source
    /// simply has no member row, which is what the suites that build one
    /// without a controller get.
    void setMemberSource(AppController* controller);

    /// What the pipeline is running on.
    struct Subject {
        QString path;
        /// The shape the slice sees: the dataset's own, with the axes the
        /// member chain appends after it.
        std::vector<hsize_t> shape;
        /// False for the datatypes there is no arithmetic for, which greys the
        /// panel with a reason rather than offering operations that cannot run.
        bool numeric = false;
        /// The dataset's own shape, before any member was named. The input row
        /// states this one, so the panel reads as the story it is: a hundred
        /// thousand structs, then `.samples`, then a hundred thousand by four.
        std::vector<hsize_t> originShape;
        /// Every member chain the dataset offers, from postproc::memberChains.
        ///
        /// One field rather than a `compound` flag beside a list, because the
        /// two would say the same thing and could disagree: there is a Select
        /// row exactly when there is something to select in it.
        QStringList memberChoices;
        /// The dataset's own datatype, before any member was named. What a
        /// script's `.select` is resolved against when the panel checks one.
        h5core::TypeInfo originType;
    };

    void setDataset(const Subject& subject);

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
    /// Whether there is a member row: a compound, with a controller to ask.
    /// The rows below it are all numbered from this.
    [[nodiscard]] bool hasMember() const;

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

    [[nodiscard]] QString script() const;
    [[nodiscard]] QString outputText() const;
    [[nodiscard]] bool canAddToCustom() const;
    [[nodiscard]] QString customRefusal() const;

    /// Why a script typed into the panel's text box will not do, or empty.
    ///
    /// Checked whole, on every keystroke, as every box here is. Against the
    /// dataset selected when the script names it; against what the file has
    /// already said about another path when it names that one; and for its
    /// grammar alone when nothing is known about the path yet, because a path
    /// nobody has asked about is not known to be wrong.
    [[nodiscard]] Q_INVOKABLE QString scriptError(const QString& text) const;

    /// Make the pipeline what a script says: the member, the slice, the steps,
    /// and the switch on. One refresh at the end rather than one per part, so
    /// the views run the result once.
    ///
    /// A script that does not parse, or names a member the datatype does not
    /// have, changes nothing and says why. One whose steps cannot all run is
    /// applied anyway and stops where the rows would stop, with the reason on
    /// `error` -- the contract every box in this window keeps.
    ///
    /// A script naming **another dataset** selects it, as clicking it in the
    /// tree would, and is applied when that dataset is open. The wait is
    /// through a queued connection on `selectionChanged`, which is what puts
    /// it after DatasetMemory has restored what was filed for that dataset --
    /// the script is the newer instruction and has to win.
    Q_INVOKABLE QString applyScript(const QString& text);

    /// The script of what the views are drawing, which is the chain run only
    /// as far as the active row. What the panel's add-to-custom-plot button
    /// hands a custom plot.
    [[nodiscard]] Q_INVOKABLE QString customScript() const;

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

private slots:
    /// The selection moved; if a script was waiting for it, apply it now.
    void applyPending();

private:
    /// Re-walk the shapes and tell everyone. Cheap -- it reads no elements --
    /// so it runs on every keystroke that commits. Held off while a script is
    /// being applied, which changes the member, the slice and the steps one
    /// after another and should be run once.
    void refresh();
    /// applyScript against the dataset already selected.
    QString applyParsed(const postproc::Script& script);
    /// The chain as a script, running `count` of the steps.
    [[nodiscard]] QString scriptOf(std::size_t count) const;
    /// Which row the slice is, and everything counted from it.
    ///
    /// Every piece of row arithmetic here goes through these rather than
    /// through the constants they used to be, because the member row above the
    /// slice moves all of them by one and a constant that was right in four
    /// places and wrong in a fifth is exactly how this would break.
    [[nodiscard]] int sliceRow() const { return hasMember() ? 2 : 1; }
    [[nodiscard]] int stepRow(int index) const { return sliceRow() + 1 + index; }
    [[nodiscard]] int stepIndex(int row) const { return row - sliceRow() - 1; }
    /// Which stage of the trace a row states. The slice is stage 0; the rows
    /// above it state no stage and are never asked.
    [[nodiscard]] int stageOf(int row) const { return row - sliceRow(); }
    [[nodiscard]] bool isStep(int row) const;
    /// The row holding the add control, which is the one above the output.
    [[nodiscard]] int adderRow() const { return rowCount() - 2; }
    [[nodiscard]] QString sliceText() const;

    TableSetupModel* slice_ = nullptr;
    AppController* member_ = nullptr;
    QString path_;
    std::vector<hsize_t> shape_;
    std::vector<hsize_t> originShape_;
    QStringList memberChoices_;
    bool numeric_ = false;
    bool enabled_ = false;
    std::vector<postproc::Step> steps_;
    /// The row the pipeline runs to, counted in steps: 1 is the slice alone.
    /// Clamped up to the full pipeline whenever a step is added.
    std::size_t upTo_ = 0;
    int chosenOperation_ = 0;
    postproc::Trace trace_;
    h5core::TypeInfo originType_;
    /// A script naming a dataset that was not yet selected, waiting for it.
    std::optional<postproc::Script> pending_;
    bool batching_ = false;
};

} // namespace gui
