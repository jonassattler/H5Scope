// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Per-frame timing, split into the two numbers that mean different things.
//
//   cpu    beforeFrameBegin -> afterRendering. Polish, synchronise and record:
//          everything the renderer does to turn data into draw calls, which is
//          the part that grows with the data and the only part a renderer can
//          be held responsible for.
//   wall   frameSwapped to frameSwapped. What a reader would feel, vsync and
//          compositor included.
//
// The split is not fastidiousness. afterFrameEnd comes after the present, and
// on a window the compositor is not consuming -- a headless session, a
// minimised window, a remote display -- the swap blocks for a second or two at
// a time. Measured through afterFrameEnd, every renderer here scores about a
// thousand milliseconds a frame and they are indistinguishable; measured
// through afterRendering, the frames are tenths of a millisecond and the
// differences between them are the answer. The one that includes the swap is
// still reported, as `wall`, where it belongs.
//
// Reported as p50 and p95 rather than a mean. A plot that draws in four
// milliseconds and stalls for ninety every twentieth frame has a fine mean and
// is unusable, and the mean is exactly the statistic that hides it.
//
// Frames are counted from the signals Qt Quick emits around its own render
// pass, not from a timer around update(): with a threaded render loop the two
// are not the same thing, and the spikes are driven with QSG_RENDER_LOOP=basic
// precisely so that they can be.

#include <QtCore/QElapsedTimer>
#include <QtCore/QObject>

#include <vector>

QT_BEGIN_NAMESPACE
class QQuickWindow;
QT_END_NAMESPACE

namespace spike {

struct FrameStats {
    int frames = 0;
    double cpuMedian = 0.0;
    double cpuP95 = 0.0;
    double cpuWorst = 0.0;
    double wallMedian = 0.0;
    double wallP95 = 0.0;
};

class FrameTimer : public QObject
{
    Q_OBJECT

public:
    explicit FrameTimer(QQuickWindow* window, QObject* parent = nullptr);

    /// Throw away what has been collected and start again. Called after the
    /// warm-up frames, which measure the shader cache and the first upload
    /// rather than the renderer.
    void reset();

    /// Frames recorded since the last reset().
    [[nodiscard]] int frames() const { return static_cast<int>(cpu_.size()); }

    /// Milliseconds inside the most recent completed frame.
    [[nodiscard]] double lastCpu() const { return cpu_.empty() ? 0.0 : cpu_.back(); }

    [[nodiscard]] FrameStats stats() const;

Q_SIGNALS:
    /// Emitted after each completed frame, so a scripted run can advance its
    /// gesture exactly once per frame rather than once per timer tick.
    void framed(int index);

private:
    void begin();
    void end();
    void swapped();

    QElapsedTimer clock_;
    qint64 frameStart_ = 0;
    qint64 previousEnd_ = -1;
    std::vector<double> cpu_;
    std::vector<double> wall_;
};

/// Peak resident set size in mebibytes, from VmHWM in /proc/self/status.
/// Negative where the file does not exist, which report() prints as "-" rather
/// than as a confident zero.
[[nodiscard]] double peakResidentMiB();

/// Resident set size right now, likewise from VmRSS.
[[nodiscard]] double residentMiB();

} // namespace spike
