// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

//! One plot in three languages: plotcore/tests/golden/script.txt run through
//! the Rust binding must write the digest test_capi.c wrote from C. The
//! language and the digest are golden/README.md's.

use std::path::PathBuf;

use plotcore::{Button, Figure, Modifiers, Rgba, Scene};

fn golden() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../tests/golden")
}

/// A generator of README.md, and how many words it took.
fn generate(words: &[&str]) -> (Vec<f64>, usize) {
    let n: usize = words[1].parse().unwrap();
    let number = |i: usize| -> f64 { words[i].parse().unwrap() };
    match words[0] {
        "ramp" => {
            let (from, step) = (number(2), number(3));
            ((0..n).map(|i| from + i as f64 * step).collect(), 4)
        }
        "quadratic" => {
            let scale = number(2);
            ((0..n).map(|i| scale * (i + 1) as f64 * (i + 1) as f64).collect(), 3)
        }
        "square" => {
            let period: usize = words[2].parse().unwrap();
            let (low, high) = (number(3), number(4));
            ((0..n).map(|i| if i % period < period / 2 { low } else { high }).collect(), 5)
        }
        "holes" => {
            let every: usize = words[2].parse().unwrap();
            ((0..n).map(|i| if i % every < 3 { f64::NAN } else { i as f64 }).collect(), 3)
        }
        "walk" => {
            let mut state: u32 = words[2].parse().unwrap();
            let mut at = 0.0f64;
            let values = (0..n)
                .map(|i| {
                    state = state.wrapping_mul(1_103_515_245).wrapping_add(12_345);
                    at += f64::from((state >> 16) & 0x7fff) / 32767.0 - 0.5;
                    if i % 997 == 996 {
                        at + 500.0
                    } else {
                        at
                    }
                })
                .collect();
            (values, 3)
        }
        other => panic!("no generator {other}"),
    }
}

fn modifiers(word: &str) -> Modifiers {
    let mut out = Modifiers::NONE;
    if word.contains("shift") {
        out |= Modifiers::SHIFT;
    }
    if word.contains("control") {
        out |= Modifiers::CONTROL;
    }
    if word.contains("alt") {
        out |= Modifiers::ALT;
    }
    out
}

fn button(word: &str) -> Button {
    match word {
        "middle" => Button::Middle,
        "right" => Button::Right,
        _ => Button::Left,
    }
}

/// A number as Rust writes one: a round trip, which is all the comparison
/// needs.
fn g(value: f64) -> String {
    format!("{value:?}")
}

fn colour(c: &Rgba) -> String {
    format!(" {} {} {} {}", g(c.red), g(c.green), g(c.blue), g(c.alpha))
}

fn digest(scene: &Scene, index: usize, out: &mut Vec<String>) {
    out.push(format!("scene {index}"));
    out.push(format!("size {} {}", g(scene.width), g(scene.height)));
    let a = &scene.area;
    out.push(format!("area {} {} {} {}", g(a.x), g(a.y), g(a.width), g(a.height)));
    let [x0, x1, y0, y1] = scene.view;
    out.push(format!("view {} {} {} {}", g(x0), g(x1), g(y0), g(y1)));
    out.push(format!("ground{}", colour(&scene.ground)));
    let c = &scene.chrome;
    out.push(format!("chrome{}{}{}", colour(&c.ink), colour(&c.axis_rule), colour(&c.cursor_ink)));
    out.push(format!("message |{}", scene.message));
    for line in &scene.lines {
        let (mut sx, mut sy, mut vx, mut vy) = (0.0, 0.0, 0.0, 0.0);
        let mut points = 0;
        for (run, values) in line.runs.iter().zip(&line.values) {
            for (p, v) in run.iter().zip(values) {
                sx += p[0];
                sy += p[1];
                vx += v[0];
                vy += v[1];
            }
            points += run.len();
        }
        out.push(format!(
            "line {}{} {} {} {} {} {} {} {}",
            line.line,
            colour(&line.colour),
            g(line.width),
            line.runs.len(),
            points,
            line.triangles.len(),
            line.markers.len(),
            g(sx),
            g(sy)
        ));
        out.push(format!(
            "window {} {} {} {} {} {} {}",
            g(line.y_low),
            g(line.y_high),
            line.y_log as i32,
            line.own as i32,
            line.numbered as i32,
            g(vx),
            g(vy)
        ));
    }
    for (name, segments) in [("rules", &scene.rules), ("strokes", &scene.strokes)] {
        let (mut sx, mut sy) = (0.0, 0.0);
        for s in segments {
            sx += s.x0;
            sx += s.x1;
            sy += s.y0;
            sy += s.y1;
        }
        out.push(format!("{name} {} {} {}", segments.len(), g(sx), g(sy)));
    }
    for t in &scene.texts {
        out.push(format!(
            "text {} {} {} {} {} {}{} |{}",
            role(t.role),
            align(t.across),
            align(t.down),
            t.vertical as i32,
            g(t.x),
            g(t.y),
            colour(&t.colour),
            t.text
        ));
    }
    for e in &scene.legend {
        let [a, b, c, d] = e.swatch;
        let [tx, ty] = e.text_at;
        out.push(format!(
            "legend {}{} {} {} {} {} {} {} |{}",
            e.line,
            colour(&e.colour),
            g(a),
            g(b),
            g(c),
            g(d),
            g(tx),
            g(ty),
            e.name
        ));
    }
    if let Some(r) = &scene.legend_rect {
        out.push(format!("legend_rect {} {} {} {}", g(r.x), g(r.y), g(r.width), g(r.height)));
    }
    match &scene.reading {
        Some(r) => out.push(format!(
            "reading 1 {} {} {} {} {} |{}|{}",
            r.line,
            g(r.x),
            g(r.y),
            g(r.px),
            g(r.py),
            r.x_text,
            r.y_text
        )),
        None => out.push("reading 0 -1 0 0 0 0 ||".to_owned()),
    }
    match &scene.band {
        Some(b) => out.push(format!("band 1 {} {} {} {}", g(b.x), g(b.y), g(b.width), g(b.height))),
        None => out.push("band 0 0 0 0 0".to_owned()),
    }
}

fn role(role: plotcore::TextRole) -> i32 {
    plotcore::TextRole::ALL.iter().position(|r| *r == role).unwrap() as i32
}

fn align(align: plotcore::Align) -> i32 {
    match align {
        plotcore::Align::Start => 0,
        plotcore::Align::Middle => 1,
        plotcore::Align::End => 2,
    }
}

/// The text after the first `skip` words.
fn rest(line: &str, skip: usize) -> &str {
    line.splitn(skip + 1, ' ').nth(skip).unwrap_or("")
}

fn run(script: &str) -> Vec<String> {
    let mut figure = Figure::new();
    let mut out = Vec::new();
    let mut last: Option<Scene> = None;
    let mut kept: Option<String> = None;
    let mut pointer = [0.0f64; 2];
    let mut scenes = 0;
    let number = |w: &str| -> f64 { w.parse().unwrap() };
    let index = |w: &str| -> i32 { w.parse().unwrap() };
    for raw in script.lines() {
        let line = raw.trim_end_matches('\r');
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        let w: Vec<&str> = line.split(' ').collect();
        let sample = |last: &Option<Scene>, line: i32, at: usize| -> [f64; 2] {
            last.as_ref()
                .and_then(|s| s.lines.iter().find(|d| d.line == line))
                .and_then(|d| d.runs.first())
                .and_then(|run| run.get(at))
                .copied()
                .unwrap_or_else(|| panic!("no sample {at} of line {line}"))
        };
        let result = match w[0] {
            "size" => figure.set_size(number(w[1]), number(w[2]), number(w[3])),
            "line" => {
                let (values, used) = generate(&w[1..]);
                figure.add_line(&values, rest(line, 1 + used)).map(|_| ())
            }
            "remove" => figure.remove_line(index(w[1])),
            "clear" => figure.clear(),
            "time" if w[1] == "none" => figure.set_time(None),
            "time" => figure.set_time(Some(&generate(&w[1..]).0)),
            "line_time" => figure.set_line_time(index(w[1]), Some(&generate(&w[2..]).0)),
            "budget" => figure.set_budget_bytes(w[1].parse().unwrap()),
            "set_bool" => figure.set_bool(w[1], w[2] != "0"),
            "set_number" => figure.set_number(w[1], number(w[2])),
            "set_text" => figure.set_text(w[1], rest(line, 2)),
            "set_colour" => figure.set_colour(
                w[1],
                Rgba::new(number(w[2]), number(w[3]), number(w[4]), number(w[5])),
            ),
            "line_bool" => figure.set_line_bool(index(w[1]), w[2], w[3] != "0"),
            "line_number" => figure.set_line_number(index(w[1]), w[2], number(w[3])),
            "line_text" => figure.set_line_text(index(w[1]), w[2], rest(line, 3)),
            "line_colour" => figure.set_line_colour(
                index(w[1]),
                Some(Rgba::new(number(w[2]), number(w[3]), number(w[4]), number(w[5]))),
            ),
            "wheel" => {
                pointer = [number(w[1]), number(w[2])];
                figure.wheel(pointer[0], pointer[1], number(w[3]), modifiers(w[4]))
            }
            "press" => {
                pointer = [number(w[1]), number(w[2])];
                figure.press(pointer[0], pointer[1], button(w[3]), modifiers(w[4]))
            }
            "move" => {
                pointer = [number(w[1]), number(w[2])];
                figure.move_to(pointer[0], pointer[1], modifiers(w[3]))
            }
            "release" => {
                pointer = [number(w[1]), number(w[2])];
                figure.release(pointer[0], pointer[1], button(w[3]), modifiers(w[4]))
            }
            "double" => figure.double_click(number(w[1]), number(w[2]), button(w[3])),
            "hover" => {
                pointer = [number(w[1]), number(w[2])];
                figure.hover(pointer[0], pointer[1])
            }
            "leave" => figure.leave(),
            "reset" => figure.reset_view(),
            "view" => figure.set_view(number(w[1]), number(w[2]), number(w[3]), number(w[4])),
            "press_line" => {
                pointer = sample(&last, index(w[1]), w[2].parse().unwrap());
                figure.press(pointer[0], pointer[1], Button::Left, modifiers(w[3]))
            }
            "wheel_line" => {
                pointer = sample(&last, index(w[1]), w[2].parse().unwrap());
                figure.wheel(pointer[0], pointer[1], number(w[3]), modifiers(w[4]))
            }
            "click_line" => {
                pointer = sample(&last, index(w[1]), w[2].parse().unwrap());
                figure.press(pointer[0], pointer[1], Button::Left, modifiers(w[3])).and_then(|()| {
                    figure.release(pointer[0], pointer[1], Button::Left, modifiers(w[3]))
                })
            }
            "move_by" => {
                pointer = [pointer[0] + number(w[1]), pointer[1] + number(w[2])];
                figure.move_to(pointer[0], pointer[1], modifiers(w[3]))
            }
            "release_here" => figure.release(pointer[0], pointer[1], button(w[1]), modifiers(w[2])),
            "state" => {
                let state = figure.state().unwrap();
                out.push(format!("state |{state}"));
                kept = Some(state);
                Ok(())
            }
            "restore" => figure.restore(kept.as_deref().unwrap_or("")),
            "scene" => {
                let scene = figure.scene().unwrap();
                digest(&scene, scenes, &mut out);
                scenes += 1;
                last = Some(scene);
                Ok(())
            }
            other => panic!("no command {other}"),
        };
        result.unwrap_or_else(|error| panic!("{line}: {error}"));
    }
    out
}

/// README.md's comparison: numbers within a relative 1e-9, everything after
/// the first '|' character for character.
fn same_line(expected: &str, actual: &str) -> bool {
    let (head_e, tail_e) = expected.split_once('|').map_or((expected, None), |(h, t)| (h, Some(t)));
    let (head_a, tail_a) = actual.split_once('|').map_or((actual, None), |(h, t)| (h, Some(t)));
    if tail_e != tail_a {
        return false;
    }
    let words_e: Vec<&str> = head_e.split_whitespace().collect();
    let words_a: Vec<&str> = head_a.split_whitespace().collect();
    words_e.len() == words_a.len()
        && words_e.iter().zip(&words_a).all(|(e, a)| match (e.parse::<f64>(), a.parse::<f64>()) {
            (Ok(x), Ok(y)) if x.is_nan() || y.is_nan() => x.is_nan() && y.is_nan(),
            (Ok(x), Ok(y)) => (x - y).abs() <= 1e-9 * 1f64.max(x.abs()).max(y.abs()),
            _ => e == a,
        })
}

#[test]
fn the_golden_script_draws_the_c_digest() {
    let script = std::fs::read_to_string(golden().join("script.txt")).unwrap();
    let expected = std::fs::read_to_string(golden().join("expected.txt")).unwrap();
    let actual = run(&script);
    let expected: Vec<&str> = expected.lines().collect();
    for (number, (e, a)) in expected.iter().zip(&actual).enumerate() {
        assert!(same_line(e, a), "golden line {}\n  expected: {e}\n  actual:   {a}", number + 1);
    }
    assert_eq!(actual.len(), expected.len(), "a digest of a different length");
}

#[test]
fn the_comparison_catches_a_changed_number() {
    assert!(same_line("line 0 1 2 |a b", "line 0 1.0 2.0000000000001 |a b"));
    assert!(!same_line("line 0 1 1404", "line 0 1 1405"));
    assert!(!same_line("message |one", "message |two"));
}
