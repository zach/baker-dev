---
name: acton-review
description: Review Moira designs, milestone plans, and diffs the way a data-oriented console-engine technical director (Mike Acton's school) would. Use when writing or reviewing an M-plan, a decisions.md entry, a hot-path change (evaluation, dispatch, tweaks, messages, tasks, the binary cache), or anything that adds allocation, indirection, abstraction, or runtime work; also when the user asks "what would Acton say", for a data-oriented or performance review, or to check alignment with console engine practice.
---

# Data-oriented review for Moira

Moira is being built for a project whose technical director is Mike Acton. His views stand
in for those of high-end console engine programmers generally. This skill turns those views
into review questions you can check against Moira's code and plans. Where Moira chooses
something else, the review makes the choice explicit and costed. It does not make Moira
conform blindly.

Sources and how sure each attribution is: `references/sources.md`. Moira's known gaps
against this lens: `references/moira-gaps.md`. Read both before a full review.

## The stance, in his terms

Quotes are from his slides or abstracts, with slide numbers from the CppCon 2014 deck,
unless marked otherwise.

1. **"The transformation of data is the only purpose of any program"** (the keynote
   abstract). Slide 30 puts it as: "The purpose of all programs, and all parts of those
   programs, is to transform data from one form to another." The programmer's job is to
   solve data-transformation problems, not to write code. Every Moira subsystem is a
   transform: text → tree, tree → merged config, command → value, cache file → tree. Name
   the input, the output, and the cost of each one.
2. **Know your data, and know your hardware.** The hardware is the platform, not the
   software. Reason about cache lines, latency, and what the real values are.
3. **Solve the problem you have.** "Solving problems you probably don't have creates more
   problems you definitely do." Generic machinery has to be paid for by an actual use case.
4. **"Where there is one, there are many. Try looking on the time axis."** (slide 41) An
   API that handles one thing at a time (one tweak, one message, one object, one node)
   should be asked what happens when it is called 10,000 times a frame, or every frame
   for an hour.
5. **"If you have different data, you have a different problem."** (slide 34) A 20-child
   config array, a 5 MB song database, and a 3-node command are different problems. One
   code path serving all three is a claim that needs evidence.
6. **"Latency and throughput are only the same in sequential systems."** Say when each
   result is needed, whether this frame, at load, or offline at build time.
7. **Reason about cost, including the cost of change.** His CppCon deck adds
   maintainability, debuggability, and the cost of changing code as goals. "Ignoring
   inconvenient facts is not engineering; it's dogma." (slide 120)
8. **Design for the common case.** Spoken, in a 2018 Unity keynote: "The actual global
   energy that you use to make a transformation should be proportional to the amount of
   surprise in the data." Expected values take the cheap path, and only surprising ones
   pay.
9. **From "Everyone Watching This Is Fired" (GDC 2019).** These items are verbatim, as
   reported from the slides:
   - "I can articulate precisely what problem I am trying to solve."
   - "I can articulate how much my problem is worth solving."
   - "I have a plan b in case my solution to my current problem doesn't work."
   - "I can articulate the (various) latency requirements for my current problem."
   - "I know the most common actual, real life values of the data I am transforming."
   - "I know the acceptable ranges of values of all the data I am transforming."

   The talk also calls for profiling and debugging rather than guessing (paraphrased).

**Practice at Insomniac** (CppCon 2014, slides 27–28). Confirmed verbatim: "No STL",
"Custom allocators (lots)", and "Custom debugging tools". Excerpts of the same slides also
list no exceptions, no RTTI, no iostream, no multiple inheritance, no operator overloading,
and limited templates, but that part is not confirmed verbatim. Treat all of it as a
strong default for runtime engine code, not as a law for every line. See "Where the school
disagrees" below.

**The "Three Big Lies"** (CellPerformance, 2008): "Software is a platform." "Code should be
designed around a model of the world." "Code is more important than data." Moira's own
hazards with each:
- treating the library as a platform for every possible game;
- an object model that mirrors designers' nouns, not the data's access patterns;
- clever evaluator code that ignores the shape of the data it walks.

**Shared practice in the field** (Llopis and others, not Acton himself):
- "Don't do anything at runtime that you can do offline."
- Load in place: a flat, relocatable blob, read in one go, then a pointer-fixup pass.

## Review procedure

For a plan, decision, or diff, answer these in order and write the answers down.

### 1. Classify the path
Mark each piece of code as one of:
- **hot**: per frame, per call, or per node at runtime;
- **load**: once per file or reload;
- **tool**: editor, `check`, the reference, write-back, or development only;
- **offline**: the build-time bake.

The hard rules below apply to **hot** with full force and to **load** for allocation and
memory layout. **Tool** code may be ordinary C++. Acton's critics, and many of his peers,
mainly fault him for applying console-hot-path rules everywhere. Do not make that mistake
in either direction.

### 2. State the transform
- What is the input data, what is the output, how often does it run, and how much data
  goes through it?
- What are the common real values? For example: most commands have 1–3 arguments, most
  tweaks are floats, and most arrays have fewer than 20 children. Is the design built
  around those, with the rare cases on a slow path?
- If the plan cannot answer these, that is the first finding.

### 3. Hot-path rules (check every item)
- **Allocation.** The hot path allocates nothing after warm-up, and a test proves it with
  `tests/support/alloc_counter`, not by inspection. Moira already does this for calls,
  methods, `get`/`set`, and tweak reads. New hot paths must add the same test.
- **Indirection.** Count the dependent loads (pointer chases) per operation. Each likely
  cache miss on a cold path costs about 100+ cycles. A `shared_ptr`, a `std::function`, a
  virtual call, or a per-node heap object on the hot path needs a measured reason.
- **Layout.**
  - Hot fields go together, and cold fields (docs, names, file and line, debug data) go
    elsewhere.
  - Look for padding and stray `bool`s in structs that are iterated. The deck's phrase for
    a `bool` in a struct is "extremely low information density".
  - Prefer index-addressed flat arrays (Moira's per-Context tables indexed by
    `Symbol::index()`, D44) to maps.
  - New per-element structs on a hot path get a `static_assert(sizeof(...))`, as `Node`
    (16) and `Array` (24) have.
- **One versus many.** Does the API force one call per item? If the natural caller is
  "every object", "every tweak", or "every task", there should be a batch form taking a
  span. The single-item form is then a thin wrapper.
- **Branches on the common path.** A feature that is off should cost at most one
  predictable branch. Check that with the benchmark, not by reasoning.
- **Exceptions and RTTI.** Never needed on a hot path. Everything works under
  `MOIRA_NO_EXCEPTIONS` (CI has the job) and with no RTTI (D49's `TypeRecord`). A `try`
  block on the hot path is a finding: D52 measured one at about 10 ns per call.
- **Templates.** Allowed where they remove runtime work (signature deduction removes
  per-call marshalling decisions). Ask what each one costs in compile time, code size per
  instantiation, and debug-build speed. A template that only buys "genericity" for one
  real use case is a finding.
- **Shipping.** Development-only work compiles out under `MOIRA_SHIPPING`
  (`if constexpr (kShipping)`), and the shipping CI job proves it.

### 4. Load-path and offline rules
- Anything computable at bake time belongs in the binary cache (M8): macro expansion,
  symbol interning, sorting, resolving the head of each command.
- Is the runtime load a single read plus fixups, with the allocation count fixed (spec
  §7: ≤ 2)?
- **Memory ownership.** Who owns each allocation, from which heap, and when is it freed?
  Today Moira uses global `operator new` everywhere (see `references/moira-gaps.md`). A
  console team will ask for an allocator hook, so any new load-path allocation should go
  through one funnel that a hook could later replace.
- **Iteration speed is a requirement, not a nicety.** Insomniac's engine group built
  designers' tools themselves, and fast iteration (hot reload, live tweaks) is why Moira
  exists. A data-oriented review must not trade away designer iteration time without
  saying so. Weigh the frame-time cost against the iteration-time cost explicitly.

### 5. Evidence
- Every performance claim names a benchmark row in `bench/` and a before/after number from
  the same machine, as D47, D51, and D52 do.
- "Should be fast" is not evidence.
- A regression on a §7 target blocks the change until it is fixed or a decision is
  recorded.

### 6. Report
Write findings as a short list, most costly first. Each finding names:
- the path class;
- the cost in cycles, allocations, bytes, or iteration time;
- the evidence or the missing evidence;
- a concrete change, or "record a decision".

Accepted deviations go into `docs/decisions.md` as a D-entry (spec §11: deviating is fine,
deviating silently is not). Do not rewrite code to follow this lens without a finding that
names its cost.

## Applying it to Moira's subsystems

| Area | The question a data-oriented TD asks |
|---|---|
| Parser, preprocessor | Is this a load-time transform whose output (the tree) is shaped for the runtime's reads, and could the bake do it instead? |
| `Node` / `Array` | 16-byte nodes, contiguous children, refcounted arrays. Are refcount traffic and per-array heap blocks acceptable for the 5 MB tree case, or should the cache load give one arena (M8)? |
| Lookup (`find*`) | Is the linear scan right for the real child counts (mostly < 20)? Sorted mode exists for the rest; is it used where the data is big? |
| Tweaks | Is the read one load (it is: §7 codegen test)? Are tweaks updated in a batch on reload rather than chased one by one? |
| Evaluator | Lazy evaluation means no temporary argument lists, which is good. Per-call cost is recorded (34 ns). Count the loads on the dispatch path. |
| Objects and handles | Salted handles beat pointers (D4, D19). The slot table is a flat array. Do per-object loose parameters (vectors) belong on a hot path? |
| Typed messages (M6) | "Where there is one there are many": a broadcast to N objects should be one call over a span, grouped by type, with no `Node` built. Does the single-object `handle` force N dispatches and N misses? |
| Call statistics (M6) | Counters next to the function record (one cache line), with no clock read unless asked, and compiled out in shipping. |
| Tasks (M7) | Is a coroutine frame a heap allocation per task? Then where are the allocator and the pool? How many tasks are live per frame, and are they stepped in a batch by the scheduler? |
| Binary cache (M8) | Load in place: one read, fixups, ≤ 2 allocations, with the expensive work done at bake time. |
| Tooling (`check`, reference, write-back) | Tool path: ordinary C++ is fine. Judge it on iteration time and clarity. |

## Where the school disagrees (use judgment)

- **Templates and generic code.** The Insomniac list avoids templates, but Moira's binding
  layer is templates. Accept them where they move work to compile time and the benchmarks
  hold. Flag them where they only add generality.
- **"Never virtual" is not the rule.** Measured, the virtual-call cost is small when the
  call is predictable and in cache. The real costs are scattered heap objects and lost
  batching. Review the data layout first, then dispatch.
- **Scripting itself.** No quoted source shows Acton for or against embedded interpreters.
  A second researcher reported that he prefers compiled graphs to interpreted bytecode
  (the DOTS era), but gave no quote, so do not cite it as his view.
  - Insomniac's engine team listed visual scripting among the tools it built.
  - Peers in the same school shipped interpreters or Lisps: Bitsquid shipped Lua with hot
    reload. Naughty Dog built GOOL and GOAL, custom Lisps, for its PlayStation games. The data-oriented question is not
  "script or not", but whether the interpreter is kept off the per-entity hot loop and
  whether its data is baked.
- **Scope.** Critics (Shamus Young, HN and gamedev.net practitioners) accept the
  performance argument but reject applying it to every program. For Moira this means:
  runtime and load paths face the full lens, tool paths face the cost-of-change lens.
