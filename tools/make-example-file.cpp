// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

// Writes the example HDF5 files the viewer is exercised against, into the
// directory named on the command line (the working directory by default).
//
// The files themselves are described in ExampleFile.cpp, which
// tests/test_example.cpp shares so that the suite asserts against exactly the
// file this program writes.
//
// `--scale` additionally writes example_scale.h5: the same kinds of object as
// example.h5 holds one of, in the tens of thousands, over a few gigabytes of
// storage. It is not written by default because it takes a minute and most of
// a disk, and nothing in the test suite reads it -- tools/bench-tree does.

#include "ExampleFile.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <string>

namespace {

void usage(const char* program)
{
    std::fprintf(stderr,
                 "usage: %s [directory] [--scale] [--runs N] [--flat N]\n"
                 "\n"
                 "  directory   where to write (default: the working directory)\n"
                 "  --scale     also write example_scale.h5, the many-object file\n"
                 "  --runs N    acquisition groups in the scale file (default 240)\n"
                 "  --flat N    members of its one flat group (default 8192)\n"
                 "  --sessions N  groups holding many members each (default 256)\n"
                 "  --small     a scale file a tenth the size, for a quick check\n",
                 program);
}

/// The number after `--flag`, or -1 when it is missing or not a number. A
/// generator's command line is not worth a parser; it is worth being told
/// clearly when it was typed wrong, which is what the -1 is for.
int intOption(int argc, char** argv, int& i)
{
    if (i + 1 >= argc) {
        return -1;
    }
    char* end = nullptr;
    const long value = std::strtol(argv[++i], &end, 10);
    if (end == argv[i] || *end != '\0' || value <= 0) {
        return -1;
    }
    return static_cast<int>(value);
}

} // namespace

int main(int argc, char** argv)
{
    std::filesystem::path directory = ".";
    bool scale = false;
    h5example::ScaleSpec spec;

    for (int i = 1; i < argc; ++i) {
        const std::string_view argument = argv[i];
        if (argument == "--scale") {
            scale = true;
        } else if (argument == "--small") {
            scale = true;
            spec.runs = 24;
            spec.flatChildren = 1024;
            spec.sessions = 32;
        } else if (argument == "--runs") {
            spec.runs = intOption(argc, argv, i);
            scale = true;
        } else if (argument == "--flat") {
            spec.flatChildren = intOption(argc, argv, i);
            scale = true;
        } else if (argument == "--sessions") {
            spec.sessions = intOption(argc, argv, i);
            scale = true;
        } else if (argument == "--help" || argument == "-h") {
            usage(argv[0]);
            return 0;
        } else if (argument.starts_with("--")) {
            usage(argv[0]);
            return 2;
        } else {
            directory = argument;
        }
        if (spec.runs < 0 || spec.flatChildren < 0 || spec.sessions < 0) {
            usage(argv[0]);
            return 2;
        }
    }

    try {
        h5example::writeExampleFiles(directory);
        for (const char* name :
             {"example.h5", "example_external.h5", "example_raw.bin"}) {
            std::printf("wrote %s\n", (directory / name).string().c_str());
        }

        if (scale) {
            const auto path = directory / "example_scale.h5";
            const std::size_t bytes = h5example::writeScaleFile(path, spec);
            std::printf("wrote %s (%.2f GB)\n", path.string().c_str(),
                        static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
        }
        return 0;
    }
    catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
