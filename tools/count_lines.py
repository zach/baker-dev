#!/usr/bin/env python3
"""Counts non-blank, non-comment C++ lines per library against the soft budget.

See docs/decisions.md D2 and D40. Informational; always exits 0. Understands //, /* */, string,
character, and raw-string literals, so comment markers inside literals are not comments.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SCRIPT_HEADERS = ("script", "object", "task")
BUDGET = 15000  # docs/decisions.md D2; raised from the spec's 4,000 by D40, D48, then D69


def code_lines(text: str) -> int:
    lines = set()
    i, n, line = 0, len(text), 1
    while i < n:
        c = text[i]
        if c == "\n":
            line += 1
            i += 1
        elif text.startswith("//", i):
            while i < n and text[i] != "\n":
                i += 1
        elif text.startswith("/*", i):
            end = text.find("*/", i + 2)
            end = n if end < 0 else end + 2
            line += text.count("\n", i, end)
            i = end
        elif c == "R" and text.startswith('R"', i):
            m = re.match(r'R"([^(\s]*)\(', text[i:])
            delim = ")" + (m.group(1) if m else "") + '"'
            end = text.find(delim, i + 2)
            end = n if end < 0 else end + len(delim)
            for k in range(text.count("\n", i, end) + 1):
                lines.add(line + k)
            line += text.count("\n", i, end)
            i = end
        elif c in "\"'":
            lines.add(line)
            i += 1
            while i < n and text[i] != c and text[i] != "\n":
                i += 2 if text[i] == "\\" else 1
            i += 1
        else:
            if not c.isspace():
                lines.add(line)
            i += 1
    return len(lines)


def count(paths):
    return sum(code_lines(p.read_text(encoding="utf-8")) for p in paths)


def sources(directory, keep):
    d = ROOT / directory
    return sorted(p for p in d.rglob("*") if p.suffix in (".hpp", ".cpp") and keep(p))


def is_script_header(p):
    return p.name.removeprefix("std_").startswith(SCRIPT_HEADERS)


def is_shape_header(p):
    return p.name.startswith("shape")


data = count(sources("src/data", lambda p: True) +
             sources("include/baker",
                     lambda p: not is_script_header(p) and not is_shape_header(p)))
shape = count(sources("src/shape", lambda p: True) + sources("include/baker", is_shape_header))
script = count(sources("src/script", lambda p: True) +
               sources("include/baker", is_script_header))
print(f"baker::data   {data}")
print(f"baker::script {script}")
print(f"core total    {data + script} / {BUDGET} (soft budget)")
print(f"baker::shape  {shape} / 600 (spec §6, outside the core)")
sys.exit(0)
