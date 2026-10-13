# Game stacks

Windows PC prototypes built on Moira, one stack per kind of creative work, for focused
agent-assisted development. The design is `docs/background/game-stacks.md`; this directory
is its code. It is a separate CMake project that uses Moira from this repository, and it
lives outside `src/`: Moira's line budget and core rules do not cover it.

| Directory | What | Status |
|---|---|---|
| `base/` | The shared base (design §2): live data loop, window, renderer, tuning panels, console | Built; see `base/README.md` |
| `cube/` | The base's acceptance game: a cube whose every value is a tweak | Built |
| `duel/`, `ledger/` | The first two stacks (design §4.6, §4.2) | Next, after the agent harness (§3) |

## Build and run

You need CMake 3.20, Ninja or Visual Studio 2022, and network access the first time:
SDL3, bgfx, and Dear ImGui are fetched at pinned releases (`THIRD_PARTY.md`).

```sh
cmake -S stacks -B build-stacks -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-stacks
ctest --test-dir build-stacks
build-stacks/bin/stacks_cube              # Windows: build-stacks\bin\stacks_cube.exe
```

With the cube running, edit `stacks/cube/data/default.dta` and save: the next frame shows
the change, and the top left shows the time from the save to that frame. F1 shows and
hides Moira's panels: tweaks, console, loop times, layers, the edit journal, shapes, and
snapshots. A value tuned in a panel can be saved back to its line in the file.

The remote console listens on 127.0.0.1:4555; each line is a Moira command:

```sh
printf '{cube.angle}\n' | nc 127.0.0.1 4555
```

Options every stack's game takes:

| Option | Effect |
|---|---|
| `--data DIR` | The game's data directory (default: its own `data/`) |
| `--renderer NAME` | `auto` (default: bgfx picks the platform's best), `d3d11`, `d3d12`, `vulkan`, `gl`, `noop` |
| `--headless` | No window and the Noop renderer: for tests and CI |
| `--frames N` | Quit after N frames |
| `--screenshot PATH` | Write the last frame (frame 30 without `--frames`) as a PNG |
| `--console-port N` | The remote console's port; 0 turns it off |
| `--no-vsync`, `--step-hz N`, `--no-tools` | Present unthrottled; the simulation rate; start with the panels hidden |
