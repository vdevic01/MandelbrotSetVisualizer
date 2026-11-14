// The command-line arguments the app gives to the renderer (the `mandelbrot_visualizer` sidecar).
//
// This file is plain Rust with no dependencies, on purpose: the contract tests (contract-tests/) compile it on its
// own and run the real renderer with the argument lists it builds, so a flag that is renamed or removed in the
// renderer fails a test instead of failing at run time in the app.
//
// The renderer is driven by named flags (run `mandelbrot_visualizer --help` for the list).

/// The colour settings of an image.
pub struct Colour {
    pub palette_id: i32,
    pub palette_length: i32,
    pub color_spread: f64,
    pub histogram: bool,
    pub color_phase: f64,
    pub histogram_cycles: f64,
}

/// The part of the complex plane to render. Coordinates are decimal text for fixed point, so no digit is lost on
/// the way.
pub enum View<'a> {
    Double { re_start: f64, re_end: f64, im_start: f64, im_end: f64 },
    Fixed { re_start: &'a str, re_end: &'a str, im_start: &'a str, im_end: &'a str, fraction_words: u32 },
}

/// Everything that is rendered: the view, the iteration settings and the colours.
pub struct Render<'a> {
    pub mode: &'a str,
    pub view: View<'a>,
    pub max_iter: i32,
    pub samples: i32,
    pub escape_radius: f64,
    pub colour: Colour,
    /// The renderer draws 900 x 600 unless it is given an explicit size.
    pub width: Option<i32>,
    pub height: Option<i32>,
}

// The renderer draws 900 x 600 unless it is given an explicit size.
fn size_args(width: Option<i32>, height: Option<i32>) -> Result<Vec<String>, String> {
    match (width, height) {
        (None, None) => Ok(Vec::new()),
        (Some(w), Some(h)) if w > 0 && h > 0 => Ok(vec!["--size".to_string(), format!("{}x{}", w, h)]),
        _ => Err("Width and height must be given together and be positive.".to_string()),
    }
}

// The view as `--re` and `--im` ranges.
fn view_args(re_start: &str, re_end: &str, im_start: &str, im_end: &str) -> Vec<String> {
    vec![
        format!("--re={}:{}", re_start, re_end),
        format!("--im={}:{}", im_start, im_end),
    ]
}

// The colour method and the settings that belong to it (the renderer rejects settings of the other method).
fn colour_args(colour: &Colour) -> Vec<String> {
    let mut args = vec!["--palette".to_string(), colour.palette_id.to_string()];
    args.extend(["--color-phase".to_string(), colour.color_phase.to_string()]);
    if colour.histogram {
        args.extend([
            "--color".to_string(), "histogram".to_string(),
            "--histogram-cycles".to_string(), colour.histogram_cycles.to_string(),
        ]);
    } else {
        args.extend([
            "--color".to_string(), "cyclic".to_string(),
            "--palette-length".to_string(), colour.palette_length.to_string(),
            "--color-spread".to_string(), colour.color_spread.to_string(),
        ]);
    }
    args
}

/// The arguments of a render. `save_escape` is where the renderer keeps the escape results, so that the colours can
/// be changed later without rendering again (only the image shown in the app needs that).
pub fn render_args(render: &Render, output: &str, save_escape: Option<&str>) -> Result<Vec<String>, String> {
    let mut args = vec![
        "--mode".to_string(), render.mode.to_string(),
        "--output".to_string(), output.to_string(),
    ];
    match &render.view {
        View::Double { .. } => args.extend(["--precision".to_string(), "double".to_string()]),
        View::Fixed { fraction_words, .. } => args.extend([
            "--precision".to_string(), "fixed".to_string(),
            "--fraction-words".to_string(), fraction_words.to_string(),
        ]),
    }
    args.extend([
        "--max-iter".to_string(), render.max_iter.to_string(),
        "--samples".to_string(), render.samples.to_string(),
        "--escape-radius".to_string(), render.escape_radius.to_string(),
    ]);
    match &render.view {
        View::Double { re_start, re_end, im_start, im_end } => args.extend(view_args(
            &re_start.to_string(), &re_end.to_string(), &im_start.to_string(), &im_end.to_string(),
        )),
        View::Fixed { re_start, re_end, im_start, im_end, .. } => args.extend(view_args(re_start, re_end, im_start, im_end)),
    }
    args.extend(colour_args(&render.colour));
    args.extend(size_args(render.width, render.height)?);
    if let Some(path) = save_escape {
        args.extend(["--save-escape".to_string(), path.to_string()]);
    }
    Ok(args)
}

/// The arguments that paint the escape results saved by an earlier render again with other colours.
pub fn recolour_args(escape_file: &str, output: &str, colour: &Colour) -> Vec<String> {
    let mut args = vec![
        "--recolour-from".to_string(), escape_file.to_string(),
        "--output".to_string(), output.to_string(),
    ];
    args.extend(colour_args(colour));
    args
}
