// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

//! Compiles plotcore -- the core and its C interface -- into a static library
//! linked into whatever depends on this crate. No CMake and no libclang: the
//! sources are listed here and the bindings are checked in (src/lib.rs).

use std::path::PathBuf;

const SOURCES: &[&str] = &[
    "src/Budget.cpp",
    "src/Camera.cpp",
    "src/Figure.cpp",
    "src/Format.cpp",
    "src/Json.cpp",
    "src/Levels.cpp",
    "src/Palettes.cpp",
    "src/Parallel.cpp",
    "src/Plot.cpp",
    "src/Projection.cpp",
    "src/Pyramid.cpp",
    "src/Ticks.cpp",
    "capi/plotcore.cpp",
];

fn main() {
    let manifest = PathBuf::from(std::env::var("CARGO_MANIFEST_DIR").expect("cargo sets it"));
    // plotcore/rust/plotcore-sys -> plotcore
    let root = manifest.join("../..");
    let version = std::env::var("CARGO_PKG_VERSION").expect("cargo sets it");

    let mut build = cc::Build::new();
    build
        .cpp(true)
        .std("c++20")
        .include(root.join("include"))
        .include(root.join("capi"))
        .define("PLOTCORE_VERSION_STRING", Some(format!("\"{version}\"").as_str()))
        .warnings(false);
    if build.get_compiler().is_like_msvc() {
        build.flag("/utf-8");
    }
    for source in SOURCES {
        let path = root.join(source);
        println!("cargo:rerun-if-changed={}", path.display());
        build.file(path);
    }
    // Not plotcore: the C sizes of its structures, which the test holds the
    // checked-in bindings to.
    let sizes = manifest.join("csrc/sizes.cpp");
    println!("cargo:rerun-if-changed={}", sizes.display());
    build.file(sizes);
    for header in ["include/plotcore", "capi"] {
        println!("cargo:rerun-if-changed={}", root.join(header).display());
    }
    build.compile("plotcore");

    // The pool's threads.
    let target = std::env::var("CARGO_CFG_TARGET_OS").unwrap_or_default();
    if target == "linux" {
        println!("cargo:rustc-link-lib=pthread");
    }
}
