# Integration Plans: Agents, Editors, Godot, Published Overlays

Plans for the four integrations chosen in `dev/background/integration-landscape.md` §7.
Each plan was drafted, critiqued, and refined. Each plan below gives a short first draft,
the critique as findings and the change each forced, and the refined plan, which is the
one to follow.

Oct 15, 2026. A proposal; the owner's first choices are in §8 and D153. Later choices go in
`dev/decisions.md`.

---

## 0. Summary

| Plan | What it is | Where it lives | First proof |
|---|---|---|---|
| **A. Agent bridge** (closed, D207) | An MCP server that gives any agent client a Baker game's tools: `check`, the reference, values with their layer chain, the agent layer, A/B, runs, a guarded promote | `baker-mcp` (MIT), a thin adapter; the semantics live in Baker. Archived | An agent tunes Duel through the bridge as well as the stacks-agent skill does through the CLI, in fewer calls |
| **B. Editor tooling** | A tree-sitter grammar, an LSP server driven by Baker's own `check`, and live values from the running game in the editor | `baker-editor` (MIT): grammar, server, VS Code extension | An error appears as you type; hover shows a value's layer chain and its live value |
| **C. Baker for Godot** | A GDExtension: layers, provenance, and a value tuned while playing written back to its line | `baker-godot` (MIT) | A value tuned in a running Godot game reaches git as a one-line diff |
| **D. Published overlays** | Versioned, checked overlays fetched by a shipped build, applied under a narrow policy, rolled back in one call, promoted to source when they stick | `baker-liveops` (MIT): publisher, client helper, a static-HTTPS backend, a Nakama backend | A shipped stack takes a balance overlay, refuses a bad one, and rolls back |

**The finding that shaped all four:** most of the work is Baker's own. Plans A, B and C need
the same four Baker features, and plan D needs a fifth that A also uses (§1). Built once,
they serve every host, including hosts that use no integration at all. The integrations
themselves then stay thin, which is what keeps them maintainable.

**Order** (§6):
1. The shared Baker features E1–E3.
2. Plan A, because it is cheapest and the stacks are ready for it.
3. E4 and plan B.
4. Plan C.
5. E5 and plan D.

C and D can run in parallel once A is done.

---

## 1. Shared Baker features

These are Baker features, designed here in Baker's terms. Each is motivated by hosts in
general: tools, editors, agents, scripting hosts, live games. Each gets its own D-entry
and tests against a mock host. None touches a hot path, and every one compiles out or
costs nothing in a shipping build unless noted.

There is about 3,000 lines of room in the core budget: 11,989 of 15,000 by
`tools/count_lines.py` today. The estimates below total about 1,000.

### E1. `explain`: a path's whole layer chain

**Need.** Today a tool learns where a value came from in two partial ways. `source()`
names the winning layer only. `layer_effect(layer)` works one layer at a time. An agent,
an editor's hover, a Godot inspector, or a modder asking "who set this" all need the
whole chain for one path, bottom to top.

**Design.**

```cpp
struct ExplainEntry {
    Symbol layer;          // a layer name, "scratch", or "code" for a tweak's default
    Node value;            // packed like LayerEffectEntry::value; empty if removed
    Source source;         // file, line, origin
    enum class State : uint8_t { Wins, Shadowed, Removed, Disabled } state;
};
[[nodiscard]] Expected<List<ExplainEntry>> Config::explain(const Path& path) const;
```

- **Order and contents.** Entries run bottom to top.
  - A disabled layer that supplies the path is listed as `Disabled`, so an A/B toggle is
    visible.
  - A `#remove` marker is listed as `Removed`.
  - A bound tweak's code default is the first entry.
- **Cost.** About 120 lines, reusing `supplier`, `source_in` and `value_nodes`. It
  allocates its result, and is never on a hot path.

**Critique → change.**
- *"A conflict report for modders is a separate feature."* → It is `explain` run over the
  paths that more than one pack supplies. It goes in later as a filter over E1, not as new
  machinery.

### E2. A request/reply form for tools, and the Config's tools as script functions

**Need.** The remote console is an example: text lines in, `=> value` text out, with no
request id and errors as prose. Every bridge parses that text and breaks when it changes.

**First draft.** A JSON request/reply protocol in the library, with verbs `get`, `set`,
`explain`, `eval` and so on.

**Critique → change.**
- *"A JSON parser in the core costs about 300 lines and duplicates the reader, and verbs
  are a second command language beside script."* → Requests stay in Baker's own text. A
  command is already a serialized, parseable, evaluable value (`iteration-backgrounder.md`
  §6.3), so the verbs are script functions.
- *Replies must be machine-readable, and Baker already writes deterministic JSON for
  `check`.* → Replies are JSON.

**Refined design.**

1. **`eval_request`.** It reads one line of the form `(ID {command})`, evaluates the
   command, and writes one JSON line:

   ```cpp
   void eval_request(Context& ctx, std::string_view line, TextSink reply);
   ```

   The reply schema is `baker.reply/1`:
   - `{"schema", "id", "ok": true, "value", "text"}` on success.
   - `{"schema", "id", "ok": false, "error": {code, file, line, column, path, message, stack[]}}`
     on failure.
   - `"text"` is the printed Baker form, which round-trips.
   - `"value"` is a JSON projection of it: ints and floats as numbers; strings and symbols
     as strings; arrays as JSON arrays, with the tag first.
   - The projection is lossy by design. The text is the authority.
   - About 150 lines, sharing `check`'s JSON writer.
2. **`bind_config_tools(Context&, Config&)`.** It registers the Config's tooling as
   ordinary functions with docs, so the reference lists them and `check` checks calls to
   them:
   - reading: `config.get`, `config.source`, `config.explain`, `config.layers`;
   - layers: `config.toggle`, `config.effect`, `config.diff`;
   - live edits: `config.set`, `config.unsaved`, `config.journal`;
   - state and telemetry: `config.snapshot`, `config.restore`, `config.loop`;
   - writing: `config.write_back` and `config.promote`.

   The two writers are registered only when the host passes
   `{.allow_write = true}`. They are compiled out under `BAKER_SHIPPING`, like the rest
   of live editing. About 200 lines in `baker::baker` (it needs both libraries).
3. **The example console switches to `eval_request`.** `stacks/tools/console.py` reads
   JSON, and its exit codes are unchanged.

**Acton notes.**
- Nothing new on a hot path.
- The bindings allocate only when called.
- A host that wants none of it does not call `bind_config_tools`.

### E3. A machine-readable reference

**Need.** `reference` is Markdown only. An agent client needs tool and function schemas,
an editor needs completion and signature help, and a Godot dock needs the tweak list:
none of them should scrape Markdown.

**Design.**

```cpp
void write_reference_json(const Context& ctx, TextSink out, const LoadSession* packs = nullptr);
void shape::write_json(const shape::Set& shapes, TextSink out);   // in baker::shape
void write_tweaks_json(const TweakRegistry& reg, TextSink out);    // in baker::data
```

- **Schema `baker.reference/1`**, versioned like `baker.check/1`.
- **What it lists:**
  - every function, with each overload's parameter and return types (the names the
    Markdown already prints), its status, replacement, tier, doc, and, for a script
    function, its file and line;
  - globals;
  - classes, with their methods, properties and messages;
  - packs and origins.
- **Two documents beside it:**
  - The shapes are written by `shape::write_json`, so `baker::script` does not depend on
    `baker::shape`.
  - The tweaks are written by `write_tweaks_json`: path, type, default, and code
    metadata.
- **Cost.** About 250 lines across the three. `docs/` gains `reference-json.md`.

**Critique → change.**
- *"Export JSON Schema instead, which editors already understand."* → Not instead.
  Shapes carry provenance, ranges with steps, `kind` and keyed tables, which JSON Schema
  would flatten. A JSON Schema export can be written later over `shape::write_json` by a
  tool, outside the core.
- *"Script functions need their definition site, which may not be recorded."* → Check
  first. If it is missing, recording file and line per `{func}` and macro is a few bytes
  per definition, made at load. It goes in as part of E3, with its own test.

### E4. Columns on errors

**Need.** An error carries file and line but no column (`error.hpp`). An editor
underlines a span, not a line, and an agent fixing a long line needs the column too.

**Design.**
- `Error` gains `column()`, 1-based, 0 when unknown. The record lives on the error path
  only (D55), so `Expected<T>` does not grow.
- The parser already tracks the line. It keeps a pointer to the start of the current line
  and computes the column only when it makes an error.
- Arrays keep their line but no column, so lookup and shape errors give the array's
  line and leave the column 0. The editor (plan B) then widens the mark to the first
  token on that line, using edit-mode spans.
- `baker.check/1` gains an optional `column` field. That is a minor addition under the
  versioning rule in `docs/check-json.md`.

**Acton note.**
- The parser is a measured path: 121–131 MB/s (D82). Measure before and after with
  `bench/`, and keep the line-start pointer in a register-friendly local.
- If the number moves, compute the column lazily from the byte offset on the error path
  instead.

### E5. Checking a layer before it is enabled, under a policy

**Need.** A live layer is all or nothing today. `live_layers` either allows layer changes
after `freeze()` or does not. A host taking overlays from outside wants three things:
1. To check one before it takes effect. Its validators run, its pack's `requires` and
   versions are checked, and its effect is shown.
2. To bound what any overlay may change: data only, with no commands; only listed paths;
   only numbers.
3. A refusal that changes nothing.

An agent's `propose` tool (plan A) wants the same check.

**Design.**

```cpp
struct LayerPolicy {
    bool data_only = false;            // refuse {commands} and [properties]
    bool scalars_only = false;         // values may only be ints, floats, symbols, strings
    std::span<const Path> allow{};     // if not empty, every path must lie under one of these
};
struct LayerReport {
    List<LayerEffectEntry> effect;     // what it would change
    List<Error> refusals;              // policy violations, located
    List<Error> warnings;              // validators and pack checks, located
};
[[nodiscard]] Expected<LayerReport> Config::check_layer(std::string_view name, Ref<Array> root,
                                                        const LayerPolicy& policy) const;
// LayerOptions gains `const LayerPolicy* policy`: push_layer refuses a layer that breaks
// it, and changes nothing.
```

- **How the check runs.** It merges the candidate into a copy of the current stack's
  root, calls the validators on that root, and computes the effect. That is
  `layer_effect`'s existing path (a disabled layer's effect is already computable).
- **Cost.** About 200 lines. It allocates while checking, never while running.

**Critique → change.**
- *"Should Baker bucket players for experiments?"* → No. Bucketing is a hash of a player
  id, the host's business and vendors' product. Baker applies the layer the host chose.
- *"A key-map → layer helper for flag vendors?"* → No. Writing `(a (b 3))` text from
  pairs is a few lines in the integration that needs it. Baker gains nothing by owning
  it.

### E6. A tweak whose path the registry keeps

**Need.** `Tweak(const char* path, ...)` requires the path to outlive the tweak. A
scripting host, such as the Godot extension's tunables made from GDScript, builds paths
at runtime.

**Design.** A constructor that takes a `Symbol`. Symbols are interned for the life of the
program, so the path outlives everything. About 15 lines and one test.

**Critique → change.**
- *"Interning grows without bound if a host makes tweaks in a loop."* → Hosts make
  tunables once, at startup or at load. Document the expectation, and do not build an
  owning string.

---

## 2. Plan A: the agent bridge

**Status: closed** (D207). The owner archived `baker-mcp` and its session: the server showed
no benefit over the CLI and the stacks-agent skill, which is the comparison this plan made its
kill criterion. What it asked of Baker stays: the JSON forms of `check`, `eval` and the
reference, `promote --patch`, and the host kit's commands.

### Goal and fit

- **The goal.** Any MCP-capable agent client can work in a Baker game the way the stacks'
  agent does today:
  - read the vocabulary;
  - propose in the `agent` layer;
  - `check`;
  - run scenarios;
  - A/B in the running game;
  - report;
  - promote only with a person's approval.
- **The fit.** This is the third meaning of harness (`harness-tree-vision.md` §7). It is
  the one place where Baker's distinctive pieces (§6.1 of the landscape) beat every engine's
  MCP server outright. Those servers have no provenance, no reviewable diff, and no
  validation beyond the log.

### First draft

- **Shape.** A Python MCP server in `baker-mcp` with about 15 tools: `check`,
  `reference`, `get`, `set`, `eval`, `diff`, `journal`, `loop_stats`, `snapshot`,
  `restore`, `toggle`, `run`, `capture`, `propose`, `promote`.
- **How it reaches the game.** It parses the console's text replies and the game
  binary's command output.

### Critique

| # | Finding | Change |
|---|---|---|
| A1 | Parsing `=> value` text is brittle, and every parser in every bridge would drift separately | E2 first: `baker.reply/1` JSON; the bridge parses nothing Baker-shaped |
| A2 | Fifteen tools crowd an agent's context and degrade tool choice; the engine MCP servers that do well expose few, composite tools | Eight tools (below), each returning one structured result |
| A3 | The CLI and the skill already work for Claude Code in this repository; an MCP server must earn its place | Its value is (1) any MCP client, not one; (2) a persistent session with a live game; (3) structured results. The evaluation (below) measures it against the CLI, with a kill criterion. |
| A4 | `eval` executes arbitrary script in a running game | Loopback only, dev builds only (the console compiles out under `BAKER_SHIPPING`). The bridge refuses non-loopback addresses. Writes (`write_back`, `promote`) are off unless the game is started with them allowed (E2's `allow_write`). |
| A5 | An MCP server cannot enforce "promote needs a person's approval" | It never promotes. It returns the would-be diff (the `promote` report as a unified diff) and writes it to a patch file. A person applies it, or the client's own permission prompt gates a separate, explicit apply tool. |
| A6 | A second language (Python) duplicating logic will drift from the C++ | The bridge holds no semantics: every tool is a command to the game binary or a request to the console. Target: under 600 lines. |
| A7 | `propose` writing `agent.dta` and reloading has no gate before it takes effect | `propose` calls E5's `check_layer` on the candidate first, and writes nothing on a refusal |

### Refined plan

**Tools** (each returns JSON that Baker wrote, unchanged):

| Tool | Does | Built on |
|---|---|---|
| `baker_vocabulary` | The functions, classes, packs, shapes, tweaks | E3, through the game binary's `reference --json` |
| `baker_check` | Errors, warnings, demand, findings | `check` (`baker.check/1`) |
| `baker_explain` | A path's value, layer chain, live value, unsaved state | E1 and E2 (`{config.explain p}` live, or headless) |
| `baker_propose` | Write a candidate into the agent layer: checked (E5), then reloaded | E5, the writer hook, `reload` |
| `baker_run` | Scenarios over seeds, before and after; objectives that moved | the stack's `sim` command |
| `baker_ab` | Toggle the agent layer in the running game; snapshot/restore | E2 (`config.toggle`, `config.snapshot`) |
| `baker_session` | The journal since a time, the unsaved list, loop stats: what the person changed after the agent | E2 (`config.journal`, `config.unsaved`, `config.loop`) |
| `baker_promote_preview` | The agent layer's effect as a unified diff, written to a patch file; never applied | `promote` dry run (`config.effect` plus the writer hook into memory) |

**Resources.** The reference and the stack's skill, so a client can load the vocabulary
without spending a tool call.

**Repository.** `baker-mcp`, MIT, from `tools/integration-template/`, using the MCP Python
SDK (MIT). It speaks stdio to the client, TCP loopback to a running game, and subprocess
calls to the game binary. It depends on a released Baker only through the game binaries
it drives, so its CI builds the stacks' cube against a Baker release.

**Steps.**
1. In Baker: E1, E2, E3, each with a D-entry and mock-host tests. The stacks move to
   them: `console.py` reads `baker.reply/1`, and the game binaries gain `reference --json`
   and `explain`. About 600 lines in the core.
2. `baker-mcp` with the eight tools, against cube and Duel.
3. **Evaluation.** Twelve tasks from the stacks' agent skill (tune Duel's matchup
   spread, fix a broken Ledger price table, and so on), each run three times by an agent
   with the CLI and skill, and by an agent with the bridge. Recorded:
   - task success, judged by the stacks' objectives;
   - tool calls;
   - tokens;
   - wall time;
   - promotions that a person rejected.
4. A cookbook section, "Agents", in `docs/` (shipped, in Baker's terms). It covers the
   reply form, `bind_config_tools`, and the agent layer convention.

**Acceptance.**
- Every tool's result is Baker's JSON, unchanged.
- No write to a data file happens except through `baker_propose` into `agent.dta`, and
  that refuses on a failed `check_layer`.
- The bridge is under 600 lines.
- The evaluation's success rate with the bridge is at least the CLI's, with fewer tool
  calls, for a second client as well as Claude Code.

**Kill criterion.** If the bridge does not beat the CLI and skill on calls or success,
publish E1–E3 (they stand alone) and park the bridge.

**Effort.** Baker features about 1.5 weeks; bridge about 1 week; evaluation about
3 days.

---

## 3. Plan B: Baker in the editor

### Goal and fit

- **The goal.** The file a designer or an agent edits shows what Baker knows:
  - errors as you type, located to the column;
  - completion from shapes;
  - hover with a value's layer chain;
  - the value the running game holds now;
  - a code action that pulls a tuned value into the file.
- **The fit.** This closes the loop in the other direction from write-back: the running
  game's state comes to the text. It serves designers, who live in the editor, and agents
  that edit files.

### First draft

- **Grammar.** A tree-sitter grammar for highlighting.
- **Server.** A C++ LSP server that links Baker and runs `check` on each change, with
  hover, completion, go-to-definition and formatting.
- **Packaging.** A VS Code extension that bundles both.

### Critique

| # | Finding | Change |
|---|---|---|
| B1 | The LSP surface is large; trying all of it ships none of it | In order: diagnostics, hover, completion of shape fields, go-to-definition, live values. Formatting is out. Baker's printer drops trivia, and write-back's lossless writer is not a formatter. |
| B2 | The server must know the layer stack, defines, pack levels and include paths, which today are set up in each game's C++ `main`. A second copy in an editor config would drift. | A project file in Baker text (`baker-project.dta`: layers, defines, pack levels, search paths) that the game's own setup reads too. Plus an example helper in the stacks' base. The server reads only that file. |
| B3 | `check` needs the game's native functions to know their arity; an offline server has no game Context | The game binary writes E3's reference JSON in its build (`game reference --json > build/reference.json`); the server registers each function as a stub with that signature. A stale reference is visible: the file's age and the binary it came from. |
| B4 | VS Code does not use tree-sitter for highlighting | A small TextMate grammar for VS Code plus LSP semantic tokens. tree-sitter serves Neovim, Helix and Zed. |
| B5 | `.dta` is Stata's extension in editors and on GitHub | Language id `baker`. The extension activates on `.dta` only in a workspace with a `baker-project.dta`. GitHub highlighting waits. |
| B6 | Columns: lookup and shape errors have none | E4; the server widens a zero column to the first token of the line from edit-mode spans |
| B7 | Re-checking the whole project on each keystroke | Parsing runs at 120 MB/s, so a 2 MB project re-checks in about 20 ms. Debounce at 150 ms, and serve unsaved buffers through the reader hook. Measure on the largest stack's data before optimizing anything. |
| B8 | Live values in the editor need a running game; the server must not hang or lie when there is none | Live features appear only while a loopback console answers. Each hover says "live" or "from files", and the console is polled with a 50 ms timeout. |
| B9 | A JSON-RPC layer in C++ | The server repository may take a permissive JSON library (MIT). Baker's core stays free of it. |

### Refined plan

**Repository.** `baker-editor`, MIT. It holds:
- `tree-sitter-baker/`;
- `server/` (C++20, links `baker::baker`, built against a Baker release);
- `vscode/` (the extension, with the TextMate grammar).

**Steps.**
1. **In Baker.**
   - E4 (columns), with a parse benchmark before and after.
   - The project file convention: documented in the cookbook, read by an example in
     `stacks/base`.
   - E1 and E3 already exist from plan A.
2. **Diagnostics.** Unsaved buffers through the reader, `check` with the reference's
   stubs, shape validation, and the one-way rule. Errors map to LSP ranges.
3. **Hover and completion.**
   - Hover shows the shape field's type, range, `desc` and `note`, the path, and
     `explain` from the files.
   - Completion offers shape fields at a keyed table, function names with signatures,
     macros, and pack names in `requires`.
4. **Go-to-definition.** Macros, script functions (E3's definition sites), packs, and the
   layer that supplies a value: from `local.dta`'s override, jump to the `default.dta`
   line it shadows.
5. **Live.**
   - While a game answers on loopback, the server sends `{config.explain p}` for the
     hovered path and shows an inlay with the live value.
   - A code action, "Write the live value here", calls `{config.write_back p}`. That is
     the game's own write-back, not an edit made by the editor, so provenance stays
     exact.
6. **tree-sitter grammar.** It goes to the Neovim, Helix and Zed grammar registries.

**Acceptance.**
- A syntax error shows within 200 ms of the keystroke, at the right column, on the
  largest stack's data.
- Hover on a value overridden in `local.dta` shows both layers with their lines.
- Completion offers every field of the enclosing shape.
- With Duel running, hover shows the live value, and "Write the live value here" leaves
  a one-line diff.
- With no game running, nothing claims to be live.

**Kill criterion.** If the project-file convention cannot describe a stack's setup
without the stack's `main` changing in ways its authors reject, stop at diagnostics with
an explicit command-line configuration.

**Effort.** E4 2 days; server through hover and completion about 2 weeks; live and
definition about 1 week; grammar about 3 days.

---

## 4. Plan C: Baker for Godot

### Goal and fit

- **The goal.** A Godot 4 game gains what the engine lacks in this space:
  - ordered layers (`default`, `local`, `agent`, experiments) with provenance;
  - a value tuned while playing written back to its line, instead of being discarded
    when the game stops;
  - an agent harness through plan A.
- **The fit.** It is the clearest demonstration, in a popular open engine, that values
  found by playing can reach version control. Godot's license is MIT, so no terms cross
  in either direction.

### First draft

- **The extension.** A GDExtension with a `Baker` singleton owning a Config, `.dta` as a
  Godot resource type, and a GDScript API (`Baker.get_value`, `Baker.tweak`).
- **The editor.** A dock with layers and tweaks, and an inspector plugin showing file and
  line.
- **Write-back.** From the running game, directly to `res://`.

### Critique

| # | Finding | Change |
|---|---|---|
| C1 | The one-line, one-load read is lost in GDScript: every `Baker.get_value` is a call through Godot's variant dispatch | The main pattern is a `BakerBinding` node. It binds properties of its parent to paths and assigns them on bind and on reload, so a GDScript read is a plain property read with no per-frame cost. `Baker.tweak()` stays for one-offs and caches its value. C++ GDExtension code uses `Tweak<T>` directly. |
| C2 | Godot already has an inspector and `@export`. If the demonstration does not show what Godot lacks within five minutes, this is a curiosity. | The demonstration script is fixed before any code: (1) play; (2) tune a jump height in the remote inspector; (3) stop; (4) the dock lists the change, with "Keep" writing a one-line diff to `tuning.dta`; (5) toggle an experiment layer while playing. |
| C3 | Writing to `res://` from the game works only when it runs from the editor, and races the editor's own file handling | The game sends its journal over Godot's debugger channel. The editor process writes back through Baker's writer hook and lets the editor's file system notice the change. Exported games never write. |
| C4 | Overlap with Godot's own resources: tuning duplicated in `.tres` and `.dta` | The scope is tunables and rules, never scenes. Bound properties read their defaults from the scene, so a path missing from the data leaves the scene's value standing, as a tweak's default stands. |
| C5 | godot-cpp is pinned per Godot minor version, so maintenance grows with every version supported | Support one version line (4.5 or the current stable), with CI on Windows and Linux. A new minor version is a deliberate update. |
| C6 | Godot may load resources on worker threads, but a Config is changed from one thread | The `.dta` resource loader returns only bytes. The Config lives on the main thread. Reload is polled once a frame in debug builds; under Baker's shipping switch it is inert. |
| C7 | Runtime-made tweaks need paths that outlive them | E6 |
| C8 | Reviewing a value's source in the inspector needs the chain, not the winner | E1 |
| C9 | Agents: a Godot game should get plan A's tools for free | The singleton registers `bind_config_tools` (E2) and runs the loopback console in debug builds. Godot's `--headless` gives the runner. Plan A's evaluation adds one Godot task. |

### Refined plan

**Repository.** `baker-godot`, MIT, from the template.
- The license notice says Godot is its owners' and endorses nothing.
- It contains no Godot engine source, only godot-cpp, as the GDExtension route requires.
- The sample is built from scratch rather than adapted from a Godot demo project, so the
  repository holds only its own code and Godot's binding layer.

**Pieces.**
- **The `Baker` singleton** (an autoload):
  - owns the Config and a Context;
  - has a reader over Godot's file access (`res://`, `user://`);
  - takes its layers from `res://baker/project.dta`, plan B's project file;
  - uses a `local` layer in `user://` that version control ignores.
- **`BakerBinding`** (a node): `bindings = {"jump_height": "player.jump.height", ...}`.
  It shows in the editor's inspector with each path's source.
- **The editor plugin:**
  - a dock with Layers (toggles, effect), Tweaks (all bound paths, with source), and
    Session (the running game's journal with Keep and Discard);
  - an inspector plugin that shows a bound property's layer chain from E1.
- **Debugger messages** carry the journal and live edits between game and editor.
- **The sample game**, about 300 lines of GDScript: a platformer with ten tunables, one
  experiment layer, and a headless scenario for plan A.

**Steps.**
1. **Runtime.** The singleton, the reader, layers, `BakerBinding`, reload, and the
   sample.
2. **Editor loop.** The dock, the inspector plugin, the journal over the debugger
   channel, and write-back from the editor process. This is the demonstration in C2.
3. **Agents and shipping.**
   - The console and `bind_config_tools` in debug builds; plan A's bridge drives the
     sample headless.
   - Exported builds load text: caches are tied to pointer size and Baker's version, and
     text keeps exports simple.
4. **Requirements back to Baker.**
   - Anything this integration needs that Baker lacks becomes a requirement note,
     written in Baker's terms, in `baker-godot/requirements/`.
   - The owner brings it to baker-dev (`INTEGRATIONS.md`). E1, E2 and E6 are expected to
     cover the first round.

**Acceptance.**
- The C2 demonstration works start to finish on Windows and Linux.
- Edit-to-see median under 250 ms for a `.dta` edit in an external editor, read from
  `loop_stats()`.
- "Keep" writes one line with comments intact.
- A bound property's per-frame cost is zero: no Baker call per frame, checked with
  Godot's profiler.
- Plan A's bridge tunes the sample headless.

**Kill criterion.** If three Godot developers shown the C2 demonstration would not use it
over `@export` and the remote inspector, park the integration, keep the sample as a
cookbook entry, and spend the effort on Unreal instead.

**Effort.** Runtime about 1.5 weeks; editor loop about 2 weeks; agents and shipping about
1 week.

---

## 5. Plan D: published overlays

### Goal and fit

- **The goal.** A shipped game takes a versioned balance overlay, checked before it takes
  effect and bounded by a policy. It is applied at a safe moment, rolled back in one call,
  and promoted into source when it sticks. The publishing side works with any storage:
  a static HTTPS host first, then Nakama, then vendors' stores through private bridges.
- **The fit.** It is safe live ops, with the lessons of §4.3 of the landscape built in:
  - bounded changes;
  - automatic validation, because hotfixes skip QA;
  - version-checked rollback;
  - undo;
  - provenance of what is live.

  It also closes the last gap in the loop: a live value reaches version control.

### First draft

- **Repository.** `baker-liveops`: a publisher command-line tool that uploads `.dta`
  overlays with a manifest, and a client that polls, downloads and pushes them as a live
  layer.
- **Experiments and backends.** Experiment bucketing in the client, and bridges to
  Firebase, PlayFab and others.

### Critique

| # | Finding | Change |
|---|---|---|
| D1 | `live_layers` is all or nothing: an overlay could change anything, including script | E5: every overlay is pushed under a `LayerPolicy` the game declares (`data_only`, `scalars_only`, and the paths it allows) |
| D2 | Hotfixes skip QA, so the publisher must refuse what the client would refuse | The publisher runs the client's own check: `check_layer` against the base data the overlay targets, the same policy, and the shapes. Then it runs the stack's headless scenarios with the overlay as a layer (plan A's `run`), and refuses a publish that fails an objective. |
| D3 | A rollback or an overlay meeting a base it was not made for (Path of Exile 3.7.4) | An overlay is a pack: `(pack balance_2026_10 (version 3) (requires (game_data 1.4)))`. The client's check refuses one whose `requires` the base does not meet (D147). |
| D4 | Bucketing and statistics are services' work | Out. The client takes the overlay or the variant the host's own logic (or a vendor) chose. A vendor bridge turns the vendor's resolved key map into overlay text, in that bridge's repository. |
| D5 | Clients and servers on different overlays mid-match | The host applies an overlay only at a boundary it chooses: session start, a lobby. The client helper exposes "stage" and "apply" separately. Dedicated servers use the same helper and the same manifest. |
| D6 | An overlay from storage is untrusted input | Text only (`text_only`, D142), never a cache. Verified before Baker sees it: the manifest carries a content hash and an Ed25519 signature, checked by the helper with libsodium (ISC) in the integration repository. Baker's `Cipher` is not a signature, and Baker stays out of signing. |
| D7 | Rollback must restore exactly, including provenance | It already does (removing a layer restores every value and `source()`, M3). The plan adds a test with a shipping build. |
| D8 | Offline start, or the store unreachable | The helper keeps the last applied overlay in `user://`-style storage and re-applies it at start only if it still passes `check_layer`. Otherwise it starts on the base data. |
| D9 | A live value that sticks never reaches source, and the next patch silently drops it | The publisher's `retire` step writes the overlay's effect into the base data as a pull request (`promote` from a checkout), and marks the overlay superseded by the build that contains it |
| D10 | `BAKER_SHIPPING` compiles out tools: does it keep validators and `check_layer`? | Verify first. `check_layer` must exist in shipping, because it is what makes live layers safe there. It allocates only when an overlay arrives. |

### Refined plan

**In Baker.**
- **E5**, available in shipping builds, and `LayerOptions::policy`.
- **A cookbook section, "Published overlays,"** in `docs/`:
  - the pack declaration convention for overlays;
  - stage and apply;
  - the policy;
  - provenance through `(origin "balance_2026_10" (tool baker-publish))`;
  - rollback;
  - retiring into source.
- **An example**, `examples/live-ops`, extended to fetch from a directory standing in for
  storage. It uses no network, as befits a Baker example.

**Repository.** `baker-liveops`, MIT. It holds:

- **`publish`** (a command-line tool):
  - `check` an overlay against a base checkout;
  - run the stack's scenarios with it;
  - write the manifest: name, version, requires, content hash, signature, policy name;
  - upload through a backend.
- **`client`** (a C++ helper, about 400 lines): fetch the manifest through a
  host-supplied transport, verify, `check_layer`, stage, apply at the host's boundary,
  keep the last good overlay, roll back, and report what is live.
- **Backends:**
  - `static/`: files under an HTTPS prefix, with any CDN or bucket.
  - `nakama/`: storage objects per environment, with a Nakama runtime RPC that returns
    the manifest. Its CI runs Nakama in a container.
- **`retire`**: a pull request that promotes the overlay into the base data.

**Private bridges, later.** Vendor stores such as PlayFab title data, Epic title storage
and Firebase Remote Config each get a private repository from the template. Each turns
the vendor's resolved values into overlay text plus a manifest. No vendor SDK reaches
`baker-liveops`.

**Steps.**
1. **In Baker:** E5, verified in shipping; the cookbook section; the example.
2. **In `baker-liveops`:** the client and the `static` backend, against Ledger, the
   economy stack, with a balance overlay.
3. **The publisher, with gates:** check, scenarios, signing.
4. **Nakama.**
5. **`retire`:** a pull request into the stack's data.

**Acceptance.**
- A signed overlay applies at a session boundary in a shipping build.
- `source()` names it and its version.
- An overlay that adds a command, touches a path outside its allow list, or requires a
  newer base is refused, with nothing changed.
- Rollback restores every value and `source()`.
- An overlay failing a Ledger objective is refused by the publisher.
- Applying a 10 KB overlay takes under 1 ms of main-thread time, measured, after
  download and verification.
- `retire` produces a pull request whose diff is the overlay's effect, line by line.

**Kill criterion.** None for the Baker side: E5 is worth having for mods and agents alone.
If no stack or user needs live ops within the plan's span, stop after step 2.

**Effort.** E5 and docs about 1 week; client and static backend about 1 week; publisher
about 1 week; Nakama and retire about 1 week.

---

## 6. Sequencing

| Phase | Work | Unblocks |
|---|---|---|
| 1 | E1 `explain`, E2 reply form and Config tools, E3 machine-readable reference | A; most of B and C |
| 2 | Plan A: `baker-mcp` and its evaluation on the stacks | Evidence for or against the agent route; a Godot target for C |
| 3 | E4 columns; plan B through hover and completion | Designers' editing; agents' precise fixes |
| 4a | E6; plan C | The Godot demonstration |
| 4b | E5; plan D (can run beside 4a) | Live ops; `baker_propose`'s gate gets stricter |
| 5 | Plan B's live features; plan D's Nakama and retire | The loop closed both ways |

Each Baker feature follows the usual rules:
- a short design note before code that changes the API;
- a D-entry with measurements;
- `tools/local_matrix.sh` green before each commit;
- the Acton review (`.claude/skills/acton-review`).

Each integration repository starts from `tools/integration-template/`. Needs found there
come back as requirement notes, not code.

## 7. What was considered and left out

- **Unreal Engine.**
  - The pitch is legible: console variables with file-and-line provenance and
    write-back.
  - The case against starting now: a commercial EULA, a private repository, a large
    engine around a small library, and `iteration-backgrounder.md`'s judgment that
    Unreal already covers most of the ground.
  - Revisit once E1–E3 exist, since an Unreal bridge would use exactly them.
- **Spreadsheet round-trip.** It is the runner-up. Over `origin` and write-back, a
  CSV-shaped export with a provenance column and an import that writes through the
  lossless writer would close the designers' oldest loop. It needs nothing beyond E1 and
  E3, so it can follow at any time.
- **Tracy.** A small adapter: reload records as messages, and edit-to-see as a plot.
  Worth an afternoon in the stacks' base, whenever a stack adds Tracy (`game-stacks.md`
  §2.1).
- **flecs, Lua, Unity, O3DE, OpenFeature.** Low mission value for their cost, or blocked
  by immaturity (OpenFeature's C++ SDK). See the landscape's §7.

## 8. The owner's decisions

Recorded in D153:
- Start with E1–E3, then plan A.
- Every integration, the MIT-only ones included, lives in its own repository.
- E2's reply form and `bind_config_tools` ship in the published library.
