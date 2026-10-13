#!/usr/bin/env python3
"""Sends Moira commands to a running stack game's remote console and prints the replies.

    python3 stacks/tools/console.py "{cube.angle}" "{cube.reset}"
    python3 stacks/tools/console.py --port 4555 < commands.dta     # one command per line

The console listens on 127.0.0.1 only (stacks/README.md). Each reply is "=> value" or the
error with its script stack. Exits 1 if any reply is an error, 2 if no game is listening.
"""
import argparse
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


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("commands", nargs="*", help="commands; none: read lines from stdin")
    parser.add_argument("--port", type=int, default=4555)
    args = parser.parse_args()
    lines = args.commands or [l.strip() for l in sys.stdin if l.strip() and not l.startswith(";")]
    try:
        sock = socket.create_connection(("127.0.0.1", args.port), timeout=10)
    except OSError as e:
        print(f"no game listening on 127.0.0.1:{args.port}: {e}", file=sys.stderr)
        return 2
    failed = False
    with sock:
        for line in lines:
            reply = ask(sock, line)
            print(f"{line}\n{reply}")
            failed |= not reply.startswith("=>")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
