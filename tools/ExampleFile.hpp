// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <filesystem>

namespace h5example {

/// Write example.h5, example_external.h5 and example_raw.bin into `directory`,
/// creating it if it does not exist. Throws std::runtime_error on failure.
///
/// The three files are one artefact: example.h5 links into the other two, so
/// moving one without the others leaves broken links behind -- which is itself
/// one of the states the file exists to exercise.
void writeExampleFiles(const std::filesystem::path& directory);

} // namespace h5example
