#!/usr/bin/env python3
"""Spell metadata with the sigil: (range 0 10) after a value becomes (@range 0 10).

    tools/migrate_dta.py stacks/duel/data/default.dta          # print the rewritten text
    tools/migrate_dta.py --write $(git ls-files '*.dta')       # rewrite in place
    tools/migrate_dta.py --report $(git ls-files '*.dta')      # what would change, and the review list
    tools/migrate_dta.py --write --cpp tests/data/test_config.cpp   # DTA inside C++ string literals
    tools/migrate_dta.py --write --md docs/language.md              # DTA inside Markdown

The at-metadata rule (dev/syntax-at-metadata-design.md): a tagged child whose tag begins
with '@' is metadata, never a value and never a field. Three rewrites, over a frozen copy of
the lexer's rules (tokens and brackets by byte span; nothing is parsed, expanded or
reformatted, so comments and layout stay as they are):

1. In every tagged array with at least one untagged value, today's rule (D218) names the
   metadata: the trailing run of children tagged range, step, desc, note, kind, group or
   one_of. Each gets the sigil: (hit_pause 6 (range 0 30)) -> (hit_pause 6 (@range 0 30)).
2. Inside (shapes ...): a shape's (at ...) and (keyed) become (@at ...) and (@keyed); a
   field's children after its type that are tagged range, step, kind, desc, note, optional or
   one_of get the sigil. (list T) and * are a type and a wildcard and stay.
3. In {func name (params) ...} and {override ...}: the (status ...) and (doc ...) forms
   between the parameters and the body get the sigil.

What it will not decide, it reports: a tagged array with no untagged value whose last child
is tagged with a metadata key, (ball (kind ball) (pos 1 2)) or (limits (min 0) (step 0.5)),
is D218's ambiguity. Today it reads as all value; under the rule it still does, so nothing
is renamed, and a person decides whether (step 0.5) was meant as metadata.

--cpp reads a C++ file as its string literals' text: a '"' is a literal's edge (whitespace),
'\\"' is a DTA string's quote, and '\\n' is a newline. --md does the same for a Markdown
file, so the fenced blocks and inline code in the documentation migrate alike. Both are
best effort over text that is not all DTA; read the diff.
"""

import argparse
import sys

META_KEYS = ("range", "step", "desc", "note", "kind", "group", "one_of")
FIELD_KEYS = META_KEYS + ("optional",)
SHAPE_KEYS = ("at", "keyed")
FUNC_KEYS = ("status", "doc")
ATOM_END = set(chr(c) for c in range(0x21)) | set('(){}[]";')


class Tok:
    __slots__ = ("kind", "text", "begin", "end", "line", "children", "parent", "closed")

    def __init__(self, kind, text, begin, end, line):
        self.kind = kind  # 'open', 'close', 'atom', 'string', 'quoted'
        self.text = text
        self.begin = begin
        self.end = end
        self.line = line
        self.children = []  # for 'open': the tokens and brackets directly inside
        self.parent = None
        self.closed = False


def lex(src, mode):
    """The lexer's token rules, frozen: whitespace, ; and /* */ comments, "strings",
    'quoted symbols', atoms ending at the six brackets, a quote, ';' or a control character.
    In --cpp and --md mode a bare '"' is whitespace and '\\"' a string's quote."""
    toks = []
    i, n, line = 0, len(src), 1
    cpp = mode in ("cpp", "md")
    while i < n:
        c = src[i]
        if c == "\n":
            line += 1
            i += 1
        elif c <= " ":
            i += 1
        elif cpp and c == "\\" and i + 1 < n and src[i + 1] in "nt":  # a C++ escape: whitespace
            i += 2
        elif cpp and c == '"':  # a literal's edge
            i += 1
        elif c == ";":
            end = src.find("\n", i)
            i = n if end < 0 else end
        elif c == "/" and i + 1 < n and src[i + 1] == "*":
            end = src.find("*/", i + 2)
            seg = src[i : n if end < 0 else end + 2]
            line += seg.count("\n")
            i += len(seg)
        elif c == '"' or (cpp and c == "\\" and i + 1 < n and src[i + 1] == '"'):
            begin = i
            i += 2 if cpp else 1
            while True:
                end = src.find('\\"' if cpp else '"', i)
                if end < 0:
                    end = n
                    break
                if cpp and src[end - 1] == "\\" and src[max(0, end - 2) : end] != "\\\\":
                    i = end + 2  # an escaped quote inside the string: \\\" in the C++
                    continue
                break
            stop = min(n, end + (2 if cpp else 1))
            line += src.count("\n", begin, stop)
            toks.append(Tok("string", src[begin:stop], begin, stop, line))
            i = stop
        elif c == "'":
            end = src.find("'", i + 1)
            stop = n if end < 0 else end + 1
            toks.append(Tok("quoted", src[i + 1 : stop - 1], i, stop, line))
            line += src.count("\n", i, stop)
            i = stop
        elif c in "({[":
            toks.append(Tok("open", c, i, i + 1, line))
            i += 1
        elif c in ")}]":
            toks.append(Tok("close", c, i, i + 1, line))
            i += 1
        else:
            begin = i
            while i < n and src[i] not in ATOM_END and not (cpp and src[i] == "\\"):
                i += 1
            toks.append(Tok("atom", src[begin:i], begin, i, line))
    return toks


def bracket_tree(toks):
    """Nests the tokens: each 'open' holds its direct children; a close of the wrong kind
    closes the innermost (read_structure's tolerance); an unclosed bracket runs to the end."""
    root = Tok("open", "", 0, 0, 0)
    stack = [root]
    for t in toks:
        if t.kind == "open":
            t.parent = stack[-1]
            stack[-1].children.append(t)
            stack.append(t)
        elif t.kind == "close":
            if len(stack) > 1:
                stack[-1].closed = True
                stack[-1].end = t.end
                stack.pop()
        else:
            t.parent = stack[-1]
            stack[-1].children.append(t)
    return root


def tag_of(t):
    """The name an array is tagged with: its first child when that is a symbol."""
    if t.kind != "open" or t.text != "(" or not t.children:
        return None
    first = t.children[0]
    if first.kind == "atom" and not first.text[0].isdigit() and first.text[0] not in "-.$#":
        return first.text
    if first.kind == "quoted":
        return first.text
    return None


def is_value(t):
    """An untagged child: an atom, a string, a variable, {command}, [property], or an array
    with no symbol tag. The tag of a `(` array is in slot 0 and is never a value."""
    return not (t.kind == "open" and t.text == "(" and tag_of(t) is not None)


def plain_tag(t, keys):
    """The key `t` is tagged with, without a sigil, if it is one of `keys`."""
    tag = tag_of(t)
    return tag if tag in keys else None


class Migration:
    def __init__(self, src, name):
        self.src = src
        self.name = name
        self.renames = []  # (byte offset to insert '@' at, key)
        self.review = []  # (line, text): D218's ambiguity, for a person
        self.done = set()  # arrays a shape or func rule handled

    def rename(self, t, key):
        first = t.children[0]
        at = first.begin + (1 if first.kind == "quoted" else 0)
        self.renames.append((at, key))
        self.done.add(id(t))

    def walk(self, t, in_shapes=False):
        if t.kind != "open":
            return
        if t.text == "(" and tag_of(t) == "shapes" and t.parent is not None and t.parent.text == "":
            for decl in t.children[1:]:
                self.shape(decl)
            return
        if t.text == "{" and len(t.children) >= 3 and t.children[0].kind == "atom" and \
                t.children[0].text in ("func", "override"):
            for form in t.children[3:]:
                key = plain_tag(form, FUNC_KEYS)
                if key is None:
                    break
                self.rename(form, key)
        if t.text == "(" and tag_of(t) is not None and id(t) not in self.done:
            self.d218(t)
        for c in t.children:
            self.walk(c)

    def d218(self, t):
        kids = t.children[1:]
        has_value = any(is_value(k) for k in kids)
        trailing = []
        for k in reversed(kids):
            key = plain_tag(k, META_KEYS)
            if key is None:
                break
            trailing.append((k, key))
        if not trailing:
            return
        if has_value:
            for k, key in trailing:
                self.rename(k, key)
        else:
            text = self.src[t.begin : min(t.end, t.begin + 72)].replace("\n", " ")
            self.review.append((t.line, text + ("..." if t.end - t.begin > 72 else "")))

    def shape(self, decl):
        if tag_of(decl) is None:
            return
        self.done.add(id(decl))
        for child in decl.children[1:]:
            key = plain_tag(child, SHAPE_KEYS)
            if key is not None:
                self.rename(child, key)
                continue
            if tag_of(child) is None:
                continue
            self.done.add(id(child))  # a field: (name type metadata...)
            for meta in child.children[2:]:
                key = plain_tag(meta, FIELD_KEYS)
                if key is not None:
                    self.rename(meta, key)

    def apply(self):
        out = self.src
        for at, _ in sorted(self.renames, reverse=True):
            out = out[:at] + "@" + out[at:]
        return out


def migrate(src, name, mode="dta"):
    m = Migration(src, name)
    m.walk(bracket_tree(lex(src, mode)))
    return m


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("files", nargs="+")
    ap.add_argument("--write", action="store_true", help="rewrite each file in place")
    ap.add_argument("--report", action="store_true",
                    help="per file: the renames by key, and the review list; no text")
    ap.add_argument("--cpp", action="store_true", help="the files are C++ with DTA in literals")
    ap.add_argument("--md", action="store_true", help="the files are Markdown with DTA in them")
    args = ap.parse_args()
    mode = "cpp" if args.cpp else "md" if args.md else "dta"
    total = {}
    review_count = 0
    for path in args.files:
        with open(path, encoding="utf-8", newline="") as f:
            src = f.read()
        m = migrate(src, path, mode)
        out = m.apply()
        by_key = {}
        for _, key in m.renames:
            by_key[key] = by_key.get(key, 0) + 1
            total[key] = total.get(key, 0) + 1
        review_count += len(m.review)
        if args.report:
            if m.renames or m.review:
                keys = ", ".join(f"{k} {n}" for k, n in sorted(by_key.items()))
                print(f"{path}: {len(m.renames)} renames ({keys}); "
                      f"{len(out.encode()) - len(src.encode()):+d} bytes")
            for line, text in m.review:
                print(f"  review {path}:{line}: {text}")
        elif args.write:
            if out != src:
                with open(path, "w", encoding="utf-8", newline="") as f:
                    f.write(out)
            for line, text in m.review:
                print(f"review {path}:{line}: {text}", file=sys.stderr)
        else:
            sys.stdout.write(out)
            for line, text in m.review:
                print(f"review {path}:{line}: {text}", file=sys.stderr)
    if args.report:
        n = sum(total.values())
        keys = ", ".join(f"{k} {v}" for k, v in sorted(total.items()))
        print(f"total: {n} renames ({keys}); {review_count} to review")


if __name__ == "__main__":
    main()
