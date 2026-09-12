// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick

/// An x axis stated as start, step and stop, with at most two of the three
/// stated at once and the third following from them.
///
/// This was PlotSurface's, inline, and it is here because a second plot now
/// asks the same question. The arithmetic is the part worth having in one
/// place: it is small, it is not obvious, and two copies of it would drift the
/// first time one of them was corrected.
///
/// The three numbers are the x values themselves, not a window onto them.
/// Element i of the data is drawn at `start + i * step`, and `stop` is where
/// the axis ends -- so these say what the columns of the table *are*, which is
/// the question a dataset that is a measurement against something asks.
///
/// Any two of them determine the third, and the third is computed rather than
/// typed, so they can never contradict each other. The default is what a
/// reader would write for data with no x of its own: 0 : 1 : len(data), the
/// element's own index, which is exactly what the grid's column headers count.
///
/// The upper bound is exclusive, as it is everywhere else in this program --
/// the data settings panel's index expressions are numpy's and h5py's -- so
/// `stop = start + step * len`, and 0 : 1 : len(data) is len(data) points in
/// both places.
QtObject {
    id: axis

    property real start: 0.0
    property real step: 1.0
    property real stop: 1.0

    /// Which of "start", "step", "stop" the reader has stated, oldest first.
    /// At most two: a third pushes out the oldest, so the two most recent
    /// edits are always the ones in force and nothing has to be released
    /// before it can be typed over.
    property var locks: []

    /// How long the data is, in elements. `len(data)` in the default above.
    property int length: 1

    /// Kept for readers that only want to know whether the axis is the data's
    /// own.
    readonly property bool automatic: axis.locks.length === 0

    /// Whether the three numbers describe an axis at all. A step of zero has
    /// no ticks and a stop below its start has no extent; either is something
    /// the reader typed on the way to something else, so it is reported rather
    /// than drawn.
    readonly property bool valid: axis.resolved.step > 0
                                  && axis.resolved.stop > axis.resolved.start
                                  && isFinite(axis.resolved.start)
                                  && isFinite(axis.resolved.step)
                                  && isFinite(axis.resolved.stop)

    function locked(which) {
        return axis.locks.indexOf(which) !== -1
    }

    /// State `which` at the value the reader just entered, dropping the oldest
    /// of the two already stated if that would make three.
    function lock(which) {
        if (axis.locked(which))
            return
        const next = axis.locks.concat([which])
        axis.locks = next.length > 2 ? next.slice(next.length - 2) : next
    }

    function unlock(which) {
        axis.locks = axis.locks.filter(entry => entry !== which)
    }

    function setLocked(which, on) {
        if (on)
            axis.lock(which)
        else
            axis.unlock(which)
    }

    /// Back to 0 : 1 : len(data), which is what releasing every statement
    /// means.
    function release() {
        axis.locks = []
    }

    /// The three numbers the axis is actually drawn with.
    ///
    /// With two stated the third follows from `stop = start + step * len`.
    /// With one, the default supplies the next one along -- start before step
    /// before stop -- and the third still follows, so there is exactly one
    /// answer whatever the reader has said and nothing is ever derived from a
    /// number they cannot see.
    readonly property var resolved: {
        const n = axis.length

        const hasStart = axis.locked("start")
        const hasStep = axis.locked("step")
        const hasStop = axis.locked("stop")

        if (hasStart && hasStep) {
            return { start: axis.start,
                     step: axis.step,
                     stop: axis.start + axis.step * n }
        }
        if (hasStart && hasStop) {
            return { start: axis.start,
                     step: (axis.stop - axis.start) / n,
                     stop: axis.stop }
        }
        if (hasStep && hasStop) {
            return { start: axis.stop - axis.step * n,
                     step: axis.step,
                     stop: axis.stop }
        }
        if (hasStart) {
            return { start: axis.start,
                     step: 1.0,
                     stop: axis.start + n }
        }
        if (hasStep) {
            return { start: 0.0,
                     step: axis.step,
                     stop: axis.step * n }
        }
        if (hasStop) {
            return { start: 0.0,
                     step: axis.stop / n,
                     stop: axis.stop }
        }
        return { start: 0.0, step: 1.0, stop: n }
    }

    /// The x bounds the axis takes. A trio that does not describe an axis falls
    /// back to the default one, so a half-typed number leaves the plot standing
    /// rather than blanking it.
    readonly property real minimum: axis.valid ? axis.resolved.start : 0.0
    readonly property real maximum: axis.valid ? axis.resolved.stop : axis.length
}
