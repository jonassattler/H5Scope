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

// The most stroke vertices one frame may submit, which at twenty bytes apiece
// is eighty megabytes of vertex buffer.
//
// The envelope bounds each *line* by the pane's width, which is what makes a
// ten-million-point dataset draw in under a millisecond. It does not bound
// their sum: ten thousand lines at two points a column is forty million points
// however narrow the pane, and the evaluation branch measured exactly that
// arrangement reaching 3871 MiB.
//
// So the budget is divided between the lines and each gets a share of the
// columns. The arithmetic below reaches this ceiling only above about two
// thousand lines, so the default sixty-four -- and a thousand -- are
// unaffected and draw at one column a pixel. Beyond that a line loses
// horizontal resolution and keeps its extremes, because that is what an
// envelope does: nothing goes missing, a spike may sit a few pixels from where
// it fell. Ten thousand overlapping hairlines is a picture of a distribution
// rather than of a line, and this is the honest way to lose detail nobody
// could have seen.
constexpr int kMaxVertices = 4 << 20;

/// The colour a vertex carries: the line's own, multiplied by its strength and
/// premultiplied, which is the convention the scene graph blends in.
struct Ink {
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
    return {channel(line.colour.red()), channel(line.colour.green()),
            channel(line.colour.blue()),
            static_cast<uchar>(std::lround(alpha * 255.0))};
}

} // namespace

PlotItem::PlotItem(QQuickItem* parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
}

PlotItem::~PlotItem() = default;

void PlotItem::setLines(std::vector<PlotLine> lines, const PlotAxis& axis)
{
    lines_ = std::move(lines);
    axis_ = axis;
    update();
}

void PlotItem::clear()
{
    lines_.clear();
    axis_ = PlotAxis{};
    update();
}

int PlotItem::lineCount() const
{
    return static_cast<int>(lines_.size());
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

void PlotItem::setLogY(bool on)
{
    if (view_.logY != on) {
        view_.logY = on;
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
    if (!view_.logY) {
        return view_.yMin + fraction * (view_.yMax - view_.yMin);
    }
    // The same floor the projection arrives at, and arrived at the same way --
    // see mappingFor() in PlotProjection.cpp. This is the one piece of
    // arithmetic the chrome and the curve have to agree about, so it is written
    // as the exact inverse of yFraction() and nothing else.
    const double high = std::max(view_.yMax, std::numeric_limits<double>::min());
    const double low =
        view_.yMin > 0.0 ? view_.yMin : high * std::pow(10.0, -kLogDecades);
    return std::pow(10.0,
                    std::log10(low)
                        + fraction * (std::log10(high) - std::log10(low)));
}

double PlotItem::xFraction(double x) const
{
    const double span = view_.xMax - view_.xMin;
    return span > 0.0 ? (x - view_.xMin) / span : 0.0;
}

double PlotItem::xAt(double fraction) const
{
    return view_.xMin + fraction * (view_.xMax - view_.xMin);
}

QVariantMap PlotItem::sampleNear(int index, double x) const
{
    QVariantMap answer;
    answer.insert(QStringLiteral("valid"), false);
    if (index < 0 || index >= lineCount()) {
        return answer;
    }
    const PlotLine& line = lines_[static_cast<std::size_t>(index)];
    if (line.values == nullptr || line.count <= 0) {
        return answer;
    }

    // The nearest drawable sample, searched rather than solved: a time base
    // need not be monotonic, and a search that assumed it was would report a
    // reading from the wrong end of the line. A plot holds a couple of thousand
    // points per line and this runs on a pointer move, so the loop costs less
    // than the arithmetic that would replace it.
    double bestDistance = std::numeric_limits<double>::infinity();
    double bestX = 0.0;
    double bestY = 0.0;
    for (qsizetype i = 0; i < line.count; ++i) {
        const double value = line.values[i];
        if (!drawable(value, view_.logY)) {
            continue;
        }
        const double at = xOf(line, axis_, i);
        if (!std::isfinite(at)) {
            continue;
        }
        const double distance = std::abs(at - x);
        if (distance < bestDistance) {
            bestDistance = distance;
            bestX = at;
            bestY = value;
        }
    }
    if (!std::isfinite(bestDistance)) {
        return answer;
    }
    answer.insert(QStringLiteral("valid"), true);
    answer.insert(QStringLiteral("x"), bestX);
    answer.insert(QStringLiteral("y"), bestY);
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
            projectLine(lines_[line], axis_, view, points_, runs_).decimated;
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

    const bool software = window() != nullptr && window()->rendererInterface() != nullptr
                          && window()->rendererInterface()->graphicsApi()
                                 == QSGRendererInterface::Software;
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
        strips > 0 ? 2 * static_cast<int>(points_.size()) + kMarkerSides * marked
                         + 2 * (strips - 1)
                   : 0;

    if (root->childCount() == 0) {
        auto* fresh = new QSGGeometryNode;
        auto* geometry =
            new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0);
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
        vertex[at].set(static_cast<float>(point.x()), static_cast<float>(point.y()),
                       ink.red, ink.green, ink.blue, ink.alpha);
        ++at;
    };

    bool started = false;
    for (std::size_t line = 0; line < lines_.size(); ++line) {
        const Ink ink = inkFor(lines_[line]);
        const double width = lines_[line].width;
        for (int r = lineRuns_[line]; r < lineRuns_[line + 1]; ++r) {
            const PlotRun& run = runs_[static_cast<std::size_t>(r)];
            stroke_.clear();
            strokeRun(&points_[static_cast<std::size_t>(run.first)], run.count, width,
                      stroke_);
            if (stroke_.empty()) {
                continue;
            }
            if (started) {
                // Two degenerate vertices, which is also what preserves the
                // strip's parity so the next run is wound like a fresh one.
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
                markerAt(points_[static_cast<std::size_t>(run.first + i)],
                         markerSize_ / 2.0, stroke_);
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
            colour.setAlphaF(
                std::clamp(colour.alphaF() * lines_[line].opacity, 0.0, 1.0));
            // Round joins rather than the mitred ones the geometry path builds.
            // QPainter's miter limit is its own, and the two only have to agree
            // about where the line goes, not about how a corner is finished.
            painter.setPen(QPen(colour, lines_[line].width, Qt::SolidLine, Qt::FlatCap,
                                Qt::RoundJoin));
            for (int r = lineRuns_[line]; r < lineRuns_[line + 1]; ++r) {
                const PlotRun& run = runs_[static_cast<std::size_t>(r)];
                painter.drawPolyline(&points_[static_cast<std::size_t>(run.first)],
                                     run.count);
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
