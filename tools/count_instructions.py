#!/usr/bin/env python3
"""Count the instructions of Baker's hot operations on two commits, under cachegrind.

    tools/count_instructions.py                    # the working tree against main
    tools/count_instructions.py --base HEAD~1      # against the previous commit
    tools/count_instructions.py --case parse set --n 2000

Instruction counts are deterministic: the same binary and input give the same count on any
run, on a busy machine too, so a change of a fraction of a percent is real where a wall-clock
benchmark would call it noise. Each side builds tools/count (a driver of fixed workloads)
against its own tree, Release; the base is checked out into a git worktree. Each case runs
under `valgrind --tool=cachegrind` with N and 2N operations, and the difference over N is the
instructions of one operation, with setup and startup cancelled. The table is Markdown, for a
D-entry. It is a local tool: counts move with the compiler, so they compare two builds on one
machine, never a committed baseline.

Build directories go under build-count/ (ignored by git): base-src/ is the worktree, base/ and
new/ the builds. --keep leaves the worktree for a second comparison.
"""

import argparse
import os
import re
import subprocess
import sys

CASES = {"parse": 200, "parse_meta": 200, "lookup": 20000, "lookup_text": 20000, "set": 20000,
         "frame": 2000}
REFS = re.compile(r"I\s+refs:\s+([\d,]+)")


def run(cmd, **kw):
    return subprocess.run(cmd, check=True, text=True, capture_output=True, **kw)


def build(baker_root, out, driver_src, jobs):
    run(["cmake", "-S", driver_src, "-B", out, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
         f"-DBAKER_ROOT={baker_root}"])
    cmd = ["cmake", "--build", out, "--target", "baker_count"]
    if jobs:
        cmd += ["--", f"-j{jobs}"]
    run(cmd)


def instructions(exe, case, n):
    p = subprocess.run(["valgrind", "--tool=cachegrind", "--cache-sim=no",
                        "--cachegrind-out-file=/dev/null", exe, case, str(n)],
                       text=True, capture_output=True)
    if p.returncode != 0:
        raise SystemExit(f"{exe} {case} {n} failed:\n{p.stderr}")
    m = REFS.search(p.stderr)
    return int(m.group(1).replace(",", ""))


def per_op(exe, case, n):
    return (instructions(exe, case, 2 * n) - instructions(exe, case, n)) / n


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--base", default="main", help="commit to compare against (default: main)")
    ap.add_argument("--case", nargs="+", default=list(CASES), choices=list(CASES))
    ap.add_argument("--n", type=int, help="operations per run (default: per case)")
    ap.add_argument("--jobs", type=int, help="build jobs")
    ap.add_argument("--dir", default="build-count", help="where the worktree and builds go")
    ap.add_argument("--keep", action="store_true", help="keep the base worktree afterwards")
    args = ap.parse_args()

    repo = run(["git", "rev-parse", "--show-toplevel"]).stdout.strip()
    os.chdir(repo)
    base_sha = run(["git", "rev-parse", "--short", args.base]).stdout.strip()
    root = os.path.abspath(args.dir)
    base_src, base_out, new_out = (os.path.join(root, d) for d in ("base-src", "base", "new"))
    driver = os.path.join(repo, "tools", "count")  # the working tree's driver, on both sides
    os.makedirs(root, exist_ok=True)
    if os.path.isdir(base_src):
        run(["git", "-C", base_src, "checkout", "--detach", "-q", args.base])
    else:
        run(["git", "worktree", "add", "--detach", base_src, args.base])
    try:
        print(f"building {args.base} ({base_sha}) and the working tree ...", file=sys.stderr)
        build(base_src, base_out, driver, args.jobs)
        build(repo, new_out, driver, args.jobs)
        print(f"| case | {args.base} ({base_sha}) | working tree | change |")
        print("|---|---:|---:|---:|")
        for case in args.case:
            n = args.n or CASES[case]
            print(f"counting {case} ...", file=sys.stderr)
            a = per_op(os.path.join(base_out, "baker_count"), case, n)
            b = per_op(os.path.join(new_out, "baker_count"), case, n)
            change = (b - a) / a * 100 if a else 0.0
            print(f"| {case} | {a:,.1f} | {b:,.1f} | {change:+.2f}% |")
        print()
        print("Instructions per operation (cachegrind, Release); runs of N and 2N, differenced.")
    finally:
        if not args.keep:
            run(["git", "worktree", "remove", "--force", base_src])


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as e:
        sys.stderr.write(e.stderr or str(e))
        sys.exit(1)
