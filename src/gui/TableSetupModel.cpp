// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "TableSetupModel.hpp"

#include <QStringList>

#include <algorithm>

namespace gui {
namespace {

static_assert(static_cast<int>(AxisMode::All) == TableSetupModel::All);
static_assert(static_cast<int>(AxisMode::Index) == TableSetupModel::Index);
static_assert(static_cast<int>(AxisMode::Range) == TableSetupModel::Range);
static_assert(static_cast<int>(AxisMode::Custom) == TableSetupModel::Custom);

/// Write a sorted index list the way one would type it back into a Custom box:
/// runs of consecutive indices collapse to "first:last+1", a full dimension to
/// ":". The upper bound is exclusive, matching parseIndexExpression, so the
/// slice line round-trips through the expression box.
///
/// A selection that needs more than one term is bracketed, as numpy brackets
/// fancy indexing -- without it "[:, :, 0,3]" reads like four subscripts
/// rather than three.
QString renderIndices(const std::vector<hsize_t>& indices, hsize_t extent)
{
    if (extent == 0) {
        // Selecting all of an empty dimension still reads as ":". There is
        // nothing to name, and a placeholder would suggest something is wrong
        // with the selection rather than with the dataset.
        return QStringLiteral(":");
    }
    if (indices.empty()) {
        return QStringLiteral("<none>");
    }
    if (indices.size() == extent && indices.front() == 0
        && indices.back() == extent - 1) {
        return QStringLiteral(":");
    }

    QStringList parts;
    for (std::size_t i = 0; i < indices.size();) {
        std::size_t run = i;
        while (run + 1 < indices.size() && indices[run + 1] == indices[run] + 1) {
            ++run;
        }
        if (run == i) {
            parts << QString::number(indices[i]);
        } else {
            parts << QStringLiteral("%1:%2").arg(indices[i]).arg(indices[run] + 1);
        }
        i = run + 1;
    }
    const QString rendered = parts.join(QLatin1Char(','));
    return parts.size() > 1 ? QStringLiteral("[%1]").arg(rendered) : rendered;
}

} // namespace

TableSetupModel::TableSetupModel(QObject* parent) : QAbstractListModel(parent) {}

void TableSetupModel::setShape(const std::vector<hsize_t>& shape,
                               const std::optional<h5core::ImageInfo>& image)
{
    // One source of truth with DatasetTableModel's own default: the panel has
    // to open showing the slice the grid is already displaying, or the reader
    // is looking at two different answers to the same question.
    const DefaultAxes axes = defaultAxes(shape, image);

    beginResetModel();
    dimensions_.clear();
    dimensions_.reserve(shape.size());
    for (std::size_t i = 0; i < shape.size(); ++i) {
        Dimension dimension;
        dimension.extent = shape[i];
        // A pinned dimension opens in Index mode rather than All, so the panel
        // states the channel it is showing and the slider is there to change it.
        dimension.mode = (axes.pinned.has_value() && *axes.pinned == i)
                             ? AxisMode::Index
                             : AxisMode::All;
        dimension.index = 0;
        dimension.first = 0;
        dimension.last = shape[i] > 0 ? shape[i] - 1 : 0;
        dimension.onX = axes.onX[i];
        dimensions_.push_back(std::move(dimension));
    }
    endResetModel();
    emit tableLayoutChanged();
}

int TableSetupModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(dimensions_.size());
}

bool TableSetupModel::valid(int dimension) const
{
    return dimension >= 0 && static_cast<std::size_t>(dimension) < dimensions_.size();
}

std::vector<hsize_t> TableSetupModel::indicesFor(const Dimension& dimension) const
{
    std::vector<hsize_t> indices;
    if (dimension.extent == 0) {
        return indices;
    }

    switch (dimension.mode) {
    case AxisMode::All:
        indices.resize(static_cast<std::size_t>(dimension.extent));
        for (hsize_t i = 0; i < dimension.extent; ++i) {
            indices[static_cast<std::size_t>(i)] = i;
        }
        return indices;
    case AxisMode::Index:
        indices.push_back(std::min(dimension.index, dimension.extent - 1));
        return indices;
    case AxisMode::Range: {
        const hsize_t first = std::min(dimension.first, dimension.extent - 1);
        const hsize_t last = std::min(dimension.last, dimension.extent - 1);
        for (hsize_t i = std::min(first, last); i <= std::max(first, last); ++i) {
            indices.push_back(i);
        }
        return indices;
    }
    case AxisMode::Custom:
        return dimension.custom;
    }
    return indices;
}

QVariant TableSetupModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || !valid(index.row())) {
        return {};
    }
    const Dimension& dimension = dimensions_[static_cast<std::size_t>(index.row())];

    switch (role) {
    case ExtentRole:
        return static_cast<qint64>(dimension.extent);
    case ModeRole:
        return static_cast<int>(dimension.mode);
    case IndexValueRole:
        return static_cast<qint64>(dimension.index);
    case RangeFirstRole:
        return static_cast<qint64>(dimension.first);
    case RangeLastRole:
        return static_cast<qint64>(dimension.last);
    case ExpressionRole:
        return dimension.expression;
    case ExpressionErrorRole:
        return dimension.expressionError;
    case OnXRole:
        return dimension.onX;
    case SelectedCountRole:
        return static_cast<qint64>(indicesFor(dimension).size());
    case SummaryRole:
        return summaryFor(dimension);
    default:
        return {};
    }
}

QHash<int, QByteArray> TableSetupModel::roleNames() const
{
    return {
        {ExtentRole, "extent"},
        {ModeRole, "mode"},
        {IndexValueRole, "indexValue"},
        {RangeFirstRole, "rangeFirst"},
        {RangeLastRole, "rangeLast"},
        {ExpressionRole, "expression"},
        {ExpressionErrorRole, "expressionError"},
        {OnXRole, "onX"},
        {SelectedCountRole, "selectedCount"},
        {SummaryRole, "summary"},
    };
}

TableLayout TableSetupModel::layout() const
{
    TableLayout result;
    result.indices.reserve(dimensions_.size());
    result.onX.reserve(dimensions_.size());
    for (const Dimension& dimension : dimensions_) {
        result.indices.push_back(indicesFor(dimension));
        result.onX.push_back(dimension.onX);
    }
    return result;
}

QString TableSetupModel::summaryFor(const Dimension& dimension) const
{
    // Written from the mode rather than from the indices, which is the whole
    // of the fix for a line that rewrote what was typed into it. `1` and `1:2`
    // select the same one element and are not the same subscript; reading the
    // line back off the resolved indices could only ever print one of them,
    // and it printed `1`.
    switch (dimension.mode) {
    case AxisMode::All:
        return QStringLiteral(":");
    case AxisMode::Index:
        if (dimension.extent == 0) {
            return QStringLiteral(":");
        }
        return QString::number(std::min(dimension.index, dimension.extent - 1));
    case AxisMode::Range: {
        if (dimension.extent == 0) {
            return QStringLiteral(":");
        }
        const hsize_t top = dimension.extent - 1;
        const hsize_t first = std::min(dimension.first, top);
        const hsize_t last = std::min(dimension.last, top);
        // The upper bound is exclusive everywhere in this notation, and these
        // two boxes are inclusive, so the run ends one past the box.
        return QStringLiteral("%1:%2")
            .arg(std::min(first, last))
            .arg(std::max(first, last) + 1);
    }
    case AxisMode::Custom:
        // What the reader wrote, when they wrote something that parsed: a
        // stride comes back as `::2` rather than as the five indices it stands
        // for. Only a Custom selection that was never typed -- one seeded from
        // the panel -- falls back to writing its indices out.
        if (!dimension.customText.isEmpty()) {
            return postproc::bracketedIfListed(dimension.customText);
        }
        return renderIndices(dimension.custom, dimension.extent);
    }
    return renderIndices(indicesFor(dimension), dimension.extent);
}

QStringList TableSetupModel::summaries() const
{
    QStringList parts;
    parts.reserve(static_cast<qsizetype>(dimensions_.size()));
    for (const Dimension& dimension : dimensions_) {
        parts << summaryFor(dimension);
    }
    return parts;
}

QString TableSetupModel::sliceText() const
{
    return summaries().join(QStringLiteral(", "));
}

bool TableSetupModel::readSlice(const QString& text,
                                std::vector<IndexExpression>& chosen,
                                QStringList& written, QString& error) const
{
    // The grammar, the ellipsis, the omitted trailing dimensions and every one
    // of the reasons a line can be refused live in postproc::readSubscripts,
    // because a Slice step in the postprocessing pipeline reads the same line
    // and the two must not drift apart. All this adds is the extents.
    std::vector<hsize_t> shape;
    shape.reserve(dimensions_.size());
    for (const Dimension& dimension : dimensions_) {
        shape.push_back(dimension.extent);
    }
    return readSubscripts(text, shape, chosen, written, error);
}

QString TableSetupModel::sliceError(const QString& text) const
{
    std::vector<IndexExpression> chosen;
    QStringList written;
    QString error;
    // The error is the answer; whether it is empty is the same question
    // asked twice.
    static_cast<void>(readSlice(text, chosen, written, error));
    return error;
}

QString TableSetupModel::applySlice(const QString& text)
{
    // Read the whole line before changing anything: a slice describes one
    // selection across every dimension at once, so a subscript that does not
    // parse leaves the table exactly as it was.
    std::vector<IndexExpression> chosen;
    QStringList written;
    QString error;
    if (!readSlice(text, chosen, written, error)) {
        return error;
    }

    const auto rank = static_cast<qsizetype>(dimensions_.size());
    for (qsizetype d = 0; d < rank; ++d) {
        const auto slot = static_cast<std::size_t>(d);
        Dimension& dimension = dimensions_[slot];
        const IndexExpression& subscript = chosen[slot];
        // The mode that matches how the subscript was *written*. Each of the
        // first three is a selection the panel can draw with its own controls;
        // only what none of them describes falls through to a Custom
        // expression, which then keeps the text that produced it so the line
        // prints back what was typed.
        switch (subscript.form) {
        case IndexExpression::Form::Whole:
            dimension.mode = AxisMode::All;
            break;
        case IndexExpression::Form::Single:
            dimension.mode = AxisMode::Index;
            dimension.index = subscript.first;
            break;
        case IndexExpression::Form::Span:
            dimension.mode = AxisMode::Range;
            dimension.first = subscript.first;
            dimension.last = subscript.last;
            break;
        case IndexExpression::Form::Scattered:
            dimension.mode = AxisMode::Custom;
            dimension.custom = subscript.indices;
            // The subscript as it was written, both in the panel's box and as
            // what the line prints: a stride stays `::2` rather than becoming
            // the five indices it stands for.
            dimension.expression = written.at(d);
            dimension.customText = written.at(d);
            dimension.expressionError.clear();
            break;
        }
    }

    // One announcement for the whole line rather than `touch` per dimension:
    // each of those carries a layout change with it, and a rank-4 slice would
    // rebuild the table four times on its way to the selection asked for.
    emit dataChanged(index(0, 0), index(static_cast<int>(rank) - 1, 0),
                     {ModeRole, IndexValueRole, RangeFirstRole, RangeLastRole,
                      ExpressionRole, ExpressionErrorRole, SelectedCountRole,
                      SummaryRole});
    emit tableLayoutChanged();
    return {};
}

void TableSetupModel::touch(int dimension, const std::vector<int>& roles)
{
    const QModelIndex changed = index(dimension, 0);
    QList<int> announced(roles.begin(), roles.end());
    // Both of these derive from whatever else changed, so every edit reports
    // them and no caller has to remember to.
    announced << SelectedCountRole << SummaryRole;
    emit dataChanged(changed, changed, announced);
    emit tableLayoutChanged();
}

void TableSetupModel::setMode(int dimension, int mode)
{
    if (!valid(dimension) || mode < All || mode > Custom) {
        return;
    }
    Dimension& target = dimensions_[static_cast<std::size_t>(dimension)];
    const auto wanted = static_cast<AxisMode>(mode);
    if (target.mode == wanted) {
        return;
    }

    if (wanted == AxisMode::Custom) {
        // Seed the box from whatever is selected now, so switching to Custom
        // shows the current selection written out rather than an empty field
        // reporting itself as an error.
        const std::vector<hsize_t> current = indicesFor(target);
        target.custom = current;
        target.expression = renderIndices(current, target.extent);
        target.customText = target.expression;
        target.expressionError.clear();
    }

    target.mode = wanted;
    touch(dimension, {ModeRole, ExpressionRole, ExpressionErrorRole});
}

void TableSetupModel::setIndex(int dimension, int value)
{
    if (!valid(dimension)) {
        return;
    }
    Dimension& target = dimensions_[static_cast<std::size_t>(dimension)];
    const hsize_t clamped =
        target.extent == 0
            ? 0
            : std::min(static_cast<hsize_t>(std::max(value, 0)), target.extent - 1);
    if (target.index == clamped) {
        return;
    }
    target.index = clamped;
    touch(dimension, {IndexValueRole});
}

void TableSetupModel::setRange(int dimension, int first, int last)
{
    if (!valid(dimension)) {
        return;
    }
    Dimension& target = dimensions_[static_cast<std::size_t>(dimension)];
    if (target.extent == 0) {
        return;
    }
    const hsize_t top = target.extent - 1;
    hsize_t low = std::min(static_cast<hsize_t>(std::max(first, 0)), top);
    hsize_t high = std::min(static_cast<hsize_t>(std::max(last, 0)), top);
    if (low > high) {
        std::swap(low, high);
    }
    if (target.first == low && target.last == high) {
        return;
    }
    target.first = low;
    target.last = high;
    touch(dimension, {RangeFirstRole, RangeLastRole});
}

void TableSetupModel::setExpression(int dimension, const QString& text)
{
    if (!valid(dimension)) {
        return;
    }
    Dimension& target = dimensions_[static_cast<std::size_t>(dimension)];
    if (target.expression == text) {
        return;
    }
    target.expression = text;

    const IndexExpression parsed = parseIndexExpression(text, target.extent);
    target.expressionError = parsed.error;
    if (parsed.valid()) {
        target.custom = parsed.indices;
        // ...and the text that produced it, which is what the slice line
        // prints. A box halfway through being retyped leaves this alone, so
        // the line above keeps showing the selection actually on screen.
        target.customText = text.trimmed();
    }
    touch(dimension, {ExpressionRole, ExpressionErrorRole});
}

void TableSetupModel::setAxis(int dimension, bool onX)
{
    if (!valid(dimension)) {
        return;
    }
    Dimension& target = dimensions_[static_cast<std::size_t>(dimension)];
    if (target.onX == onX) {
        return;
    }
    target.onX = onX;
    touch(dimension, {OnXRole});
}

} // namespace gui
