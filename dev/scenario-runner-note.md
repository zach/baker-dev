# Scenarios a tool can find and run: a design note

*Requirement note 0016, from the editor integration. Decided and built: D208.*

## The need

A tool (an editor's test view, CI, an agent) should find a game's scenarios and objectives,
run one by name with a seed, and read a located, versioned result, without knowing the game.
Today only the stacks can, through their own `sim` command, scenario forms and
`stacks.sim/1.1` JSON, which a tool would have to learn as one game's convention.

## What exists

The stacks' runner (`stacks/base/src/sim.cpp`, about 630 lines) already has the model:

- `(scenario name (seconds s) (start {...}) (at t {...}) (objectives a b))`;
- `(objective name (measure {...}) (pass {>= $value 2}) [(over median)])`;
- a run that is a function of the data, the scenario and the seed: reset with the seed,
  the start commands, fixed steps ticking the scheduler, `at` commands on time, then each
  objective's measure and pass;
- objectives judged by their median over seeds;
- results as `.dta` files and as JSON, and `compare` of two result sets.

It is generic but for one thing: it drives the stacks' `base::Game` (reset, step).

## The proposal

- **The project file** gains three optional entries, read by `read_project` and ignored by
  a game: `(tool "build/bin/stacks_duel")`, the binary that answers the tool commands, and
  `(scenarios "sim")` and `(objectives "metrics")`, files or directories, each relative to
  the project file. An absent path is not an error.
- **The host kit** gains the runner, in Baker's terms:
  - `<baker/host/scenarios.hpp>`: the two forms above, read with their file and line;
    `Scenario`, `Objective`, a run, the median verdicts;
  - **hooks:** `CommandHooks` gains `step(Context&, double dt)`, the game's fixed step,
    beside the `reset` it already has; the game owns stepping and measuring (its measure
    commands are its own script functions);
  - **commands:** `sim [--scenario NAME]... [--seeds A-B] [--step-hz N] [--output FILE]`,
    and `compare BEFORE AFTER`;
  - **JSON:** `baker.sim/1` and `baker.compare/1`. Every scenario and objective is located
    by file and line, and a failed command by where it failed, as check's problems are.
    The exit code is 0, 1 (an objective failed), or 2 (it could not run).
- **The stacks** move onto it, as they moved onto the kit's other commands (D192): `sim`
  and `compare` become the kit's, `stacks.sim/1.1` and `stacks.compare/1.1` are retired,
  and `base::Game` gives the kit its `step`.

## Cost

- The kit grows by about 450 lines (the runner, its JSON and `compare`, without the stacks'
  `.dta` result files), past its 1,500-line budget (D186; 1,315 used).
- The core grows by the three project entries, about 30 lines.
- Nothing for a game without scenarios: the entries, the hook and the files are optional.

## Questions for the owner

1. Where the runner lives: the host kit with a larger budget, a library of its own, or
   nowhere (the stacks keep theirs, and a tool learns their format).
2. Whether the stacks move onto it, retiring their own `sim` and `compare` formats.
3. Whether `compare` comes with it, or only `sim` for now.

The owner's answers: the host kit, with its budget raised to 2,000 (then 2,200, D208); the
stacks move onto it; `compare` comes with it.
