# The agent harness: design note

`dev/background/game-stacks.md` §3, built on the base (`base/README.md`). What an agent
needs to work on a stack's data the way a technical designer would: run the game without a
window, judge a run by written objectives, check its own edits, and hand them to a person
to keep or drop.

## One binary per game, with commands

Every stack's game is also its own tool. `base::main` reads a command first; with none, the
game plays as before:

```
stacks_cube                                   play (the base's app)
stacks_cube sim [--scenario NAME]... [--seeds A-B] [--out DIR]
stacks_cube check                             baker::check over data, its #autorun script,
                                              scenarios, metrics; baker.check/1.1 JSON with
                                              passed, load and shape problems; exit 1 on any,
                                              or an undefined, unstubbed call; 2 if no load
stacks_cube reference [--json]                the vocabulary: functions, shapes, packs (Markdown);
                                              --json: Baker's reference, tweaks and shapes JSON
stacks_cube diff                              what agent.dta changes, value by value
stacks_cube promote --to default [--write]    agent.dta into default.dta; --write is a person's
stacks_cube eval [--seed N] "{command}"...     commands on the data and the reset game: a frame
                                              table, a search, a report
```

The design called this a tool server. A command-line tool is the same surface with less
code: an agent with a shell calls it, CI calls it, a person calls it. The console of a
running game is the existing remote console; `tools/console.py` sends it a line, and reads
the JSON reply. The game binds Baker's Config tools there (`{config.explain p}`,
`{config.set p v}`, `{config.journal}`, ...), without the ones that write files.

Commands need the game's own script functions (check reports a call to one that does not
exist; the reference lists them), so they live in the game's binary, not a separate tool.

## Simulation

- **`Game` moves to `base/game.hpp`**, apart from SDL and bgfx, so the simulation, its
  tests, and the commands link without a window. It gains `reset(seed)`, called before
  each run, so runs do not leak state into each other.
- **The simulation is the play loop's own steps**: `Game::step` at the fixed rate, then
  the Context's scheduler ticked by the same time (the play loop gains that too: script
  tasks did not run before). Nothing reads the wall clock, so a run is the same on any
  machine that computes the same floats.
- **Layers:** `default` and `agent`, not `local`: a developer's own tuning must not change
  an agent's results.

## Data

A stack keeps three directories side by side (design §2.4): `data/` (the layers), `sim/`
(scenarios), `metrics/` (objectives).

```dta
; sim/turn.dta
(scenario turn
   (desc "Twenty seconds of spinning from rest")
   (seconds 20)
   (start {cube.reset})              ; before the first step
   ; (at T {...})                   ; commands at a time, in seconds; any number of these
   (objectives cube.turns))          ; which objectives judge it; all of them if absent

; metrics/cube.dta
(objective cube.turns
   (desc "Twenty seconds make 2 to 10 full turns")
   (measure {cube.turns})            ; evaluated after the run; the result is $value
   (pass {and {>= $value 2} {<= $value 10}}))
```

A run's result is `.dta`, so an agent reads results with the tools it reads data with:

```dta
(run (scenario turn) (seed 1) (steps 1200)
   (objective cube.turns (value 2.5) (pass 1)))
(summary (runs 5) (passed 5) (failed 0)
   (objective cube.turns (passed 5) (failed 0) (min 2.5) (median 2.5) (max 2.5)))
```

`sim` exits 0 when every objective passed, 1 when one failed, and 2 when something could
not run (a missing scenario, a broken file): CI can gate on it.

## Script functions from data

A data file defines script functions with `#autorun {func ...}`: task packs, stubs for a
push-up, helpers for scenarios. Baker's `Config` reads each layer with a session of its own,
so the commands a layer's `#autorun` collected were out of reach; `Config::autorun()` (D107)
returns them, enabled layers bottom to top. `Live` runs them after the load and at the frame
after any change to the stack (a reload, a toggle from a panel), so a redefinition in
`agent.dta` replaces the default's at once.

## Check and shapes

`check` also runs the shapes (`baker::shape`) over the merged data: a value out of range or
of the wrong type fails it, and a field no shape declares is printed as a warning, which
is how a designer's request for a word the game lacks shows up. Stacks keep data keyed, as
`(moves (jab ...))`, so a layer can retune one entry; their shapes are `(keyed)` (D108).

## Who may write what (design §9)

The agent writes `data/agent.dta`, `sim/`, and task packs without review; `metrics/`,
game packs, and promotion go by pull request. `promote` without `--write` only shows what
it would write; the agent's instructions (`.claude/skills/stacks-agent/`) say never to
pass `--write`.
