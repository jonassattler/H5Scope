// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "gui/PlotItem.hpp"

#include <QtCore/QMetaObject>
#include <QtCore/QVariantMap>
#include <QtGui/QImage>
#include <QtGui/QPainter>
#include <QtGui/QPen>
#include <QtQuick/QQuickWindow>
#include <QtQuick/QSGGeometryNode>
#include <QtQuick/QSGNode>
#include <QtQuick/QSGRendererInterface>
#include <QtQuick/QSGSimpleTextureNode>
#include <QtQuick/QSGVertexColorMaterial>

#include <algorithm>
#include <cmath>
#include <limits>

namespace gui {
namespace {

// The most stroke vertices one frame may submit.
//
// The envelope bounds each *line* by the pane's width, which is what makes a
// ten-million-point dataset draw in under a millisecond. It does not bound
// their sum: ten thousand lines at two points a column is forty million points
// however narrow the pane, and the evaluation branch measured exactly that
// arrangement reaching 3871 MiB.
//
// So the budget is divided between the lines and each gets a share of the
// columns. The number is set from the frame rather than from the memory:
// building a stroke measures at about six nanoseconds a vertex on this
// machine, so two million of them is a little over a third of a frame at sixty
// hertz, with the projection and the upload still to pay for. At twenty bytes
// apiece it is also forty megabytes of buffer, which is the smaller of the two
// reasons to stop there.
//
// It bites only above about five hundred lines. The default sixty-four draws
// at one column a pixel on any pane; past the ceiling a line loses horizontal
// resolution and keeps its extremes, because that is what an envelope does --
// nothing goes missing, a spike may sit a few pixels from where it fell. Ten
// thousand overlapping hairlines is a picture of a distribution rather than of
// a line, and this is the honest way to lose detail nobody could have seen.
constexpr int kMaxVertices = 2 << 20;

/// The colour a vertex carries: the line's own, multiplied by its strength and
/// premultiplied, which is the convention the scene graph blends in.
struct Ink
{
    uchar red = 0;
    uchar green = 0;
    uchar blue = 0;
    uchar alpha = 0;
};

Ink inkFor(const PlotLine& line)
{
    const double alpha = std::clamp(line.colour.alphaF() * line.opacity, 0.0, 1.0);
    const auto channel = [&](int value) {
        return static_cast<uchar>(std::lround(static_cast<double>(value) * alpha));
    };
    return {channel(line.colour.red()), channel(line.colour.green()), channel(line.colour.blue()),
            static_cast<uchar>(std::lround(alpha * 255.0))};
}

} // namespace

PlotItem::PlotItem(QQuickItem* parent) : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
}

PlotItem::~PlotItem() = default;

void PlotItem::setLines(std::vector<PlotLine> lines, const PlotAxis& axis)
{
    lines_ = std::move(lines);
    // Whatever this item owned, it no longer draws. Unconditional and safe:
    // every caller of this is a model handing over values it owns itself, so
    // the lines just installed cannot be pointing into the storage below.
    // adopt(), which is the one case where they can, sets the members itself.
    owned_.clear();
    axis_ = axis;
    update();
}

void PlotItem::clear()
{
    lines_.clear();
    owned_.clear();
    axis_ = PlotAxis{};
    update();
}

void PlotItem::adopt(PlotItem* source)
{
    if (source == nullptr || source == this) {
        return;
    }

    // Built beside the old storage rather than into it, so that a second
    // adopt() over the same item cannot free what the lines being copied are
    // still pointing at -- which is exactly what happens when the source *is*
    // an item this one adopted from before.
    std::vector<std::vector<double>> owned;
    owned.reserve(source->lines_.size() + 2);

    std::vector<PlotLine> lines = source->lines_;
    for (PlotLine& line : lines) {
        if (line.values == nullptr || line.count <= 0) {
            line.values = nullptr;
            line.count = 0;
            continue;
        }
        owned.emplace_back(line.values, line.values + line.count);
        line.values = owned.back().data();
    }

    // The axis borrows twice: the whole time base, and the run of it the
    // reader has zoomed into. A picture drawn against only the first would be
    // the staircase of vertical treads that the closer look exists to fix.
    PlotAxis axis = source->axis_;
    if (axis.values != nullptr && axis.count > 0) {
        owned.emplace_back(axis.values, axis.values + axis.count);
        axis.values = owned.back().data();
    }
    else {
        axis.values = nullptr;
        axis.count = 0;
    }
    if (axis.closerValues != nullptr && axis.closerCount > 0) {
        owned.emplace_back(axis.closerValues, axis.closerValues + axis.closerCount);
        axis.closerValues = owned.back().data();
    }
    else {
        axis.closerValues = nullptr;
        axis.closerCount = 0;
    }

    // Markers travel with the lines. They are a statement about the data --
    // "this dot is a measurement somebody took" -- so a picture of the plot
    // that dropped them would be saying less than the plot does.
    markers_ = source->markers_;
    markerSize_ = source->markerSize_;

    // Assigned here rather than through setLines(), which frees `owned_` --
    // correctly, for every other caller, and fatally for this one.
    owned_ = std::move(owned);
    lines_ = std::move(lines);
    axis_ = axis;
    update();
    emit markersChanged();
}

int PlotItem::lineCount() const
{
    return static_cast<int>(lines_.size());
}

QColor PlotItem::seriesColor(int index) const
{
    if (index < 0 || index >= lineCount()) {
        return {};
    }
    return lines_[static_cast<std::size_t>(index)].colour;
}

void PlotItem::setSeriesColor(int index, const QColor& colour)
{
    if (index < 0 || index >= lineCount()) {
        return;
    }
    PlotLine& line = lines_[static_cast<std::size_t>(index)];
    if (line.colour != colour) {
        line.colour = colour;
        update();
    }
}

void PlotItem::setSeriesOpacity(int index, double opacity)
{
    if (index < 0 || index >= lineCount()) {
        return;
    }
    PlotLine& line = lines_[static_cast<std::size_t>(index)];
    if (line.opacity != opacity) {
        line.opacity = opacity;
        update();
    }
}

void PlotItem::setSeriesWidth(int index, double width)
{
    if (index < 0 || index >= lineCount()) {
        return;
    }
    PlotLine& line = lines_[static_cast<std::size_t>(index)];
    if (line.width != width) {
        line.width = width;
        update();
    }
}

void PlotItem::setSeriesYRange(int index, double low, double high)
{
    if (index < 0 || index >= lineCount()) {
        return;
    }
    PlotLine& line = lines_[static_cast<std::size_t>(index)];
    if (!line.ownY || line.yMin != low || line.yMax != high) {
        line.ownY = true;
        line.yMin = low;
        line.yMax = high;
        update();
    }
}

void PlotItem::clearSeriesYRange(int index)
{
    if (index < 0 || index >= lineCount()) {
        return;
    }
    PlotLine& line = lines_[static_cast<std::size_t>(index)];
    if (line.ownY) {
        line.ownY = false;
        update();
    }
}

bool PlotItem::seriesHasOwnY(int index) const
{
    return index >= 0 && index < lineCount() && lines_[static_cast<std::size_t>(index)].ownY;
}

double PlotItem::seriesYFraction(int index, double value) const
{
    if (index < 0 || index >= lineCount()) {
        return yFraction(value);
    }
    return yFractionOf(value, lineView(lines_[static_cast<std::size_t>(index)], view_));
}

void PlotItem::setXMin(double value)
{
    if (view_.xMin != value) {
        view_.xMin = value;
        Q_EMIT viewChanged();
        update();
    }
}

void PlotItem::setXMax(double value)
{
    if (view_.xMax != value) {
        view_.xMax = value;
        Q_EMIT viewChanged();
        update();
    }
}

void PlotItem::setYMin(double value)
{
    if (view_.yMin != value) {
        view_.yMin = value;
        Q_EMIT viewChanged();
        update();
    }
}

void PlotItem::setYMax(double value)
{
    if (view_.yMax != value) {
        view_.yMax = value;
        Q_EMIT viewChanged();
        update();
    }
}

void PlotItem::setXLog(bool on)
{
    if (view_.xLog != on) {
        view_.xLog = on;
        Q_EMIT viewChanged();
        update();
    }
}

void PlotItem::setYLog(bool on)
{
    if (view_.yLog != on) {
        view_.yLog = on;
        Q_EMIT viewChanged();
        update();
    }
}

void PlotItem::setXLogBase(double base)
{
    if (view_.xLogBase != base) {
        view_.xLogBase = base;
        Q_EMIT viewChanged();
        update();
    }
}

void PlotItem::setYLogBase(double base)
{
    if (view_.yLogBase != base) {
        view_.yLogBase = base;
        Q_EMIT viewChanged();
        update();
    }
}

void PlotItem::setMarkers(bool on)
{
    if (markers_ != on) {
        markers_ = on;
        Q_EMIT markersChanged();
        update();
    }
}

void PlotItem::setMarkerSize(double size)
{
    if (markerSize_ != size) {
        markerSize_ = size;
        Q_EMIT markersChanged();
        update();
    }
}

double PlotItem::yFraction(double value) const
{
    return yFractionOf(value, view_);
}

double PlotItem::valueAt(double fraction) const
{
    // The exact inverse of yFraction(), which is the one piece of arithmetic
    // the chrome and the curve have to agree about.
    return yMappingOf(view_).valueAt(fraction);
}

double PlotItem::xFraction(double x) const
{
    return xFractionOf(x, view_);
}

double PlotItem::xAt(double fraction) const
{
    return xMappingOf(view_).valueAt(fraction);
}

QVariantMap PlotItem::nearestSample(double px, double py) const
{
    QVariantMap answer;
    answer.insert(QStringLiteral("valid"), false);

    const double w = width();
    const double h = height();
    const AxisMapping xMap = xMappingOf(view_);
    if (!(w > 0.0) || !(h > 0.0) || !xMap.usable || lines_.empty()) {
        return answer;
    }
    // In the data's own units, through the axis's own scale -- so on a
    // logarithmic axis the pointer resolves to the x it is actually over
    // rather than to the one it would be over if the axis were linear.
    const double wanted = xMap.valueAt(px / w);

    double bestDistance = std::numeric_limits<double>::infinity();
    int bestLine = -1;
    double bestX = 0.0;
    double bestY = 0.0;
    double bestPx = 0.0;
    double bestPy = 0.0;

    // Per line, because a line on an axis of its own is drawn through that
    // axis: snapping it through the common one would put the crosshair's dot
    // somewhere the stroke is not.
    AxisMapping yMap;
    const auto consider = [&](int index, qsizetype at, double x, double value) {
        // A sample the axes cannot place is not a sample the crosshair may snap
        // to: it was not drawn, and a readout of it would name a point that is
        // not on the pane.
        if (!xMap.draws(x) || !yMap.draws(value)) {
            return false;
        }
        const double sx = xMap.fractionOf(x) * w;
        const double sy = h - yMap.fractionOf(value) * h;
        const double distance = (sx - px) * (sx - px) + (sy - py) * (sy - py);
        if (distance < bestDistance) {
            bestDistance = distance;
            bestLine = index;
            bestX = x;
            bestY = value;
            bestPx = sx;
            bestPy = sy;
        }
        (void)at;
        return true;
    };

    for (int index = 0; index < lineCount(); ++index) {
        const PlotLine& line = lines_[static_cast<std::size_t>(index)];
        if (line.values == nullptr || line.count <= 0) {
            continue;
        }
        yMap = yMappingOf(lineView(line, view_));

        if (axis_.explicitX() || !(std::abs(line.positionStep * axis_.step) > 0.0)) {
            // A time base need not be monotonic, so there is no index to solve
            // for and the line is searched. Affordable because a line drawn
            // against one is a custom plot entry, and those are thinned on the
            // way out of the file like everything else.
            for (qsizetype i = 0; i < line.count; ++i) {
                const double value = line.values[i];
                if (!std::isfinite(value)) {
                    continue;
                }
                const double x = xOf(line, axis_, i);
                if (std::isfinite(x)) {
                    consider(index, i, x, value);
                }
            }
            continue;
        }

        // x is affine in the index, so the nearest one is arithmetic.
        const double x0 = axis_.start + line.positionStart * axis_.step;
        const double dx = line.positionStep * axis_.step;
        const double exact = (wanted - x0) / dx;
        if (!std::isfinite(exact)) {
            continue;
        }
        const auto centre = static_cast<qsizetype>(
            std::clamp(std::llround(exact), 0LL, static_cast<long long>(line.count - 1)));

        // Outward from there until a drawable sample turns up. A gap wider than
        // this is a gap the crosshair declines to reach across, which is the
        // same answer the line itself gives: there is nothing drawn there.
        constexpr qsizetype kReach = 64;
        for (qsizetype step = 0; step <= kReach; ++step) {
            bool found = false;
            for (const qsizetype at : {centre - step, centre + step}) {
                if (at < 0 || at >= line.count) {
                    continue;
                }
                const double value = line.values[at];
                if (!std::isfinite(value)) {
                    continue;
                }
                // What the walk is looking for is a sample that could be
                // *taken*, not one that is merely finite. Stopping on a value
                // the axes declined to place -- every non-positive one on a
                // logarithmic axis -- ended the search having considered
                // nothing, and the crosshair then found no sample at all in a
                // pane full of them.
                found = consider(index, at, xOf(line, axis_, at), value) || found;
            }
            if (found) {
                break;
            }
        }
    }

    if (bestLine < 0) {
        return answer;
    }
    answer.insert(QStringLiteral("valid"), true);
    answer.insert(QStringLiteral("line"), bestLine);
    answer.insert(QStringLiteral("x"), bestX);
    answer.insert(QStringLiteral("y"), bestY);
    answer.insert(QStringLiteral("px"), bestPx);
    answer.insert(QStringLiteral("py"), bestPy);
    return answer;
}

void PlotItem::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        // The envelope is computed to the pane's width, so a resize is a
        // different set of points and not the same ones stretched.
        update();
    }
}

PlotView PlotItem::viewForFrame() const
{
    PlotView view = view_;
    view.width = width();
    view.height = height();
    // What the framebuffer has, rather than what the layout counts in. See
    // PlotView::pixelRatio.
    view.pixelRatio = window() != nullptr ? window()->effectiveDevicePixelRatio() : 1.0;
    // Two stroke vertices per projected point, two points per column, and the
    // budget is counted in stroke vertices.
    const int lines = std::max(lineCount(), 1);
    view.maxColumns = std::max(1, kMaxVertices / (4 * lines));
    return view;
}

void PlotItem::projectAll()
{
    points_.clear();
    runs_.clear();

    const auto lines = static_cast<std::size_t>(lineCount());
    const PlotView view = viewForFrame();
    lineRuns_.assign(lines + 1, 0);
    lineDecimated_.assign(lines, false);
    for (std::size_t line = 0; line < lines; ++line) {
        lineRuns_[line] = static_cast<int>(runs_.size());
        lineDecimated_[line] =
            projectLine(lines_[line], axis_, lineView(lines_[line], view), points_, runs_)
                .decimated;
    }
    lineRuns_[lines] = static_cast<int>(runs_.size());

    drawnPoints_ = static_cast<int>(points_.size());
    drawnRuns_ = static_cast<int>(runs_.size());
    // Queued: updatePaintNode runs inside the render pass, and anything that
    // reacted to this by touching the scene from the same stack would be
    // changing the graph while it is being walked.
    QMetaObject::invokeMethod(this, &PlotItem::drew, Qt::QueuedConnection);
}

QSGNode* PlotItem::updatePaintNode(QSGNode* old, UpdatePaintNodeData*)
{
    QSGNode* root = old;
    if (root == nullptr) {
        root = new QSGNode;
    }

    projectAll();

    const bool software =
        window() != nullptr && window()->rendererInterface() != nullptr &&
        window()->rendererInterface()->graphicsApi() == QSGRendererInterface::Software;
    const Drawn wanted = software ? Drawn::Painted : Drawn::Geometry;
    if (drawn_ != wanted) {
        root->removeAllChildNodes();
        drawn_ = wanted;
    }

    return software ? buildPainted(root) : buildGeometry(root);
}

bool PlotItem::marksLine(std::size_t line) const
{
    return markers_ && markerSize_ > 0.0 && !lineDecimated_[line];
}

QSGNode* PlotItem::buildGeometry(QSGNode* root)
{
    // One node, one draw call, one buffer. Everything is chained into a single
    // triangle strip, and every break in it -- between two runs of one line, or
    // between a line and the next, or between two markers -- is bridged by a
    // pair of repeated vertices, which makes two triangles of no area that
    // rasterise to nothing.
    //
    // The count is worked out here rather than by building into a scratch
    // buffer and measuring it, which would be a second copy of the vertex data
    // at every size. It is exact because the two things that emit vertices
    // both emit a fixed number: strokeRun two per station, markerAt
    // kMarkerSides, and tests/test_plotprojection.cpp asserts both.
    int marked = 0;
    for (std::size_t line = 0; line < lines_.size(); ++line) {
        if (!marksLine(line)) {
            continue;
        }
        for (int r = lineRuns_[line]; r < lineRuns_[line + 1]; ++r) {
            marked += runs_[static_cast<std::size_t>(r)].count;
        }
    }

    const int strips = static_cast<int>(runs_.size()) + marked;
    const int vertices =
        strips > 0 ? 2 * static_cast<int>(points_.size()) + kMarkerSides * marked + 2 * (strips - 1)
                   : 0;

    if (root->childCount() == 0) {
        auto* fresh = new QSGGeometryNode;
        auto* geometry = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0);
        geometry->setDrawingMode(QSGGeometry::DrawTriangleStrip);
        fresh->setGeometry(geometry);
        fresh->setFlag(QSGNode::OwnsGeometry);
        fresh->setMaterial(new QSGVertexColorMaterial);
        fresh->setFlag(QSGNode::OwnsMaterial);
        root->appendChildNode(fresh);
    }
    auto* node = static_cast<QSGGeometryNode*>(root->firstChild());
    QSGGeometry* geometry = node->geometry();
    geometry->allocate(vertices);
    if (vertices == 0) {
        node->markDirty(QSGNode::DirtyGeometry);
        return root;
    }

    auto* vertex = geometry->vertexDataAsColoredPoint2D();
    int at = 0;
    const auto place = [&](const QPointF& point, const Ink& ink) {
        vertex[at].set(static_cast<float>(point.x()), static_cast<float>(point.y()), ink.red,
                       ink.green, ink.blue, ink.alpha);
        ++at;
    };

    bool started = false;
    for (std::size_t line = 0; line < lines_.size(); ++line) {
        const Ink ink = inkFor(lines_[line]);
        const double width = lines_[line].width;
        for (int r = lineRuns_[line]; r < lineRuns_[line + 1]; ++r) {
            const PlotRun& run = runs_[static_cast<std::size_t>(r)];
            // Straight into the buffer. The bridge to the previous strip is
            // built on the first vertex of this one, because that is the first
            // moment it is known -- and it is two vertices, which is also what
            // preserves the strip's parity so the next run is wound like a
            // fresh one.
            bool opening = true;
            strokeRunInto(&points_[static_cast<std::size_t>(run.first)], run.count, width,
                          [&](double x, double y) {
                              if (opening) {
                                  opening = false;
                                  if (started) {
                                      vertex[at] = vertex[at - 1];
                                      ++at;
                                      place(QPointF(x, y), ink);
                                  }
                              }
                              place(QPointF(x, y), ink);
                          });
            if (!opening) {
                started = true;
            }
        }
    }

    // The markers, after every line, so a dot is never drawn under a stroke it
    // belongs to.
    for (std::size_t line = 0; line < lines_.size(); ++line) {
        if (!marksLine(line)) {
            continue;
        }
        const Ink ink = inkFor(lines_[line]);
        for (int r = lineRuns_[line]; r < lineRuns_[line + 1]; ++r) {
            const PlotRun& run = runs_[static_cast<std::size_t>(r)];
            for (int i = 0; i < run.count; ++i) {
                stroke_.clear();
                markerAt(points_[static_cast<std::size_t>(run.first + i)], markerSize_ / 2.0,
                         stroke_);
                if (started) {
                    vertex[at] = vertex[at - 1];
                    ++at;
                    place(stroke_.front(), ink);
                }
                for (const QPointF& point : stroke_) {
                    place(point, ink);
                }
                started = true;
            }
        }
    }
    // A run that produced no stroke would leave the tail of the buffer
    // unwritten, and an unwritten vertex is whatever the allocation happened to
    // hold. Collapse the strip onto its last real vertex rather than ship it.
    while (at < vertices) {
        vertex[at] = vertex[at > 0 ? at - 1 : 0];
        ++at;
    }

    node->markDirty(QSGNode::DirtyGeometry);
    return root;
}

QSGNode* PlotItem::buildPainted(QSGNode* root)
{
    QQuickWindow* target = window();
    const qreal ratio = target != nullptr ? target->effectiveDevicePixelRatio() : 1.0;
    const int pixelWidth = std::max(1, static_cast<int>(std::lround(width() * ratio)));
    const int pixelHeight = std::max(1, static_cast<int>(std::lround(height() * ratio)));

    QImage image(pixelWidth, pixelHeight, QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(ratio);
    image.fill(Qt::transparent);

    {
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing, true);
        for (std::size_t line = 0; line < lines_.size(); ++line) {
            QColor colour = lines_[line].colour;
            colour.setAlphaF(std::clamp(colour.alphaF() * lines_[line].opacity, 0.0, 1.0));
            // Round joins rather than the mitred ones the geometry path builds.
            // QPainter's miter limit is its own, and the two only have to agree
            // about where the line goes, not about how a corner is finished.
            painter.setPen(
                QPen(colour, lines_[line].width, Qt::SolidLine, Qt::FlatCap, Qt::RoundJoin));
            for (int r = lineRuns_[line]; r < lineRuns_[line + 1]; ++r) {
                const PlotRun& run = runs_[static_cast<std::size_t>(r)];
                painter.drawPolyline(&points_[static_cast<std::size_t>(run.first)], run.count);
            }
            if (!marksLine(line)) {
                continue;
            }
            painter.setBrush(colour);
            painter.setPen(Qt::NoPen);
            for (int r = lineRuns_[line]; r < lineRuns_[line + 1]; ++r) {
                const PlotRun& run = runs_[static_cast<std::size_t>(r)];
                for (int i = 0; i < run.count; ++i) {
                    painter.drawEllipse(points_[static_cast<std::size_t>(run.first + i)],
                                        markerSize_ / 2.0, markerSize_ / 2.0);
                }
            }
            painter.setBrush(Qt::NoBrush);
        }
    }

    auto* node = static_cast<QSGSimpleTextureNode*>(root->firstChild());
    if (node == nullptr) {
        node = new QSGSimpleTextureNode;
        node->setOwnsTexture(true);
        node->setFiltering(QSGTexture::Linear);
        root->appendChildNode(node);
    }
    // A texture per frame, which would be indefensible on the path that draws
    // for a reader and is nothing at all on this one: the software renderer is
    // what a headless run gets, and a headless run draws each frame once.
    if (target != nullptr) {
        node->setTexture(
            target->createTextureFromImage(image, QQuickWindow::TextureHasAlphaChannel));
    }
    node->setRect(0.0, 0.0, width(), height());
    node->markDirty(QSGNode::DirtyMaterial);
    return root;
}

} // namespace gui
