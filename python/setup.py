# SPDX-FileCopyrightText: 2026 Andreas Martin
# SPDX-License-Identifier: GPL-3.0-only

"""Compile the fold, then install the package around it.

The shared library is loaded with ctypes, so it is not a Python extension
and the wheel is tagged py3-none-<platform>: one build for every Python
from 3.11 on. The sources live in the repository, outside this package.
An sdist of python/ cannot contain them. cibuildwheel sets H5PLOT_REPO to
the checkout it copied into the build; a local install falls back to the
directory beside python/.
"""

import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from setuptools import setup
from setuptools.command.bdist_wheel import bdist_wheel
from setuptools.command.build_py import build_py
from setuptools.command.install import install


def _library_name() -> str:
    if sys.platform == "win32":
        return "h5plot.dll"
    if sys.platform == "darwin":
        return "libh5plot.dylib"
    return "libh5plot.so"


def _is_repo(path: Path) -> bool:
    return (path / "src" / "plotcore" / "plotcore_c.cpp").is_file()


def repo_root() -> Path:
    candidates: list[Path] = []
    env = os.environ.get("H5PLOT_REPO")
    if env:
        given = Path(env)
        # $(pwd) is the checkout. If it was evaluated inside python/, the
        # sources are one directory up.
        candidates.append(given)
        candidates.append(given.parent)
    # Hosted runners export this. The Linux container does not see it; that
    # build is pointed at /project instead.
    workspace = os.environ.get("GITHUB_WORKSPACE")
    if workspace:
        candidates.append(Path(workspace))
    # python/setup.py in a checkout. An isolated build copies this file
    # into a temporary tree that has no src/ beside it.
    candidates.append(Path(__file__).resolve().parent.parent)
    candidates.append(Path.cwd())
    # cibuildwheel's Linux container copies the working directory here.
    candidates.append(Path("/project"))
    seen: set[Path] = set()
    for candidate in candidates:
        try:
            resolved = candidate.resolve()
        except OSError:
            continue
        if resolved in seen:
            continue
        seen.add(resolved)
        if _is_repo(resolved):
            return resolved
    raise SystemExit(
        "Cannot find src/plotcore. Set H5PLOT_REPO to the repository root, "
        "or install from the checkout with --no-build-isolation."
    )


def _osx_architectures() -> str | None:
    preset = os.environ.get("CMAKE_OSX_ARCHITECTURES")
    if preset:
        return preset
    # cibuildwheel says which slices a macOS wheel holds with ARCHFLAGS,
    # not with CMAKE_OSX_ARCHITECTURES. CMake does not read ARCHFLAGS.
    arches = re.findall(r"-arch\s+(\S+)", os.environ.get("ARCHFLAGS", ""))
    if not arches:
        return None
    return ";".join(arches)


def _pe_imported_dlls(path: Path) -> list[str]:
    data = path.read_bytes()
    if data[:2] != b"MZ":
        raise SystemExit(f"{path.name} is not a PE file")
    pe = int.from_bytes(data[0x3C:0x40], "little")
    if data[pe : pe + 4] != b"PE\0\0":
        raise SystemExit(f"{path.name} is not a PE file")
    coff = pe + 4
    sections = int.from_bytes(data[coff + 2 : coff + 4], "little")
    optional_size = int.from_bytes(data[coff + 16 : coff + 18], "little")
    optional = coff + 20
    magic = int.from_bytes(data[optional : optional + 2], "little")
    # Data directory 1 is the import table, 13 the delay-load table.
    if magic == 0x20B:
        count_at, directories = optional + 108, optional + 112
    elif magic == 0x10B:
        count_at, directories = optional + 92, optional + 96
    else:
        raise SystemExit(f"{path.name} has an unknown PE magic {magic:#x}")
    directory_count = int.from_bytes(data[count_at : count_at + 4], "little")
    section_table = optional + optional_size

    def rva_to_offset(rva: int) -> int:
        for index in range(sections):
            entry = section_table + index * 40
            virtual = int.from_bytes(data[entry + 12 : entry + 16], "little")
            extent = max(
                int.from_bytes(data[entry + 8 : entry + 12], "little"),
                int.from_bytes(data[entry + 16 : entry + 20], "little"),
            )
            if virtual <= rva < virtual + extent:
                raw = int.from_bytes(data[entry + 20 : entry + 24], "little")
                return raw + (rva - virtual)
        raise SystemExit(f"{path.name} has an import outside its sections")

    def names_at(rva: int, stride: int, name_at: int) -> list[str]:
        if rva == 0:
            return []
        offset = rva_to_offset(rva)
        found: list[str] = []
        while offset + stride <= len(data):
            name_rva = int.from_bytes(
                data[offset + name_at : offset + name_at + 4], "little"
            )
            if name_rva == 0:
                break
            start = rva_to_offset(name_rva)
            end = data.index(b"\0", start)
            found.append(data[start:end].decode("ascii", "replace"))
            offset += stride
        return found

    def directory_rva(index: int) -> int:
        if index >= directory_count:
            return 0
        at = directories + index * 8
        return int.from_bytes(data[at : at + 4], "little")

    imported = names_at(directory_rva(1), 20, 12)
    imported += names_at(directory_rva(13), 32, 4)
    return imported


# A DLL whose import is not on this list is not part of Windows. The wheel
# test loads with ctypes, which does not search PATH, so a runtime that
# exists only beside the compiler is reported as this file being missing.
_WINDOWS_SYSTEM_DLLS = {
    "kernel32.dll",
    "msvcrt.dll",
    "user32.dll",
    "advapi32.dll",
    "ws2_32.dll",
    "bcrypt.dll",
    "ntdll.dll",
    "ole32.dll",
    "oleaut32.dll",
    "shell32.dll",
    "gdi32.dll",
    "shlwapi.dll",
    "combase.dll",
    "rpcrt4.dll",
    "sechost.dll",
    "version.dll",
    "winmm.dll",
    "imm32.dll",
    "setupapi.dll",
    "cfgmgr32.dll",
    "ucrtbase.dll",
}


def _reject_foreign_dlls(path: Path) -> None:
    if sys.platform != "win32":
        return
    imported = _pe_imported_dlls(path)
    print(f"{path.name} imports: {', '.join(imported) or '(none)'}", flush=True)
    foreign = [
        name
        for name in imported
        if name.lower() not in _WINDOWS_SYSTEM_DLLS
        and not name.lower().startswith(("api-ms-win-", "ext-ms-"))
    ]
    if foreign:
        raise SystemExit(
            f"{path.name} imports {', '.join(foreign)}. Link the C runtime "
            "statically; the wheel does not ship those DLLs."
        )


def _run(cmd: list[str]) -> None:
    try:
        subprocess.check_call(cmd)
    except FileNotFoundError as error:
        raise SystemExit(
            f"{cmd[0]} was not found. The build needs CMake and Ninja, "
            "and a C++20 compiler."
        ) from error


class build_with_fold(build_py):
    def run(self):
        root = repo_root()
        package = Path(__file__).resolve().parent / "h5plot"
        name = _library_name()
        # A library left from another platform matches package-data too.
        # The wheel would then carry a DLL beside a .so.
        for stale in ("h5plot.dll", "libh5plot.so", "libh5plot.dylib", "libh5plot.dll"):
            if stale != name:
                leftover = package / stale
                if leftover.is_file():
                    leftover.unlink()
        build = Path(tempfile.mkdtemp(prefix="h5plot-"))
        try:
            cmake = [
                "cmake",
                "-S",
                str(root / "python" / "h5plot"),
                "-B",
                str(build),
                "-G",
                "Ninja",
                "-DCMAKE_BUILD_TYPE=Release",
                f"-DH5SCOPE_SRC={root.joinpath('src').as_posix()}",
            ]
            arches = _osx_architectures()
            if arches:
                cmake.append(f"-DCMAKE_OSX_ARCHITECTURES={arches}")
            deploy = os.environ.get("MACOSX_DEPLOYMENT_TARGET")
            if deploy:
                cmake.append(f"-DCMAKE_OSX_DEPLOYMENT_TARGET={deploy}")
            # CMake tries g++ before cl. A MinGW DLL still needs libstdc++
            # and winpthread, which are not installed with the wheel.
            if sys.platform == "win32" and shutil.which("cl"):
                cmake.append("-DCMAKE_CXX_COMPILER=cl")
            _run(cmake)
            _run(["cmake", "--build", str(build)])
            built = build / name
            if not built.is_file():
                found = list(build.rglob(name))
                if len(found) != 1:
                    raise SystemExit(f"{name} was not produced in {build}")
                built = found[0]
            _reject_foreign_dlls(built)
            shutil.copy2(built, package / name)
        finally:
            shutil.rmtree(build, ignore_errors=True)
        super().run()


class install_platlib(install):
    def finalize_options(self):
        super().finalize_options()
        # distutils sends the package to purelib when ext_modules is empty.
        # This library is not an extension module, and auditwheel will not
        # repair a wheel that keeps a shared object under purelib.
        self.install_libbase = self.install_platlib
        self.install_lib = self.install_platlib


class bdist_wheel_abi_none(bdist_wheel):
    def finalize_options(self):
        super().finalize_options()
        # Pure would tag the wheel py3-none-any, and pip would install a
        # Windows DLL on Linux.
        self.root_is_pure = False

    def get_tag(self):
        _python, _abi, plat = super().get_tag()
        flags = os.environ.get("ARCHFLAGS", "")
        if sys.platform == "darwin" and "-arch arm64" in flags and "-arch x86_64" in flags:
            # The runner is one architecture. Both slices were compiled, so
            # the tag has to say universal2 or pip will treat it as arm64 only.
            deploy = os.environ.get("MACOSX_DEPLOYMENT_TARGET", "11.0").replace(".", "_")
            plat = f"macosx_{deploy}_universal2"
        return "py3", "none", plat


setup(
    cmdclass={
        "build_py": build_with_fold,
        "bdist_wheel": bdist_wheel_abi_none,
        "install": install_platlib,
    }
)
