# Game Stacks on Moira

Six prototype development environments ("stacks") built around Moira, one per kind of
creative work, for focused agent-assisted development. Every game targets Windows PC only,
for now; §10 keeps what a later console port would need.

Oct 13, 2026. A design proposal; §9 records what the owner has decided and what is open.

Companion to `harness-tree-vision.md` (layers L0–L4, the four properties of a harness),
`reading-a-studio.md` (loop telemetry, the harness register), and `pushing-up.md`. Code in
`.dta` blocks follows `docs/language.md`; the vocabulary in it (`duel.`, `econ.`, and
so on) is illustrative, not a spec.

---

## 0. What a stack is

A stack is a harness tree cut down to one task family, plus the environment an agent
needs to work inside it:

```
L0  Moira                          shared, unchanged
L1  Base engine harness            shared by all six stacks (§2)
L2  Game harness                   one per stack: the genre's vocabulary, in C++ and packs
L3  Task harness                   one or more per stack: workbenches, written mostly in data
L4  Instance                       levels, fights, tracks, missions, price tables
──  Agent harness                  cross-cutting (§3): headless sim, metrics, tools, rules
```

Three rules hold for every stack:

1. **The loop comes first, on the minimum-spec PC.** No stack's vocabulary is worth
   building until an edit to a `.dta` file shows in the running game in under a second
   (vision doc §5, phase 1), measured on the slowest machine the game supports, not the
   developer's workstation.
2. **An agent works where a gardener works: L3 and L4, in data.** It writes packs,
   instance files, scenarios, and metrics. It does not edit L0–L2 C++. When it needs a
   word only C++ can provide, that is a push-up, written as a stub (`(status stub)`) that
   the demand report lists for an engineer.
3. **Every claim an agent makes about the game is backed by a run.** "This level is
   solvable", "this economy has no dominant strategy", "this combo is not infinite": each
   is a scenario in `sim/`, run headless, deterministic, and re-runnable in CI.

---

## 1. What Windows PC alone implies

**One platform, a range of machines.** The constraint moves from a platform's floor to a
**minimum-spec PC** (§9): a 4-core, 8-thread x86-64 CPU from about 2019, 16 GB of RAM, a
D3D12-class GPU with 4 GB, an SSD, Windows 10 or 11. Every stack's budget in §4 is a
starting point to measure on that machine from the first week; a fast workstation hides
CPU problems until they are expensive.

**Input.** Mouse and keyboard, and a gamepad (XInput and others, through SDL3). Each stack
says which comes first: a tycoon game is played with a mouse, a racing or fighting game
with a pad.

**Toolchain.** MSVC 2022, which Moira's CI already builds and tests (C++20 and
C++latest); clang-cl as a second compiler if a stack wants one. No porting gate: Moira's
C++20 coroutines and designated initializers already pass on MSVC.

**The base engine.** The owner chose a thin C++ base of our own (§9) when PS4 was a
target and ruled out Unreal Engine 5. With PC alone, UE5 is possible again (it has
defaulted to C++20 since 5.3, so Moira could live in a plugin), as are Unity and Godot. The
thin base still fits these stacks best:

| | A. Thin C++ base (chosen) | B. Unreal Engine 5, Moira in a plugin | C. Unity or Godot, Moira for data |
|---|---|---|---|
| Moira's one-line `Tweak` | Everywhere | In C++ gameplay code; Blueprints need a binding | Only in native plugins |
| Determinism (fighting, racing, puzzle replays) | Ours to guarantee | Hard: Chaos physics and tick order are not ours | Hard, for the same reason |
| Headless runner for agents | By construction | Possible (`-nullrhi`, commandlets), heavy to start | Batch mode, heavy to start |
| Agent fit | Text all the way down | Assets are binary `.uasset`s | Scenes are YAML (Unity) or text (Godot) |
| Time to first playable | Slowest: weeks for the base, shared by six stacks | Fast | Fastest |
| Acton review (`.claude/skills/acton-review`) | Passes by construction | A large engine around a small library | A managed runtime or a variant type in the core loop |

So A stands. B or C is the hedge if the base slips: Moira then holds the data (shapes,
layers, provenance, write-back, `check`) and the stacks keep most of §3, but lose
determinism and the one-line promise in gameplay code.

---

## 2. The base: shared by all six stacks

*Built (D106): `stacks/base/`, with the cube game in `stacks/cube/`. Its README records
what it holds and what was measured; Jolt, Recast/Detour, miniaudio, and Tracy wait for the
first stack that needs them.*

### 2.1 Components

Everything is permissively licensed and recorded in each stack's `THIRD_PARTY.md`.

| Need | Choice | Licence | Notes |
|---|---|---|---|
| Data, tuning, script | Moira | MIT | L0. `moira::moira`; `MOIRA_SHIPPING` in release. |
| Containers | Moira's (`MOIRA_USE_EASTL` if the base adopts EASTL) | MIT / BSD-3 | One container family across base and Moira. |
| Platform layer (window, input, gamepads, files, threads) | SDL3 | zlib | Keeps Win32 types out of game code (§10). |
| Rendering | bgfx | BSD-2 | D3D11, D3D12, and Vulkan back ends. Prototypes need little: lit meshes, debug lines, text. |
| Physics | Jolt Physics | MIT | Build with `JPH_CROSS_PLATFORM_DETERMINISTIC` for the stacks that replay (§4). |
| Navigation | Recast/Detour | zlib | Behaviour and exploration stacks. |
| Audio | miniaudio | MIT-0 / public domain | Enough for prototypes; FMOD or Wwise when a stack needs authored audio. |
| Tools UI | Dear ImGui | MIT | Moira's `examples/harness-imgui` panels (Tweaks, Shapes, Console, Loop, Layers) are the starting point; it already builds with MSVC in CI. |
| Profiling | Tracy | BSD-3 | |
| Build | CMake presets: `dev`, `shipping` | | MSVC 2022. |

**No ECS framework in the base.** Each system owns plain arrays of its own data, laid out
for its own loop. An ECS can come later in a stack that shows it needs one; it is easy to
add and hard to remove (Acton review, "entanglement").

### 2.2 The loop

Moira owns no I/O; the base supplies it.

- **Change notification.** A directory watcher in the base (`ReadDirectoryChangesW`,
  behind the platform layer) notes each saved file with its save time. The game calls
  `cfg.reload({.trigger = "watcher", .origin_ns = t})` at the start of the next frame and
  `cfg.mark_visible()` when that frame presents, so `loop_stats()` reports edit-to-feel
  time per file.
- **Console.** `examples/remote-console`, on loopback as it is, in `dev` builds only. The
  agent and the ImGui console both use it.
- **Write-back.** A value tuned in the running game (panel or console) is written back to
  the file and line it came from, through the default local writer: a one-line diff.

**Acceptance for the base:** on the minimum-spec PC, a float tweak edited in a text editor
is visible in the running game in under one second, median, measured by `loop_stats()`;
and a value tuned in the game writes back to the right line.

### 2.3 Layers, levels, and defines

Every stack uses the same layer stack, bottom to top:

```
default        data/default.dta          the game
local          data/local.dta            per developer, ignored by version control, optional
agent          data/agent.dta            the agent's experiments (§3.3), optional
scratch        (Moira's)                 unsaved live edits
```

- `cfg.session().set_pack_levels({"engine", "game", "task"})`: L1, L2, L3. Instance data
  declares no pack.
- Graphics quality presets are game settings, not layers; a stack that needs a
  minimum-spec profile for its simulation (fewer characters, shorter view distance) adds
  an optional `spec/low.dta` layer and toggles it with `cfg.toggle_layer`, which also
  re-runs the validators.
- **Shipping.** The same stack, baked with `moira_bake` in the build and loaded through
  `ConfigOptions::cache_dir`; under `MOIRA_SHIPPING`, reload and live edits are inert.

### 2.4 Repository shape

Each stack will be one repository. Until the first stack is ready to move out, the base and
the stacks live in this repository's `stacks/` directory, one CMake project over Moira
(§9).

```
engine/          the base (L1), shared: a submodule or package, versioned
game/            L2 C++: native functions, objects, typed messages, the sim
packs/game/      L2 packs: shapes, macros, functions (level game)
packs/task/      L3 packs: workbenches (level task)       agent-writable, unreviewed
data/            L4 instance data and the layer files     agent.dta only; the rest by PR
sim/             headless runner; scenarios/*.dta          agent-writable, unreviewed
metrics/         objectives, as data                       agent proposes by PR
tools/           agent tool server
.claude/         CLAUDE.md and skills: the stack's vocabulary and loop, for the agent
```

---

## 3. The agent harness

The third meaning of "harness" (vision doc §7): a bounded, instrumented environment that
gives an agent the domain's tools and a feedback loop. Each stack ships one, built from four
parts.

### 3.1 The headless simulation runner

`sim` is the game's own simulation, linked without the renderer or audio, stepping on a
fixed timestep as fast as the CPU allows. It takes the same layer stack, a scenario, and a
seed:

```
sim --layers default,agent --scenario sim/scenarios/warden_p3.dta \
    --seeds 1-200 --out runs/2026-10-13T1405/
```

A **scenario** is a Moira repro script (cookbook, "Repro scripts"): the state to start
from, then commands, driven by a fixed-step scheduler. A recorded play session is a
scenario; so is a bot's run. Output is `.dta`, one file per run plus a summary, so the
agent reads results with the same tools it reads the game's data with.

The runner is what makes agent work trustworthy. It runs on developers' PCs and in CI on
Windows runners, built by the same compiler with the same floating-point flags as the
game (`/fp:precise`, no fast-math), so a scenario that passes in CI replays the same way in
the game. A Linux build of the runner, for cheaper CI, is a determinism claim across
compilers and has to be proven per stack before it is relied on.

### 3.2 Metrics as data

What "good" means for a stack is written in `metrics/`, as data the runner evaluates over
a run's output:

```dta
(objective puzzle.not_trivial
   (desc "No solution with fewer than half the intended pieces")
   (measure {puzzle.min_pieces_found})
   (pass {>= $value {* 0.5 {puzzle.intended_pieces}}}))
```

Objectives are written by people, or by the agent and reviewed by people. The agent may
not change an objective and the data it judges in the same change: a reviewer sees each
separately.

### 3.3 The tool surface

One tool server per stack (`tools/agent-server`), exposing Moira's existing machinery.
None of this needs a change to Moira:

| Tool | Built on |
|---|---|
| `check` | `moira::check` with `{.packs = &cfg.session()}`, and `to_json` (`docs/check-json.md`): errors, one-way warnings, demand, repetition, single-owner findings |
| `reference` | `moira::reference`: every function, shape, pack, and generated file; the vocabulary the agent may use |
| `eval` | The remote console, on loopback |
| `source` | `cfg.source(path)`: where a value came from, origin included |
| `run` | The headless runner (§3.1) |
| `loop`, `journal` | `cfg.loop_stats()`, `cfg.journal()`, `cfg.unsaved()` |
| `capture` | A screenshot or short clip from the running game, for a human or a vision model |
| `propose` | Writes a change to `data/agent.dta` (or a new pack), reloads, and runs `check` |
| `diff` | `cfg.layer_effect("agent")`, `cfg.diff(a, b)`: exactly what the agent layer changes |
| `promote` | `cfg.promote("agent", "default")`: writes the agent layer's effect into the target layer's file, all or nothing. **Human approval required.** |

The agent works in the `agent` layer. Its experiments are visible (`source()` reports the
layer), switchable (`toggle_layer("agent")` gives an A/B in the running game), and
discardable. Promotion writes the layer's effect into a real file as a small diff, which
goes through review like any other change.

### 3.4 The loop the agent runs

1. Read the stack's `.claude/` skill, the `reference`, and the relevant `metrics/`.
2. Propose a change in the `agent` layer, or a new L3 pack.
3. `check`: no errors, no new one-way warnings, no new demand it did not intend.
4. `run`: the stack's scenarios over a seed range, before and after; compare objectives.
5. Report to the human: what changed, which objectives moved, what to look at, and how to
   see it in the running game (`toggle_layer("agent")`).
6. The human plays it and tunes it live. Their edits land in `scratch` and the journal;
   the agent reads the journal to learn what the human changed after it.
7. `promote` on approval.

**What the agent may write without review** (§9): the `agent` layer, `sim/` scenarios,
and L3 task packs. Metrics, L2 packs, and promotion go by pull request.

**Packs the agent writes** declare it as one owner and a person as the other:
`(owners "agent" "td-encounter")`. A pack with only `"agent"` is a single-owner finding in
`check`, which is the right alarm: nobody human can extend it.

---

## 4. The six stacks

Each section gives the creative question the stack exists to answer, what it adds to the
base, its L2 and L3 vocabulary, an instance sample, the agent's loop, and what bites first
on PC.

### 4.1 Contraption: physics-based puzzle levels

**The question.** Is this level solvable, solvable only the intended way, and does
solving it feel clever rather than fiddly?

**Adds to the base.** Jolt in deterministic mode, rigid bodies, hinges, springs, sensors.
A piece catalog. A replay format: the player's placements and inputs, timestamped in sim
ticks.

**L2: game harness.** Native: piece kinds (`ramp`, `plank`, `ball`, `fan`, `spring`,
`bucket`), each with physical parameters; goal predicates (`in_zone`, `touching`,
`resting_for`). Shapes:

```dta
(pack contraption_core (level game) (owners "gameplay-physics" "td-puzzle") (stage custom))
(shapes
   (piece (at levels * pieces)
      (kind     symbol)
      (pos      (list float) (desc "x y z, meters"))
      (angle    float (range -180 180) (kind degrees) (optional))
      (mass     float (range 0.01 50) (optional))
      (fixed    bool (optional))))
```

**L3: the level workbench.** A pack of macros for common rigs (`SEESAW`, `DOMINO_RUN`), a
`{puzzle.restart}` / `{puzzle.step $ticks}` console pair, a ghost overlay that draws the
intended solution, and a sensitivity panel: nudge any placed piece by ±n mm and see
whether the solution still works.

**L4: a level.**

```dta
(levels
   (ramp_and_ball
      (pieces
         (ramp   (kind ramp)   (pos 0 2 0)  (angle 30) (fixed 1))
         (ball   (kind ball)   (pos -1 4 0) (mass 0.5))
         (bucket (kind bucket) (pos 6 0 0)  (fixed 1)))
      (tray (plank 2) (spring 1))
      (goal {in_zone ball bucket})
      (intended (place plank 1.2 1.0 0 15) (place plank 2.0 0.4 0 -10))))
```

**Agent loop.**
- *Solvability:* run the `intended` solution as a scenario; it must reach the goal in the
  runner and in the game, bit for bit.
- *Cheese search:* a search over the tray (random restarts, then local search over
  placements) looking for solutions with fewer pieces than intended, or none at all. Each
  find is a scenario file a designer can watch.
- *Robustness:* perturb the intended placements by the precision the input allows (a
  mouse with grid snapping, and a gamepad cursor); report the fraction that still solve.
  Fiddly levels show up as a low number.
- *Generation:* propose variants of a level by mutating its pieces, keeping the ones
  that stay solvable, uncheesed, and robust, for a designer to play.

**Metrics.** Solved by intended (pass/fail); minimum pieces found; robustness at ±5 mm
and ±2°; ticks to settle.

**On PC.** Mouse first. Replays must match between the runner and the game: one compiler,
one set of floating-point flags, and a CI job that replays every level's intended
solution through both. Start with a budget of 500 active bodies at 60 Hz on the
minimum-spec CPU.

**Push-up signals.** Pieces with behaviour (magnets, conveyors, portals) are native
functions; a designer asking for them in data appears in the demand report.

### 4.2 Ledger: a tycoon game economy

**The question.** Does the economy produce interesting decisions from the first minute to
the fortieth hour, with no dominant strategy, no dead ends, and a pace the designer
intends?

**Adds to the base.** No physics. A tick-based economy simulator in C++ (goods, recipes,
buildings, demand, prices, upkeep), and bot players with parameterized strategies.

**L2: game harness.** Shapes over goods, buildings, and recipes; natives for price and
demand curves. Keyed children are not reached by shapes (D85), so data a shape should
check is written `(good (name flour) ...)`:

```dta
(pack ledger_core (level game) (owners "gameplay-econ" "systems-design") (stage custom))
(shapes
   (good
      (name       symbol)
      (base_price float (range 0.01 100000))
      (spoil_days int   (range 0 3650) (optional)))
   (building
      (name    symbol)
      (cost    int   (range 0 100000000))
      (upkeep  float (range 0 100000) (desc "Per in-game day"))
      (recipe  (list symbol) (desc "inputs -> output, by good name"))
      (unlock  any (optional) (desc "A command; true when the building unlocks"))))
```

**L3: the balance workbench.** The spreadsheet stays, as a first-class harness, not a
fled one: an export from `economy.xlsx` writes `data/economy.dta` with an `origin` form, so
`source()` names the sheet behind every number, and the reference lists the tool. Beside
it: a panel graphing money, stock, and price over a simulated run; console commands to
jump the clock (`{econ.advance_days 30}`) and to grant or drain money.

**L4: the economy.**

```dta
(origin "economy.xlsx" (tool sheets-export))
(economy
   (good (name wheat) (base_price 2))
   (good (name flour) (base_price 5) (spoil_days 60))
   (building (name mill) (cost 1200) (upkeep 8) (recipe wheat wheat -> flour)
             (unlock {>= {econ.total_earned} 5000})))
```

Moira's integers are 64-bit, so late-game balances fit in data and in script without
scaling.

**Agent loop.**
- *Monte Carlo:* run each bot strategy (greedy, hoarder, specialist, random) over 200
  seeds and 40 in-game hours.
- *Dominance:* report any strategy that beats all others on every seed, and the buildings
  it never uses (dead content).
- *Pacing:* compare time-to-milestone curves against the designer's target curve in
  `metrics/`; propose changes to costs and upkeep in the `agent` layer, never to the
  spreadsheet export directly (the export would overwrite them; the agent reports which
  sheet cells to change, and the human changes them).
- *Sinks and faucets:* money created versus destroyed per hour; flag inflation.

**Metrics.** Time to each milestone (median and 90th percentile against target); dominant
strategy (none); unused buildings at hour 10 (none); money supply growth per hour (within
a band).

**On PC.** Mouse and keyboard first: dense UI, tooltips, hotkeys. Light on CPU, so the
minimum spec is rarely the constraint; save size and save time on a long game are.

**Push-up signals.** Market behaviours that need simulation (competitors, events) start
as script functions; the repetition report shows when designers are hand-building the
same event three times.

### 4.3 Hearth: character behaviour in a simulation

**The question.** Do the characters read as people, with routines, preferences, and
reactions, without getting stuck, oscillating, or starving?

**Adds to the base.** Recast/Detour navigation. A utility-AI core in C++: needs that
decay, smart objects that advertise how much they satisfy each need, and actions as
sequences. A behaviour log per character.

**L2: game harness.** Characters and smart objects are Moira objects (`Object<T>`), so
`{class ...}` lets a designer change one type's reaction in script without touching C++.
Actions are script tasks (`thread_task`), so "walk to the fridge, open it, eat, close it"
is data. Scoring is native; script runs at decision time only.

```dta
(pack hearth_core (level game) (owners "gameplay-ai" "td-behaviour") (stage custom))
(shapes
   (need (at needs *)
      (decay_per_hour float (range 0 100))
      (critical       float (range 0 100) (desc "Below this, the need overrides routine"))
      (curve          symbol (desc "linear, quadratic, or logistic"))))
```

**L3: the behaviour workbench.** A timeline panel per character (need values, chosen
action, runner-up and score margin); a "why did you do that?" console command that
prints the last decision's scores; time controls (`{hearth.speed 20}`); a spawner for
test households.

**L4: a household.**

```dta
(needs
   (hunger (decay_per_hour 6) (critical 20) (curve quadratic))
   (social (decay_per_hour 3) (critical 15) (curve logistic)))
(objects
   (fridge (advertise (hunger 40)) (action eat_snack))
   (sofa   (advertise (fun 10) (comfort 30)) (action sit)))
{class villager
   (on_need_critical ($need) {hearth.interrupt $this $need})}
```

**Agent loop.**
- *Soak:* run 50 households for 30 in-game days per seed. Detect stuck characters (no
  progress for an hour), oscillation (alternating between two actions more than n times),
  starvation (a need at zero for longer than a threshold), and unused objects.
- *Readability:* per character, the entropy of daily routines: too low is robotic, too
  high is random. The designer sets the band.
- *Authoring:* write new actions and smart objects as data in an L3 pack, from a
  designer's description ("they should gossip at the well in the morning"), then soak.

**Metrics.** Stuck, oscillating, and starving events per character-day (zero);
routine entropy (in band); share of objects used (above a floor).

**On PC.** CPU is the constraint, on the minimum-spec machine. Score at a staggered
decision rate (each character re-decides a few times a second, not every frame), keep
scoring native, and spread households across worker threads, each with its own Moira
`Context` (a Config is changed from one thread; others read its `root()`). Start with a
budget of 150 characters at full simulation on the minimum-spec CPU.

**Push-up signals.** A script function called every frame, or a designer writing scoring
logic in script, appears in a profile; move it native.

### 4.4 Wayfarer: exploration missions in a free-roaming world

**The question.** Does exploring lead somewhere? Are missions discoverable, reachable,
paced, and impossible to break?

**Adds to the base.** World streaming by grid cell; Recast/Detour per cell; a spatial
index of points of interest (POIs); a mission director in script. Traversal (walk, climb,
glide, mount) as native movement with tunable limits.

**L2: game harness.** Declarative objectives in the manner of Halo 3's objectives system
(vision doc §1): a mission states what must become true, not the steps. Natives: `at_poi`,
`flag`, `has_item`, `time_of_day`; the director evaluates objective commands and advances.

```dta
(pack wayfarer_core (level game) (owners "gameplay-world" "td-mission") (stage custom))
(shapes
   (objective (at missions * objectives)
      (goal      any (desc "A command; true when done"))
      (requires  (list symbol) (optional))
      (hint_after float (range 0 3600) (optional) (kind seconds))))
```

**L3: the mission workbench.** A map panel with POIs and mission graphs drawn over the
world; `{mission.start lighthouse_signal}`, `{mission.skip_to light}`, `{world.teleport
lighthouse}`; a gating view that colours every mission by whether it is reachable from the
current save.

**L4: a mission.**

```dta
(missions
   (lighthouse_signal
      (region north_coast)
      (unlock {mission.done harbor_intro})
      (objectives
         (reach (goal {at_poi lighthouse}) (hint_after 120))
         (light (goal {flag lamp_lit}) (requires reach)))
      (reward (xp 300) (item spyglass))))
```

**Agent loop.**
- *Softlock check:* build the graph of missions, items, flags, and traversal abilities;
  prove every mission completable from every save point. This is a graph search, not a
  simulation, and it runs in seconds.
- *Reachability:* for each objective, a navmesh path from where the player can be when
  it unlocks, with the traversal abilities they can have by then.
- *Pacing:* bot explorers (curious, direct, completionist) roam headless; report the time
  between discoveries along each route as a pacing curve, and dead zones where nothing is
  found for minutes.
- *Drafting:* propose missions from the POI database and the designer's themes, as data
  in the `agent` layer, already softlock-checked.

**Metrics.** Softlocks (zero); unreachable objectives (zero); longest stretch without a
discovery, per bot style (under a target); mission completion time against the design.

**On PC.** With an SSD in the minimum spec, streaming is a memory budget more than a
bandwidth one: stream by cell anyway, so memory stays bounded as the world grows and
iteration loads stay short. Load mission data with the world cell (each region's
missions a file the cell loads through the same session), and keep the full mission
graph in memory for the director: it is small.

**Push-up signals.** Objectives that need spatial queries the natives lack (line of
sight, "within the storm") appear as demand.

### 4.5 Stuntline: stunt race levels from track pieces

**The question.** Is the track fun to drive: fast, readable, with jumps that land and
moments of airtime, built quickly from a kit of pieces?

**Adds to the base.** Jolt vehicles (`WheeledVehicleController`) in deterministic mode; a
track-piece kit with typed connectors; a layout solver that places pieces end to end; AI
drivers that follow the racing line; ghost replays.

**L2: game harness.** Pieces are data with connectors; a track is a sequence (or a graph,
for branches) of pieces with parameters.

```dta
(pack stuntline_core (level game) (owners "gameplay-vehicle" "td-track") (stage custom))
(shapes
   (piece (at pieces *)
      (length float (range 1 200) (kind meters))
      (rise   float (range -50 50) (kind meters) (optional))
      (bank   float (range -90 90) (kind degrees) (optional))
      (in     symbol (desc "Connector type at the start"))
      (out    symbol (desc "Connector type at the end"))))
```

**L3: the track workbench.** A builder panel that snaps pieces by connector; jump
preview (the ballistic arc at the speed the AI arrives with, drawn over the gap); `{track.drive
canyon_jump}` to spawn an AI driver; a ghost of the best lap.

**L4: a track.**

```dta
(tracks
   (canyon_jump
      (lay straight_40 ramp_up_15 gap_30 landing_15 loop_r12 straight_40 banked_turn_90)
      (checkpoints 2 5)
      (par 42.0)))
```

**Agent loop.**
- *Validity:* connectors match; circuits close (position and heading within tolerance);
  every gap is cleared at the speed the AI arrives with, and not cleared at 70% of it (so
  a jump needs commitment).
- *Drive:* AI drivers at three skill levels, 50 seeds each, with small steering noise.
  Report lap time, crash points (as track positions), airtime, top speed, and the speed
  profile.
- *Generation:* lay new tracks from the kit with a grammar (no two loops in a row; a
  breather after every jump), keep the valid ones, rank by the designer's thrill metric.

**Metrics.** Valid (pass/fail); crash rate per piece for the medium AI (under a ceiling);
airtime share (in band); lap-time spread between AI levels (in band).

**On PC.** Gamepad first (analog steering and throttle), keyboard supported. Run physics
at a fixed rate and interpolate rendering, so high-refresh displays look smooth without
changing the simulation; hold 60 frames per second on the minimum spec. Ghosts must match
between the runner and the game, as in 4.1.

**Push-up signals.** Pieces with behaviour (boost pads, moving platforms) are natives.

### 4.6 Duel: player-versus-computer fighting

**The question.** Does the fight feel good: readable, responsive, with risk and reward on
every move, and a computer opponent that is beatable, fair, and teaches the player?

**Adds to the base.** A deterministic fixed 60 Hz simulation independent of rendering;
frame data; hitbox and hurtbox volumes per animation frame; input buffering and
cancels; a CPU opponent. No physics middleware: collision is boxes against boxes.

**L2: game harness.** Frame data is the vocabulary:

```dta
(pack duel_core (level game) (owners "gameplay-fight" "td-combat") (stage custom))
(shapes
   (move (at fighters * moves)
      (startup   int (range 1 60) (kind frames) (desc "Frames before the first active frame"))
      (active    int (range 1 30) (kind frames))
      (recovery  int (range 1 90) (kind frames))
      (damage    int (range 0 400))
      (hitstun   int (range 0 90) (kind frames))
      (blockstun int (range 0 60) (kind frames))
      (cancels   (list symbol) (optional))
      (hitbox    (list float) (desc "x y w h, meters, relative to the fighter"))))
```

**L3: the frame-data workbench.** A frame stepper with boxes drawn over the character;
a training-mode panel (record and play back the dummy, show frame advantage on every hit
and block); `{duel.hit_pause}` as a tweak, since hit pause is what makes a hit feel like
one; the CPU's difficulty as layers (`cpu/easy.dta`, `cpu/hard.dta`) toggled at run time,
so a designer compares difficulties in one session.

**L4: a fighter.**

```dta
(fighters
   (kestrel
      (moves
         (jab      (startup 4) (active 2) (recovery 7) (damage 30) (hitstun 12) (blockstun 9)
                   (cancels jab low_kick) (hitbox 0.4 1.4 0.5 0.2))
         (low_kick (startup 6) (active 3) (recovery 12) (damage 45) (hitstun 16) (blockstun 10)
                   (cancels special) (hitbox 0.5 0.2 0.6 0.2)))))
```

**Agent loop.**
- *Frame tables:* compute advantage on hit and on block for every move and every cancel;
  flag moves that are safe and plus on block with no counterplay.
- *Infinites:* search the cancel graph with hitstun and pushback for loops that combo
  forever, and for combos over a damage ceiling. Each find is a scenario.
- *CPU fairness:* the CPU's reaction time has a floor per difficulty (no frame-1
  reactions to a move the player could not react to). Run CPU against scripted player
  bots (a masher, a turtle, a combo player) and report win rate per difficulty against
  the designer's curve.
- *Proposing:* small changes to frame data in the `agent` layer, with the frame tables
  before and after.

**Metrics.** Infinites (zero); maximum combo damage (under a ceiling); CPU win rate per
difficulty against each bot (in band); reaction floor (respected).

**On PC.** Input latency is the feel, and PC adds variables a console does not have:
present modes, display refresh rates, and many controllers (pads and arcade sticks through
SDL3). Poll input immediately before each simulation step, keep the simulation at exactly
60 Hz whatever the display does, and measure input-to-photon with a high-speed camera on
the minimum spec and on a high-refresh display. The simulation itself is light.

**Push-up signals.** Move properties the simulation does not yet have (armour,
invincibility frames, projectiles) appear as undeclared fields in `check` (warnings) and as
demand.

---

## 5. The six at a glance

| Stack | Game | Deterministic replay | Physics | Navigation | Headless metric that matters most | What bites first on PC |
|---|---|---|---|---|---|---|
| Contraption | Physics puzzle | Required: runner and game agree | Jolt rigid bodies | — | Cheese search: fewest pieces that solve | Replays matching across builds |
| Ledger | Tycoon economy | Seeded | — | — | Dominant strategy across bot styles | Save size and time on long games |
| Hearth | Behaviour simulation | Seeded | — | Recast/Detour | Stuck, oscillating, starving events | Minimum-spec CPU |
| Wayfarer | Free-roaming exploration | Seeded | Jolt (character) | Recast/Detour | Softlocks; time between discoveries | Memory as the world grows |
| Stuntline | Stunt racing | Required: runner and game agree | Jolt vehicles | — | Crash rate per piece; airtime | 60 Hz on the minimum spec |
| Duel | Fighting vs CPU | Required: runner and game agree | Boxes only | — | Infinites; CPU win rate per difficulty | Input latency across displays |

Shared modules worth building once, in the base or a shared L1 library: the deterministic
fixed-step core (Contraption, Stuntline, Duel); Jolt integration (Contraption, Wayfarer,
Stuntline); navigation (Hearth, Wayfarer); bot drivers and the runner's statistics (all).

---

## 6. Order of work

1. **The base and its loop** (§2), with one trivial game (a cube and a tweak), meeting the
   one-second acceptance on the minimum-spec PC. Then the agent harness (§3) on that
   trivial game. *Done (D106): on the test machine, edit to see measured 21–41 ms, median
   about 25 ms. The agent harness is next.*
2. **Duel and Ledger first.** Both are small, need no streaming, and have the clearest
   headless metrics; Duel proves the deterministic core and Ledger proves the spreadsheet
   path and bot Monte Carlo. They exercise the two ends of the agent loop: tight numeric
   search and long statistical soak.
3. **Contraption and Stuntline** next: both reuse Duel's deterministic core and add Jolt.
4. **Hearth and Wayfarer** last: the largest worlds, the most native code, the most
   content.

At each step, run the audit from `reading-a-studio.md` §7 on the stacks in use: the
register comes from `reference`, loop times from `loop_stats()`, demand and repetition
from `check`. Record results the way Moira's own steps are recorded: with numbers.

---

## 7. Risks

- **Budgeting on a fast workstation.** Every stack measured only on developers' machines
  will miss the minimum spec. Keep one minimum-spec PC in the loop from week one.
- **Determinism between the runner and the game.** Proven per stack in CI, not assumed:
  one compiler, one set of floating-point flags. If Jolt's deterministic mode does not
  hold for a stack's use, Duel's model (our own fixed-point or integer simulation) is the
  fallback for Contraption; Stuntline would need the same.
- **Agents overfit the metrics.** A level that passes every objective can still be dull.
  Step 6 of §3.4 (a person plays it) is not optional, and objectives change apart from
  the data they judge.
- **The fled harness, again.** If designers export from their own spreadsheets around
  Ledger's workbench, `origin` makes it visible; treat it as a signal about the
  workbench, as the vision doc says.
- **Single gardener.** A pack owned only by `"agent"` is reported by `check`; keep a
  person on every pack.
- **PC-only habits that make a later port expensive.** §10 lists the few rules that keep
  consoles cheap to add back.

---

## 8. What Moira needs

Nothing in the core: every stack is built from what Moira already has (layers and toggles,
shapes, packs and levels, `origin`, `check` and its JSON, the reference, the journal,
`loop_stats`, `promote`, the remote console, the binary cache), and Moira already builds
and tests with MSVC. One candidate, to be pushed up only when a stack proves the need:

- **A headless-runner example**: a fixed-step scheduler driven from a scenario file, with
  results written as `.dta`. Outside the core, like `remote-console`.

---

## 9. Decisions

Decided by the project owner, 2026-10-13:

1. **Platform:** Windows PC only, for now. PS4 and Switch 2 are deferred (§10).
2. **The base engine:** A, the thin C++ base (§1, §2). Chosen while consoles were targets;
   §1 rechecks it for PC alone, and it stands.
3. **First stacks:** Duel and Ledger (§6, step 2).
4. **Agent write scope:** without review, the `agent` layer, `sim/` scenarios, and L3 task
   packs (`packs/task/`). Metrics, L2 packs, and promotion into the real layers go by
   pull request. Every task pack the agent writes still names a person among its owners
   (§3.4).
5. **The minimum-spec PC** (§1): a 2019-era 4-core, 8-thread CPU, 16 GB of RAM, a 4 GB
   D3D12-class GPU, an SSD, and Windows 10 or 11.
6. **Rendering:** bgfx (§2.1).
7. **Where the code lives:** `stacks/` in this repository for now (§2.4).

---

## 10. If consoles return

Notes from the first version of this document, which targeted PS4 and Switch 2, kept so
a port starts informed:

- **PS4 is the floor** for CPU (eight 1.6 GHz Jaguar cores) and storage (a 5400 rpm hard
  drive on the base model); Switch 2 adds Arm64 and a handheld mode, which fits a Moira
  layer toggled on undock.
- **Gate 0:** build and run Moira's tests with each console SDK's compiler first. PS4's
  support for C++20 coroutines (`moira::script` tasks) is unconfirmed; the fallbacks are a
  newer SDK, `MOIRA_BUILD_SCRIPT=OFF` there, or a coroutine-free task implementation (a
  spec deviation).
- **Unreal Engine 5 does not target PS4**, which is why the base is our own; with PS4
  out, that reason no longer applies (§1), but the others do.
- **Devkits need a host link:** `LoadSession::set_reader` and `Config::set_writer` over
  the devkit's network connection, with change notification from a watcher on the PC.
- **Binary caches:** all three platforms are 64-bit and little-endian, so one bake per
  define set serves each (D105).
- **Cross-platform determinism** (x86-64 against Arm64) becomes a CI requirement for
  Contraption, Stuntline, and Duel.

What to do now so that a port stays cheap:

- Keep Win32 and D3D types behind SDL3 and the renderer; game code never sees them.
- Keep platform differences in data: a `platform` layer slots in below `local` later.
- Build with `/fp:precise` and no fast-math, as determinism already requires.
- Use Moira's containers (or EASTL) in engine code, not the STL directly.
- Budget on the minimum spec, and keep the budgets in data, so a console's numbers are a
  layer, not a rewrite.
