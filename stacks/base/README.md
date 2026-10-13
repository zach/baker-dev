# The base

What every stack shares (`docs/background/game-stacks.md` §2): Moira's live data loop, a
window, a renderer, Moira's tuning panels and console, and a fixed-step frame loop that
drives a game. A game is one class:

```cpp
class CubeGame final : public base::Game {
    void bind(moira::Context& ctx) override;      // script functions, before the data loads
    void step(double dt) override;                // one fixed simulation step; no rendering
    void draw(base::Renderer& r, float alpha) override;  // camera and boxes
};
int main(int argc, char** argv) {
    CubeGame game;
    return base::run(game, base::parse_args(argc, argv, {.title = "Cube", .data_dir = CUBE_DATA}));
}
```

## Pieces

| Library | Files | What |
|---|---|---|
| `stacks_core` | `live`, `watcher`, `fixed_step` | No window, no renderer: tests and the coming headless runner (§3.1) link only this |
| `stacks_app` | `app`, `render` | SDL3 window and input, the bgfx renderer, Dear ImGui, the frame loop |
| `stacks_moira_tools` | Moira's `examples/harness-imgui/panels.cpp` and `examples/remote-console/console.cpp` | Used as they are, not copied |

- **`Live`** owns the Config and the layer stack of §2.3: `default.dta`, then `local.dta`
  (one developer's, ignored by git) and `agent.dta` (an agent's experiments), both
  optional, then Moira's scratch. It sets the session's pack levels (`engine`, `game`,
  `task`), adds the shape and `check` validators, and uses the file system's clock as the
  Config's, so a reload's origin is its file's save time.
- **`Watcher`** asks the operating system which `.dta` files changed
  (`ReadDirectoryChangesW` on Windows, inotify on Linux), so a frame calls
  `Config::reload` only after a save. Polling instead, as Moira's harness example does,
  reads and hashes every file four times a second and adds up to 250 ms to every edit.
- **`FixedStep`** counts integer nanoseconds: the same elapsed time gives the same steps at
  any frame rate. (Its first version summed seconds as doubles, and its own test caught 59
  steps at 144 Hz against 60 at 30 Hz.)
- **`Renderer`** is bgfx behind a small API (camera, lit boxes, debug text, ImGui, PNG
  screenshots), with no bgfx type in its header. It runs bgfx single-threaded. Its
  shaders are bgfx's own precompiled ones (the debug-draw lit fill and the ImGui shader,
  for every renderer), so the build runs no shader compiler.
- **`run`** is the frame: events; `Live::begin_frame` (reload what was saved); the
  remote console; fixed steps; ImGui panels (F1); draw; present; `Live::end_frame`, which
  calls `Config::mark_visible`, so `loop_stats()` measures save to presented frame.

## Not in the base yet

The design's §2.1 also lists Jolt, Recast/Detour, miniaudio, and Tracy. None of the first
two stacks needs them (Duel collides boxes; Ledger has no physics or navigation), so each
comes in with the first stack that does, once, in the base. Likewise the `spec/low.dta`
layer, `moira_bake` caches, and a `shipping` preset wait for the first stack to ship a build.

## Measured

Debug build, on the development container (Linux, Xeon), not yet on the minimum-spec PC:

| | |
|---|---|
| Save to the next frame, headless (`test_live`) | 0.2–0.3 ms |
| Save to presented frame, OpenGL with vsync under Xvfb (software rendering) | 16.6 ms: the next vsync |
| The reload itself (one 0.5 KB file: scan, read, parse, merge, bind) | 1.4 ms |

The acceptance in §2.2 (under one second, median, on the minimum-spec PC) still needs that
PC: Windows itself is covered so far by a MinGW cross-build, run under Wine (9 of the 10
base test cases pass there; the one failure is a file saved in a subdirectory that existed
before watching began, which Wine's emulation of recursive watches may not report), and by
CI's MSVC job.
