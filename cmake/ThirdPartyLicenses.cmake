# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-License-Identifier: GPL-3.0-only

# Assemble the licence texts of everything statically linked into the binary
# into one file, which is then compiled into the executable and published
# beside it.
#
# THIRD-PARTY-NOTICES.md is the *inventory*: what is inside the binary and
# under which licence. It is not the notice those licences ask for. BSD-3, MIT,
# Zlib, libpng and bzip2 each require the copyright notice, the list of
# conditions and the disclaimer to be reproduced in the materials accompanying
# a binary distribution -- HDF5's second clause says so in as many words -- and
# an SPDX identifier in a table is none of those three. This file is the texts.
#
# The source is vcpkg's own `share/<port>/copyright`, written when the port is
# installed, so what lands here is the text that came with the version that was
# actually built rather than whatever upstream says today.
#
# The port list is explicit rather than a glob over vcpkg_installed. That tree
# also holds Catch2, gperf, the vcpkg-* helper ports and two Qt modules nothing
# links; attributing what is not in the binary would make this file a different
# kind of wrong from the one it exists to fix. The names below are the ports
# behind the static archives on the executable's link line, and a name that
# stops resolving fails the configure rather than quietly shortening the file.

# Ports whose code is linked into H5Scope. Keep in step with the inventory
# in THIRD-PARTY-NOTICES.md; between them, the link line is the authority.
set(H5SCOPE_LINKED_PORTS
  brotli
  bzip2
  double-conversion
  expat
  fontconfig
  freetype
  harfbuzz
  hdf5
  libaec
  libb2
  libpng
  md4c
  meshoptimizer
  pcre2
  qtbase
  qtdeclarative
  qtgraphs
  qtquick3d
  qtshadertools
  qtsvg
  xcb-util-cursor
  zlib
  zstd
)

# Ports whose vcpkg copyright file points at a licence instead of being one.
# See licenses/README.md; the checks below are what keep the substitution
# honest.
#
# Located from this file rather than from CMAKE_SOURCE_DIR, so the module says
# where its own data is instead of assuming who included it.
get_filename_component(H5SCOPE_VENDORED_LICENCE_DIR
                       "${CMAKE_CURRENT_LIST_DIR}/../licenses" ABSOLUTE)
set(H5SCOPE_VENDORED_LICENCE_PORTS pcre2)
set(H5SCOPE_VENDORED_LICENCE_pcre2 "pcre2-LICENCE.md")
set(H5SCOPE_VENDORED_VERSION_pcre2 "10.47")

# Read the version vcpkg recorded for `port`, out of the name of the file list
# it writes on install: vcpkg/info/<port>_<version>_<triplet>.list.
function(_h5scope_port_version port installed triplet out_var)
  file(GLOB entries "${installed}/vcpkg/info/${port}_*_${triplet}.list")
  set(version "")
  foreach(entry ${entries})
    get_filename_component(name "${entry}" NAME)
    # <port>_<version>_<triplet>.list, and the version may carry a port-version
    # suffix of its own (`1.2.3#4`), which is kept: it identifies the build.
    string(REGEX REPLACE "^${port}_(.+)_${triplet}\\.list$" "\\1" version "${name}")
  endforeach()
  set(${out_var} "${version}" PARENT_SCOPE)
endfunction()

# Write <output> as the concatenated licence texts of H5SCOPE_LINKED_PORTS.
function(h5scope_collect_third_party_licenses output)
  if(DEFINED VCPKG_INSTALLED_DIR)
    set(installed "${VCPKG_INSTALLED_DIR}")
  else()
    set(installed "${CMAKE_BINARY_DIR}/vcpkg_installed")
  endif()
  set(triplet "${VCPKG_TARGET_TRIPLET}")

  if(NOT IS_DIRECTORY "${installed}/${triplet}/share")
    message(FATAL_ERROR
      "No vcpkg share directory at ${installed}/${triplet}/share.\n"
      "The third-party licence texts are read from the ports vcpkg installed, "
      "so this has to exist before the binary can be assembled.")
  endif()

  set(text
"H5Scope -- third-party licence texts
=======================================

H5Scope links every dependency statically, so one executable contains the
code of everything below. This file reproduces the copyright notices, licence
conditions and disclaimers that those licences require to accompany a binary
distribution. It is generated at configure time from the licence text vcpkg
installed with each port, so each entry corresponds to the version that was
built into this binary and not to whatever upstream carries today.

THIRD-PARTY-NOTICES.md, published beside this file, is the inventory: which
libraries these are, which Qt modules are linked, why the combined work is
GPL-3.0-only, and which libraries are the host system's rather than ours.

H5Scope's own licence is the GNU General Public License version 3, in
LICENSE beside this file. The typefaces are under the SIL Open Font License
1.1, whose text ships with the faces it covers; `H5Scope --notices` prints
that too.

")

  foreach(port ${H5SCOPE_LINKED_PORTS})
    set(copyright "${installed}/${triplet}/share/${port}/copyright")
    if(NOT EXISTS "${copyright}")
      message(FATAL_ERROR
        "No licence text for '${port}' at ${copyright}.\n"
        "Every port in H5SCOPE_LINKED_PORTS is linked into the binary and "
        "its licence has to travel with it. Either the port was renamed -- fix "
        "the list in cmake/ThirdPartyLicenses.cmake -- or it is no longer "
        "linked, in which case remove it from the list and from "
        "THIRD-PARTY-NOTICES.md.")
    endif()

    _h5scope_port_version("${port}" "${installed}" "${triplet}" version)
    set(source "as installed by vcpkg")

    if(port IN_LIST H5SCOPE_VENDORED_LICENCE_PORTS)
      # The substitution is only sound while the two things that justified it
      # still hold: vcpkg's file is still a pointer, and the port is still the
      # version the vendored text was taken from.
      file(SIZE "${copyright}" pointer_size)
      if(pointer_size GREATER 512)
        message(FATAL_ERROR
          "vcpkg now installs a real licence text for '${port}' "
          "(${pointer_size} bytes), so the vendored copy in licenses/ is no "
          "longer the fix for anything. Drop ${port} from "
          "H5SCOPE_VENDORED_LICENCE_PORTS and delete the vendored file.")
      endif()
      set(expected "${H5SCOPE_VENDORED_VERSION_${port}}")
      if(NOT version STREQUAL expected)
        message(FATAL_ERROR
          "'${port}' is now version ${version}, but the licence text vendored "
          "in licenses/${H5SCOPE_VENDORED_LICENCE_${port}} was taken from "
          "${expected}. Refresh it from the new release and update "
          "H5SCOPE_VENDORED_VERSION_${port}, or the binary will ship a "
          "notice that does not correspond to the code inside it.")
      endif()
      set(copyright
          "${H5SCOPE_VENDORED_LICENCE_DIR}/${H5SCOPE_VENDORED_LICENCE_${port}}")
      set(source "vendored: see licenses/README.md")
    endif()

    # Re-run the configure when a licence text changes underneath us, which is
    # what an upgraded port looks like from here.
    set_property(DIRECTORY APPEND
                 PROPERTY CMAKE_CONFIGURE_DEPENDS "${copyright}")

    file(READ "${copyright}" body)
    string(APPEND text
"===============================================================================
${port} ${version}
  (${source})
===============================================================================

${body}

")
  endforeach()

  list(LENGTH H5SCOPE_LINKED_PORTS count)
  message(STATUS "Third-party licence texts: ${count} ports -> ${output}")

  # Written only when it changes, so a reconfigure does not rebuild the
  # resource -- and with it the executable -- for an identical file.
  set(previous "")
  if(EXISTS "${output}")
    file(READ "${output}" previous)
  endif()
  if(NOT previous STREQUAL text)
    file(WRITE "${output}" "${text}")
  endif()
endfunction()
