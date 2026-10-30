#!/usr/bin/env python3
"""Tests the agent commands' machine-readable output against a copy of a stack's data.

    python3 -I stacks/tools/test_commands.py build-stacks/bin/stacks_duel stacks/duel

CTest runs it for Duel (stacks/duel/CMakeLists.txt). Each case writes data/agent.dta in a
fresh copy of the stack, so the stack's own files are never touched. Exits 1 on a failure.
"""
import json
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time
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
        p = subprocess.run([self.game, args[0], "--data", "data", *args[1:]], cwd=self.dir,
                           capture_output=True, text=True, timeout=600)
        return p.returncode, p.stdout, p.stderr


def check_gate(s):
    def check(agent):
        code, out, _ = s.run("check", agent=agent)
        return code, json.loads(out)

    code, r = check("")
    expect(code == 0 and r["passed"] and r["schema"] == "baker.check/1.5", "clean data passes")

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


def eval_json(s):
    code, out, _ = s.run("eval", "--baker-json", "{config.explain fighters.kestrel.width}",
                         "{nosuch}", agent="")
    lines = [json.loads(l) for l in out.splitlines()]
    expect(code == 1 and len(lines) == 2, "eval --baker-json: one reply a command; exit 1 on an error")
    first = lines[0]
    expect(first["schema"] == "baker.reply/1.2" and first["id"] == 1 and first["ok"],
           "eval --baker-json: the reply form, with its id")
    expect(first["value"][-1]["standing"] == "wins" and first["value"][-1]["layer"] == "default",
           "eval --baker-json: explain's layer chain as JSON")
    expect(not lines[1]["ok"] and lines[1]["id"] == 2 and lines[1]["error"]["message"],
           "eval --baker-json: an error reply")
    code, out, _ = s.run("eval", "--baker-json", "{+ 1 2}")
    expect(code == 0 and json.loads(out)["value"] == 3, "eval --baker-json: exit 0 when all ran")


def sim_json(s):
    def sim(out, agent):
        code, text, _ = s.run("sim", "--baker-seeds", "1-1", "--baker-results", out, "--baker-json", agent=agent)
        return code, text

    code, first = sim("before", "")
    r = json.loads(first)
    expect(code == 0 and r["schema"] == "baker.sim/1.3" and r["status"] == "passed",
           "sim --baker-json: the results, passed")
    expect((s.dir / "before" / "results.json").read_text() == first,
           "sim --baker-results writes the same JSON to results.json")
    expect(sim("again", "")[1] == first, "sim --baker-json: the same data gives the same bytes")
    keys = [(run["scenario"], run["seed"]) for run in r["runs"]]
    expect(keys == sorted(keys), "sim --baker-json: runs sorted by scenario, then seed")
    expect(all(run["file"].startswith("sim/") and run["line"] > 0 for run in r["runs"]),
           "sim --baker-json: each run located in the stack's sim/")

    code, out, _ = s.run("compare", "--baker-before", "before", "--baker-after", "again")
    expect(code == 0 and json.loads(out)["broke"] == 0, "compare: identical runs move nothing")

    sim("after", "(fighters (kestrel (health 50)))\n")
    code, out, _ = s.run("compare", "--baker-before", "before", "--baker-after", "after")
    c = json.loads(out)
    broke = [o["name"] for o in c["objectives"] if o["moved"] == "broke"]
    expect(code == 1 and c["schema"] == "baker.compare/1" and c["broke"] == len(broke) > 0,
           "compare: a weaker fighter breaks objectives, and exits 1")
    first_broken = next(o for o in c["objectives"] if o["moved"] == "broke")
    expect(first_broken["changed"] and first_broken["changed"][0]["before"]
           and not first_broken["changed"][0]["after"], "compare: names the run that broke")
    expect(first_broken["file"].startswith("metrics/") and first_broken["line"] > 0,
           "compare: the objective that broke, located")
    s.run("check", agent="")


def promote_patch(s):
    agent = "(fighters (kestrel (health 1100) (walk_back 1.7)))\n(cpu (easy (reaction 26)))\n"
    data = s.dir / "data"
    (data / "agent.dta").write_text(agent)
    before = {p.name: p.read_bytes() for p in data.iterdir()}
    code, _, _ = s.run("promote", "--baker-patch", str(s.dir / "p.diff"))
    after = {p.name: p.read_bytes() for p in data.iterdir()}
    expect(code == 0 and before == after, "promote --baker-patch writes nothing in data/")
    patch = (s.dir / "p.diff").read_text()
    expect(patch.startswith("--- a/default.dta\n+++ b/default.dta\n@@ "),
           "promote --baker-patch: a unified diff, relative to the data directory")
    # git apply takes the bytes as they are, CRLF lines included (a Windows checkout's); the
    # patch tool some Windows runners carry (2.5.9) cannot, so it is only the fallback.
    git, patch_tool = shutil.which("git"), shutil.which("patch")
    if git or patch_tool:
        applied = Path(s.tmp.name) / "applied"
        shutil.copytree(s.dir, applied)
        if git:
            apply = [git, "apply", "-p1", "--whitespace=nowarn", str(s.dir / "p.diff")]
        else:
            apply = [patch_tool, "-p1", "-s", "-i", str(s.dir / "p.diff")]
        subprocess.run(apply, cwd=applied / "data", check=True)
        code, _, _ = s.run("promote", "--baker-write")
        expect(code == 0 and (applied / "data" / "default.dta").read_bytes() ==
               (data / "default.dta").read_bytes(),
               "promote --baker-patch applies to exactly what --baker-write writes")
    else:
        print("skipped: neither git nor patch to apply the diff")


def project_file(s):
    """The game opens the stack its baker-project.dta describes, which names the game."""
    project = s.dir / "baker-project.dta"
    original = project.read_text()
    # (tool "$build/bin/stacks_duel"): the binary's place under whatever build directory a
    # tool names, which is where the build put the game this test runs.
    tool = re.search(r'\(tool "\$build/([^"]+)"', original)
    game = Path(s.game)
    expect(tool is not None and
           game.with_suffix("").as_posix().endswith("/" + tool.group(1)),
           "the project names the game binary under $build")
    try:
        (s.dir / "data" / "experiment.dta").write_text("(fighters (kestrel (width 9.5)))\n")
        project.write_text(original.replace(
            '(agent "data/agent.dta" optional dev_only)',
            '(agent "data/agent.dta" optional dev_only)\n      (experiment "data/experiment.dta")'))
        code, out, _ = s.run("check", agent="")
        r = json.loads(out)
        expect(code == 1 and len(r["shape_errors"]) == 1 and
               r["shape_errors"][0]["file"].endswith("experiment.dta"),
               "a layer only the project file names is in the game's stack")
        project.write_text("(project (layers (default \"data/default.dta\")) (levels x))\n")
        code, out, _ = s.run("check", agent="")
        r = json.loads(out)
        expect(code == 2 and [e["file"] for e in r["load"]] == ["baker-project.dta"] and
               "no 'levels'" in r["load"][0]["message"],
               "a project file's mistake is a load problem, located, as data that does not load")
    finally:
        project.write_text(original)
        (s.dir / "data" / "experiment.dta").unlink(missing_ok=True)


def built_reference(s):
    """The build writes the game's reference where the stack's project file names it."""
    named = '(reference "build/reference.json")'
    expect(named in (s.dir / "baker-project.dta").read_text(),
           "the project file names the build's reference")
    written = s.dir / "build" / "reference.json"
    expect(written.is_file(), "the build wrote build/reference.json beside the project file")
    if not written.is_file():
        return
    code, out, _ = s.run("reference", "--baker-json", agent="")
    names = lambda r: [f["name"] for f in r["reference"]["functions"]]
    built = json.loads(written.read_text())
    expect(code == 0 and built["reference"]["schema"] == "baker.reference/1.5" and
           names(built) == names(json.loads(out)),
           "the built reference is the game's reference --baker-json")


def serve(s, *flags):
    """A headless game serving its console, and a connection to it (None if it never came)."""
    with socket.socket() as probe:  # a free port
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
    game = subprocess.Popen([s.game, "--data", "data", "--headless", "--console-port", str(port),
                             *flags], cwd=s.dir, stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)
    for _ in range(100):
        try:
            return game, socket.create_connection(("127.0.0.1", port), timeout=5)
        except OSError:
            time.sleep(0.1)
    return game, None


def console_write(s):
    """--console-write lets the console write a live edit to its line; without it, it cannot."""
    default = s.dir / "data" / "default.dta"
    original = default.read_text()
    (s.dir / "data" / "agent.dta").write_text("")
    try:
        for flags, allowed in (((), False), (("--console-write",), True)):
            game, conn = serve(s, *flags)
            try:
                expect(conn is not None, "the game's console accepts a connection")
                if conn is None:
                    continue
                with conn, conn.makefile("rwb") as f:
                    f.write(b"(1 {config.set fighters.kestrel.health 1234})\n"
                            b"(2 {config.write_back fighters.kestrel.health})\n")
                    f.flush()
                    replies = []  # one JSON line each; blank lines between them
                    while len(replies) < 2:
                        line = f.readline()
                        if not line:
                            break
                        if line.strip():
                            replies.append(json.loads(line))
                    first, second = (replies + [{}, {}])[:2]
                expect(first.get("ok"), "config.set answers on the console")
                written = default.read_text()
                if allowed:
                    expect(second.get("ok") and "(health 1234)" in written and
                           len(written.splitlines()) == len(original.splitlines()),
                           "--console-write: config.write_back writes the live value to its line")
                else:
                    expect(not second.get("ok") and written == original,
                           "without --console-write the console cannot write files")
            finally:
                game.terminate()
                game.wait(timeout=30)
    finally:
        default.write_text(original)


def headless_idle(s):
    """A headless game serving its console paces its frames: it idles, and still answers."""
    game, conn = serve(s)
    try:
        expect(conn is not None, "the headless game's console accepts a connection")
        if conn is None:
            return
        with conn, conn.makefile("rwb") as f:
            f.write(b"(1 {+ 1 2})\n")
            f.flush()
            reply = json.loads(f.readline())
            expect(reply.get("value") == 3, "the headless game answers a request")
        stat = Path(f"/proc/{game.pid}/stat")
        if not stat.exists():
            print("skipped: no /proc to measure the idle game's CPU")
            return
        def cpu():  # seconds of CPU so far: utime and stime, after the command's parentheses
            fields = stat.read_text().rsplit(")", 1)[1].split()
            return (int(fields[11]) + int(fields[12])) / os.sysconf("SC_CLK_TCK")
        before, t0 = cpu(), time.monotonic()
        time.sleep(3)
        share = (cpu() - before) / (time.monotonic() - t0)
        print(f"an idle headless game: {share:.0%} of a core")
        expect(share < 0.5, f"an idle headless game uses under half a core ({share:.0%})")
    finally:
        game.terminate()
        game.wait(timeout=30)


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    s = Stack(sys.argv[1], sys.argv[2])
    check_gate(s)
    eval_json(s)
    sim_json(s)
    project_file(s)
    built_reference(s)
    headless_idle(s)
    console_write(s)
    promote_patch(s)  # last: --baker-write changes the copy's default.dta
    print(f"{len(FAILURES)} failures" if FAILURES else "all passed")
    return 1 if FAILURES else 0


if __name__ == "__main__":
    sys.exit(main())
