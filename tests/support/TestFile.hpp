// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <filesystem>
#include <string>

namespace h5test {

/// Creates an HDF5 file on disk in a unique temp directory and removes it on
/// destruction. Fixtures are generated at test time rather than committed as
/// binaries, so the suite stays readable and diffable.
class TempFile
{
public:
    explicit TempFile(std::string stem);
    ~TempFile();

    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;

    [[nodiscard]] const std::string& path() const noexcept { return path_; }

private:
    std::filesystem::path directory_;
    std::string path_;
};

/// Build the shared fixture covering the datatype and layout matrix the viewer
/// must handle. Structure:
///
///   /scalar_int              scalar int32, attr "units" = "kelvin"
///   /vec_int                 1-D int32 [5]
///   /matrix                  2-D float64 [4x3]
///   /cube                    3-D int32 [2x3x4]
///   /hypercube               4-D int32 [2x3x4x5]
///   /str_fixed               1-D fixed string [3]
///   /str_vlen                1-D variable string [3]
///   /str_scalar              scalar variable string, multi-line and long
///   /str_grid                2-D variable string [2x2]
///   /compound                1-D compound {id:int32, value:float64} [2]
///   /enum                    1-D enum {RED,GREEN,BLUE} [3]
///   /compressed              2-D int32 [100x100], chunked + gzip
///   /long_vec                1-D int32 [1000] (exceeds the view block size)
///   /empty                   1-D int32 [0]
///   /group                   group, attrs "title" and "version"
///   /group/nested            group
///   /group/nested/leaf       1-D int32 [2]
///   /link_to_matrix          hard link to /matrix (cycle-detection input)
///
void writeFixture(const std::string& path);

} // namespace h5test
