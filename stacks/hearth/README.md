# Hearth

Characters with routines, in a household (`dev/background/game-stacks.md` §4.3). The
question it exists to answer: do the characters read as people, with routines,
preferences, and reactions, without getting stuck, oscillating, or starving?

```sh
build-stacks/bin/stacks_hearth                                   # play
build-stacks/bin/stacks_hearth sim --seeds 1-5                   # soak 50 households for 30 days, per seed
build-stacks/bin/stacks_hearth eval "{hearth.soak 50 30}" "{hearth.soak_report}"
build-stacks/bin/stacks_hearth eval "{hearth.bench 150}"         # microseconds per step, 150 villagers
```

**Playing.** The Hearth window is the workbench: pick a household (the spawner), set the
speed, and pick a villager to see its needs, a day's timeline of each, its last decision
("why": every option's score, the pick, and the margin over the runner-up), and its
behaviour log. Space pauses; 1 to 4 set the speed (1x, 60x, 600x, 3600x); tab picks the
next villager; N spawns the next household. In the console: `{hearth.why ada}`,
`{hearth.speed 600}`, `{hearth.spawn farmhouse}`, `{ada need hunger}`.

## Layout

| Path | What | Who writes it (§9) |
|---|---|---|
| `household.hpp`, `household.cpp` | L2: needs, things, villagers as Baker objects, scoring, the soak on worker threads, its measures | Engineers |
| `main.cpp` | L2: the workbench window, script words (`hearth.`), drawing | Engineers |
| `packs/game/hearth_core.dta` | L2: the shapes of a need, an object, a household (all `(keyed)`) | By pull request |
| `data/default.dta` | L4: needs, objects, households, and the actions (`{class villager ...}`) | By pull request |
| `data/agent.dta` | The agent's layer | The agent |
| `sim/households.dta` | Scenarios: the soak; a day of the cottage on the game's own steps | The agent |
| `metrics/hearth.dta` | Objectives: not stuck, not oscillating, not starving, scripts run, things used, routines readable | By pull request |

## Design

- **Villagers and things are Baker objects** (`Villager`, `Thing`), so `{class villager
  ...}` changes a type's reaction in script. A need falling below critical sends the typed
  message `NeedCritical`: a script handler `(on_need_critical ($need) ...)` first, then
  the native default (interrupt unless the current action helps that need).
- **Actions are script tasks** on `clock`, a Scheduler in game minutes: a thing's kind
  names the handler a use starts (`use_object` by default), which walks, waits, uses, and
  says `{$this done}`. "Walk to the fridge, open it, eat, close it" is data.
- **Scoring is native** and runs only when a villager decides: each thing's gain on the
  villager's need curves (linear, quadratic, logistic), weighted per resident, scaled down
  by distance and outside the thing's hours; a weighted pick among the best few. A need
  below critical overrides the routine: only things that help a critical need are options.
- **Failures are measured, per character-day**: stuck (no progress for
  `hearth.stuck_minutes`), oscillating (interrupted back and forth between two kinds of thing
  `hearth.oscillation_switches` times without finishing either), starving (a need at zero for
  `hearth.starve_minutes`), and script errors. Routine entropy is, per villager, the mean
  over the hours of the day of the entropy (bits) of what it was about at that hour across
  days; the band is the designer's (0.6 to 2.2 in `metrics/hearth.dta`).
- **The soak runs on worker threads, each with its own Context** (§4.3, "On PC"): the
  villager and thing types, a clock, and the data's `#autorun` commands, over the one
  Config's tree. A household's run depends only on its seed, so the results are the same on
  any number of threads (a test checks it).
- **Navigation is Recast/Detour** through the base (`base::NavMesh`): a house is one tile
  built from its floor, walls, and things; a thing whose use spot is not on the mesh is
  never chosen, and shows as unused.

## Not yet

Villagers talking to each other (social comes from things); several floors; an example L3
task pack in the repository (one adds an action with `{extend villager ...}`, D113, never a
second `{class}`); animation beyond standing and lying down.

## Measured

See `dev/decisions.md` D112.
