# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-License-Identifier: GPL-3.0-only

# x64-linux, with D-Bus opened at run time rather than linked.
#
# The stock x64-linux triplet plus the qtbase block below, under the stock name
# on purpose: an overlay triplet takes precedence over vcpkg's own of the same
# name, so the presets go on saying x64-linux and every vcpkg_installed path in
# a local tree stays where it was. CMakePresets.json points the Linux presets
# at this directory for that.
#
# Why qtbase needs telling. Its dbus feature is what builds the xdg-desktop-
# portal platform theme -- the native file dialog on Linux; see vcpkg.json --
# and the port then configures Qt with -DINPUT_dbus=linked, against vcpkg's
# dbus port, which builds only a shared library. The binary would come out
# needing libdbus-1.so.3 from the host and refuse to start without it, which is
# a library the loader asks for before main() and so a library `--license`
# would need on a headless box as well.
#
# `runtime` is Qt's other way of having D-Bus: it opens libdbus with dlopen the
# first time a connection is asked for, and where there is none there is no
# D-Bus and nothing else changes -- the portal theme finds no portal and Qt
# Quick draws its own dialog in its place. VCPKG_CMAKE_CONFIGURE_OPTIONS reach
# the configure line after the port's own options, so this one wins.
#
# Kept in step with x64-linux-release.cmake by hand rather than included from
# it: vcpkg hashes a triplet's own text into every package's ABI, and an
# include() would let this file change without the release one's packages
# noticing.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

set(VCPKG_CMAKE_SYSTEM_NAME Linux)

if(PORT STREQUAL "qtbase")
  list(APPEND VCPKG_CMAKE_CONFIGURE_OPTIONS -DINPUT_dbus=runtime)
endif()
