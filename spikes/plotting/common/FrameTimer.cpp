// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "common/FrameTimer.hpp"

#include <QtCore/QByteArray>
#include <QtCore/QFile>
#include <QtCore/QTextStream>
#include <QtQuick/QQuickWindow>

#include <algorithm>
#include <cmath>

namespace spike {
namespace {

double percentile(std::vector<double> values, double fraction)
{
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    // Nearest rank. Interpolating between two samples would invent a frame
    // time no frame took, and the question here is what the frames did.
    const auto index = static_cast<std::size_t>(
        std::ceil(fraction * static_cast<double>(values.size())) - 1.0);
    return values[std::min(index, values.size() - 1)];
}

double statusFieldMiB(const char* field)
{
    QFile status(QStringLiteral("/proc/self/status"));
    if (!status.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return -1.0;
    }
    // readAll(), not a QTextStream line loop. A file under /proc reports a
    // size of zero, and QFileDevice::atEnd() is pos() == size() -- so the loop
    // ends before it begins and the whole rss column silently reads "-". It
    // did, for the first run of this benchmark.
    const QByteArray whole = status.readAll();
    const QByteArray wanted = QByteArray(field) + ':';
    for (const QByteArray& line : whole.split('\n')) {
        if (!line.startsWith(wanted)) {
            continue;
        }
        const QList<QByteArray> parts =
            line.simplified().split(' ');
        if (parts.size() >= 2) {
            bool ok = false;
            const double kibibytes = parts.at(1).toDouble(&ok);
            if (ok) {
                return kibibytes / 1024.0;
            }
        }
        return -1.0;
    }
    return -1.0;
}

} // namespace

FrameTimer::FrameTimer(QQuickWindow* window, QObject* parent)
    : QObject(parent)
{
    clock_.start();
    if (window == nullptr) {
        return;
    }
    // Qt::DirectConnection because these are emitted from the render thread
    // when the render loop is threaded. The spikes run the basic loop, where
    // that is the GUI thread anyway -- but a queued connection would record
    // the time the event was *delivered*, which is a different quantity that
    // happens to look plausible.
    connect(window, &QQuickWindow::beforeFrameBegin, this, &FrameTimer::begin,
            Qt::DirectConnection);
    connect(window, &QQuickWindow::afterRendering, this, &FrameTimer::end,
            Qt::DirectConnection);
    // The wall clock is taken from the swap, which is the moment the frame
    // actually became visible.
    connect(window, &QQuickWindow::frameSwapped, this, &FrameTimer::swapped,
            Qt::DirectConnection);
}

void FrameTimer::reset()
{
    cpu_.clear();
    wall_.clear();
    previousEnd_ = -1;
}

void FrameTimer::begin() { frameStart_ = clock_.nsecsElapsed(); }

void FrameTimer::end()
{
    const qint64 now = clock_.nsecsElapsed();
    cpu_.push_back(static_cast<double>(now - frameStart_) / 1e6);
    // Announced here rather than at the swap. frameSwapped is the more
    // faithful moment -- it is when the frame became visible -- and it is not
    // a signal the harness can rely on: with the swap interval at zero on this
    // compositor it does not arrive at all, and a run driven from it waits out
    // its four-second guard on every frame and reports a renderer that drew
    // nothing. afterRendering is emitted whenever a frame was recorded, which
    // is the condition drawOneFrame actually needs.
    Q_EMIT framed(static_cast<int>(cpu_.size()) - 1);
}

void FrameTimer::swapped()
{
    const qint64 now = clock_.nsecsElapsed();
    if (previousEnd_ >= 0) {
        wall_.push_back(static_cast<double>(now - previousEnd_) / 1e6);
    }
    previousEnd_ = now;
}

FrameStats FrameTimer::stats() const
{
    FrameStats out;
    out.frames = static_cast<int>(cpu_.size());
    out.cpuMedian = percentile(cpu_, 0.50);
    out.cpuP95 = percentile(cpu_, 0.95);
    out.cpuWorst = cpu_.empty() ? 0.0 : *std::max_element(cpu_.begin(), cpu_.end());
    out.wallMedian = percentile(wall_, 0.50);
    out.wallP95 = percentile(wall_, 0.95);
    return out;
}

double peakResidentMiB() { return statusFieldMiB("VmHWM"); }

double residentMiB() { return statusFieldMiB("VmRSS"); }

} // namespace spike
