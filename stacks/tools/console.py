#!/usr/bin/env python3
"""Sends Baker commands to a running stack game's remote console and prints the replies.

    python3 stacks/tools/console.py "{cube.angle}" "{cube.reset}"
    python3 stacks/tools/console.py --port 4555 < commands.dta     # one command per line
    python3 stacks/tools/console.py --stack stacks/duel "{duel.frame_table}"  # its port
    python3 stacks/tools/console.py --json "{config.explain cube.speed}"

The console listens on 127.0.0.1 only (stacks/README.md). Each command is sent as a request,
(N {command}), and answered with one line of JSON (Baker's docs/reply-json.md). This prints
"=> value", or the error with its script stack; --json prints the reply as it came. Exits 1
if any reply is an error, 2 if no game is listening.
"""
import argparse
import re
import json
import socket
import sys
from pathlib import Path


def ask(sock, line):
    sock.sendall((line + "\n").encode())
    reply = b""
    while not reply.endswith(b"\n\n"):
        chunk = sock.recv(4096)
        if not chunk:
            break
        reply += chunk
    return reply.decode(errors="replace").rstrip("\n")


def describe(error):
    out = f"baker: {error['message']}"
    if error["file"]:
        out += f"  ({error['file']}:{error['line']})"
    for frame in error["stack"]:
        on = f"{frame['on']} " if frame["on"] else ""
        out += f"\n  in {{{on}{frame['function']}}}  ({frame['file']}:{frame['line']})"
    return out


def stack_port(stack):
    """The port a stack's baker-project.dta names, (console PORT); 4555 if it names none."""
    text = (Path(stack) / "baker-project.dta").read_text(encoding="utf-8")
    m = re.search(r"\(console\s+(\d+)\)", text)
    return int(m.group(1)) if m else 4555


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("commands", nargs="*", help="commands; none: read lines from stdin")
    parser.add_argument("--port", type=int, help="the console's port (default 4555)")
    parser.add_argument("--stack", help="a stack's directory: its project file's (console PORT)")
    parser.add_argument("--json", action="store_true", help="print each reply's JSON line")
    args = parser.parse_args()
    if args.port is None:
        args.port = stack_port(args.stack) if args.stack else 4555
    lines = args.commands or [l.strip() for l in sys.stdin if l.strip() and not l.startswith(";")]
    try:
        sock = socket.create_connection(("127.0.0.1", args.port), timeout=10)
    except OSError as e:
        print(f"no game listening on 127.0.0.1:{args.port}: {e}", file=sys.stderr)
        return 2
    failed = False
    with sock:
        for n, line in enumerate(lines, 1):
            raw = ask(sock, f"({n} {line})")
            try:
                reply = json.loads(raw)
            except ValueError:
                reply = {"ok": False, "error": {"message": f"not a reply: {raw}", "file": "",
                                                "line": 0, "stack": []}}
            failed |= not reply["ok"]
            if args.json:
                print(raw)
            else:
                print(f"{line}\n" + (f"=> {reply['text']}" if reply["ok"]
                                     else describe(reply["error"])))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
