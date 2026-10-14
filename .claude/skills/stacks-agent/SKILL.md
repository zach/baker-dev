---
name: stacks-agent
description: Work on a game stack's data as its agent (stacks/, docs/background/game-stacks.md §3) - tune values, write scenarios and task packs, judge them with the headless simulation, and hand the result to a person. Use when asked to tune, balance, test, or extend a stack game's data (the cube, Duel, Ledger, ...), to write or run a scenario, or to explain why an objective fails.
---

# Working on a stack's data

A stack's game (`stacks/<game>/`) is also its own tool. Its data is Moira (`docs/language.md`).
You work where a technical designer works: in data, never in the game's C++.

```
stacks/<game>/data/default.dta    the game's data. Read it; never edit it.
stacks/<game>/data/agent.dta      YOUR layer, over default. Every experiment goes here.
stacks/<game>/data/local.dta      one person's tuning. Never read, write, or rely on it.
stacks/<game>/sim/*.dta           scenarios. You may add and edit them.
stacks/<game>/metrics/*.dta       objectives: what "good" means. Propose changes by pull
                                  request only, never in the same change as the data they judge.
packs/task/ (when a stack has one) task packs. You may write them; name a person as an owner.
```

## The commands

Build once: `cmake -S stacks -B build-stacks -G Ninja && cmake --build build-stacks`.
Then, with `G=build-stacks/bin/stacks_<game>`:

| Command | What it tells you |
|---|---|
| `$G reference` | Every script function, global, shape, and pack: the words you may use. Read it first. |
| `$G check` | Errors and calls to undefined functions in data, scenarios, and metrics, as JSON. Exit 1 means fix before anything else. |
| `$G sim [--scenario NAME] [--seeds 1-20] [--out DIR]` | Runs scenarios headless and judges them by the objectives. Exit 0: all passed; 1: an objective failed; 2: something could not run. |
| `$G eval "{cmd}"...` | Commands on the data and the reset game, headless: a frame table, a search, a report. |
| `$G diff` | Exactly what `agent.dta` changes: path, old value, new value. |
| `$G promote` | What promotion would write into `default.dta`. **Never pass `--write`**: promotion is a person's decision. |
| `python3 -I stacks/tools/console.py "{cmd}"` | A command in a game a person is running (127.0.0.1:4555). Use it only when asked to. |

`sim` and `check` see `default.dta` and `agent.dta`, never `local.dta`, so results do not
depend on whose machine runs them. Nothing in a run reads the clock: the same data, scenario,
and seed give the same result, so a failing seed is a reproduction.

## The loop

1. **Read** `$G reference`, the objectives in `metrics/`, and the scenarios in `sim/`.
2. **Baseline:** `$G sim --seeds 1-10` before you change anything. Note what passes.
3. **Change** `data/agent.dta` (or add a scenario or task pack). Keep each change small and
   say in a `;` comment why.
4. **`$G check`**: exit 0, or fix it.
5. **`$G sim`** over the same seeds; compare objective by objective with the baseline.
6. **Report** to the person: what you changed and why (`$G diff`), which objectives moved
   (before and after), what you could not judge by objectives (how it looks or feels), and
   how to see it: start the game and toggle the `agent` layer in the Layers panel (F1).
7. **Stop.** The person plays it, tunes it live, and decides whether to promote. Their own
   edits show in the game's Journal panel; read what they changed before your next round.

## The stacks

- **Duel** (`stacks/duel/`): frame data under `(fighters (NAME (moves (MOVE ...))))`, the
  CPU's difficulties under `(cpu ...)`, the bots under `(bots ...)`. Read
  `$G eval "{duel.frame_table}" "{duel.combo_route}"` before and after a frame-data change,
  and quote both in your report. Win rates are per run over an hour of rounds; one seed
  outside a band is noise, a shifted median is a finding.
- **Ledger** (`stacks/ledger/`): goods and buildings come from `economy.xlsx` through
  `data/economy.dta`, which is generated: never edit it. Tune in `agent.dta`, and report each
  value as the sheet cell to change (each line of `economy.dta` ends with its sheet and
  row). `$G eval "{econ.compete}" "{econ.advance_days 720}" "{econ.report}"` shows every
  strategy's company after an hour.

- **Contraption** (`stacks/contraption/`): kinds and levels in `data/default.dta`. A level's
  intended solution, robustness, and a cheese search are objectives; after a change, quote
  `$G eval "{puzzle.load LEVEL}" "{puzzle.fewest_pieces 300}" "{puzzle.cheese_route}"`. A
  found cheese is a placement you can paste into a scenario for a person to watch.
- **Stuntline** (`stacks/stuntline/`): the car, the kit, tracks, drivers. Read
  `$G eval "{track.load TRACK}" "{track.report}"` before and after: times, crashes by piece
  (1-based), airtime, jump speeds. A track change re-lays the whole track.
- **Hearth** (`stacks/hearth/`): needs, objects, households, and the actions (the
  `{class villager ...}` in `data/default.dta`). A new action in a task pack is
  `{extend villager (NAME ($obj) ...)}`, never a second `{class}`, which replaces them all. After a change, quote `$G eval "{hearth.soak 50 30}" "{hearth.soak_report}"`:
  failures per kind, things used, and the first few failures with the household, villager,
  and game time. `{hearth.why NAME}` after `{hearth.spawn HOUSE}` explains one decision.
- **Wayfarer** (`stacks/wayfarer/`): cells, points of interest, and the traversal limits in
  `data/default.dta`; missions in `data/regions/`. Quote `$G eval "{world.prove}"
  "{world.proof_report}"` before and after any change: a softlock names the save point and
  the missions it strands. Pacing is per bot style: `{world.pacing STYLE 1800}` then
  `{world.pacing_report}`. A changed traversal limit changes what can be reached: prove
  again.

## Rules

- An objective is a claim a person signed off on. If one seems wrong, say so in your report
  and propose the change by pull request; never change an objective to make data pass.
- Every claim you make about the game is backed by a run: quote the `sim` output.
- When you need a word the game does not have (a function, an object), do not fake it in
  script. Write it as a stub in `agent.dta` (or a task pack), so the game runs and `check`
  lists it as demand for an engineer:
  `#autorun {func duel.armor_frames ($move) (status stub) (doc "What it should do") 0}`.
  A data file defines script functions only through `#autorun`; the game runs those commands
  after every load and reload.
- Passing objectives is not the goal; a game that feels right is. Say what the objectives
  cannot see.
