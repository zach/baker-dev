# M6 plan: messages and debugging

Draft for review. Spec §5 ("Typed messages", "Debugging"), §7 (the message benchmark),
and §10 (M6 row) are the requirements. The talk digest (Appendix A, "Messages" and
"Debugging") fills in the original. The open questions at the end change the design. Once
they are answered, the decisions go into `docs/decisions.md` (D52 onward) and this file is
deleted.

The script call stack does not depend on any open question, so it is built first (step 1
below) while the questions are open. *Done: D52.*

## Scope

From the M6 row:

- typed messages, and the allocation-free C++→C++ path they give;
- the script call stack, with `script_backtrace()`;
- the trace hook;
- call statistics and `hardening_candidates`.

Done when:

1. The message benchmark meets §7: C++→C++ typed dispatch in ≤ 15 ns, with zero
   allocations.
2. An error thrown 4 frames deep renders a correct stack: `file.dta:line` frames,
   innermost first.
3. Call statistics add no allocations, and compile out under `MOIRA_SHIPPING`.

Carried from §5 and M5:

- the two message paths converge: a C++ `handle(HitMsg)` reaches a script handler
  `(hit ($points) ...)`, and `{bob hit 20}` reaches a C++ `on<HitMsg>` handler;
- aggregate members map positionally to script arguments;
- D50's dispatch order holds for messages too: script handlers first;
- the native call (`{add 1 2}`, 31 ns) must stay under 40 ns with the frame list and the
  statistics in it.

Not in M6:

- tasks (M7), which will need to save and restore the frame list across a suspend;
- an interactive debugger (spec: reachable, not built);
- the original's message macros on the script side ("specialized script macros match").
  A message's script name is its `name`; nothing more is needed to keep handlers stable.

## Design

### 1. The script call stack (spec §5, "Debugging")

- **Frames.** `Impl::call` already keeps a `Frame` on the C++ stack for the depth count.
  It gains the command being run and a parent pointer, and `Impl` keeps the innermost
  frame. That is two more stores per call, and no allocation.
  ```cpp
  struct CallFrame {                     // public, read-only: for debuggers and tools
      const Array* command;              // the call: its head, file, and line
      const CallFrame* parent;
  };
  const CallFrame* Context::top_frame() const noexcept;
  ```
- **Errors carry the stack.** `Error` gains `std::vector<StackEntry> stack` (function,
  file, line), innermost first. When a failed result first leaves a frame, the frames
  still live are copied into it. This is on the error path only. `moira::data` never fills
  it in.
- **Native exceptions.** *(Changed in the building: D52.)* A `moira::Exception` thrown by
  a native function records the stack when it is constructed, through a hook in
  `moira::data`. Catching it at each thunk instead cost about 10 ns per call.
- **Rendering.**
  - `to_string(error)` stays the one-line form.
  - `describe(error)` adds one line per frame, innermost first:
    `  in {grow} (people.dta:12)`.
  - `Exception::what()` and the default notice sink use `describe`.
- **`moira::script_backtrace()`** renders the current thread's live frames, through
  `Context::current()`, so an assert handler or crash reporter can splice it in without
  holding a Context. `ctx.backtrace()` returns the same frames as `StackEntry`s.

### 2. Typed messages (spec §5, "Typed messages")

```cpp
struct HitMsg { static constexpr auto name = "hit"; int points; };

struct Person : moira::Object<Person> {
    int hp = 100;
    void on_hit(const HitMsg& m) { hp -= m.points; }
    static void bind(moira::Binder<Person>& b) { b.on<HitMsg>(&Person::on_hit); }
};

bob.handle(HitMsg{.points = 20});     // C++ -> C++: no Node, no Array, no allocation
```

- **Fields without reflection.** An aggregate's members are reached by structured
  binding. Its field count is found by the usual brace-initialisation probe, for up to 8
  fields. A message with more fields, or one that is not an aggregate, is a compile error
  naming the limit.
- **Message identity.** Each message type `M` gets a key: the address of an inline
  variable template, `&detail::message_key<M>`. No RTTI and no string compare.
- **`b.on<M>(handler)`.** The handler is a member function or a callable taking `T&`
  first, then `const M&`. It registers two things in `T`'s `TypeRecord`:
  - a **message entry** `{key, thunk}` for the C++ path. Entries are sorted by key, and a
    type has few, so the search is short;
  - a **method** named `M::name`, whose parameters are `M`'s field types. Its thunk reads
    the arguments through M4's binding, builds an `M`, and calls the handler. So
    `{bob hit 20}`, the reference, and `check` all see `hit (int)` with no new machinery.
- **`obj.handle(msg)`** (on `ObjectBase`; `try_handle` is the non-throwing twin) follows
  D50's order:
  1. **A script handler named `M::name`** on the object's class. A per-Context bitmap,
     indexed by symbol, records which names any `{class}` handles, so the usual answer
     ("no") costs one load. If there is one, the fields become argument nodes and the
     command is sent as `ctx.call` would (this path may allocate).
  2. **The typed entry** for `M`: the fast path. One search and one indirect call, with
     no `Node` built.
  3. **A native method named `M::name`** bound with `b.method`, called with the fields as
     arguments (it may allocate).
  4. Otherwise unhandled: Q1.
- **Objects in no Context** still take step 2. Steps 1 and 3 need a Context.
- **The reply** is whatever the handler returns, as a `Node` (Q1).
- **Inheritance.** Message entries are copied when `T`'s table is built, as methods are
  (D49). A derived type's entry replaces its base's.

### 3. The trace hook

- `ctx.trace(bool)` turns tracing on or off. While it is on, every call reports to a
  sink:
  ```cpp
  struct TraceEvent { const CallFrame* frame; uint32_t depth; const Node* result; /* null on entry */ };
  using TraceSink = void (*)(void* user, const TraceEvent&);
  void Context::set_trace_sink(TraceSink, void* user);
  ```
  The default sink prints each call indented by depth with its file and line, and on exit
  its result or error.
- When off, the cost is one well-predicted branch per call.

### 4. Call statistics (spec §5)

- Each `Function` record keeps `calls`, `total_ns` (inclusive), and `self_ns` (inclusive
  minus the calls it made), updated in place. Script handlers on a `{class}` keep the same
  counters, so expensive handlers can be found too.
  - Self time is what `hardening_candidates` ranks by, because inclusive time counts a
    recursive function once per level.
  - Each frame carries its children's time. That is the only extra state.
- Native methods are not counted. Their tables are static and shared between Contexts
  (D49), so counting them would need a per-Context table. They are C++ already, so there
  is nothing to push up.
- The clock is a `TimeSource` (M3b's type), defaulting to `steady_clock`, so a test can
  use a fake clock and check exact sums.
- Reading:
  - `ctx.stats()` returns a vector of `{name, on, tier, calls, total_ns, self_ns}`, sorted
    by self time. Reading allocates; collecting does not.
  - `ctx.reset_stats()` clears the counters.
  - `ctx.hardening_candidates(threshold_ns)` lists the script-tier entries whose self time
    is at least the threshold.
- **`MOIRA_SHIPPING`.** The counters, the clock reads, and the frames' child time are
  removed by `if constexpr (kShipping)`. `stats()` returns an empty list. A test built in
  the shipping CI job checks that `stats()` is empty after calls.
- When the clock is read: Q2.

### Tooling

- **The reference** lists the messages each type handles, under their method names, with
  the message type: `hit (int)  — message HitMsg`.
- **`check`** needs nothing new, because a message is a method to it.

## Benchmarks

`bench/bench_call.cpp` gains these rows:

- `bob.handle(HitMsg{20})`: the target is ≤ 15 ns, with 0 allocations.
- `ctx.call(bob, "grow", 1.5f)` beside it, for comparison: the general path, which
  allocates.
- `{add 1 2}` with tracing off and statistics collecting. It must stay ≤ 40 ns, and the
  change from M5's 31 ns is recorded.
- The same, with the clock on, if Q2 makes that a separate mode.

## Budget

| Area | Estimated lines |
|---|---:|
| Frames, error stacks, `describe`, `script_backtrace`, catching at the thunk | 110 |
| Field count, decomposition, message keys | 60 |
| `on<M>`, message entries, `handle` and its four steps, script bitmap | 150 |
| Trace hook and default sink | 50 |
| Statistics, `stats`, `hardening_candidates`, shipping switch | 110 |
| Reference additions | 20 |
| **Total** | **~500** |

That takes the core from 7,148 to about 7,650 of 10,000. It leaves about 2,350 lines for
M7 (~400), M8 (~400), and M10's core instrumentation (~300 or more), so the budget holds
with room to spare, unless M8b's shapes are counted in it, which they are not (spec §6).

## Open questions

**Answered 2026-10-11 (D53):**
- **Q1, messages.** The recommended option, plus a broadcast `handle_all` that groups by
  type.
- **Q2, statistics.** Counts always; timing behind `set_profiling`.
- **Q3, tracing.** Compiled out under shipping.

The rest of M6 waited for the hygiene work of M5b (D54–D56; done in D59–D65). Note that D54
removes the throwing forms, so `handle` exists only in its `Expected` form, and drops the
`try_` prefix.

The original options follow, for the record.

**Q1. What does `handle` return, and is an unhandled message an error?**
- **Recommended.** `try_handle(msg)` returns `Expected<Node>`: the handler's result
  converted as `to_result` does, Int 0 for `void`. An unhandled message is a `NotFound`
  error naming the type and message, consistent with unhandled methods (spec §5).
  `obj.handles<M>()` answers cheaply, without allocating, so a broadcaster that sends to
  many objects can skip the ones that do not care instead of paying for an error string
  each time.
- **Silent.** An unhandled message returns an empty result, not an error, as the
  original's `Handle` did. Broadcasting is cheap, but a misspelled handler name is silent.
- **Typed replies.** `M::reply` names the reply type, and `handle` returns
  `Expected<M::reply>` with no `Node` in between. This is faster for float replies (no
  decimal conversion), but every message must say what it returns, and a script handler's
  result must convert.

**Q2. When are call statistics timed?**
- **Recommended.** Counts are always collected in non-shipping builds (one increment).
  Timing is switched on at run time with `ctx.set_profiling(true)`, off by default. Two
  clock reads per call (about 20–40 ns with `steady_clock`) would roughly double a native
  call in every development build, where designers spend their day, even when nobody is
  looking at the numbers.
- **Always timed.** As the spec reads: every call in non-shipping builds is timed. This is
  simplest, but `{add 1 2}` would likely miss the 40 ns target in development builds.
- **All behind the switch.** Neither counts nor times without `set_profiling(true)`. This
  is the cheapest, but you cannot ask "was this ever called?" after the fact.

**Q3. Is tracing compiled out under `MOIRA_SHIPPING`?**
- **Recommended.** Yes, with the statistics. It is a development aid, and removing it
  takes its branch out of the shipped hot path. `trace(true)` then does nothing, and
  `set_trace_sink` is kept so code still compiles.
- **Keep it.** A shipped game can turn on tracing to diagnose a live issue (through an
  explicitly enabled path, spec §6), at the cost of one branch per call.

**Defaults, no answer needed.**
- The frame list belongs to the Context. `script_backtrace()` finds it through
  `Context::current()`, which is what evaluation already sets.
- A frame is pushed for every call, native or script, so a backtrace shows the native
  function an error came from, as well as the script that called it.
- `Error::stack` is filled once, by the innermost frame the error leaves. An error that
  already has a stack keeps it as it unwinds further.
- `describe` is new, rather than a change to `to_string`. Tests and tools that match the
  one-line form keep working.
- Messages may have up to 8 fields. The limit is one constant; raising it costs a line
  per field.
- `handle` on a message type the object's type never bound, and has no script handler or
  method for, is unhandled at run time, not a compile error. A base type pointer cannot
  know what a derived type handles.
