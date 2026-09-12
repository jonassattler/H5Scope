// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "PlotItem.hpp"

#include "common/Palette.hpp"

#include <QtCore/QMetaObject>
#include <QtQuick/QSGFlatColorMaterial>
#include <QtQuick/QSGGeometryNode>
#include <QtQuick/QSGNode>
#include <QtQuick/QSGVertexColorMaterial>

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <limits>

namespace spike {
namespace {

// Below this many samples per pixel column the envelope has nothing to do and
// every visible sample is drawn at its own x. Two per column is the envelope's
// own output rate, so drawing fewer than that is never a saving.
constexpr double kSamplesPerColumn = 2.0;

// A log axis has to put its floor somewhere when the data reaches zero or goes
// negative. Six decades below the largest value is what a reader of a log plot
// expects to see, and it is stated here rather than derived so that two
// renderers asked the same question give the same picture.
constexpr double kLogDecades = 6.0;

} // namespace

PlotItem::PlotItem(QQuickItem* parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
}

PlotItem::~PlotItem() = default;

void PlotItem::setSource(const SyntheticSource* source)
{
    source_ = source;
    update();
}

void PlotItem::setView(const ViewWindow& view)
{
    view_ = view;
    update();
}

void PlotItem::setBatching(Batching batching)
{
    batching_ = batching;
    update();
}

bool PlotItem::setLogY(bool on)
{
    if (on && source_ != nullptr && !(source_->maximum() > 0.0)) {
        // Nothing positive anywhere. Refusing is the honest answer; an empty
        // pane would look like a renderer that failed rather than like data
        // that cannot be shown this way.
        return false;
    }
    logY_ = on;
    update();
    return true;
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

int PlotItem::project(const std::vector<double>& values)
{
    const int startRuns = static_cast<int>(runs_.size());
    const auto count = static_cast<std::int64_t>(values.size());
    if (count == 0 || source_ == nullptr) {
        return 0;
    }

    const double w = width();
    const double h = height();
    if (!(w > 0.0) || !(h > 0.0)) {
        return 0;
    }

    const double xSpan = view_.xMax - view_.xMin;
    if (!(std::abs(xSpan) > 0.0)) {
        return 0;
    }

    // The y mapping, resolved once per line rather than per sample.
    double yLow = view_.yMin;
    double yHigh = view_.yMax;
    if (logY_) {
        yHigh = std::max(view_.yMax, std::numeric_limits<double>::min());
        yLow = view_.yMin > 0.0 ? view_.yMin
                                : yHigh * std::pow(10.0, -kLogDecades);
        yLow = std::log10(yLow);
        yHigh = std::log10(yHigh);
    }
    const double ySpan = yHigh - yLow;
    if (!(std::abs(ySpan) > 0.0)) {
        return 0;
    }

    // Everything below is arithmetic in double. Only the finished pixel
    // coordinate becomes a float, and a pixel coordinate is at most a few
    // thousand -- which is the entire trick, and the reason an epoch timestamp
    // draws as a line here and as a staircase anywhere that casts first.
    const auto toY = [&](double value) {
        const double mapped = logY_ ? std::log10(value) : value;
        return h - (mapped - yLow) / ySpan * h;
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

    if (source_->hasExplicitX()) {
        // x that is not a step cannot be bucketed by column: the samples are
        // not in x order, so a column holds samples from all over the line and
        // its extremes are not an envelope of anything. Every point is drawn.
        // That is a real limit of the envelope and not of this renderer, and
        // the report says so rather than the code hiding it.
        const std::vector<double>& xs = source_->xValues();
        for (std::int64_t i = 0; i < count; ++i) {
            const double value = values[static_cast<std::size_t>(i)];
            if (!drawable(value)) {
                closeRun();
                continue;
            }
            place((xs[static_cast<std::size_t>(i)] - view_.xMin) / xSpan * w,
                  toY(value));
        }
        closeRun();
        return static_cast<int>(runs_.size()) - startRuns;
    }

    const double xStart = source_->xStart();
    const double xStep = source_->xStep();
    if (!(std::abs(xStep) > 0.0)) {
        return 0;
    }

    // One sample either side of the window, so the line enters and leaves the
    // pane at its edges instead of stopping a pixel short of them.
    const auto indexAt = [&](double x) { return (x - xStart) / xStep; };
    std::int64_t first = static_cast<std::int64_t>(std::floor(indexAt(view_.xMin))) - 1;
    std::int64_t last = static_cast<std::int64_t>(std::ceil(indexAt(view_.xMax))) + 1;
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
            const double value = values[static_cast<std::size_t>(i)];
            if (!drawable(value)) {
                closeRun();
                continue;
            }
            const double x = xStart + static_cast<double>(i) * xStep;
            place((x - view_.xMin) / xSpan * w, toY(value));
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
        std::int64_t i0 = first + static_cast<std::int64_t>(
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

        double low = std::numeric_limits<double>::infinity();
        double high = -std::numeric_limits<double>::infinity();
        std::int64_t lowAt = -1;
        std::int64_t highAt = -1;
        for (std::int64_t i = i0; i <= i1; ++i) {
            const double value = values[static_cast<std::size_t>(i)];
            if (!drawable(value)) {
                continue;
            }
            if (value < low) {
                low = value;
                lowAt = i;
            }
            if (value > high) {
                high = value;
                highAt = i;
            }
        }
        if (lowAt < 0) {
            // The whole column is a gap.
            closeRun();
            continue;
        }

        const double px = (xStart + static_cast<double>(i0) * xStep - view_.xMin)
                          / xSpan * w;
        if (lowAt <= highAt) {
            place(px, toY(low));
            if (highAt != lowAt) {
                place(px, toY(high));
            }
        } else {
            place(px, toY(high));
            place(px, toY(low));
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
    emptyLines_ = 0;

    const int series = source_ == nullptr ? 0 : source_->seriesCount();
    // Where each line's runs start, so the colouring loops below can find
    // them. A member rather than a local: at ten thousand lines this is forty
    // kilobytes, and allocating it per frame would put the harness's own
    // bookkeeping into the column that measures the renderer.
    runStart_.assign(static_cast<std::size_t>(series) + 1, 0);
    for (int line = 0; line < series; ++line) {
        runStart_[static_cast<std::size_t>(line)] = static_cast<int>(runs_.size());
        if (project(source_->line(line)) == 0) {
            ++emptyLines_;
        }
    }
    runStart_[static_cast<std::size_t>(series)] = static_cast<int>(runs_.size());

    vertices_ = static_cast<int>(points_.size());
    // Queued: updatePaintNode runs inside the render pass, and anything that
    // reacted to this by touching the scene from the same stack would be
    // changing the graph while it is being walked.
    QMetaObject::invokeMethod(this, &PlotItem::drew, Qt::QueuedConnection);

    if (batching_ == Batching::Batched) {
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
            auto* geometry = new QSGGeometry(
                QSGGeometry::defaultAttributes_ColoredPoint2D(), 0);
            geometry->setDrawingMode(QSGGeometry::DrawLines);
            node->setGeometry(geometry);
            node->setFlag(QSGNode::OwnsGeometry);
            auto* material = new QSGVertexColorMaterial;
            node->setMaterial(material);
            node->setFlag(QSGNode::OwnsMaterial);
            root->appendChildNode(node);
        }
        auto* node = static_cast<QSGGeometryNode*>(root->childAtIndex(0));
        QSGGeometry* geometry = node->geometry();
        geometry->allocate(segments * 2);
        auto* vertex = geometry->vertexDataAsColoredPoint2D();

        int at = 0;
        for (int line = 0; line < series; ++line) {
            const QColor colour = seriesColour(line, series);
            const auto red = static_cast<uchar>(colour.red());
            const auto green = static_cast<uchar>(colour.green());
            const auto blue = static_cast<uchar>(colour.blue());
            for (int r = runStart_[static_cast<std::size_t>(line)];
                 r < runStart_[static_cast<std::size_t>(line) + 1]; ++r) {
                const Run& run = runs_[static_cast<std::size_t>(r)];
                for (int i = 0; i < run.count - 1; ++i) {
                    const QPointF& a = points_[static_cast<std::size_t>(run.first + i)];
                    const QPointF& b = points_[static_cast<std::size_t>(run.first + i + 1)];
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
        auto* geometry =
            new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 0);
        geometry->setDrawingMode(QSGGeometry::DrawLineStrip);
        // One device pixel. Anything wider is not portable through the RHI --
        // line width above 1 is an optional feature and several backends
        // silently ignore it -- so a thicker stroke has to be built from
        // triangles, which is work this spike does not need to do to answer
        // the question it was written for.
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
        const QColor colour = seriesColour(line, series);
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

} // namespace spike
