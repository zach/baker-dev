# A scenario run the host advances: a design note

*Requirement note 0011, from the Godot integration. Built as D209.*

## The need

`sim` steps a game through `CommandHooks::step`, in a loop inside one call. An engine that
owns its main loop advances its physics only inside that loop, a frame at a time, and has
no call that runs one step and comes back. Stepping its systems from inside one call skips
the engine's own physics, and the runs are wrong and not repeatable. Such a game needs to
advance each run itself, one fixed step per frame, while the kit does the rest of `sim`.

## The shape

`baker::host::SimRun`, in `<baker/host/commands.hpp>` beside `run_command`:

```cpp
baker::host::SimRun run(line, hooks);       // a `sim` command line
// once a frame:
switch (run.wants()) {
case SimRun::Wants::Reset:                  // reset the world with run.seed(), over as
    if (world_ready) run.ready();           // many frames as it takes, then say so
    break;
case SimRun::Wants::Steps:                  // one fixed step of run.step_seconds() passed
    run.step();
    break;
case SimRun::Wants::Done:                   // run.status(): run_command's exit status;
    break;                                  // the output went where run_command sends it
}
```

- **One runner, two drivers.** The kit's runner becomes a stepper: `begin` (the start
  commands, then what is due at 0), `advance` (tick the scheduler, count the step, run what
  is now due), `judge` (the objectives). `sim` through `run_command` is a `SimRun` driven in
  a loop with `reset` and `step`; a host drives the same `SimRun` from its frames. Both give
  the same runs, bytes and exit status.
- **Reset.** The kit sets `$seed`, then waits in `Wants::Reset`. It does not call
  `CommandHooks::reset`; the host resets in its own way and calls `ready()`. Waiting counts
  no steps.
- **The rate.** `step_seconds()` is the step the run counts, `--step-hz` rounded to whole
  nanoseconds, so the host can set its own fixed rate to match.
- **Stopping.** `stop()` ends the run now (a window closed, a timeout). The run in progress
  is kept with its steps and marked not finished; its objectives are not judged; runs not
  started are left out. The output is written as at the end, and the status is 2 (could not
  run everything).
- **The schema.** `baker.sim/1.1` adds `"finished"` to each run, and `"stopped"` as a status.
  `results.dta` keeps `(finished 0)` for a run that did not finish, and `compare` reads it.
- **Threads.** Every call runs on the thread that owns the run's Context, between frames.
- **A quiet step** (nothing due) is a scheduler tick, a count and a compare: no allocation.

## Not changed

`CommandHooks::step` and `sim` through `run_command` stay as they are, for hosts that can
step on demand.
