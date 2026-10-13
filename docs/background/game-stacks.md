# Game Stacks on Moira

Six prototype development environments ("stacks") built around Moira, one per kind of
creative work, for focused agent-assisted development. Every game targets PS4, Windows PC,
and Switch 2.

Oct 13, 2026. A design proposal; §9 records what the owner has decided and what is open.

Companion to `harness-tree-vision.md` (layers L0–L4, the four properties of a harness),
`reading-a-studio.md` (loop telemetry, the harness register), and `pushing-up.md`. Code in
`.dta` blocks follows `docs/language.md`; the vocabulary in it (`fight.`, `econ.`, and
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

1. **The loop comes first, on target hardware.** No stack's vocabulary is worth building
   until an edit to a `.dta` file shows on a PS4 and a Switch 2 devkit in seconds (vision
   doc §5, phase 1). §1 explains why both consoles, not just the PC.
2. **An agent works where a gardener works: L3 and L4, in data.** It writes packs,
   instance files, scenarios, and metrics. It does not edit L0–L2 C++. When it needs a
   word only C++ can provide, that is a push-up, written as a stub (`(status stub)`) that
   the demand report lists for an engineer.
3. **Every claim an agent makes about the game is backed by a run.** "This level is
   solvable", "this economy has no dominant strategy", "this combo is not infinite": each
   is a scenario in `sim/`, run headless, deterministic, and re-runnable in CI.

---

## 1. What the three platforms imply

| | PS4 | Switch 2 | Windows PC |
|---|---|---|---|
| CPU | 8 × AMD Jaguar, 1.6 GHz, two 4-core clusters, x86-64 (Pro: 2.1 GHz) | 8 × Arm Cortex-A78C, clocked low, Arm64 | x86-64, wide range |
| Memory | 8 GB GDDR5 shared; roughly 5 GB for the game | 12 GB LPDDR5X shared | Varies; set a floor spec |
| Storage | 5400 rpm HDD on the base model | Game card or internal flash | Assume a SATA SSD |
| Modes | One | Docked and handheld, switched at run time | Many resolutions |
| Native graphics API | Sony's, under NDA | Nintendo's, under NDA | D3D12 or Vulkan |

Platform specifics beyond the public figures above are under NDA; every "verify" below
means verify with the platform holder's current SDK documentation.

**PS4 is the floor.** Its CPU is the weakest of the three per core, and every stack here is
CPU-bound before it is GPU-bound: physics, AI, simulation ticks, and script. Set each
stack's simulation budget on the base PS4 and measure it there from the first week. The
budgets in §4 are starting points to validate, not measurements.

**Switch 2 is the variant.** Arm64 and a second performance mode. Handheld mode becomes a
Moira layer the game toggles when the console undocks (§2.3), so one data set serves both
modes.

**What this rules out.** Unreal Engine 5 does not target PS4; UE 4.27 does, but it is a
frozen engine built around C++17, and Moira needs C++20. That leaves three paths:

| | A. Thin C++ base (recommended) | B. Unity host, Moira for data | C. Godot host via a porting partner |
|---|---|---|---|
| Moira's one-line `Tweak` | Yes, everywhere | Only in native plugins; C# code gets a binding layer | Only in GDExtension code |
| Console support | Middleware below, each console-proven; we own the integration | Unity's; confirm the version's PS4 and Switch 2 support | A third-party port; PS4 coverage varies by partner |
| Determinism (fighting, racing, puzzle replays) | Ours to guarantee; Jolt has a cross-platform deterministic mode | Hard: engine physics and update order are not ours | Hard, for the same reason |
| Time to first playable | Slowest: weeks for the base, shared by six stacks | Fastest | Fast |
| Acton review (`.claude/skills/acton-review`) | Passes by construction | A managed runtime in the core loop | A scene tree and a variant type in the core loop |
| Agent fit | Text all the way down; headless by construction | Scenes and prefabs are binary or YAML; headless needs batch mode | Text scenes; headless works |

The owner chose **A** (§9). B is the hedge if the base slips: Moira then holds the data
(shapes, layers, provenance, write-back, `check`) and the stacks keep most of §3, but lose
determinism and the one-line promise in gameplay code.

### Gate 0: the PS4 toolchain and C++20

Moira requires C++20. `moira::script` uses coroutines (`include/moira/task.hpp`), and the
API uses designated initializers. Before anything else, build Moira's test suite with the
PS4 SDK's compiler and library and run it on a devkit. If coroutines are not available
there, the fallbacks, in order of preference:

1. A newer PS4 SDK whose compiler supports them.
2. `MOIRA_BUILD_SCRIPT=OFF` on PS4 (data, config, tweaks, and the cache still work; the
   script layer, and with it script tasks, does not), which costs four of the six stacks
   most of their L3 harnesses on PS4. Bad, but it shows early.
3. A Moira change: a task implementation that does not need coroutines. That is a spec
   deviation and goes to `docs/decisions.md`.

The same gate covers the Switch 2 toolchain, where the risk is lower. MSVC, GCC 13, and
Clang 18 already pass CI.

---

## 2. The base: shared by all six stacks

### 2.1 Components

Everything is permissively licensed or a console-standard commercial product, and has
shipped on consoles. Recorded in each stack's `THIRD_PARTY.md`.

| Need | Choice | Licence | Notes |
|---|---|---|---|
| Data, tuning, script | Moira | MIT | L0. `moira::moira`; `MOIRA_SHIPPING` in release. |
| Containers | Moira's (`MOIRA_USE_EASTL` if the base adopts EASTL) | MIT / BSD-3 | One container family across base and Moira. |
| Platform layer (window, input, files, threads) | SDL3 on PC; a thin layer of our own over each console SDK | zlib | SDL's console ports go to registered developers; verify before relying on them. |
| Rendering | The Forge or bgfx | Apache-2.0 / BSD-2 | Both supply console back ends to platform licensees; confirm Switch 2 coverage. Prototypes need little: lit meshes, debug lines, text. |
| Physics | Jolt Physics | MIT | Shipped on PS4 and PS5 in *Horizon Forbidden West*. Build with `JPH_CROSS_PLATFORM_DETERMINISTIC` for the stacks that replay (§4). |
| Navigation | Recast/Detour | zlib | Behaviour and exploration stacks. |
| Audio | FMOD or Wwise | Commercial | Both certified on all three platforms; choose by studio licence. |
| Tools UI | Dear ImGui | MIT | Moira's `examples/harness-imgui` panels (Tweaks, Shapes, Console, Loop, Layers) are the starting point. |
| Profiling | Tracy on PC; the platform profilers on devkits | BSD-3 | |
| Build | CMake presets: `pc`, `ps4`, `switch2`, each × `dev`, `shipping` | | Console toolchain files come from the SDKs. |

**No ECS framework in the base.** Each system owns plain arrays of its own data, laid out
for its own loop. An ECS can come later in a stack that shows it needs one; it is easy to
add and hard to remove (Acton review, "entanglement").

### 2.2 The loop on a devkit

Moira owns no I/O; the base supplies it. One mechanism serves all three platforms:

- **Files.** The base sets the session's reader (`LoadSession::set_reader`) and the
  Config's writer (`Config::set_writer`) to a **host link**: a small server on the
  developer's PC that serves the project's `data/` over the devkit's network connection.
  On PC the default local filesystem reader is used.
- **Change notification.** The host link watches `data/` and sends "changed" with the
  save time. The game calls `cfg.reload({.trigger = "watcher", .origin_ns = t})` at the
  start of the next frame and `cfg.mark_visible()` when that frame presents, so
  `loop_stats()` reports edit-to-feel time per file on each console.
- **Console.** `examples/remote-console`, bound to the devkit's development interface
  instead of loopback, in `dev` builds only. The agent and the ImGui console both use it.
- **Write-back.** A value tuned on the devkit (panel or console) is written back through
  the host link to the file and line it came from: a one-line diff on the PC.

**Acceptance for the base:** a float tweak edited on the PC is visible on the PS4 and the
Switch 2 in under two seconds, median, measured by `loop_stats()`, and a value tuned on
each devkit writes back to the right line.

### 2.3 Layers, levels, and defines

Every stack uses the same layer stack, bottom to top:

```
default        data/default.dta          the game, every platform
platform       data/platform/ps4.dta     one of ps4, switch2, pc; picked at start
handheld       data/platform/handheld.dta   Switch 2 only; toggled on undock, optional
local          data/local.dta            per developer, ignored by version control, optional
agent          data/agent.dta            the agent's experiments (§3.3), optional
scratch        (Moira's)                 unsaved live edits
```

- `cfg.session().define("PS4")` (or `SWITCH2`, `PC`) before the layers, for the rare
  `#ifdef`; most platform differences belong in the `platform` layer, where `source()`
  can show them.
- `cfg.toggle_layer("handheld")` when the console docks or undocks. A Config runs its
  validators after a toggle, so a handheld value out of a shape's range is reported.
- `cfg.session().set_pack_levels({"engine", "game", "task"})`: L1, L2, L3. Instance data
  declares no pack.
- **Shipping.** The same stack, baked with `moira_bake` in the build, once per platform's
  define set. All three targets are 64-bit and little-endian, so one bake per define set
  serves its console (D105). `MOIRA_SHIPPING` makes reload, toggles, and live edits
  inert, so the handheld toggle in a shipped Switch 2 build switches between two baked
  Configs instead (or `live_layers`, if the game freezes its Config); decide per stack
  before the first submission build.

### 2.4 Repository shape

Each stack is one repository:

```
engine/          the base (L1), shared: a submodule or package, versioned
game/            L2 C++: native functions, objects, typed messages, the sim
packs/game/      L2 packs: shapes, macros, functions (level game)
packs/task/      L3 packs: workbenches (level task)       agent-writable, unreviewed
data/            L4 instance data and the layer files     agent.dta only; the rest by PR
sim/             headless runner; scenarios/*.dta          agent-writable, unreviewed
metrics/         objectives, as data                       agent proposes by PR
tools/           host link, agent tool server
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
sim --layers default,platform/ps4,agent --scenario sim/scenarios/warden_p3.dta \
    --seeds 1-200 --out runs/2026-10-13T1405/
```

A **scenario** is a Moira repro script (cookbook, "Repro scripts"): the state to start
from, then commands, driven by a fixed-step scheduler. A recorded play session is a
scenario; so is a bot's run. Output is `.dta`, one file per run plus a summary, so the
agent reads results with the same tools it reads the game's data with.

The runner is what makes agent work trustworthy. It runs on PC and in CI, and the same
binary, built for each devkit, runs the same scenario there to catch platform divergence.

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
| `eval` | The remote console, on PC or a devkit |
| `source` | `cfg.source(path)`: where a value came from, origin included |
| `run` | The headless runner (§3.1) |
| `loop`, `journal` | `cfg.loop_stats()`, `cfg.journal()`, `cfg.unsaved()` |
| `capture` | A screenshot or short clip from a running devkit, for a human or a vision model |
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
   see it on a devkit (`toggle_layer("agent")`).
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
base, its L2 and L3 vocabulary, an instance sample, the agent's loop, and the platform
concerns that bite first.

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
- *Solvability:* run the `intended` solution as a scenario; it must reach the goal on every
  platform build, bit for bit.
- *Cheese search:* a search over the tray (random restarts, then local search over
  placements) looking for solutions with fewer pieces than intended, or none at all. Each
  find is a scenario file a designer can watch.
- *Robustness:* perturb the intended placements by the precision a controller allows;
  report the fraction that still solve. Fiddly levels show up as a low number.
- *Generation:* propose variants of a level by mutating its pieces, keeping the ones
  that stay solvable, uncheesed, and robust, for a designer to play.

**Metrics.** Solved by intended (pass/fail); minimum pieces found; robustness at ±5 mm
and ±2°; ticks to settle.

**Platforms.** Determinism across x86-64 and Arm64 is the hard requirement. Jolt's
cross-platform deterministic build exists for this; prove it with the same scenario run
on all three platforms in CI on day one, compiled with the base's floating-point flags
(no fast-math, no FMA contraction unless every platform does the same). Budget a start
of 300 active bodies at 60 Hz on base PS4.

**Push-up signals.** Pieces with behaviour (magnets, conveyors, portals) are native
functions; a designer asking for them in data appears in the demand report.

### 4.2 Ledger: a tycoon game economy

**The question.** Does the economy produce interesting decisions from the first minute to
the fortieth hour, with no dominant strategy, no dead ends, and a pace the designer
intends?

**Adds to the base.** No physics. A tick-based economy simulator in C++ (goods, recipes,
buildings, demand, prices, upkeep), and bot players with parameterized strategies. UI
built for controllers first, since two of three targets have no mouse.

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

**Platforms.** Light on CPU, heavy on UI and on save data: budget save size and save
time per platform early, and test text and UI scale in handheld mode, where the screen
is small.

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

**Platforms.** The PS4 CPU is the constraint. Score at a staggered decision rate (each
character re-decides a few times a second, not every frame), keep scoring native, and
measure a Moira native call on the Jaguar against the 28–31 ns measured on the
development Xeon. Start with a budget of 60 characters on screen at 30 Hz on base PS4.

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

**Platforms.** Streaming on the base PS4's hard drive is the risk: set cell size and
streaming radius by its seek time and bandwidth, and test traversal at the fastest
movement speed. Load mission data with the world cell (each region's missions a file the
cell loads through the same session), and keep the full mission graph in memory for the
director: it is small.

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

**Platforms.** Racing wants 60 frames per second on every target, including base PS4
and Switch 2 handheld: budget vehicle physics with substeps, and few AI cars on screen.
Ghosts and replays must match across platforms, so the same determinism proof as 4.1.

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

**Platforms.** Input latency is the feel: lock 60 Hz on every target, with no frame
between input poll and simulation; measure input-to-photon on each console and in each
Switch 2 mode. The simulation is light, so PS4 is not the constraint here; display and
controller latency are.

**Push-up signals.** Move properties the simulation does not yet have (armour,
invincibility frames, projectiles) appear as undeclared fields in `check` (warnings) and as
demand.

---

## 5. The six at a glance

| Stack | Game | Deterministic replay | Physics | Navigation | Headless metric that matters most | Platform that bites first |
|---|---|---|---|---|---|---|
| Contraption | Physics puzzle | Required, cross-platform | Jolt rigid bodies | — | Cheese search: fewest pieces that solve | Determinism, x86-64 against Arm64 |
| Ledger | Tycoon economy | Seeded, per platform | — | — | Dominant strategy across bot styles | Controller UI, handheld text size |
| Hearth | Behaviour simulation | Seeded, per platform | — | Recast/Detour | Stuck, oscillating, starving events | PS4 CPU |
| Wayfarer | Free-roaming exploration | Seeded, per platform | Jolt (character) | Recast/Detour | Softlocks; time between discoveries | PS4 HDD streaming |
| Stuntline | Stunt racing | Required, cross-platform | Jolt vehicles | — | Crash rate per piece; airtime | 60 Hz on PS4 and handheld |
| Duel | Fighting vs CPU | Required, cross-platform | Boxes only | — | Infinites; CPU win rate per difficulty | Input latency |

Shared modules worth building once, in the base or a shared L1 library: the deterministic
fixed-step core (Contraption, Stuntline, Duel); Jolt integration (Contraption, Wayfarer,
Stuntline); navigation (Hearth, Wayfarer); bot drivers and the runner's statistics (all).

---

## 6. Order of work

1. **Gate 0** (§1): Moira's tests on PS4 and Switch 2 devkits. Days.
2. **The base and its loop** (§2), with one trivial game (a cube and a tweak), on all three
   platforms, meeting the two-second acceptance. Then the agent harness (§3) on that
   trivial game.
3. **Duel and Ledger first.** Both are small, need no streaming, and have the clearest
   headless metrics; Duel proves the deterministic core and Ledger proves the spreadsheet
   path and bot Monte Carlo. They exercise the two ends of the agent loop: tight numeric
   search and long statistical soak.
4. **Contraption and Stuntline** next: both reuse Duel's deterministic core and add Jolt.
5. **Hearth and Wayfarer** last: the largest worlds, the most native code, the most
   content.

At each step, run the audit from `reading-a-studio.md` §7 on the stacks in use: the
register comes from `reference`, loop times from `loop_stats()`, demand and repetition
from `check`. Record results the way Moira's own steps are recorded: with numbers.

---

## 7. Risks

- **Gate 0 fails on PS4.** §1 lists the fallbacks; the worst costs PS4 its script layer.
- **Determinism across Arm64 and x86-64.** Proven per stack in CI, not assumed. If Jolt's
  deterministic mode does not hold for a stack's use, Duel's model (our own fixed-point
  or integer simulation) is the fallback for Contraption; Stuntline would need the same.
- **Console middleware access.** Renderer and platform-layer console back ends depend on
  licensee programmes; confirm before committing to The Forge or bgfx, and to SDL3.
- **Agents overfit the metrics.** A level that passes every objective can still be dull.
  Step 6 of §3.4 (a person plays it on a devkit) is not optional, and objectives change
  apart from the data they judge.
- **The fled harness, again.** If designers export from their own spreadsheets around
  Ledger's workbench, `origin` makes it visible; treat it as a signal about the
  workbench, as the vision doc says.
- **Single gardener.** A pack owned only by `"agent"` is reported by `check`; keep a
  person on every pack.

---

## 8. What Moira needs

Nothing in the core, as designed: every stack is built from what Moira already has
(layers and toggles, shapes, packs and levels, `origin`, `check` and its JSON, the
reference, the journal, `loop_stats`, `promote`, the reader and writer hooks, the remote
console, the binary cache). Candidates that the stacks may surface, each to be pushed up
only when a stack proves the need:

- **A host-link example** (`examples/host-link`): the reader and writer over a socket,
  with change notification, which is §2.2. Outside the core, like `remote-console`.
- **A headless-runner example**: a fixed-step scheduler driven from a scenario file, with
  results written as `.dta`. Outside the core.
- **Gate 0's outcome**, if coroutines are unavailable on PS4: a spec deviation, recorded in
  `docs/decisions.md`.

---

## 9. Decisions

Decided by the project owner, 2026-10-13:

1. **The base engine:** A, the thin C++ base (§1, §2).
2. **First stacks:** Duel and Ledger (§6, step 3).
3. **Agent write scope:** without review, the `agent` layer, `sim/` scenarios, and L3 task
   packs (`packs/task/`). Metrics, L2 packs, and promotion into the real layers go by
   pull request. Every task pack the agent writes still names a person among its owners
   (§3.4).

Still open:

4. **Rendering middleware:** The Forge or bgfx, once their Switch 2 back ends are
   confirmed.
5. **Handheld mode in shipping builds:** two baked Configs, or a frozen Config with
   `live_layers` for the `handheld` layer.
