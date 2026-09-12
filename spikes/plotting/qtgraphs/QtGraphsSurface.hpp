// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// The baseline: the plot H5Scope draws today.
//
// This is deliberately a copy of the application's arrangement rather than the
// fastest thing Qt Graphs can be made to do. The C++ side hands the renderer
// one line at a time through fill(QAbstractSeries*, int) -- the same signature
// DatasetPlot carries, for the same reason, building one QList<QPointF> and
// calling QXYSeries::replace() once. The QML side creates the series itself,
// and rebuilds the whole GraphsView when the set of lines changes, because
// PlotSurface.qml:440-455 has to: Qt Graphs keeps what a series last drew, and
// neither emptying it nor removing it from the graph clears the old path.
//
// Reproducing the workarounds is the point. A baseline measured without them
// would be a baseline of a plot nobody can ship, and the comparison would
// flatter the incumbent by exactly the cost of the defects it is being
// compared for.

#include "common/SpikeMain.hpp"

#include <QtCore/QList>
#include <QtGui/QColor>

QT_BEGIN_NAMESPACE
class QAbstractSeries;
QT_END_NAMESPACE

namespace spike {

class QtGraphsSurface : public QmlSurface
{
    Q_OBJECT

    /// How many lines the QML should create. Read during the rebuild.
    Q_PROPERTY(int seriesCount READ seriesCount NOTIFY rebuilt FINAL)

    /// The window the axes should show. Written once per frame by the gesture,
    /// bound by the QML onto ValueAxis.min and .max.
    Q_PROPERTY(double xMin READ xMin NOTIFY viewChanged FINAL)
    Q_PROPERTY(double xMax READ xMax NOTIFY viewChanged FINAL)
    Q_PROPERTY(double yMin READ yMin NOTIFY viewChanged FINAL)
    Q_PROPERTY(double yMax READ yMax NOTIFY viewChanged FINAL)

    /// What the strip at the top prints, for the interactive run.
    Q_PROPERTY(QString caption READ caption NOTIFY rebuilt FINAL)

public:
    using QmlSurface::QmlSurface;

    [[nodiscard]] const char* rendererName() const override { return "qtgraphs"; }

    void setSource(const SyntheticSource* source) override;
    void setViewWindow(const ViewWindow& window) override;

    /// Qt 6.11's 2-D Qt Graphs ships QValueAxis, QBarCategoryAxis and
    /// QDateTimeAxis. There is no logarithmic axis for a 2-D graph at all --
    /// QLogValue3DAxisFormatter exists, and it formats a 3-D axis.
    [[nodiscard]] bool canDrawLogY() const override { return false; }

    [[nodiscard]] int seriesCount() const;
    [[nodiscard]] double xMin() const { return view_.xMin; }
    [[nodiscard]] double xMax() const { return view_.xMax; }
    [[nodiscard]] double yMin() const { return view_.yMin; }
    [[nodiscard]] double yMax() const { return view_.yMax; }
    [[nodiscard]] QString caption() const;

    /// The seam. Byte for byte the shape of DatasetPlot::fill(): a cell that
    /// would not read is a gap in the line rather than a zero, the x comes from
    /// an origin and a step, and the whole line goes in with one replace()
    /// because appending across this boundary is what made a graph of ten
    /// thousand points slow.
    Q_INVOKABLE void fill(QAbstractSeries* target, int series);

    /// The colour of one line, so the QML does not have to invent its own and
    /// disagree with the other two spikes.
    Q_INVOKABLE QColor seriesColor(int index) const;

Q_SIGNALS:
    /// The set of lines changed: the QML must throw the GraphsView away and
    /// build a new one.
    void rebuilt();
    /// The same lines, a different window.
    void viewChanged();

private:
    const SyntheticSource* source_ = nullptr;
    ViewWindow view_;
};

} // namespace spike
