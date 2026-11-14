// Prints, as JSON, the argument lists the app gives to the renderer for a few representative requests. It builds
// them with the real code of the app (src-tauri/src/cli_args.rs, compiled as a module of this file), so
// check-cli.mjs can run the real renderer with exactly what the app would run it with.
//
//   rustc --edition 2021 -o dump_args contract-tests/dump_args.rs && ./dump_args <directory for the files>

#[path = "../src-tauri/src/cli_args.rs"]
mod cli_args;

use cli_args::{recolour_args, render_args, Colour, Render, View};

fn json_string(text: &str) -> String {
    let mut out = String::from("\"");
    for c in text.chars() {
        match c {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            '\n' => out.push_str("\\n"),
            c => out.push(c),
        }
    }
    out.push('"');
    out
}

fn cyclic() -> Colour {
    Colour { palette_id: 0, palette_length: 250, color_spread: 0.0, histogram: false, color_phase: 0.0, histogram_cycles: 1.0 }
}

fn histogram() -> Colour {
    Colour { palette_id: 2, palette_length: 250, color_spread: 0.0, histogram: true, color_phase: 0.5, histogram_cycles: 3.0 }
}

struct Scenario {
    name: &'static str,
    args: Vec<String>,
    output: String,
    /// The escape file this run writes (and later runs read), if any.
    escape_file: Option<String>,
    /// Stage names the app reads from the renderer's output.
    stages: &'static [&'static str],
}

fn main() {
    let dir = std::env::args().nth(1).expect("usage: dump_args <directory>");
    let file = |name: &str| format!("{}/{}", dir.trim_end_matches(['/', '\\']), name);
    let escape = file("escape.bin");

    let double = |colour: Colour, width: Option<i32>, height: Option<i32>| Render {
        mode: "CPU_PARALLEL",
        view: View::Double { re_start: -2.2, re_end: 0.8, im_start: -1.0, im_end: 1.0 },
        max_iter: 60, samples: 2, escape_radius: 2.0, colour, width, height,
    };
    let fixed = |colour: Colour| Render {
        mode: "CPU_PARALLEL",
        view: View::Fixed { re_start: "-2.2", re_end: "0.8", im_start: "-1", im_end: "1", fraction_words: 2 },
        max_iter: 60, samples: 1, escape_radius: 2.0, colour, width: Some(80), height: Some(60),
    };

    let mut scenarios = Vec::new();

    // The image shown in the app: no size (the renderer's default), and the escape results are kept.
    scenarios.push(Scenario {
        name: "render in double precision (the image shown in the app)",
        args: render_args(&double(cyclic(), None, None), &file("shown.png"), Some(&escape)).unwrap(),
        output: file("shown.png"), escape_file: Some(escape.clone()),
        stages: &["Pixel mapping", "Calculating escape iterations", "Coloring", "Image generation", "Total generation time"],
    });
    // A colour change of that image, without rendering again.
    scenarios.push(Scenario {
        name: "recolour with the cyclic method",
        args: recolour_args(&escape, &file("recoloured_cyclic.png"), &Colour { palette_id: 3, color_phase: 0.25, ..cyclic() }),
        output: file("recoloured_cyclic.png"), escape_file: None,
        stages: &["Coloring", "Image generation", "Total recolour time"],
    });
    scenarios.push(Scenario {
        name: "recolour with the histogram method",
        args: recolour_args(&escape, &file("recoloured_histogram.png"), &histogram()),
        output: file("recoloured_histogram.png"), escape_file: None,
        stages: &["Coloring", "Image generation", "Total recolour time"],
    });
    // An export: an explicit size and no escape file.
    scenarios.push(Scenario {
        name: "export in double precision with the histogram method",
        args: render_args(&double(histogram(), Some(96), Some(64)), &file("export.png"), None).unwrap(),
        output: file("export.png"), escape_file: None,
        stages: &["Coloring", "Image generation", "Total generation time"],
    });
    scenarios.push(Scenario {
        name: "render in fixed point",
        args: render_args(&fixed(cyclic()), &file("fixed.png"), None).unwrap(),
        output: file("fixed.png"), escape_file: None,
        stages: &["Calculating escape iterations", "Coloring", "Image generation", "Total generation time"],
    });

    let list = |items: &[String]| items.iter().map(|s| json_string(s)).collect::<Vec<_>>().join(", ");
    let entries: Vec<String> = scenarios.iter().map(|s| {
        format!(
            "{{\"name\": {}, \"args\": [{}], \"output\": {}, \"escapeFile\": {}, \"stages\": [{}]}}",
            json_string(s.name), list(&s.args), json_string(&s.output),
            s.escape_file.as_deref().map(json_string).unwrap_or_else(|| "null".to_string()),
            s.stages.iter().map(|t| json_string(t)).collect::<Vec<_>>().join(", "),
        )
    }).collect();
    println!("[{}]", entries.join(",\n "));
}
