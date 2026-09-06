# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-License-Identifier: GPL-3.0-only

# x64-windows-static, release only.
#
# The stock x64-windows-static triplet with VCPKG_BUILD_TYPE added, for the
# same reason as x64-linux-release beside it: CI never links a debug Qt, and
# building one costs twice the time and twice the disk on a runner that has
# less of both than the Linux one. The presets still name plain
# x64-windows-static, so a local tree keeps both configurations.
#
# The static CRT is the point of this triplet rather than an incidental part
# of its name. x64-windows-static-md would build the same static Qt against
# the *dynamic* runtime, and the resulting executable does not start on a
# machine without the Visual C++ redistributable -- which is the Windows
# spelling of the failure the whole RHEL 8 argument exists to avoid: a binary
# that runs everywhere it was tested and refuses to start on the user's
# desktop, naming a library they have no obvious way to get.
#
# tools/ci/verify-windows-binary.ps1 is what proves it stayed true: it reads
# the import table back out and fails if vcruntime140.dll appears there.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE static)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_BUILD_TYPE release)
