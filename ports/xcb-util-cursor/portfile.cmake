# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-License-Identifier: GPL-3.0-only
#
# xcb-util-cursor, built as a static archive so that libxcb-cursor ends up
# inside the executable instead of being asked of the machine that runs it.
#
# This is the one X library the release cannot leave to the host. Qt 6.5 and
# newer link the xcb platform plugin against it unconditionally -- it is in
# `PUBLIC_LIBRARIES` in qtbase's src/plugins/platforms/xcb/CMakeLists.txt, with
# no feature guarding it, and the `xcb` feature's own compile test includes
# <xcb/xcb_cursor.h> -- so a Qt built without it has no xcb plugin at all. RHEL
# 8 ships the library in neither BaseOS nor AppStream; it is EPEL only. The
# result, before this port existed, was a self-contained binary that a stock
# RHEL 8 desktop refused to start, with a loader error naming a library the
# user had never heard of and no way to get it but enabling a third-party
# repository.
#
# Only three symbols are involved -- xcb_cursor_context_new, ..._free and
# xcb_cursor_load_cursor -- and it is tempting to stub them. That does not
# work: QXcbCursor::createFontCursor() begins `if (!m_cursorContext) return
# XCB_NONE;`, so a context that fails to initialise does not fall back to the
# glyph cursors below it. The application would run with no mouse cursor at
# all. The real library is the only answer, and at ~40 KB of object code it is
# a cheap one.
#
# Why an overlay port rather than vendored sources: this project's rule is that
# every dependency is built and pinned by vcpkg (see the root CMakeLists), and
# following it means the licence machinery and the source bundle pick this up
# for free -- cmake/ThirdPartyLicenses.cmake reads share/<port>/copyright, and
# tools/make-source-bundle.sh reads port.data.cmake beside this file. Vendoring
# the four .c files would have meant doing both by hand.
#
# vcpkg has no xcb-util-cursor port of its own at the pinned baseline. Its
# sibling ports (xcb-image, xcb-util, xcb-render-util) all begin by declaring
# themselves an empty package on Linux -- "should be provided by your system"
# -- which is exactly the assumption being overturned here, so none of them
# could have been used as-is even had this one existed.

# The whole point of the port is a library RHEL 8's X stack lacks, and the
# `supports` field in vcpkg.json already says linux. This is the same statement
# made where it produces a readable error rather than a resolver message.
if(NOT VCPKG_TARGET_IS_LINUX)
    message(FATAL_ERROR
        "xcb-util-cursor is an X11 library and this port exists only to keep "
        "it out of the runtime dependencies of a Linux binary.")
endif()

# A shared build would defeat the exercise entirely: the release would go back
# to needing libxcb-cursor.so.0 on the host, only now from vcpkg's tree rather
# than the distribution's, which is worse than where it started.
if(NOT VCPKG_LIBRARY_LINKAGE STREQUAL "static")
    message(FATAL_ERROR
        "xcb-util-cursor must be built static -- the reason this port exists "
        "is to remove libxcb-cursor.so.0 from the executable's NEEDED list. "
        "Use a static triplet.")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/port.data.cmake")

vcpkg_download_distfile(archive
    URLS ${xcb-util-cursor_URL}
    FILENAME "${xcb-util-cursor_FILENAME}"
    SHA512 "${xcb-util-cursor_HASH}"
)

vcpkg_extract_source_archive(SOURCE_PATH ARCHIVE "${archive}")

# Upstream is autotools and the tarball's `configure` works, so the substitution
# needs a reason. It is that the autotools build asks for things this
# environment cannot give it and does not need: libtool would build a shared
# object to throw away, and configure's three PKG_CHECK_MODULES calls look for
# xcb-render, xcb-renderutil and xcb-image through a pkg-config that vcpkg has
# pointed at its own prefix -- where, on Linux, none of them are, because vcpkg
# leaves the X libraries to the system. See the injected file for what it does
# reproduce, and why that part matters.
file(COPY "${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt" DESTINATION "${SOURCE_PATH}")

vcpkg_cmake_configure(SOURCE_PATH "${SOURCE_PATH}")
vcpkg_cmake_install()

# One copy of the header serves both configurations; vcpkg's post-build checks
# require the debug tree to carry none.
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

# MIT, plus the X11-style clause forbidding use of the authors' names in
# advertising. cmake/ThirdPartyLicenses.cmake reads the file this writes and
# reproduces it verbatim in THIRD-PARTY-LICENSES.txt, which is what the licence
# actually asks for; THIRD-PARTY-NOTICES.md carries the inventory entry.
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/COPYING")
