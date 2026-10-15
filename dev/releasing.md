# Releasing Baker

Baker is developed here, in baker-dev, and published to
[github.com/zach/baker](https://github.com/zach/baker): the core library alone, with its
tests, examples and user docs, one commit per release, tagged with its semantic version.
The public repository carries none of this one's process: no decisions, spec, background
notes, skills, stacks, dev tools or history (D150).

A release is automatic but never autonomous. **The owner decides when to release.** An
agent prepares or starts one only when the owner asks for it in so many words (CLAUDE.md).

## What ships

`tools/release/manifest.txt` lists it: the library (`include/`, `src/`, `third_party/`),
`tests/` with its corpus and fuzz regressions, `fuzz/`, `examples/`, `bench/`, the tools the
build and tests use, all of `docs/` (the user documentation), `README.md`, `CHANGELOG.md`,
`LICENSE` and `THIRD_PARTY.md`. The development documents are in `dev/`, which never ships
(D152). `tools/release/public/` is laid over it: the public repository's own CI
workflow and `.gitignore`.

On the way out, `tools/release/publish.py` strips the citations of this repository's
documents: a parenthetical such as `(D77)`, `(spec §5, D80)` or
`(dev/decisions.md D55)` goes, and one that mixes citations with words keeps the words.
A citation that began a line hands its punctuation to the line before. It then refuses the
release if anything published still names what the release leaves out: a decision number
or spec section outside such a parenthetical, `decisions.md`, `spec.md`, `dev/background`,
`stacks/`, `CLAUDE.md`, `baker-dev`, a gap number. Such a line is reworded here, not
patched in the published copy. `publish.py check` runs that dry run, and CI's "release dry
run" job and `tools/local_matrix.sh` run it on every change, so a sentence the release
cannot publish fails where it is written.

## Making a release

1. **Prepare** (a normal PR, reviewed and merged):
   `python3 tools/release/publish.py bump 0.2.0` sets the version in `CMakeLists.txt`,
   `include/baker/version.hpp`, `src/data/version.cpp` and the README's `FetchContent` tag,
   and adds a `## 0.2.0` entry to `CHANGELOG.md`. Write the entry for users of the library:
   what changed, what breaks, what to do about it; no decision numbers. A release refuses an
   entry that still says TODO.
2. **Rehearse** (optional): Actions > Release > Run workflow on `main` with the version and
   *publish* unchecked. It builds the public tree, checks it, and builds and tests it alone
   with GCC, Clang with ASan and UBSan, shipping, and MSVC, then stops. The tree is
   attached to the run as an artifact.
3. **Publish**: the same, with *publish* checked. After the checks pass, the `publish` job
   clones zach/baker, replaces its tree with the release's, commits it on `main` as
   "Baker 0.2.0" with the changelog entry as its message, tags `v0.2.0`, pushes both, and
   creates the GitHub release. It refuses a version not above the newest tag.

Versions: before 1.0.0, a minor version may change the API and a patch version does not.

## Setup, once

- **The token.** A fine-grained personal access token for the repository zach/baker with
  *Contents: read and write*, stored in baker-dev as the Actions secret
  `BAKER_PUBLISH_TOKEN` (Settings > Secrets and variables > Actions).
- **The gate** (recommended). Settings > Environments > New environment `release`, with
  the owner as a required reviewer. The `publish` job then waits for an approval, even when
  the run was started with *publish* checked.

## By hand

The workflow's steps run anywhere with Python 3, git and push access to zach/baker:

```sh
python3 tools/release/publish.py prepare --version 0.2.0 --out build/public --notes build/notes.md
cmake -S build/public -B build/public-build -G Ninja && cmake --build build/public-build
ctest --test-dir build/public-build
git clone https://github.com/zach/baker.git build/baker
python3 tools/release/publish.py commit --tree build/public --repo build/baker \
  --version 0.2.0 --notes build/notes.md
git -C build/baker push origin HEAD:main v0.2.0
```

`prepare` reads the committed tree (`--ref`, default `HEAD`), never the work tree.
