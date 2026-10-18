// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-FileCopyrightText: 2026 Andreas Martin
// SPDX-License-Identifier: GPL-3.0-only

//! The plot H5Scope draws, for Rust.
//!
//! ```
//! let mut figure = plotcore::Figure::new();
//! let sine: Vec<f64> = (0..1_000_000).map(|i| (i as f64 / 1000.0).sin()).collect();
//! figure.add_line(&sine, "sine").unwrap();
//! figure.set_size(800.0, 500.0, 1.0).unwrap();
//! figure.wheel(400.0, 250.0, 360.0, plotcore::Modifiers::NONE).unwrap();
//! let scene = figure.scene().unwrap();
//! assert_eq!(scene.lines.len(), 1);
//! ```
//!
//! A [`Figure`] is plotcore::Figure across the C interface: lines copied in,
//! settings named as a saved view names them, gestures forwarded, a [`Scene`]
//! copied out. Nothing here decides anything about the picture, which is what
//! makes a figure drawn from Rust the one drawn from C++ and Python --
//! `tests/golden.rs` holds the three to one digest. `plotcore-egui` paints a
//! scene in egui.

use std::ffi::{c_char, c_double, c_int, c_void, CStr, CString};
use std::fmt;
use std::ptr::NonNull;

use plotcore_sys as sys;

/// A call plotcore refused: the status it gave and why.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Error {
    pub status: Status,
    pub message: String,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Status {
    /// A null pointer, a name that is not a setting, a value it cannot take.
    Argument,
    /// A line that does not exist.
    Range,
    /// A call out of order, such as appending to a finished line.
    State,
    /// Out of memory, or anything else plotcore threw.
    Internal,
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{:?}: {}", self.status, self.message)
    }
}

impl std::error::Error for Error {}

pub type Result<T> = std::result::Result<T, Error>;

fn last_error() -> String {
    // SAFETY: a static, thread-local string the library owns.
    unsafe { text_of(sys::plotcore_last_error()) }
}

fn check(status: sys::plotcore_status) -> Result<()> {
    let status = match status {
        sys::PLOTCORE_OK => return Ok(()),
        sys::PLOTCORE_ERROR_ARGUMENT => Status::Argument,
        sys::PLOTCORE_ERROR_RANGE => Status::Range,
        sys::PLOTCORE_ERROR_STATE => Status::State,
        _ => Status::Internal,
    };
    Err(Error { status, message: last_error() })
}

/// # Safety
/// `raw` is null or a NUL-terminated string that lives for this call.
unsafe fn text_of(raw: *const c_char) -> String {
    if raw.is_null() {
        return String::new();
    }
    // SAFETY: the caller's promise.
    unsafe { CStr::from_ptr(raw) }.to_string_lossy().into_owned()
}

fn c_string(text: &str) -> Result<CString> {
    CString::new(text).map_err(|_| Error {
        status: Status::Argument,
        message: format!("a string with a NUL in it: {text:?}"),
    })
}

/// A string the C side copies out: asked its length, then asked again.
fn copied(
    mut call: impl FnMut(*mut c_char, i64, *mut i64) -> sys::plotcore_status,
) -> Result<String> {
    let mut length = 0i64;
    check(call(std::ptr::null_mut(), 0, &mut length))?;
    let mut buffer = vec![0u8; length as usize + 1];
    check(call(buffer.as_mut_ptr().cast(), length + 1, std::ptr::null_mut()))?;
    buffer.truncate(length as usize);
    Ok(String::from_utf8_lossy(&buffer).into_owned())
}

/// A colour, each channel from 0 to 1.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Rgba {
    pub red: f64,
    pub green: f64,
    pub blue: f64,
    pub alpha: f64,
}

impl Rgba {
    pub const fn new(red: f64, green: f64, blue: f64, alpha: f64) -> Self {
        Self { red, green, blue, alpha }
    }
}

impl From<sys::plotcore_rgba> for Rgba {
    fn from(c: sys::plotcore_rgba) -> Self {
        Self::new(c.red, c.green, c.blue, c.alpha)
    }
}

impl From<Rgba> for sys::plotcore_rgba {
    fn from(c: Rgba) -> Self {
        sys::plotcore_rgba { red: c.red, green: c.green, blue: c.blue, alpha: c.alpha }
    }
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Rect {
    pub x: f64,
    pub y: f64,
    pub width: f64,
    pub height: f64,
}

impl From<sys::plotcore_rect> for Rect {
    fn from(r: sys::plotcore_rect) -> Self {
        Self { x: r.x, y: r.y, width: r.width, height: r.height }
    }
}

/// Which button a press came from.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum Button {
    Left,
    Middle,
    Right,
}

impl Button {
    fn raw(self) -> sys::plotcore_button {
        match self {
            Button::Left => sys::PLOTCORE_BUTTON_LEFT,
            Button::Middle => sys::PLOTCORE_BUTTON_MIDDLE,
            Button::Right => sys::PLOTCORE_BUTTON_RIGHT,
        }
    }
}

/// Modifier keys, or-ed together.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Hash)]
pub struct Modifiers(pub i32);

impl Modifiers {
    pub const NONE: Self = Self(0);
    pub const SHIFT: Self = Self(sys::PLOTCORE_SHIFT);
    pub const CONTROL: Self = Self(sys::PLOTCORE_CONTROL);
    pub const ALT: Self = Self(sys::PLOTCORE_ALT);

    pub const fn contains(self, other: Self) -> bool {
        self.0 & other.0 == other.0
    }
}

impl std::ops::BitOr for Modifiers {
    type Output = Self;
    fn bitor(self, other: Self) -> Self {
        Self(self.0 | other.0)
    }
}

impl std::ops::BitOrAssign for Modifiers {
    fn bitor_assign(&mut self, other: Self) {
        self.0 |= other.0;
    }
}

/// How much of the machine the lines may hold between them.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Appetite {
    Low,
    Medium,
    Greedy,
}

/// The faces text is set in; a host maps each to a font.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum TextRole {
    Tick,
    TickMinor,
    Title,
    AxisName,
    Legend,
}

impl TextRole {
    pub const ALL: [TextRole; 5] = [
        TextRole::Tick,
        TextRole::TickMinor,
        TextRole::Title,
        TextRole::AxisName,
        TextRole::Legend,
    ];

    fn of(raw: i32) -> Self {
        match raw {
            sys::PLOTCORE_TEXT_TICK_MINOR => TextRole::TickMinor,
            sys::PLOTCORE_TEXT_TITLE => TextRole::Title,
            sys::PLOTCORE_TEXT_AXIS_NAME => TextRole::AxisName,
            sys::PLOTCORE_TEXT_LEGEND => TextRole::Legend,
            _ => TextRole::Tick,
        }
    }

    /// The name of the style measurement that is this face's size.
    pub fn style_name(self) -> &'static str {
        match self {
            TextRole::Tick => "tickPixels",
            TextRole::TickMinor => "tickMinorPixels",
            TextRole::Title => "titlePixels",
            TextRole::AxisName => "axisNamePixels",
            TextRole::Legend => "legendPixels",
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Align {
    Start,
    Middle,
    End,
}

impl Align {
    fn of(raw: i32) -> Self {
        match raw {
            sys::PLOTCORE_ALIGN_MIDDLE => Align::Middle,
            sys::PLOTCORE_ALIGN_END => Align::End,
            _ => Align::Start,
        }
    }
}

// --- the library ------------------------------------------------------------------

/// The C library's version.
pub fn version() -> String {
    // SAFETY: a static string.
    unsafe { text_of(sys::plotcore_version()) }
}

pub fn abi_version() -> i32 {
    // SAFETY: no arguments, no state.
    unsafe { sys::plotcore_abi_version() }
}

/// The categorical cycles, in the order a settings panel offers them.
pub fn palette_names() -> Vec<String> {
    // SAFETY: static strings, by index within the count.
    unsafe {
        (0..sys::plotcore_palette_count()).map(|i| text_of(sys::plotcore_palette_name(i))).collect()
    }
}

/// The continuous maps, in the same sense.
pub fn ramp_names() -> Vec<String> {
    // SAFETY: as palette_names.
    unsafe {
        (0..sys::plotcore_ramp_count()).map(|i| text_of(sys::plotcore_ramp_name(i))).collect()
    }
}

/// A cycle's or a map's stops, or None for a name that is neither.
pub fn palette(name: &str, on_dark: bool) -> Option<Vec<Rgba>> {
    let name = CString::new(name).ok()?;
    // SAFETY: a count with no buffer, then a buffer of that count.
    unsafe {
        let count = sys::plotcore_palette(name.as_ptr(), on_dark as i32, std::ptr::null_mut(), 0);
        if count < 0 {
            return None;
        }
        let mut stops = vec![sys::plotcore_rgba::default(); count as usize];
        sys::plotcore_palette(name.as_ptr(), on_dark as i32, stops.as_mut_ptr(), count);
        Some(stops.into_iter().map(Rgba::from).collect())
    }
}

// --- the scene ---------------------------------------------------------------------

/// What everything that is not a line is drawn in.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Chrome {
    pub ground: Rgba,
    pub ink: Rgba,
    pub rule_minor: Rgba,
    pub rule_major: Rgba,
    pub axis_rule: Rgba,
    pub cursor_ink: Rgba,
}

/// A rule or a stroke.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Segment {
    pub x0: f64,
    pub y0: f64,
    pub x1: f64,
    pub y1: f64,
    pub colour: Rgba,
    pub width: f64,
}

/// One drawn line. Points are pane pixels with y down; `values` are the same
/// points in the data's own units.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct DrawnLine {
    pub line: i32,
    pub colour: Rgba,
    pub width: f64,
    pub marker_radius: f64,
    pub runs: Vec<Vec<[f64; 2]>>,
    pub values: Vec<Vec<[f64; 2]>>,
    /// The stroke as triangles, three points each.
    pub triangles: Vec<[f64; 2]>,
    pub markers: Vec<[f64; 2]>,
    pub y_low: f64,
    pub y_high: f64,
    pub y_log: bool,
    pub own: bool,
    pub numbered: bool,
}

/// A piece of text: `base` in `role`'s face, `exponent` raised beside it in
/// TickMinor's, `text` both in one string.
#[derive(Clone, Debug, PartialEq)]
pub struct Text {
    pub x: f64,
    pub y: f64,
    pub across: Align,
    pub down: Align,
    pub role: TextRole,
    pub vertical: bool,
    pub colour: Rgba,
    pub text: String,
    pub base: String,
    pub exponent: String,
}

#[derive(Clone, Debug, PartialEq)]
pub struct LegendEntry {
    pub line: i32,
    pub colour: Rgba,
    pub name: String,
    pub swatch: [f64; 4],
    pub text_at: [f64; 2],
}

/// The sample under the pointer.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct Reading {
    pub line: i32,
    pub x: f64,
    pub y: f64,
    pub px: f64,
    pub py: f64,
    pub x_text: String,
    pub y_text: String,
}

/// One frame, owned: nothing in it refers to the library.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct Scene {
    pub width: f64,
    pub height: f64,
    pub ground: Rgba,
    pub chrome: Chrome,
    pub area: Rect,
    /// x0, x1, y0, y1.
    pub view: [f64; 4],
    pub message: String,
    pub lines: Vec<DrawnLine>,
    pub rules: Vec<Segment>,
    pub strokes: Vec<Segment>,
    pub texts: Vec<Text>,
    pub legend: Vec<LegendEntry>,
    pub legend_rect: Option<Rect>,
    pub reading: Option<Reading>,
    pub band: Option<Rect>,
}

/// # Safety
/// `pointer` is null or holds `count` x, y pairs.
unsafe fn points(pointer: *const c_double, count: i64) -> Vec<[f64; 2]> {
    if pointer.is_null() || count <= 0 {
        return Vec::new();
    }
    // SAFETY: the caller's promise.
    let flat = unsafe { std::slice::from_raw_parts(pointer, count as usize * 2) };
    flat.chunks_exact(2).map(|p| [p[0], p[1]]).collect()
}

/// # Safety
/// `pointer` is null or holds `count` segments.
unsafe fn segments(pointer: *const sys::plotcore_segment, count: i64) -> Vec<Segment> {
    if pointer.is_null() || count <= 0 {
        return Vec::new();
    }
    // SAFETY: the caller's promise.
    let raw = unsafe { std::slice::from_raw_parts(pointer, count as usize) };
    raw.iter()
        .map(|s| Segment {
            x0: s.x0,
            y0: s.y0,
            x1: s.x1,
            y1: s.y1,
            colour: s.colour.into(),
            width: s.width,
        })
        .collect()
}

/// # Safety
/// `scene` is a live scene.
unsafe fn copy_scene(scene: *const sys::plotcore_scene) -> Scene {
    // SAFETY: every call below reads `scene` and copies out of it.
    unsafe {
        let mut out = Scene::default();
        sys::plotcore_scene_size(scene, &mut out.width, &mut out.height);
        out.ground = sys::plotcore_scene_ground(scene).into();
        let c = sys::plotcore_scene_chrome(scene);
        out.chrome = Chrome {
            ground: c.ground.into(),
            ink: c.ink.into(),
            rule_minor: c.rule_minor.into(),
            rule_major: c.rule_major.into(),
            axis_rule: c.axis_rule.into(),
            cursor_ink: c.cursor_ink.into(),
        };
        out.area = sys::plotcore_scene_area(scene).into();
        let [x0, x1, y0, y1] = &mut out.view;
        sys::plotcore_scene_view(scene, x0, x1, y0, y1);
        out.message = text_of(sys::plotcore_scene_message(scene));
        for index in 0..sys::plotcore_scene_line_count(scene) {
            let mut info = sys::plotcore_line_info::default();
            sys::plotcore_scene_line(scene, index, &mut info);
            let mut line = DrawnLine {
                line: info.line,
                colour: info.colour.into(),
                width: info.width,
                marker_radius: info.marker_radius,
                y_low: info.y_low,
                y_high: info.y_high,
                y_log: info.y_log != 0,
                own: info.own != 0,
                numbered: info.numbered != 0,
                triangles: points(
                    sys::plotcore_scene_line_triangles(scene, index),
                    info.triangle_points,
                ),
                markers: points(sys::plotcore_scene_line_markers(scene, index), info.marker_count),
                ..DrawnLine::default()
            };
            for run in 0..info.run_count {
                let count = sys::plotcore_scene_line_run_length(scene, index, run);
                line.runs.push(points(sys::plotcore_scene_line_run(scene, index, run), count));
                line.values
                    .push(points(sys::plotcore_scene_line_run_values(scene, index, run), count));
            }
            out.lines.push(line);
        }
        out.rules =
            segments(sys::plotcore_scene_rules(scene), sys::plotcore_scene_rule_count(scene));
        out.strokes =
            segments(sys::plotcore_scene_strokes(scene), sys::plotcore_scene_stroke_count(scene));
        for index in 0..sys::plotcore_scene_text_count(scene) {
            let mut t = std::mem::zeroed::<sys::plotcore_text>();
            sys::plotcore_scene_text(scene, index, &mut t);
            out.texts.push(Text {
                x: t.x,
                y: t.y,
                across: Align::of(t.across),
                down: Align::of(t.down),
                role: TextRole::of(t.role),
                vertical: t.vertical != 0,
                colour: t.colour.into(),
                text: text_of(t.text),
                base: text_of(t.base),
                exponent: text_of(t.exponent),
            });
        }
        for index in 0..sys::plotcore_scene_legend_count(scene) {
            let mut e = std::mem::zeroed::<sys::plotcore_legend_entry>();
            sys::plotcore_scene_legend(scene, index, &mut e);
            out.legend.push(LegendEntry {
                line: e.line,
                colour: e.colour.into(),
                name: text_of(e.name),
                swatch: [e.x0, e.y0, e.x1, e.y1],
                text_at: [e.text_x, e.text_y],
            });
        }
        if !out.legend.is_empty() {
            out.legend_rect = Some(sys::plotcore_scene_legend_rect(scene).into());
        }
        let r = sys::plotcore_scene_reading(scene);
        if r.valid != 0 {
            out.reading = Some(Reading {
                line: r.line,
                x: r.x,
                y: r.y,
                px: r.px,
                py: r.py,
                x_text: text_of(r.x_text),
                y_text: text_of(r.y_text),
            });
        }
        let mut band = sys::plotcore_rect::default();
        if sys::plotcore_scene_band(scene, &mut band) != 0 {
            out.band = Some(band.into());
        }
        out
    }
}

// --- callbacks ------------------------------------------------------------------------

/// Reads `out.len()` elements of a line from element `first`; false when it
/// cannot.
pub type Reader = Box<dyn FnMut(i64, &mut [f64]) -> bool + Send>;

unsafe extern "C" fn call_reader(
    context: *mut c_void,
    first: i64,
    count: i64,
    out: *mut c_double,
) -> c_int {
    if context.is_null() || out.is_null() || count < 0 {
        return 0;
    }
    // SAFETY: `context` is the Box<Reader> set_line_reader leaked, alive until
    // release_reader; `out` holds `count` doubles.
    let reader = unsafe { &mut *context.cast::<Reader>() };
    let slice = unsafe { std::slice::from_raw_parts_mut(out, count as usize) };
    // A panic must not unwind into C.
    std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| reader(first, slice))).unwrap_or(false)
        as c_int
}

unsafe extern "C" fn release_reader(context: *mut c_void) {
    if !context.is_null() {
        // SAFETY: the Box set_line_reader leaked, let go exactly once by the C side.
        drop(unsafe { Box::from_raw(context.cast::<Reader>()) });
    }
}

type WidthFn = Box<dyn Fn(&str, TextRole) -> f64 + Send>;
type HeightFn = Box<dyn Fn(TextRole) -> f64 + Send>;

struct Measure {
    width: WidthFn,
    height: HeightFn,
}

unsafe extern "C" fn measure_width(
    context: *mut c_void,
    text: *const c_char,
    role: i32,
) -> c_double {
    // SAFETY: `context` is the figure's Box<Measure>, alive while the figure
    // holds it; `text` is a NUL-terminated string for this call.
    let measure = unsafe { &*context.cast::<Measure>() };
    let text = unsafe { CStr::from_ptr(text) }.to_string_lossy();
    std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        (measure.width)(&text, TextRole::of(role))
    }))
    .unwrap_or(0.0)
}

unsafe extern "C" fn measure_height(context: *mut c_void, role: i32) -> c_double {
    // SAFETY: as measure_width.
    let measure = unsafe { &*context.cast::<Measure>() };
    std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| (measure.height)(TextRole::of(role))))
        .unwrap_or(0.0)
}

// --- the figure --------------------------------------------------------------------------

/// A plot: lines, settings, the view, the gestures, and a [`Scene`] out.
///
/// Settings are named as a saved view names them -- `set_bool("xLog", true)`;
/// plotcore.h lists them. Not `Sync`: one thread at a time, as with any widget.
pub struct Figure {
    handle: NonNull<sys::plotcore_figure>,
    measure: Option<Box<Measure>>,
}

// SAFETY: the handle is owned outright, and everything it calls back into
// (readers, the measure) is `Send`.
unsafe impl Send for Figure {}

impl Default for Figure {
    fn default() -> Self {
        Self::new()
    }
}

impl Drop for Figure {
    fn drop(&mut self) {
        // SAFETY: the figure is ours; freeing it releases its readers, and the
        // measure it calls back into is dropped only after.
        unsafe { sys::plotcore_figure_free(self.handle.as_ptr()) };
    }
}

impl Figure {
    pub fn new() -> Self {
        // SAFETY: no arguments.
        let raw = unsafe { sys::plotcore_figure_new() };
        Self { handle: NonNull::new(raw).expect("plotcore could not make a figure"), measure: None }
    }

    fn raw(&self) -> *mut sys::plotcore_figure {
        self.handle.as_ptr()
    }

    // --- lines

    /// Copy `values` in as a line; its index.
    pub fn add_line(&mut self, values: &[f64], name: &str) -> Result<i32> {
        let name = c_string(name)?;
        let mut line = 0;
        // SAFETY: `values` is read for this call only; the library copies it.
        check(unsafe {
            sys::plotcore_add_line(
                self.raw(),
                values.as_ptr(),
                values.len() as i64,
                name.as_ptr(),
                &mut line,
            )
        })?;
        Ok(line)
    }

    /// A line that arrives in pieces: `count` elements, appended in order and
    /// then finished. Not drawn until it is.
    pub fn begin_line(&mut self, count: usize, name: &str) -> Result<i32> {
        let name = c_string(name)?;
        let mut line = 0;
        // SAFETY: plain arguments.
        check(unsafe {
            sys::plotcore_begin_line(self.raw(), count as i64, name.as_ptr(), &mut line)
        })?;
        Ok(line)
    }

    pub fn append_line(&mut self, line: i32, values: &[f64]) -> Result<()> {
        // SAFETY: as add_line.
        check(unsafe {
            sys::plotcore_append_line(self.raw(), line, values.as_ptr(), values.len() as i64)
        })
    }

    pub fn finish_line(&mut self, line: i32) -> Result<()> {
        // SAFETY: plain arguments.
        check(unsafe { sys::plotcore_finish_line(self.raw(), line) })
    }

    /// Where a closer look finer than the line is held at reads from; None
    /// for nowhere. The reader is dropped when it is replaced, when the line
    /// goes, or with the figure.
    pub fn set_line_reader(&mut self, line: i32, reader: Option<Reader>) -> Result<()> {
        let Some(reader) = reader else {
            // SAFETY: a null reader clears it.
            return check(unsafe {
                sys::plotcore_set_line_reader(self.raw(), line, None, std::ptr::null_mut(), None)
            });
        };
        let context = Box::into_raw(Box::new(reader)).cast::<c_void>();
        // SAFETY: the C side owns `context` from here and gives it back to
        // release_reader exactly once -- or now, when the call fails.
        let status = unsafe {
            sys::plotcore_set_line_reader(
                self.raw(),
                line,
                Some(call_reader),
                context,
                Some(release_reader),
            )
        };
        if status != sys::PLOTCORE_OK && status != sys::PLOTCORE_ERROR_INTERNAL {
            // Refused before the reader was taken: it is still ours to let go.
            // SAFETY: not handed over.
            unsafe { release_reader(context) };
        }
        check(status)
    }

    pub fn remove_line(&mut self, line: i32) -> Result<()> {
        // SAFETY: plain arguments.
        check(unsafe { sys::plotcore_remove_line(self.raw(), line) })
    }

    pub fn clear(&mut self) -> Result<()> {
        // SAFETY: plain arguments.
        check(unsafe { sys::plotcore_clear(self.raw()) })
    }

    pub fn line_count(&self) -> i32 {
        // SAFETY: plain arguments.
        unsafe { sys::plotcore_line_count(self.raw()) }
    }

    pub fn line_length(&self, line: i32) -> i64 {
        // SAFETY: plain arguments.
        unsafe { sys::plotcore_line_length(self.raw(), line) }
    }

    pub fn line_name(&self, line: i32) -> Result<String> {
        // SAFETY: the buffer is the length copied asks for.
        copied(|buffer, capacity, length| unsafe {
            sys::plotcore_get_line_name(self.raw(), line, buffer, capacity, length)
        })
    }

    pub fn set_line_name(&mut self, line: i32, name: &str) -> Result<()> {
        let name = c_string(name)?;
        // SAFETY: plain arguments.
        check(unsafe { sys::plotcore_set_line_name(self.raw(), line, name.as_ptr()) })
    }

    /// Whether a line streamed in has finished arriving.
    pub fn line_ready(&self, line: i32) -> bool {
        // SAFETY: plain arguments.
        unsafe { sys::plotcore_line_ready(self.raw(), line) == 1 }
    }

    pub fn line_has_time(&self, line: i32) -> bool {
        // SAFETY: plain arguments.
        unsafe { sys::plotcore_line_has_time(self.raw(), line) == 1 }
    }

    /// The smallest and largest finite value of a line.
    pub fn line_extent(&self, line: i32) -> Result<(f64, f64)> {
        let (mut low, mut high) = (0.0, 0.0);
        // SAFETY: two out-parameters.
        check(unsafe { sys::plotcore_line_extent(self.raw(), line, &mut low, &mut high) })?;
        Ok((low, high))
    }

    /// The finest bucket a line is held at; one is every element.
    pub fn line_base_bucket(&self, line: i32) -> i64 {
        // SAFETY: plain arguments.
        unsafe { sys::plotcore_line_base_bucket(self.raw(), line) }
    }

    // --- x

    /// The x of element i of a line with no time: start + i * step.
    pub fn set_index_axis(&mut self, start: f64, step: f64) -> Result<()> {
        // SAFETY: plain arguments.
        check(unsafe { sys::plotcore_set_index_axis(self.raw(), start, step) })
    }

    /// A time base every line without its own is drawn against; None for none.
    pub fn set_time(&mut self, times: Option<&[f64]>) -> Result<()> {
        let (pointer, count) =
            times.map_or((std::ptr::null(), 0), |t| (t.as_ptr(), t.len() as i64));
        // SAFETY: copied.
        check(unsafe { sys::plotcore_set_time(self.raw(), pointer, count) })
    }

    /// A line's own time base, which beats the shared one.
    pub fn set_line_time(&mut self, line: i32, times: Option<&[f64]>) -> Result<()> {
        let (pointer, count) =
            times.map_or((std::ptr::null(), 0), |t| (t.as_ptr(), t.len() as i64));
        // SAFETY: copied.
        check(unsafe { sys::plotcore_set_line_time(self.raw(), line, pointer, count) })
    }

    // --- holding

    pub fn set_appetite(&mut self, appetite: Appetite) -> Result<()> {
        let raw = match appetite {
            Appetite::Low => sys::PLOTCORE_APPETITE_LOW,
            Appetite::Medium => sys::PLOTCORE_APPETITE_MEDIUM,
            Appetite::Greedy => sys::PLOTCORE_APPETITE_GREEDY,
        };
        // SAFETY: plain arguments.
        check(unsafe { sys::plotcore_set_appetite(self.raw(), raw) })
    }

    /// Pin what the lines may hold; zero goes back to the appetite.
    pub fn set_budget_bytes(&mut self, bytes: i64) -> Result<()> {
        // SAFETY: plain arguments.
        check(unsafe { sys::plotcore_set_budget_bytes(self.raw(), bytes) })
    }

    pub fn budget_bytes(&self) -> i64 {
        // SAFETY: plain arguments.
        unsafe { sys::plotcore_budget_bytes(self.raw()) }
    }

    pub fn held_bytes(&self) -> i64 {
        // SAFETY: plain arguments.
        unsafe { sys::plotcore_held_bytes(self.raw()) }
    }

    // --- settings

    pub fn set_bool(&mut self, name: &str, value: bool) -> Result<()> {
        let name = c_string(name)?;
        // SAFETY: plain arguments.
        check(unsafe { sys::plotcore_set_bool(self.raw(), name.as_ptr(), value as i32) })
    }

    pub fn set_number(&mut self, name: &str, value: f64) -> Result<()> {
        let name = c_string(name)?;
        // SAFETY: plain arguments.
        check(unsafe { sys::plotcore_set_number(self.raw(), name.as_ptr(), value) })
    }

    pub fn set_text(&mut self, name: &str, value: &str) -> Result<()> {
        let (name, value) = (c_string(name)?, c_string(value)?);
        // SAFETY: plain arguments.
        check(unsafe { sys::plotcore_set_text(self.raw(), name.as_ptr(), value.as_ptr()) })
    }

    pub fn set_colour(&mut self, name: &str, value: Rgba) -> Result<()> {
        let name = c_string(name)?;
        // SAFETY: plain arguments.
        check(unsafe { sys::plotcore_set_colour(self.raw(), name.as_ptr(), value.into()) })
    }

    pub fn get_bool(&self, name: &str) -> Result<bool> {
        let name = c_string(name)?;
        let mut value = 0;
        // SAFETY: one out-parameter.
        check(unsafe { sys::plotcore_get_bool(self.raw(), name.as_ptr(), &mut value) })?;
        Ok(value != 0)
    }

    pub fn get_number(&self, name: &str) -> Result<f64> {
        let name = c_string(name)?;
        let mut value = 0.0;
        // SAFETY: one out-parameter.
        check(unsafe { sys::plotcore_get_number(self.raw(), name.as_ptr(), &mut value) })?;
        Ok(value)
    }

    pub fn get_text(&self, name: &str) -> Result<String> {
        let name = c_string(name)?;
        // SAFETY: the buffer is the length copied asks for.
        copied(|buffer, capacity, length| unsafe {
            sys::plotcore_get_text(self.raw(), name.as_ptr(), buffer, capacity, length)
        })
    }

    pub fn get_colour(&self, name: &str) -> Result<Rgba> {
        let name = c_string(name)?;
        let mut value = sys::plotcore_rgba::default();
        // SAFETY: one out-parameter.
        check(unsafe { sys::plotcore_get_colour(self.raw(), name.as_ptr(), &mut value) })?;
        Ok(value.into())
    }

    pub fn set_line_bool(&mut self, line: i32, name: &str, value: bool) -> Result<()> {
        let name = c_string(name)?;
        // SAFETY: plain arguments.
        check(unsafe { sys::plotcore_set_line_bool(self.raw(), line, name.as_ptr(), value as i32) })
    }

    pub fn set_line_number(&mut self, line: i32, name: &str, value: f64) -> Result<()> {
        let name = c_string(name)?;
        // SAFETY: plain arguments.
        check(unsafe { sys::plotcore_set_line_number(self.raw(), line, name.as_ptr(), value) })
    }

    pub fn set_line_text(&mut self, line: i32, name: &str, value: &str) -> Result<()> {
        let (name, value) = (c_string(name)?, c_string(value)?);
        // SAFETY: plain arguments.
        check(unsafe {
            sys::plotcore_set_line_text(self.raw(), line, name.as_ptr(), value.as_ptr())
        })
    }

    /// A colour of its own over whatever cycle is in force, or None for the
    /// cycle's.
    pub fn set_line_colour(&mut self, line: i32, colour: Option<Rgba>) -> Result<()> {
        // SAFETY: plain arguments.
        check(unsafe {
            match colour {
                Some(c) => sys::plotcore_set_line_colour(self.raw(), line, c.into()),
                None => sys::plotcore_clear_line_colour(self.raw(), line),
            }
        })
    }

    pub fn line_bool(&self, line: i32, name: &str) -> Result<bool> {
        let name = c_string(name)?;
        let mut value = 0;
        // SAFETY: one out-parameter.
        check(unsafe { sys::plotcore_get_line_bool(self.raw(), line, name.as_ptr(), &mut value) })?;
        Ok(value != 0)
    }

    pub fn line_number(&self, line: i32, name: &str) -> Result<f64> {
        let name = c_string(name)?;
        let mut value = 0.0;
        // SAFETY: one out-parameter.
        check(unsafe {
            sys::plotcore_get_line_number(self.raw(), line, name.as_ptr(), &mut value)
        })?;
        Ok(value)
    }

    pub fn line_text(&self, line: i32, name: &str) -> Result<String> {
        let name = c_string(name)?;
        // SAFETY: the buffer is the length copied asks for.
        copied(|buffer, capacity, length| unsafe {
            sys::plotcore_get_line_text(self.raw(), line, name.as_ptr(), buffer, capacity, length)
        })
    }

    pub fn line_colour(&self, line: i32) -> Result<Option<Rgba>> {
        let mut has = 0;
        let mut value = sys::plotcore_rgba::default();
        // SAFETY: two out-parameters.
        check(unsafe { sys::plotcore_get_line_colour(self.raw(), line, &mut has, &mut value) })?;
        Ok((has != 0).then(|| value.into()))
    }

    /// One of the frame's measurements, by name: `tickPixels`, `gapS`, ...
    pub fn set_style(&mut self, name: &str, value: f64) -> Result<()> {
        let name = c_string(name)?;
        // SAFETY: plain arguments.
        check(unsafe { sys::plotcore_set_style(self.raw(), name.as_ptr(), value) })
    }

    pub fn style(&self, name: &str) -> Result<f64> {
        self.get_number(name)
    }

    /// How the host measures text, in pane pixels; None for an approximation
    /// that needs no font.
    pub fn set_text_measure(&mut self, measure: Option<(WidthFn, HeightFn)>) -> Result<()> {
        let Some((width, height)) = measure else {
            // SAFETY: a null measure goes back to the approximation; the old
            // one is dropped only once the library has stopped holding it.
            let status = unsafe {
                sys::plotcore_set_text_measure(self.raw(), None, None, std::ptr::null_mut())
            };
            self.measure = None;
            return check(status);
        };
        let boxed = Box::new(Measure { width, height });
        let context = std::ptr::from_ref::<Measure>(&boxed).cast_mut().cast::<c_void>();
        // SAFETY: the Box is held by the figure, so its address outlives every
        // call the library makes with it.
        let status = unsafe {
            sys::plotcore_set_text_measure(
                self.raw(),
                Some(measure_width),
                Some(measure_height),
                context,
            )
        };
        if status == sys::PLOTCORE_OK {
            self.measure = Some(boxed);
        }
        check(status)
    }

    // --- the pane and the gestures, in pane pixels with y down

    pub fn set_size(&mut self, width: f64, height: f64, pixel_ratio: f64) -> Result<()> {
        // SAFETY: plain arguments.
        check(unsafe { sys::plotcore_set_size(self.raw(), width, height, pixel_ratio) })
    }

    /// `angle_delta` in eighths of a degree: 120 is one notch. Shift zooms
    /// across the pane, Control up it; Alt and Control scale the line under
    /// the pointer.
    pub fn wheel(&mut self, x: f64, y: f64, angle_delta: f64, modifiers: Modifiers) -> Result<()> {
        // SAFETY: plain arguments.
        check(unsafe { sys::plotcore_wheel(self.raw(), x, y, angle_delta, modifiers.0) })
    }

    pub fn press(&mut self, x: f64, y: f64, button: Button, modifiers: Modifiers) -> Result<()> {
        // SAFETY: plain arguments.
        check(unsafe { sys::plotcore_press(self.raw(), x, y, button.raw(), modifiers.0) })
    }

    pub fn move_to(&mut self, x: f64, y: f64, modifiers: Modifiers) -> Result<()> {
        // SAFETY: plain arguments.
        check(unsafe { sys::plotcore_move(self.raw(), x, y, modifiers.0) })
    }

    pub fn release(&mut self, x: f64, y: f64, button: Button, modifiers: Modifiers) -> Result<()> {
        // SAFETY: plain arguments.
        check(unsafe { sys::plotcore_release(self.raw(), x, y, button.raw(), modifiers.0) })
    }

    pub fn double_click(&mut self, x: f64, y: f64, button: Button) -> Result<()> {
        // SAFETY: plain arguments.
        check(unsafe { sys::plotcore_double_click(self.raw(), x, y, button.raw()) })
    }

    pub fn hover(&mut self, x: f64, y: f64) -> Result<()> {
        // SAFETY: plain arguments.
        check(unsafe { sys::plotcore_hover(self.raw(), x, y) })
    }

    pub fn leave(&mut self) -> Result<()> {
        // SAFETY: plain arguments.
        check(unsafe { sys::plotcore_leave(self.raw()) })
    }

    pub fn reset_view(&mut self) -> Result<()> {
        // SAFETY: plain arguments.
        check(unsafe { sys::plotcore_reset_view(self.raw()) })
    }

    /// The window named in values, clipped to what the axes can draw.
    pub fn set_view(&mut self, x0: f64, x1: f64, y0: f64, y1: f64) -> Result<()> {
        // SAFETY: plain arguments.
        check(unsafe { sys::plotcore_set_view(self.raw(), x0, x1, y0, y1) })
    }

    /// x0, x1, y0, y1.
    pub fn view(&self) -> [f64; 4] {
        let mut v = [0.0; 4];
        let [x0, x1, y0, y1] = &mut v;
        // SAFETY: four out-parameters.
        unsafe { sys::plotcore_view(self.raw(), x0, x1, y0, y1) };
        v
    }

    /// The line drawn nearest (x, y) within `reach` pixels.
    pub fn line_at(&mut self, x: f64, y: f64, reach: f64) -> Option<i32> {
        // SAFETY: plain arguments.
        let found = unsafe { sys::plotcore_line_at(self.raw(), x, y, reach) };
        (found >= 0).then_some(found)
    }

    // --- a view, written down

    /// The settings, every line's settings and the window, as JSON.
    pub fn state(&self) -> Result<String> {
        // SAFETY: the buffer is the length copied asks for.
        copied(|buffer, capacity, length| unsafe {
            sys::plotcore_state(self.raw(), buffer, capacity, length)
        })
    }

    /// Put back what state() wrote. A setting this build does not know is
    /// passed over.
    pub fn restore(&mut self, state: &str) -> Result<()> {
        let state = c_string(state)?;
        // SAFETY: plain arguments.
        check(unsafe { sys::plotcore_restore(self.raw(), state.as_ptr()) })
    }

    // --- the picture

    pub fn scene(&mut self) -> Result<Scene> {
        // SAFETY: the scene is copied out whole and freed before returning.
        unsafe {
            let raw = sys::plotcore_scene_new(self.raw());
            if raw.is_null() {
                return Err(Error { status: Status::Internal, message: last_error() });
            }
            let scene = copy_scene(raw);
            sys::plotcore_scene_free(raw);
            Ok(scene)
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn ramp(count: usize, from: f64, step: f64) -> Vec<f64> {
        (0..count).map(|i| from + i as f64 * step).collect()
    }

    fn figure_of(lines: &[Vec<f64>]) -> Figure {
        let mut figure = Figure::new();
        for (i, values) in lines.iter().enumerate() {
            figure.add_line(values, &format!("line {i}")).unwrap();
        }
        figure.set_size(800.0, 500.0, 1.0).unwrap();
        figure.set_bool("legend", false).unwrap();
        figure
    }

    #[test]
    fn the_library_is_the_one_this_crate_speaks_to() {
        assert_eq!(abi_version(), sys::PLOTCORE_ABI_VERSION);
        assert_eq!(version(), "0.1.0");
        assert_eq!(palette_names()[0], "okabe-ito");
        assert_eq!(ramp_names()[1], "viridis");
        let okabe = palette("okabe-ito", true).unwrap();
        assert_eq!(okabe.len(), 8);
        assert_eq!(okabe[7], Rgba::new(1.0, 1.0, 1.0, 1.0));
        assert!(palette("plaid", true).is_none());
    }

    #[test]
    fn settings_are_named_and_read_back_as_written() {
        let mut figure = figure_of(&[ramp(10, 0.0, 1.0)]);
        figure.set_bool("xLog", true).unwrap();
        figure.set_number("xLogBase", 2.0).unwrap();
        figure.set_text("grid", "dense").unwrap();
        figure.set_colour("colours.single", Rgba::new(0.1, 0.2, 0.3, 1.0)).unwrap();
        assert!(figure.get_bool("xLog").unwrap());
        assert_eq!(figure.get_number("xLogBase").unwrap(), 2.0);
        assert_eq!(figure.get_text("grid").unwrap(), "dense");
        assert_eq!(figure.get_colour("colours.single").unwrap(), Rgba::new(0.1, 0.2, 0.3, 1.0));
        figure.set_line_text(0, "axisLabel", "volts").unwrap();
        assert_eq!(figure.line_text(0, "axisLabel").unwrap(), "volts");
        assert_eq!(figure.line_name(0).unwrap(), "line 0");
        figure.set_line_colour(0, Some(Rgba::new(1.0, 0.0, 0.0, 1.0))).unwrap();
        assert!(figure.line_colour(0).unwrap().is_some());
        figure.set_line_colour(0, None).unwrap();
        assert!(figure.line_colour(0).unwrap().is_none());
    }

    #[test]
    fn what_cannot_be_done_is_an_error_that_says_why() {
        let mut figure = figure_of(&[ramp(10, 0.0, 1.0)]);
        let refused = figure.set_bool("noSuchThing", true).unwrap_err();
        assert_eq!(refused.status, Status::Argument);
        assert!(refused.message.contains("no such setting"));
        assert_eq!(figure.set_line_bool(7, "visible", true).unwrap_err().status, Status::Range);
        assert_eq!(figure.append_line(0, &[1.0]).unwrap_err().status, Status::State);
        assert_eq!(figure.set_text("title", "a\0b").unwrap_err().status, Status::Argument);
    }

    #[test]
    fn a_reader_is_asked_below_the_base_and_dropped_exactly_once() {
        use std::sync::atomic::{AtomicUsize, Ordering};
        use std::sync::Arc;

        struct Counted(Arc<AtomicUsize>);
        impl Drop for Counted {
            fn drop(&mut self) {
                self.0.fetch_add(1, Ordering::SeqCst);
            }
        }

        let values = ramp(1 << 20, 0.0, 1.0);
        let mut figure = Figure::new();
        figure.set_size(800.0, 500.0, 1.0).unwrap();
        figure.set_budget_bytes(1 << 20).unwrap();
        figure.add_line(&values, "tight").unwrap();
        assert!(figure.line_base_bucket(0) > 1);
        let dropped = Arc::new(AtomicUsize::new(0));
        let asked = Arc::new(AtomicUsize::new(0));
        let guard = Counted(dropped.clone());
        let counter = asked.clone();
        let source = values.clone();
        figure
            .set_line_reader(
                0,
                Some(Box::new(move |first, out: &mut [f64]| {
                    let _ = &guard;
                    counter.fetch_add(1, Ordering::SeqCst);
                    let first = first as usize;
                    out.copy_from_slice(&source[first..first + out.len()]);
                    true
                })),
            )
            .unwrap();
        figure.set_view(4000.0, 4100.0, 0.0, (1 << 20) as f64).unwrap();
        let scene = figure.scene().unwrap();
        assert!(asked.load(Ordering::SeqCst) > 0);
        for p in &scene.lines[0].values[0] {
            assert!((p[0] - p[1]).abs() < 1e-6, "every value is its element");
        }
        assert_eq!(dropped.load(Ordering::SeqCst), 0);
        figure.remove_line(0).unwrap();
        assert_eq!(dropped.load(Ordering::SeqCst), 1);
    }

    #[test]
    fn the_host_s_measure_lays_the_frame_out() {
        let mut figure = figure_of(&[ramp(1000, 0.0, 1.0)]);
        let narrow = figure.scene().unwrap();
        figure
            .set_text_measure(Some((
                Box::new(|text, _role| 30.0 * text.chars().count() as f64),
                Box::new(|_role| 20.0),
            )))
            .unwrap();
        let wide = figure.scene().unwrap();
        assert!(wide.area.x > narrow.area.x);
        figure.set_text_measure(None).unwrap();
        assert_eq!(figure.scene().unwrap().area, narrow.area);
    }

    #[test]
    fn a_view_written_down_reads_back() {
        let mut figure = figure_of(&[ramp(20_000, 0.0, 1.0), ramp(20_000, 1e6, 1.0)]);
        figure.set_line_bool(1, "ownAxis", true).unwrap();
        figure.wheel(400.0, 250.0, 360.0, Modifiers::NONE).unwrap();
        let state = figure.state().unwrap();
        let view = figure.view();
        figure.reset_view().unwrap();
        figure.restore(&state).unwrap();
        assert_eq!(figure.view(), view);
        assert!(figure.line_bool(1, "ownAxis").unwrap());
        assert_eq!(figure.restore("{]").unwrap_err().status, Status::Argument);
    }

    #[test]
    fn a_scaled_line_keeps_its_sample_under_the_pointer() {
        let sine: Vec<f64> = (0..400).map(|i| 1000.0 * (i as f64 / 30.0).sin() + 5000.0).collect();
        let mut figure = figure_of(&[ramp(400, 0.0, 1.0), sine]);
        let first = figure.scene().unwrap();
        let [x, y] = first.lines[1].runs[0][200];
        figure.wheel(x, y, 240.0, Modifiers::ALT | Modifiers::CONTROL).unwrap();
        let scaled = figure.scene().unwrap();
        assert!(figure.line_number(1, "scale").unwrap() > 1.0);
        assert!((scaled.lines[1].runs[0][200][1] - y).abs() < 1e-6);
        figure.press(x, y, Button::Left, Modifiers::CONTROL).unwrap();
        figure.release(x, y, Button::Left, Modifiers::CONTROL).unwrap();
        assert_eq!(figure.line_number(1, "scale").unwrap(), 1.0);
    }

    #[test]
    fn a_figure_can_move_to_another_thread() {
        let mut figure = figure_of(&[ramp(100, 0.0, 1.0)]);
        let scene = std::thread::spawn(move || figure.scene().unwrap()).join().unwrap();
        assert_eq!(scene.lines.len(), 1);
    }
}
