# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-License-Identifier: GPL-3.0-only
#
# The build environment for every Linux artefact this project publishes.
#
# Why an image at all, when the runner already has a perfectly good Ubuntu on
# it: because "runs on RHEL 8" is a property of the glibc the binary was linked
# against, and nothing else can supply it. A binary built on Ubuntu 24.04
# references GLIBC_2.39 and refuses to start on RHEL 8's 2.28 -- and no
# packaging step downstream can undo that. An AppImage bundles libraries; it
# does not bundle the loader, so it cannot rescue a binary built too high. The
# floor has to be set here, at the compiler, or it is not set at all.
#
# So the whole build moves onto the floor. AlmaLinux 8 is RHEL 8 rebuilt from
# the same sources, and glibc 2.28 is what it has.
#
# The awkward part is that RHEL 8's own GCC is 8.5, which cannot compile C++20.
# gcc-toolset-13 is Red Hat's answer to exactly that -- a modern compiler that
# still targets the platform's runtime -- so this image compiles with GCC 13.3
# and links against glibc 2.28. The C++ runtime is the one remaining gap, and
# the root CMakeLists closes it with -static-libstdc++; see the comment there.
#
# Build it, and get a shell in it, with:
#   docker build -t h5scope-el8 -f tools/ci/el8.Dockerfile tools/ci
#   tools/ci/el8.sh bash
FROM almalinux:8

# Two repositories have to be turned on before anything can be installed.
#
#   EPEL        carries xcb-util-cursor-devel, and nothing else this build
#               wants. Qt 6.5 and newer link the xcb platform plugin against
#               libxcb-cursor unconditionally, and the `xcb` feature's own
#               configure test includes <xcb/xcb_cursor.h> -- so without the
#               header here, vcpkg's Qt builds with no xcb plugin at all.
#
#               Note what this is and is not. It is a *build-time* requirement,
#               and only qtbase's. The release binary asks nothing of EPEL: it
#               links the library statically, from ports/xcb-util-cursor, which
#               is exactly because a stock RHEL 8 desktop does not have it and
#               a user cannot be told to enable a third-party repository to
#               open a file. If Qt ever stops needing the header at configure
#               time, this line goes and nothing downstream notices.
#   PowerTools  carries autoconf-archive and ninja-build.
#
# Then, in one layer because a half-installed build environment is not worth
# caching:
#
#   gcc-toolset-13   the compiler, as the metapackage rather than the pieces:
#                    it pulls gcc, gcc-c++ and a binutils new enough to link
#                    what they emit.
#   cmake and co.    AlmaLinux 8's cmake is 3.26.5, exactly what the root
#                    CMakeLists asks for. vcpkg fetches its own for the ports.
#   autoconf..unzip  what an Ubuntu runner image happens to come with and a
#                    minimal AlmaLinux does not. Left out, ports fail one at a
#                    time and a long way from here.
#   libX11-devel..   the platform SDK headers. This list is the RHEL 8
#                    spelling of the one the workflow used to hand to apt, and
#                    it has to cover every qtbase feature vcpkg.json forces
#                    on -- Qt fails its own configure with "Forcing to ON
#                    breaks its condition" otherwise, naming the feature but
#                    never the package.
#   libtool-ltdl-devel  libxcrypt, further down the graph, asks for it by name.
RUN dnf -y install dnf-plugins-core epel-release \
 && { dnf config-manager --set-enabled powertools \
      || dnf config-manager --set-enabled PowerTools; } \
 && dnf -y update \
 && dnf -y install \
      gcc-toolset-13 \
      glibc-langpack-en \
      cmake ninja-build make git patch \
      autoconf automake libtool autoconf-archive m4 bison flex gperf \
      pkgconf-pkg-config perl python3.11 \
      gawk diffutils findutils file which \
      curl wget tar gzip bzip2 xz zip unzip \
      libX11-devel libXext-devel libXfixes-devel libXi-devel libXrender-devel \
      libXrandr-devel libXcursor-devel libXinerama-devel \
      libSM-devel libICE-devel \
      libxcb-devel xcb-util-devel xcb-util-image-devel xcb-util-keysyms-devel \
      xcb-util-renderutil-devel xcb-util-wm-devel xcb-util-cursor-devel \
      libxkbcommon-devel libxkbcommon-x11-devel \
      mesa-libGL-devel mesa-libEGL-devel mesa-libgbm-devel libglvnd-devel \
      fontconfig-devel dbus-devel \
      wayland-devel wayland-protocols-devel \
      libtool-ltdl-devel \
 && dnf clean all \
 && rm -rf /var/cache/dnf

# Two names the rest of the world assumes and RHEL 8 spells differently.
#
# python3, because vcpkg's meson-based ports need Python 3.7 or newer and
# AlmaLinux 8's default python3 is 3.6; python3.11 installs under that name
# only. dnf keeps using its own platform-python regardless.
#
# ninja, because the ninja-build package installs the binary as `ninja-build`.
# CMake does look for both, but the presets and every habit here say ninja, and
# a symlink is cheaper than finding out which of the two some port assumed.
RUN ln -sf /usr/bin/python3.11 /usr/local/bin/python3 \
 && ln -sf /usr/bin/python3.11 /usr/local/bin/python \
 && ln -sf /usr/bin/ninja-build /usr/local/bin/ninja

# gcc-toolset lives outside the default PATH and is normally entered through
# `scl enable`, which wraps the command in a subshell -- awkward to compose
# with and easy to lose across a pipeline. Setting the variables its enable
# script sets makes GCC 13 simply the compiler, for every process in here,
# with no wrapper anywhere.
#
# Set before the autotools build below rather than after it, because that build
# has to find the autoconf it just installed in /usr/local/bin.
ENV PATH=/opt/rh/gcc-toolset-13/root/usr/bin:/usr/local/bin:/usr/local/sbin:/usr/bin:/usr/sbin \
    LD_LIBRARY_PATH=/opt/rh/gcc-toolset-13/root/usr/lib64:/opt/rh/gcc-toolset-13/root/usr/lib \
    CC=gcc \
    CXX=g++ \
    LANG=en_US.UTF-8

# Autoconf, from source, because RHEL 8's is 2.69 and vcpkg's gperf port wants
# 2.70 or newer:
#
#   configure.ac:22: error: Autoconf version 2.70 or higher is required
#   error: building gperf:x64-linux-release failed with: BUILD_FAILED
#
# This is the whole shape of the old-distribution tax, and it is worth being
# precise about what it is and is not. The glibc has to be old, because that is
# what the binary links against and what decides where it will start. The build
# *tools* do not -- nothing about autoconf ends up in the executable -- so
# replacing them costs nothing and buys the ports the versions they expect.
# Ubuntu 24.04 had autoconf 2.71 and this never came up.
#
# Automake comes along for the same ride: 1.16.1 is contemporary with autoconf
# 2.69, and pairing it with 2.72 is the combination nobody tests.
#
# Both are checksummed. A build environment that fetches unpinned tarballs over
# the network is not a reproducible one, and this image is the definition of
# what the release was compiled by.
RUN set -eux; \
    cd /tmp; \
    curl -sSLO https://ftp.gnu.org/gnu/autoconf/autoconf-2.72.tar.gz; \
    echo "afb181a76e1ee72832f6581c0eddf8df032b83e2e0239ef79ebedc4467d92d6e  autoconf-2.72.tar.gz" | sha256sum -c -; \
    tar xf autoconf-2.72.tar.gz; \
    cd autoconf-2.72; \
    ./configure --prefix=/usr/local; \
    make -j"$(nproc)"; \
    make install; \
    cd /tmp; \
    rm -rf autoconf-2.72 autoconf-2.72.tar.gz; \
    curl -sSLO https://ftp.gnu.org/gnu/automake/automake-1.17.tar.gz; \
    echo "397767d4db3018dd4440825b60c64258b636eaf6bf99ac8b0897f06c89310acd  automake-1.17.tar.gz" | sha256sum -c -; \
    tar xf automake-1.17.tar.gz; \
    cd automake-1.17; \
    ./configure --prefix=/usr/local; \
    make -j"$(nproc)"; \
    make install; \
    cd /tmp; \
    rm -rf automake-1.17 automake-1.17.tar.gz; \
    mkdir -p /usr/local/share/aclocal; \
    echo /usr/share/aclocal > /usr/local/share/aclocal/dirlist

# The consequence of installing automake under /usr/local, and it is not
# obvious. aclocal searches its own prefix -- /usr/local/share/aclocal -- and
# nothing else by default, while the macros belonging to the RPMs are in
# /usr/share/aclocal: autoconf-archive's AX_* set, and libtool's LT_INIT and
# LTDL_INIT. So a from-source automake cannot see any of them, and vcpkg says
# so in a way that names every dependency except the one at fault:
#
#   gperf currently requires the following programs from the system package
#   manager:
#       autoconf autoconf-archive automake libtoolize
#
# All four were installed. What vcpkg actually does is run aclocal over a probe
# configure.ac testing for those three macros, so the failure was the macro
# path and never the packages. `dirlist` above is automake's own mechanism for
# exactly this -- the file is read from the system acdir and each line added to
# the search path -- and is how distributions point a local automake at the
# system macros.
#
# Asserted here rather than discovered an hour into a CI run, by running the
# same probe vcpkg runs. If the macro path ever breaks again, the image fails
# to build and says which package it could not see.
RUN set -eux; \
    d="$(mktemp -d)"; \
    cd "$d"; \
    printf '%s\n' \
      'AC_INIT([check-autoconf], [1.0])' \
      'AM_INIT_AUTOMAKE' \
      'm4_ifndef([AX_CHECK_COMPILE_FLAG], [m4_errprintn([System package autoconf-archive is missing.])])' \
      'm4_ifndef([LT_INIT], [m4_errprintn([System package libtool is missing.])])' \
      'm4_ifndef([LTDL_INIT], [m4_errprintn([System package libltdl-dev is missing.])])' \
      'AC_OUTPUT' > configure.ac; \
    aclocal --dry-run 2> err.log || true; \
    cat err.log; \
    ! grep -q "is missing" err.log; \
    echo "OK: aclocal resolves the autoconf-archive and libtool macros"; \
    cd /; \
    rm -rf "$d"

# The workspace is bind-mounted from the host and owned by the runner's uid,
# while everything in here runs as root. Without this, git refuses to read the
# repository as "dubious ownership" -- and cmake/Version.cmake, which counts
# release tags to work out the version, would report an unversioned build
# rather than a wrong one, which is the failure it was written to produce.
# --system rather than --global because el8.sh overrides HOME.
RUN git config --system --add safe.directory '*'

# Fail loudly and immediately if the image is not what it claims to be, rather
# than an hour into Qt. The glibc assertion is the load-bearing one: it is the
# entire reason this file exists.
RUN gcc --version | head -1 \
 && test "$(gcc -dumpversion | cut -d. -f1)" -ge 13 \
 && ldd --version | head -1 \
 && test "$(ldd --version | head -1 | grep -oE '[0-9]+\.[0-9]+$')" = "2.28" \
 && cmake --version | head -1 \
 && ninja --version \
 && python3 --version \
 && autoconf --version | head -1 \
 && test "$(autoconf --version | head -1 | grep -oE '[0-9]+\.[0-9]+$')" = "2.72" \
 && automake --version | head -1

CMD ["/bin/bash"]
