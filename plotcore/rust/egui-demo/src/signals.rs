// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

//! What the example plots: signals made to show one thing each, and the two
//! file formats a reader is most likely to have a line in.

use std::f64::consts::TAU;

pub struct Signal {
    pub name: String,
    pub values: Vec<f64>,
}

/// The C library's generator, so every language's example can make the same
/// line.
struct Lcg(u32);

impl Lcg {
    fn next(&mut self) -> f64 {
        self.0 = self.0.wrapping_mul(1_103_515_245).wrapping_add(12_345);
        f64::from((self.0 >> 16) & 0x7fff) / 32767.0
    }
}

pub const NAMES: [&str; 6] = [
    "Sine with a one-sample spike",
    "Square wave",
    "Gaps (NaN)",
    "Twelve decades",
    "Random walk",
    "Ten million samples",
];

pub fn make(which: usize, seed: u32) -> Signal {
    match which {
        0 => spiked_sine(200_000),
        1 => square_wave(20_000),
        2 => with_gaps(20_000),
        3 => many_decades(100_000),
        4 => random_walk(1_000_000, seed),
        _ => ten_million(),
    }
}

/// A sine with one sample a hundred times its height -- the spike an envelope
/// keeps and a stride throws away.
pub fn spiked_sine(count: usize) -> Signal {
    let mut values: Vec<f64> = (0..count).map(|i| (TAU * i as f64 / 5000.0).sin()).collect();
    values[count * 7 / 10] = 100.0;
    Signal { name: "sine with a spike".into(), values }
}

pub fn square_wave(count: usize) -> Signal {
    let values = (0..count).map(|i| if (i / 250) % 2 == 0 { -1.0 } else { 1.0 }).collect();
    Signal { name: "square wave".into(), values }
}

/// A ramp with runs of NaN in it: every gap ends a stroke.
pub fn with_gaps(count: usize) -> Signal {
    let values = (0..count)
        .map(|i| {
            if (i / 400) % 5 == 4 {
                f64::NAN
            } else {
                (i as f64 / 300.0).cos() + 0.0001 * i as f64
            }
        })
        .collect();
    Signal { name: "with gaps".into(), values }
}

/// Twelve decades, for a logarithmic axis.
pub fn many_decades(count: usize) -> Signal {
    let values = (0..count)
        .map(|i| {
            let at = i as f64 / (count.max(2) - 1) as f64;
            10f64.powf(-6.0 + 12.0 * at) * (1.0 + 0.3 * (40.0 * at).sin())
        })
        .collect();
    Signal { name: "twelve decades".into(), values }
}

/// A random walk with a one-sample spike every 997 elements.
pub fn random_walk(count: usize, seed: u32) -> Signal {
    let mut random = Lcg(seed);
    let mut at = 0.0;
    let values = (0..count)
        .map(|i| {
            at += random.next() - 0.5;
            if i % 997 == 996 {
                at + 500.0
            } else {
                at
            }
        })
        .collect();
    Signal { name: "random walk".into(), values }
}

/// Ten million samples of a chirp with noise, and two samples far from where
/// anyone would think to zoom.
pub fn ten_million() -> Signal {
    let count = 10_000_000;
    let mut random = Lcg(2026);
    let mut values: Vec<f64> = (0..count)
        .map(|i| {
            let t = i as f64 / count as f64;
            (TAU * (20.0 * t + 2000.0 * t * t)).sin() + 0.2 * (random.next() - 0.5)
        })
        .collect();
    values[7_777_777] = 6.0;
    values[123_456] = -6.0;
    Signal { name: "ten million".into(), values }
}

pub fn regular_time(count: usize, step: f64) -> Vec<f64> {
    (0..count).map(|i| i as f64 * step).collect()
}

/// A time base that only goes one way, at a rate that wanders.
pub fn irregular_time(count: usize, seed: u32) -> Vec<f64> {
    let mut random = Lcg(seed);
    let mut at = 0.0;
    (0..count)
        .map(|_| {
            let here = at;
            at += 0.0002 + 0.0018 * random.next() * random.next();
            here
        })
        .collect()
}

/// The lines in a .npy file (1-D, or one per column of 2-D; floats and
/// integers of either byte order) or a CSV (one per numeric column; a first
/// row that is not numbers names them).
pub fn load(path: &str) -> Result<Vec<Signal>, String> {
    let bytes = std::fs::read(path).map_err(|e| format!("cannot open {path}: {e}"))?;
    if path.to_lowercase().ends_with(".npy") {
        load_npy(&bytes)
    } else {
        load_csv(&String::from_utf8_lossy(&bytes))
    }
}

fn load_npy(bytes: &[u8]) -> Result<Vec<Signal>, String> {
    if bytes.len() < 10 || &bytes[..6] != b"\x93NUMPY" {
        return Err("not a .npy file".into());
    }
    let (header_length, start) = if bytes[6] == 1 {
        (usize::from(u16::from_le_bytes([bytes[8], bytes[9]])), 10)
    } else {
        let length = u32::from_le_bytes([bytes[8], bytes[9], bytes[10], bytes[11]]);
        (length as usize, 12)
    };
    let header =
        String::from_utf8_lossy(bytes.get(start..start + header_length).ok_or("a short header")?);
    let field = |key: &str| header.split(&format!("'{key}'")).nth(1).unwrap_or("").to_owned();
    let descr = field("descr");
    let descr = descr.split('\'').nth(1).unwrap_or("").to_owned();
    let fortran = field("fortran_order").trim_start_matches([':', ' ']).starts_with("True");
    let shape_text = field("shape");
    let shape_text = shape_text.split('(').nth(1).and_then(|s| s.split(')').next()).unwrap_or("");
    let shape: Vec<usize> = shape_text.split(',').filter_map(|p| p.trim().parse().ok()).collect();
    if descr.len() < 3 || shape.is_empty() || shape.len() > 2 {
        return Err(format!(
            "only 1-D and 2-D arrays of numbers are plotted, not {descr} ({shape_text})"
        ));
    }
    let (order, kind) = (descr.as_bytes()[0], descr.as_bytes()[1]);
    let width: usize = descr[2..].parse().map_err(|_| format!("not a type this reads: {descr}"))?;
    if !matches!((kind, width), (b'f', 4 | 8) | (b'i' | b'u', 1 | 2 | 4 | 8)) {
        return Err(format!("not a type this reads: {descr}"));
    }
    let big = order == b'>';
    let (rows, columns) = (shape[0], shape.get(1).copied().unwrap_or(1));
    let data = &bytes[start + header_length..];
    if data.len() < rows * columns * width {
        return Err("a .npy file shorter than its shape".into());
    }
    let value = |index: usize| -> f64 {
        let mut raw = [0u8; 8];
        raw[..width].copy_from_slice(&data[index * width..(index + 1) * width]);
        if big {
            raw[..width].reverse();
        }
        match (kind, width) {
            (b'f', 4) => f64::from(f32::from_le_bytes([raw[0], raw[1], raw[2], raw[3]])),
            (b'f', _) => f64::from_le_bytes(raw),
            (b'u', _) => u64::from_le_bytes(raw) as f64,
            _ => {
                let shift = 64 - 8 * width as u32;
                ((i64::from_le_bytes(raw) << shift) >> shift) as f64
            }
        }
    };
    Ok((0..columns)
        .map(|c| Signal {
            name: if columns == 1 { "npy".into() } else { format!("column {c}") },
            values: (0..rows)
                .map(|r| value(if fortran { c * rows + r } else { r * columns + c }))
                .collect(),
        })
        .collect())
}

fn load_csv(text: &str) -> Result<Vec<Signal>, String> {
    let mut lines =
        text.lines().map(|l| l.trim_end_matches('\r')).filter(|l| !l.is_empty()).peekable();
    let first = lines.peek().ok_or("no numbers in the file")?;
    let separator = if first.contains(';') {
        ';'
    } else if first.contains('\t') {
        '\t'
    } else {
        ','
    };
    let cells =
        |line: &str| line.split(separator).map(str::trim).map(str::to_owned).collect::<Vec<_>>();
    let header = cells(first);
    let named = header.iter().any(|cell| cell.parse::<f64>().is_err());
    let mut out: Vec<Signal> = header
        .iter()
        .enumerate()
        .map(|(i, cell)| Signal {
            name: if named { cell.clone() } else { format!("column {i}") },
            values: vec![],
        })
        .collect();
    if named {
        lines.next();
    }
    for line in lines {
        let row = cells(line);
        for (i, signal) in out.iter_mut().enumerate() {
            signal.values.push(row.get(i).and_then(|c| c.parse().ok()).unwrap_or(f64::NAN));
        }
    }
    if out.first().is_none_or(|s| s.values.is_empty()) {
        return Err("no numbers in the file".into());
    }
    Ok(out)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_csv_with_a_header_is_its_columns() {
        let lines = load_csv("time;volts\n0;1.5\n1;2.5\n2;x\n").unwrap();
        assert_eq!(lines.len(), 2);
        assert_eq!(lines[1].name, "volts");
        assert_eq!(lines[1].values[..2], [1.5, 2.5]);
        assert!(lines[1].values[2].is_nan());
    }

    #[test]
    fn a_npy_of_two_columns_is_two_lines() {
        let header = "{'descr': '<i2', 'fortran_order': False, 'shape': (3, 2), }";
        let mut bytes = b"\x93NUMPY\x01\x00".to_vec();
        let padded = format!("{header:<54}\n");
        bytes.extend_from_slice(&(padded.len() as u16).to_le_bytes());
        bytes.extend_from_slice(padded.as_bytes());
        for v in [1i16, -1, 2, -2, 3, -3] {
            bytes.extend_from_slice(&v.to_le_bytes());
        }
        let lines = load_npy(&bytes).unwrap();
        assert_eq!(lines.len(), 2);
        assert_eq!(lines[0].values, [1.0, 2.0, 3.0]);
        assert_eq!(lines[1].values, [-1.0, -2.0, -3.0]);
    }
}
