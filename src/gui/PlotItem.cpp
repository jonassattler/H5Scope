// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "gui/PlotItem.hpp"

#include <QtCore/QMetaObject>
#include <QtQuick/QSGFlatColorMaterial>
#include <QtQuick/QSGGeometryNode>
#include <QtQuick/QSGNode>
#include <QtQuick/QSGVertexColorMaterial>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace gui {
namespace {

// Below this many samples per pixel column the envelope has nothing to do and
// every visible sample is drawn at its own x. Two per column is the envelope's
// own output rate, so drawing fewer than that is never a saving.
constexpr double kSamplesPerColumn = 2.0;

// A log axis has to put its floor somewhere when the data reaches zero or goes
// negative. Six decades below the largest value is what a reader of a log plot
// expects to see, and it is stated rather than derived so that the chrome can
// state the same thing and the two agree.
constexpr double kLogDecades = 6.0;

} // namespace

PlotItem::PlotItem(QQuickItem* parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
}

PlotItem::~PlotItem() = default;

void PlotItem::setLines(std::vector<PlotLine> lines, double xStart, double xStep)
{
    lines_ = std::move(lines);
    xStart_ = xStart;
    xStep_ = xStep;
    update();
}

void PlotItem::clear()
{
    lines_.clear();
    update();
}

void PlotItem::setXMin(double value)
{
    if (!qFuzzyCompare(xMin_, value)) {
        xMin_ = value;
        Q_EMIT viewChanged();
        update();
    }
}

void PlotItem::setXMax(double value)
{
    if (!qFuzzyCompare(xMax_, value)) {
        xMax_ = value;
        Q_EMIT viewChanged();
        update();
    }
}

void PlotItem::setYMin(double value)
{
    if (!qFuzzyCompare(yMin_, value)) {
        yMin_ = value;
        Q_EMIT viewChanged();
        update();
    }
}

void PlotItem::setYMax(double value)
{
    if (!qFuzzyCompare(yMax_, value)) {
        yMax_ = value;
        Q_EMIT viewChanged();
        update();
    }
}

void PlotItem::setLogY(bool on)
{
    if (logY_ != on) {
        logY_ = on;
        Q_EMIT viewChanged();
        update();
    }
}

void PlotItem::setBatched(bool on)
{
    if (batched_ != on) {
        batched_ = on;
        Q_EMIT batchedChanged();
        update();
    }
}

void PlotItem::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        // The envelope is computed to the pixel width, so a resize is a
        // different set of vertices and not the same ones stretched.
        update();
    }
}

int PlotItem::project(const PlotLine& line)
{
    const int startRuns = static_cast<int>(runs_.size());
    const auto count = static_cast<std::int64_t>(line.count);
    if (count == 0 || line.values == nullptr) {
        return 0;
    }
    const double* values = line.values;

    const double w = width();
    const double h = height();
    if (!(w > 0.0) || !(h > 0.0)) {
        return 0;
    }

    const double xSpan = xMax_ - xMin_;
    if (!(std::abs(xSpan) > 0.0)) {
        return 0;
    }

    // The y mapping, resolved once per line rather than per sample.
    double low = yMin_;
    double high = yMax_;
    if (logY_) {
        high = std::max(yMax_, std::numeric_limits<double>::min());
        low = yMin_ > 0.0 ? yMin_ : high * std::pow(10.0, -kLogDecades);
        low = std::log10(low);
        high = std::log10(high);
    }
    const double ySpan = high - low;
    if (!(std::abs(ySpan) > 0.0)) {
        return 0;
    }

    // Everything below is arithmetic in double. Only the finished pixel
    // coordinate becomes a float, and a pixel coordinate is at most a few
    // thousand -- which is the entire trick, and the reason an epoch timestamp
    // draws as a line here and as a staircase anywhere that casts first.
    const auto toY = [&](double value) {
        const double mapped = logY_ ? std::log10(value) : value;
        return h - (mapped - low) / ySpan * h;
    };
    const auto drawable = [&](double value) {
        return std::isfinite(value) && (!logY_ || value > 0.0);
    };

    int open = -1; // index into points_ where the current run started
    const auto closeRun = [&]() {
        if (open >= 0) {
            const int length = static_cast<int>(points_.size()) - open;
            // A run of one vertex is a point, and a line renderer draws
            // nothing for it. Dropping it keeps the vertex count honest.
            if (length >= 2) {
                runs_.push_back({open, length});
            } else if (length == 1) {
                points_.pop_back();
            }
            open = -1;
        }
    };
    const auto place = [&](double px, double py) {
        if (open < 0) {
            open = static_cast<int>(points_.size());
        }
        points_.emplace_back(px, py);
    };

    if (!(std::abs(xStep_) > 0.0)) {
        return 0;
    }

    // One sample either side of the window, so the line enters and leaves the
    // pane at its edges instead of stopping a pixel short of them.
    const auto indexAt = [&](double x) { return (x - xStart_) / xStep_; };
    std::int64_t first = static_cast<std::int64_t>(std::floor(indexAt(xMin_))) - 1;
    std::int64_t last = static_cast<std::int64_t>(std::ceil(indexAt(xMax_))) + 1;
    first = std::clamp<std::int64_t>(first, 0, count - 1);
    last = std::clamp<std::int64_t>(last, 0, count - 1);
    if (last < first) {
        return 0;
    }

    const std::int64_t visible = last - first + 1;
    const auto columns = static_cast<std::int64_t>(std::ceil(w));

    if (visible <= static_cast<std::int64_t>(kSamplesPerColumn) * columns) {
        // Zoomed in far enough that the envelope would emit more vertices than
        // there are samples. Draw the samples.
        for (std::int64_t i = first; i <= last; ++i) {
            const double value = values[i];
            if (!drawable(value)) {
                closeRun();
                continue;
            }
            const double x = xStart_ + static_cast<double>(i) * xStep_;
            place((x - xMin_) / xSpan * w, toY(value));
        }
        closeRun();
        return static_cast<int>(runs_.size()) - startRuns;
    }

    // The envelope. One column at a time: find the smallest and the largest
    // drawable sample that falls in it, and emit both -- in the order they
    // occur, so the stroke keeps the direction the data has and does not
    // zig-zag where the data rose steadily.
    //
    // This is what stride sampling cannot do. A spike one sample wide is the
    // maximum of whatever column it lands in, so it is selected *because* it is
    // extreme, where a stride selects by position and reaches it only by luck.
    const double perColumn = static_cast<double>(visible) / static_cast<double>(columns);
    for (std::int64_t column = 0; column < columns; ++column) {
        std::int64_t i0 = first
                          + static_cast<std::int64_t>(
                              std::floor(static_cast<double>(column) * perColumn));
        std::int64_t i1 = first
                          + static_cast<std::int64_t>(std::floor(
                              static_cast<double>(column + 1) * perColumn))
                          - 1;
        i1 = std::min(i1, last);
        if (i1 < i0) {
            i1 = i0;
        }
        if (i0 > last) {
            break;
        }

        double lowest = std::numeric_limits<double>::infinity();
        double highest = -std::numeric_limits<double>::infinity();
        std::int64_t lowAt = -1;
        std::int64_t highAt = -1;
        for (std::int64_t i = i0; i <= i1; ++i) {
            const double value = values[i];
            if (!drawable(value)) {
                continue;
            }
            if (value < lowest) {
                lowest = value;
                lowAt = i;
            }
            if (value > highest) {
                highest = value;
                highAt = i;
            }
        }
        if (lowAt < 0) {
            // The whole column is a gap.
            closeRun();
            continue;
        }

        const double px = (xStart_ + static_cast<double>(i0) * xStep_ - xMin_)
                          / xSpan * w;
        if (lowAt <= highAt) {
            place(px, toY(lowest));
            if (highAt != lowAt) {
                place(px, toY(highest));
            }
        } else {
            place(px, toY(highest));
            place(px, toY(lowest));
        }
    }
    closeRun();
    return static_cast<int>(runs_.size()) - startRuns;
}

QSGNode* PlotItem::updatePaintNode(QSGNode* old, UpdatePaintNodeData*)
{
    auto* root = old;
    if (root == nullptr) {
        root = new QSGNode;
    }

    points_.clear();
    runs_.clear();

    const int series = static_cast<int>(lines_.size());
    // Where each line's runs start, so the colouring loops below can find them.
    // A member rather than a local: at ten thousand lines this is forty
    // kilobytes, and allocating it per frame would put the item's own
    // bookkeeping into the frame time.
    runStart_.assign(static_cast<std::size_t>(series) + 1, 0);
    for (int line = 0; line < series; ++line) {
        runStart_[static_cast<std::size_t>(line)] = static_cast<int>(runs_.size());
        project(lines_[static_cast<std::size_t>(line)]);
    }
    runStart_[static_cast<std::size_t>(series)] = static_cast<int>(runs_.size());

    vertices_ = static_cast<int>(points_.size());
    // Queued: updatePaintNode runs inside the render pass, and anything that
    // reacted to this by touching the scene from the same stack would be
    // changing the graph while it is being walked.
    QMetaObject::invokeMethod(this, &PlotItem::drew, Qt::QueuedConnection);

    if (batched_) {
        // One node, one draw call, one buffer. Disjoint segments rather than a
        // strip, because a strip cannot express a gap and every line here has
        // at least the two at its ends.
        int segments = 0;
        for (const Run& run : runs_) {
            segments += run.count - 1;
        }

        while (root->childCount() > 1) {
            QSGNode* extra = root->childAtIndex(root->childCount() - 1);
            root->removeChildNode(extra);
            delete extra;
        }
        if (root->childCount() == 0) {
            auto* node = new QSGGeometryNode;
            auto* geometry =
                new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0);
            geometry->setDrawingMode(QSGGeometry::DrawLines);
            node->setGeometry(geometry);
            node->setFlag(QSGNode::OwnsGeometry);
            node->setMaterial(new QSGVertexColorMaterial);
            node->setFlag(QSGNode::OwnsMaterial);
            root->appendChildNode(node);
        }
        auto* node = static_cast<QSGGeometryNode*>(root->childAtIndex(0));
        QSGGeometry* geometry = node->geometry();
        geometry->allocate(segments * 2);
        auto* vertex = geometry->vertexDataAsColoredPoint2D();

        int at = 0;
        for (int line = 0; line < series; ++line) {
            const QColor colour = lines_[static_cast<std::size_t>(line)].colour;
            const auto red = static_cast<uchar>(colour.red());
            const auto green = static_cast<uchar>(colour.green());
            const auto blue = static_cast<uchar>(colour.blue());
            for (int r = runStart_[static_cast<std::size_t>(line)];
                 r < runStart_[static_cast<std::size_t>(line) + 1]; ++r) {
                const Run& run = runs_[static_cast<std::size_t>(r)];
                for (int i = 0; i < run.count - 1; ++i) {
                    const QPointF& a = points_[static_cast<std::size_t>(run.first + i)];
                    const QPointF& b =
                        points_[static_cast<std::size_t>(run.first + i + 1)];
                    vertex[at++].set(static_cast<float>(a.x()), static_cast<float>(a.y()),
                                     red, green, blue, 255);
                    vertex[at++].set(static_cast<float>(b.x()), static_cast<float>(b.y()),
                                     red, green, blue, 255);
                }
            }
        }
        node->markDirty(QSGNode::DirtyGeometry);
        return root;
    }

    // A node per run. Reused between frames rather than rebuilt: the run count
    // is one per line for almost every dataset, so after the first frame this
    // loop changes vertex data and nothing else.
    const int needed = static_cast<int>(runs_.size());
    while (root->childCount() < needed) {
        auto* node = new QSGGeometryNode;
        auto* geometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 0);
        geometry->setDrawingMode(QSGGeometry::DrawLineStrip);
        // One device pixel. Anything wider is not portable through the RHI --
        // line width above 1 is an optional feature and several backends
        // silently ignore it -- so a thicker stroke has to be built from
        // triangles. Theme.plotLineWidth is 1.0 today, and the day it is not
        // is the day that work becomes necessary.
        geometry->setLineWidth(1.0F);
        node->setGeometry(geometry);
        node->setFlag(QSGNode::OwnsGeometry);
        node->setMaterial(new QSGFlatColorMaterial);
        node->setFlag(QSGNode::OwnsMaterial);
        root->appendChildNode(node);
    }
    while (root->childCount() > needed) {
        QSGNode* extra = root->childAtIndex(root->childCount() - 1);
        root->removeChildNode(extra);
        delete extra;
    }

    for (int line = 0; line < series; ++line) {
        const QColor colour = lines_[static_cast<std::size_t>(line)].colour;
        for (int r = runStart_[static_cast<std::size_t>(line)];
             r < runStart_[static_cast<std::size_t>(line) + 1]; ++r) {
            const Run& run = runs_[static_cast<std::size_t>(r)];
            auto* node = static_cast<QSGGeometryNode*>(root->childAtIndex(r));
            QSGGeometry* geometry = node->geometry();
            geometry->allocate(run.count);
            auto* vertex = geometry->vertexDataAsPoint2D();
            for (int i = 0; i < run.count; ++i) {
                const QPointF& point = points_[static_cast<std::size_t>(run.first + i)];
                vertex[i].set(static_cast<float>(point.x()),
                              static_cast<float>(point.y()));
            }
            auto* material = static_cast<QSGFlatColorMaterial*>(node->material());
            if (material->color() != colour) {
                material->setColor(colour);
                node->markDirty(QSGNode::DirtyMaterial);
            }
            node->markDirty(QSGNode::DirtyGeometry);
        }
    }

    return root;
}

} // namespace gui
