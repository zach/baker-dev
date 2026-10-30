# Scenarios: Named Endings and Repeated Commands

A design note, agreed with the owner on Oct 22, 2026. It follows D223, which gave
scenarios a game's own steps and an `(until ...)`. Results and deviations go into
`dev/decisions.md` when it is built.

## Named endings

- **The need.** A game ends in one of several ways: victory, defeat, a draw, a soft-lock.
  A run records only that `(until ...)` ended it. An objective that wants to tell a win
  from a loss has to measure the game again, after the fact.
- **The design.** `(until {command})` may return the ending's name:
  - `0` (or false) goes on;
  - a symbol ends the run with that name;
  - any other nonzero value ends it as today, named `until`.

  ```dta
  (scenario siege (steps 40) (until {board.outcome}))   ; 0, or victory, defeat, draw
  (objective siege.won (measure {== $ended victory}) (pass {!= $value 0}))
  ```

- **`$ended`** is set before the objectives are measured: the ending's name, `until`, or
  `length`.
- **The results.** `baker.sim/1.3` writes `"ended": "victory"`, a value beyond the
  `"until"` and `"length"` that 1.2 gave. A reader of 1.2 that knew only those two must
  allow others, so this is a new minor version. `summary` tallies endings per scenario:
  `"endings": {"victory": 7, "defeat": 3}`.
- **Cost.** None beyond today's one command a step.

## Repeated commands

- **The need.** Waves every 5 turns, an income tick every second, a probe every 10 steps.
  Today a scenario lists an `(at ...)` for each one, or the game does it.
- **The design.** `(every SECONDS {...})` and `(every step N {...})`, beside `(at ...)`,
  run at each multiple of their period, from the first:

  ```dta
  (scenario waves (steps 100)
     (every step 5 {spawn.wave})
     (every 1 {economy.tick}))
  ```

  - **Order.** At one step, the `(at ...)`s due run first, then the `(every ...)`s, each
    in the order written.
  - A period of zero, or a negative one, is a check error located at the form.
- **Cost.**
  - A run works out each `(every ...)`'s next step once, as `(at ...)` does, so a step
    with nothing due still allocates nothing (the quiet-step test).
  - A step that is due runs its commands.

## Tests to write

- An `(until ...)` that returns `victory`: the run ends named so, and `$ended` and the
  tally show it.
- A nonzero number still ends a run as `until`.
- An `(every step 5 ...)` over 20 steps runs four times, after any `(at ...)` due at the
  same step.
- An `(every ...)` in seconds at a step rate that does not divide it runs on the half-step
  rule, as `(at ...)` does.
- A zero period is a check error.
- `SimRun` gives the same results as `run_command`.
