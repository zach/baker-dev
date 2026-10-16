# Received Layers: Push as Text, Apply Once, Check Off the Owning Thread

A design note for three requirement notes from the live-ops integration (plan D), brought by
the owner on Oct 16, 2026:

- **0001.** Push text a host received as a file layer, read as text only.
- **0002.** Apply a checked layer without checking it again.
- **0003.** Check a candidate layer off the thread that owns the Config.

The hosts are the same for all three: a live game taking a balance or event overlay, a
dedicated server applying one between rounds, a mod loader, and a tool applying a proposed
change. Each separates *it arrived* from *it takes effect*. Results go into
`dev/decisions.md`.

---

## 1. Today

- **Received text has two routes, and each loses something.**
  - *A reader that serves the text by name, then `push_layer` by path.* The pack and origin
    are recorded, but with `cache_dir` and edit mode off the read goes through the cache:
    a development build writes a cache of the received layer, and a shipping build looks
    for one before it reads the text. That breaks D142.
  - *`read_string` under a text-only session, then `push_layer` with the tree.* It is text
    only, but the tree has no session. `source()` gives no origin, the pack is not in
    `read_session()`, and a push with a policy never sees the read's warnings.
- **A push with a policy checks again.** The note measures 1.4–1.7 ms for that second
  check. A push without a policy costs 0.14 ms, or 0.45 ms with `shape::validator`.
- **D161's profile.** 54% of a check is `detail::resolve`: three lookups per changed value,
  each a linear scan of the 2,000-entry table.
- **Threads.** A Config is not thread-safe, so every check runs on the owning thread, in
  the frame the layer arrives.

## 2. The API

```cpp
// A layer from text the host received: read as a file layer is (its packs, origin, and
// #autorun are recorded, against the session below it), but from `text` only: never a
// cache, written or read, whatever cache_dir and the build say. `name` is the layer's name
// and the file its sources name (for example "received/balance@3.dta"). The text is kept,
// so a layer pushed or removed below it re-reads it. An #include or #merge in it is refused.
// Write-back refuses its values: it has no file.
[[nodiscard]] Expected<void> Config::push_layer_text(std::string_view name,
                                                     std::string_view text,
                                                     LayerOptions options = {});

struct LayerOptions {
    // ...
    const LayerPolicy* policy = nullptr;
    // A report from check_layer, check_layer_text or a LayerChecker for this layer under
    // this policy. If it still holds, the push takes its verdict instead of checking again.
    // It holds only if the Config has not changed since the check and the push puts the same
    // text or tree on top. Otherwise the push checks as if no report were given.
    const LayerReport* checked = nullptr;
};

struct LayerReport {
    List<LayerEffectEntry> effect;
    List<Error> refusals;
    List<Error> warnings;
    CheckStamp stamp;  // opaque: what the report was made against
    [[nodiscard]] bool ok() const noexcept { return refusals.empty(); }
};

// A fixed state of a Config to check candidate layers against, from any thread. It shares
// the Config's trees and copies the rest, and never touches the Config after it is made:
// the owning thread may change the Config, or destroy it, meanwhile. Copyable; checks made
// with one copy may run on several threads at once.
class LayerChecker {
public:
    [[nodiscard]] Expected<LayerReport> check(std::string_view name, const Ref<Array>& root,
                                              const LayerPolicy& policy = {}) const;
    [[nodiscard]] Expected<LayerReport> check_text(std::string_view name,
                                                   std::string_view text,
                                                   const LayerPolicy& policy = {}) const;
};
// On the owning thread. What the checker keeps, and each check's temporaries, come from
// `allocator`; trees come from the default allocator, as every tree's do (D61).
[[nodiscard]] LayerChecker Config::checker(const Allocator& allocator = default_allocator()) const;
```

## 3. Rules

### 3.1 Received text (0001)

- **The read.** `push_layer_text` reads with a copy of the session below the layer, as
  `read()` does for a file layer. Two things differ:
  - the session is text only, and no cache name is formed, so nothing is written or read
    through `cache_dir`;
  - the session's reader refuses every path, so an `#include` or `#merge` is a located
    error. A layer from outside never reaches the game's disk. `check_layer_text` and
    `LayerChecker::check_text` read the same way, so a verdict and a push agree.
- **The layer.** `LayerState` gains `text` (a `Str` on the Config's allocator). A re-read,
  after `remove_layer` or a push below, reads it again. It has no dependencies, so
  `reload()` never sees it change.
- **Write-back** refuses a value supplied by a text layer, as it refuses an overlay's:
  "layer 'x' was pushed as text; it has no file". `layer_effect`, `explain` and `diff`
  work as for any layer.
- **Removing the layer** removes its pack and origin from `read_session()`, because the
  layers above re-read without it.
- **Shipping.** It is available, under `live_layers` after `freeze()`.

### 3.2 A report that still holds (0002)

- **The generation.** The Config keeps a `generation`. It increments on:
  - every merge;
  - `add_validator` and `remove_validator`;
  - every `set()`, the fast path included;
  - `clear_scratch`;
  - every call of the non-const `session()`, which may change defines, levels or the
    reader.

  The host never sees the generation. It is not `epoch()`, which a fast-path `set()` leaves
  alone.
- **The stamp.** `CheckStamp` records:
  - the Config it was made against, by address;
  - the generation;
  - a hash of the name and the text, or the tree's address;
  - a hash of the policy's rules and `allow` paths;
  - whether validators that must run on the owning thread were left out (§3.3).
- **When the report holds.** A push with `policy` and `checked` uses the report if every
  field matches and the layer goes on top. The layer goes on top unless `below` or `above`
  is given, since only the top is where the check stands.
  - If the report holds and refuses, the push refuses with it at once.
  - If it holds and passes, the push skips the check. For a text push, the read still runs
    (it builds the layer) and costs what `read_string` costs.
  - If it does not hold, the push checks, exactly as without `checked`. A stale report is
    never used.
- **A file layer pushed by path** never matches a report, because no check reads by path,
  so it always checks.
- **The limit.** The tree form trusts that the host did not change the tree between the
  check and the push. Trees are not meant to be changed once built, and the API says so.
- **Validators that read outside the Config** (`check_validator` reads a Context) are not
  covered: a change to the Context between the check and the push is not seen. The API says
  so too.

### 3.3 Checking off the owning thread (0003)

- **What `checker()` copies.** On the owning thread, into one refcounted block on the
  allocator:
  - the merged root;
  - each layer's name, enabled flag, tree, source map and session, the trees shared by
    reference;
  - `read_session()`;
  - the validators that may run on another thread (below);
  - the Config's address and generation.

  Nothing in it refers to the Config. Refcounts are atomic by default. A program built
  with plain refcounts (D20) uses a checker on the owning thread only, and the API says so.
- **The check.** `check_candidate` becomes a function of a *basis*:
  - the root, the layers and the validators;
  - its own merge scratch.

  The Config passes its own basis; a checker passes its copy and a scratch it allocates per
  check. The Config's shared `merge_scratch` is never touched from a worker.
- **Validators** (the owner's choice, Q1). `add_validator` takes options:
  `{.any_thread = true}` says a validator reads only what it is given, so it may run on
  any thread.
  - The checker copies and runs those, and records in the stamp that the others were left
    out.
  - A push with such a report, if the report holds, runs the left-out validators on the
    owning thread before it commits. It runs them over the stack merged with the candidate,
    and they may refuse under `warnings_refuse`. So the boundary pays only for them.
  - **The validator's argument changes.** For `shape::validator` to run on a worker, it
    must stop reading the Config. A validator receives a `ValidatorStack`: the merged root;
    the enabled layers' trees, bottom to top (when checking, with the candidate on top);
    and the read session.
    - `shape::validator()` takes no Config and is any-thread.
    - `check_validator` reads only the root, and stays on the owning thread.
    - Every caller changes; there is no old form (CLAUDE.md).

### 3.4 A cheaper check

- **The cost.** D161's profile puts most of a check in three linear lookups per changed
  value: the stack's value, the supplying layer, and its source.
- **The fix.** The check builds, once, a key index for each wide array it walks through,
  as the merge does (D127), and resolves through it.
- **The goal.** At least halving the 10 KB case. This helps every host, including the one
  whose report no longer holds and so checks at the boundary anyway.

## 4. Cost and shipping (acton-review)

- **Path class.** Tool or load: a layer arrives once, never per frame. Nothing on a hot path
  changes; a push without `policy` is unchanged.
- **The generation.** One increment in the places listed. On the fast-path `set()` it is
  one add on a member the call already touches.
- **Allocation.**
  - `checker()` makes one block: the layer records (names copied; trees, maps and sessions
    shared) and the validator copies.
  - A check allocates what it does today, from the checker's allocator.
  - A push with a holding report allocates only what a push without a policy does, plus the
    hash of the text.
- **Goals from the notes.**
  - Applying a 10 KB overlay with a holding report: under 1 ms on the owning thread, close
    to the 0.14 ms (0.45 ms with `shape::validator`) of a push without a policy.
  - The check itself moves to a worker.
  - Both measured with `bench/` in the D-entry.

## 5. Tests

- **0001.**
  - The pack is in `read_session()`, and `source()` gives the origin and tool.
  - With `cache_dir` set and edit mode off, nothing is written there, and a planted cache of
    the same name is ignored. The shipping job covers the shipped reader.
  - `#include` is refused, located.
  - Removing a layer below re-reads the text layer.
  - Write-back refuses.
  - The policy sees the read's warnings (an unmet `requires`).
  - Frozen with `live_layers`.
- **0002.**
  - A holding report skips the check, which a validator that counts its calls shows.
  - Each change that must invalidate a report does: a push, a removal, a toggle, a
    validator added or removed, a fast-path `set()`, `session()`, other text, another
    policy, placement below.
  - A holding refusal refuses with nothing changed.
- **0003.**
  - A checker's report equals the Config's for the same layer.
  - A checker checks on a thread while the owner pushes, removes and sets. TSan runs this
    in CI.
  - A checker outlives its Config.
  - A report from a checker taken before a change does not hold.
  - The alloc test takes a checker on its own allocator.

## 6. The owner's decisions

1. **Validators.** They opt in to running on any thread, and the owning thread runs the
   rest at the push (§3.3).
2. **The API.** A `LayerChecker` value from `Config::checker()`.
3. **Includes in received text.** Refused, in the push, `check_layer_text` and the checker
   alike.
4. **The cheaper check (§3.4).** In this work.

## 7. Steps

Each is committed green.

1. `push_layer_text`, and text reads that refuse includes (0001).
2. The cheaper check (§3.4).
3. The generation, `CheckStamp` and `LayerOptions::checked` (0002).
4. `ValidatorStack`, `add_validator` options, and `shape::validator()` without a Config.
5. `LayerChecker` (0003).
