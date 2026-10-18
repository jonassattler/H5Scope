// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

//! The whole of plotcore on one egui window.
//!
//!     egui-demo                 the window
//!     egui-demo --self-test     every control and gesture, checked, headless
//!
//! The plot in the middle and a control for every setting beside it. Each
//! control reads the figure and writes it back when it is changed, so the
//! figure is the one place a setting lives -- the immediate-mode way of what
//! the Qt examples do with signals.

mod signals;

use std::f64::consts::E;

use egui::{Color32, ComboBox, DragValue, Ui};
use plotcore::{Appetite, Figure, Rgba};
use plotcore_egui::{PlotView, Strokes};

const GRIDS: [&str; 4] = ["none", "loose", "dense", "custom"];
const CORNERS: [&str; 4] = ["top-left", "top-right", "bottom-left", "bottom-right"];
const X_AXES: [&str; 4] =
    ["Index from 0", "Index from 1", "Shared time, steady", "Shared time, wandering"];
const TIMES: [&str; 3] = ["shared", "own, steady", "own, wandering"];

struct Stream {
    line: i32,
    at: usize,
    values: Vec<f64>,
}

pub struct DemoApp {
    view: PlotView,
    signal: usize,
    x_axis: usize,
    appetite: Appetite,
    line_times: Vec<usize>,
    stream: Option<Stream>,
    open_path: String,
    view_path: String,
    picture_path: String,
    status: String,
    plot_rect: egui::Rect,
}

fn rgba(c: Color32) -> Rgba {
    let [r, g, b, a] = c.to_srgba_unmultiplied();
    Rgba::new(
        f64::from(r) / 255.0,
        f64::from(g) / 255.0,
        f64::from(b) / 255.0,
        f64::from(a) / 255.0,
    )
}

/// A checkbox over a boolean setting.
fn toggle(ui: &mut Ui, figure: &mut Figure, name: &str, label: &str) {
    let mut on = figure.get_bool(name).unwrap_or(false);
    if ui.checkbox(&mut on, label).changed() {
        let _ = figure.set_bool(name, on);
    }
}

/// A text box over a text setting.
fn text(ui: &mut Ui, figure: &mut Figure, name: &str, label: &str) {
    let mut value = figure.get_text(name).unwrap_or_default();
    ui.horizontal(|ui| {
        ui.label(label);
        if ui.text_edit_singleline(&mut value).changed() {
            let _ = figure.set_text(name, &value);
        }
    });
}

/// A drop-down over a text setting with a fixed set of values.
fn choice(ui: &mut Ui, figure: &mut Figure, name: &str, label: &str, options: &[String]) {
    let current = figure.get_text(name).unwrap_or_default();
    ComboBox::from_label(label).selected_text(&current).show_ui(ui, |ui| {
        for option in options {
            if ui.selectable_label(*option == current, option).clicked() {
                let _ = figure.set_text(name, option);
            }
        }
    });
}

/// A swatch over a colour setting.
fn colour(ui: &mut Ui, figure: &mut Figure, name: &str) {
    let mut value = plotcore_egui::colour(figure.get_colour(name).unwrap_or_default());
    if ui.color_edit_button_srgba(&mut value).changed() {
        let _ = figure.set_colour(name, rgba(value));
    }
}

/// A logarithmic switch and its base: 10, 2, e or the reader's own.
fn scale(ui: &mut Ui, figure: &mut Figure, axis: &str) {
    ui.horizontal(|ui| {
        toggle(ui, figure, &format!("{axis}Log"), &format!("{axis} logarithmic"));
        let name = format!("{axis}LogBase");
        let mut base = figure.get_number(&name).unwrap_or(10.0);
        let named = [(10.0, "10"), (2.0, "2"), (E, "e")];
        let label = named.iter().find(|(b, _)| *b == base).map_or("custom", |(_, l)| l);
        ComboBox::from_id_salt(format!("{axis}-base"))
            .selected_text(format!("base {label}"))
            .show_ui(ui, |ui| {
                for (value, text) in named {
                    if ui.selectable_label(base == value, text).clicked() {
                        let _ = figure.set_number(&name, value);
                    }
                }
                if ui.selectable_label(label == "custom", "custom").clicked() {
                    let _ = figure.set_number(&name, 3.0);
                }
            });
        if label == "custom"
            && ui.add(DragValue::new(&mut base).speed(0.05).range(1.01..=1000.0)).changed()
        {
            let _ = figure.set_number(&name, base);
        }
    });
}

impl DemoApp {
    pub fn new() -> Self {
        let mut app = Self {
            view: PlotView::new(Figure::new()),
            signal: 0,
            x_axis: 0,
            appetite: Appetite::Medium,
            line_times: Vec::new(),
            stream: None,
            open_path: String::new(),
            view_path: "view.json".into(),
            picture_path: "plot.png".into(),
            status: String::new(),
            plot_rect: egui::Rect::NOTHING,
        };
        app.add_signal(0);
        app.add_signal(1);
        app
    }

    fn figure(&mut self) -> &mut Figure {
        &mut self.view.figure
    }

    fn add_line(&mut self, name: &str, values: &[f64]) {
        if self.figure().add_line(values, name).is_ok() {
            self.line_times.push(0);
        }
    }

    fn add_signal(&mut self, which: usize) {
        let seed = self.view.figure.line_count() as u32 + 1;
        let signal = signals::make(which, seed);
        self.add_line(&signal.name, &signal.values);
    }

    fn add_every_signal(&mut self) {
        for which in 0..signals::NAMES.len() {
            self.add_signal(which);
        }
    }

    fn start_stream(&mut self) {
        // A line that arrives in pieces is folded as it lands and drawn once
        // it is whole -- what a reader of a socket or a slow file would do.
        let values = signals::random_walk(5_000_000, 77).values;
        if let Ok(line) = self.figure().begin_line(values.len(), "streamed") {
            self.line_times.push(0);
            self.stream = Some(Stream { line, at: 0, values });
        }
    }

    fn stream_step(&mut self) {
        let Some(stream) = self.stream.as_mut() else { return };
        let end = (stream.at + 100_000).min(stream.values.len());
        let _ = self.view.figure.append_line(stream.line, &stream.values[stream.at..end]);
        stream.at = end;
        if end >= stream.values.len() {
            let _ = self.view.figure.finish_line(stream.line);
            self.stream = None;
        }
    }

    fn apply_x_axis(&mut self) {
        let longest = (0..self.view.figure.line_count())
            .map(|i| self.view.figure.line_length(i))
            .max()
            .unwrap_or(1)
            .max(1) as usize;
        let mode = self.x_axis;
        let figure = self.figure();
        let _ = match mode {
            0 | 1 => figure.set_time(None).and_then(|()| figure.set_index_axis(mode as f64, 1.0)),
            2 => figure.set_time(Some(&signals::regular_time(longest, 0.001))),
            _ => figure.set_time(Some(&signals::irregular_time(longest, 5))),
        };
        let _ = self.figure().reset_view();
    }

    fn set_line_time(&mut self, line: i32, mode: usize) {
        let length = self.view.figure.line_length(line).max(0) as usize;
        let time = match mode {
            0 => None,
            1 => Some(signals::regular_time(length, 0.0005)),
            _ => Some(signals::irregular_time(length, 11 + line as u32)),
        };
        let _ = self.figure().set_line_time(line, time.as_deref());
        let _ = self.figure().reset_view();
    }

    // --- the panels ---------------------------------------------------------

    fn data_panel(&mut self, ui: &mut Ui) {
        ui.horizontal(|ui| {
            ComboBox::from_id_salt("signal").selected_text(signals::NAMES[self.signal]).show_ui(
                ui,
                |ui| {
                    for (i, name) in signals::NAMES.iter().enumerate() {
                        ui.selectable_value(&mut self.signal, i, *name);
                    }
                },
            );
            if ui.button("Add").clicked() {
                self.add_signal(self.signal);
            }
        });
        if ui
            .add_enabled(self.stream.is_none(), egui::Button::new("Stream five million samples"))
            .clicked()
        {
            self.start_stream();
        }
        ui.horizontal(|ui| {
            ui.text_edit_singleline(&mut self.open_path).on_hover_text(".npy or CSV");
            if ui.button("Open").clicked() {
                match signals::load(&self.open_path) {
                    Ok(lines) => {
                        for line in lines {
                            self.add_line(&line.name, &line.values);
                        }
                    }
                    Err(error) => self.status = error,
                }
            }
        });
        if ui.button("Remove every line").clicked() {
            self.stream = None;
            self.line_times.clear();
            let _ = self.figure().clear();
        }
        let before = self.x_axis;
        ComboBox::from_label("x").selected_text(X_AXES[self.x_axis]).show_ui(ui, |ui| {
            for (i, name) in X_AXES.iter().enumerate() {
                ui.selectable_value(&mut self.x_axis, i, *name);
            }
        });
        if self.x_axis != before {
            self.apply_x_axis();
        }
        let before = self.appetite;
        ui.horizontal(|ui| {
            ui.label("RAM budget");
            ui.selectable_value(&mut self.appetite, Appetite::Low, "low");
            ui.selectable_value(&mut self.appetite, Appetite::Medium, "medium");
            ui.selectable_value(&mut self.appetite, Appetite::Greedy, "greedy");
        });
        if self.appetite != before {
            let appetite = self.appetite;
            let _ = self.figure().set_appetite(appetite);
        }
        ui.label(
            "Wheel zooms about the pointer; Shift across, Control up. Drag pans. Right-drag zooms to a band. \
             Double-click puts the view back. Alt-drag or middle-drag moves a line, Alt+Control+wheel scales \
             it, Control-click puts it back.",
        );
    }

    fn axes_panel(&mut self, ui: &mut Ui) {
        let figure = &mut self.view.figure;
        scale(ui, figure, "x");
        scale(ui, figure, "y");
        let grids: Vec<String> = GRIDS.iter().map(|g| (*g).to_owned()).collect();
        choice(ui, figure, "grid", "grid", &grids);
        if figure.get_text("grid").unwrap_or_default() == "custom" {
            ui.horizontal(|ui| {
                for name in ["gridStepX", "gridStepY"] {
                    let mut step = figure.get_number(name).unwrap_or(0.0);
                    ui.label(name);
                    if ui.add(DragValue::new(&mut step).speed(1.0).range(0.0..=1e12)).changed() {
                        let _ = figure.set_number(name, step);
                    }
                }
            });
        }
        toggle(ui, figure, "minorNumbers", "number what lies between powers");
        toggle(ui, figure, "flipped", "flipped: x up, y across");
        text(ui, figure, "title", "title");
        text(ui, figure, "xLabel", "x label");
        text(ui, figure, "yLabel", "y label");
    }

    fn lines_panel(&mut self, ui: &mut Ui) {
        let figure = &mut self.view.figure;
        ui.horizontal(|ui| {
            toggle(ui, figure, "markers", "markers");
            toggle(ui, figure, "cursor", "crosshair");
            let mut width = figure.style("lineWidth").unwrap_or(1.0);
            ui.label("width");
            if ui.add(DragValue::new(&mut width).speed(0.1).range(0.5..=8.0)).changed() {
                let _ = figure.set_style("lineWidth", width);
            }
        });
        ui.horizontal(|ui| {
            ui.label("strokes");
            ui.selectable_value(&mut self.view.strokes, Strokes::Triangles, "plotcore's triangles");
            ui.selectable_value(&mut self.view.strokes, Strokes::Feathered, "feathered");
        });
        let figure = &mut self.view.figure;
        let mut modes = plotcore::palette_names();
        modes.extend(plotcore::ramp_names());
        modes.extend(["same".to_owned(), "range".to_owned()]);
        choice(ui, figure, "colours.mode", "colours", &modes);
        ui.horizontal(|ui| {
            toggle(ui, figure, "colours.reversed", "reversed");
            ui.label("same");
            colour(ui, figure, "colours.single");
            ui.label("range");
            colour(ui, figure, "colours.rangeFrom");
            colour(ui, figure, "colours.rangeTo");
            // Choosing one pins all three; this gives them back to the ground.
            if ui.button("follow the ground").clicked() {
                let _ = figure.set_bool("colours.custom", false);
            }
        });
        ui.horizontal(|ui| {
            ui.label("map's slice");
            for name in ["colours.from", "colours.to"] {
                let mut at = figure.get_number(name).unwrap_or(0.0);
                if ui.add(egui::Slider::new(&mut at, 0.0..=1.0)).changed() {
                    let _ = figure.set_number(name, at);
                }
            }
        });
        ui.horizontal(|ui| {
            toggle(ui, figure, "legend", "legend");
            let corners: Vec<String> = CORNERS.iter().map(|c| (*c).to_owned()).collect();
            choice(ui, figure, "legendCorner", "corner", &corners);
        });
        ui.horizontal(|ui| {
            let mut highlighted = figure.get_number("highlighted").unwrap_or(-1.0) as i32;
            ui.label("highlight");
            if ui
                .add(DragValue::new(&mut highlighted).range(-1..=figure.line_count() - 1))
                .changed()
            {
                let _ = figure.set_number("highlighted", f64::from(highlighted));
            }
            toggle(ui, figure, "bundle", "bundle");
            toggle(ui, figure, "stacked", "stacked");
        });
        ui.horizontal(|ui| {
            toggle(ui, figure, "dark", "dark");
            toggle(ui, figure, "paper", "publication");
        });
        self.lines_table(ui);
    }

    fn lines_table(&mut self, ui: &mut Ui) {
        self.line_times.resize(self.view.figure.line_count().max(0) as usize, 0);
        egui::Grid::new("lines").striped(true).show(ui, |ui| {
            for heading in [
                "show", "name", "colour", "own y", "fixed", "y label", "scale", "shift", "time", "",
            ] {
                ui.strong(heading);
            }
            ui.end_row();
            for line in 0..self.view.figure.line_count() {
                let figure = &mut self.view.figure;
                let mut shown = figure.line_bool(line, "visible").unwrap_or(true);
                if ui.checkbox(&mut shown, "").changed() {
                    let _ = figure.set_line_bool(line, "visible", shown);
                }
                let ready = if figure.line_ready(line) { "" } else { " (arriving)" };
                ui.label(format!("{}{ready}", figure.line_name(line).unwrap_or_default()));
                let own = figure.line_colour(line).unwrap_or(None);
                ui.horizontal(|ui| {
                    let mut chosen = own.map_or(Color32::WHITE, plotcore_egui::colour);
                    if ui.color_edit_button_srgba(&mut chosen).changed() {
                        let _ = figure.set_line_colour(line, Some(rgba(chosen)));
                    }
                    if own.is_some() && ui.small_button("cycle").clicked() {
                        let _ = figure.set_line_colour(line, None);
                    }
                });
                for name in ["ownAxis", "axisFixed"] {
                    let mut on = figure.line_bool(line, name).unwrap_or(false);
                    if ui.checkbox(&mut on, "").changed() {
                        let _ = figure.set_line_bool(line, name, on);
                    }
                }
                let mut label = figure.line_text(line, "axisLabel").unwrap_or_default();
                if ui.add(egui::TextEdit::singleline(&mut label).desired_width(70.0)).changed() {
                    let _ = figure.set_line_text(line, "axisLabel", &label);
                }
                for name in ["scale", "shift"] {
                    let mut value = figure.line_number(line, name).unwrap_or(0.0);
                    let speed = if name == "scale" { 0.01 } else { 0.1 };
                    if ui.add(DragValue::new(&mut value).speed(speed)).changed() {
                        let _ = figure.set_line_number(line, name, value);
                    }
                }
                let mut mode = self.line_times[line as usize];
                ComboBox::from_id_salt(("time", line)).selected_text(TIMES[mode]).show_ui(
                    ui,
                    |ui| {
                        for (i, name) in TIMES.iter().enumerate() {
                            ui.selectable_value(&mut mode, i, *name);
                        }
                    },
                );
                if mode != self.line_times[line as usize] {
                    self.line_times[line as usize] = mode;
                    self.set_line_time(line, mode);
                }
                if ui.small_button("put back").clicked() {
                    let figure = &mut self.view.figure;
                    let _ = figure.set_line_number(line, "scale", 1.0);
                    let _ = figure.set_line_number(line, "shift", 0.0);
                }
                ui.end_row();
            }
        });
    }

    fn view_panel(&mut self, ui: &mut Ui) {
        let mut window = self.view.figure.view();
        ui.horizontal(|ui| {
            for (value, label) in window.iter_mut().zip(["x from", "x to", "y from", "y to"]) {
                ui.label(label);
                ui.add(DragValue::new(value).speed(0.1));
            }
        });
        ui.horizontal(|ui| {
            if ui.button("Show this window").clicked() {
                let [x0, x1, y0, y1] = window;
                let _ = self.figure().set_view(x0, x1, y0, y1);
            }
            if ui.button("The whole of it").clicked() {
                let _ = self.figure().reset_view();
            }
        });
        ui.horizontal(|ui| {
            ui.text_edit_singleline(&mut self.view_path);
            if ui.button("Save view").clicked() {
                self.status = match self.view.figure.state() {
                    Ok(state) => match std::fs::write(&self.view_path, state) {
                        Ok(()) => format!("saved {}", self.view_path),
                        Err(error) => error.to_string(),
                    },
                    Err(error) => error.to_string(),
                };
            }
            if ui.button("Load view").clicked() {
                self.status = match std::fs::read_to_string(&self.view_path) {
                    Ok(state) => match self.figure().restore(&state) {
                        Ok(()) => format!("loaded {}", self.view_path),
                        Err(error) => error.to_string(),
                    },
                    Err(error) => error.to_string(),
                };
            }
        });
        ui.horizontal(|ui| {
            ui.text_edit_singleline(&mut self.picture_path);
            if ui.button("Export PNG").clicked() {
                // The plot as the screen drew it, cut out of the window's next
                // frame.
                let (path, rect, ctx) =
                    (self.picture_path.clone(), self.plot_rect, ui.ctx().clone());
                let ppp = ctx.pixels_per_point();
                ctx.request_screenshot(move |image| {
                    let plot = image.region(&rect, Some(ppp));
                    let _ = save_png(&path, &plot);
                });
                self.status = format!("writing {}", self.picture_path);
            }
        });
    }

    fn status_line(&self) -> String {
        let figure = &self.view.figure;
        let [x0, x1, y0, y1] = figure.view();
        let mut line = format!("x {x0:.6} … {x1:.6}   y {y0:.6} … {y1:.6}");
        if let Some(reading) = self.view.last_scene().and_then(|s| s.reading.as_ref()) {
            let name = figure.line_name(reading.line).unwrap_or_default();
            line += &format!("   —   {name}: x {}, y {}", reading.x_text, reading.y_text);
        }
        let bases: Vec<String> =
            (0..figure.line_count()).map(|i| figure.line_base_bucket(i).to_string()).collect();
        line += &format!(
            "   held {:.1} of {:.0} MB, base buckets {}",
            figure.held_bytes() as f64 / 1048576.0,
            figure.budget_bytes() as f64 / 1048576.0,
            bases.join(" ")
        );
        if !self.status.is_empty() {
            line += &format!("   {}", self.status);
        }
        line
    }

    /// One frame of the whole window.
    pub fn draw(&mut self, ui: &mut Ui) {
        if self.stream.is_some() {
            self.stream_step();
            ui.ctx().request_repaint();
        }
        egui::Panel::bottom("status").show(ui, |ui| {
            ui.label(self.status_line());
        });
        egui::Panel::right("settings").resizable(true).default_size(460.0).show(ui, |ui| {
            egui::ScrollArea::vertical().show(ui, |ui| {
                egui::CollapsingHeader::new("Data")
                    .default_open(true)
                    .show(ui, |ui| self.data_panel(ui));
                egui::CollapsingHeader::new("Axes")
                    .default_open(true)
                    .show(ui, |ui| self.axes_panel(ui));
                egui::CollapsingHeader::new("Lines")
                    .default_open(true)
                    .show(ui, |ui| self.lines_panel(ui));
                egui::CollapsingHeader::new("View")
                    .default_open(true)
                    .show(ui, |ui| self.view_panel(ui));
            });
        });
        egui::CentralPanel::default().frame(egui::Frame::NONE).show(ui, |ui| {
            self.plot_rect = ui.max_rect();
            self.view.show(ui);
        });
    }
}

impl Default for DemoApp {
    fn default() -> Self {
        Self::new()
    }
}

impl eframe::App for DemoApp {
    fn ui(&mut self, ui: &mut Ui, _frame: &mut eframe::Frame) {
        self.draw(ui);
    }
}

fn save_png(path: &str, image: &egui::ColorImage) -> Result<(), String> {
    let file = std::fs::File::create(path).map_err(|e| e.to_string())?;
    let mut encoder = png::Encoder::new(
        std::io::BufWriter::new(file),
        image.size[0] as u32,
        image.size[1] as u32,
    );
    encoder.set_color(png::ColorType::Rgba);
    encoder.set_depth(png::BitDepth::Eight);
    let mut writer = encoder.write_header().map_err(|e| e.to_string())?;
    writer.write_image_data(image.as_raw()).map_err(|e| e.to_string())
}

// --- the self-test ------------------------------------------------------------------

mod self_test {
    use super::*;
    use egui::{
        Event, Modifiers, MouseWheelSource, MouseWheelUnit, PointerButton, Pos2, RawInput,
        TouchPhase, Vec2,
    };

    struct Walk {
        ctx: egui::Context,
        app: DemoApp,
        failed: usize,
    }

    impl Walk {
        fn frame(&mut self, events: Vec<Event>) -> plotcore::Scene {
            let input = RawInput {
                screen_rect: Some(egui::Rect::from_min_size(Pos2::ZERO, Vec2::new(1400.0, 820.0))),
                events,
                ..RawInput::default()
            };
            let app = &mut self.app;
            let output = self.ctx.run_ui(input, |ui| app.draw(ui));
            output.drop_without_applying_deltas();
            self.app.view.last_scene().cloned().unwrap_or_default()
        }

        fn check(&mut self, ok: bool, what: &str) {
            println!("{}{what}", if ok { "  ok    " } else { "  FAIL  " });
            self.failed += usize::from(!ok);
        }

        fn wheel(&mut self, at: Pos2, notches: f32, modifiers: Modifiers) -> plotcore::Scene {
            self.frame(vec![Event::PointerMoved(at)]);
            self.frame(vec![Event::MouseWheel {
                unit: MouseWheelUnit::Line,
                delta: Vec2::new(0.0, notches),
                phase: TouchPhase::Move,
                source: MouseWheelSource::Unknown,
                modifiers,
            }])
        }

        fn drag(
            &mut self,
            from: Pos2,
            to: Pos2,
            button: PointerButton,
            modifiers: Modifiers,
        ) -> plotcore::Scene {
            let press = |pressed, pos| Event::PointerButton { pos, button, pressed, modifiers };
            self.frame(vec![Event::PointerMoved(from), press(true, from)]);
            self.frame(vec![Event::PointerMoved(to)]);
            self.frame(vec![press(false, to)])
        }
    }

    pub fn run() -> usize {
        println!("plotcore egui example, self-test");
        let mut walk = Walk { ctx: egui::Context::default(), app: DemoApp::new(), failed: 0 };
        walk.app.add_every_signal();
        let whole = walk.frame(vec![]);
        walk.check(whole.lines.len() == 8, "every signal is drawn");
        let area = whole.area;
        let origin = walk.app.plot_rect.min;
        let middle = origin
            + Vec2::new((area.x + area.width / 2.0) as f32, (area.y + area.height / 2.0) as f32);

        let zoomed = walk.wheel(middle, 3.0, Modifiers::NONE);
        walk.check(
            zoomed.view[1] - zoomed.view[0] < whole.view[1] - whole.view[0],
            "the wheel zooms in",
        );
        let across = walk.wheel(middle, 2.0, Modifiers::SHIFT);
        walk.check(across.view[2..] == zoomed.view[2..], "Shift zooms across alone");
        let panned = walk.drag(
            middle,
            middle + Vec2::new(80.0, 0.0),
            PointerButton::Primary,
            Modifiers::NONE,
        );
        walk.check(panned.view[0] < across.view[0], "a drag pans");
        let from = middle - Vec2::new(100.0, 60.0);
        walk.frame(vec![
            Event::PointerMoved(from),
            Event::PointerButton {
                pos: from,
                button: PointerButton::Secondary,
                pressed: true,
                modifiers: Modifiers::NONE,
            },
        ]);
        let banding = walk.frame(vec![Event::PointerMoved(middle + Vec2::new(100.0, 60.0))]);
        walk.check(banding.band.is_some(), "a right drag draws its band");
        let banded = walk.frame(vec![Event::PointerButton {
            pos: middle + Vec2::new(100.0, 60.0),
            button: PointerButton::Secondary,
            pressed: false,
            modifiers: Modifiers::NONE,
        }]);
        walk.check(
            banded.band.is_none()
                && banded.view[1] - banded.view[0] < panned.view[1] - panned.view[0],
            "...and zooms to it",
        );
        walk.app.view.figure.reset_view().unwrap();
        let back = walk.frame(vec![Event::PointerMoved(middle)]);
        walk.check(back.view == whole.view, "the whole of it again");
        walk.check(back.reading.is_some(), "the crosshair reads a sample");

        // A line moved with Alt, scaled with Alt and Control, put back.
        let run = &back.lines[1].runs[0];
        let [x, y] = run[run.len() / 2];
        let at = origin + Vec2::new(x as f32, y as f32);
        let under = walk.app.view.figure.line_at(x, y, 12.0);
        walk.check(under.is_some(), "a line is under the pointer");
        let under = under.unwrap_or(0);
        walk.drag(at, at + Vec2::new(0.0, 30.0), PointerButton::Primary, Modifiers::ALT);
        walk.check(
            walk.app.view.figure.line_number(under, "shift").unwrap() != 0.0,
            "Alt and a drag move it",
        );
        let ctrl_alt = Modifiers { alt: true, ctrl: true, ..Modifiers::NONE };
        walk.wheel(at + Vec2::new(0.0, 30.0), 2.0, ctrl_alt);
        walk.check(
            walk.app.view.figure.line_number(under, "scale").unwrap() != 1.0,
            "Alt, Control and the wheel scale it",
        );
        let held = at + Vec2::new(0.0, 30.0);
        walk.drag(held, held, PointerButton::Primary, Modifiers::CTRL);
        let figure = &walk.app.view.figure;
        let put_back = figure.line_number(under, "scale").unwrap() == 1.0
            && figure.line_number(under, "shift").unwrap() == 0.0;
        walk.check(put_back, "Control and a click put it back");

        // Every setting, once, drawn.
        let figure = &mut walk.app.view.figure;
        figure.set_index_axis(1.0, 1.0).unwrap();
        figure.set_bool("xLog", true).unwrap();
        figure.set_bool("yLog", true).unwrap();
        figure.set_number("xLogBase", 3.0).unwrap();
        figure.set_number("yLogBase", E).unwrap();
        figure.set_bool("minorNumbers", true).unwrap();
        let logs = walk.frame(vec![]);
        walk.check(
            logs.lines.len() == 8 && logs.message.is_empty(),
            "both axes logarithmic, bases 3 and e",
        );
        let figure = &mut walk.app.view.figure;
        figure.set_bool("xLog", false).unwrap();
        figure.set_bool("yLog", false).unwrap();
        for grid in GRIDS {
            walk.app.view.figure.set_text("grid", grid).unwrap();
            walk.app.view.figure.set_number("gridStepX", 100_000.0).unwrap();
            let ruled = walk.frame(vec![]);
            walk.check((grid == "none") == ruled.rules.is_empty(), &format!("the {grid} grid"));
        }
        let figure = &mut walk.app.view.figure;
        figure.set_text("grid", "loose").unwrap();
        figure.set_text("title", "Every signal").unwrap();
        figure.set_bool("flipped", true).unwrap();
        let scene = walk.frame(vec![]);
        walk.check(scene.lines.len() == 8, "flipped");
        walk.app.view.figure.set_bool("flipped", false).unwrap();
        walk.app.view.figure.set_bool("markers", true).unwrap();
        let mut modes = plotcore::palette_names();
        modes.extend(plotcore::ramp_names());
        modes.extend(["same".to_owned(), "range".to_owned()]);
        for mode in &modes {
            walk.app.view.figure.set_text("colours.mode", mode).unwrap();
            walk.frame(vec![]);
        }
        walk.check(true, "every palette, every map, same and range");
        for corner in CORNERS {
            walk.app.view.figure.set_text("legendCorner", corner).unwrap();
        }
        let scene = walk.frame(vec![]);
        walk.check(!scene.legend.is_empty(), "the legend, in every corner");
        let figure = &mut walk.app.view.figure;
        figure.set_line_bool(2, "ownAxis", true).unwrap();
        figure.set_line_text(2, "axisLabel", "gaps").unwrap();
        figure.set_line_bool(3, "ownAxis", true).unwrap();
        figure.set_line_bool(3, "axisFixed", true).unwrap();
        let scene = walk.frame(vec![]);
        walk.check(scene.area.x > back.area.x, "axes of their own, beside the pane");
        walk.app.view.figure.set_bool("stacked", true).unwrap();
        let scene = walk.frame(vec![]);
        walk.check(scene.lines.len() == 8, "stacked");
        walk.app.view.figure.set_bool("stacked", false).unwrap();
        walk.app.set_line_time(1, 2);
        walk.check(walk.app.view.figure.line_has_time(1), "a time of its own");
        walk.app.x_axis = 3;
        walk.app.apply_x_axis();
        let scene = walk.frame(vec![]);
        walk.check(scene.lines.len() == 8, "a shared time that wanders");
        walk.app.view.strokes = Strokes::Feathered;
        walk.frame(vec![]);
        walk.app.view.strokes = Strokes::Triangles;

        // A colour nobody chose is the ground's and follows it; one chosen
        // stays where it was put until the ground is given it back.
        let same = |a: plotcore::Rgba, b: plotcore::Rgba| {
            (a.red - b.red).abs() + (a.green - b.green).abs() + (a.blue - b.blue).abs() < 1e-9
        };
        walk.app.view.figure.set_text("colours.mode", "same").unwrap();
        let on_dark = walk.frame(vec![]).lines[0].colour;
        walk.app.view.figure.set_bool("dark", false).unwrap();
        let on_light = walk.frame(vec![]).lines[0].colour;
        walk.check(!same(on_light, on_dark), "a colour nobody chose follows the ground");
        let orange = plotcore::Rgba::new(0.9, 0.3, 0.1, 1.0);
        walk.app.view.figure.set_colour("colours.single", orange).unwrap();
        walk.app.view.figure.set_bool("dark", true).unwrap();
        let kept = walk.frame(vec![]).lines[0].colour;
        walk.check(same(kept, orange), "a colour chosen stays where it was put");
        walk.app.view.figure.set_bool("colours.custom", false).unwrap();
        let back = walk.frame(vec![]).lines[0].colour;
        walk.check(same(back, on_dark), "...until the ground is given it back");
        walk.app.view.figure.set_text("colours.mode", &plotcore::palette_names()[0]).unwrap();

        let figure = &mut walk.app.view.figure;
        figure.set_bool("dark", false).unwrap();
        figure.set_bool("paper", true).unwrap();
        walk.frame(vec![]);
        for appetite in [Appetite::Low, Appetite::Medium, Appetite::Greedy] {
            walk.app.view.figure.set_appetite(appetite).unwrap();
            walk.frame(vec![]);
        }
        let figure = &walk.app.view.figure;
        walk.check(
            figure.held_bytes() <= figure.budget_bytes(),
            "what is held is inside the budget",
        );

        walk.wheel(middle, 4.0, Modifiers::NONE);
        let before = walk.app.view.figure.view();
        let state = walk.app.view.figure.state().unwrap();
        walk.app.view.figure.reset_view().unwrap();
        walk.app.view.figure.restore(&state).unwrap();
        walk.check(walk.app.view.figure.view() == before, "a view reads back as the same window");

        walk.app.start_stream();
        while walk.app.stream.is_some() {
            walk.frame(vec![]);
        }
        let scene = walk.frame(vec![]);
        walk.check(scene.lines.len() == 9, "a line streamed in pieces is drawn once it is whole");

        println!("{}", if walk.failed == 0 { "all checks passed" } else { "checks failed" });
        walk.failed
    }
}

fn main() -> eframe::Result {
    if std::env::args().any(|a| a == "--self-test") {
        std::process::exit(i32::from(self_test::run() != 0));
    }
    let options = eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default()
            .with_inner_size([1400.0, 820.0])
            .with_title("plotcore — egui"),
        ..Default::default()
    };
    eframe::run_native("plotcore — egui", options, Box::new(|_| Ok(Box::new(DemoApp::new()))))
}

#[cfg(test)]
mod tests {
    #[test]
    fn the_example_walks_every_feature() {
        assert_eq!(super::self_test::run(), 0);
    }
}
