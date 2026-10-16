#!/usr/bin/env python3
"""Tests the agent commands' machine-readable output against a copy of a stack's data.

    python3 -I stacks/tools/test_commands.py build-stacks/bin/stacks_duel stacks/duel

CTest runs it for Duel (stacks/duel/CMakeLists.txt). Each case writes data/agent.dta in a
fresh copy of the stack, so the stack's own files are never touched. Exits 1 on a failure.
"""
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

FAILURES = []


def expect(cond, what):
    if not cond:
        FAILURES.append(what)
        print(f"FAIL: {what}")


class Stack:
    def __init__(self, game, source):
        self.game = str(Path(game).resolve())
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name) / "stack"
        shutil.copytree(source, self.dir, ignore=shutil.ignore_patterns("*.cpp", "*.hpp"))

    def run(self, *args, agent=None):
        if agent is not None:
            (self.dir / "data" / "agent.dta").write_text(agent)
        paths = ["--data", "data", "--sim", "sim", "--metrics", "metrics"]
        p = subprocess.run([self.game, args[0], *paths, *args[1:]], cwd=self.dir,
                           capture_output=True, text=True, timeout=600)
        return p.returncode, p.stdout, p.stderr


def check_gate(s):
    def check(agent):
        code, out, _ = s.run("check", agent=agent)
        return code, json.loads(out)

    code, r = check("")
    expect(code == 0 and r["passed"] and r["schema"] == "baker.check/1.1", "clean data passes")

    code, r = check("#autorun {spawnn 1}\n")
    expect(code == 1 and not r["passed"], "a failing #autorun command fails the check")
    expect([e["line"] for e in r["load"]] == [1] and "spawnn" in r["load"][0]["message"],
           "a failing #autorun command is in load, located")

    code, r = check("#autorun {func helper () {nosuch 1}}\n")
    expect(code == 1, "an undefined call inside an #autorun function fails the check")
    expect([d["name"] for d in r["demand"] if not d["stub"]] == ["nosuch"],
           "an undefined call inside an #autorun function is demand")

    code, r = check("(fighters (kestrel (width 9.5)))\n")
    expect(code == 1 and len(r["shape_errors"]) == 1 and r["shape_errors"][0]["line"] == 1,
           "a value out of range is a shape error in the JSON")

    code, r = check("(fighters (broken\n")
    expect(code == 2 and not r["passed"] and len(r["load"]) == 1,
           "a parse error exits 2 with the error in the JSON")
    s.run("check", agent="")


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    s = Stack(sys.argv[1], sys.argv[2])
    check_gate(s)
    print(f"{len(FAILURES)} failures" if FAILURES else "all passed")
    return 1 if FAILURES else 0


if __name__ == "__main__":
    sys.exit(main())
