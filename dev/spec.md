# Baker — Handoff Specification

A small, MIT-licensed, modern C++ data + scripting system in the spirit of Harmonix's
DataArray (DTA), as described in Eric Malafeew's GDC 2005 talk *Data-Driven Programming
Made Easy*.

**Audience:** Claude Code, implementing from scratch.
**Status:** greenfield. No existing code.

**Provenance.** The system being reimplemented shipped in Frequency, Amplitude, Karaoke
Revolution, EyeToy: AntiGrav, Guitar Hero, and the entire Rock Band line. Its config files
are still the seam the Rock Band modding community works through twenty years later, and
its song-metadata format is what made downloadable content and the Rock Band Network
tractable. It was described publicly exactly once, in a 60-minute conference talk, by one
engineer, and then not written about again. Build it like the primary source it is.

---

## 0. Read this first

### What we are building

A library that lets a C++ program be driven by Lisp-ish data files. The headline
ergonomic goal, stated by the original talk and non-negotiable here:

> Replacing a hardcoded constant with a configured, hot-reloadable, designer-editable
> value should cost one line and no ceremony.

Everything else in the design serves that, plus the scripting layer that grows out of it.

### What we are *not* building

- Not a general-purpose scripting language. No modules, no classes-as-first-class-values
  beyond what is specified, no stdlib.
- Not a serialization framework for arbitrary C++ types.
- Not a game engine, UI system, or asset pipeline.
- Not bit-compatible with Harmonix's shipped `.dtb` binary format (see §9, stretch goal).

### Legal constraints — important

The output must be cleanly MIT-licensable.

- **Do** work from the public GDC 2005 slide deck (digested in Appendix A), from public
  community documentation of the DTA format, and from real `.dta` text files used as test
  corpora.
- **Do not** copy, paste, transliterate, or closely paraphrase code from any Harmonix
  engine decompilation (`DarkRTA/rb3`, `freeqaz/dc3-decomp`, `freeqaz/rb3-xenon`,
  `freeqaz/milo-native-engine`). Those are reconstructions of proprietary source with
  unclear licensing. They may be consulted *only* to answer factual questions about
  observable file-format behavior, and any such consultation should be noted in the commit
  message.
- Third-party code we vendor must be MIT/BSD/Apache-2.0 and recorded in `THIRD_PARTY.md`.

### Naming

Library **Baker**, namespace `baker`, CMake target `baker::baker`, headers under
`<baker/...>`, build switches prefixed `BAKER_`.

---

## 1. The data language

Text format, UTF-8, `.dta` by convention.

```
; a comment

(menu
  ('bacon and eggs'
    (price 12.75)
    (calories 120))
  ('fruit and cereal'
    (price 11.75)
    (calories 12)))
```

### Node types

| Type       | Syntax            | Payload                               |
|------------|-------------------|---------------------------------------|
| `Int`      | `42`, `-7`, `0x1F`| `int32_t`                             |
| `Float`    | `12.75`, `1e-3`   | `float`                               |
| `Symbol`   | `price`, `'odd sym'` | interned pointer                   |
| `String`   | `"bacon"`         | refcounted immutable string           |
| `Array`    | `( ... )`         | `Ref<Array>`                          |
| `Command`  | `{ ... }`         | `Ref<Array>`, evaluates when accessed |
| `Property` | `[ ... ]`         | `Ref<Array>`, property-path lookup    |
| `Var`      | `$name`           | pointer to a variable slot            |
| `Func`     | —                 | bound native function                 |
| `Object`   | —                 | non-owning `Object*`                  |

`Int`/`Float` are deliberately 32-bit: it matches the source format, keeps the node at 16
bytes, and 64-bit numerics are not what this system is for. Provide
`Node::as<int64_t>()`-style widening accessors; do not widen the storage.

### Directives

Handled by the parser/preprocessor, not at runtime:

- `#include "path"` — splice another file.
- `#require "path"` — include once per load session; subsequent `#require`s of the same
  canonical path are no-ops. This is how harness packs (`harness-tree-vision.md` §6) are
  loaded: several task harnesses can depend on the same game-level pack without
  redefining its macros. Macro and function names in packs use dotted prefixes by
  convention (`boss.restart_phase`, `econ.drop_rate`); the generated reference and tuning
  panels group by prefix. The library enforces nothing about prefixes.
- `(pack name (layer L) (requires ...))` as the first form of a file declares it a harness
  pack in layer `L`, from an ordered, user-defined list of layer names
  (`cfg.layers({"engine", "game", "task"})`, hardest first). Declared packs let `check`
  enforce the one-way rule (§5, Load-time checking). Undeclared files are unconstrained,
  so small projects pay nothing. The declaration may also carry optional harness
  metadata — `(owners "td-encounter" "td-combat")`, `(for designers)`, and
  `(stage genesis|custom|product|commodity)` — which the library stores, emits in the
  generated reference, and otherwise attaches no behavior to. The generated reference
  thereby doubles as the studio's harness register (`reading-a-studio.md` §7).
- `(origin "economy.xlsx" (tool sheets-export))` as a top-level form records that a file
  was generated by an external tool. Provenance (§4) carries the origin through to every
  value the file supplies, so `source()` can name the spreadsheet or tool a number came
  from, and the reference can list which external tools feed the config. This makes
  "fled harnesses" visible instead of silently competing with the official path.
- `#merge "path"` — recursive merge (§4) of another file into the current array.
- `#ifdef NAME` / `#ifndef NAME` / `#else` / `#endif`
- `#define NAME` / `#undef NAME`
- `[NAME node node ...]` at top level defines a **macro**: a named node sequence that is
  spliced wherever `NAME` appears as a bare symbol. Macros persist across files within a
  load session. Splicing shares the underlying array (refcount bump), it does not deep-copy.

```
[TRUE 1]
[RED 1 0 0]
[HANDLER (hit {fall_down})]
(object1 HANDLER)
(object2 HANDLER)
```

### Error reporting

Every parse and lookup error carries file, line, and the array path that led there:

```
baker: 'price' is float, not int  (menu.dta:3, at menu/'bacon and eggs')
```

Arrays retain their source file (interned `Symbol`) and line number. Retaining this is a
hard requirement, not a debug-build nicety — it is most of what makes the system pleasant
to use in anger.

### Edit-mode parsing and write-back

Two parse modes, after Butcher's "load for editing" versus "load for gameplay":

- **Gameplay mode** (default) produces the lean tree described above. Comments and
  whitespace are discarded.
- **Edit mode** additionally retains *trivia* — comments, whitespace, and the original
  token text and byte span of every node — in a side table owned by the loaded file, so
  the gameplay tree's node layout and size targets are unchanged.

Edit mode exists for one capability: **write-back**. A value tuned in the running game must
flow back to its source file at the location provenance already records, losslessly —
comments, `note` metadata, formatting, and macro structure intact — so that the result is
a minimal diff. `cfg.write_back("warden_fight.phase.2.cadence")` and
`kCadence.write_back()` replace exactly the token that produced the current value. If
that value came from a macro expansion, an overlay layer, or anything else without a
single literal token as its source, write-back refuses with a located error naming what it
would have had to edit; it never guesses. Writing to a different layer than the one that
supplied the value (for example, saving a tweak into `local.dta` instead of the shared
file) is an explicit, separate call.

Write-back's rules, in full (the calls themselves are in §4, "Personal, experiment, and
scratch layers"):

- **A scratch-sourced value** (a value tuned live, held in the in-memory `scratch` layer)
  is written to the highest enabled non-scratch layer that supplies the path. On success
  the path leaves `scratch` and the patched file is reloaded (trigger `write_back`), so
  provenance then names that file and line.
- **Writing into another layer** (`write_back_to`) replaces the target file's literal token
  if it has one. Otherwise it inserts: it finds the deepest ancestor array literally present
  in that file and adds the missing entries as its last child, indented like the previous
  sibling, in the pretty-printer's form. No other byte changes. If the target layer is
  optional and its file is absent, the file is created.
- **Refusals are always located.** Write-back refuses when a positional segment (such as
  `phase.2`) does not exist in the target; when the ancestor array it would insert into
  came from a macro, `#include`, or `#merge` rather than the file itself; and when the file
  on disk changed since it was loaded (compared by a content hash recorded at load).
- Every write returns a `WriteReport` listing each file and line written.

This is the hinge of direct online discovery: values found by playing must reach version
control without being transcribed by hand. See `harness-tree-vision.md` §4.

### Robustness principle

**Nothing typed into a data file, and no corrupt file on disk, may crash the host program.**
Every atomic value is bounds-checked as it is parsed; every structural assumption is
checked before it is relied on; malformed input produces a located error and nothing else.
Bungie held their tag loader to this standard because artists edited tags directly, and it
is the right standard here for the same reason — these files are edited by hand, by people
who are not thinking about your invariants. The fuzzing requirement in §8 is how this gets
verified rather than asserted.

---

## 2. Memory layout and ownership

Non-negotiable targets, enforced by `static_assert` and a test:

- `sizeof(Node) == 16`, `alignof(Node) == 8`.
- `Array` header ≤ 24 bytes, with its `Node`s allocated **inline in the same allocation**
  (trailing flexible array). One allocation per array, contiguous children, no
  `vector<Node>` indirection.
- `Ref<T>` is a non-atomic intrusive refcount by default; an
  `BAKER_ATOMIC_REFCOUNT` option switches to atomic for multithreaded loading. Document
  the default as single-threaded-mutation, multi-thread-read-after-freeze.

```cpp
class Node {
public:
    Node() noexcept;                       // Type::Int, 0
    Node(int32_t) noexcept;
    Node(float) noexcept;
    Node(Symbol) noexcept;
    Node(Ref<Array>) noexcept;
    // ... etc

    [[nodiscard]] Type type() const noexcept;

    // Throwing, checked accessors. Message includes expected/actual type + source loc.
    [[nodiscard]] int32_t     as_int()   const;
    [[nodiscard]] float       as_float() const;
    [[nodiscard]] Symbol      as_sym()   const;
    [[nodiscard]] std::string_view as_str() const;
    [[nodiscard]] const Array& as_array() const;

    // Non-throwing mirrors.
    [[nodiscard]] std::optional<int32_t> try_int() const noexcept;
    // ...
private:
    union { int32_t i; float f; const void* p; } value_;
    Type  type_;
    // 3 bytes padding available — do not spend them without a reason.
};
```

Numeric accessors coerce Int↔Float freely (an `(x 1)` should satisfy a float read); all
other mismatches are errors.

Symbols are interned in a process-global, immortal, append-only table. Interning is
`O(1)` hashed; `Symbol` comparison is pointer comparison; `Symbol` carries a precomputed
hash. Provide `"price"_sym`. Because the table is immortal, document that symbol memory
is never reclaimed — that is the original design and it is correct for this use case.

---

## 3. Reading data

The API people will actually type all day. Keep it short.

```cpp
baker::Ref<baker::Array> menu = baker::read_file("menu.dta");

float price = menu->find("bacon and eggs")->find_float("price");
auto  maybe = menu->try_find("fruit and cereal");        // optional-ish

for (const baker::Array& item : menu->subarrays()) { ... } // ranges-compatible view
```

- `find(sym)` searches direct children that are arrays whose first node is `sym`. Linear
  scan with pointer comparison — that is the right default and the talk says so.
- `find_int` / `find_float` / `find_sym` / `find_str` / `find_array` are shorthand for
  `find(sym)->at(1).as_*()`.
- Every `find_*` has a `try_find_*` returning `std::expected<T, Error>` (fall back to a
  vendored `expected` shim under C++20; prefer `std::expected` when available).
- `find_*(sym, default)` overloads returning a default instead of erroring.
- Optional sorted-array mode: `Array::sort_for_lookup()` marks an array as sorted by tag,
  after which `find` uses binary search. Opt-in, never automatic.

Construction from C++ should be equally short:

```cpp
auto price = baker::array("price", 12.75f);              // (price 12.75)
auto eggs  = baker::array("eggs", price);                // (eggs (price 12.75))
```

Variadic factory, perfect-forwarded, one allocation sized exactly.

---

## 4. Merging, config, and the `Tweak` type

### Merge semantics

For each subarray of the incoming array, look for a child of the target with the same tag.
Insert if absent; recurse if present. Leaf values are overwritten by the incoming value.

```
(a (b 1))              ; original
(a (b 2) (c 2))        ; merged in
(a (b 2) (c 2))        ; result
```

(The slide's example prints `(a (b 1) (c 2))`, which is a slide typo — a merge whose
leaves don't win can't override anything, and the config override story on the following
slides depends on leaves winning. The talk audio doesn't clarify it either way.
Last-write-wins is confirmed as the intended behavior; leave a one-line comment in the
source noting the slide discrepancy for the next person who checks.)

### Config

```cpp
baker::Config cfg;
cfg.load("default.dta");
cfg.merge("app.dta");     // overrides
```

Lookup by dotted path: `cfg.get<float>("renderer.clear_color.0")`,
`cfg.get<Vec2>("renderer.screen_size")` via a user-extensible converter.

**Layers.** `Config` holds an ordered stack of named layers, merged bottom to top:

```cpp
cfg.push_layer("default",  "default.dta");
cfg.push_layer("platform", "ps5.dta");
cfg.push_layer("app",      "app.dta");
cfg.push_layer("local", "local/" + user + ".dta",  // per-user, never checked in
               {.optional = true});
cfg.push_layer("patch-2026-10-11", overlay_array); // a live-ops overlay
cfg.remove_layer("patch-2026-10-11");              // rollback
```

`load`/`merge` above are shorthand for pushing anonymous layers. Pushing, popping, or
removing a layer re-merges and fires the normal reload callbacks (§ Hot reload). A
removed layer leaves no trace. This gives `#merge` the operational wrapping that live
games need — the shape of Riot's micropatches and of Unreal's set-by hierarchy — without
any new semantics. See `iteration-backgrounder.md` §5–6.

**Provenance.** Every value lookup can report where its current value came from: layer
name, file, and line. `cfg.source("renderer.clear_color")` and `kBoxWidth.source()`
return it; error messages and the tweak registry surface it. "Why is this number what it
is?" is the most common question anyone asks a config system, and arrays already carry
file and line, so this is close to free.

### `Tweak<T>` — the one-line requirement

This is the centerpiece. It is what makes replacing a constant free.

```cpp
// Before:
const float kBoxWidth = 2.0f;

// After:
static baker::Tweak<float> kBoxWidth{"box.width", 2.0f,
    {.range = {0, 10}, .step = 1, .desc = "Box width"}};

float w = kBoxWidth;      // implicit conversion, one relaxed atomic load
```

Requirements:

- Constructor registers with the global (or injected) `TweakRegistry` at static-init time;
  registration is allocation-free and order-independent.
- If the config is already loaded, the value is bound immediately; otherwise it binds on
  the next `Config::load`/`reload`.
- If the path is absent from config, the default stands and the tweak is flagged
  `unbound` — never an error, never a crash, optionally a warning at load.
- `operator T() const` is a relaxed atomic load for trivially-copyable `T`; for
  non-trivial `T`, a `get()` returning `const T&` guarded by the reload epoch.
- Metadata (`range`, `step`, `desc`, `note`, `kind`, plus a `group`) is stored once and enumerable:
  `registry.each([](const TweakBase&){...})` is all a tweak UI needs. `note` is
  **data-file-only** and exists so whoever is tuning a value can leave a remark for the
  next person — "this doesn't work yet", "good values are 0.2–0.5". Bungie generated their
  editing UI from code definitions and specifically could not do this; being
  self-describing means we can, for free.
- `kind` is an optional presentation hint (`color`, `angle`, `percent`, `path`, …) for
  tools; the library attaches no behavior to it. When absent, the registry infers `color`
  for a 3- or 4-element float array ranged 0–1. Inferring presentation from shape is
  borrowed from SeExpr's editor, which does exactly this.
- `on_change(callback)` per tweak, and `Config::on_reload(callback)` globally, for the
  "notify dependent C++" case from the talk.
- A tweak may be marked `.dev_only = true`. Under `BAKER_SHIPPING` (below), dev-only
  tweaks ignore all data overrides and always return their code default — the equivalent
  of Unreal's `ECVF_Cheat`.
- Metadata may equivalently live in the data file, after the slide syntax with each tag
  marked by `@` (D248): `(width 2 (@range 0 10) (@step 1) (@desc "Box width"))`. Data-side
  metadata wins over code-side.

### Hot reload

`cfg.reload()` re-reads changed files, re-merges, rebinds tweaks, fires callbacks. Reload
granularity is per-file. Old arrays stay alive until the last `Ref` drops, so a reload
never dangles a pointer someone is mid-read on.

The original's stated convention, worth carrying into the docs: parameters should be
grouped under the code that owns them, and reading another subsystem's parameters is bad
form even though a global database makes it possible. We enforce nothing; we just say it
in `docs/cookbook.md`, because reload correctness depends on it — a group is the unit that
gets refreshed and notified together.

**Migrate or punt.** A reload callback returns one of two outcomes: it migrated its runtime
state onto the new values, or it punts — declares the state unsalvageable and asks to be
torn down and rebuilt from scratch. Punting is the default and must require no code at all;
migration is what you write later, for the specific field someone complained about. This
is Bungie's reload contract and it is better than a bare change notification, because it
makes hot reload useful on day one rather than after every subsystem has been taught to
migrate. Provide `Config::on_reload` returning `Reload::Migrated` or `Reload::Punt`, and a
`rebuild` hook the punt path calls.

### Reload telemetry

Report how long each reload took and where the time went, so a studio's loop dashboard can
show Baker's share of edit-to-see time (from saving a change in a tool to the running game
reflecting it) and catch regressions.

```cpp
namespace baker {

// Monotonic nanoseconds from an arbitrary origin.
// Default reads std::chrono::steady_clock; tests inject a fake.
using TimeSource = std::uint64_t (*)(void* user);

enum class ReloadOutcome : std::uint8_t { Ok, Unchanged, ParseError, Refused };

struct ReloadRequest {
    Symbol        trigger{};      // host tag: "watcher", "console", "livelink", ...
    std::uint64_t origin_ns = 0;  // when the edit was saved, in the TimeSource domain; 0 = unknown
};

struct ReloadRecord {
    std::uint64_t epoch;          // epoch after this reload (unchanged if nothing changed)
    Symbol        trigger;
    std::uint64_t origin_ns, start_ns, end_ns;
    std::uint64_t scan_ns, write_ns, read_ns, parse_ns, merge_ns, bind_ns, notify_ns;  // write_ns: D132
    std::uint32_t files_reloaded, tweaks_changed;
    std::uint32_t migrated, punted;  // reload-callback outcomes
    Symbol        first_file;        // first changed file, for display
    ReloadOutcome outcome;
};

class Config {
public:
    void set_time_source(TimeSource fn, void* user = nullptr);
    bool reload(const ReloadRequest& request = {});      // keeps its return type; try_ twin too
    [[nodiscard]] std::optional<ReloadRecord> last_reload() const noexcept;
    template <class F> void each_reload(F&& fn) const;   // oldest to newest
    void set_reload_history(std::size_t capacity);        // default 256; allocates once
    void on_reload_complete(/* same callable form as on_reload */);
};

}  // namespace baker
```

- **Six stages, in order**, each timed once per reload: *scan* (find changed files),
  *read*, *parse* (preprocessor and edit-mode trivia included), *merge* (re-merge the layer
  stack), *bind* (rebind tweaks), and *notify* (reload callbacks, including punt and
  rebuild hooks).
- Layer pushes, pops, removals, toggles, and `restore()` also produce records, with
  trigger `layer` or `restore` and zero scan, read, and parse time.
- A reload that finds no changed files records `Unchanged`. A parse failure records
  `ParseError`, keeps the previous tree, and leaves bind and notify at zero.
- When the host supplies `origin_ns`, the substrate's edit-to-see time is
  `end_ns - origin_ns`; otherwise hosts compute it from the absolute `start_ns` and
  `end_ns`. Clock domains across machines (a PC tool and a devkit) are the host's problem.
- History is a fixed-capacity ring, allocated once; when full, the oldest record is
  dropped. Telemetry adds no allocation per reload once the ring exists.
- `on_reload_complete` fires after notify with the finished record, so a host can forward
  it to telemetry without polling.
- Under `BAKER_SHIPPING` the timing code compiles out; `last_reload()` returns empty and
  `each_reload` visits nothing, so hosts need no `#ifdef`s.

The library still owns no clock: the time source is injectable and only defaults to
`steady_clock`.

### Personal, experiment, and scratch layers

Let people experiment without touching shared files or blocking each other, compare
variants A/B during polish, and move a winning experiment into shared data as a reviewable
diff.

```cpp
namespace baker {

struct LayerOptions {
    bool optional = false;  // a missing file is not an error; the layer exists, empty
    bool enabled  = true;   // disabled layers keep their position but are skipped by merge
};

struct LayerEffectEntry {
    std::string           path;          // dotted path, as get<T>() takes it
    Node                  value;         // this layer's value
    std::optional<Node>   below;         // the stack's value without this layer, if any
    Source                source;        // provenance, as source() returns it
    std::optional<Source> below_source;
    bool                  shadowed;      // a higher enabled layer overrides this path
};

struct DiffEntry {
    std::string           path;
    std::optional<Node>   a, b;          // empty = that layer does not set this path
    std::optional<Source> source_a, source_b;
};

class Config {
public:
    void push_layer(std::string_view name, std::string_view path, LayerOptions options = {});

    void set_layer_enabled(std::string_view name, bool enabled);
    bool toggle_layer(std::string_view name);              // returns the new state
    [[nodiscard]] bool layer_enabled(std::string_view name) const;

    // Tool edits land in the reserved in-memory "scratch" layer.
    void set(std::string_view path, Node value);
    void clear_scratch(std::string_view path = {});        // one path, or all of it

    [[nodiscard]] std::vector<LayerEffectEntry> layer_effect(std::string_view layer) const;
    [[nodiscard]] std::vector<DiffEntry> diff(std::string_view a, std::string_view b) const;

    // Write-back (§1). Each has a try_ twin; WriteReport lists every file and line written.
    WriteReport write_back(std::string_view path);
    WriteReport write_back_to(std::string_view path, std::string_view layer);
    WriteReport promote(std::string_view from_layer, std::string_view to_layer);
};

}  // namespace baker
```

- **Enable and toggle.** Changing a layer's state re-merges, advances the epoch, fires
  reload callbacks, and records telemetry, exactly as push and remove do. Provenance never
  names a disabled layer.
- **Personal layers** are a convention, not a mechanism. The host pushes a layer named
  `local` with `{.optional = true}` at a per-user path such as `local/<user>.dta`, above the
  shared layers and below live patches. The project keeps that directory out of version
  control. Baker never looks up user names.
- **Scratch.** Every `Config` has a reserved, in-memory layer named `scratch`, always
  topmost and created empty. Edits from tools, including UI edits made through the tweak
  registry, go there, so provenance stays honest and "my tuning versus the files" becomes a
  toggle. `pop_layer` skips scratch, and scratch is never written to disk directly.
- **`set` is a fast path.** Because scratch is topmost, `set` need not re-merge: it updates
  scratch, rebinds only the tweaks bound to that path, and fires only their `on_change`.
  Lookups consult scratch before the merged tree. Setting an existing scalar path
  allocates nothing. Toggling scratch itself is a full re-merge like any other layer.
- **Effect and diff** work on leaf entries as merge defines them. `layer_effect` answers
  "what does this layer actually change?"; `diff` compares two layers' own contents, for
  variant A against variant B. Both are tooling calls computed on demand; they may
  allocate and need not be fast.
- **Promote** plans every entry of `layer_effect(from)` into the target layer's file and
  validates all of them first. It writes only if every entry succeeds; on any refusal it
  writes nothing and returns all refusals. The source layer stays in the stack, and the
  caller removes it.
- Under `BAKER_SHIPPING`, toggles, `set`, write-back, and `promote` are inert and return a
  "shipping build" error. `layer_effect` and `diff` still work.

### Snapshot and restore

Give polish and QA an exact configuration to return to. Paired with a host's input
recording, this makes a captured moment replayable; it also makes A/B comparison of two
tuned states one call each.

```cpp
namespace baker {

class ConfigSnapshot;  // copyable; shares loaded arrays by Ref, so capture is cheap

class Config {
public:
    [[nodiscard]] ConfigSnapshot snapshot() const;
    void restore(const ConfigSnapshot& snapshot);
};

}  // namespace baker
```

- A snapshot holds the ordered layer list (names, options, enabled state), a `Ref` to each
  layer's loaded tree, each file's load-time content hash, and a copy of scratch. Loaded
  arrays are immutable and refcounted, so nothing is deep-copied.
- `restore` replaces the stack, re-merges, rebinds tweaks, and fires reload callbacks with
  migrate-or-punt as usual. It advances to a new epoch rather than reusing the old one, so
  any cached derived data invalidates.
- `restore` never reads disk. If a file changed after the snapshot, values still come from
  the snapshot, and write-back of them refuses under the content-hash rule (§1).
- Not captured: script variables, objects, tasks, and host state. Those belong to
  `baker::script` and the host.

### Loop telemetry and the edit journal

Baker sits under every harness in the tree, so it is the cheapest place to measure the
loop. Everything below is observational, allocation-bounded (fixed-size ring buffers), and
compiled out under `BAKER_SHIPPING`. The library still owns no I/O, clock, or threads:
the host supplies a time source (`cfg.set_time_source`, above) and reads results through
calls or callbacks. See `reading-a-studio.md` §2 and §9.

- **Loop statistics.** `cfg.mark_visible()`, called by the host when the next frame
  presents, closes the loop past what reload records cover. `cfg.loop_stats()` aggregates
  the reload records (§ Reload telemetry) into rolling medians and
  worst cases per file and per layer, with change-failure counts (reloads that failed to
  parse, callbacks that errored). This is DORA's change lead time and change-failure rate,
  measured for content where it actually happens.
- **Edit journal.** Every live change to a tweak or config value in a session — from a
  panel, the console, or `eval` — is appended to `cfg.journal()` with old value, new value,
  provenance, and whether it was later written back. `cfg.unsaved()` lists live changes
  never written back, so a host can warn at shutdown. This gives write-back its success
  metric: discoveries kept versus discoveries lost.

---

## 5. Scripting

Commands are `{...}` arrays: `{func args...}` or `{object method args...}`.

```
(object
  (height 10)
  (collide
    {play "bonk.wav"}
    {game add_score 10}))
```

### Evaluation semantics — read this twice

This is the cleverest part of the original design and the easiest thing to accidentally
throw away. **Executing a command does not evaluate its arguments.** The evaluator's whole
job is: look up the handler for slot 0, hand it the raw command array, done. Evaluation
happens later and only where a handler reaches for an argument *by type*.

The consequence is the important bit: **there are no special forms and no keywords.** `if`
is an ordinary registered function that reads argument 1 as a bool and then chooses
whether to touch arguments 2 and 3. A conventional interpreter has to make `if` a keyword
precisely because it eagerly evaluates arguments into a temporary list first. Here the
laziness falls out of the accessor design, which also means no temp lists, no recursive
pre-pass, and no allocation per call.

So the rules are:

- `Array::at(i)` returns the **raw** node. No evaluation.
- Typed accessors (`at(i).as_int()`, `args.get<float>(i)`, and every `find_*`) evaluate a
  `Command` or `Var` node and return the result. Accessing a node *as a command* does not
  evaluate it.
- Evaluating a `Var` is a pointer dereference and must stay that cheap.
- A registered function must be able to declare specific parameters as **unevaluated**
  (§"Native function binding"). `set`, `if`, and the task commands all need this.

The original called this "a little less flexible and correct than the LISP style of
evaluation" and reported only a handful of cases over five years where it bit them. That
is a good trade and we are taking it. What we add is that the raw/evaluated distinction is
*visible in the API* rather than implicit — `at()` versus `as_int()` — so a reader can see
which one they are getting.

### Evaluating text

`ctx.eval(std::string_view text)` parses and evaluates a command string and returns the
resulting `Node` or a located `Error` (the non-throwing twin is `try_eval`). It is the
entry point for a developer console, a remote command channel, and recorded test or repro
scripts — all of which are the same thing, since an s-expression command is already a
serialized, parseable, evaluable value and needs no separate protocol. The library owns no
sockets and no threads; `examples/remote-console` (M9) shows a TCP listener feeding
`eval` in under a hundred lines, the pattern Frykholm (Bitsquid) and Malafeew (Harmonix)
each described independently.

### Native function binding

No MFC-style macro tables. Deduce everything from the callable's signature:

```cpp
ctx.func("+",  [](int a, int b) { return a + b; });
ctx.func("print", [](std::string_view s) { std::puts(s.data()); });
ctx.func("variadic", [](baker::Args args) { ... });   // escape hatch, raw nodes
```

`func` inspects the callable via a `signature_of` trait, builds a marshalling shim that
converts each `Node` to the parameter type through `Convert<T>` (an ADL-findable
customization point, specializable by users for their own types), calls it, and converts
the return. Arity and type mismatches produce a `baker::Error` naming the function, the
argument index, and the call site's file:line.

Parameter types supported out of the box: `int`, `float`, `bool`, `Symbol`,
`std::string_view`, `const Array&`, `Node`, `Object*` and `T*` for bound object types,
`std::optional<T>` for trailing optional args, `baker::Args` for the rest.

Three capabilities the original needed, all of which fall out of the signature if we let
them:

```cpp
// 1. Unevaluated parameters. baker::Raw<T> suppresses evaluation for that slot.
ctx.func("set", [](baker::Raw<baker::Var> v, baker::Node value) { v.store(value); });
ctx.func("if",  [](bool cond, baker::Raw<baker::Node> t, baker::Raw<baker::Node> f) {
    return (cond ? t : f).eval();
});

// 2. Overloading by argument type. The original's `+` did different things for
//    (int, int) than for (int, float).
ctx.func("+", [](int a, int b)     { return a + b; });
ctx.func("+", [](float a, float b) { return a + b; });   // registered as an overload set

// 3. Variadics, via baker::Args.
```

Overload resolution is first-match over the registered set, in registration order, on
runtime node types with Int→Float promotion allowed last. Ambiguity is a registration-time
error, not a call-time one.

**One namespace, three tiers.** Native functions, script-defined functions (`{func ...}`),
and macros share call syntax, so promoting a word from script to native never changes a
call site — the property that makes pushing up cheap (see `pushing-up.md` §5). The rules
for collisions:

- A native registration of a name already defined in script **supersedes** it, with a
  logged notice and a signature comparison; an arity or type mismatch warns.
- A script `func` definition of a name that is already native is an **error** unless
  written as `{override name (...) ...}`, which logs.
- Every registration records its **tier** (`native`, `script`, `macro`) and an optional
  **status**: `stub` (placeholder awaiting a harder implementation), `provisional`, or
  `deprecated` with a replacement name. Script functions take status from data:
  `{func boss.find_cover ($p) (status stub) ...}`. Native ones take it from the binder:
  `.status(baker::Stub)`. Tier and status appear in the generated reference.

**Handler caching.** The original bound the resolved handler directly into the command
node on first execution — safe because a function pointer never moves — at the cost of
requiring a script reload if a function was re-registered. Do the same, but make it
correct rather than a footgun: cache a resolved-handler pointer in the node, and bump a
registry generation counter on every `func` registration so a stale cache is detected and
re-resolved rather than silently wrong. Measure both paths; if the check costs more than
the lookup saves, drop the cache and say so in `dev/decisions.md`. *(Done: D44 replaces
the cache with per-Context tables indexed by symbol, which resolve a name in about a
nanosecond without modifying the tree.)*

**Object commands evaluate slot 0.** `{{nearest_object $bomb_position} suffer_damage}`
must work — the object slot is evaluated before dispatch, unlike the argument slots.

**Object references are salted handles, not pointers.** A `Node` of type `Object` stores a
32-bit handle: 16 bits of index into a per-`Context` object table, 16 bits of salt
regenerated on each registration. Dispatch checks index bounds and salt equality, so a
script holding a reference to a destroyed object produces a clean error naming the object
rather than a use-after-free. Borrowed from Bungie's datum arrays (see
`halo-tags-vs-dataarray.md` §5); it is the cheapest large safety win available to us.
Provide strong (`get`, errors on mismatch) and weak (`try_get`, returns null on mismatch)
resolution.

### Objects

CRTP + a static bind function. No macros in user code.

```cpp
struct Person : baker::Object<Person> {
    float height = 6.0f;

    float on_grow(float amount) { height += amount; tell_mom(); return height; }

    static void bind(baker::Binder<Person>& b) {
        b.inherits<Creature>();
        b.method("grow", &Person::on_grow);
        b.property("height", &Person::height);     // gettable and settable
        b.method("shrink", [](Person& self, float a) { self.height -= a; });
    }
};
```

- `{bob grow 10}` resolves `bob` through the context's namespace, then dispatches.
- Method tables are built once per type, lazily, into a flat sorted array of
  `(Symbol, thunk)` — binary search on dispatch, no `std::map`, no virtual per-method.
- Inherited bindings are flattened at table-build time, not chased at dispatch time.
- Unhandled method: error carrying the type name, method name, and call site.

### Typed messages

The talk's specialized `Message` subclasses, done with plain structs:

```cpp
struct HitMsg { static constexpr auto name = "hit"; int points; };

// binding:
b.on<HitMsg>(&Person::on_hit);        // void on_hit(const HitMsg&)

// calling, from C++:
bob->handle(HitMsg{.points = 20});    // no Node/Array ever constructed
```

Two dispatch paths must converge: a C++-side `handle(HitMsg)` must reach a script-side
handler `(hit ($points) ...)` and vice versa, with the C++→C++ path allocation-free.
Aggregate members map positionally to script arguments. This is the design's sharpest
edge — get it right and the library feels magic.

### Variables and scoping

`$name` nodes point at variable slots. Dynamic scoping via a value stack:

```
{do ($a)
  {set $a {some_func}}
  {other_func $a}}
```

`do` pushes the named slots, runs the body, restores on exit (including on error — use
RAII, not manual pops). `ctx.var("game_time") = 500;` from C++, `int t =
ctx.var("game_time").as_int();` back.

Name lookup is the only cost; evaluating a bound `Var` node is a dereference. Provide
`baker::VarRef` — a resolved handle callers hold in a static or member to skip lookup
entirely, which is what the original advised doing by hand.

Dynamic scoping means a callee sees the caller's slots and can clobber them. That is a
deliberate simplicity trade, inherited intentionally. Say so in the header comment so
nobody "fixes" it into lexical scoping later.

### Script-defined functions and classes

```
{func add1 ($a) {+ $a 1}}
{class Person (grow ($n) ...) (miss {print "whoosh"})}
{new Person Bob}
```

Body evaluates to its last expression. Arguments bind via the same dynamic scoping. `$this`
is bound after argument evaluation, not before.

### Tasks — use coroutines

The talk lists delay/interp/thread tasks and flags variable-preservation at construction
time as an open problem it was "investigating LISP closures" for. C++20 coroutines solve
it outright; take the win:

```cpp
baker::Task blink(baker::Context& ctx) {
    for (int i = 0; i < 3; ++i) {
        light.on();
        co_await baker::delay(100_ticks);
        light.off();
        co_await baker::until([] { return player_ready(); });
    }
}
ctx.scheduler().spawn(blink(ctx));
```

Script side keeps the original surface: `{scheduler delay_task 100 {...}}`,
`{scheduler interp_task $frame 0 100 {...}}`, `{scheduler thread_task {...} {wait {...}} {...}}`,
implemented on the same scheduler. Scheduler ticks are caller-driven
(`scheduler.tick(dt)`); the library owns no threads and no clock.

### Documenting script hooks — we can beat the original here

The talk names this as an unsolved pain point: because registrations are scattered through
C++ with no headers to grep, they hand-maintained a separate lightweight file describing
the syntax of every command and global variable, and considered generating it but never
did.

We get it for free. Because `ctx.func` and `Binder` know the full signature, the registry
can dump itself:

```cpp
baker::write_reference(ctx, "docs/script-reference.md");
```

Output: every registered function and object method with parameter types, arity, optional
and raw markers, overload sets, the owning type, and a `doc("...")` string if the
registration supplied one. Same for global variables and bound properties. Wire this into
the test suite so the checked-in reference can never drift from the code.

This is a small feature and it is the single clearest "twenty years later" win in the
whole library. Do not skip it.

### Load-time checking

`baker::check(const Array&, const Context&)` walks a loaded tree **without evaluating
anything** and reports every statically knowable error in one located pass: a command
whose head resolves to a registered function but has the wrong arity for every overload,
or passes a literal of the wrong type in a slot the signature fixes. Unregistered heads,
arguments that are themselves commands or variables, and `baker::Args` slots are skipped —
laziness means their types aren't knowable until access, and the check must never
pretend otherwise.

The binder already holds every signature, so this costs little. It turns most script
errors from "found when the event finally fires in playtest" into "found at load." Opt-in
via a `Config` flag; run in CI over the real-corpus test with Harmonix's built-ins stubbed
by arity. Adapted from SeExpr's prep-time type checking (see `seexpr-vs-baker.md` §6).

`check` also produces a **demand report**: every unresolved command head, every unknown
object method, and every call to a function whose status is `stub`, each with all of its
call sites, aggregated across everything checked. This is the push-up backlog generated
from the data itself — what the softer layers are asking for that the harder layers don't
yet provide. It is informational, never an error: unresolved words are normal while a
harness is being grown.

When packs declare layers (§1, `#require`), `check` also enforces **the one-way rule**:
it warns whenever a pack references vocabulary defined by a softer pack. This is
Shumaker's hard/soft rule — softer may depend on harder, never the reverse — applied to
every boundary (see `pushing-up.md` §1–2).

`check` also produces a **repetition report**: it hashes command and array subtrees
structurally (after macro expansion, ignoring source location) and reports every
non-trivial subtree that appears three or more times across the files it was given, with
all locations. These are crystallization candidates — the harness tree's "third time" rule,
computed rather than noticed. Demand shows words the softer layers are missing;
repetition shows words they have already built by hand. Minimum subtree size and the
repeat threshold are configurable. Informational, never an error.

Where packs declare `owners`, `check` reports any pack with fewer than two as an
informational finding — the single-gardener failure mode, surfaced from data.

The demand report, repetition report, owner findings, and pack metadata share one
stable, versioned, documented machine-readable output (JSON), alongside the human-readable
form, so a studio can diff them milestone to milestone. A harness audit then becomes
partly a build artifact (`reading-a-studio.md` §7).

### Debugging

- Script call stack maintained as an intrusive linked list of frames on the C++ stack —
  no allocation, ~2 stores per call.
- Any error unwinds with the stack rendered as `file.dta:line` frames, innermost first.
  The original dumped this *alongside* the C++ stack on assert, which is what makes it
  useful — provide `baker::script_backtrace()` as a standalone call an assert handler or
  crash reporter can splice in.
- `ctx.trace(bool)` for a per-command trace hook.
- **Call statistics** in non-shipping builds: each function record keeps a call count and
  cumulative time, updated in place with no allocation. `ctx.stats()` returns them;
  `ctx.hardening_candidates(threshold)` lists script functions whose cumulative cost
  crosses a threshold — the measured list of what to push up for performance. Compiled
  out under `BAKER_SHIPPING`.
- Everything needed for an interactive debugger should be reachable, but do not build one.
  They shipped five years of games on print statements.

---

## 6. Public API surface

**Shapes: an optional third library.** `baker::shape` (depends only on `baker::data`)
lets a layer declare the expected structure of a data domain *in data*:

```
(shape boss_phase
  (enter_at float (range 0 1) (kind percent) (desc "HP fraction that starts this phase"))
  (attacks  (list symbol))
  (cadence  float (range 0.2 5)))
```

A shape gives fields types, ranges, steps, kinds, descriptions, and notes — the tweak
metadata of §4, generalized to arbitrary data. Shapes drive two things: validation (a
`baker::check` extension reporting shape violations at load, located) and enumeration for
generated tuning panels. They are strictly opt-in; unshaped data works everywhere, and a
shaped domain still accepts extra fields with a warning rather than an error, so adding a
field never requires touching the shape first. Shapes recover the one real benefit of
Halo's code-defined schemas — a generated editing UI — while keeping schemas in data,
where gardeners can write them.

**Two libraries, strictly layered.** `baker::data` covers nodes, symbols, arrays, parsing,
writing, lookup, merge, config, tweaks, and the binary cache. `baker::script` covers
evaluation, functions, objects, messages, and tasks, and depends on `baker::data`. The
reverse dependency must never exist. A program that only wants hot-reloadable config links
`baker::data` and pays for nothing else; in that configuration `Command` and `Var` nodes
still parse and round-trip, but typed access to one is a clean error saying the script
layer isn't linked. Enforce the boundary in CI by building and testing `baker::data` alone.

The reason is adoption, and the evidence is SeExpr (see `seexpr-vs-baker.md` §7): the
portable, host-agnostic half of a system like this is useful to far more people than the
engine-shaped half, and only stays portable if nothing reaches across the line.

Headers, each independently includable:

```
<baker/node.hpp>        Node, Type, Symbol, Ref
<baker/array.hpp>       Array, find/eval, construction helpers
<baker/parse.hpp>       read_file, read_string, Preprocessor options
<baker/write.hpp>       pretty-printer (round-trips, preserves nothing else)
<baker/config.hpp>      Config, Tweak, TweakRegistry
<baker/script.hpp>      Context, func binding, variables
<baker/object.hpp>      Object<T>, Binder, messages
<baker/task.hpp>        Task, Scheduler, awaitables
<baker/binary.hpp>      cache save/load
<baker/baker.hpp>       umbrella
```

Style rules, applied uniformly:

- Types `PascalCase`, functions and variables `snake_case`, constants `kPascalCase`.
- `[[nodiscard]]` on every pure accessor. `noexcept` wherever it is true.
- No macros in public headers except optional `BAKER_*` build switches.
- No `iostream` anywhere in the library. Errors carry structured data; formatting is the
  caller's problem, with a `to_string(const Error&)` provided.
- Exceptions on error paths only, and every throwing API has a non-throwing twin.
  `BAKER_NO_EXCEPTIONS` compiles the throwing forms out and makes the twins mandatory.
  *(Superseded by D54: Baker never throws. Every fallible API returns `Expected<T>`,
  and the `try_` prefix now marks only weak lookups that return null.)*
- `BAKER_SHIPPING` makes reload and layer mutation no-ops after initial load, freezes every
  tweak at its loaded value, and applies `dev_only` (§4). Loop telemetry, the edit
  journal, and call statistics are compiled out. Shipping builds should not be
  live-editable by accident; anything a live game *does* need to change after ship goes
  through an explicitly enabled layer path, not a forgotten debug hook.
- No RTTI required. No global mutable state except the symbol table and the default
  `Context`/`TweakRegistry`, each of which is injectable for tests.
- Zero external runtime dependencies.

---

## 7. Performance requirements

Measured by benchmarks committed alongside the code:

- Parse throughput ≥ 40 MB/s single-threaded on a modern x86-64 for typical config text.
- `find_float` on a 20-child array: ≤ 25 ns.
- Native function call through the binding layer with 2 scalar args: ≤ 40 ns, **zero
  allocations**.
- C++→C++ typed message dispatch: ≤ 15 ns, zero allocations.
- `Tweak<float>` read: compiles to a single relaxed atomic load; verify by inspecting
  codegen in a test (a `-O2` disassembly check, or just an assertion that it is not a
  function call in the benchmark).
- Binary cache load of a 5 MB tree: ≤ 10 ms, ≤ 2 allocations (the blob and the fixup pass).

Allocation counts are enforced by a test-only global allocation counter, not by eyeball.

---

## 8. Testing

- **Unit tests** per module. Vendor doctest (MIT, single header) — no FetchContent, no
  network at build time.
- **Round-trip property test**: parse → write → parse produces structurally equal trees.
- **Real-corpus test**: the library must parse a corpus of genuine Harmonix `.dta` files
  (available from `mariteaux/milo-script-library`; the PS2 Rock Band and Amplitude OPM
  prototype sets are plain DTA with original comments intact). Vendor a small,
  clearly-attributed subset for CI, and provide a script to point the parser at a full
  local checkout. Parsing must succeed on 100% of the corpus; evaluation is not required.
  This is the single best correctness signal available and it should be wired up early —
  do it in M1, not at the end.
- **Fuzzing**: libFuzzer harness on the parser, seeded from the corpus. Must survive 10M
  execs clean under ASan+UBSan.
- **Sanitizers**: ASan, UBSan, and TSan (for the atomic-refcount build) in CI.
- CI on gcc, clang, and MSVC; C++20 minimum; also build a C++23 configuration to confirm
  `std::expected` path.

---

## 9. Binary cache format

Purpose: skip text parsing at startup. Our own format, `.datb`, versioned.

- Header: magic, format version, endianness tag, symbol-table offset, root offset.
- Symbol table: length-prefixed strings, interned once at load.
- Node blob: arrays laid out contiguously, children adjacent to parents, references as
  offsets so load is a single read plus a pointer-fixup pass.
- Macros and `#ifdef` are resolved *before* serialization — the cache stores the expanded
  tree. Record which defines were active in the header so a stale cache can be detected.
- `Cipher` interface (two functions, encrypt/decrypt a span) with no implementation
  shipped. The original encrypted caches so players wouldn't mail the studio their own
  cheat codes; ours leaves the hook and the anecdote in a comment.

**Stretch goal (P3, do not start unless asked):** a read-only compatibility module for
Harmonix's real DTB variants, behind `BAKER_DTB_COMPAT`, enabling the corpus to be tested
from binary as well as text.

---

## 10. Milestones

Each milestone ends green — builds clean on all three compilers, tests pass, benchmarks
recorded. Do not start the next until the previous is green.

| # | Scope | Done when |
|---|-------|-----------|
| **M0** | Repo skeleton: CMake, C++20, doctest vendored, CI, clang-format/tidy, LICENSE, README stub; separate `baker::data` and `baker::script` targets | `cmake --build` and `ctest` pass with one trivial test; CI builds and tests `baker::data` alone |
| **M1** | `Node`, `Symbol`, `Ref`, `Array`, text parser, preprocessor (include/merge/ifdef/define/macros), pretty-printer, error type with file:line | Layout `static_assert`s hold; round-trip test passes; **real-corpus parse at 100%**; fuzzer seeded and clean for 1M execs |
| **M2** | Lookup API: `find*`/`try_find*`, defaults, sorted mode, path access, `Convert<T>` | Lookup benchmarks meet §7; error messages include path and source location |
| **M3** | `Config`, merge, named layers, provenance, edit-mode parse and write-back, `Tweak<T>`, registry, hot reload, change callbacks, `BAKER_SHIPPING` | One-line constant replacement demo in `examples/`; tweak read compiles to a single load; reload test mutates a file on disk and observes the new value; pushing then removing a layer restores every value exactly; `source()` names the correct layer, file, and line; under `BAKER_SHIPPING`, reload is inert and `dev_only` tweaks return defaults; tweaking a value and calling `write_back` produces a one-line diff in the source file with comments and notes preserved, and refuses cleanly for a macro-sourced value |
| **M3b** | Reload telemetry (`TimeSource`, `ReloadRequest`, `ReloadRecord`, history ring, `on_reload_complete`); `LayerOptions` (optional, enabled), toggles, the `scratch` layer and its `set` fast path, `layer_effect`, `diff`, `write_back_to` insertion, `promote`, `WriteReport`; `snapshot()` / `restore()`; the headless readiness test `tests/readiness_loop.cpp` | **Telemetry:** with a fake time source that advances a fixed step per call, one reload yields a record whose six stage durations match exactly; zero allocations per reload once the history exists; the ring wraps at capacity keeping the newest in order; a parse error yields `ParseError`, zero bind and notify time, and unchanged tweak values; `migrated` and `punted` match the callbacks' returns; `origin_ns` appears unchanged. **Layers:** toggling a layer off and on restores every value and every `source()`; an optional layer with a missing file loads and contributes nothing; `set` on an existing scalar path allocates nothing, triggers no full re-merge, and fires only the affected tweak's `on_change`; `set` then `write_back` changes one line on disk, keeps comments and notes, empties that path from scratch, and leaves `source()` naming the file and line; `write_back_to` into an absent personal layer creates the file with the nested entry, and into a populated file inserts beside existing siblings changing no other bytes; `promote` with one unwritable entry writes nothing and reports every refusal with file and line; write-back refuses when the file changed on disk after loading; in a three-layer stack `layer_effect` reports `below` and `shadowed` correctly. **Snapshots:** snapshot, push a layer, toggle another, set three values, restore: every tweak value and `source()` match; restore fires each reload callback exactly once and records trigger `restore`; taking a snapshot copies no array. **Readiness test:** in a temporary directory, a commented tunable with a note and an absent optional `local` layer; a bound `Tweak<float>`; a disk edit and `reload({.trigger = "test"_sym, .origin_ns = t})` under a fake time source with a complete record; `set` and `write_back` with a one-line diff, comment and note intact; an experiment layer toggled with `layer_effect` and both A/B values asserted; snapshot, disturb, restore, equal; zero allocations throughout in reload telemetry and tweak reads |
| **M4** | `Context`, variables, dynamic scoping, lazy command evaluation, native `func` binding with signature deduction, `Raw<T>` params, overload sets, generated script reference | Calculator example; binding benchmark meets §7 with zero allocations; **`if` and `set` are ordinary registered functions with no evaluator special-casing**; a test asserts the untaken branch of an `if` is never evaluated; `script-reference.md` generates and is checked for drift; `baker::check` catches a wrong-arity call to a registered function at load, without evaluating anything; a script stub replaced by a native registration of the same name runs unchanged call sites; the demand report lists an unresolved head with all its call sites |
| **M5** | `Object<T>`, `Binder`, method/property binding, inheritance flattening, namespace lookup, script-side `func`/`class`/`new` | Both dispatch directions work; the talk's `Person::grow` example runs from script and from C++ |
| **M6** | Typed messages, allocation-free C++→C++ path, script call stack, trace hook, call statistics | Message benchmark meets §7; an error thrown 4 frames deep renders a correct stack; call statistics add no allocations and compile out under `BAKER_SHIPPING` |
| **M7** | `Task`, `Scheduler`, coroutine awaitables, script-side scheduler commands | `delay`/`until`/interp all tick correctly; a task holding a local across a suspend point works (the problem the original had open) |
| **M8** | Binary cache: write, load, fixups, staleness detection, `Cipher` hook | Cache load benchmark meets §7; cache and text loads produce structurally equal trees |
| **M8b** | `baker::shape`: shape declarations, shape-aware `check`, enumeration API; `#require` | A shape violation in the corpus-style test data is reported at load with file and line; extra fields warn without failing; a pack loaded via `#require` from three files defines its macros once; a `game`-layer pack referencing a `task`-layer function produces a one-way-rule warning |
| **M9** | Docs: README with the one-line-constant pitch up front, `docs/language.md`, `docs/cookbook.md`, `examples/` (config, scripting, objects, tasks, remote-console, live-ops overlay, and `harness-imgui`: a Dear ImGui reference harness with panels generated from tweaks and shapes, an `eval` console, provenance on hover, save-to-source via write-back, and three readiness panels: **Loop** (recent reload records as stage bars, with edit-to-see time when the host supplies origins), **Layers** (the stack with enable toggles, each layer's effect, and a promote button that shows refusals in place), and **Snapshots** (capture A, capture B, flip between them)) | A reader can go from zero to a working tweak in under five minutes of reading; the cookbook's live-ops section covers environment boundaries, version history, rollback, and personal and experiment layers; the harness pushes a personal `local` layer by the convention in §4 |
| **M10** | Harness instrumentation: loop statistics (aggregating M3b's reload records), edit journal, pack harness metadata (`owners`, `for`, `stage`), `origin` provenance, repetition report, owner findings, machine-readable check output | `loop_stats()` reports rolling median and worst edit-to-see per file and per layer, and counts a failed reload; `unsaved()` lists a tweaked-but-not-written-back value; `source()` names a value's `origin` tool; a subtree repeated in three files appears in the repetition report with all three locations; a one-owner pack is reported; check JSON output is schema-versioned and stable across runs; all instrumentation adds no allocations on the hot path and compiles out under `BAKER_SHIPPING` |

---

## 11. Working agreements

- Commit per logical change, with a message saying *why*. Squash exploratory work.
- Any deviation from this spec gets recorded in `dev/decisions.md` with the reasoning —
  short entries, not ADR ceremony. Deviating is fine; deviating silently is not.
- If a design choice here turns out to be wrong once you're in the code, say so before
  implementing around it. Specs written before the first line of code always contain a
  couple of mistakes, and §5's implicit-eval rule and §4's merge semantics are the two
  most likely candidates.
- Keep `README.md` honest from M1 onward — no documenting features that don't exist yet.

---

## Appendix A — Source material digest (GDC 2005 slides)

Condensed from the public deck, for offline reference. Reproduced as facts about the
system's design, not as a code source.

**System scale.** Evolved over 5 years. LISP-inspired. Data format + API + script engine.
~70 KB of code, widely used across the studio. Predecessor was Python, abandoned because
getting an int out required "token command string interpreter function pyint int" —
wrapper hell — and 3 MB was untenable on a 32 MB console.

**Node.** 12 bytes packed: 4-byte value union (int/float/pointers), 4-byte type, plus
size, file, line, refcount on arrays. Serializable. Nodes act as smart pointers for
refcounted types; that is the entirety of the memory management story.

**Symbols.** Unique permanent strings, used instead of heap strings almost everywhere.
Saves memory across instances, enables pointer comparison. Heap strings still exist.

**Searches.** `menu->FindArray("eggs")->FindFloat("price")` — subarrays matched by symbol
tag, linear with pointer comparisons, sortable for binary search when speed demands.

**Type errors** name the expectation and the source location:
`'12.75' not int (menu.dta, line 3)`.

**Data files** also carry comments, macros, `#include`, `#merge`, `#ifdef`.

**Macros** persist outside the file they're defined in and multiply-reference the same
array rather than copying.

**Config.** Loaded at startup, globally accessible, encrypted when cached. A `default.dta`
plus an app-level override file that `#merge`s it. Reload a portion of the database and
notify dependent C++; parameters must be grouped by user for this to work. In-memory
editing wants extra metadata inline: `(width 2 (range 0 10) (step 1) (desc "Box width"))`.

**Scripting.** Indicated when data is "too fragile or limited"; wordier than data, so
combine rather than subsume. Commands are `{func args}` or `{object method args}`. Uses
listed: UI and world event handling, tool plugins, command console, remote level editing
(editor previews changes over a serialized script protocol), and the C++ messaging system.

**Native funcs** registered by name, handed the raw command array; arguments evaluated
inside the handler; accessors evaluate commands implicitly "unless accessed as command" —
described as trading LISP complexity for a little danger. Optimization: bind the handler
to the func node on first execute.

**Objects.** `DataObject` with a name in a namespace and a virtual `Handle(DataArray*)`.
Method dispatch via an MFC-style macro table (`BEGIN_HANDLERS` / `HANDLE` /
`HANDLE_EXPR` / `HANDLE_SUPERCLASS` / `HANDLE_CHECK`). `DataClass` associates script-side
handler arrays with a C++ object and supports instance parameters. `$this` assigned after
argument evaluation.

**Messages.** A `Message` builds the command array; specialized subclasses (`HitMsg`)
expose typed accessors so C++→C++ dispatch never touches DataArrays. Specialized script
macros match, so the specialization can change without breaking handlers.

**Balance guidance.** Script handlers for flexibility, prototyping, avoiding C++
dependencies, and reducing subclass count. C++ handlers for special argument handling,
performance, and maintainability.

**Tasks.** Commands executing over time: delay, interp, thread. Variables used in the
script must be preserved from construction time; done explicitly via `(preserve $msg)`,
with LISP closures under investigation.

**Debugging.** Script call stack dumped on assert, print statements, interactive debugger
listed as future work.

---

### From the talk audio (not on the slides)

A transcript of the session recording adds the following. Where it conflicts with the
slides, the audio wins — it is the author explaining the slide.

**Scale.** ~70 KB total, of which ~40 KB is built-in functions; the core is "tiny." The
most widely used library in the codebase, treated as a language extension.

**Why not Python.** Getting an int from a name meant: token → command string → interpret →
call a Python function → Python integer → convert back to int. "What you really want to do
is go straight from the token to an integer." Plus wrapper layers on every input and
output, plus ~3 MB of code on a 32 MB console.

**Execution does not evaluate arguments.** Stated explicitly and at length: `Execute`
finds the registered handler and calls it with the command array, and "it doesn't do any
evaluation on its arguments." Contrast drawn with interpreters that recursively evaluate
into a temporary list and then need special cases to *avoid* interpreting arguments, as
for `if`. Here it is "left up to the handler in a natural way." Evaluation is triggered by
accessing a command node *as some other type*; accessing it as a command does not
evaluate.

**No keywords.** The system "only specifies the syntax for calling functions and objects.
There's no implicit language or keywords except those we choose to register all the time."
Naming those registrations was "a constant source of flaming" internally.

**Handler caching.** On first execution the handler is bound into the node itself — safe
because the function pointer never goes away — but re-registering a function requires
reloading the script.

**Variables.** Globally named data nodes; variable nodes point at them; evaluation is just
a dereference, "extremely fast." Lookup cost is offset by caching resolved variables into
statics or member variables. Dynamic rather than lexical scoping, chosen for simplicity
with the clobbering hazard acknowledged as a deliberate trade.

**Object commands.** The first argument is evaluated, so the object itself can be the
result of a function call: `{{nearest_object $bomb_position} suffer_damage}`. Objects may
be nameless, in which case they bind directly to nodes or variables.

**Instance parameters** on `DataClass` live in a loose key-value array, "a lot like a Perl
dictionary." Fully script-side class definition and instancing is possible but they
generally didn't go that far, preferring `DataClass` on real C++ objects.

**C++ handlers earn their place** for specialized argument handling — passing a variable
for setting rather than evaluating it, polymorphic dispatch (their real `+` behaves
differently for two ints than for an int and a float), and variadic argument lists — plus
performance and maintainability.

**Documenting script hooks** was an acknowledged weak spot: no headers to grep, so they
hand-maintained a lightweight file describing the syntax of every command and global
variable. Generating it was considered, never done. (§5 makes us do it.)

**Task variable preservation** is done by explicitly naming the variables, storing them
like dynamic scoping, and restoring them each time the command runs. Closures named as the
cleaner solution they didn't have.

**Q&A.** Scripts were written mostly by programmers, not designers. They gave data files a
distinctive extension rather than `.txt` specifically so batch tooling and searches could
target them. Branching is runtime in script and load-time in data via `#ifdef`.

## Appendix B — Reference material

- Slides (PDF): `https://mariteaux.somnolescent.net/modding/guitar-hero/tutorials/data-driven-programming-made-easy.pdf`
- Slides (uploaded by the author): `https://www.scribd.com/presentation/53884818/Data-Driven-Programming-Made-Easy`
- Session page: `https://gdcvault.com/play/1019955/Data-Driven-Programming-Made`
- Community format notes: `https://mariteaux.somnolescent.net/modding/guitar-hero/tutorials/an-introduction-to-data-array`
- Test corpus: `https://github.com/mariteaux/milo-script-library`
- Companion analyses, required reading: `halo-tags-vs-dataarray.md` (Bungie's tag
  system, Game Tech 2004), `seexpr-vs-baker.md` (Disney's SeExpr, 2005–2019), and
  `iteration-backgrounder.md` (iteration time, contemporary engines, ranked use cases),
  `harness-tree-vision.md` (the harness tree and Baker's role as its substrate), and
  `pushing-up.md` (hardness, push-ups, and how each layer boundary is managed), and
  `reading-a-studio.md` (auditing a studio's harness tree; the source of the
  instrumentation in §4, §5, and M10)
- SeExpr: `https://wdas.github.io/SeExpr/` — design notes on typing and lifetime
  qualifiers are worth reading for the reasoning even though we adopt neither
- Behavioral reference only, **not a code source** (see §0): `DarkRTA/rb3`,
  `freeqaz/dc3-decomp`, `freeqaz/milo-native-engine`

The session audio (45:54) has been transcribed and its substance is folded into
Appendix A. The transcript is machine-generated and lossy in places — several passages
degrade into repeated fragments, and a few slides are effectively missing from the audio
record. Where Appendix A reports something from the audio, it is from a passage that came
through clearly; don't go looking for more precision than is there.
