// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

// Writes the example HDF5 files the viewer is exercised against, into the
// directory named on the command line (the working directory by default).
//
// The files themselves are described in ExampleFile.cpp, which
// tests/test_example.cpp shares so that the suite asserts against exactly the
// file this program writes.

#include "ExampleFile.hpp"

#include <cstdio>
#include <exception>
#include <filesystem>

int main(int argc, char** argv)
{
    try {
        const std::filesystem::path directory = (argc > 1) ? argv[1] : ".";
        h5example::writeExampleFiles(directory);
        for (const char* name :
             {"example.h5", "example_external.h5", "example_raw.bin"}) {
            std::printf("wrote %s\n", (directory / name).string().c_str());
        }
        return 0;
    }
    catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
