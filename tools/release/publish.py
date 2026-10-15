#!/usr/bin/env python3
"""Builds and publishes a release of Baker's core library to github.com/zach/baker.

The release process is docs/releasing.md. In short:

  publish.py bump 0.2.0
      Sets the version in CMakeLists.txt, include/baker/version.hpp and
      src/data/version.cpp, and adds an entry to CHANGELOG.md to fill in. This is the
      release-preparation PR.

  publish.py prepare --version 0.2.0 --out build/public --notes build/notes.md
      Exports the committed tree at --ref (default HEAD) through tools/release/manifest.txt,
      strips the citations of baker-dev's own documents (decision numbers, spec sections),
      lays tools/release/public/ over it, and checks the result: the version everywhere
      matches, CHANGELOG.md has the entry, and nothing names a file the release leaves out.
      Writes the tree to --out and the changelog entry to --notes.

  publish.py check
      `prepare` at the version the tree declares, into a temporary directory, over the work
      tree's tracked files as they stand. CI and tools/local_matrix.sh run it, so that a
      change the release could not publish fails where it is made.

  publish.py commit --tree build/public --repo path/to/baker --version 0.2.0 --notes ...
      In a clone of zach/baker: replaces the work tree with --tree, commits it as one
      commit on main ("Baker 0.2.0", the notes as its body) and tags it v0.2.0. Refuses a
      version that is not above the newest tag. Pushing is the caller's.

Only the owner starts a release; an agent runs these only when asked to (CLAUDE.md).
"""
import argparse
import datetime
import fnmatch
import io
import pathlib
import re
import shutil
import subprocess
import sys
import tarfile

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
HERE = ROOT / "tools" / "release"
MANIFEST = HERE / "manifest.txt"
OVERLAY = HERE / "public"

SEMVER = re.compile(r"^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$")
TEXT_SUFFIXES = {".hpp", ".cpp", ".h", ".md", ".py", ".txt", ".cmake", ".yml", ".yaml", ".json"}
TEXT_NAMES = {"CMakeLists.txt", ".clang-format", ".clang-tidy", ".gitattributes", ".gitignore"}

# --- Citations of baker-dev's own documents, stripped from the published copy. ---
#
# A parenthetical whose items are all citations goes, with the space before it; one that
# mixes citations with words keeps the words. Citations anywhere else are left for check()
# to report, so that a sentence is reworded in baker-dev rather than mangled here.
_D = r"D\d{1,3}(?:\s*(?:–|-)\s*D\d{1,3})?(?:\s+Q\d+)?"
CITATION_ITEM = re.compile(
    r"^(?:"
    r"(?:docs/)?decisions\.md(?:\s+" + _D + r")?"  # docs/decisions.md D55
    r"|" + _D +  # D77, D90 Q3, D61–D64
    r"|(?:docs/)?spec(?:\.md)?\s+§\s*[\d.]+(?:\s*(?:,|and)\s*§\s*[\d.]+)*"  # spec §5, §10
    r"|(?:docs/)?(?:background/)?[a-z][\w-]*(?:\.md)?\s+§\s*[\d.]+"  # reading-a-studio §7
    r"|(?:docs/)?(?:background/[\w-]+|spec|allocation-review)\.md"  # a document of baker-dev's
    r"|gaps?\s+\d+\w*(?:\s*(?:,|and)\s*\d+\w*)*"  # gap 31, gaps 2m and 6
    r")$"
)
PAREN = re.compile(r" ?\(([^()\n]*)\)")
SPLIT = re.compile(r"\s*([,;])\s*")


def _strip_paren(m):
    inner = m.group(1)
    parts = SPLIT.split(inner)  # item, sep, item, sep, ...
    items = parts[0::2]
    seps = parts[1::2]
    if not any(CITATION_ITEM.match(i.strip()) for i in items):
        return m.group(0)
    kept = []
    for k, item in enumerate(items):
        if CITATION_ITEM.match(item.strip()):
            continue
        kept.append((seps[k - 1] if k > 0 else None, item.strip()))
    if not kept:
        return ""
    text = kept[0][1]
    for sep, item in kept[1:]:
        text += (sep or ",") + " " + item
    lead = " " if m.group(0).startswith(" ") else ""
    return lead + "(" + text + ")"


def strip_citations(text):
    return PAREN.sub(_strip_paren, text)


# Where a line's text starts, after its indent and any comment marker or list bullet.
PREFIX = re.compile(r"^\s*(?:(?://+|#|-|\*)\s+|(?://+|#)$)?")
# A stripped line that starts with punctuation, or is a comment marker with nothing after
# it, lost a citation in a way strip_file() does not mend: reword the source.
LEFTOVER = re.compile(r"^\s*(?:(?://|#|-|\*)\s*)?[.,;:)]|(?://|#)\s*$")
CODE_SUFFIXES = {".hpp", ".cpp", ".h"}
COLUMNS = 100  # .clang-format's ColumnLimit


def strip_file(name, text):
    """`text` with citations stripped, and the lines stripping left malformed.

    A citation that began a line hands the punctuation after it to the line before, and a
    line it leaves empty goes; a trailing comment that was only a citation goes too."""
    code = pathlib.PurePosixPath(name).suffix in CODE_SUFFIXES
    out, bad = [], []
    for n, line in enumerate(text.split("\n"), 1):
        stripped = strip_citations(line)
        if stripped == line:
            out.append(line)
            continue
        # The prefix as the source has it: stripping takes the space before a citation, and
        # so the one after a comment marker when the citation began the comment.
        pre = PREFIX.match(line).group(0)
        if stripped.startswith(pre):
            rest = stripped[len(pre):]
        else:
            rest = stripped[len(pre.rstrip()):].lstrip()
        began = line[len(pre):].startswith("(")
        if began and out and rest[:1] in (".", ",", ";", ":"):
            out[-1] = out[-1].rstrip() + rest[0]
            rest = rest[1:].lstrip()
            if code and len(out[-1]) > COLUMNS:
                bad.append(f"{name}:{n - 1}: is over {COLUMNS} columns once stripped")
        if began and not rest.strip():
            continue  # the line was only a citation
        stripped = pre + rest
        if re.search(r"\S\s*(?://|#)\s*$", stripped) and not re.search(r"(?://|#)\s*$", line):
            stripped = re.sub(r"\s*(?://|#)\s*$", "", stripped)  # a comment of only a citation
        if LEFTOVER.search(stripped) and not LEFTOVER.search(line):
            bad.append(f"{name}:{n}: a citation stripped badly here: {line.strip()}")
        out.append(stripped)
    return "\n".join(out), bad


# What a published file may not mention: baker-dev's process documents and what the
# release leaves out. Each pattern is reported with its file and line.
FORBIDDEN = [
    (re.compile(r"(?<![\w/.-])D\d{1,3}\b(?![\w.])"), "a decision number"),
    (re.compile(r"\bspec(?:\.md)?\s+§"), "a spec section"),
    (re.compile(r"\bdecisions\.md\b"), "docs/decisions.md"),
    (re.compile(r"\bdocs/spec\.md\b|\(spec\.md\)|\bspec\.md\b"), "docs/spec.md"),
    (re.compile(r"\bdocs/background\b|\bbackground/[\w-]+\.md"), "docs/background/"),
    (re.compile(r"\ballocation-review\b"), "docs/allocation-review.md"),
    (re.compile(r"\bCLAUDE\.md\b|\.claude/"), "the agents' files"),
    (re.compile(r"\bstacks/"), "stacks/"),
    (re.compile(r"\bbaker-dev\b"), "the development repository"),
    (re.compile(r"\bgaps?\s+\d+\b"), "a gap number"),
    (re.compile(r"\bm\d+-plan\.md\b|\blocal_matrix\.sh\b|\bcount_lines\.py\b"), "a dev tool or plan"),
]


def fail(msg):
    print(f"publish.py: {msg}", file=sys.stderr)
    sys.exit(1)


def git(*args, cwd=ROOT, capture=True):
    r = subprocess.run(["git", *args], cwd=cwd, capture_output=capture, text=False)
    if r.returncode != 0:
        err = r.stderr.decode(errors="replace") if r.stderr else ""
        fail(f"git {' '.join(args)} failed: {err.strip()}")
    return r.stdout


def read_manifest():
    rules = []
    for raw in MANIFEST.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        rules.append((line.startswith("!"), line.lstrip("!")))
    return rules


def glob_match(path, pattern):
    if pattern.endswith("/**"):
        return path.startswith(pattern[:-2])
    return fnmatch.fnmatchcase(path, pattern)


def selected(path, rules):
    keep = False
    for negate, pattern in rules:
        if glob_match(path, pattern):
            keep = not negate
    return keep


def is_text(name):
    p = pathlib.PurePosixPath(name)
    return p.suffix in TEXT_SUFFIXES or p.name in TEXT_NAMES


def version_of(ref):
    """The version CMakeLists.txt, version.hpp and version.cpp declare at `ref`, or a failure
    naming the disagreement."""

    def show(path):
        return git("show", f"{ref}:{path}").decode()

    cmake = re.search(r"project\(baker VERSION (\d+\.\d+\.\d+)", show("CMakeLists.txt"))
    hpp = show("include/baker/version.hpp")
    parts = [re.search(rf"kVersion{k} = (\d+);", hpp) for k in ("Major", "Minor", "Patch")]
    cpp = re.search(r'return "(\d+\.\d+\.\d+)";', show("src/data/version.cpp"))
    if not cmake or not all(parts) or not cpp:
        fail("cannot find the version in CMakeLists.txt, version.hpp or version.cpp")
    found = {
        "CMakeLists.txt": cmake.group(1),
        "include/baker/version.hpp": ".".join(p.group(1) for p in parts),
        "src/data/version.cpp": cpp.group(1),
    }
    if len(set(found.values())) != 1:
        fail("the version files disagree: " + ", ".join(f"{k} {v}" for k, v in found.items()))
    return cmake.group(1)


def changelog_entry(text, version):
    m = re.search(rf"^## {re.escape(version)}\b[^\n]*\n(.*?)(?=^## |\Z)", text, re.M | re.S)
    if not m or not m.group(1).strip():
        return None
    body = m.group(1).strip()
    if "TODO" in body:
        fail(f"CHANGELOG.md's entry for {version} still has a TODO")
    return body


def check(files):
    problems = []
    for name, data in sorted(files.items()):
        if not is_text(name):
            continue
        for n, line in enumerate(data.decode("utf-8").splitlines(), 1):
            for pattern, what in FORBIDDEN:
                if pattern.search(line):
                    problems.append(f"{name}:{n}: names {what}: {line.strip()}")
    return problems


def cmd_prepare(a):
    if not SEMVER.match(a.version):
        fail(f"'{a.version}' is not a version (MAJOR.MINOR.PATCH)")
    declared = version_of(a.ref)
    if declared != a.version:
        fail(f"the tree at {a.ref} declares {declared}, not {a.version}: run `publish.py bump`")
    rules = read_manifest()
    archive = git("archive", "--format=tar", a.ref)
    files = {}
    with tarfile.open(fileobj=io.BytesIO(archive)) as tar:
        for member in tar.getmembers():
            if member.isfile() and selected(member.name, rules):
                files[member.name] = tar.extractfile(member).read()
    for p in sorted(OVERLAY.rglob("*")):
        if p.is_file():
            files[p.relative_to(OVERLAY).as_posix()] = p.read_bytes()
    leftovers = []
    for name, data in files.items():
        if is_text(name):
            text, bad = strip_file(name, data.decode("utf-8"))
            files[name] = text.encode("utf-8")
            leftovers += bad

    notes = changelog_entry(files.get("CHANGELOG.md", b"").decode(), a.version)
    if notes is None:
        fail(f"CHANGELOG.md has no entry for {a.version}")
    problems = leftovers + check(files)
    if problems:
        print("\n".join(problems), file=sys.stderr)
        fail(f"{len(problems)} lines name what the release leaves out; reword them in baker-dev")

    out = pathlib.Path(a.out)
    if out.exists():
        shutil.rmtree(out)
    for name, data in files.items():
        dest = out / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(data)
    if a.notes:
        pathlib.Path(a.notes).write_text(notes + "\n", encoding="utf-8")
    print(f"prepared Baker {a.version}: {len(files)} files in {out}")


def cmd_check(a):
    import tempfile

    snapshot = git("stash", "create").decode().strip()  # tracked changes, uncommitted
    with tempfile.TemporaryDirectory() as tmp:
        a.version = version_of(snapshot or "HEAD")
        a.ref = snapshot or "HEAD"
        a.out = str(pathlib.Path(tmp) / "public")
        a.notes = None
        cmd_prepare(a)


def tags(repo):
    out = git("tag", "--list", "v*", cwd=repo).decode().split()
    return sorted(
        (tuple(int(x) for x in t[1:].split(".")) for t in out if SEMVER.match(t[1:])), reverse=True
    )


def cmd_commit(a):
    if not SEMVER.match(a.version):
        fail(f"'{a.version}' is not a version (MAJOR.MINOR.PATCH)")
    repo = pathlib.Path(a.repo)
    tree = pathlib.Path(a.tree)
    want = tuple(int(x) for x in a.version.split("."))
    have = tags(repo)
    if have and want <= have[0]:
        fail(f"v{a.version} is not above the newest release, v{'.'.join(map(str, have[0]))}")
    notes = pathlib.Path(a.notes).read_text(encoding="utf-8").strip()
    # The work tree becomes the release's tree exactly: what it dropped goes too.
    for name in git("ls-files", "-z", cwd=repo).decode().split("\0"):
        if name:
            (repo / name).unlink(missing_ok=True)
    for p in sorted(tree.rglob("*")):
        if p.is_file():
            dest = repo / p.relative_to(tree)
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(p, dest)
    git("add", "--all", cwd=repo)
    message = f"Baker {a.version}\n\n{notes}\n"
    subprocess.run(["git", "commit", "--quiet", "--file", "-"], cwd=repo, input=message.encode(),
                   check=True)
    subprocess.run(["git", "tag", "--annotate", f"v{a.version}", "--file", "-"], cwd=repo,
                   input=message.encode(), check=True)
    print(f"committed and tagged v{a.version} in {repo}; push main and the tag to publish")


def cmd_bump(a):
    if not SEMVER.match(a.version):
        fail(f"'{a.version}' is not a version (MAJOR.MINOR.PATCH)")
    major, minor, patch = a.version.split(".")
    edits = [
        ("CMakeLists.txt", r"(project\(baker VERSION )\d+\.\d+\.\d+", rf"\g<1>{a.version}"),
        ("include/baker/version.hpp", r"(kVersionMajor = )\d+", rf"\g<1>{major}"),
        ("include/baker/version.hpp", r"(kVersionMinor = )\d+", rf"\g<1>{minor}"),
        ("include/baker/version.hpp", r"(kVersionPatch = )\d+", rf"\g<1>{patch}"),
        ("src/data/version.cpp", r'(return ")\d+\.\d+\.\d+(";)', rf"\g<1>{a.version}\g<2>"),
        ("README.md", r"(baker\.git GIT_TAG v)\d+\.\d+\.\d+", rf"\g<1>{a.version}"),
    ]
    for path, pattern, repl in edits:
        p = ROOT / path
        text, n = re.subn(pattern, repl, p.read_text(encoding="utf-8"))
        if n != 1:
            fail(f"cannot set the version in {path}")
        p.write_text(text, encoding="utf-8")
    log = ROOT / "CHANGELOG.md"
    text = log.read_text(encoding="utf-8")
    if not re.search(rf"^## {re.escape(a.version)}\b", text, re.M):
        today = datetime.date.today().isoformat()
        entry = f"## {a.version} - {today}\n\n- TODO: what changed, for users of the library.\n\n"
        first = re.search(r"^## ", text, re.M)
        at = first.start() if first else len(text)
        log.write_text(text[:at] + entry + text[at:], encoding="utf-8")
    print(f"set the version to {a.version}; fill in CHANGELOG.md's entry")


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("bump", help="set the version and add a changelog entry")
    b.add_argument("version")
    b.set_defaults(func=cmd_bump)
    r = sub.add_parser("prepare", help="build and check the public tree")
    r.add_argument("--version", required=True)
    r.add_argument("--ref", default="HEAD")
    r.add_argument("--out", required=True)
    r.add_argument("--notes")
    r.set_defaults(func=cmd_prepare)
    k = sub.add_parser("check", help="prepare at the declared version, into a temporary dir")
    k.set_defaults(func=cmd_check)
    c = sub.add_parser("commit", help="commit and tag the public tree in a clone of zach/baker")
    c.add_argument("--tree", required=True)
    c.add_argument("--repo", required=True)
    c.add_argument("--version", required=True)
    c.add_argument("--notes", required=True)
    c.set_defaults(func=cmd_commit)
    a = p.parse_args()
    a.func(a)


if __name__ == "__main__":
    main()
