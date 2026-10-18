// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

//! plotcore.h, declared for Rust.
//!
//! Written out once from `plotcore/capi/plotcore.h` and checked in, so building
//! this needs a C++ compiler and nothing else -- no libclang. They follow the
//! header line for line: a change to one is a change to the other, and
//! `PLOTCORE_ABI_VERSION` is what says they are the same. The safe interface
//! is the `plotcore` crate.

#![allow(non_camel_case_types)]
#![allow(clippy::missing_safety_doc)]

use std::ffi::{c_char, c_double, c_int, c_void};

pub const PLOTCORE_ABI_VERSION: i32 = 1;

pub type plotcore_status = c_int;
pub const PLOTCORE_OK: plotcore_status = 0;
pub const PLOTCORE_ERROR_ARGUMENT: plotcore_status = 1;
pub const PLOTCORE_ERROR_RANGE: plotcore_status = 2;
pub const PLOTCORE_ERROR_STATE: plotcore_status = 3;
pub const PLOTCORE_ERROR_INTERNAL: plotcore_status = 4;

pub type plotcore_button = c_int;
pub const PLOTCORE_BUTTON_LEFT: plotcore_button = 0;
pub const PLOTCORE_BUTTON_MIDDLE: plotcore_button = 1;
pub const PLOTCORE_BUTTON_RIGHT: plotcore_button = 2;

pub const PLOTCORE_SHIFT: i32 = 1;
pub const PLOTCORE_CONTROL: i32 = 2;
pub const PLOTCORE_ALT: i32 = 4;

pub const PLOTCORE_TEXT_TICK: i32 = 0;
pub const PLOTCORE_TEXT_TICK_MINOR: i32 = 1;
pub const PLOTCORE_TEXT_TITLE: i32 = 2;
pub const PLOTCORE_TEXT_AXIS_NAME: i32 = 3;
pub const PLOTCORE_TEXT_LEGEND: i32 = 4;

pub const PLOTCORE_ALIGN_START: i32 = 0;
pub const PLOTCORE_ALIGN_MIDDLE: i32 = 1;
pub const PLOTCORE_ALIGN_END: i32 = 2;

pub type plotcore_appetite = c_int;
pub const PLOTCORE_APPETITE_LOW: plotcore_appetite = 0;
pub const PLOTCORE_APPETITE_MEDIUM: plotcore_appetite = 1;
pub const PLOTCORE_APPETITE_GREEDY: plotcore_appetite = 2;

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct plotcore_rgba {
    pub red: c_double,
    pub green: c_double,
    pub blue: c_double,
    pub alpha: c_double,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct plotcore_chrome {
    pub ground: plotcore_rgba,
    pub ink: plotcore_rgba,
    pub rule_minor: plotcore_rgba,
    pub rule_major: plotcore_rgba,
    pub axis_rule: plotcore_rgba,
    pub cursor_ink: plotcore_rgba,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct plotcore_rect {
    pub x: c_double,
    pub y: c_double,
    pub width: c_double,
    pub height: c_double,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct plotcore_segment {
    pub x0: c_double,
    pub y0: c_double,
    pub x1: c_double,
    pub y1: c_double,
    pub colour: plotcore_rgba,
    pub width: c_double,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct plotcore_line_info {
    pub line: i32,
    pub colour: plotcore_rgba,
    pub width: c_double,
    pub marker_radius: c_double,
    pub run_count: i64,
    pub triangle_points: i64,
    pub marker_count: i64,
    pub y_low: c_double,
    pub y_high: c_double,
    pub y_log: i32,
    pub own: i32,
    pub numbered: i32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct plotcore_text {
    pub x: c_double,
    pub y: c_double,
    pub across: i32,
    pub down: i32,
    pub role: i32,
    pub vertical: i32,
    pub colour: plotcore_rgba,
    pub text: *const c_char,
    pub base: *const c_char,
    pub exponent: *const c_char,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct plotcore_legend_entry {
    pub line: i32,
    pub colour: plotcore_rgba,
    pub x0: c_double,
    pub y0: c_double,
    pub x1: c_double,
    pub y1: c_double,
    pub text_x: c_double,
    pub text_y: c_double,
    pub name: *const c_char,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct plotcore_reading {
    pub valid: i32,
    pub line: i32,
    pub x: c_double,
    pub y: c_double,
    pub px: c_double,
    pub py: c_double,
    pub x_text: *const c_char,
    pub y_text: *const c_char,
}

/// An opaque figure.
#[repr(C)]
pub struct plotcore_figure {
    _private: [u8; 0],
}

/// An opaque scene.
#[repr(C)]
pub struct plotcore_scene {
    _private: [u8; 0],
}

pub type plotcore_reader = Option<
    unsafe extern "C" fn(context: *mut c_void, first: i64, count: i64, out: *mut c_double) -> c_int,
>;
pub type plotcore_free_context = Option<unsafe extern "C" fn(context: *mut c_void)>;
pub type plotcore_text_width =
    Option<unsafe extern "C" fn(context: *mut c_void, text: *const c_char, role: i32) -> c_double>;
pub type plotcore_text_height =
    Option<unsafe extern "C" fn(context: *mut c_void, role: i32) -> c_double>;

unsafe extern "C" {
    // --- the library
    pub fn plotcore_abi_version() -> i32;
    pub fn plotcore_version() -> *const c_char;
    pub fn plotcore_last_error() -> *const c_char;
    pub fn plotcore_palette_count() -> i32;
    pub fn plotcore_palette_name(index: i32) -> *const c_char;
    pub fn plotcore_ramp_count() -> i32;
    pub fn plotcore_ramp_name(index: i32) -> *const c_char;
    pub fn plotcore_palette(
        name: *const c_char,
        on_dark: i32,
        out: *mut plotcore_rgba,
        capacity: i32,
    ) -> i32;

    // --- a figure
    pub fn plotcore_figure_new() -> *mut plotcore_figure;
    pub fn plotcore_figure_free(figure: *mut plotcore_figure);

    // --- lines
    pub fn plotcore_add_line(
        figure: *mut plotcore_figure,
        values: *const c_double,
        count: i64,
        name: *const c_char,
        line: *mut i32,
    ) -> plotcore_status;
    pub fn plotcore_begin_line(
        figure: *mut plotcore_figure,
        count: i64,
        name: *const c_char,
        line: *mut i32,
    ) -> plotcore_status;
    pub fn plotcore_append_line(
        figure: *mut plotcore_figure,
        line: i32,
        values: *const c_double,
        count: i64,
    ) -> plotcore_status;
    pub fn plotcore_finish_line(figure: *mut plotcore_figure, line: i32) -> plotcore_status;
    pub fn plotcore_set_line_reader(
        figure: *mut plotcore_figure,
        line: i32,
        reader: plotcore_reader,
        context: *mut c_void,
        release: plotcore_free_context,
    ) -> plotcore_status;
    pub fn plotcore_remove_line(figure: *mut plotcore_figure, line: i32) -> plotcore_status;
    pub fn plotcore_clear(figure: *mut plotcore_figure) -> plotcore_status;
    pub fn plotcore_line_count(figure: *const plotcore_figure) -> i32;
    pub fn plotcore_line_length(figure: *const plotcore_figure, line: i32) -> i64;
    pub fn plotcore_set_line_name(
        figure: *mut plotcore_figure,
        line: i32,
        name: *const c_char,
    ) -> plotcore_status;
    pub fn plotcore_line_extent(
        figure: *const plotcore_figure,
        line: i32,
        minimum: *mut c_double,
        maximum: *mut c_double,
    ) -> plotcore_status;
    pub fn plotcore_line_base_bucket(figure: *const plotcore_figure, line: i32) -> i64;
    pub fn plotcore_get_line_name(
        figure: *const plotcore_figure,
        line: i32,
        buffer: *mut c_char,
        capacity: i64,
        length: *mut i64,
    ) -> plotcore_status;
    pub fn plotcore_line_ready(figure: *const plotcore_figure, line: i32) -> i32;
    pub fn plotcore_line_has_time(figure: *const plotcore_figure, line: i32) -> i32;

    // --- x
    pub fn plotcore_set_index_axis(
        figure: *mut plotcore_figure,
        start: c_double,
        step: c_double,
    ) -> plotcore_status;
    pub fn plotcore_set_time(
        figure: *mut plotcore_figure,
        values: *const c_double,
        count: i64,
    ) -> plotcore_status;
    pub fn plotcore_set_line_time(
        figure: *mut plotcore_figure,
        line: i32,
        values: *const c_double,
        count: i64,
    ) -> plotcore_status;

    // --- holding
    pub fn plotcore_set_appetite(
        figure: *mut plotcore_figure,
        appetite: plotcore_appetite,
    ) -> plotcore_status;
    pub fn plotcore_set_budget_bytes(figure: *mut plotcore_figure, bytes: i64) -> plotcore_status;
    pub fn plotcore_budget_bytes(figure: *const plotcore_figure) -> i64;
    pub fn plotcore_held_bytes(figure: *const plotcore_figure) -> i64;

    // --- settings
    pub fn plotcore_set_bool(
        figure: *mut plotcore_figure,
        name: *const c_char,
        value: i32,
    ) -> plotcore_status;
    pub fn plotcore_set_number(
        figure: *mut plotcore_figure,
        name: *const c_char,
        value: c_double,
    ) -> plotcore_status;
    pub fn plotcore_set_text(
        figure: *mut plotcore_figure,
        name: *const c_char,
        value: *const c_char,
    ) -> plotcore_status;
    pub fn plotcore_set_colour(
        figure: *mut plotcore_figure,
        name: *const c_char,
        value: plotcore_rgba,
    ) -> plotcore_status;
    pub fn plotcore_get_bool(
        figure: *const plotcore_figure,
        name: *const c_char,
        value: *mut i32,
    ) -> plotcore_status;
    pub fn plotcore_get_number(
        figure: *const plotcore_figure,
        name: *const c_char,
        value: *mut c_double,
    ) -> plotcore_status;
    pub fn plotcore_get_text(
        figure: *const plotcore_figure,
        name: *const c_char,
        buffer: *mut c_char,
        capacity: i64,
        length: *mut i64,
    ) -> plotcore_status;
    pub fn plotcore_get_colour(
        figure: *const plotcore_figure,
        name: *const c_char,
        value: *mut plotcore_rgba,
    ) -> plotcore_status;
    pub fn plotcore_set_line_bool(
        figure: *mut plotcore_figure,
        line: i32,
        name: *const c_char,
        value: i32,
    ) -> plotcore_status;
    pub fn plotcore_set_line_number(
        figure: *mut plotcore_figure,
        line: i32,
        name: *const c_char,
        value: c_double,
    ) -> plotcore_status;
    pub fn plotcore_set_line_text(
        figure: *mut plotcore_figure,
        line: i32,
        name: *const c_char,
        value: *const c_char,
    ) -> plotcore_status;
    pub fn plotcore_set_line_colour(
        figure: *mut plotcore_figure,
        line: i32,
        value: plotcore_rgba,
    ) -> plotcore_status;
    pub fn plotcore_clear_line_colour(figure: *mut plotcore_figure, line: i32) -> plotcore_status;
    pub fn plotcore_get_line_bool(
        figure: *const plotcore_figure,
        line: i32,
        name: *const c_char,
        value: *mut i32,
    ) -> plotcore_status;
    pub fn plotcore_get_line_number(
        figure: *const plotcore_figure,
        line: i32,
        name: *const c_char,
        value: *mut c_double,
    ) -> plotcore_status;
    pub fn plotcore_get_line_text(
        figure: *const plotcore_figure,
        line: i32,
        name: *const c_char,
        buffer: *mut c_char,
        capacity: i64,
        length: *mut i64,
    ) -> plotcore_status;
    pub fn plotcore_get_line_colour(
        figure: *const plotcore_figure,
        line: i32,
        has: *mut i32,
        value: *mut plotcore_rgba,
    ) -> plotcore_status;
    pub fn plotcore_set_style(
        figure: *mut plotcore_figure,
        name: *const c_char,
        value: c_double,
    ) -> plotcore_status;
    pub fn plotcore_set_text_measure(
        figure: *mut plotcore_figure,
        width: plotcore_text_width,
        height: plotcore_text_height,
        context: *mut c_void,
    ) -> plotcore_status;

    // --- the pane and the gestures
    pub fn plotcore_set_size(
        figure: *mut plotcore_figure,
        width: c_double,
        height: c_double,
        pixel_ratio: c_double,
    ) -> plotcore_status;
    pub fn plotcore_wheel(
        figure: *mut plotcore_figure,
        px: c_double,
        py: c_double,
        angle_delta: c_double,
        modifiers: i32,
    ) -> plotcore_status;
    pub fn plotcore_press(
        figure: *mut plotcore_figure,
        px: c_double,
        py: c_double,
        button: plotcore_button,
        modifiers: i32,
    ) -> plotcore_status;
    pub fn plotcore_move(
        figure: *mut plotcore_figure,
        px: c_double,
        py: c_double,
        modifiers: i32,
    ) -> plotcore_status;
    pub fn plotcore_release(
        figure: *mut plotcore_figure,
        px: c_double,
        py: c_double,
        button: plotcore_button,
        modifiers: i32,
    ) -> plotcore_status;
    pub fn plotcore_double_click(
        figure: *mut plotcore_figure,
        px: c_double,
        py: c_double,
        button: plotcore_button,
    ) -> plotcore_status;
    pub fn plotcore_hover(
        figure: *mut plotcore_figure,
        px: c_double,
        py: c_double,
    ) -> plotcore_status;
    pub fn plotcore_leave(figure: *mut plotcore_figure) -> plotcore_status;
    pub fn plotcore_reset_view(figure: *mut plotcore_figure) -> plotcore_status;
    pub fn plotcore_set_view(
        figure: *mut plotcore_figure,
        x0: c_double,
        x1: c_double,
        y0: c_double,
        y1: c_double,
    ) -> plotcore_status;
    pub fn plotcore_view(
        figure: *const plotcore_figure,
        x0: *mut c_double,
        x1: *mut c_double,
        y0: *mut c_double,
        y1: *mut c_double,
    ) -> plotcore_status;
    pub fn plotcore_line_at(
        figure: *mut plotcore_figure,
        px: c_double,
        py: c_double,
        reach: c_double,
    ) -> i32;

    // --- a view, written down
    pub fn plotcore_state(
        figure: *const plotcore_figure,
        buffer: *mut c_char,
        capacity: i64,
        length: *mut i64,
    ) -> plotcore_status;
    pub fn plotcore_restore(figure: *mut plotcore_figure, json: *const c_char) -> plotcore_status;

    // --- one frame
    pub fn plotcore_scene_new(figure: *mut plotcore_figure) -> *mut plotcore_scene;
    pub fn plotcore_scene_free(scene: *mut plotcore_scene);
    pub fn plotcore_scene_size(
        scene: *const plotcore_scene,
        width: *mut c_double,
        height: *mut c_double,
    );
    pub fn plotcore_scene_ground(scene: *const plotcore_scene) -> plotcore_rgba;
    pub fn plotcore_scene_chrome(scene: *const plotcore_scene) -> plotcore_chrome;
    pub fn plotcore_scene_area(scene: *const plotcore_scene) -> plotcore_rect;
    pub fn plotcore_scene_view(
        scene: *const plotcore_scene,
        x0: *mut c_double,
        x1: *mut c_double,
        y0: *mut c_double,
        y1: *mut c_double,
    );
    pub fn plotcore_scene_message(scene: *const plotcore_scene) -> *const c_char;
    pub fn plotcore_scene_line_count(scene: *const plotcore_scene) -> i64;
    pub fn plotcore_scene_line(
        scene: *const plotcore_scene,
        index: i64,
        out: *mut plotcore_line_info,
    ) -> plotcore_status;
    pub fn plotcore_scene_line_run_length(
        scene: *const plotcore_scene,
        index: i64,
        run: i64,
    ) -> i64;
    pub fn plotcore_scene_line_run(
        scene: *const plotcore_scene,
        index: i64,
        run: i64,
    ) -> *const c_double;
    pub fn plotcore_scene_line_run_values(
        scene: *const plotcore_scene,
        index: i64,
        run: i64,
    ) -> *const c_double;
    pub fn plotcore_scene_line_triangles(
        scene: *const plotcore_scene,
        index: i64,
    ) -> *const c_double;
    pub fn plotcore_scene_line_markers(scene: *const plotcore_scene, index: i64)
        -> *const c_double;
    pub fn plotcore_scene_rule_count(scene: *const plotcore_scene) -> i64;
    pub fn plotcore_scene_rules(scene: *const plotcore_scene) -> *const plotcore_segment;
    pub fn plotcore_scene_stroke_count(scene: *const plotcore_scene) -> i64;
    pub fn plotcore_scene_strokes(scene: *const plotcore_scene) -> *const plotcore_segment;
    pub fn plotcore_scene_text_count(scene: *const plotcore_scene) -> i64;
    pub fn plotcore_scene_text(
        scene: *const plotcore_scene,
        index: i64,
        out: *mut plotcore_text,
    ) -> plotcore_status;
    pub fn plotcore_scene_legend_count(scene: *const plotcore_scene) -> i64;
    pub fn plotcore_scene_legend(
        scene: *const plotcore_scene,
        index: i64,
        out: *mut plotcore_legend_entry,
    ) -> plotcore_status;
    pub fn plotcore_scene_legend_rect(scene: *const plotcore_scene) -> plotcore_rect;
    pub fn plotcore_scene_reading(scene: *const plotcore_scene) -> plotcore_reading;
    pub fn plotcore_scene_band(scene: *const plotcore_scene, band: *mut plotcore_rect) -> i32;
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_library_linked_in_speaks_this_abi() {
        // SAFETY: no arguments, no state.
        assert_eq!(unsafe { plotcore_abi_version() }, PLOTCORE_ABI_VERSION);
    }

    unsafe extern "C" {
        fn plotcore_sys_size_of(which: c_int) -> usize;
    }

    #[test]
    fn the_layouts_are_the_ones_the_c_compiler_makes() {
        // Asked of the compiler that built the library, on this target, rather
        // than worked out by hand -- which got two of these wrong.
        let rust = [
            std::mem::size_of::<plotcore_rgba>(),
            std::mem::size_of::<plotcore_chrome>(),
            std::mem::size_of::<plotcore_rect>(),
            std::mem::size_of::<plotcore_segment>(),
            std::mem::size_of::<plotcore_line_info>(),
            std::mem::size_of::<plotcore_text>(),
            std::mem::size_of::<plotcore_legend_entry>(),
            std::mem::size_of::<plotcore_reading>(),
        ];
        for (which, size) in rust.iter().enumerate() {
            // SAFETY: a pure function of its argument.
            let c = unsafe { plotcore_sys_size_of(which as c_int) };
            assert_eq!(*size, c, "structure {which}");
        }
    }
}
