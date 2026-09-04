#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-License-Identifier: GPL-3.0-only
#
# Wrap the release executable in an AppImage.
#
# Why there is anything to do at all. Qt, HDF5 and the fonts are already inside
# the executable, so this is not the usual Qt deployment problem: there are no
# plugins to copy, no qml directory, no qt.conf, and linuxdeploy's Qt plugin
# would have nothing to find. What is left is a much smaller list, and it is
# the reason an AppImage earns its place here rather than being ceremony on top
# of a binary that already runs:
#
#   * libxcb-cursor. Qt 6.5 and newer load it from the xcb platform plugin and
#     abort with "could not load the Qt platform plugin xcb" when it is absent.
#     RHEL 8 does not ship it in BaseOS or AppStream at all -- it is EPEL only
#     -- so on a stock RHEL 8 desktop the plain executable fails at startup and
#     the AppImage is what makes it run. That single library is most of the
#     argument.
#
#   * The xcb-util family, libSM/libICE and libxkbcommon. Present on RHEL 8,
#     but old, and bundling costs about a megabyte.
#
#   * A .desktop file and an icon, so the thing has a name, an icon and an
#     association with .h5 files instead of being an anonymous blob in
#     ~/Downloads.
#
# What is deliberately NOT bundled is more important than what is. The GL and
# EGL entry points are glvnd: they dispatch to the installed GPU driver, and a
# bundled copy sends every NVIDIA user to a software renderer or a crash. Core
# libX11 and libxcb speak to the running X server and are coupled to that same
# GL stack. libwayland-client is coupled to the compositor. glibc is never
# bundled by anything sane. All of those come from the host, which is why the
# executable inside still has to be built against an old glibc -- see
# tools/ci/el8.Dockerfile. An AppImage is not a substitute for that and cannot
# be: it carries libraries, not a loader.
#
# Usage: tools/make-appimage.sh <executable> <output-dir>
#    eg: tools/make-appimage.sh build/release/bin/H5Scope-0.2.0 dist

set -euo pipefail

# --- pinned tools ----------------------------------------------------------
# Both are downloaded, and both are checked. appimagetool otherwise fetches its
# runtime from the network at build time, which would put an unpinned binary
# inside the first eight kilobytes of every release we publish; --runtime-file
# with a known hash is how that is avoided.
#
# The runtime matters for RHEL 8 in one specific way: it is statically linked,
# so it asks nothing of the host's glibc, and it mounts through FUSE 2 --
# /usr/bin/fusermount, from the `fuse` package -- rather than fusermount3.
# That is the right way round for RHEL 8, where FUSE 2 is the one that is
# actually there.
APPIMAGETOOL_URL="https://github.com/AppImage/appimagetool/releases/download/1.9.1/appimagetool-x86_64.AppImage"
APPIMAGETOOL_SHA256="ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0"

RUNTIME_URL="https://github.com/AppImage/type2-runtime/releases/download/20251108/runtime-x86_64"
RUNTIME_SHA256="2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d"

# --- the exclusion list ----------------------------------------------------
# Matched against the soname, anchored at both ends, so that libxcb.so.1 is
# excluded without also excluding libxcb-cursor.so.0 -- which is the whole
# point of the exercise and would be silently lost by a prefix match.
readonly -a HOST_LIBRARIES=(
    # glibc and the loader. Bundling any of these breaks NSS, dlopen and
    # eventually the process.
    'ld-linux-x86-64\.so\..*' 'libc\.so\..*' 'libm\.so\..*' 'libdl\.so\..*'
    'libpthread\.so\..*' 'librt\.so\..*' 'libresolv\.so\..*' 'libnsl\.so\..*'
    'libutil\.so\..*' 'libcrypt\.so\..*' 'libanl\.so\..*'
    # Linked statically by the build. If they turn up here something has gone
    # wrong upstream of this script, and check-glibc-floor.sh will say so.
    'libstdc\+\+\.so\..*' 'libgcc_s\.so\..*'
    # glvnd and the driver stack. Never bundle: these dispatch into whatever
    # GPU driver the host has installed.
    'libGL\.so\..*' 'libGLX\.so\..*' 'libGLdispatch\.so\..*' 'libOpenGL\.so\..*'
    'libEGL\.so\..*' 'libGLESv[12]\.so\..*' 'libglapi\.so\..*'
    'libdrm\.so\..*' 'libgbm\.so\..*'
    # The core X protocol libraries, coupled to the running server and to the
    # GL stack above. The xcb-util libraries layered on top are not, and are
    # bundled.
    'libX11\.so\..*' 'libX11-xcb\.so\..*' 'libxcb\.so\..*' 'libxcb-glx\.so\..*'
    # Coupled to the compositor and, for -egl, to the driver.
    'libwayland-client\.so\..*' 'libwayland-server\.so\..*' 'libwayland-egl\.so\..*'
)

is_host_library() {
    local soname="$1" pattern
    for pattern in "${HOST_LIBRARIES[@]}"; do
        [[ "$soname" =~ ^${pattern}$ ]] && return 0
    done
    return 1
}

# --- arguments -------------------------------------------------------------
if [ "$#" -ne 2 ]; then
    echo "usage: $0 <executable> <output-dir>" >&2
    exit 2
fi

binary="$1"
outdir="$2"

[ -x "$binary" ] || { echo "error: not an executable: $binary" >&2; exit 1; }

for tool in objdump ldd; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "error: $tool is required (binutils)" >&2; exit 2; }
done

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
packaging="$repo_root/packaging"

# The version is taken from the file name rather than by running the binary:
# this script has to work on a build machine with no display, and the name is
# already the authority everywhere else in the release (see cmake/Version.cmake
# and the tag check in CI).
name="$(basename "$binary")"
version="${name#H5Scope-}"
if [ "$version" = "$name" ]; then
    echo "error: expected an executable named H5Scope-<version>, got '$name'" >&2
    exit 1
fi

echo "H5Scope $version -> AppImage"

# --- fetch the pinned tools ------------------------------------------------
tools_dir="${H5SCOPE_APPIMAGE_TOOLS:-${XDG_CACHE_HOME:-$HOME/.cache}/h5scope/appimage-tools}"
mkdir -p "$tools_dir"

fetch() {
    local url="$1" sha="$2" dest="$3"
    if [ -f "$dest" ] && echo "$sha  $dest" | sha256sum --check --status; then
        echo "  cached: $(basename "$dest")"
        return
    fi
    echo "  fetching: $(basename "$dest")"
    curl -sSL --fail --retry 3 --retry-delay 5 -o "$dest.tmp" "$url"
    if ! echo "$sha  $dest.tmp" | sha256sum --check --status; then
        echo "error: checksum mismatch for $url" >&2
        echo "  expected $sha" >&2
        echo "  got      $(sha256sum "$dest.tmp" | cut -d' ' -f1)" >&2
        rm -f "$dest.tmp"
        exit 1
    fi
    mv "$dest.tmp" "$dest"
}

fetch "$APPIMAGETOOL_URL" "$APPIMAGETOOL_SHA256" "$tools_dir/appimagetool"
fetch "$RUNTIME_URL" "$RUNTIME_SHA256" "$tools_dir/runtime-x86_64"
chmod +x "$tools_dir/appimagetool"

# --- assemble the AppDir ---------------------------------------------------
appdir="$(mktemp -d)/H5Scope.AppDir"
trap 'rm -rf "$(dirname "$appdir")"' EXIT

mkdir -p "$appdir/usr/bin" "$appdir/usr/lib" \
         "$appdir/usr/share/applications" \
         "$appdir/usr/share/icons/hicolor/256x256/apps" \
         "$appdir/usr/share/icons/hicolor/scalable/apps" \
         "$appdir/usr/share/doc/h5scope"

install -m 755 "$binary" "$appdir/usr/bin/$name"
# The unversioned name AppRun execs, so the launcher never has to be rewritten
# for a release.
ln -s "$name" "$appdir/usr/bin/H5Scope"

install -m 755 "$packaging/AppRun" "$appdir/AppRun"

# appimagetool insists on a .desktop file and an icon at the AppDir root, and
# desktop environments that integrate the AppImage read the copies under
# usr/share. X-AppImage-Version is appended rather than committed, because the
# version is counted at build time and a checked-in copy would go stale.
install -m 644 "$packaging/h5scope.desktop" "$appdir/h5scope.desktop"
printf 'X-AppImage-Version=%s\n' "$version" >> "$appdir/h5scope.desktop"
install -m 644 "$appdir/h5scope.desktop" "$appdir/usr/share/applications/h5scope.desktop"

install -m 644 "$packaging/h5scope.png" "$appdir/h5scope.png"
install -m 644 "$packaging/h5scope.png" \
    "$appdir/usr/share/icons/hicolor/256x256/apps/h5scope.png"
install -m 644 "$packaging/h5scope.svg" \
    "$appdir/usr/share/icons/hicolor/scalable/apps/h5scope.svg"
# The thumbnail a file manager shows for the AppImage itself.
cp "$packaging/h5scope.png" "$appdir/.DirIcon"

# The AppImage is a separate conveyance of the same object code, so it carries
# the same three documents the bare binary is published with. They are inside
# the executable too -- `--license` and `--notices` work from within the
# AppImage exactly as they do outside it -- but section 4 of the GPL is about
# what accompanies the Program, and this is that.
bindir="$(dirname "$binary")"
for doc in LICENSE THIRD-PARTY-NOTICES.md THIRD-PARTY-LICENSES.txt; do
    if [ -f "$bindir/$doc" ]; then
        install -m 644 "$bindir/$doc" "$appdir/usr/share/doc/h5scope/$doc"
    else
        echo "error: $doc is not beside the executable; the build should have put it there" >&2
        exit 1
    fi
done

# --- bundle the libraries --------------------------------------------------
# A breadth-first walk over DT_NEEDED entries rather than a flat `ldd` dump.
# The difference matters: ldd reports the whole transitive closure, including
# everything reachable only through libraries that are deliberately left to the
# host, and copying those would drag half of libX11's dependency tree into the
# bundle for no reason. Walking NEEDED and following only what is actually
# bundled keeps the set to what the executable itself asks for, plus what those
# libraries in turn need.
echo "  resolving libraries"

declare -A resolved=()
while read -r soname arrow path _; do
    [ "$arrow" = "=>" ] || continue
    [ -n "$path" ] && [ -e "$path" ] || continue
    resolved["$soname"]="$path"
done < <(ldd "$binary" | sed 's/^[[:space:]]*//')

needed_of() {
    objdump -p "$1" 2>/dev/null | awk '/NEEDED/ {print $2}'
}

declare -A seen=()
declare -a queue=()
declare -a bundled=()

while read -r soname; do
    [ -n "$soname" ] && queue+=("$soname")
done < <(needed_of "$binary")

while [ "${#queue[@]}" -gt 0 ]; do
    soname="${queue[0]}"
    queue=("${queue[@]:1}")

    [ -n "${seen[$soname]:-}" ] && continue
    seen["$soname"]=1

    is_host_library "$soname" && continue

    path="${resolved[$soname]:-}"
    if [ -z "$path" ]; then
        echo "error: $soname is required but was not found by ldd" >&2
        exit 1
    fi

    install -m 644 "$path" "$appdir/usr/lib/$soname"
    bundled+=("$soname")

    while read -r dependency; do
        [ -n "$dependency" ] && queue+=("$dependency")
    done < <(needed_of "$path")
done

echo "  bundled ${#bundled[@]} libraries:"
printf '    %s\n' "${bundled[@]}" | sort

# --- build it --------------------------------------------------------------
mkdir -p "$outdir"
output="$outdir/H5Scope-$version-x86_64.AppImage"
rm -f "$output"

# APPIMAGE_EXTRACT_AND_RUN because appimagetool is itself an AppImage and there
# is no FUSE inside a build container. --no-appstream because the AppDir ships
# no AppStream metadata to validate; the .desktop file is still checked.
echo "  packing"
ARCH=x86_64 APPIMAGE_EXTRACT_AND_RUN=1 "$tools_dir/appimagetool" \
    --runtime-file "$tools_dir/runtime-x86_64" \
    --no-appstream \
    "$appdir" "$output"

chmod +x "$output"

echo
echo "wrote $output ($(du -h "$output" | cut -f1))"
