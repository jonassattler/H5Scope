// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <cstddef>
#include <filesystem>

namespace h5example {

/// Write example.h5, example_external.h5 and example_raw.bin into `directory`,
/// creating it if it does not exist. Throws std::runtime_error on failure.
///
/// The three files are one artefact: example.h5 links into the other two, so
/// moving one without the others leaves broken links behind -- which is itself
/// one of the states the file exists to exercise.
void writeExampleFiles(const std::filesystem::path& directory);

/// The shape of the scale file. Every figure is a count of objects rather than
/// a count of bytes, because what a tree costs is objects: the bytes only
/// decide how far apart on disk they end up.
///
/// The defaults describe the file the viewer was found to be slow on -- a few
/// gigabytes spread over some eighteen thousand objects, most of them datasets
/// carrying attributes. A benchmark that wants a smaller one scales `runs` and
/// `flatChildren` down; nothing else has to move with them.
struct ScaleSpec
{
    /// Top-level acquisition groups. The tree's first expansion is this many
    /// rows, and the readout beside each is a member count.
    int runs = 240;
    /// Instrument groups per run, and channels per instrument. The product
    /// with `runs` is how many datasets carry real data.
    int detectorsPerRun = 4;
    int channelsPerDetector = 6;
    /// Scalar settings datasets per run, in a group of their own. Small
    /// objects in a deep place: the case where a tree pays for the walk and
    /// not for the read.
    int settingsPerRun = 12;
    /// Children of one flat group -- the widest single listing in the file,
    /// and the one that decides whether expanding a node is instant.
    int flatChildren = 8192;
    /// A level of groups that each hold many members. Listing *this* level is
    /// the case a tree gets quadratically wrong: a row that says how many
    /// members its group has, taken by counting them, costs the whole of the
    /// level below for every row of the level above. Nothing else in the file
    /// has that shape and it is the shape real acquisition files have.
    int sessions = 256;
    int framesPerSession = 64;
    /// Shape of one channel's data, in elements of float32. The default is
    /// 100 x 100 x 10, which is 400 KB per dataset and puts the whole file a
    /// little over two gigabytes at the default counts.
    std::size_t frames = 10;
    std::size_t rows = 100;
    std::size_t columns = 100;
    /// Attributes per channel dataset. A tree that shows an attribute tag has
    /// to ask every row about them, so a file with none of them would not
    /// exercise the path that costs.
    int attributesPerChannel = 4;
    /// Deflate the channel data. Off by default: the point of the file is that
    /// it is large, and a benchmark that reads no elements is not helped by
    /// making the ones it does not read smaller.
    bool compress = false;
};

/// Write a large, many-object HDF5 file at `path` and return its size in bytes.
///
/// Deliberately separate from writeExampleFiles(): example.h5 is read by the
/// whole test suite and is written for every run of it, so what belongs there
/// is one of everything rather than a lot of anything. This is the other file
/// -- nothing in it that example.h5 does not already have a case for, and
/// thousands of each.
std::size_t writeScaleFile(const std::filesystem::path& path, const ScaleSpec& spec = {});

} // namespace h5example
