# The base

What every stack shares (`docs/background/game-stacks.md` §2): Moira's live data loop, a
window, a renderer, Moira's tuning panels and console, and a fixed-step frame loop that
drives a game. A game is one class:

```cpp
class CubeGame final : public base::Game {
    void bind(moira::Context& ctx) override;      // script functions, before the data loads
    void reset(uint64_t seed) override;           // the starting state, before play and each run
    void load(const moira::Config& cfg) override; // structured data, after every change to the stack
    void step(double dt) override;                // one fixed simulation step; no rendering
    void draw(base::Renderer& r, float alpha) override;  // camera and boxes
};
int main(int argc, char** argv) {
    CubeGame game;  // plays, or runs an agent command: sim, check, reference, diff, promote
    return base::main(game, argc, argv, {.title = "Cube", .data_dir = CUBE_DATA});
}
```

## Pieces

| Library | Files | What |
|---|---|---|
| `stacks_core` | `live`, `watcher`, `fixed_step`, `game`, `sim` | No window, no renderer: the headless simulation (`../agent.md`) and the tests |
| `stacks_app` | `app`, `render`, `tools` | SDL3 window and input, the bgfx renderer, Dear ImGui, the frame loop, the agent's commands |
| `stacks_moira_tools` | Moira's `examples/harness-imgui/panels.cpp` and `examples/remote-console/console.cpp` | Used as they are, not copied |

- **`Live`** owns the Config and the layer stack of §2.3: `default.dta`, then `local.dta`
  (one developer's, ignored by git) and `agent.dta` (an agent's experiments), both
  optional, then Moira's scratch. It sets the session's pack levels (`engine`, `game`,
  `task`), adds the shape and `check` validators, and uses the file system's clock as the
  Config's, so a reload's origin is its file's save time.
- **`Game::load`** is called after the load and after every change to the layer stack,
  once `#autorun` has run: a game copies its structured data (a roster, an economy) into
  its own arrays there. Tweaks need nothing. **`Game::ui`** draws a game's own windows every
  frame (a game played with the mouse); `Game::tools` only while the panels show.
- **Packs** live in `<data>/../packs` (design §2.4); data files `#require` them by relative
  path, and the watcher watches that directory too.
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

**The acceptance (§2.2) is met.** On the Windows test machine (Lenovo Legion T5 28IMB05:
Intel Core i5-10400F, 6 cores and 12 threads at 2.9 GHz; 8 GB DDR4-2666; NVIDIA GeForce
GTX 1650 SUPER, 4 GB; Windows 11), with the packaged Release build (CI artifact `stacks-cube-windows`, D3D, vsync on), two groups of five saves to
`default.dta` in Notepad, on 2026-10-13:

| | Edit to see, median / worst | The reload itself, median / worst |
|---|---|---|
| Group 1 (Loop panel) | 2x / 40.8 ms (tens digit unreadable in the photo) | 12.6 / 23.6 ms |
| Group 2 (corner readout) | 25 / 40 ms (40, 29, 25, 21, 23) | |

Against the minimum spec (§1), the machine has more CPU (6 cores, not 4), the same class
of GPU, and half the memory (8 GB, not 16), so it is a fair stand-in for the loop and a
low one for memory. Under one second by a factor of 25: two or three frames at 60 Hz. The reload's own time
there, 5–24 ms against 1.4 ms on the container, is mostly its scan (Config::reload reads and
hashes every file of the layers); with one small file that is noise, and it is the first
place to look when a stack's data grows.

Windows is also covered by CI's MSVC job, which builds the stacks and passes every test,
the live-reload and watcher tests included, on a Windows runner. (Under Wine, a MinGW
cross-build fails one watcher case, a file saved in a subdirectory that existed before
watching began: Wine's emulation of recursive watches, since real Windows passes.)
