# x64-linux, release only.
#
# vcpkg builds both configurations of every port by default, so a CI job that
# only ever builds Release still pays for a full debug Qt -- twice the time and
# twice the disk. A debug Qt is what filled a runner's disk mid-compile:
#
#   buildtrees/qtbase/x64-linux-dbg/.../cmake_pch.hxx.gch
#   fatal error: cannot write PCH file: No space left on device
#
# This is the stock x64-linux triplet with VCPKG_BUILD_TYPE added. It is used
# by CI alone, through --overlay-triplets; the presets still name x64-linux, so
# a local tree keeps both configurations and the debug preset keeps working.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Linux)
set(VCPKG_BUILD_TYPE release)
