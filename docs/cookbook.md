# Moira cookbook

Patterns for using Moira well. The full cookbook is milestone M9; these sections exist
because the code they describe does.

## Group parameters under the code that owns them

Put a subsystem's parameters under one tag (`(renderer ...)`, `(audio ...)`) and have only
that subsystem read them. Reading another subsystem's parameters is bad form even though a
global database makes it possible. Moira enforces nothing, but hot reload depends on it:
`cfg.on_reload("renderer", rebuild)` fires when the `renderer` subtree changes, so a group
is the unit that gets refreshed and notified together.

## Personal layers: `local`

Each developer gets a layer of their own, for tuning without touching shared files or
blocking anyone. It is a convention, not a mechanism: Moira never looks up user names.

```cpp
cfg.push_layer("default", "config/default.dta");
cfg.push_layer("platform", "config/ps5.dta");
cfg.push_layer("local", "config/local/" + user + ".dta", {.optional = true});
cfg.push_layer("patch-2026-10-11", overlay);   // live patches go above local
```

- Push it above the shared layers and below live patches.
- `{.optional = true}` makes a missing file an empty layer. The next `reload()` picks the
  file up once it exists, and `write_back_to(path, "local")` creates it.
- Keep the directory out of version control:

  ```gitignore
  config/local/
  ```

- To keep a value for yourself, tune it, then `write_back_to(path, "local")`. To share an
  experiment, `promote("local", "default")` writes its entries into the shared file as one
  reviewable diff, or nothing if any entry cannot be written.

## Experiments and A/B comparison

Put a variant in its own layer and flip it:

```cpp
cfg.push_layer("boss-faster", "config/experiments/boss-faster.dta");
cfg.toggle_layer("boss-faster");               // A
cfg.toggle_layer("boss-faster");               // B
auto changes = cfg.layer_effect("boss-faster"); // what it actually changes
auto ab = cfg.diff("boss-slower", "boss-faster");
```

A disabled layer keeps its place and is skipped by merging, provenance, and write-back.
Live tuning goes to the `scratch` layer, so "my tuning versus the files" is also a toggle:
`cfg.toggle_layer("scratch")`.

`cfg.snapshot()` and `cfg.restore(s)` capture and return to an exact state (the layer
stack, each layer's trees, and scratch) without reading files. Two snapshots make A/B
comparison of two tuned states one call each.
