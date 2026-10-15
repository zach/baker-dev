# Baker cookbook

Patterns for using Baker well. The [language guide](language.md) says what each piece is;
this says how they fit a studio's day. Each pattern links the example that runs it.

- [Group parameters under the code that owns them](#group-parameters-under-the-code-that-owns-them)
- [Tweaks in practice](#tweaks-in-practice)
- [Reload callbacks: migrate or punt](#reload-callbacks-migrate-or-punt)
- [Personal layers: `local`](#personal-layers-local)
- [Experiments and A/B comparison](#experiments-and-ab-comparison)
- [Live ops](#live-ops)
- [The binary cache in a build step](#the-binary-cache-in-a-build-step)
- [Linking a shipped game](#linking-a-shipped-game)
- [Harness packs](#harness-packs)
- [A developer console over `eval`](#a-developer-console-over-eval)
- [Repro scripts](#repro-scripts)

## Group parameters under the code that owns them

Put a subsystem's parameters under one tag (`(renderer ...)`, `(audio ...)`) and have only
that subsystem read them. Reading another subsystem's parameters is bad form even though a
global database makes it possible. Baker enforces nothing, but hot reload depends on it:
`cfg.on_reload("renderer", rebuild)` fires when the `renderer` subtree changes, so a group
is the unit that gets refreshed and notified together.

## Tweaks in practice

A tweak replaces a constant where the constant was:

```cpp
static baker::Tweak<float> kJumpHeight{"player.jump.height", 1.2f,
                                       {.range = {0.5, 3}, .step = 0.1, .desc = "Peak, in m"}};
```

- **Declare tweaks at namespace scope.** A function-local static adds a guard check to
  every read. At namespace scope, reading a `Tweak<float>` is one relaxed atomic load,
  checked in the generated assembly by a test.
- **The default is the old constant.** A path the data does not set keeps it, and that
  is never an error: a tweak can go in before anyone writes its data.
- **Metadata belongs in the data once someone tunes it.** The code's `TweakMeta` is a
  starting point; `(height 1.4 (range 0.5 4) (note "4 breaks the level 3 ceiling"))` in
  the data wins, and `note` exists only there: a remark from whoever tuned the value for
  whoever tunes it next. A value of several numbers takes metadata the same way:
  `(tint 0.9 0.5 0.2 (range 0 1))` is three numbers, which a tool shows as a color (D88).
- **Types.** Any `T` with a `Convert<T>` works. One whose `std::atomic` is lock-free reads
  with a load; another (`std::string`, `std::vector<float>`) reads through `get()`, whose
  reference stays valid until the next reload.
- **`on_change`** runs after the value changes, on the thread that changed the Config,
  for the few values whose change must do something (resize a buffer).
- **`dev_only`** marks a tweak a shipped build must not take from data: under
  `BAKER_SHIPPING` it keeps its default, and `set()` is refused. Use it for cheats and
  debug switches.
- **Tests** give a tweak its own `TweakRegistry` and Config, so they do not share the
  global one.

`examples/one_line_tweak` and the [README's five-minute path](../README.md) run them.

## Reload callbacks: migrate or punt

When a group's data changes, its subsystem either rebuilds from scratch (*punts*) or moves
its runtime state onto the new values (*migrates*). Punting is the default and needs no
code beyond the rebuild:

```cpp
cfg.on_reload("renderer", [&] { renderer.rebuild(); });   // punt
cfg.on_reload(
    "audio", [&] { audio.restart(); },                     // the punt, if migration declines
    [&] {                                                  // migrate first
        if (!audio.can_retune(cfg)) return baker::Reload::Punt;
        audio.retune(cfg);
        return baker::Reload::Migrated;
    });
```

- A callback fires only when its group's subtree changed, after every tweak has its new
  value, for a reload, a layer pushed, removed, or toggled, and a restore alike.
- Punt first. Migrate only where a rebuild loses state someone is looking at (a playing
  sound, a camera mid-move), and let migration decline when the change is too large.
- A reload that fails to parse changes nothing: no callback fires, and the old tree stays.
- `cfg.on_reload_complete(...)` sees each change's `ReloadRecord`: six timed stages (scan,
  read, parse, merge, bind, notify), the files reloaded, the tweaks changed, and how many
  callbacks migrated or punted. Give it `ReloadRequest{.origin_ns = saved_at}` from a file
  watcher, and the record measures edit to effect.
- Call `cfg.mark_visible()` once a frame, when it presents: it costs one branch when
  nothing is waiting. `cfg.loop_stats()` then gives, per file and per layer, the median and
  worst time from a saved edit to the frame that showed it, over the last 32 edits, with
  failed reloads counted (D92). Polling for changes does not dilute it.

`examples/config` runs a reload that rebuilds the audio group and leaves the renderer alone.

## Personal layers: `local`

Each developer gets a layer of their own, for tuning without touching shared files or
blocking anyone. It is a convention, not a mechanism: Baker never looks up user names.

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
- Before quitting, check what was tuned and not kept: `cfg.unsaved()` lists the paths, and
  `cfg.journal(fn)` every live edit of the session (up to the last 256) with its old and
  new values, where the old one came from, and whether it was written, reverted, or
  superseded (D93). A harness can warn at exit, and the journal's counts say how much
  tuning was kept.

`examples/config` writes a value into a `local` layer that did not exist;
`examples/harness-imgui` pushes `local/<user>.dta` this way.

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

## Live ops

A shipped game changes its data without a new build by taking **overlays**: small files of
only what changes, each pushed as a layer of its own above everything shipped. A layer is
the whole mechanism: merging applies it, provenance names it, `layer_effect` says what it
changes, and removing it takes it back. `examples/live-ops` runs each step below.

### Environment boundaries

Development, QA, and live are separate layer stacks, built from separate directories, and
the difference between them is what each is allowed to change after it starts.

```
config/default.dta           shared, checked in, shipped
config/platform/ps5.dta      shipped
config/local/<user>.dta      development only, never checked in
overlays/qa/...              pushed to QA builds
overlays/live/...            pushed to players, after QA
```

- A development build reloads files, takes live edits, and writes back.
- A shipped build loads its data, then calls `freeze()`. After that, reloads, layer
  changes, and live edits are refused, unless `ConfigOptions::live_layers` allows layer
  changes: `Config({.live_layers = true})` is the one door overlays come through. Under
  `BAKER_SHIPPING`, reload, toggles, `set()`, write-back, and `promote()` are inert
  whatever the options say, and `dev_only` tweaks keep their defaults.
- An overlay arrives as text from the studio's server; read it with
  `read_string(text, "patch-2026-10-17.dta")`, whose name labels its errors and its
  provenance, and push the tree: `cfg.push_layer("patch-2026-10-17", tree)`.
- Do not push a QA overlay to live by copying it into the live directory by hand; promote
  it through the same review a code change gets (below).

### Version history

- **Overlays are files in version control**, one per change, named by the date it ships
  and what it does: `overlays/live/2026-10-17-double-xp.dta`. A comment at the top says
  when it goes out and when it comes back. The history of live data is then `git log
  overlays/live`, and each change has its review.
- **The layer's name is the file's name**, so `source()` on any value in a player's game
  names the overlay that set it, and a bug report can carry it.
- **`snapshot()` beside a repro.** When QA captures a bug, capture the Config's state with
  it; restoring the snapshot gives back the exact values, overlays included, without
  reading files that may have changed since.

### Rollback

```cpp
cfg.remove_layer("2026-10-17-double-xp");
```

The values below the overlay return, the tweaks rebind, and reload callbacks fire, as for
any layer change. Rollback leaves no trace because an overlay never modified anything: the
shipped trees are immutable, and the merged tree is recomputed without it. That is why
overlays are layers and not edits to a shipped file. Keep overlays independent (each
setting only its own values) so that removing one never needs another removed first.

### Tests before an overlay ships

- **Shapes.** Declare, in the shipped data, the shapes of everything an overlay may set,
  with ranges: `(event (drop_rate float (range 0 1)))`. Before pushing, merge the overlay
  over the current root and check it: `examples/live-ops` refuses an overlay that sets a
  15% drop rate as `15`. In a development build, `cfg.add_validator(baker::shape::validator())`
  checks every push.
- **`check`.** An overlay that carries script (`(on_event {grant_xp 2})`) gets
  `baker::check` against a Context with the game's functions bound: a wrong call is an
  error before a player runs it. `cfg.add_validator(baker::check_validator(ctx))` checks
  every push in a development build.
- **In CI**, run both over every file under `overlays/`, stacked on the shipped data, on
  every change. An overlay that fails never reaches the server.

### Personal and experiment layers

Developers tune in their own [`local` layer](#personal-layers-local), and variants live in
[experiment layers](#experiments-and-ab-comparison). Both sit below the live overlays, so
a developer reproducing a live problem sees what players see, with their own tuning on
top only if they enable it.

## The binary cache in a build step

Text is for people; a shipped game can load the same trees from binary caches, one read
and one allocation each: a 6.2 MB tree in 2.3–2.5 ms (D79).

- **A file at a time:** `baker_bake [-D NAME]... [-I DIR]... in.dta out.datb` in the build,
  and `baker::read_cached(text_path, cache_path, session)` in the game, which uses the
  cache when it is fresh and the text otherwise. Give `baker_bake` the same defines as the
  game, or the cache is stale for it.
- **A cache is a memory image**, for one byte order and pointer size: bake it with the
  target's layout. Today's consoles and PCs are all little-endian and 64-bit, so a bake
  on a development PC serves them. A cache from another layout is refused with an error
  naming it; `read_cached` then reads the text, if the game shipped it (D105).
- **A Config's layers:** `ConfigOptions::cache_dir` keeps one cache per file layer, used
  outside edit mode. Each layer is read with the macros and defines of the layers below
  it, so bake the layers the way the game stacks them: a build step runs a development
  build's Config with the same layer paths and `cache_dir`, then the directory ships.

  ```cpp
  baker::Config bake({.edit_mode = false, .tweaks = nullptr, .cache_dir = "build/datb"});
  bake.session().define("PS5");
  for (auto& [name, path] : layers) bake.push_layer(name, path);   // writes the caches
  ```

  The shipped game builds the same Config with the same `cache_dir`. Under
  `BAKER_SHIPPING` it loads each layer from its cache without re-reading the files. It
  does check that the session begins as the bake did, in memory: the same macros, pack
  levels, and required files (D139). A cache baked for another session is set aside for
  the text, with a warning, or refused if there is no text. Caches are named by the
  normalized layer path, so the bake and the game may spell paths differently (D140).
- A cache holds the expanded tree and replays the defines, macros, and `#autorun` commands
  its load made. It is build output: it detects damage, not tampering, so text from a
  source you do not trust goes through the parser. A `Cipher` (`LoadSession::cipher`)
  enciphers caches, if a game wants players not to edit them.

## Content packages: patches, DLC, and mods

How a game delivers content after it ships, from `docs/background/content-updates.md`:

- **Each package is a layer:** the base game, then each DLC, then title updates, then live
  ops. A package installed later still goes in its place: `push_layer("pack3", path,
  {.below = "tu1"})` puts a new DLC under the title updates, so their fixes to it win
  (D144). A DLC that may be absent is pushed with `LayerOptions::optional`. Code that reads
  its content takes a fallback (`get(path, fallback)`), so a save that names an
  uninstalled DLC's content still loads.
- **Anything packages extend is a keyed table.** Two layers that set one value list leave
  the top one's list, because Overwrite replaces values. Keyed children merge, so every
  package's entries survive. This is how Rock Band's `songs.dta` scaled to thousands of
  downloadable songs:

  ```dta
  ; base: (songs (song_a (bpm 120)) (song_b (bpm 90)))
  ; dlc_pack1.dta:
  (songs (song_c (bpm 140)))
  ; merged: (songs (song_a ...) (song_b ...) (song_c ...))
  ```

  A value list such as `(setlist song_a song_b)` is right for something one package owns.
  `layer_effect` shows a layer that replaces another's list.
- **A title update removes with `(key #remove)`**, a debug menu or an exploit's entry, and
  the key stays gone until the layer is removed (D143).
- **A title update changes values, not the macros below it.** A layer reads with the macros
  of the layers below it. A macro a title update redefines reaches only the layers above
  it, never the base that already expanded it.
- **Run autorun with `Context::run_autorun(cfg.autorun().items())`** after every stack
  change. Script an uninstalled DLC defined goes with it, and what it replaced comes back
  (D146).
- **Declare what a package requires.** `(pack expansion (level dlc) (requires
  season_pass))`. A requirement that is not loaded once the pack's file is read is a
  located warning, forwarded to the Config's warning handler (D141).
- **Mods and user files are text, never caches.** A cache is a memory image that a forged
  file can point anywhere. A loader of content from outside the game's own packages sets
  `LoadSession::text_only`, which refuses every cache and parses the text, which no input
  can make crash the reader (D142). The game's own packages are signed by their platform.

## Linking a shipped game

Baker's libraries are static, and the tools share them with the runtime (D100). A game
pays only for what it links:

- **Tools in their own files cost nothing.** `check`, its JSON, the reference, and
  `find_repeats` are in object files a game never references, so the linker leaves them
  out: 0 bytes.
- **`Config`'s tools come with `Config`.** Write-back, `promote`, `diff`, and
  `layer_effect` share an object file with the loader. With GCC or Clang, compile with
  `-ffunction-sections -fdata-sections` and link with `-Wl,--gc-sections`, and the linker
  drops the ones the game never calls. MSVC's release defaults (`/O2` implies `/Gy`, and
  the linker uses `/OPT:REF` unless given `/DEBUG`) already do.

  ```cmake
  target_compile_options(baker_data PRIVATE -ffunction-sections -fdata-sections)
  target_compile_options(baker_script PRIVATE -ffunction-sections -fdata-sections)
  target_link_options(game PRIVATE -Wl,--gc-sections)
  ```

A minimal game (load, read, a tweak, a function, an object, `eval`) built with
`BAKER_SHIPPING` links 580 KB of Baker code, 41.5 KB of it tooling; with sections, 407
KB, 4.8 KB of it tooling (the source maps that loading reaches).

## Harness packs

A harness is the set of data, macros, and functions a team builds to make one kind of work
fast: a boss-tuning harness, an economy harness. Packs give harnesses a structure:

```dta
; harness/combat_core.dta
(pack combat_core (level game))
[combat.hit_pause 0.08]
{func combat.knockback ($force) {* $force 1.5}}
```

```dta
; harness/boss_tools.dta
(pack boss_tools (level task) (requires combat_core))
#require "combat_core.dta"
[boss.restart_phase {boss_phase_restart}]
```

- **`#require`** a pack from every file that uses it: it is read once per session however
  many files require it, and its macros serve them all.
- **Levels** are the session's: `cfg.session().set_pack_levels({"engine", "game", "task"})`,
  before pushing layers. A game-level pack that expands a task pack's macro, or calls its
  function, is warned about, located: engine and game harnesses must not depend on one
  task's tools. Run `check` with the session to see calls:
  `baker::check(*cfg.root(), ctx, report, {.packs = &cfg.read_session()})`.
- **Prefix** a pack's names with its subject (`boss.`, `combat.`); the reference and
  tuning panels group by prefix.

## A developer console over `eval`

`ctx.eval(text)` parses and runs a command and returns its value, or a located error with
the script stack. A console is that, fed from a text box or a socket:

- Poll the socket once a frame, on the thread that owns the Context, so commands run
  between frames like any other script. `examples/remote-console` does it in about a
  hundred lines on BSD sockets and Winsock, listening on 127.0.0.1 only: a console runs any
  command it is sent, so it is a development tool, not a shipped port.
- Name the input (`ctx.eval(line, "<console>")`) so errors say where they came from, and
  print `describe(error)` for the stack.
- Bind what designers ask for as they ask for it; `check`'s demand report over the data
  lists what they have already written calls to.
- `examples/harness-imgui` has the same console as a panel.

## Repro scripts

A command is already a serialized, parseable, evaluable value, so a sequence of console
commands is a script that reproduces what was done:

- Log each command a console or tool runs, one per line, to a `.dta` file. Running it with
  `ctx.run((*baker::read_file("repro.dta"))->nodes())` replays it.
- Begin it with the state: the Config snapshot QA captured, or the overlays that were live
  (`cfg.layers()`). A repro of data that has since changed reproduces nothing.
- Drive time through the scheduler the script uses (`{wait_for 2}` in a `thread_task`), and
  tick it from the test with fixed steps, so a replay does not depend on frame timing.
- A repro that runs in CI against the fixed build is a regression test, written in the
  language the bug was found in.
