# Benchmarks

Local performance benchmark for the visualizer. It runs the real executable on fixed scenes and records the stage timings the program prints (pixel mapping, escape iterations, coloring, image generation, total).

Timings only mean something when compared on the **same machine**, so everything is local: reports are git-ignored.

## Usage

Run from the repository root.

```bash
python benchmarks/bench.py run                   # benchmark HEAD
python benchmarks/bench.py run main              # any commit, branch or tag
python benchmarks/bench.py run 8bfd514 --force   # rerun an old commit (overwrites its report)
python benchmarks/bench.py compare main HEAD     # build both, run them in turn, table + HTML chart
python benchmarks/bench.py compare main HEAD --stored   # compare two reports of `run` (quick, less reliable)
```

To find out whether a change made the program faster or slower, use `compare`. `run` is for keeping a record of one commit.

`compare` also writes `benchmarks/charts/compare-<old>-<new>.html`: per stage, old and new bars with a noise whisker, the % change and its noise (red = slower, green = faster, only when the change is above 5% and above the noise). Use `--no-chart` for the table only, or `--no-open` to write the file without opening it.

Useful options for `run` and `compare`: `--runs N` (default 10), `--discard N` (default 3), `--scenes sp hp`, `--cmake <path>`. `compare` keeps its measurement in `benchmarks/results/compare-<old>-<new>.json` and shows it again the next time; `--force` measures again.

Only **committed** code is benchmarked. Commit first, then run.

## What it does

1. Checks the commit out into a temporary git worktree (so your working folder and branch are never touched) and builds it in Release mode. This takes a few minutes every time.
2. Runs each scene `--runs` times. The first `--discard` runs are thrown away (warm-up). The headline number is the **mean** of the remaining runs; median, min, max and standard deviation are stored too.
3. Deletes the worktree and the build, then writes `benchmarks/results/<full commit hash>.json`.

Nothing is kept except the report (and the chart, if you run `compare`).

## Why `compare` runs the two commits in turn

The speed of a machine changes over minutes: it warms up and slows down, and other programs come and go. Two commits measured one after the other can therefore differ by tens of percent with no change in the code. This happened here: a commit that only touched fixed-point code showed the untouched single-precision scene as 40% slower, because the machine had slowed down between the two runs.

So `compare` builds both commits first and then measures them round by round. A round runs the scene once with each build, and the order alternates (old, new / new, old / ...), so both builds see the same machine. For every stage the report gives:

- **change**: the mean of the new build against the mean of the old one.
- **noise**: the change is also computed inside every round; the noise is the spread (standard deviation) of those values. A change counts only when it is above 5% and above this noise.

If the machine got more than 10% faster or slower between the first and the last rounds of a scene, the table and the chart say so. The change between the builds still holds in that case, but the times themselves are less reliable, so run it again when the machine is quiet.

A scene whose code you did not change is a useful check: it should come out as "same".

`compare --stored` skips all of this and compares two reports of `run`. Those were measured at different times, so only trust it when the machine was steady in between; the output carries a warning.

Scenes (the exact command lines are in `bench.py`; they are converted to the named flags for commits that understand them, and older commits get the old positional arguments, so every commit renders the same image):
- `sp`: single precision.
- `hp`: high precision (fixed-point).

CMake is taken from `--cmake`, then `$CMAKE`, then `PATH`, then CLion's bundled one (together with its MinGW and Ninja).

## Report format

Report of `run` (`benchmarks/results/<full commit hash>.json`):

```json
{
  "commit": "<40-char hash>",
  "subject": "commit message",
  "commit_date": "...", "measured_at": "...",
  "machine": { "os": "...", "cpu": "...", "logical_cpus": 12 },
  "settings": { "runs": 10, "discarded_runs": 3 },
  "scenes": {
    "hp": {
      "arguments": ["CPU_PARALLEL", "{out}", "..."],
      "stages": {
        "Calculating escape iterations": {
          "mean_ms": 9880.7, "median_ms": 9689, "min_ms": 9400, "max_ms": 11000, "stdev_ms": 615.7,
          "samples_ms": [ ... ], "discarded_ms": [ ... ]
        }
      }
    }
  }
}
```

The report of `compare` (`benchmarks/results/compare-<old>-<new>.json`) holds, per scene, the same `stages` data for `old` and for `new`, plus `changes` (per stage: the change inside every round and its spread) and `drift_pct` (how much the machine sped up or slowed down during the scene).

## Getting reliable numbers

- Close other heavy programs and keep the laptop plugged in.
- Use `compare` (without `--stored`) to judge a change; do not compare two `run` reports measured at different times.
- Look at the noise before trusting a small difference; a change within the noise is not a real change.
- Compare reports only from the same machine and the same settings.
- To get historical data, rerun old commits with `run <hash>`; the report is named after the hash.

## Precision calibration

`precision_calibration.py` is a separate tool. It measures how many pixels of an image are wrong for each fixed-point size at a given number of resolution steps per pixel, which is where the app's zoom-level switch points come from (see the "High-precision mode" section of the main README). It needs numpy:

```bash
python benchmarks/precision_calibration.py --exe path/to/mandelbrot_visualizer
```
