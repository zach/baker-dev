# The Tool Surface: `explain`, the Reply Form, and the Machine-Readable Reference

A design note for E1–E3 of `dev/integration-plans.md` §1, the first step D153 chose. These
three give tools, editors and agents what they now get by scraping text: a value's whole
layer chain, replies they can parse, and the vocabulary as data. Plan A (the agent bridge)
builds on them, and so do plans B and C.

Oct 15, 2026. Agreed with the owner (§8). Results and deviations go into
`dev/decisions.md` as each feature lands.

---

## 0. Scope and order

| Step | Feature | Library | Estimate |
|---|---|---|---|
| 1 | E1 `Config::explain` | `baker::data` | ~130 lines |
| 2 | A JSON writer shared by data, script and shape (moved, not new) | `baker::data` (internal) | ~+20 net |
| 3 | E2a `eval_request` and the reply form `baker.reply/1` | `baker::script` | ~150 |
| 4 | E2b `bind_config_tools` | `baker::script` (it links `baker::data` already) | ~220 |
| 5 | The example console and `stacks/tools/console.py` move to E2 | examples, stacks | — |
| 6 | E3 `write_reference_json`, `write_json` for tweaks and for shapes | script, data, shape | ~300 |
| 7 | The stacks' game binaries gain `reference --json` and `explain` | stacks | — |

The core is at 11,989 of 15,000 lines (`tools/count_lines.py`), and the shape library is
at 466 of 600. These steps add about 800 to the core and 50 to shape.

Each step is one commit, green on `tools/local_matrix.sh` and clang-tidy, with its D-entry.

---

## 1. E1: `explain`

### The transform

- **Input.** A path, plus the Config's layers, scratch, and the tweak registry.
- **Output.** One entry per contributor to that path, bottom to top.
- **When.** On demand, from a tool: a hover, an agent's question, an operator asking
  what is live. It is never called per frame.
- **Size.** Real stacks have 3–8 layers and paths 2–5 segments deep, so it does about
  40 resolves.

### The API

```cpp
// How one contributor stands in the merged value.
enum class Standing : uint8_t {
    Wins,      // supplies the value get() returns
    Shadowed,  // supplies a value that a higher contributor overrides or removes
    Removes,   // (key #remove) at the path or an ancestor: below it, the key is gone
    Disabled,  // a disabled layer that supplies the path: what toggling it on would bring
};

struct ExplainEntry {
    Symbol layer{};  // a layer's name, "scratch", or "code" (a tweak's default)
    Node value{};    // packed as LayerEffectEntry::value; the symbol #remove for Removes
    Source source{};
    Standing standing = Standing::Wins;
};

// Every contributor to `path`'s value, bottom to top: the code default of a tweak at the
// path, each layer whose tree holds a value there (or removes it), and scratch.
[[nodiscard]] Expected<List<ExplainEntry>> Config::explain(const Path& path) const;
```

### Rules

1. **Walk the contributors from the top down:** scratch, then the layers from the top, then
   the code default.
   - The first enabled contributor with a value **Wins**.
   - The first enabled contributor with a removal instead **Removes**, and then none wins.
   - Every enabled contributor after either one is **Shadowed**.
   - A disabled layer is **Disabled**, whatever lies above it.
   - The result is then reversed, so it reads bottom to top.
2. **What counts as a layer's value** is what `supplier()` already decides, so the winner
   is the layer `source()` names. A test checks this for every leaf of the M3b readiness
   config and of the content-update scenarios.
3. **An ancestor's removal.** `(cheats #remove)` removes `cheats.god_mode`. The layer holding
   the marker is listed as Removes, with the marker's line, for every path beneath it.
   - The marker is found with the same resolve, a segment at a time, so no new tree walk
     is needed.
4. **The code default.** It appears when the registry holds a tweak at the path, bound or
   not. It **Wins** only when nothing above supplies the path. That answers "why is it
   4.0?" with "the code's default stands".
   - This needs one new virtual on `TweakBase`, `default_values(ValueList&)`. The
     vtable exists already (`values`), and the read path (`operator T`) is not virtual
     and does not change.
5. **A path that names a subtree, not a value,** is refused with a located error: "'boss'
   holds arrays, not values: explain one of its leaves". `layer_effect` and `diff` cover
   subtrees.
6. **Positional segments** (`phase.2`) resolve as `source()` resolves them.

### Cost

- **Path class:** tool. It allocates its result and does about 40 resolves, a few
  microseconds.
- **Shipping:** available. `source()` is, and an operator asking what an overlay changed
  is exactly the live-ops question.

### Tests

These go in a new `tests/data/test_explain.cpp`, with layers read through an in-memory
reader:
- the three-layer override;
- a disabled layer;
- scratch over everything;
- a direct removal and an ancestor removal;
- a layer above a removal setting the key again;
- a tweak's default, alone and shadowed;
- an index path;
- the subtree refusal;
- the agreement property with `source()` and `get()`.

---

## 2. A JSON writer, written once

- **Where it is now.** `reference.cpp` holds the escaping and list helpers for
  `baker.check/1`, in an anonymous namespace in `baker::script`.
- **Who needs them.** E2 and E3 need the same helpers in script, in data (the tweaks)
  and in shape.
- **The move.** They go to an internal header and source in `src/data` (`json.hpp`, not
  public), which script and shape link through `baker::data`.
- **What it is.** A writer only: escaped strings, numbers, and the comma and bracket
  bookkeeping.
- **What it is not.** Baker gains no JSON *parser*. Requests stay in Baker's own text.
- **The check that nothing changed.** The check output's tests pin its bytes.

---

## 3. E2a: `eval_request` and `baker.reply/1`

### The transform

- **Input.** One line of text from a tool.
- **Output.** One line of JSON.
- **When.** Per request, on the Context's thread, at a point the host chooses: the
  console polls once a frame.

### The API

```cpp
// Evaluates one request and writes one JSON line (schema baker.reply/1, docs/reply-json.md).
// A request is (ID {command}): ID an int, symbol or string, echoed back. Any other line
// is evaluated whole, with a null id.
void eval_request(Context& ctx, std::string_view line, TextSink reply);
```

### The reply

```json
{"schema":"baker.reply/1","id":7,"ok":true,"text":"(source (layer default) (file \"d.dta\") (line 3))","value":{"layer":"default","file":"d.dta","line":3}}
{"schema":"baker.reply/1","id":8,"ok":false,"error":{"code":"Type","file":"<console>","line":1,"path":"","message":"...","stack":[{"function":"grow","on":"bob","file":"objects.dta","line":12}]}}
```

- **`text`** is the printed Baker form. It round-trips, and it is the authority.
- **`value`** is a JSON projection, lossy by design. Its rules:
  - ints and floats become numbers, and a non-finite float becomes `null`;
  - symbols and strings become strings;
  - a variable becomes `"$name"`, a function its name, an object its name;
  - **a keyed array becomes an object:** an array whose children are all arrays tagged
    by distinct symbols becomes `{tag: rest}`, where a rest of one value is that value
    and an empty rest is `null`;
  - any other array becomes a JSON array, tag included.

  The keyed-array rule is what makes `(source (layer default) (line 3))` read as
  `{"layer":"default","line":3}` to a client. Lists whose order matters, such as
  `explain`'s entries, are written as arrays of keyed arrays, so their order survives as
  a JSON array.
- **Malformed requests.** A request that does not parse replies `ok: false` with code
  `Parse` and a null id. It never crashes, and the fuzz harness gains this entry point.

### Cost

- **Path class:** tool. It allocates per request (the parse, the reply text) through the
  Context's allocator.
- **Nothing on the evaluator's path changes.** `bench_call` should not move; it is run to
  confirm.

---

## 4. E2b: `bind_config_tools`

### The API

```cpp
struct ConfigToolOptions {
    bool allow_write = false;  // also bind config.write_back and config.promote
};
// Registers the Config's tooling as script functions, documented, so the reference lists
// them and check() checks calls to them. The Config must outlive the Context's use of them.
[[nodiscard]] Expected<void> bind_config_tools(Context& ctx, Config& cfg,
                                               const ConfigToolOptions& options = {});
```

### The functions

Each result is Baker data, so it prints as text and projects to JSON by §3's rules.

| Function | Returns | Shipping |
|---|---|---|
| `{config.get path}` | the value | yes |
| `{config.source path}` | `(source (layer L) (file F) (line N) [(origin O) (tool T)])` | yes |
| `{config.explain path}` | `(((layer L) (value ...) (standing wins) (file F) (line N)) ...)` | yes |
| `{config.layers}` | `(((layer L) (enabled 1)) ...)`, bottom to top | yes |
| `{config.effect layer}` | `layer_effect` as keyed entries | yes |
| `{config.diff a b}` | `diff` as keyed entries | yes |
| `{config.set path values...}` | the new value; writes scratch | no |
| `{config.toggle layer}` | the new enabled state | no |
| `{config.reload}` | 1 if something changed (trigger `console`) | no |
| `{config.unsaved}` | the unsaved paths | no |
| `{config.journal [since_ns]}` | journal entries since a time | no |
| `{config.loop}` | `loop_stats` | no |
| `{config.snapshot name}`, `{config.restore name}` | named snapshots held by the tools | no |
| `{config.write_back path}`, `{config.promote from to}` | a `WriteReport` as data | no; only with `allow_write` |

**Shipping.** Under `BAKER_SHIPPING` only the "yes" rows are bound. The others are
inert or compiled out in a shipping Config anyway, and an agent should see "no such
function" rather than an inert success.

**State.** The named snapshots live in one block the bound functions share. It is
allocated through the Context's allocator and freed with the last of them;
`baker_alloc_tests` covers it.

**Paths.** A path argument is a symbol or a string (`player.speed`, or `"phase.2"`), parsed
by `Path` as `get` parses it.

### Tests

`tests/script/test_config_tools.cpp`:
- every function against a mock-host Config;
- the writers absent without `allow_write`, and present and writing through a mock writer
  with it;
- the shipping set under `if constexpr (kShipping)`;
- `check` finding a wrong-arity call to `config.get`;
- the reference listing them.

---

## 5. The console and its client

- **`examples/remote-console`.**
  - A line in request form gets a `baker.reply/1` line.
  - A bare line keeps today's human-readable reply (`=> value`, or the error and its
    stack, then an empty line), so the example stays usable with `nc`.
  - It calls `bind_config_tools` when it is given a Config.
- **`stacks/tools/console.py`** sends the request form and reads JSON. Its exit codes stay
  as they are: 1 on an error reply, 2 when nothing listens.
- **The game binaries** register the Config's tools in dev builds, so the agent's console
  can ask `{config.explain p}` of a running game.

---

## 6. E3: the machine-readable reference

### The API

```cpp
void write_reference_json(const Context& ctx, TextSink out, const LoadSession* packs = nullptr);
void write_json(const TweakRegistry& tweaks, TextSink out);   // <baker/tweak.hpp>, baker::data
void shape::write_json(const shape::Set& shapes, TextSink out);  // <baker/shape.hpp>
```

There are three documents, so that `baker::script` does not depend on `baker::shape`, and
so that a data-only host can write its tweaks. Each one is deterministic, sorted by name,
and versioned like `baker.check/1`.

- **`baker.reference/1`:**
  - `functions`: name, tier, status, replacement, doc, `file` and `line` for a script
    function (already recorded on `Function`), and `overloads`. Each overload has
    `params` (`type`, `kind`: required, optional or rest, and `raw`), `result`,
    `min_args` and `max_args`.
  - `types`: name, bases, methods (with overloads, status, doc and message), properties
    (name, type, doc) and script handlers.
  - `script_classes`.
  - `globals`.
  - `packs` and `origins`, when a session is given: the same fields as `check`'s.
- **`baker.tweaks/1`:**
  - path, type, default (text), current value (text), bound;
  - range, step, desc, kind, group, dev_only;
  - source, when bound.

  `type` needs the tweak's type name. `Tweak<T>` records it at construction from a small
  trait: `float`, `double`, `int`, `int64`, `bool`, `symbol`, `string`, else `value`.
- **`baker.shapes/1`:** per shape, name, `at`, keyed, and fields. Each field has name, type,
  element, shape, optional, range (when ranged), step, kind, desc, note, file and line.

### Not in it

- **JSON Schema.** It would flatten ranges with steps, `kind`, keyed tables and
  provenance. A tool can derive it from `baker.shapes/1` later, outside the core.
- **Macro definition sites.** The session attributes a macro to its pack (`macro_pack`)
  but keeps no line. Plan B's go-to-definition will want one. That is a separate, later
  change with its own D-entry, because it touches the load path.

### Docs and tests

- **Docs.** `docs/reference-json.md` documents all three schemas (it ships), and
  `docs/reply-json.md` the reply form.
- **Tests:**
  - byte-stable output across two runs;
  - every built-in present, with its overloads;
  - a script function's file and line;
  - a type's methods, properties and handlers;
  - tweaks of each recorded type;
  - a keyed shape;
  - the packs section matching `check`'s.

---

## 7. Review (acton-review)

### 1. Path classes

- **Tool:** all of E1, E2 and E3.
- **Hot:** nothing new. Two cold additions touch existing types, `TweakBase`'s
  `default_values` and a type name recorded beside it. Neither is on the tweak read,
  which stays one load. The codegen test (`tests/codegen`) proves it again.

### 2. The transforms

They are stated in §1, §3 and §6. Real values: 3–8 layers, 2–5 segments, replies under a
kilobyte except whole-layer effects.

### 3. Hot-path rules

- No hot path is touched.
- `bench_call` and `bench_lookup` run before and after anyway, as cheap insurance against
  a layout change from the moved JSON code.

### 4. Public headers

- New signatures use `Symbol`, `Node`, `List`, `TextSink`, `std::string_view` and
  `Expected`. No standard containers.
- `check_headers.py` and the alone-compile gate cover them.

### 5. Allocation and ownership

- Everything allocates through its owner: the Config's allocator for `explain`, the
  Context's for replies and the tools' state.
- `baker_alloc_tests` gains a Context with the tools bound, and a Config answering
  `explain`.

### 6. Shipping

- `explain`, the reply form and the reference stay.
- Only the read-only tools bind (§4).
- Write-back and live edits stay compiled out, as they are.

### 7. Removability

- A host opts in by calling `bind_config_tools` or `eval_request`. A host that calls
  neither pays nothing but code size in a static library, and the linker drops it.

### 8. Dependencies

None. A JSON writer, no parser.

### 9. "Where there is one, there are many"

- An editor hovering one path calls `explain` once.
- An agent wanting a whole layer uses `config.effect`.
- A batch `explain` over a prefix is not built until a caller shows it is needed.

### 10. Findings

- **None blocking.**
- **One to watch:** the moved JSON helpers must leave `baker.check/1`'s bytes unchanged,
  which the check tests pin.

---

## 8. The owner's decisions

Made on Oct 15, 2026, each as recommended:
1. **The console:** request-form lines get JSON, and bare lines keep the human-readable
   reply.
2. **The projection:** keyed arrays become JSON objects; `text` stays the authority.
3. **Shipping:** the read-only tools bind, and the rest do not.
