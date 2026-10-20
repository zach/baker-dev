# Plan: `baker::host`, the host kit

*The owner's decision on D184's open question (D186): an optional library beside the core,
in this repository, for what every host rewrites.*

**Status: done.** Steps 1 to 6 are D187 to D190, D192 and D194; D191 and D193 added what
the Godot and editor integrations asked of it along the way.

| Decision | Choice |
|---|---|
| Scope of the first version | The file watcher, the loopback console, the live loop, and the tool commands |
| Under `BAKER_SHIPPING` | The watcher and the console are inert; the rest works |
| Where, and its name | `baker::host`, in `src/host/` and `include/baker/host/`, beside `baker::shape` |

## 1. Why

- **Every host rewrites these pieces.** Layer 2 (`dev/tooling-layers.md`) holds the
  operating system and policy, and some of that is the same in every game:
  - watching the files the stack was read from, and reloading with the save's time;
  - serving one-line requests on loopback;
  - opening the project with validators and tools bound, and running `#autorun` after
    each change;
  - the commands a tool runs against the game binary.
- **Where the copies are today:**
  - the stacks' `base` (`Live`, `Watcher`, `tools.cpp`);
  - `examples/remote-console`, which the stacks compile from the example's directory;
  - the Godot integration, which is writing its own watcher and console.
- **The cost of copies.** They drift, and each new host pays for the same bugs. The
  `initializer_list` lifetime fault (D185) was found once, in the Godot integration;
  the next host would meet it again.
- **Why not the core.** The pieces touch the operating system, which the core's I/O rule
  keeps out. So they go in a library of their own, which a host links or not.

## 2. Shape

- **The target.** `baker::host` (`baker_host`) has the option `BAKER_BUILD_HOST`, ON by
  default.
  - **Dependencies:** `baker::script` and `baker::data`. It uses `baker::shape` when that is
    built (`BAKER_HOST_SHAPE`), for the shape validator and the shape half of `check`.
  - **What `baker` brings:** the umbrella target `baker::baker` does not link it, so a game
    that wants only the core gets only the core.
- **Rules.** The kit follows the core's rules, so it reads like the rest of Baker:
  - no exceptions;
  - warnings as errors, `clang-format`, and `clang-tidy` over `src/host/`;
  - Baker's containers and `Callback` in its sources.
  - **Its public headers** reach only the standard headers D55 allows, checked by
    `check_headers.py`, and compile alone.
  - **Its sources** may include operating-system headers (inotify, Winsock,
    `ReadDirectoryChangesW`) and use `std::filesystem` through its `error_code` overloads.
- **A line budget of its own.** 1,500 lines, reported by `tools/count_lines.py` beside
  `baker::shape`'s and outside the core's 15,000.
- **It ships.** `src/` and `include/` are in the release manifest already, so the kit is
  in every release.
- **Shipping builds.**
  - **Inert:** `Watcher::poll` never reports a change, and `Console::listen` refuses (so no
    socket opens in a shipped game).
  - **Still works:** the live loop opens the project, takes live-ops overlays through
    `live_layers`, and runs `#autorun`; the commands that only read (`check`,
    `reference`, `eval`).
  - **Inert like the core's write tools:** `diff` and `promote`.
- **Frame cost.**
  - **When nothing changed:** `Watcher::poll`, `Console::poll` and `Live::begin_frame`
    allocate nothing and make one non-blocking system call each.
  - **Tests:** `baker_alloc_tests` gains a host case, and the frame cost is measured
    with cachegrind.

## 3. The pieces

### 3.1 `Watcher`: the files the stack was read from

- **The API:**
  - `watch(std::span<const StackFile>)` sets what to watch: a Config's `stack_files()`,
    plus any path the host adds (the project file).
  - `poll(Changes&)` reports what changed, with the newest save time.
- **How it watches.** It watches the files' parent directories, and reports only the files
  in the list.
  - **No spurious reloads:** an editor's swap file, or a `.dta` nothing reads, reloads
    nothing.
  - **Nothing missed:** an include directory outside `data/` is watched, and so is a
    missing optional layer's file appearing.
- **Following the stack.** The live loop re-calls `watch` when `stack_files_epoch()` moves.
  Directories already watched keep their handles.
- **Platforms:**
  - inotify on Linux and `ReadDirectoryChangesW` on Windows, from the stacks' watcher, as
    it is;
  - elsewhere (macOS, BSD), a check of each file's write time and size, at most once every
    100 ms, with no thread.
  - **Paths with a scheme** (`user://`) are not on the disk. The host gives a stamp hook
    (path to write time and size) for those, and they are checked the same way.

### 3.2 `Console`: one-line requests on loopback

- **Where it comes from.** `examples/remote-console`'s `Console` and `sockets.hpp` move in,
  as they are.
- **Behaviour:**
  - it listens on 127.0.0.1 only, with port 0 meaning "any free port";
  - `poll()` once a frame;
  - a request line, `(ID {command})`, gets `eval_request`'s JSON reply;
  - a bare line gets the human reply.
- **The reply separator is documented, not changed** (D188). Every reply, JSON or not,
  is its lines, then an empty line. The plan first had JSON replies drop the empty line,
  but the example's client and possibly other tools read up to it. Dropping it could leave
  a client waiting forever, while keeping it costs a tool only a skipped empty line.
- **The example keeps working.** `examples/remote-console` becomes a 40-line user of the
  kit.

### 3.3 `Live`: the edit-to-see loop

- **Where it comes from.** The stacks' `base::Live`, made general.
- **Opening the stack** (`open(LiveOptions)`):
  - it finds the project with `find_project` from a given path, or takes a `Project` the
    host built (a packaged game's built-in stack);
  - it opens it with the host's `ProjectOptions`;
  - it adds the check validator, and the shape validator when built;
  - it binds `bind_config_tools`, with writing only when the host asks (`console_write`);
  - it runs every enabled layer's `#autorun`;
  - it starts the watcher and, if a port is given, the console.
- **Each frame:**
  - `begin_frame()` polls the console and the watcher, reloads with the save's time as the
    origin, and runs `#autorun` again after any change to the stack;
  - `end_frame()` calls `mark_visible()`.
- **What the host gets:**
  - the Config;
  - a `changed` callback, where the game reads its structured data;
  - the log;
  - the load's located errors, for the check gate;
  - `loop_report()`.
- **Not in it:** the stacks' fixed step, their panels, their `local`/`agent` layer
  convention, and their `sim/` and `metrics/` directories. Those stay in the stacks: the
  kit takes whatever stack the project file describes.

### 3.4 The commands

- **The entry point.** `int run_command(int argc, char** argv, const CommandHooks&)` is
  what a game's `main` calls when its first argument is a command.
  - It returns the exit status: 0, 1 when something failed, 2 when it could not run.
  - Every command takes `--project PATH`.
- **The commands:**
  - `check`: `baker.check/1.2` JSON, with load problems, shapes, and unresolved calls
    failing. It also checks extra trees the host names (the stacks' scenarios and
    objectives).
  - `reference [--json]`: the Markdown, or `{reference, tweaks, shapes}`, as the stacks'
    build already writes it (D177).
  - `diff [--layer NAME]`: what one layer changes.
  - `promote --from A --to B [--write | --patch FILE]`.
  - `eval [--seed N] [--json] "{...}"...`.
- **The hooks:**
  - `bind(Context&)`: the game's script functions, so `check` and `reference` know them;
  - `reset(seed)`: optional, the game's state before `eval`;
  - `extra_trees(...)`: optional, files the game wants checked beside its data;
  - `more(command, argc, argv)`: optional, commands of the game's own. The stacks' `sim`
    and `compare` go there.
- **Compatibility.** The stacks' JSON (`baker.check/1.2`, `baker.reply/1.1`, the reference
  object) is unchanged, so the editor, the agent bridge and the stacks' tests read the
  same bytes. `stacks.sim/1.1` and `stacks.compare/1.1` stay the stacks'.

## 4. Steps

Each step is committed green (`tools/local_matrix.sh`), with a D-entry and measurements.

1. **Scaffold and `Watcher`.**
   - The target, the option, the line budget, the header gate, a CI build on Linux and
     Windows, and `tests/host/`.
   - The watcher, from the stacks' with the file filter and the polling fallback.
   - Tests: an edit, an include, a file that appears, a swap file ignored, the stamp
     hook, inert under shipping.
2. **`Console`.** Moved from the example.
   - Tests: a request answered, a bare line, two clients, an incomplete line kept,
     loopback only, inert under shipping.
   - `examples/remote-console` becomes its user.
3. **`Live`.**
   - Tests: open from a project file and from a built `Project`; reload on a watched edit
     with its origin; `#autorun` after a change; console write-back only when asked.
   - Allocation: none on a quiet frame (`baker_alloc_tests`).
   - Cost: cachegrind on a quiet frame.
4. **The commands.**
   - Tests: each command against a small game in `tests/host/` (two script functions, one
     shape). The JSON is compared with the stacks' current output, so the bytes are kept.
5. **The stacks move onto the kit.**
   - `base::Live`, `base::Watcher` and the shared half of `tools.cpp` go, along with
     `stacks_baker_tools`'s console.
   - `sim` and `compare` become `more` commands.
   - All 30 stacks tests pass unchanged, including `test_commands.py`.
   - Edit-to-see: the median of `loop_stats()` on the cube, headless, before and after.
6. **Docs and the integrations.**
   - `docs/tools.md` names the kit as the host half of a game's tooling, with a short
     section per piece.
   - The cookbook's "Watching files yourself" points at it.
   - The editor and baker-godot sessions are told what they can drop; baker-godot keeps
     its own file access through the stamp hook.

## 5. Acceptance

- **A small host.** A new game gets reloading, a console and the tool commands from about
  fifty lines of host code. `examples/host` shows it.
- **The stacks** run on the kit with every test passing, and `stacks/base` is smaller by
  the pieces that moved.
- **A quiet frame** allocates nothing and makes no more system calls than today. Measured.
- **Shipping:** no socket opens, and no watch is set.
- **Budget:** the kit stays within 1,500 lines.

## 6. Not in it

- **Panels.** The ImGui panels (`examples/harness-imgui`) depend on Dear ImGui, and stay an
  example.
- **A protocol beyond the console's lines:** no HTTP, no language-server protocol. Those
  are an external tool's.
- **Threads.** Everything runs on the caller's thread, once a frame.
