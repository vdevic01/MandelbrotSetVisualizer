#!/usr/bin/env python3
"""
Local performance benchmark for the Mandelbrot visualizer.

Builds a commit in Release mode (in its own git worktree, so your working tree is never touched), runs the
binary on fixed scenes, parses the stage timings the program already prints (e.g. "Calculating escape
iterations: 6232 ms"), and stores a machine-readable report named after the commit hash.

    python benchmarks/bench.py run                  # benchmark HEAD
    python benchmarks/bench.py run main             # benchmark any commit / branch / tag
    python benchmarks/bench.py run 8bfd514 --force  # re-run an old commit (overwrites its report)
    python benchmarks/bench.py compare main HEAD    # table + HTML chart of the difference between two reports

Reports live in benchmarks/results/<full commit hash>.json and charts in benchmarks/charts/ (both git-ignored:
timings are only comparable between runs on the same machine). Uncommitted changes are NOT benchmarked - commit first.

Method: each scene is run `--runs` times; the first `--discard` runs are thrown away (caches, CPU clocks,
page cache warm up) and the report's headline number is the mean of the remaining runs. Median, min, max and
standard deviation are stored too, to judge how noisy a measurement was. Close other heavy programs and keep
the laptop on mains power while benchmarking.
"""

import argparse
import contextlib
import datetime
import glob
import html
import json
import os
import platform
import re
import shutil
import stat
import statistics
import subprocess
import sys
import tempfile
import webbrowser
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
BENCH_DIR = REPO / "benchmarks"
RESULTS_DIR = BENCH_DIR / "results"  # JSON reports, one per commit
CHARTS_DIR = BENCH_DIR / "charts"    # HTML comparison charts
# Both are git-ignored. Nothing else is kept: builds happen in a temporary folder that is deleted afterwards.

TARGET = "mandelbrot_visualizer"
EXE_NAME = TARGET + (".exe" if os.name == "nt" else "")
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
    return subprocess.run(["git", *args], cwd=cwd, check=True, capture_output=True, text=True,
                          encoding="utf-8").stdout.strip()


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


def remove_tree(path):
    """Deletes a folder tree. Git marks its pack files read-only, which plain shutil.rmtree cannot delete on Windows."""
    def make_writable_and_retry(func, failed_path, _error):
        os.chmod(failed_path, stat.S_IWRITE)
        func(failed_path)
    hook = {"onexc": make_writable_and_retry} if sys.version_info >= (3, 12) else {"onerror": make_writable_and_retry}
    try:
        shutil.rmtree(path, **hook)
    except OSError as error:
        print("warning: could not fully delete %s (%s)" % (path, error))


@contextlib.contextmanager
def built_executable(commit, cmake, extra_path):
    """Builds `commit` in Release mode inside a temporary folder and yields the executable.

    The commit is checked out into a temporary git worktree (your working folder is never touched). On
    exit - also on errors and Ctrl+C - the worktree and the whole temporary folder are deleted.
    """
    tmp = Path(tempfile.mkdtemp(prefix="bench-"))
    src, bld = tmp / "src", tmp / "build"
    try:
        git("worktree", "add", "--detach", str(src), commit)
        env = dict(os.environ)
        env["PATH"] = os.pathsep.join(extra_path + [env.get("PATH", "")])
        generator = ["-G", "Ninja"] if shutil.which("ninja", path=env["PATH"]) else []
        print("configuring + building %s (a few minutes) ..." % commit[:10], flush=True)
        subprocess.run([cmake, "-S", str(src), "-B", str(bld), "-DCMAKE_BUILD_TYPE=Release", *generator],
                       check=True, env=env, stdout=subprocess.DEVNULL)
        subprocess.run([cmake, "--build", str(bld), "--target", TARGET], check=True, env=env,
                       stdout=subprocess.DEVNULL)
        exe = next(iter(bld.rglob(EXE_NAME)), None)
        if not exe:
            sys.exit("built %s but could not find the executable under %s" % (TARGET, bld))
        yield exe
    finally:
        subprocess.run(["git", "worktree", "remove", "--force", str(src)], cwd=REPO, check=False, capture_output=True)
        remove_tree(tmp)
        subprocess.run(["git", "worktree", "prune"], cwd=REPO, check=False, capture_output=True)
        print("temporary build files deleted")


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
    report = {
        "commit": commit,
        "subject": git("log", "-1", "--format=%s", commit),
        "commit_date": git("log", "-1", "--format=%cI", commit),
        "measured_at": datetime.datetime.now().astimezone().isoformat(timespec="seconds"),
        "machine": {"os": platform.platform(), "cpu": platform.processor(), "logical_cpus": os.cpu_count()},
        "settings": {"runs": args.runs, "discarded_runs": args.discard},
        "scenes": {},
    }
    with built_executable(commit, cmake, extra_path) as exe:
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


def percent_change(old, new):
    return (new - old) / old * 100 if old else 0.0


def cmd_compare(args):
    a, b = load_report(args.old), load_report(args.new)
    print("old: %s  %s" % (a["commit"][:10], a["subject"]))
    print("new: %s  %s" % (b["commit"][:10], b["subject"]))
    print()
    print("%-6s %-36s %10s %10s %9s" % ("scene", "stage", "old (ms)", "new (ms)", "change"))
    for scene in b["scenes"]:
        if scene not in a["scenes"]:
            continue
        for stage, nv in b["scenes"][scene]["stages"].items():
            ov = a["scenes"][scene]["stages"].get(stage)
            if ov:
                print("%-6s %-36s %10.1f %10.1f %+8.1f%%" % (scene, stage, ov["mean_ms"], nv["mean_ms"],
                                                           percent_change(ov["mean_ms"], nv["mean_ms"])))
    if not args.no_chart:
        out = CHARTS_DIR / ("compare-%s-%s.html" % (a["commit"][:10], b["commit"][:10]))
        CHARTS_DIR.mkdir(parents=True, exist_ok=True)
        out.write_text(chart_html(a, b), encoding="utf-8")
        print()
        print("chart: " + str(out.relative_to(REPO)))
        if not args.no_open:
            webbrowser.open(out.as_uri())


def chart_html(a, b):
    """Self-contained HTML page (no dependencies): per stage, old vs new bars scaled so the whiskers fit."""
    esc = html.escape
    rows = []
    for scene, bdata in b["scenes"].items():
        if scene not in a["scenes"]:
            continue
        rows.append('<h2>%s precision</h2>' % ("High" if scene == "hp" else "Single" if scene == "sp" else esc(scene)))
        stages = bdata["stages"]
        # the slow, interesting stages first
        order = sorted(stages, key=lambda n: -stages[n]["mean_ms"])
        for stage in order:
            ov, nv = a["scenes"][scene]["stages"].get(stage), stages[stage]
            if not ov:
                continue
            change = percent_change(ov["mean_ms"], nv["mean_ms"])
            noise = max(ov["stdev_ms"] / ov["mean_ms"], nv["stdev_ms"] / nv["mean_ms"]) * 100 if ov["mean_ms"] and nv["mean_ms"] else 0
            verdict = "slower" if change > max(5.0, noise) else "faster" if change < -max(5.0, noise) else "same"
            scale = max(ov["mean_ms"] + ov["stdev_ms"], nv["mean_ms"] + nv["stdev_ms"]) or 1.0   # leave room for the whiskers
            bars = ""
            for label, v, cls in (("old", ov, "old"), ("new", nv, "new " + verdict)):
                width = v["mean_ms"] / scale * 100
                err = v["stdev_ms"] / scale * 100
                bars += ('<div class="bar-row"><span class="who">%s</span><div class="track">'
                         '<div class="bar %s" style="width:%.2f%%"></div>'
                         '<div class="whisker" style="left:%.2f%%;width:%.2f%%"></div></div>'
                         '<span class="val">%.0f ms <small>&plusmn;%.0f</small></span></div>'
                         % (label, cls, width, max(0, width - err), min(2 * err, 100 - max(0, width - err), width + err),
                            v["mean_ms"], v["stdev_ms"]))
            rows.append('<div class="stage"><div class="stage-head"><b>%s</b><span class="badge %s">%+.1f%%</span></div>%s</div>'
                        % (esc(stage), verdict, change, bars))
    css = """
:root{--bg:#fff;--fg:#1d1d1f;--muted:#6e6e73;--card:#f5f5f7;--old:#8e8e93;--new:#0a84ff;--bad:#d70015;--good:#1a7f37;--same:#8e8e93}
@media (prefers-color-scheme:dark){:root{--bg:#1c1c1e;--fg:#f5f5f7;--muted:#a1a1a6;--card:#2c2c2e;--old:#636366;--new:#64a8ff;--bad:#ff6961;--good:#4cd964}}
body{font:15px/1.4 system-ui,sans-serif;background:var(--bg);color:var(--fg);max-width:820px;margin:0 auto;padding:24px 16px}
h1{font-size:20px;margin:0 0 4px}h2{font-size:17px;margin:28px 0 10px}.sub{color:var(--muted);margin:2px 0}
.stage{background:var(--card);border-radius:10px;padding:10px 14px;margin:8px 0}
.stage-head{display:flex;justify-content:space-between;margin-bottom:6px}
.badge{font-weight:600;font-variant-numeric:tabular-nums}.badge.slower{color:var(--bad)}.badge.faster{color:var(--good)}.badge.same{color:var(--muted)}
.bar-row{display:flex;align-items:center;gap:8px;margin:3px 0}.who{width:30px;color:var(--muted);font-size:13px}
.track{position:relative;flex:1;height:16px}.bar{height:100%;border-radius:3px;background:var(--old)}
.bar.new{background:var(--new)}.bar.new.slower{background:var(--bad)}.bar.new.faster{background:var(--good)}
.whisker{position:absolute;top:7px;height:2px;background:var(--fg);opacity:.55}
.val{width:120px;text-align:right;font-variant-numeric:tabular-nums}small{color:var(--muted)}
.note{color:var(--muted);font-size:13px;margin-top:24px}
"""
    head = ('<h1>Benchmark comparison</h1><p class="sub">old: <b>%s</b> %s</p><p class="sub">new: <b>%s</b> %s</p>'
            % (a["commit"][:10], esc(a["subject"]), b["commit"][:10], esc(b["subject"])))
    note = ('<p class="note">Bars show the mean, with a whisker for &plusmn; one standard deviation; each pair is scaled '
            'to the larger of the two (mean + deviation). A change counts as slower/faster only when it exceeds 5%% and the run-to-run noise.</p>')
    return ('<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">'
            '<title>Benchmark comparison</title><style>%s</style></head><body>%s%s%s</body></html>'
            % (css, head, "".join(rows), note))


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
    run.set_defaults(func=cmd_run)

    cmp_ = sub.add_parser("compare", help="compare two stored reports")
    cmp_.add_argument("old")
    cmp_.add_argument("new")
    cmp_.add_argument("--no-chart", action="store_true", help="only print the table")
    cmp_.add_argument("--no-open", action="store_true", help="write the HTML chart but do not open the browser")
    cmp_.set_defaults(func=cmd_compare)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
