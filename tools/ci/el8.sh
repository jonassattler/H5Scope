#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-License-Identifier: GPL-3.0-only
#
# Run a command inside the RHEL 8 build environment.
#
#   tools/ci/el8.sh cmake --preset release
#   tools/ci/el8.sh cmake --build --preset release
#   tools/ci/el8.sh bash          # a shell in it, to poke at a failure
#
# The point of the wrapper is that paths are identical inside and outside. A
# CMake build tree records absolute paths in its cache, in build.ninja and in
# every compile_commands.json entry, so a tree configured at /work and built at
# /home/runner/work is not a tree at all. Bind-mounting the repository at its
# own path means each step can run in a fresh container and still be looking at
# what the last one produced -- which is what lets the workflow keep the
# per-step timeouts it needs without holding a container open across them.
#
# The container runs as root. Nothing in the build wants to be root, but the
# alternative is a uid with no passwd entry inside the image, and the failures
# that produces are obscure and turn up hours in. The cost is that files it
# writes into the workspace are root-owned, so the one host-side step that
# deletes them uses sudo; everything else only ever reads.
#
# Docker is the only thing needed on the host. Nothing else about it matters,
# which is the property being bought here: the same command produces the same
# binary on the runner and on a developer's machine.

set -euo pipefail

if [ "$#" -eq 0 ]; then
    echo "usage: $0 <command> [args...]" >&2
    exit 2
fi

command -v docker >/dev/null 2>&1 || {
    echo "error: docker is required to build against RHEL 8's glibc" >&2
    exit 2
}

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
image="${H5SCOPE_EL8_IMAGE:-h5scope-el8}"

# A private HOME rather than the caller's. vcpkg keeps its binary cache under
# $HOME/.cache/vcpkg and drops a marker in $HOME/.vcpkg, and a container should
# not be writing into a developer's actual home directory to do it. CI points
# this somewhere it can cache; locally it defaults under ~/.cache, and the
# whole build environment can be reset by deleting one directory.
el8_home="${H5SCOPE_EL8_HOME:-$HOME/.cache/h5scope/el8-home}"
mkdir -p "$el8_home"

# Built on demand, so a fresh clone needs no separate setup step and CI can
# still build it explicitly to keep that cost in its own timing.
if ! docker image inspect "$image" >/dev/null 2>&1; then
    echo "building $image from tools/ci/el8.Dockerfile" >&2
    docker build -t "$image" -f "$repo_root/tools/ci/el8.Dockerfile" "$repo_root/tools/ci"
fi

declare -a mounts=(
    -v "$repo_root:$repo_root"
    -v "$el8_home:$el8_home"
)

# vcpkg lives outside the repository and is passed through when it is set, at
# the same path again so the toolchain file the CMake cache recorded still
# resolves on the next step.
declare -a env_vars=(-e "HOME=$el8_home")
if [ -n "${VCPKG_ROOT:-}" ]; then
    mkdir -p "$VCPKG_ROOT"
    mounts+=(-v "$VCPKG_ROOT:$VCPKG_ROOT")
    env_vars+=(-e "VCPKG_ROOT=$VCPKG_ROOT")
fi

# Forwarded because the offscreen platform is how the test suite runs without a
# display, and because a build that thinks it is interactive prints progress
# bars into a log nobody can read.
for name in QT_QPA_PLATFORM CI GITHUB_ACTIONS H5SCOPE_APPIMAGE_TOOLS; do
    if [ -n "${!name:-}" ]; then
        env_vars+=(-e "$name=${!name}")
    fi
done

# --init so that a build killed from outside does not leave orphaned compiler
# processes holding the workspace open.
exec docker run --rm --init \
    "${mounts[@]}" \
    "${env_vars[@]}" \
    -w "$PWD" \
    "$image" \
    "$@"
