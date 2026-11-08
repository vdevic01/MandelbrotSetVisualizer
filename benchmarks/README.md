# Benchmarks

Local performance benchmark for the visualizer. It runs the real executable on fixed scenes and records the stage timings the program prints (pixel mapping, escape iterations, coloring, image generation, total).

Timings only mean something when compared on the **same machine**, so everything is local: reports are git-ignored.

## Usage

Run from the repository root.

```bash
python benchmarks/bench.py run                   # benchmark HEAD
python benchmarks/bench.py run main              # any commit, branch or tag
python benchmarks/bench.py run 8bfd514 --force   # rerun an old commit (overwrites its report)
python benchmarks/bench.py compare main HEAD     # table: old vs new, % change
python benchmarks/bench.py clean                 # delete worktrees and build folders (keeps reports)
```

Useful options for `run`: `--runs N` (default 10), `--discard N` (default 3), `--scenes sp hp`, `--cmake <path>`, `--rebuild`.

Only **committed** code is benchmarked. Commit first, then run.

## What it does

1. Checks the commit out into its own git worktree (`benchmarks/.work/<hash>`), so your working folder and branch are never touched.
2. Builds it in Release mode (`benchmarks/.build/<hash>`). The first build of a commit takes a few minutes; later runs reuse it.
3. Runs each scene `--runs` times. The first `--discard` runs are thrown away (warm-up). The headline number is the **mean** of the remaining runs; median, min, max and standard deviation are stored too.
4. Writes `benchmarks/results/<full commit hash>.json`.

Scenes (the exact command lines are in `bench.py`):
- `sp`: single precision.
- `hp`: high precision (fixed-point).

CMake is taken from `--cmake`, then `$CMAKE`, then `PATH`, then CLion's bundled one (together with its MinGW and Ninja).

## Report format

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

## Getting reliable numbers

- Close other heavy programs and keep the laptop plugged in.
- Look at `stdev_ms` before trusting a small difference; a change within the noise is not a real change.
- Compare reports only from the same machine and the same settings.
- To get historical data, rerun old commits with `run <hash>`; the report is named after the hash.

## Disk space

Each benchmarked commit keeps a worktree and a build folder (a few hundred MB) so reruns are fast. `python benchmarks/bench.py clean` removes them; `clean <commit>` removes one. Reports are never deleted by the script.
