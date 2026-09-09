# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-License-Identifier: GPL-3.0-only

# Write a small many-object file and run both benchmarks over it.
#
# A smoke run, not a threshold. Nothing here asserts that anything was fast --
# tests/test_cost.cpp is where the costs are asserted, in counts that mean the
# same thing on every machine, and a wall-clock threshold on a shared CI runner
# would fail for reasons that have nothing to do with this project.
#
# What it is for is that the benchmarks keep working. They are the tools someone
# reaches for when a file is slow, they are not built by the test suites, and a
# tool that has quietly stopped compiling -- or that segfaults on the second
# phase -- is discovered at exactly the wrong moment. Running them once per CI
# build costs a few seconds and means they are never the thing that is broken.
#
# Driven from CMake rather than from a shell script so it is the same test on
# Windows, where the paths have backslashes in them and there is no shell worth
# assuming.

foreach(required MAKE_EXAMPLE BENCH_TREE BENCH_DATA WORK_DIR)
  if(NOT DEFINED ${required})
    message(FATAL_ERROR "RunBenchmarks.cmake: ${required} was not set")
  endif()
endforeach()

set(scale_dir "${WORK_DIR}/benchmark-smoke")
file(REMOVE_RECURSE "${scale_dir}")
file(MAKE_DIRECTORY "${scale_dir}")

# Run one step and stop on the first that fails, with its own output. A
# benchmark that failed silently would leave the suite green and the tool
# broken, which is the whole thing this exists to prevent.
function(run_step what)
  execute_process(
    COMMAND ${ARGN}
    RESULT_VARIABLE status
    OUTPUT_VARIABLE output
    ERROR_VARIABLE errors
  )
  if(NOT status EQUAL 0)
    message("${output}")
    message("${errors}")
    message(FATAL_ERROR "${what} failed with status ${status}")
  endif()
  # Printed on success too: the numbers are the point of running it, and a CI
  # log that has them is a record of what this commit cost on that machine.
  message("--- ${what}")
  message("${output}")
endfunction()

# The smallest file that still has the shapes the benchmarks are about: one
# wide group, a level of groups holding many members each, and real bytes
# between the object headers. Every count is a tenth of what a person would
# run this on, because what is being checked is that it runs at all.
run_step("make-example-file --scale"
  "${MAKE_EXAMPLE}" "${scale_dir}" --runs 1 --flat 128 --sessions 4)

set(scale_file "${scale_dir}/example_scale.h5")
if(NOT EXISTS "${scale_file}")
  message(FATAL_ERROR "the generator wrote no ${scale_file}")
endif()

# --warm on both: the point is that they run, and evicting the page cache would
# only make a smoke test slower and noisier.
run_step("bench-tree" "${BENCH_TREE}" "${scale_file}" --depth 2 --warm)
run_step("bench-data" "${BENCH_DATA}" "${scale_file}" --warm)

file(REMOVE_RECURSE "${scale_dir}")
