// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "PostprocessModel.hpp"

#include "AppController.hpp"
#include "TableSetupModel.hpp"

#include <algorithm>

namespace gui {

PostprocessModel::PostprocessModel(QObject* parent) : QAbstractListModel(parent) {}

void PostprocessModel::setSliceSource(TableSetupModel* slice)
{
    slice_ = slice;
}

void PostprocessModel::setMemberSource(AppController* controller)
{
    member_ = controller;
}

bool PostprocessModel::hasMember() const
{
    return member_ != nullptr && !memberChoices_.isEmpty();
}

QString PostprocessModel::sliceText() const
{
    return slice_ != nullptr ? slice_->sliceText() : QString{};
}

bool PostprocessModel::isStep(int row) const
{
    return row > sliceRow() && stepIndex(row) >= 0
           && stepIndex(row) < static_cast<int>(steps_.size());
}

void PostprocessModel::setChosenOperation(int index)
{
    const int wanted =
        std::clamp(index, 0, static_cast<int>(postproc::operations().size()) - 1);
    if (wanted == chosenOperation_) {
        return;
    }
    chosenOperation_ = wanted;
    emit changed();
}

void PostprocessModel::addChosenStep()
{
    addStep(postproc::operations()[static_cast<std::size_t>(chosenOperation_)].name);
}

bool PostprocessModel::active() const
{
    // A switch flipped over a dataset there is no arithmetic for changes
    // nothing, so it does not get to say it is changing something. Note that
    // this is true with no operations added: the slice alone already reads
    // differently, because a bare index drops its dimension here and does not
    // in the table.
    return enabled_ && numeric_ && !path_.isEmpty();
}

std::vector<postproc::Step> PostprocessModel::pipeline() const
{
    std::vector<postproc::Step> steps;
    steps.reserve(steps_.size() + 1);
    steps.push_back({postproc::OperationKind::Slice, sliceText()});
    steps.insert(steps.end(), steps_.begin(), steps_.end());
    return steps;
}

std::size_t PostprocessModel::upTo() const { return upTo_; }

int PostprocessModel::activeRow() const
{
    // The slice row runs one step and the kth operation below it runs k+1, so
    // the row and the step count differ by exactly where the slice row is.
    // They were the same number while the slice was always row 1.
    return sliceRow() + static_cast<int>(upTo_) - 1;
}

void PostprocessModel::setActiveRow(int row)
{
    const auto last = static_cast<int>(steps_.size()) + 1;
    const std::size_t wanted = static_cast<std::size_t>(
        std::clamp(stageOf(row) + 1, 1, std::max(1, last)));
    if (wanted == upTo_) {
        return;
    }
    upTo_ = wanted;
    refresh();
}

void PostprocessModel::setEnabled(bool enabled)
{
    if (enabled == enabled_) {
        return;
    }
    enabled_ = enabled;
    refresh();
}

void PostprocessModel::setDataset(const Subject& subject)
{
    // A dataset nobody has been at opens on the defaults, which for a pipeline
    // means no pipeline: a Max over axis 0 says nothing about the next dataset,
    // and on one of a different rank it does not even exist. DatasetMemory puts
    // back whatever was made about *this* one a moment later, on
    // selectionChanged.
    //
    // Clearing rather than leaving it also keeps the first run cheap. The
    // controller hands the table a source as soon as the dataset is open, which
    // is before the restore; without this that run would be the previous
    // dataset's chain over the new dataset's elements, computed in full and
    // then thrown away.
    //
    // A *different* dataset, though. Naming a member of the one already open
    // comes back through here too, because the shape below the member changed;
    // clearing on that would mean the Select row in this very panel turned the
    // panel off every time it was used. A step the new shape cannot take stops
    // the walk and says why, which is the panel working rather than failing.
    const bool moved = subject.path != path_;
    beginResetModel();
    if (moved) {
        steps_.clear();
        enabled_ = false;
        upTo_ = 1;
    }
    path_ = subject.path;
    shape_ = subject.shape;
    numeric_ = subject.numeric;
    originShape_ = subject.originShape;
    memberChoices_ = subject.memberChoices;
    endResetModel();
    refresh();
}

void PostprocessModel::reset()
{
    beginResetModel();
    steps_.clear();
    enabled_ = false;
    upTo_ = 1;
    endResetModel();
    refresh();
}

void PostprocessModel::sliceChanged() { refresh(); }

void PostprocessModel::refresh()
{
    upTo_ = std::clamp<std::size_t>(upTo_, 1, steps_.size() + 1);
    trace_ = postproc::trace(shape_, pipeline(), upTo_);
    if (rowCount() > 0) {
        emit dataChanged(index(0, 0), index(rowCount() - 1, 0));
    }
    emit changed();
}

int PostprocessModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    // The input, the member on a compound, the slice, one per operation, the
    // row that adds another, and the output.
    return static_cast<int>(steps_.size()) + 4 + (hasMember() ? 1 : 0);
}

QVariant PostprocessModel::data(const QModelIndex& index, int role) const
{
    const int row = index.row();
    if (row < 0 || row >= rowCount()) {
        return {};
    }
    const bool output = row == rowCount() - 1;
    const int step = stepIndex(row); // < 0 for the input and the slice

    const Kind kind = row == 0                       ? Input
                      : (hasMember() && row == 1)    ? Member
                      : row == sliceRow()            ? Slice
                      : output                       ? Output
                      : row == adderRow()            ? Adder
                                                     : Operation;

    switch (role) {
    case KindRole:
        return static_cast<int>(kind);
    case LabelRole:
        switch (kind) {
        case Input:
            return path_.isEmpty() ? tr("no dataset") : path_;
        case Member:
            return tr("select");
        case Slice:
            return tr("slice");
        case Adder:
            return tr("add");
        case Output:
            return tr("output");
        case Operation:
            return postproc::operationInfo(steps_[static_cast<std::size_t>(step)].kind)
                .name;
        }
        return {};
    case ArgumentRole:
        if (kind == Member) {
            return member_->memberText();
        }
        if (kind == Slice) {
            return sliceText();
        }
        if (kind == Operation) {
            return steps_[static_cast<std::size_t>(step)].argument;
        }
        return QString{};
    case ArgumentLabelRole:
        if (kind == Member) {
            return tr("member");
        }
        if (kind == Slice) {
            return tr("subscripts");
        }
        if (kind == Operation) {
            return postproc::operationInfo(steps_[static_cast<std::size_t>(step)].kind)
                .argumentLabel;
        }
        return QString{};
    case PlaceholderRole:
        if (kind == Member) {
            // What the empty choice reads as. A compound with no member named
            // is the whole struct, which is a selection and not an absence.
            return tr("the whole struct");
        }
        if (kind == Operation) {
            return postproc::operationInfo(steps_[static_cast<std::size_t>(step)].kind)
                .placeholder;
        }
        return QString{};
    case ShapeRole: {
        if (kind == Input) {
            // The dataset's own shape, so that the member row below states what
            // naming a member did to it. They are the same number on a dataset
            // with no member named, which is the honest answer there.
            return postproc::describeShape(hasMember() ? originShape_ : shape_);
        }
        if (kind == Member) {
            return postproc::describeShape(shape_);
        }
        if (kind == Output) {
            return postproc::describeShape(trace_.output);
        }
        if (kind == Adder) {
            // It is not a step and leaves nothing behind; there is no shape
            // for it to state.
            return QString{};
        }
        // A row that did not run has no shape to state. Saying nothing is the
        // point: a stale shape beside a greyed row would be a claim about
        // data that was never computed.
        const auto stage = static_cast<std::size_t>(stageOf(row));
        if (stage < trace_.ran && stage < trace_.stages.size()) {
            return postproc::describeShape(trace_.stages[stage].shape);
        }
        return QString{};
    }
    case ErrorRole: {
        if (kind == Member) {
            // Whatever is wrong with the chain itself, which the controller
            // answers without opening anything.
            return member_->memberError(member_->memberText());
        }
        if (kind == Input || kind == Output || kind == Adder) {
            return QString{};
        }
        const auto stage = static_cast<std::size_t>(stageOf(row));
        if (stage < trace_.stages.size()) {
            return trace_.stages[stage].error;
        }
        return QString{};
    }
    case RemovableRole:
        return kind == Operation;
    case MovableRole:
        return kind == Operation;
    case ChoicesRole: {
        // Every chain the datatype offers. Built once when the dataset was
        // selected rather than per read: it is arithmetic over a TypeInfo and
        // costs no file, but it is also what a combo box binds as its model,
        // and a list rebuilt on every read resets that box under the reader.
        if (kind != Member) {
            return QStringList{};
        }
        QStringList choices = memberChoices_;
        // A chain that indexes a ragged member -- `.tags[0]` -- is a selection
        // like any other and is not in the list, because the list holds names
        // and the subscripts live on the slice line. It is still what is
        // selected, so it goes in front rather than leaving the box showing
        // something the reader did not choose.
        const QString current = member_->memberText();
        if (!current.isEmpty() && !choices.contains(current)) {
            choices.prepend(current);
        }
        return choices;
    }
    case ComputedRole: {
        // The output is never greyed: it is the end of whatever is actually
        // being computed, which is exactly what clicking a row changes.
        if (output || kind == Input || kind == Member || kind == Adder) {
            return true;
        }
        // Read off what actually ran rather than off the clicked row, because
        // there are two ways to stop and both leave everything below them
        // uncomputed: the reader asked for a shorter pipeline, or a step
        // refused. The row that refused stays lit even though it did not run,
        // because it is the one carrying the reason -- but only when there is
        // one, or a truncated pipeline would light a row past its end.
        const auto stage = static_cast<std::size_t>(stageOf(row));
        return stage < trace_.ran || (!trace_.ok() && stage == trace_.ran);
    }
    default:
        return {};
    }
}

QHash<int, QByteArray> PostprocessModel::roleNames() const
{
    return {
        {KindRole, "kind"},
        {LabelRole, "label"},
        {ArgumentRole, "argument"},
        {ArgumentLabelRole, "argumentLabel"},
        {PlaceholderRole, "placeholder"},
        {ShapeRole, "shape"},
        {ErrorRole, "error"},
        {RemovableRole, "removable"},
        {MovableRole, "movable"},
        {ComputedRole, "computed"},
        // Not "choices": the row already has a property of that name, handed
        // down by the panel, holding the operations the add row offers.
        {ChoicesRole, "memberChoices"},
    };
}

QVariantList PostprocessModel::operations() const
{
    QVariantList out;
    for (const postproc::OperationInfo& info : postproc::operations()) {
        out << QVariantMap{{QStringLiteral("name"), info.name},
                           {QStringLiteral("argumentLabel"), info.argumentLabel},
                           {QStringLiteral("placeholder"), info.placeholder}};
    }
    return out;
}

QVariantList PostprocessModel::steps() const
{
    QVariantList out;
    for (const postproc::Step& step : steps_) {
        out << QVariantMap{
            {QStringLiteral("operation"), postproc::operationInfo(step.kind).name},
            {QStringLiteral("argument"), step.argument}};
    }
    return out;
}

void PostprocessModel::setSteps(const QVariantList& steps)
{
    std::vector<postproc::Step> read;
    read.reserve(static_cast<std::size_t>(steps.size()));
    for (const QVariant& entry : steps) {
        const QVariantMap fields = entry.toMap();
        const auto kind =
            postproc::operationNamed(fields.value(QStringLiteral("operation")).toString());
        if (!kind.has_value()) {
            // A name this build does not have. Dropped rather than refused:
            // this comes back out of the per-dataset store, and one unreadable
            // entry should not cost the reader the rest of their pipeline.
            continue;
        }
        read.push_back({*kind, fields.value(QStringLiteral("argument")).toString()});
    }
    if (read == steps_) {
        return;
    }
    beginResetModel();
    steps_ = std::move(read);
    upTo_ = steps_.size() + 1;
    endResetModel();
    refresh();
}

void PostprocessModel::addStep(const QString& name)
{
    const auto kind = postproc::operationNamed(name);
    if (!kind.has_value()) {
        return;
    }
    const int row = stepRow(static_cast<int>(steps_.size()));
    beginInsertRows({}, row, row);
    steps_.push_back({*kind, QString{}});
    // A step added is a step meant, so the pipeline runs down to it.
    upTo_ = steps_.size() + 1;
    endInsertRows();
    refresh();
}

void PostprocessModel::removeStep(int row)
{
    if (!isStep(row)) {
        return;
    }
    beginRemoveRows({}, row, row);
    steps_.erase(steps_.begin() + stepIndex(row));
    endRemoveRows();
    refresh();
}

void PostprocessModel::moveStep(int from, int to)
{
    if (!isStep(from) || steps_.empty()) {
        return;
    }
    // Clamped into the operations: the slice above them and the output below
    // are the ends of the chain, and nothing is dropped past either.
    const int last = stepRow(static_cast<int>(steps_.size()) - 1);
    const int target = std::clamp(to, stepRow(0), last);
    if (target == from) {
        return;
    }

    // Qt counts a forward move to the position *after* the destination, which
    // is the one place this differs from moving in a vector.
    beginMoveRows({}, from, from, {}, target > from ? target + 1 : target);
    postproc::Step moved = steps_[static_cast<std::size_t>(stepIndex(from))];
    steps_.erase(steps_.begin() + stepIndex(from));
    steps_.insert(steps_.begin() + stepIndex(target), std::move(moved));
    endMoveRows();
    refresh();
}

void PostprocessModel::setArgument(int row, const QString& argument)
{
    if (hasMember() && row == 1) {
        // The member row is the box in the slice bar, so this writes through
        // to the controller and comes back as a layout change. The controller
        // folds any subscript onto the slice line, which is the row below this
        // one -- so the two rows of this panel state the two halves of the
        // identity the whole notation rests on.
        static_cast<void>(member_->applyMember(argument));
        return;
    }
    if (row == sliceRow()) {
        // The slice row is the slice above the table, so this writes through
        // to it and comes back as a layout change rather than being kept here.
        if (slice_ != nullptr) {
            static_cast<void>(slice_->applySlice(argument));
        }
        return;
    }
    if (!isStep(row)) {
        return;
    }
    postproc::Step& step = steps_[static_cast<std::size_t>(stepIndex(row))];
    if (step.argument == argument) {
        return;
    }
    step.argument = argument;
    refresh();
}

QString PostprocessModel::argumentError(int row, const QString& argument) const
{
    if (hasMember() && row == 1) {
        return member_->memberError(argument);
    }
    if (row == sliceRow()) {
        return slice_ != nullptr ? slice_->sliceError(argument) : QString{};
    }
    if (!isStep(row)) {
        return {};
    }
    // Checked against the shape this row is actually handed, which is the
    // shape the row above it leaves. A step whose input never resolved has
    // nothing to check against and says nothing rather than guessing.
    const auto stage = static_cast<std::size_t>(stageOf(row) - 1);
    if (stage >= trace_.ran || stage >= trace_.stages.size()) {
        return {};
    }
    const postproc::Step& step = steps_[static_cast<std::size_t>(stepIndex(row))];
    return postproc::shapeAfter({step.kind, argument}, trace_.stages[stage].shape)
        .error;
}

} // namespace gui
