# Checking a Layer Before It Takes Effect

A design note for E5 of `dev/integration-plans.md` §1, which the owner chose next. A host
taking a layer from outside wants three things before it takes effect:

1. its effect, its validators' and its packs' verdicts;
2. a policy bounding what it may change;
3. a refusal that changes nothing.

The hosts that want this:
- a live game taking a balance overlay (plan D);
- an agent's proposal (the bridge's `baker_propose` today writes, checks, and restores);
- a mod loader;
- CI gating a layer file.

Oct 16, 2026. Results go into `dev/decisions.md`.

---

## 1. The API

```cpp
// What a layer may change (Config::check_layer, LayerOptions::policy). Each rule is off by
// default; a refusal names the rule, the path, and the file and line.
struct LayerPolicy {
    bool data_only = false;      // no {commands}, [properties], $variables, or #autorun
    bool scalars_only = false;   // every value an int, float, symbol, or string; no removals
    bool existing_only = false;  // change only values the stack already has; no new keys,
                                 // no removals
    bool warnings_refuse = false;  // a validator's or the read's warning refuses too
    std::span<const Path> allow{};  // if not empty, every changed path lies under one
};

struct LayerReport {
    List<LayerEffectEntry> effect;  // what it would change, as layer_effect reports it
    List<Error> refusals;           // the policy's, located; any: the layer is refused
    List<Error> warnings;           // validators over the stack with it, and the read's
    [[nodiscard]] bool ok() const noexcept { return refusals.empty(); }
};

// The check, changing nothing: the candidate on top of the stack as it stands.
[[nodiscard]] Expected<LayerReport> Config::check_layer(std::string_view name, Ref<Array> root,
                                                        const LayerPolicy& policy = {}) const;
// The same for text a host received: read as text only (never a cache, D142), with the
// session the stack's last layer read with, so its packs' requires and versions (D141,
// D147) are checked against the stack's packs; the read's warnings join the report's.
[[nodiscard]] Expected<LayerReport> Config::check_layer_text(std::string_view name,
                                                             std::string_view text,
                                                             const LayerPolicy& policy = {}) const;
```

`LayerOptions` gains `const LayerPolicy* policy = nullptr`. With it, `push_layer` checks
the layer, by path or by tree, before it enters the stack. It refuses with the first
refusal and a count of the rest, and changes nothing. A file layer's `#autorun` commands
count under `data_only`.

---

## 2. Rules, precisely

- **The candidate's leaves** are what `layer_effect` lists: each tagged array with values,
  and each removal marker.
- **`data_only`** refuses:
  - any Command, Property or Var node anywhere in the candidate, located at its array;
  - for a file layer, any `#autorun` command.
- **`scalars_only`** refuses:
  - a leaf value that is an array, a command, a property or a variable;
  - a removal, because removing is not changing a value.
- **`existing_only`** refuses:
  - a leaf whose path the stack does not set (no `below` value);
  - a removal.
- **`allow`.** Every leaf's path must have one of the paths as a prefix, segment by
  segment.
- **Warnings.**
  - **Validators.** The Config's validators run over the stack merged with the candidate,
    and what they report is a warning.
  - **The read.** In the text form, the read's warnings join them: a pack's unmet
    `requires` or old version, or the one-way rule.
  - **`warnings_refuse`** turns every warning into a refusal: a live game should not take
    an overlay made for another base.
- **The effect** is computed against the merged stack as it stands, scratch included,
  with the candidate on top. The rare host that places a checked layer lower sees its
  effect as if on top: the check is about what the layer says.

---

## 3. Cost and shipping (acton-review)

- **Path class:** tool, or load for a live overlay. It runs when a layer arrives, never
  per frame.
- **Allocation.** One merge (the stack with the candidate), the validators, and one walk of
  the candidate, all through the Config's allocator. A 10 KB overlay should check in well
  under a millisecond; measure it with `bench/` in the D-entry.
- **Shipping.**
  - It is available: it is what makes `live_layers` safe there.
  - Validators run in shipping as they do today.
  - The text form reads as text only, so an untrusted overlay never reaches the cache
    loader.
- **Nothing on a hot path changes.** `push_layer` without a policy is unchanged.

## 4. Tests

- **Each rule, alone:**
  - a command, a property, a variable, and an `#autorun` under `data_only`;
  - an array value and a removal under `scalars_only`;
  - a new key and a removal under `existing_only`;
  - a path outside `allow`.
- **Warnings:** a validator's warning, and `warnings_refuse`.
- **Packs:** in the text form, an overlay whose `requires` names a newer base version is
  warned about, and refused under `warnings_refuse`.
- **Changing nothing:** `push_layer` with a policy refuses, the stack, `epoch()` and every
  tweak are unchanged, and the same layer without the violation is accepted.
- **Shipping:** after `freeze()` with `live_layers`, a checked overlay is pushed, and a
  refused one changes nothing.
- **Allocation:** `baker_alloc_tests` checks a layer through its own allocator.
