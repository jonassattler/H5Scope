# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-License-Identifier: GPL-3.0-only

# Remove this target's outputs from earlier versions, and point a stable name
# at the current one. Run as a POST_BUILD step; see src/CMakeLists.txt.
#
# The version is in the file name, which means every commit built in the same
# tree leaves another executable behind -- 100 MB in release and about 1.8 GB
# in debug, so a dozen commits is a full disk rather than an untidy directory.
# Only files matching this target's own versioned pattern, in this target's own
# output directory, are ever touched.
#
# The unversioned symlink is what scripts and muscle memory should use:
# `build/release/bin/H5Scope` is always the build that just finished, where
# a glob over the versioned names is only unambiguous if the pruning worked.
#
# Expects: PRUNE_DIR, PRUNE_PATTERN, PRUNE_KEEP, PRUNE_LINK.

file(GLOB stale "${PRUNE_DIR}/${PRUNE_PATTERN}")
foreach(file ${stale})
  get_filename_component(name "${file}" NAME)
  if(NOT name STREQUAL "${PRUNE_KEEP}" AND NOT IS_SYMLINK "${file}")
    message(STATUS "Removing superseded build ${name}")
    file(REMOVE "${file}")
  endif()
endforeach()

if(NOT WIN32)
  # Replaced rather than skipped when it exists: it has to point at this build,
  # not at whichever one first created it.
  file(REMOVE "${PRUNE_DIR}/${PRUNE_LINK}")
  file(CREATE_LINK "${PRUNE_KEEP}" "${PRUNE_DIR}/${PRUNE_LINK}" SYMBOLIC)
endif()
