# Game stacks

Windows PC prototypes built on Moira, one stack per kind of creative work, for focused
agent-assisted development. The design is `docs/background/game-stacks.md`; this directory
is its code. It is a separate CMake project that uses Moira from this repository, and it
lives outside `src/`: Moira's line budget and core rules do not cover it.

| Directory | What | Status |
|---|---|---|
| `base/` | The shared base (design §2): live data loop, window, renderer, tuning panels, console | Built; see `base/README.md` |
| `cube/` | The base's acceptance game: a cube whose every value is a tweak | Built |
| `agent.md`, `tools/` | The agent harness (design §3): headless simulation, objectives, the commands | Built |
| `duel/` | Player versus CPU fighting on frame data (design §4.6) | Built; see `duel/README.md` |
| `ledger/` | A tycoon economy, balanced against bot strategies (design §4.2) | Built; see `ledger/README.md` |
| `contraption/` | Physics puzzles, side on, on Jolt (design §4.1) | Built; see `contraption/README.md` |
| `stuntline/` | Stunt races on tracks laid from a kit, on Jolt's vehicle (design §4.5) | Built; see `stuntline/README.md` |

## Build and run

You need CMake 3.25, Ninja or Visual Studio 2022, and network access the first time:
SDL3, bgfx, and Dear ImGui are fetched at pinned releases (`THIRD_PARTY.md`).

```sh
cmake -S stacks -B build-stacks -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-stacks
ctest --test-dir build-stacks
build-stacks/bin/stacks_cube              # Windows: build-stacks\bin\stacks_cube.exe
```

**Without a development machine:** each CI run on GitHub builds the cube for Windows and
attaches it as the artifact `stacks-cube-windows` (and each other game as `stacks-<game>-windows`) (the run's Summary page, under
Artifacts): a zip with `stacks_cube.exe`, its `data` folder, and a `README.txt` with the
test steps. It needs no installs.

With the cube running, edit `stacks/cube/data/default.dta` and save: the next frame shows
the change, and the top left shows the time from the save to that frame. F1 shows and
hides Moira's panels: tweaks, console, loop times, layers, the edit journal, shapes, and
snapshots. A value tuned in a panel can be saved back to its line in the file.

The remote console listens on 127.0.0.1:4555; each line is a Moira command:

```sh
printf '{cube.angle}\n' | nc 127.0.0.1 4555
```

## The agent's commands

Every stack's game is also its own tool (`agent.md`); an agent's instructions are in
`.claude/skills/stacks-agent/`:

```sh
build-stacks/bin/stacks_cube sim --seeds 1-10 --out runs   # scenarios in sim/, judged by metrics/
build-stacks/bin/stacks_cube check                         # JSON; exit 1 on an error or a typo'd call
build-stacks/bin/stacks_cube reference                     # the script vocabulary
build-stacks/bin/stacks_cube diff                          # what data/agent.dta changes
build-stacks/bin/stacks_cube promote [--write]             # agent.dta into default.dta: a person's call
build-stacks/bin/stacks_cube eval "{cube.turns}"           # commands on the data, headless
python3 -I stacks/tools/console.py "{cube.turns}"          # a command in a running game
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
