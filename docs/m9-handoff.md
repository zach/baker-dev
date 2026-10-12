# Handoff: start M9

For the session that starts M9. Paste the prompt below into it; delete this file in M9's
first commit.

---

Start M9, the docs and examples, in zach/moira-dev. Read `CLAUDE.md` first: it holds the
rules, and they apply throughout. Then read `docs/m9-plan.md`, whose four questions the
owner answered in `docs/decisions.md` D87 (all as recommended). M9's spec row is in
`docs/spec.md` §10.

**Where things stand (2026-10-12).**

- M0 through M8b are done. The last results entries are D79 (M8, the binary cache) and
  D86 (M8b: `#require`, packs and levels, the one-way rule, `moira::shape`). Decisions go
  up to D87; the next entry is D88.
- The work is on branch `claude/zealous-goodall-rtindg`, CI green through `f68e4c4`.
  `main` is at `a638b2b` (the end of M8) and does not have M8b yet; merge only when the
  owner asks.
- The core is 10,103 lines of the 15,000 soft budget (D69), and `moira::shape` 400 of its
  600, outside the core (`tools/count_lines.py`).

**What M9 builds** (the plan has the detail):

1. `README.md`, short (D87 Q3): the one-line-tweak pitch first, a five-minute path to a
   working tweak, one paragraph per feature with links, the build table, measured numbers.
   The current README (206 lines) describes M5 and stops at M7.
2. `examples/quickstart`: the README's five-minute path as a CTest program, and a script
   that fails when the README's code block and the program differ (D87 Q4).
3. `docs/language.md`: data, the preprocessor (`#require` too), packs and levels, shapes,
   script, errors. `docs/script-reference.md` stays generated.
4. `docs/cookbook.md`: keep its three sections and add live ops (environment boundaries,
   version history, rollback, tests before an overlay ships), tweaks in practice, reload
   callbacks, the cache in a build step, harness packs, a console over `eval`, repro
   scripts.
5. Examples, each a CTest run against expected output: `config`, `scripting`, `objects`,
   `tasks`, `remote-console` (BSD sockets with a Winsock shim, tested over loopback on every
   CI platform; D87 Q2), `live-ops`.
6. `examples/harness-imgui` (D87 Q1): Dear ImGui at a pinned release fetched as EASTL is,
   GLFW and OpenGL 3, behind `MOIRA_BUILD_HARNESS` (off by default). The panels: tweaks and
   shapes, an `eval` console, provenance on hover, write-back, and the Loop, Layers, and
   Snapshots readiness panels (D42). It pushes `local/<user>.dta` with
   `{.optional = true}`. The panels are plain functions, so CI runs them headless (no
   backend, a few frames, assertions).

**Things the docs must get right** (each is a recorded decision, not the spec's text):

- Shapes are declared in `(shapes (name (field type metadata...) ...))`, not the spec's
  `(shape name ...)` (D84); they govern arrays by tag, so keyed children such as
  `(bosses (dragon ...))` are not reached (D85).
- A pack's rank is its *level*, `(pack name (level game))` and
  `session.set_pack_levels({...})`, not the spec's *layer* (D80).
- Moira never throws: every fallible call returns `Expected<T>` (D54). Public headers use
  no standard containers (D55); `std_interop.hpp` and `std_script.hpp` add `std::string`
  forms.
- `bool` reads numbers, not the symbol `TRUE` (DTA's `TRUE` is a macro for 1; D85).
- Numbers to quote come from the D-entries, with the machine noted: e.g. `find_float` on
  a 20-child array ~9 ns warm (D77), a native call ~31 ns (D78), a 6.2 MB tree from a
  cache in 2.3–2.5 ms with one allocation (D79).

**How to work** (from `CLAUDE.md`): commit each sub-step green after
`tools/local_matrix.sh` (9 configurations) and `clang-format-18 --dry-run --Werror`;
check CI with the GitHub tools after each push (MSVC, libc++, and TSan run only there);
ask the owner each open question with AskUserQuestion, one per decision, recommended
option first; record deviations and results in `docs/decisions.md`; delete
`docs/m9-plan.md` when M9 is done, with a results entry checking the three done-when
criteria.

A suggested order: the quickstart and the README (they define the five-minute path), the
small examples, `language.md` and the cookbook, then the harness, which needs network
access to fetch ImGui and GLFW the first time.
