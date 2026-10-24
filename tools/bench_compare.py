#!/usr/bin/env python3
"""Compare the benchmarks of two commits on this machine.

    tools/bench_compare.py                     # the working tree against main
    tools/bench_compare.py --base HEAD~1       # against the previous commit
    tools/bench_compare.py --bench lookup call --runs 5 --pin 2
    tools/bench_compare.py --new-cmake=-DBAKER_PATH_ROWS=ON  # a switch on the new side

The base commit is checked out into a git worktree and built beside a build of the working
tree, both Release with BAKER_BUILD_BENCH=ON. Each chosen benchmark then runs N times,
alternating base and new so that a change in the machine's load falls on both. Every output
line that reads as "<name> <number> <unit>" is a row; the report gives each row's median
and range on each side, the change of the medians, and marks the rows whose ranges do not
overlap, which is the least a change must do to be read as more than noise (CLAUDE.md,
"Benchmarks"). The table is Markdown, for a D-entry.

Build directories go under build-compare/ (ignored by git): base-src/ is the worktree,
base/ and new/ the builds. --keep leaves the worktree for a second comparison.
"""

import argparse
import os
import re
import statistics
import subprocess
import sys

ROW = re.compile(r"^(?P<name>\S.*?)\s+(?P<value>-?\d+(?:\.\d+)?)\s*(?P<unit>ns|us|ms|s|MB/s)\b")
BENCHES = ["parse", "cache", "lookup", "call", "layers", "layers_shape", "tasks"]
CMAKE_FLAGS = [
    "-G",
    "Ninja",
    "-DCMAKE_BUILD_TYPE=Release",
    "-DBAKER_BUILD_BENCH=ON",
    "-DBAKER_BUILD_TESTS=OFF",
    "-DBAKER_BUILD_EXAMPLES=OFF",
    "-DBAKER_BUILD_TOOLS=OFF",
]


def run(cmd, **kw):
    return subprocess.run(cmd, check=True, text=True, capture_output=True, **kw).stdout


def build(src, out, benches, jobs, flags=()):
    run(["cmake", "-S", src, "-B", out, *CMAKE_FLAGS, *flags])
    targets = [f"baker_bench_{b}" for b in benches]
    cmd = ["cmake", "--build", out, "--target", *targets]
    if jobs:
        cmd += ["--", f"-j{jobs}"]
    run(cmd)


def rows_of(text):
    rows = {}
    for line in text.splitlines():
        m = ROW.match(line.strip())
        if m:
            name = re.sub(r"\s+", " ", m.group("name")).rstrip(":")
            rows.setdefault((name, m.group("unit")), []).append(float(m.group("value")))
    return rows


def measure(exe, pin):
    cmd = ["taskset", "-c", str(pin), exe] if pin is not None else [exe]
    return rows_of(run(cmd))


def fmt(v):
    return f"{v:.3g}" if v < 10 else f"{v:.1f}" if v < 1000 else f"{v:.0f}"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--base", default="main", help="commit to compare against (default: main)")
    ap.add_argument("--bench", nargs="+", default=BENCHES, choices=BENCHES, help="benchmarks to run")
    ap.add_argument("--runs", type=int, default=3, help="runs of each benchmark per side (default: 3)")
    ap.add_argument("--pin", type=int, help="CPU to pin the benchmarks to (taskset)")
    ap.add_argument("--jobs", type=int, help="build jobs")
    ap.add_argument("--dir", default="build-compare", help="where the worktree and builds go")
    ap.add_argument("--keep", action="store_true", help="keep the base worktree afterwards")
    ap.add_argument("--new-cmake", action="append", default=[],
                    help="a CMake flag for the working tree's build only, as --new-cmake=-DX=ON")
    args = ap.parse_args()

    repo = run(["git", "rev-parse", "--show-toplevel"]).strip()
    os.chdir(repo)
    base_sha = run(["git", "rev-parse", "--short", args.base]).strip()
    root = os.path.abspath(args.dir)
    base_src, base_out, new_out = (os.path.join(root, d) for d in ("base-src", "base", "new"))
    os.makedirs(root, exist_ok=True)

    if os.path.isdir(base_src):
        run(["git", "-C", base_src, "checkout", "--detach", "-q", args.base])
    else:
        run(["git", "worktree", "add", "--detach", base_src, args.base])
    try:
        print(f"building {args.base} ({base_sha}) and the working tree ...", file=sys.stderr)
        build(base_src, base_out, args.bench, args.jobs)
        build(repo, new_out, args.bench, args.jobs, args.new_cmake)

        samples = {}  # (bench, name, unit) -> ([base...], [new...])
        for bench in args.bench:
            exe = f"baker_bench_{bench}"
            for i in range(args.runs):
                print(f"{exe}: run {i + 1} of {args.runs}", file=sys.stderr)
                for side, out in ((0, base_out), (1, new_out)):
                    for key, values in measure(os.path.join(out, exe), args.pin).items():
                        samples.setdefault((bench, *key), ([], []))[side].extend(values)

        print(f"| benchmark | row | {args.base} ({base_sha}) | working tree | change |")
        print("|---|---|---:|---:|---:|")
        for (bench, name, unit), (a, b) in samples.items():
            if not a or not b:
                continue
            ma, mb = statistics.median(a), statistics.median(b)
            change = (mb - ma) / ma * 100 if ma else 0.0
            apart = max(a) < min(b) or max(b) < min(a)
            mark = " **" if apart else ""
            print(
                f"| {bench} | {name} | {fmt(ma)} {unit} ({fmt(min(a))}–{fmt(max(a))}) "
                f"| {fmt(mb)} {unit} ({fmt(min(b))}–{fmt(max(b))}) | {change:+.1f}%{mark} |"
            )
        print()
        print("Medians with (min–max) over", args.runs, "runs a side; ** marks ranges that do not overlap.")
    finally:
        if not args.keep:
            run(["git", "worktree", "remove", "--force", base_src])


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as e:
        sys.stderr.write(e.stderr or str(e))
        sys.exit(1)
