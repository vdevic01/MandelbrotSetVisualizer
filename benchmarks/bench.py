#!/usr/bin/env python3
"""
Local performance benchmark for the Mandelbrot visualizer.

Builds a commit in Release mode (in its own git worktree, so your working tree is never touched), runs the
binary on fixed scenes, parses the stage timings the program already prints (e.g. "Calculating escape
iterations: 6232 ms"), and stores a machine-readable report named after the commit hash.

    python benchmarks/bench.py run                  # benchmark HEAD
    python benchmarks/bench.py run main             # benchmark any commit / branch / tag
    python benchmarks/bench.py run 8bfd514 --force  # re-run an old commit (overwrites its report)
    python benchmarks/bench.py compare main HEAD    # table of the difference between two stored reports
    python benchmarks/bench.py clean                # delete all worktrees and build folders (keeps reports)

Reports live in benchmarks/results/<full commit hash>.json (git-ignored: timings are only comparable
between runs on the same machine). Uncommitted changes are NOT benchmarked - commit first.

Method: each scene is run `--runs` times; the first `--discard` runs are thrown away (caches, CPU clocks,
page cache warm up) and the report's headline number is the mean of the remaining runs. Median, min, max and
standard deviation are stored too, to judge how noisy a measurement was. Close other heavy programs and keep
the laptop on mains power while benchmarking.
"""

import argparse
import datetime
import glob
import json
import os
import platform
import re
import shutil
import statistics
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
BENCH_DIR = REPO / "benchmarks"
RESULTS_DIR = BENCH_DIR / "results"
WORK_DIR = BENCH_DIR / ".work"      # git worktrees, one per benchmarked commit (git-ignored)
BUILD_DIR = BENCH_DIR / ".build"    # CMake build directories, one per benchmarked commit (git-ignored)

TARGET = "mandelbrot_visualizer"
STAGE_LINE = re.compile(r"^(.+?):\s+(\d+) ms\s*$")

# ----------------------------------------------------------------------------------------------- scenes
# Fixed command lines, so every commit renders exactly the same thing. "{out}" is replaced by a
# temporary output file. Argument order: MODE OUTPUT MAX_ITER PALETTE_LENGTH PALETTE_ID SAMPLES USE_HP
# RE_START RE_END IM_START IM_END WIDTH HEIGHT (high precision bounds are 4 words each).
SCENES = {
    "sp": ["CPU_PARALLEL", "{out}", "2000", "512", "0", "4", "0",
           "-0.153004885037500013708", "-0.152809695287500013708",
           "1.039611370300000000002", "1.039757762612500000002", "1800", "1200"],
    "hp": ["CPU_PARALLEL", "{out}", "700", "512", "0", "4", "1",
           "4294967295", "3637816318", "2730300863", "4039731417",
           "4294967295", "3638654652", "981237348", "3835888558",
           "1", "170129539", "4244482999", "3226762018",
           "1", "170758290", "785201715", "2000138050", "900", "600"],
}


def scene_args(scene, output_png):
    return [str(output_png) if a == "{out}" else a for a in SCENES[scene]]


# ----------------------------------------------------------------------------------------------- git/build
def git(*args, cwd=REPO):
    return subprocess.run(["git", *args], cwd=cwd, check=True, capture_output=True, text=True).stdout.strip()


def resolve(rev):
    return git("rev-parse", "--verify", rev + "^{commit}")


def find_tools(cmake_arg):
    """Returns (cmake path, extra PATH entries). Falls back to the CMake bundled with CLion."""
    candidates = [cmake_arg, os.environ.get("CMAKE"), shutil.which("cmake")]
    candidates += sorted(glob.glob("C:/Program Files/JetBrains/CLion*/bin/cmake/win/x64/bin/cmake.exe"), reverse=True)
    for c in candidates:
        if c and Path(c).exists():
            cmake = str(Path(c))
            break
        if c and shutil.which(c):
            cmake = shutil.which(c)
            break
    else:
        sys.exit("cmake not found. Pass --cmake <path> or set the CMAKE environment variable.")
    extra = []
    m = re.match(r"(.*[/\\]bin)[/\\]cmake[/\\]", cmake.replace("\\", "/"))
    if m and "JetBrains" in cmake:   # CLion bundle: also use its MinGW and Ninja
        clion_bin = Path(m.group(1))
        extra = [str(clion_bin / "mingw" / "bin"), str(clion_bin / "ninja" / "win" / "x64")]
    return cmake, [p for p in extra if Path(p).exists()]


def build(commit, cmake, extra_path, rebuild):
    """Builds `commit` in Release mode and returns the path of the executable."""
    src = WORK_DIR / commit
    bld = BUILD_DIR / commit
    if not src.exists():
        WORK_DIR.mkdir(parents=True, exist_ok=True)
        git("worktree", "add", "--detach", str(src), commit)
    if rebuild and bld.exists():
        shutil.rmtree(bld)
    env = dict(os.environ)
    env["PATH"] = os.pathsep.join(extra_path + [env.get("PATH", "")])
    generator = ["-G", "Ninja"] if shutil.which("ninja", path=env["PATH"]) else []
    print("configuring + building %s ..." % commit[:10], flush=True)
    subprocess.run([cmake, "-S", str(src), "-B", str(bld), "-DCMAKE_BUILD_TYPE=Release", *generator],
                   check=True, env=env, stdout=subprocess.DEVNULL)
    subprocess.run([cmake, "--build", str(bld), "--target", TARGET], check=True, env=env, stdout=subprocess.DEVNULL)
    exe = next(iter(bld.rglob(TARGET + (".exe" if os.name == "nt" else ""))), None)
    if not exe:
        sys.exit("built %s but could not find the executable under %s" % (TARGET, bld))
    return exe


# ----------------------------------------------------------------------------------------------- running
def run_once(exe, scene):
    with tempfile.TemporaryDirectory() as tmp:
        proc = subprocess.run([str(exe), *scene_args(scene, Path(tmp) / "out.png")], capture_output=True, text=True)
    if proc.returncode != 0:
        sys.exit("run failed (exit %d):\n%s%s" % (proc.returncode, proc.stdout, proc.stderr))
    stages = {}
    for line in proc.stdout.splitlines():
        m = STAGE_LINE.match(line)
        if m:
            stages[m.group(1)] = int(m.group(2))
    if not stages:
        sys.exit("no stage timings found in the program output:\n" + proc.stdout)
    return stages


def summarize(per_run, discard):
    names = per_run[0].keys()
    out = {}
    for name in names:
        all_values = [r[name] for r in per_run if name in r]
        kept = all_values[discard:]
        out[name] = {
            "mean_ms": round(statistics.mean(kept), 1),
            "median_ms": statistics.median(kept),
            "min_ms": min(kept),
            "max_ms": max(kept),
            "stdev_ms": round(statistics.stdev(kept), 1) if len(kept) > 1 else 0.0,
            "samples_ms": kept,
            "discarded_ms": all_values[:discard],
        }
    return out


def cmd_run(args):
    if args.runs <= args.discard:
        sys.exit("--runs must be larger than --discard")
    commit = resolve(args.rev)
    report_path = RESULTS_DIR / (commit + ".json")
    if report_path.exists() and not args.force:
        sys.exit("%s already exists. Use --force to run again and overwrite it." % report_path.relative_to(REPO))
    if git("rev-parse", "HEAD") == commit and git("status", "--porcelain", "--untracked-files=no"):
        print("note: you have uncommitted changes; they are NOT part of this benchmark (only commit %s)." % commit[:10])

    cmake, extra_path = find_tools(args.cmake)
    exe = build(commit, cmake, extra_path, args.rebuild)

    report = {
        "commit": commit,
        "subject": git("log", "-1", "--format=%s", commit),
        "commit_date": git("log", "-1", "--format=%cI", commit),
        "measured_at": datetime.datetime.now().astimezone().isoformat(timespec="seconds"),
        "machine": {"os": platform.platform(), "cpu": platform.processor(), "logical_cpus": os.cpu_count()},
        "settings": {"runs": args.runs, "discarded_runs": args.discard},
        "scenes": {},
    }
    for scene in args.scenes:
        print("scene '%s': %d runs (first %d discarded)" % (scene, args.runs, args.discard), flush=True)
        per_run = []
        for i in range(args.runs):
            per_run.append(run_once(exe, scene))
            print("  run %2d: %s" % (i + 1, ", ".join("%s=%d" % kv for kv in per_run[-1].items() if "Calculating" in kv[0])),
                  flush=True)
        report["scenes"][scene] = {"arguments": SCENES[scene], "stages": summarize(per_run, args.discard)}

    RESULTS_DIR.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(report, indent=2) + "\n")
    print("\nsaved " + str(report_path.relative_to(REPO)))
    print_report(report)


def print_report(report):
    print("commit %s  %s" % (report["commit"][:10], report["subject"]))
    for scene, data in report["scenes"].items():
        print("  scene %s" % scene)
        for stage, v in data["stages"].items():
            print("    %-36s mean %8.1f ms   median %8.1f   stdev %6.1f" % (stage, v["mean_ms"], v["median_ms"], v["stdev_ms"]))


# ----------------------------------------------------------------------------------------------- compare
def load_report(rev):
    path = RESULTS_DIR / (resolve(rev) + ".json")
    if not path.exists():
        sys.exit("no report for %s yet. Run: python benchmarks/bench.py run %s" % (rev, rev))
    return json.loads(path.read_text())


def cmd_compare(args):
    a, b = load_report(args.old), load_report(args.new)
    print("old: %s  %s" % (a["commit"][:10], a["subject"]))
    print("new: %s  %s\n" % (b["commit"][:10], b["subject"]))
    print("%-6s %-36s %10s %10s %9s" % ("scene", "stage", "old (ms)", "new (ms)", "change"))
    for scene in b["scenes"]:
        if scene not in a["scenes"]:
            continue
        for stage, nv in b["scenes"][scene]["stages"].items():
            ov = a["scenes"][scene]["stages"].get(stage)
            if not ov:
                continue
            change = (nv["mean_ms"] - ov["mean_ms"]) / ov["mean_ms"] * 100 if ov["mean_ms"] else 0.0
            print("%-6s %-36s %10.1f %10.1f %+8.1f%%" % (scene, stage, ov["mean_ms"], nv["mean_ms"], change))


# ----------------------------------------------------------------------------------------------- clean
def cmd_clean(args):
    """Removes the worktrees and build folders (the next run of a commit rebuilds it). Reports are kept."""
    commits = [resolve(args.rev)] if args.rev else sorted(
        {p.name for d in (WORK_DIR, BUILD_DIR) if d.exists() for p in d.iterdir() if p.is_dir()})
    if not commits:
        print("nothing to clean")
        return
    for commit in commits:
        for path, kind in ((WORK_DIR / commit, "worktree"), (BUILD_DIR / commit, "build folder")):
            if not path.exists():
                continue
            print("%s %s: %s" % ("would remove" if args.dry_run else "removing", kind, path.relative_to(REPO)))
            if args.dry_run:
                continue
            if kind == "worktree":
                subprocess.run(["git", "worktree", "remove", "--force", str(path)], cwd=REPO, check=False,
                               capture_output=True)
            if path.exists():
                shutil.rmtree(path, ignore_errors=True)
    if not args.dry_run:
        git("worktree", "prune")
        for d in (WORK_DIR, BUILD_DIR):   # remove the parent folders once empty
            if d.exists() and not any(d.iterdir()):
                d.rmdir()


# ----------------------------------------------------------------------------------------------- main
def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)

    run = sub.add_parser("run", help="build a commit and benchmark it")
    run.add_argument("rev", nargs="?", default="HEAD", help="commit, branch or tag (default: HEAD)")
    run.add_argument("--runs", type=int, default=10, help="runs per scene (default 10)")
    run.add_argument("--discard", type=int, default=3, help="warm-up runs to throw away (default 3)")
    run.add_argument("--scenes", nargs="+", choices=sorted(SCENES), default=sorted(SCENES))
    run.add_argument("--cmake", help="path to cmake (default: PATH, $CMAKE, then CLion's bundled one)")
    run.add_argument("--force", action="store_true", help="overwrite an existing report for this commit")
    run.add_argument("--rebuild", action="store_true", help="delete the build directory and build from scratch")
    run.set_defaults(func=cmd_run)

    cmp_ = sub.add_parser("compare", help="compare two stored reports")
    cmp_.add_argument("old")
    cmp_.add_argument("new")
    cmp_.set_defaults(func=cmd_compare)

    clean = sub.add_parser("clean", help="delete worktrees and build folders (reports are kept)")
    clean.add_argument("rev", nargs="?", help="only clean this commit (default: all)")
    clean.add_argument("--dry-run", action="store_true", help="show what would be removed")
    clean.set_defaults(func=cmd_clean)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
