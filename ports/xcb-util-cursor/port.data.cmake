# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-License-Identifier: GPL-3.0-only

# Where this port's upstream source comes from, in the shape vcpkg's own Qt
# ports use for the same purpose. The shape is not decoration: it is what
# tools/make-source-bundle.sh reads to check that the source of everything
# inside the binary is actually in the bundle, and it reads this file with the
# same three patterns it reads theirs with.
#
# The .tar.xz release tarball rather than a git tag, because a release tarball
# carries the products of autotools and gperf -- `configure`, and the
# `shape_to_id.c` generated from `shape_to_id.gperf` -- so nothing here needs
# autoreconf, xorg-util-macros or gperf to build. The .gperf source is in the
# tarball too, so this is still the complete source.
set(xcb-util-cursor_HASH "e2d14c3f0ab117524ba90d1a992b61717ccee04bc9e66c587a6a0f10571f15e89fc5db3413882ca7ce14ebc07b6b7b0a4ddecd59ba910e6ca654ea9b1c705ed5")
set(xcb-util-cursor_URL "https://xcb.freedesktop.org/dist/xcb-util-cursor-0.1.5.tar.xz;https://xorg.freedesktop.org/archive/individual/lib/xcb-util-cursor-0.1.5.tar.xz")
set(xcb-util-cursor_FILENAME "xcb-util-cursor-0.1.5.tar.xz")
