#!/usr/bin/env python3
"""Sends Baker commands to a running stack game's remote console and prints the replies.

    python3 stacks/tools/console.py "{cube.angle}" "{cube.reset}"
    python3 stacks/tools/console.py --port 4555 < commands.dta     # one command per line
    python3 stacks/tools/console.py --json "{config.explain cube.speed}"

The console listens on 127.0.0.1 only (stacks/README.md). Each command is sent as a request,
(N {command}), and answered with one line of JSON (Baker's docs/reply-json.md). This prints
"=> value", or the error with its script stack; --json prints the reply as it came. Exits 1
if any reply is an error, 2 if no game is listening.
"""
import argparse
import json
import socket
import sys


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


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("commands", nargs="*", help="commands; none: read lines from stdin")
    parser.add_argument("--port", type=int, default=4555)
    parser.add_argument("--json", action="store_true", help="print each reply's JSON line")
    args = parser.parse_args()
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
