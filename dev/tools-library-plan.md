# The Tools Library, the Kit's Flags, and Eight Smaller Fixes: a Plan

The ten limits found while building 0.3.0 to 0.5.0 (D208 to D224), in one phase. Two were
decided by the owner on Oct 22, 2026:

- tool-only code moves into a separate `baker::tools` library (item 3);
- the host kit's flags take the prefix `--baker-` (item 4).

The rest are each small. Results and deviations go into `dev/decisions.md` as each step
lands, one D-entry a step. The owner's answers to the plan's questions are in §12.

---

## 0. Scope and order

| Step | Item | Library | Estimate | Breaks |
|---|---|---|---|---|
| 1 | 9. A test that skips shipping builds | tests | ~10 | — |
| 2 | 1. `check_tweaks` reports in path order | `baker::data` | ~10 | output order |
| 3 | 7. `Expected::and_then` | `baker::data` (header) | ~30 | — |
| 4 | 8. One name for clearing a live edit | data, script | ~5 | API or script |
| 5 | 2. Script calls and messages in `tools/count` | `tools/count` | ~80 | — |
| 6 | 3. `baker::tools` | new library | moves ~1,750 | API, link lines |
| 7 | 4. `--baker-` before the kit's flags | `baker::host` | ~20 | command lines |
| 8 | 5, 6. A value count rule: exact, at least, or any | `baker::tools`, data | ~120 | reference/1.3, tweaks/1.4 |
| 9 | 10. The tool's path names a build directory | data (project), host | ~40 | — |
| 10 | Docs, CHANGELOG, the stacks, telling the sessions | — | — | — |

Each step is one commit, green on `tools/local_matrix.sh` and clang-tidy, with its D-entry.

- **Why this order.**
  - Step 1 first: every later step's tests use it.
  - Step 5 before 6: the instruction counts then prove the split leaves the call path, the
    message path and a quiet frame where they were.
  - Step 6 before 8: the count rule is tool code, so it should land in its new home.
  - Step 7 after 6: both change every stack's link line or command line, so the stacks
    change twice in a row, not interleaved with the rest.
- **The release.** Steps 4, 6, 7 and 8 break the API, command lines or schemas, so this is
  0.6.0. It ships only when the owner asks.

---

## 1. Step 1: a test that skips shipping builds (item 9)

Today a test that edits live, reloads or promotes must start with
`if constexpr (kShipping) return;`. One is missing, and only the shipping leg fails, after a
full matrix run. That happened in D221.

- **The change.** `tests/support/dev_only.hpp` gives `BAKER_DEV_TEST_CASE(name)`. It is a
  `TEST_CASE` with `* doctest::skip(baker::kShipping)`, so the test still compiles in every
  build and is reported as skipped under shipping.
- **The existing guards.** The ten `if constexpr (kShipping) return;` guards in `tests/`
  move to it.
- **A gate that catches a missing one.** `tools/check_tests.py`, run by `ctest`, fails when
  a test outside a `BAKER_DEV_TEST_CASE` calls one of the operations shipping refuses
  (`set(`, `reload(`, `promote(`, `write_back`, `apply_edits(`, `toggle_layer(`). It is a
  lexical check with an allow comment for the rare false positive.
- **Also.** `tools/local_matrix.sh --quick` runs clang-tidy and the shipping leg alone, for
  before the full run. Those two caught most of this session's extra runs.

## 2. Step 2: `check_tweaks` in path order (item 1)

`check_tweaks` lists errors and warnings in the order tweaks were registered, which differs
between two builds of the same game. Every test comparing more than one finding sorts
first, and a CI diff of `check --json` shows moves that are not changes.

- **The change.** Sort both lists by file, line, column, then message. (As built, D227:
  `check --json` already sorted them this way, so the gap was only in the C++ lists.)
- **Cost.** A check is cold, so the sort costs nothing that matters.
- **Tests.** The sorting in the tests is deleted. A new test registers tweaks in reverse
  order and expects the same JSON.
- **Schema.** `baker.check` already promised no order for these, so it keeps its version.

## 3. Step 3: `and_then` on `Expected` (item 7)

Baker's own `Expected` has no `and_then`, for `T` or for `void`, so chaining two
`push_layer` calls needs a lambda and an early return.

- **The change.** `and_then(f)` on `Expected<T>` and `Expected<void>`, with `std::expected`'s
  meaning: `f` runs on a value, and an error passes through.
- **Builds.** It is header-only. Under `BAKER_STD_EXPECTED`, `std::expected` already has
  it, and a test checks both builds agree.
- **Scope.** Not `transform` or `or_else` until a caller needs one.

## 4. Step 4: one name for clearing a live edit (item 8)

C++ has `Config::clear_scratch(path)`, while the console has `config.clear`. The other
console commands match their C++ names. Both become `clear_scratch` (§12 Q1).

## 5. Step 5: script calls and messages in `tools/count` (item 2)

`tools/count` has the cases parse, lookup, lookup_text, set and frame. It has none for
scripts.

- **What that cost.** D211's 8.9% overload slowdown was settled only by wall-clock runs
  repeated twice, and its 10 to 12% message speed-up is recorded as unexplained.
- **New cases.**
  - `call`: a native function of two numbers, called from script.
  - `overload`: a three-way overload set, resolved on the second argument's type.
  - `declared`: a function declared from a description, with a body (D213).
  - `message`: a message sent to an object with a handler two levels up its type chain.
- **Setup.** Each case is built once and run N and 2N times, like the existing cases.
  `CASES` in `count_instructions.py` gains them.
- **Proof.** Run on D211's commit and its parent, to check the new cases explain the moves
  D211 recorded. The numbers go in this step's D-entry.

## 6. Step 6: `baker::tools` (item 3)

Most of the last three releases' growth is code a game never runs: it serves editors,
language servers, agents and CI, either without the game or through the kit's commands.

### The rule

`baker::tools` holds what only a tool, or the host kit on a tool's behalf, calls. A game's
own runtime never calls it. A game links it only when it links the kit.

### What moves

| Code | From | Lines |
|---|---|---|
| `write_reference_json`, the reference | `src/script/reference.cpp` | 839 |
| The console's `config.*` commands (`bind_config_tools`) | `src/script/config_tools.cpp` | 351 |
| `DescribedTweaks` | `src/data/tweak.cpp` | 153 |
| `explain`, `layer_effect`, `diff`, the layer tooling | `src/data/config_write.cpp` | 152 |
| `read_structure`, `TextStructure` | `src/data/parse.cpp` | 92 |
| The tweaks as JSON (`baker.tweaks/1.3`) | `src/data/tweak.cpp` | 89 |
| `declare_type`, `declare_object`, `has_type` | `src/script/context.cpp` | 74 |
| `split_metadata`, `presented_kind` | `src/data/tweak.cpp` | 22 |

That is about 1,770 lines, plus their headers. It leaves the core near 13,300.

### What stays in the core, and why

- `check`, `check_layer_text` and `LayerPolicy`: a game runs them on what it loads and
  receives.
- `digest` and `stack_digest`: a game uses them for replays and for matching a server.
- `write_back` and `promote`: an in-game editor writes back.
- `eval_request` and the reply form: a game's own console uses them.
- `declare` with a body: it is how a game with an embedded language registers its
  functions.

### The library

- **Shape.** `include/baker/tools/*.hpp`, namespace `baker::tools`, `src/tools/`, CMake
  target `baker::tools` behind `BAKER_BUILD_TOOLS` (default ON).
- **Dependencies.** It links `baker::script`, and `baker::shape` when it is built. The kit
  links it.
- **Data-only builds.** They build neither the tools nor the kit; the `data` leg confirms
  that.

### The API changes

Methods become free functions that take what they work on:

- `Config::explain(path)` → `tools::explain(const Config&, path)`; the same for
  `layer_effect` and `diff`, and their entry types (`ExplainEntry`, `Standing`,
  `LayerEffectEntry`, `DiffEntry`) move with them;
- `Context::declare_type(...)` → `tools::declare_type(Context&, ...)`; the same for
  `declare_object` and `has_type`, and `MethodDescription` moves with them;
- `write_reference_json`, `write_json(const CheckReport&)` and
  `write_json(const TweakRegistry&)`, `bind_config_tools`, `DescribedTweaks`,
  `TweakDescription`, `read_structure` and its `Text*` types, `split_metadata` and
  `presented_kind` become `baker::tools::` names in `<baker/tools/...>` headers.

Every caller in the repository changes, as the rules ask: the kit, the stacks, the
examples, the tests and the benchmarks.

### How the tools reach the internals (design note, Oct 22)

Reading the code for this step found what the first draft of this section missed.

- **Private members.** `explain`, `layer_effect` and `diff` read `Config::Impl`, and
  `declare_type` and `declare_object` read `Context::Impl` and call its private
  `add_overload`. Each class gains one friend, `detail::ToolsAccess`, a struct in a private
  header (`src/common/tools_access.hpp`) whose static functions hand the tools library the
  `Impl`. It follows `detail::SessionAccess` (`src/data/session.hpp`), which already does this for
  `LoadSession`.
  Public headers name it only in the friend line, so they stay inside D55's headers.
- **What the declared types are stays in the core.** `Context::Impl` keeps
  `declared_types` and `declared_objects`, because `check()` holds calls to them. Only the
  functions that fill them move. `build_described`, now file-local to `context.cpp`, is
  declared in `script/internal.hpp` so both `declare` (core) and `declare_type` (tools)
  build overloads with it.
- **The lexer.** `read_structure` reads text with the parser's own `Lexer`, a class in an
  anonymous namespace in `parse.cpp`. It moves, unchanged and still defined in its class
  (inline), to a private header `src/data/lexer.hpp` in `baker::detail`, which `parse.cpp`
  and the tools' `structure.cpp` include. This is the one change near a hot path: the
  `parse` instruction count must not move.
- **The kind rule.** `presented_kind` and `TweakBase::kind()` share `kind_of`, file-local to
  `tweak.cpp`. It is declared in a private header (`src/data/tweak_internal.hpp`) so the
  rule stays written once.
- **JSON helpers.** The tools use `detail::json_string` and the other JSON writers the
  core shares between data, script and shape. They stay in the core, which writes JSON of
  its own too.

### Review (acton-review, Oct 22)

- **The lexer's move is the one load-path risk.** In `parse.cpp`'s anonymous namespace the
  `Lexer` has internal linkage, which lets GCC inline its single-caller members freely; in
  a shared header it has external linkage and may be inlined differently. Evidence
  required: the `parse` count equal before and after. Plan B: keep its internal linkage
  (an anonymous namespace inside the private header, which only `parse.cpp` and
  `structure.cpp` include), or leave `read_structure` in the core.
- **`ToolsAccess` adds no indirection:** inline static functions returning the `Impl` the
  methods reached through the same `impl_` pointer, on tool paths only.
- **The rule matches D100's option C,** which D100 costed: about 36 KB less tooling in a
  default-flags shipping game, nothing with `--gc-sections`, and the core's lines. Measure
  D100's minimal game before and after, default flags, and record it.
- **Tool-only state stays in `Context::Impl`** (`declared_types`, `declared_objects`):
  cold vectors `check()` reads, never on the call path. Accepted as it is.

### Tests

The tests of what moves (`test_reference_json`, `test_config_tools`, `test_structure`,
`test_tweaks_json` and the declared-type tests in `test_check`) move to `tests/tools/`, a
`baker_tools_tests` executable. The core's test executables then link no tools code, which
shows the core builds and passes without it.

### Size

`tools/count_lines.py` reports `baker::tools` on its own line.

### Proof

- `ctest`'s header gates cover `include/baker/tools/`.
- `tools/count_instructions.py --base HEAD~1` shows every case unchanged, `parse` above
  all (the lexer's move): instruction counts are exact, so "unchanged" means equal, not
  within noise.
- A game that links `baker::baker` alone builds and links with no tools code, and D100's
  minimal game is measured before and after with default flags.
- The release script, the manifest and `publish.py check` ship the new directory.

### As built (D231)

Where the build differs from the sections above:

- **Stayed in the core:** the tweaks' JSON (`write_json(const TweakRegistry&)`), since a
  data-only program writes it and cannot link `baker::tools`; `split_metadata` and
  `presented_kind`, which share their rule with `TweakBase::kind()`; `value_type_known` and
  `reads_as`, which declared functions use at a call; `check` and its text report, now in
  `src/script/check.cpp`.
- **No `BAKER_BUILD_TOOLS` option:** that name already switches the command-line
  utilities. The library builds with `baker::script`, as the kit does.
- **The lexer** keeps its internal linkage inside `src/data/lexer.hpp` (plan B): with
  external linkage the parse ran 1.5% more instructions.
- **Tests:** the data tests link no tools. The script tests and the allocation tests link
  `baker::tools`, since they read the reference.

## 7. Step 7: `--baker-` before the kit's flags (item 4)

The kit reads `--project`, `--scenario`, `--seed`, `--seeds`, `--step-hz`, `--results`,
`--before`, `--after`, `--json`, `--layer`, `--from`, `--to`, `--patch`, `--write` and
`--output`. The test game and the stacks had to rename their own flags to make room.

- **The change.** Each kit flag becomes `--baker-NAME`: `--baker-project`,
  `--baker-seeds`, and so on.
  - Today the kit takes those names wherever they appear, and passes only the rest to the
    game in `CommandLine::rest`. After the change, any flag without the prefix goes to
    `rest`.
  - A `--baker-` flag the kit does not know is an error, so a misspelling is caught.
- **Where it changes.** `parse_command_line`, `tool_args` (D217), the stacks' command
  lines, `stacks/tools/test_commands.py`, `docs/tools.md`, `docs/sim-json.md` and
  `examples/host`.
- **Command names.** `check`, `sim`, `reference`, `eval`, `compare`, `diff` and `promote`
  are positional. A command the kit does not know already falls through to the game
  (`run_command` returns -1), so they keep their names (§12 Q3).

## 8. Step 8: how many values a described value takes (items 5 and 6)

A converter's `values` lists one value type per position, which has two gaps:

- **Item 5.** A tool reads it as "at least these". `std::array<float, 3>` takes exactly
  three, so `(color 0.9 0.5 0.2 0.1)` passes in a tool and fails in the game (D212).
- **Item 6.** `std::vector<T>` has no `values`, so a tool can't check a list tweak at all
  (D210).

### The change

Beside `values`, a converter states its count:

```cpp
enum class ValueCount : uint8_t { Exact, AtLeast, Any };
//   std::array<T, N>: values = N of T, count = Exact
//   std::vector<T>:   values = {T},    count = Any   (zero or more, each a T)
//   a converter with optional trailing values: count = AtLeast
```

- **Default.** A converter that gives `values` without a count is `Exact`.
- **Tools.** `DescribedTweaks` and `declare_type`'s parameter types hold the data to the
  count.
- **Schemas.** The reference and `baker.tweaks` write it as `"count": "exact" | "at_least"
  | "any"`. These are new fields, so reference/1.3 and tweaks/1.4.
- **Cost.** Nothing on the game's path changes: the count is a constant beside `values`,
  read only by tools.

### Tests

- A 4-color against `std::array<float, 3>` fails in a tool, as in the game.
- A `std::vector<int>` tweak is described, written and checked, including a non-integer
  inside the list.
- The JSON of each, in both schemas.

## 9. Step 9: the tool's path names a build directory (item 10)

The stacks' `(tool ...)` is empty: the binary is inside a build directory the project file
does not know, so an editor or CI must be told each stack's binary (D208).

- **The change.** The design (§12 Q4):
  - a `(tool ...)` path may begin with `$build`;
  - the project's `(build "build")` gives its default, relative to the project file;
  - a tool may pass its own, from a flag or a setting.
- **Library.** `baker::Project` keeps the path unresolved. It gains
  `tool_path(std::string_view build_dir = {})`, which resolves it.
- **The stacks.** Each fills in `(tool "$build/bin/<stack>")`, and `test_commands.py` finds
  the binaries through it.

## 10. Step 10: docs, the stacks, and the sessions

- **User docs.** `docs/tools.md` gains a section on `baker::tools`, the new flags and
  `$build`. `docs/sim-json.md` changes its flags, and the reference and tweaks schema pages
  gain the count.
- **CHANGELOG.** The 0.6.0 section lists each breaking change with what to do.
- **The stacks.** They build against the new link lines and flags, in their own CI job.
- **The sessions.** After the release, tell the editor, Godot and MCP sessions what changed
  for them:
  - they link `baker::tools` for the tool functions;
  - their command lines take `--baker-` flags;
  - the two schemas carry `count`.

The editor, Godot and MCP sessions each update their own repository.

---

## 11. Not in this phase

- **The Thrive report's needs.** These are cross-table references in shapes, invariants
  declared in data, and a reload published together for worker threads. Each waits on a
  requirement note from the owner.
- **The gaps from this session's review that were not chosen.** Each is in the D-entry that
  found it:
  - a raw parameter's runtime type check (D213);
  - `read_structure`'s macros from other files (D220);
  - a subtree-only merge for `apply_edits` (D224);
  - `reload()` re-reading layers whose macros did not change (D221).

## 12. Decisions

The owner answered these on Oct 22, 2026:

- **Q1 (step 4).** Both use `clear_scratch`. The console's `config.clear` becomes
  `config.clear_scratch`; "scratch" is already the layer's name in `explain` and
  `layer_effect`.
- **Q2 (step 6).** The console's commands (`bind_config_tools`) move into `baker::tools`,
  and `eval_request` stays in the core.
- **Q3 (step 7).** Only the flags take the prefix. The command names stay as they are,
  since a command the kit does not know already falls through to the game.
- **Q4 (step 9).** `$build`, with a default from the project's `(build "dir")`, and
  `Project::tool_path(dir)` for a tool that passes its own.

## 13. The 0.6.0 changelog, drafted (step 10)

For `CHANGELOG.md` when the owner asks for the release; it covers `main`'s merges since
0.5.0 (the digest, steps, live reads, number text) and this branch.

```markdown
## 0.6.0 - DATE

- **Breaking:**
  - **`baker::tools`** is a new library for what only tools call; a game links it only with
    the host kit, which brings it. Move to `<baker/tools/...>` and `baker::tools::`:
    `write_reference_json`, `reference`, `write_reference`, `write_json(const CheckReport&)`,
    `script_functions` (`reference.hpp`); `explain`, `layer_effect`, `diff` and their entry
    types (`layers.hpp`), now free functions taking the Config; `bind_config_tools`
    (`console.hpp`); `DescribedTweaks`, `TweakDescription`, `MethodDescription`,
    `declare_type`, `declare_object`, `has_type` (`described.hpp`), the last three free
    functions taking the Context; `read_structure` and its types (`structure.hpp`);
    `to_json(report)` and `reference(ctx)` as strings (`std_tools.hpp`). Link
    `baker::tools` in a tool that uses them without the kit. The core is about 1,400 lines
    smaller, and a shipped game about 24 KB smaller with default link flags.
  - **The host kit's flags begin `--baker-`:** `--baker-json`, `--baker-seeds`,
    `--baker-project`, and the rest. Any other flag is the game's, and an unknown `--baker-`
    flag is an error. Command names are unchanged.
  - **The console's `{config.clear PATH}` is `{config.clear_scratch PATH}`,** as C++ names it.
  - **A converter's `values` are exact unless it says otherwise:** one that reads more than
    its listed value types declares `static constexpr baker::ValueCount count =
    baker::ValueCount::AtLeast;` (or `Any`). Tools now refuse four values for a
    `std::array<float, 3>`, as the game does.
- **New:**
  - **`Config::digest()` and `stack_digest()`:** 64-bit digests of the data, the same on
    every platform, for lockstep peers, replays and saves.
  - **Scenarios in a game's own steps:** `(steps N)`, `(at step N ...)` and `(until
    {command})`, `$steps`, and the project's `(step_name turn)`; `baker.sim/1.2` says how a
    run ended.
  - **Live edits reach reads above them:** a record or table read while an edit is pending
    beneath it sees the edit; `Config::apply_edits()` folds edits into `root()`, and the
    host kit's live loop calls it.
  - **List tweaks and parameters are described:** `std::vector<T>` declares its element's
    value type, any number of them, so a tool checks a list as the game reads it.
    `baker.reference/1.3` and `baker.tweaks/1.4` add `count`.
  - **`(tool "$build/bin/my_game")`** in a project file, with `(build "dir")` as the
    default and `Project::tool_path(dir)` to resolve it.
  - **`and_then`** on `Expected`, in C++20 builds as in C++23.
- **Fixed:**
  - Numbers are written as the same text on every platform: a Mac build wrote 120.0 as
    `1.2e+02`.
  - A tweak check lists its findings in file, line and column order, whatever order the
    tweaks were registered in.
```
