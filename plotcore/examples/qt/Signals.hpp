// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// What the example plots: signals made to show one thing each, and the two
// file formats a reader is most likely to have a line in.

#include <optional>
#include <string>
#include <vector>

namespace demo {

struct Signal
{
    std::string name;
    std::vector<double> values;
};

/// A sine with one sample a hundred times its height -- the spike an envelope
/// keeps and a stride throws away.
Signal spikedSine(long long count);
/// A square wave, whose envelope is a band and whose samples are two values.
Signal squareWave(long long count);
/// A ramp with runs of NaN in it: every gap ends a stroke.
Signal withGaps(long long count);
/// Twelve decades from 1e-6 to 1e6, for a logarithmic axis.
Signal manyDecades(long long count);
/// A random walk with a one-sample spike every 997 elements.
Signal randomWalk(long long count, unsigned seed);
/// Ten million samples of a chirp with noise and spikes in it.
Signal tenMillion();

/// A time base that only goes one way, at a steady rate...
std::vector<double> regularTime(long long count, double step);
/// ...and one that only goes one way at a rate that wanders.
std::vector<double> irregularTime(long long count, unsigned seed);

/// The lines in a .npy file: a 1-D array is one, a 2-D array one per column.
/// Floats and integers of any width, either byte order. Nothing, with `error`
/// set, for anything else.
std::optional<std::vector<Signal>> loadNpy(const std::string& path, std::string& error);
/// The lines in a CSV file, one per numeric column; a first row that is not
/// numbers names them.
std::optional<std::vector<Signal>> loadCsv(const std::string& path, std::string& error);

} // namespace demo
