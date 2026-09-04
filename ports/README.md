# Overlay ports

vcpkg ports that this project carries itself, because the registry at the
pinned baseline does not have them in the form the release needs.

`cmake/triplets/` is the same idea for triplets. Both are wired in as overlays
rather than forked into a private registry: an overlay is a directory, it
shadows the registry by name, and it is visible in a diff.

| Port | Why it is here |
|---|---|
| `xcb-util-cursor` | Qt 6.5+ links the xcb platform plugin against `libxcb-cursor` unconditionally, and RHEL 8 ships that library only through EPEL. vcpkg has no port for it, and its sibling xcb ports all declare themselves empty packages on Linux on the grounds that the system provides them — the assumption this port exists to overturn. Built static, so the library ends up inside the executable. See `xcb-util-cursor/portfile.cmake`. |

The overlay is on by default: `CMakePresets.json` sets `VCPKG_OVERLAY_PORTS`
for every preset, so `cmake --preset release` needs no extra flag and neither
does CI.
