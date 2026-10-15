# Baker spec addendum: M3b — environment readiness

Baker is the data substrate (L0). It cannot build a studio's development environment, but four changes let it make the data loop the fastest in the studio and the easiest to measure. This addendum is self-contained; it assumes `baker-spec.md` and nothing else.

## Brief for the implementer

**Why.** A studio adopting Baker will not start its vertical slice until a readiness gate passes. Four of that gate's tests touch the data layer:

- edit-to-see time is measured for every role, and Baker's share of it must be visible;
- values tuned during play are saved to source as minimal, reviewable diffs;
- people experiment without blocking each other, and compare variants A/B;
- polish and QA can return to an exact configuration state.

Baker already holds what each test needs: reload stages, the layer stack, provenance, and edit-mode trivia. These changes expose that information. None adds a subsystem.

**Where.** All four changes live in `baker::data`, header `<baker/config.hpp>`, plus one test and one example. Nothing touches `baker::script`.

**Terms.**

| Term | Meaning here |
| --- | --- |
| Edit-to-see time | Time from saving a change in a tool to the running game reflecting it |
| Layer | A named entry in `Config`'s ordered stack, merged bottom to top (spec §4) |
| Provenance | The layer, file, and line that supplied a value, as `source()` returns (spec §4) |
| Write-back | Replacing the literal token that produced a value, in its source file (spec §1) |
| Reload epoch | The counter that guards non-trivial tweak reads (spec §4); every reload or re-merge advances it |
| Migrate or punt | The reload-callback contract (spec §4) |

**Constraints carried from the spec.**

- The library owns no threads, sockets, or clock. Timing uses an injectable time source (Change 1).
- No `iostream`. Errors are structured `baker::Error` values, and every throwing call has a non-throwing `try_` twin.
- Tooling calls may allocate. Reload hot paths and tweak reads must not gain allocations; verify with the test allocation counter (spec §7).
- Under `BAKER_SHIPPING`, layer mutation and write-back are inert. Telemetry compiles out, but its API stays and returns empty results, so hosts need no `#ifdef`s.
- Record each deviation from the spec in `dev/decisions.md` (spec §11). Five are listed under "Decisions to record" below.
- Report line counts against the 4,000-line core budget when the work lands, and flag an overrun rather than absorbing it (spec §0).

**Milestone.** Add an M3b after M3 is green, holding Changes 1–3 and the headless test from Change 4. The example in Change 4 joins M9.

## Change 1: Reload telemetry

**Purpose.** Report how long each reload took and where the time went, so a studio's loop dashboard can show Baker's share of edit-to-see time and catch regressions.

**API.**

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
    std::uint64_t scan_ns, read_ns, parse_ns, merge_ns, bind_ns, notify_ns;
    std::uint32_t files_reloaded, tweaks_changed;
    std::uint32_t migrated, punted;  // reload-callback outcomes
    Symbol        first_file;        // first changed file, for display
    ReloadOutcome outcome;
};

class Config {
public:
    void set_time_source(TimeSource fn, void* user = nullptr);
    void reload(const ReloadRequest& request = {});  // existing call; keep its current return type
    [[nodiscard]] std::optional<ReloadRecord> last_reload() const noexcept;
    template <class F> void each_reload(F&& fn) const;  // oldest to newest
    void set_reload_history(std::size_t capacity);       // default 256; allocates once
    void on_reload_complete(/* same callable form as on_reload */);
};

}  // namespace baker
```

**Semantics.**

- Six stages, in order: *scan* (find changed files), *read*, *parse* (preprocessor and edit-mode trivia included), *merge* (re-merge the layer stack), *bind* (rebind tweaks), and *notify* (reload callbacks, including punt and rebuild hooks). Each is timed once per reload.
- Layer pushes, pops, removals, toggles (Change 2), and `restore()` (Change 3) also produce records, with trigger `layer` or `restore` and zero scan, read, and parse time.
- A reload that finds no changed files records `Unchanged`. A parse failure records `ParseError`, keeps the previous tree, and leaves bind and notify at zero.
- When the host supplies `origin_ns`, the substrate's edit-to-see time is `end_ns - origin_ns`. Otherwise hosts compute it from the absolute `start_ns` and `end_ns`. Clock domains across machines, such as a PC tool and a devkit, are the host's problem.
- History is a fixed-capacity ring allocated once. When it is full, the oldest record is dropped.
- `on_reload_complete` fires after notify with the finished record, so a host can forward it to telemetry without polling.

**Shipping.** Under `BAKER_SHIPPING` the timing code compiles out; `last_reload()` returns empty and `each_reload` visits nothing.

**Acceptance.**

- [ ] With a fake time source that advances a fixed step per call, one reload yields a record whose six stage durations match expected values exactly.
- [ ] Telemetry adds zero allocations per reload once the history is allocated.
- [ ] The ring wraps at capacity and keeps the newest records in order.
- [ ] A parse error yields `ParseError`, zero bind and notify time, and unchanged tweak values.
- [ ] `migrated` and `punted` match what the callbacks returned.
- [ ] `origin_ns` from the request appears unchanged in the record.

## Change 2: Personal, experiment, and scratch layers

**Purpose.** Let people experiment without touching shared files or blocking each other, compare variants A/B during polish, and move a winning experiment into shared data as a reviewable diff.

**API.**

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
    // Existing call, gains options.
    void push_layer(std::string_view name, std::string_view path, LayerOptions options = {});

    void set_layer_enabled(std::string_view name, bool enabled);
    bool toggle_layer(std::string_view name);              // returns the new state
    [[nodiscard]] bool layer_enabled(std::string_view name) const;

    // Tool edits land in the reserved in-memory "scratch" layer.
    void set(std::string_view path, Node value);
    void clear_scratch(std::string_view path = {});        // one path, or all of it

    [[nodiscard]] std::vector<LayerEffectEntry> layer_effect(std::string_view layer) const;
    [[nodiscard]] std::vector<DiffEntry> diff(std::string_view a, std::string_view b) const;

    // Write-back (spec §1), extended below. Each has a try_ twin.
    // WriteReport lists every file and line written.
    WriteReport write_back(std::string_view path);
    WriteReport write_back_to(std::string_view path, std::string_view layer);
    WriteReport promote(std::string_view from_layer, std::string_view to_layer);
};

}  // namespace baker
```

**Semantics.**

- **Enable and toggle.** Changing a layer's state re-merges, advances the epoch, fires reload callbacks, and records telemetry, exactly as push and remove do (spec §4). Provenance never names a disabled layer.
- **Personal layers.** A convention, not a mechanism. The host pushes a layer named `local` with `{.optional = true}` at a per-user path such as `local/<user>.dta`, above the shared layers and below live patches. The project keeps that directory out of version control. Baker never looks up user names.
- **Scratch.** Every `Config` has a reserved, in-memory layer named `scratch`, always topmost and created empty. Edits from tools, including UI edits made through the tweak registry, go there. Provenance stays honest, and "my tuning versus the files" becomes a toggle. `pop_layer` skips scratch, and scratch is never written to disk directly.
- **`set` is a fast path.** Because scratch is topmost, `set` need not re-merge. It updates scratch, rebinds only the tweaks bound to that path, and fires only their `on_change`. Lookups consult scratch before the merged tree. Setting an existing scalar path allocates nothing. Toggling scratch itself is a full re-merge like any other layer.
- **Effect and diff.** Both work on leaf entries as merge defines them (spec §4). `layer_effect` answers "what does this layer actually change?" `diff` compares two layers' own contents, for variant A against variant B. Both are tooling calls computed on demand; they may allocate and need not be fast.
- **Write-back of a scratch value.** Today write-back refuses values sourced from an overlay layer. Refine that rule: when the current value comes from scratch, the target is the highest enabled non-scratch layer that supplies the path. On success, Baker removes the path from scratch and reloads the patched file with trigger `write_back`, so provenance then names the file and line.
- **Writing into another layer.** `write_back_to` is the "explicit, separate call" that spec §1 mentions without defining. If the target layer's file has a literal token for the path, replace it. Otherwise insert: find the deepest ancestor array literally present in that file, and add the missing entries as its last child, indented like the previous sibling, in the pretty-printer's form. Never modify any other byte. If the target layer is optional and its file is absent, create the file.
- **Refusals, always located.** Refuse when a positional path segment such as `phase.2` does not exist in the target. Refuse when the ancestor array came from a macro, `#include`, or `#merge` rather than the file itself. Refuse when the file on disk changed since it was loaded, comparing a content hash recorded at load. Never guess.
- **Promote.** Plan every entry of `layer_effect(from)` into the target layer's file and validate all of them first. Write only if every entry succeeds; on any refusal, write nothing and return all refusals. The source layer stays in the stack, and the caller removes it.

**Shipping.** Toggles, `set`, write-back, and promote are inert and return a "shipping build" error. `layer_effect` and `diff` still work.

**Acceptance.**

- [ ] Toggling a layer off and back on restores every value and every `source()` exactly.
- [ ] An optional layer whose file is missing loads without error and contributes nothing.
- [ ] `set` on an existing scalar path allocates nothing, triggers no full re-merge, and fires only the affected tweak's `on_change`.
- [ ] `set` then `write_back` changes one line on disk, keeps comments and `note` metadata, empties that path from scratch, and leaves `source()` naming the file and line.
- [ ] `write_back_to` into an absent personal layer creates the file with the nested entry. Into a populated file, it inserts beside existing siblings and changes no other bytes.
- [ ] `promote` with one unwritable entry writes nothing and reports every refusal with file and line.
- [ ] Write-back refuses when the target file changed on disk after it was loaded.
- [ ] In a three-layer stack, `layer_effect` reports `below` and `shadowed` correctly.

## Change 3: Snapshot and restore

**Purpose.** Give polish and QA an exact configuration to return to. Paired with a host's input recording, this makes a captured moment replayable; it also makes A/B comparison of two tuned states one call each.

**API.**

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

**Semantics.**

- A snapshot holds the ordered layer list (names, options, enabled state), a `Ref` to each layer's loaded tree, each file's load-time content hash, and a copy of scratch. Loaded arrays are immutable and refcounted, so nothing is deep-copied.
- `restore` replaces the stack, re-merges, rebinds tweaks, and fires reload callbacks with migrate-or-punt as usual. It advances to a new epoch rather than reusing the old one, so any cached derived data invalidates.
- `restore` never reads disk. If a file changed after the snapshot, values still come from the snapshot, and write-back of them refuses under Change 2's hash rule.
- Not captured: script variables, objects, tasks, and host state. Those belong to `baker::script` and the host.

**Acceptance.**

- [ ] Snapshot; push a layer, toggle another, and `set` three values; then `restore`. Every tweak value and every `source()` match the snapshot.
- [ ] `restore` fires each reload callback exactly once and produces a `ReloadRecord` with trigger `restore`.
- [ ] Taking a snapshot allocates only its own small bookkeeping; no array is copied.

## Change 4: A readiness demonstration

**Purpose.** Prove the first three changes end to end, in CI, so a studio can watch the data-layer rows of its readiness gate pass rather than read about them.

**Headless test, M3b: `tests/readiness_loop.cpp`.** In a temporary directory:

1. Write `default.dta` holding a commented tunable with a `note`, push it as layer `default`, and push an optional `local` layer whose file does not exist.
2. Bind a `Tweak<float>` to the tunable.
3. Edit the file on disk, call `reload({.trigger = "test"_sym, .origin_ns = t})` under a fake time source, and assert the new value and a complete `ReloadRecord`.
4. `set` a new value, call `write_back`, and assert a one-line diff with the comment and note intact.
5. Push an experiment layer, toggle it, and assert `layer_effect` and both A/B values.
6. Snapshot, disturb the stack, restore, and assert equality.
7. Throughout, assert zero allocations in the reload telemetry and tweak-read paths.

**Example, M9: `examples/harness-imgui`.** Add three panels to the reference harness the spec already plans:

- **Loop:** recent reload records as stage bars, with edit-to-see time when the host supplies origins.
- **Layers:** the stack with enable toggles, each layer's effect, and a promote button that shows refusals in place.
- **Snapshots:** capture A, capture B, and flip between them.

## Decisions to record in `dev/decisions.md`

1. Timing uses an injectable `TimeSource`, preserving "the library owns no clock" while defaulting to `steady_clock`.
2. Tool edits go to a reserved in-memory `scratch` layer rather than mutating tweaks directly.
3. Write-back of a scratch-sourced value targets the highest enabled non-scratch layer that supplies the path. This refines spec §1's refusal for overlay-sourced values.
4. `write_back_to` and `promote` may insert entries, but only as the last child of an ancestor array literally present in the target file.
5. Every write-back first checks that the file is unchanged on disk since it was loaded.

## Open questions to raise before building

Spec §11 asks for design doubts to be raised before implementing around them. These are the likely ones.

- Should `layer_effect` re-merge without the layer on demand, as proposed, or be maintained incrementally? On demand is simpler; measure it on the corpus before deciding.
- Is 256 the right default history capacity, and should it be a compile-time constant instead?
- Should snapshots serialize to a `.dta` file, so QA can attach configuration state to a bug report? Useful, but outside M3b unless asked.
- Does the `local` layer convention belong only in `docs/cookbook.md`, or also in an example?

## Out of scope

File watching and transport (the host detects changes and calls `reload`), dashboards, input recording, and anything in `baker::script`.
