# M7 plan: tasks

Draft for review. Spec §5 ("Tasks — use coroutines") and §10 (M7 row) are the
requirements. D58 (frames from a pool, a frame budget, batch steps with a sync point) is
already decided. The talk digest (Appendix A, "Tasks") gives the original's surface and its
open problem. The open questions at the end change the design. Once they are answered,
the decisions go into `docs/decisions.md` and this file is deleted when M7 is done.

## Scope

From the M7 row:

- `Task`, `Scheduler`, coroutine awaitables;
- script-side scheduler commands: `delay_task`, `interp_task`, `thread_task`.

Done when:

1. `delay`, `until`, and `interp` all tick correctly (tests with a scripted sequence of
   `tick(dt)` calls, checking exactly which steps ran at which time).
2. A task holding a local across a suspend point works: a C++ local, and a script
   variable captured when a script task is created (the problem the original had open).

Carried from earlier milestones:

- Spawning and stepping allocate nothing once the pool is warm, and nothing ever comes
  from `operator new` (D58, D61). `moira_alloc_tests` grows a task scenario.
- `{add 1 2}` stays at its current cost: nothing in the call path changes.
- Call statistics and the trace hook (D66) see task steps like any other evaluation.

Not in M7:

- awaiting another task's result (`co_await child`), and task groups;
- threads: a Scheduler is stepped on one thread, as a Context is used on one;
- the binary cache's handling of task state (nothing is serialized).

## Design

### 1. The C++ side

```cpp
moira::Task blink(moira::Scheduler& s, Light& light) {  // the Scheduler parameter picks the pool
    for (int i = 0; i < 3; ++i) {
        light.on();
        co_await moira::delay(100);
        light.off();
        co_await moira::until([] { return player_ready(); });
    }
}

moira::Scheduler& s = ctx.scheduler();
auto h = s.spawn(blink(s, light), "blink");  // Expected<TaskHandle>
s.tick(dt);                                  // the host's frame, in the host's units
```

- **Frames come from the Scheduler's pool (D58).** A coroutine must take a `Scheduler&` or a
  `Context&` (its default Scheduler) among its parameters. For a member function, the
  object comes first, so "first parameter" would not do. Two guards make that a compile
  error, never a heap frame. Both were checked on GCC 13 and Clang 18 before this plan.
  - **The promise's `operator new`** takes the coroutine's arguments and requires a
    Scheduler among them. GCC rejects a coroutine without one. Clang 18 instead falls back
    to the global `operator new`, even when a size-only overload is declared deleted.
  - **The promise's only constructor** also takes the arguments and requires a Scheduler,
    so Clang rejects the same coroutine ("no matching constructor"). The constructor is
    also how the promise learns its Scheduler, which the awaitables use.

  With a Scheduler, neither compiler calls the global `operator new`. MSVC is checked
  in CI at the first step.
  - The pool hands out fixed-size blocks, the frame budget, in chunks taken from the
    Context's allocator. Freed blocks go on a free list; nothing is freed until the
    Scheduler is.
  - A frame larger than the budget fails at spawn: `operator new` returns null, and
    `get_return_object_on_allocation_failure` makes a `Task` that `spawn` turns into a
    `Limit` error naming the task and both sizes. It never falls back to the heap.
- **Awaitables** are free functions, as in the spec: `delay(t)`, `until(predicate)`, and
  `next_tick()`. Each lives in the suspended frame, so waiting allocates nothing. They
  find their Scheduler through the promise.
- **A task starts at the next `tick`,** not inside `spawn`: spawning during a tick is a
  structural change (D58), and the order tasks run in stays the order they were spawned.
- **`TaskHandle`** is a salted index, like `ObjectHandle` (D19): `cancel(h)` and
  `running(h)` are safe on a finished task. Cancelling destroys the frame, running the
  locals' destructors, and returns the block to the pool.
- **Errors.** A C++ task body is `void`; it handles the `Expected` results it gets. A
  script step that fails ends its task and goes to the Context's notice sink, with the
  task's name pushed on the error's stack (`{task blink}`).
- **The call-frame list never spans a suspend.** The evaluator is not a coroutine, so a
  task suspends only between evaluations. M6's note about saving the frame list across a
  suspend does not apply.

### 2. The Scheduler

- **One timeline per Scheduler.** `tick(dt)` advances the Scheduler's time by `dt` in units
  the host chooses (seconds, frames, beats), as a `double` (Q1). A game with game time and
  UI time, or a beat clock, makes one Scheduler for each. `ctx.scheduler()` is a default
  one, made on first use; more are made with `Scheduler(ctx, options)`.
- **Waiting costs nothing for a task that is not due.**
  - `delay` waiters sit in a binary heap keyed by wake time, so a tick costs
    O(due · log n), not O(n). That is the spec's "energy proportional to surprise".
  - `until` waiters are polled each tick, which is what the predicate asks for. The plan
    records this as the costly awaitable.
  - `next_tick` waiters sit in a list.
- **A tick is a batch.**
  1. Move started-this-tick tasks and due waiters into the ready list, ordered by
     (wake time, spawn order) so a run is deterministic.
  2. Resume each.
  3. At the sync point, the end of the tick, apply what the step queued (Q3).

  The ready list and heap are `Vec`s reserved once and reused: no allocation after
  warm-up.
- **Live tasks per frame (gap 7's question).** `SchedulerOptions` has `frame_bytes`
  (default 1,024), `chunk` (blocks per pool chunk, default 64), and `max_tasks` (default
  0, unlimited; with a cap, spawning past it is a `Limit` error). `stats()` reports live,
  peak, and per-tick resumed counts, and the pool's high water, so a TD sees the real
  numbers.

### 3. The script side

`{scheduler ...}` is a method call: the default Scheduler is a bound object named
`scheduler` (`Object<Scheduler>`), and a host can name others (`ctx.add(beats, "beats")`
makes `{beats delay_task ...}`). Each script task is a C++ coroutine from the same pool,
so both sides share one loop.

- **`{scheduler delay_task t body...}`** runs `body` once, `t` units from now.
- **`{scheduler interp_task $var from to t body...}`** sets `$var` from `from` to `to`
  linearly over `t`, and runs `body` after each step, including a last step exactly at
  `to` (Q6 settles the signature).
- **`{scheduler thread_task cmd... }`** runs its commands in order across ticks.
  - `{wait cond}` suspends until `cond` is true;
  - `{wait_for t}` suspends for `t` units;
  - `{wait_tick}` suspends until the next tick.

  The waits work only as the thread's own top-level commands (Q5).
- **Each returns a task handle** (an Int, the salted bits) for `{scheduler cancel $h}`.
- **Variables are captured by value at creation (Q2).** This is the original's open
  problem, solved without closures in the evaluator.
  - When a script task is made, its body is scanned once for `$variables`. The current
    value of each one that is bound is stored in the task, and globals are left out.
  - Each step re-binds them (`Rebind`, as a function call does), so the body sees what it
    saw when the task was made.
  - A `{set $x ...}` inside the task changes the task's copy, which later steps see.
  - The scan result is per body array, so a task spawned every frame from the same text
    does not re-scan.
- **`$this`.** A task made in a handler captures `$this`, and is cancelled when that
  object is removed (Q4): a salted compare before each step, no callback from `remove`.

### 4. Structural changes during a tick (D58)

A tick resumes many tasks, and each should see the world as the tick found it, whatever
order they run in (DOTS's command buffers). Recommended (Q3):

- **Deferred:**
  - spawning, from C++ or script, to the next tick;
  - `{delete}` and `ctx.remove(obj)` of an object, to the end of the tick;
  - `cancel`, to the end of the tick, except that a task cancelling itself stops at once.

  A deferred delete leaves the object's handle valid until the sync point, so later tasks
  in the batch still reach it.
- **Not deferred:** `{new}` (a script uses the new object at once), and `func`/`{class}`
  registration, which tasks rarely do and which changes no data a task holds.

The sync point is named: `tick` applies the queue at its end, and a host that runs more
than one Scheduler can call `ctx.sync()` itself.

### 5. Tooling

- The reference lists the scheduler's methods like any object's, with docs.
- `check` already sees `{scheduler delay_task ...}` as a method call. The task body is
  checked as code, because the parameter is `Raw<Node>`.

## Review (acton-review)

| Piece | Path | Cost and evidence |
|---|---|---|
| `tick` with N sleeping tasks | hot | O(due · log N); bench with 10,000 sleepers |
| resuming a C++ task | hot | one indirect call; target ≤ 20 ns per resumed task, measured |
| `spawn` | gameplay | a pool pop and a ready-list push; 0 allocations once warm, tested |
| script task step | hot | as `run` of its commands, plus re-binding captures (one store each) |
| creating a script task | gameplay | one scan per body text (cached), a frame from the pool |
| `until` polling | hot | one predicate call per waiter per tick: flagged as the costly wait |

Watch items:
- The `until` cost grows with waiters. If profiles show it, M7b adds a `signal` that wakes
  its waiters, so a waiter costs nothing until the signal fires.
- A C++ coroutine frame's size depends on the compiler and build: a debug MSVC frame can
  be several times a release one. The budget is per Scheduler so a debug build can raise
  it, and the error names both sizes.
- `<coroutine>` must enter D55's allow-list of public headers. It is language support:
  it declares `coroutine_handle` and the traits, and allocates nothing.

## Benchmarks

`bench/bench_tasks.cpp`:
- `tick` with 10,000 tasks asleep and none due (target: under 100 ns);
- resuming 1,000 C++ tasks that each `co_await next_tick()` (per task; target ≤ 20 ns);
- `spawn` plus first step of a C++ task (with allocation count: 0 once warm);
- a script `thread_task` step running `{add 1 2}`, beside `{add 1 2}` evaluated directly;
- creating a script `delay_task` whose body captures two variables.

## Budget

| Area | Estimated lines |
|---|---:|
| `Task`, promise, pool, `TaskHandle` | 120 |
| `Scheduler`: tick, heap, ready list, sync queue, stats | 150 |
| Awaitables (`delay`, `until`, `next_tick`) | 50 |
| Script tasks: three kinds, waits, capture scan and cache, `$this` | 170 |
| Deferred `{delete}`/remove and `ctx.sync()` | 40 |
| **Total** | **~530** |

That takes the core from 8,439 to about 8,970 of 10,000, leaving about 1,000 for M8 and
M10's core instrumentation.

## Open questions

**Answered 2026-10-12 (D67):** Q1–Q4, each as recommended. Q5–Q7 remain open.

**Q1. Time.** What does `tick(dt)` advance?
- **Recommended.** A `double` in units the host picks, one timeline per Scheduler. Beats,
  seconds, and frames are all one type, and a game with two clocks makes two Schedulers.
- **Integer ticks.** The spec's `100_ticks` literal reads this way. It is exact and
  deterministic, but every interp is then in whole ticks, and a host with seconds must
  convert.
- **Both.** A tick count and a time per Scheduler. More state; probably not needed.

**Q2. Script variables in a task.**
- **Recommended.** Capture by value at creation, automatically: every bound `$variable`
  the body names, except globals. This closes the original's open problem with no new
  syntax.
- **Explicit.** The original's `(preserve $a $b)`. Nothing surprising is captured, but
  every forgotten variable is a bug that shows only when the task runs.
- **By reference.** Tasks see variables as they are when they run. That is only
  dynamic scope, which is what broke in the original.

**Q3. What waits for the sync point?**
- **Recommended.** Spawn, `{delete}`/`remove`, and `cancel` (except a task cancelling
  itself) are deferred to the end of the tick. `{new}` and registration are not.
- **Everything D58 listed.** `{new}` is deferred too. Scripts then cannot use an object
  they just made until the next tick, which breaks `{set $b {new Ball}} {$b ...}`.
- **Nothing.** Changes apply at once, and results depend on the order tasks run in.

**Q4. Tasks and the object that made them.**
- **Recommended.** A task made with `$this` bound captures it, and is cancelled when the
  object is removed: the original killed an object's tasks with it.
- **Independent.** A task outlives its object, and `$this` reads as a stale handle. That
  is more general, but leaks work into dead objects.

**Q5. `{wait}` inside `thread_task`.**
- **Recommended.** Only at the thread's top level. A wait inside an `{if}` or a function
  would need the evaluator itself to suspend, which means making every native call a
  coroutine.
- **Anywhere.** The evaluator becomes resumable: much larger, and slower on every call,
  not only in tasks.

**Q6. `interp_task`'s arguments.** The spec writes `{scheduler interp_task $frame 0 100
{...}}`, with no duration.
- **Recommended.** `{interp_task $var from to duration body...}`: an explicit duration in
  the Scheduler's units, which works for any timeline.
- **The spec's form.** `from` to `to` over `to - from` units, so `$frame` counts one per
  unit. It is shorter, but is only meaningful when the value is itself a time.

**Q7. Pool defaults.** A 1 KB frame budget, chunks of 64 blocks, and no cap on live tasks
by default (`max_tasks` sets one).
- **Measured so far.** In a probe, a task with one `int` local measured 56 bytes on GCC
  at `-O2` and 48 on Clang at `-O0`. A local 1 KB array pushed GCC's frame to 1,072
  bytes, which the pool refused.
- These are guesses until real task frames are measured in debug builds. The first step
  measures them on GCC, Clang, and MSVC and records the numbers.
