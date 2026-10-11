---
name: acton-review
description: Review Moira designs, milestone plans, and diffs the way a data-oriented console-engine technical director (Mike Acton's school) would. Use when writing or reviewing an M-plan, a decisions.md entry, a hot-path change (evaluation, dispatch, tweaks, messages, tasks, the binary cache), or anything that adds allocation, indirection, abstraction, or runtime work; also when the user asks "what would Acton say", for a data-oriented or performance review, or to check alignment with console engine practice.
---

# Data-oriented review for Moira

Moira is being built for a project whose technical director is Mike Acton. His views stand
in for those of high-end console engine programmers generally. He says out loud what many
technical directors think (see "Why the line is held"). This skill turns those views
into review questions you can check against Moira's code and plans. Where Moira chooses
something else, the review makes the choice explicit and costed. The burden of proof is
on the choice, not on the reviewer.

Sources and how sure each attribution is: `references/sources.md`. Moira's known gaps
against this lens: `references/moira-gaps.md`. Read both before a full review.

## Why the line is held (read this first)

*The project owner's account of the role, recorded 2026-10-11. It sets the posture for
every review.*

**It is not one man's view.** In the project owner's firsthand experience, technical
directors they worked with (John Cash, Adrian Stephens, Jeff Lander) generally thought this
way. They were simply less outspoken about it than Acton. The view is seldom stated in
public, but it was widely held, at least up to the 2010s. Treat Acton's talks as the
clearest written statement of a common professional position, not as an outlier's
manifesto. The public record of criticism comes mostly from outside console engine work
(see "Where the school disagrees"). It should not be read as a sign that the field is
divided.

From outside, and to anyone without a technical director's combat experience, this stance
can look narrow-minded, stuck in the past, inflexible, and antagonistic. Do not read it
that way, and do not argue from that reading.

- **The TD speaks for the player.** On a large game, the technical director acts as a
  technical product manager, a role game development otherwise lacks. Their first
  concern is that the game's performance is not sacrificed to technical choices that
  give the player nothing. Players do not feel an engineer's pleasure in elegant code.
  They feel a game that is not jerky and not late. (The project owner borrows a line here
  they credit to Brad Bird: "pain is temporary, film is forever".) The investor's
  interest follows, but the player's comes first.
- **The usual threat is not malice but unawareness.** Enthusiastic engineers sometimes
  defend a costly choice with conventional wisdom about object orientation, garbage
  collection, exceptions, or RTTI. More often they make the choice without knowing what
  it costs.
- **The nightmare is entanglement.** Code is resistant to change. The worst outcome is
  finding the game dependent on a technology that taxes every frame and is so intertwined
  it cannot be removed. Holding a hard line early is how a TD prevents that, and it has
  to be hard.
- **It is a negotiation, and the TD is supposed to win the important ones.** To the
  engineer, losing useful capabilities feels like being sent back to the stone age. That
  is a real cost, and it is worth paying when the player gets the frame back.

**The closer to the core of the game, the more this attitude rules.** The industry's own
choices show it:
- Lua won game scripting, odd as it is, because the host controls its heap and its
  performance (through a custom allocator), unlike interpreters from the
  devil-may-care world of desktop applications.
- C++ is the language of choice because you do not pay for what you do not use. Into a
  carefully curated engine codebase, carelessly introducing the STL, unwinding
  exceptions, or RTTI goes very badly.

**Moira is at the very heart of a game engine, so it must be thoroughly Acton-compatible.**
Expect extreme skepticism from any TD evaluating it, and design for that review.
- A TD who sees that a recommended library depends on, say, Boost will roll their eyes,
  and the recommender will be teased for it.
- Moira's answer has to be visible in its build: zero external runtime dependencies
  (spec §6), every costly feature off or removable by a switch, nothing paid for unless
  it is used.

So when this skill and an engineer's preference disagree, the default is the skill's. Win
the exception with numbers, or drop it. And remember why: all of this serves the game and
the player.

## The stance, in his terms

Quotes are from his slides or abstracts unless marked otherwise. Slide numbers are pages
of the CppCon 2014 deck's PDF (201 pages), checked against the deck itself
(`references/sources.md`).

1. **"The transformation of data is the only purpose of any program"** (the keynote
   abstract). Slide 29 puts it as: "The purpose of all programs, and all parts of those
   programs, is to transform data from one form to another." The programmer's job is to
   solve data-transformation problems, not to write code. Every Moira subsystem is a
   transform: text → tree, tree → merged config, command → value, cache file → tree. Name
   the input, the output, and the cost of each one.
2. **Know your data, and know your hardware.**
   - "If you don't understand the data you don't understand the problem." (slide 30)
   - "If you don't understand the cost of solving the problem, you don't understand the
     problem." (slide 34)
   - "If you don't understand the hardware, you can't reason about the cost of solving
     the problem." (slide 35)
   - "Software does not run in a magic fairy aether powered by the fevered dreams of CS
     PhDs." (slide 43)

   Reason about cache lines, latency, and what the real values are.
3. **Solve the problem you have.** "Solving problems you probably don't have creates more
   problems you definitely do." (slide 37) Generic machinery has to be paid for by an actual
   use case.
4. **"Where there is one, there are many. Try looking on the time axis."** (slide 40) An
   API that handles one thing at a time (one tweak, one message, one object, one node)
   should be asked what happens when it is called 10,000 times a frame, or every frame
   for an hour.
5. **"If you have different data, you have a different problem."** (slide 33) A 20-child
   config array, a 5 MB song database, and a 3-node command are different problems. One
   code path serving all three is a claim that needs evidence.
6. **"Latency and throughput are only the same in sequential systems."** (slides 38–39) Say
   when each result is needed, whether this frame, at load, or offline at build time.
   Related rules of thumb:
   - "The more context you have, the better you can make the solution. Don't throw away
     data you need." (slide 41)
   - "NUMA extends to I/O and pre-built data all the way back through time to original
     source creation." (slide 42) In Moira's terms: a decision the bake can make should
     not be thrown away and remade at runtime.
7. **Reason about cost, including the cost of change.**
   - "Everything is a data problem. Including usability, maintenance, debug-ability,
     etc. Everything." (slide 36)
   - After a worked cache-line example (a 10x speedup, slide 117), the deck adds
     goals: "Code is maintainable", "Code is debugable", "Can REASON about cost of
     change" (slide 118).
   - "Ignoring inconvenient facts is not engineering; It's dogma." (slide 119)
8. **Design for the common case.** Spoken in his March 2018 Unity talk on data-oriented
   design, answering a question: "the actual global energy that you use to make a
   transformation should be proportional to the amount of surprise in the data." His
   example is a UI, where frame B is almost always identical to frame A, so redrawing it
   should cost almost nothing. Expected values take the cheap path, and only surprising
   ones pay. Moira's reload compares content and re-merges from the lowest change (D35)
   in this spirit: a tweak that did not change should cost nothing.
9. **From "Everyone Watching This Is Fired" (GDC 2019).** These items are verbatim, as
   reported from the slides:
   - "I can articulate precisely what problem I am trying to solve."
   - "I can articulate how much my problem is worth solving."
   - "I have a plan b in case my solution to my current problem doesn't work."
   - "I can articulate the (various) latency requirements for my current problem."
   - "I know the most common actual, real life values of the data I am transforming."
   - "I know the acceptable ranges of values of all the data I am transforming."

   The talk also calls for profiling and debugging rather than guessing (paraphrased).

**Practice at Insomniac** (CppCon 2014, slides 18–27, "How are games like the Mars
rovers?"). The slides only list the features. What he said about each, from the talk
transcript (`references/sources.md`):
- **Exceptions:** "we avoid them. We turn them off if we can at all help it." A
  third-party library that needs them is sandboxed off.
- **Templates:** "we don't have a hard rule that says no templates", but "there's
  occasionally a good use, but most of the time what it is is a poor use. And most of the
  time it slows things down." Heavy template use "cripples compile times". From the Q&A:
  "A template is just a poor man's text process[or]"; generate the code with a real tool
  instead. Code duplication is "almost certainly not as big a problem as people think it
  is."
- **Iostream:** they use standard out for logging, but not C++ iostream.
- **Multiple inheritance:** "that's right out."
- **Operator overloading:** no strict rule, but "you will get the frowny face, depending
  on how complex that overloading is."
- **RTTI, "No STL", "Custom allocators (lots)":** on the slides (24–26). The transcript
  is garbled here, so there is no spoken gloss.
- **"Custom debugging tools"** (slide 27): tools that introspect the data, pull it from
  the console to the development PC, and examine it live or offline.

Treat all of it as a strong default for runtime engine code, not as a law for every line.
See "Where the school disagrees" below.

**Also from the talk** (spoken, from the transcript):
- **Priorities:** hard deadlines; soft real time (16 or 33 ms frames, with most work
  measured in microseconds); usability, meaning tools that are "easy and quick" for
  artists, designers, and sound engineers; and performance.
- **Solve for the common case:** "solve for the most common case first. The most common
  actual, real, live case first, and not the most generic." His example: a dictionary of
  key–value pairs loads every value into the cache while scanning keys it mostly won't
  hit. Store the keys together, find an index, and read the one value from a separate
  table.
- **Costs to estimate with:** about 3 cycles for L1, about 20 for L2, and 200+ for main
  RAM (numbers he credits to Jason Gregory). Estimate on the back of an envelope to get
  the order of magnitude, then measure.
- **Last-minute decision making:** a per-item `bool` or `switch` testing state the caller
  already knows (is this node the root? which case is the whole hierarchy in?). Split the
  states so the caller picks the path once for the whole batch.
- **Strings:** "Strings are generally bad." "The best code is code that doesn't need to
  exist at all." Hash strings offline, and push work back in time. Generate code for
  results that can be known statically.
- **Help the compiler:** "compilers are not magic". Eliminate ghost reads and writes, and
  hoist loop invariants yourself, even obvious ones.
- **Portability:** "general portability is a fool's errand." Know the finite set of
  target platforms, with their minimum, maximum, average, and common case.
- **Maintainability:** fewer states are easier to reason about. Separated data also
  resists the refactor that drops a `bool` into the middle of a hot struct.
- **Language:** his own preference "would be C99". C++ is used because it is culturally
  dominant, and a feature is used only if it pays for itself.
- **Not his line:** the closing "design patterns are spoon-fed material for brainless
  programmers…" is a quote from Christer Ericson. Do not attribute it to Acton.

**The "Three Big Lies"** (CellPerformance, 2008; the deck frames data-oriented design as
"a response to the culture of C++ … and The Three Big Lies it has engendered", slides
45–46): "Software is a platform." "Code should be designed around a model of the world."
"Code is more important than data." In the talk: "Reality is not a hack… Reality is the
actual problem", and "if you think the right way is some way other than the way that
actually works in context, then your idea of the right way is wrong." Moira's own hazards
with each lie:
- treating the library as a platform for every possible game;
- an object model that mirrors designers' nouns, not the data's access patterns;
- clever evaluator code that ignores the shape of the data it walks.

**From the March 2018 Unity talk** ("democratize data-oriented design"; spoken, from the
transcript):
- **"Performance by default."** "Your first instinct when you put something in Unity
  should be a good first level approximation of the right answer", not something "you
  need to rethink or throw away". It should also be "optimizable by default": later work
  improves the same layout instead of starting over. For Moira, the obvious API (a
  one-line tweak, `ctx.func`, `{bob grow 10}`) must be the fast path. There should be no
  convenient-but-slow API with an expert-only fast one beside it.
- **Hard rules, few of them.** "These aren't complicated rules, but they're hard rules":
  declare what you read and what you write; streams of small data; value types only.
  Declaring access is what lets the system verify correctness and race freedom.
  Moira's equivalent is in its signatures: `Raw<T>` marks what a function will not
  evaluate.
- **Nothing opaque.** Component memory is "blocks of memory organized into 64K chunks,
  none of which is opaque". The compiler's output is inspectable too. "This is not about
  making things more opaque for you." Users must be able to see the layout of Moira's
  data and the cost of its calls (static asserts, the reference, benchmarks, statistics,
  the trace hook).
- **An entity is an index.** "An entity is nothing more than a lookup index or an ID into
  those parallel streams… think about it as a database key." Moira's salted object handles
  follow the same idea.
- **Few, named sync points.** Named barriers replace a hand-drawn graph of dependencies
  between systems, because that "becomes sort of too complicated for you to manage" and
  "you want to minimize the synchronization points across the frame anyway". Structural
  changes (adding or removing components) are queued and flushed at a barrier, so jobs
  see stable data and threaded results match sequential ones.
- **Cache misses are sometimes the price.** "You can't get away with not having no cache
  misses ever… At some point, you do have to load data." For random access, copy into an
  array built for it, and pay the miss knowingly.
- **"Simpler" is not "familiar."** Answering a questioner who called game objects
  simpler: "I know it's more familiar", but once the hurdle is past "I don't agree that
  it's in fact simpler", because the design "removed things other than the thing that
  actually solves the problem." And: "Show us that" A is simpler than B.
- **Power.** On mobile, go wide, finish the frame's work early, "and then let the CPU rest
  for the rest of the frame."

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
memory layout. **Tool** code may be ordinary C++ inside, but it is shipped in the same
library, so it still may not:
- add a dependency;
- require exceptions or RTTI;
- put a heavyweight type (STL containers, `std::function`, `std::string`) into a header
  that game code must include;
- add work to a hot path, even when the tool is not in use.

Acton's critics mainly fault him for applying console-hot-path rules to all programs.
That is a point about other programs: Moira is engine core, and its tool code is only
relaxed inside these limits.

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
  - **Keys together, values elsewhere.** Any search (by tag, by symbol, by name) should scan
    a contiguous run of keys, not step through records or pointers to read one field
    each. This is the talk's dictionary example.
  - Look for padding and stray `bool`s in structs that are iterated. The deck's phrase for
    a `bool` in a struct is "Extremely low information density" (slides 120–122). It
    measures it as signal against noise: bits of information used per frame, divided by
    cache-line bytes loaded per frame (slides 147–148). Do the same arithmetic for any
    hot struct under review.
  - Prefer index-addressed flat arrays (Moira's per-Context tables indexed by
    `Symbol::index()`, D44) to maps.
  - New per-element structs on a hot path get a `static_assert(sizeof(...))`, as `Node`
    (16) and `Array` (24) have.
- **One versus many.** Does the API force one call per item? If the natural caller is
  "every object", "every tweak", or "every task", there should be a batch form taking a
  span. The single-item form is then a thin wrapper.
- **Branches on the common path.** A feature that is off should cost at most one
  predictable branch. Check that with the benchmark, not by reasoning.
- **No last-minute decisions.** A per-item check of something the caller already knows
  (a state flag, "has a script override?", "is this the root?") should be decided once,
  outside the loop, with the cases split.
- **Strings.** None on a hot path. Names are `Symbol`s, interned or hashed at load or at
  bake time. A `std::string` built during a call is a finding, except on the error path.
- **Exceptions and RTTI.** Never needed on a hot path. Everything works under
  `MOIRA_NO_EXCEPTIONS` (CI has the job) and with no RTTI (D49's `TypeRecord`). A `try`
  block on the hot path is a finding: D52 measured one at about 10 ns per call.
- **Templates.** Acton's stated cost is compile time and slower code, and his stated
  alternative is generated code. Moira's binding templates remove per-call marshalling
  decisions, which is a real use. Still ask:
  - what does each template cost in compile time, in code size per instantiation, and in
    debug-build speed;
  - would a small code generator do it better?

  A template that only buys "genericity" for one real use case is a finding. Watch the
  build time of a translation unit that includes `<moira/object.hpp>` like a benchmark.
- **Shipping.** Development-only work compiles out under `MOIRA_SHIPPING`
  (`if constexpr (kShipping)`), and the shipping CI job proves it.
- **Removability.** For any technology a change leans on, ask whether a game could turn
  it off or replace it later, without rewriting callers. Ask before it spreads, because
  it cannot be asked after. Moira's existing answers are the model:
  - `MOIRA_NO_EXCEPTIONS` (every throwing API has a non-throwing twin);
  - no RTTI;
  - `MOIRA_ATOMIC_REFCOUNT`;
  - `MOIRA_SHIPPING`.

  A new dependency on the global heap, the STL in a public signature, or a mandatory
  callback type is a finding unless it comes with its switch or its funnel.
- **Dependencies.** None at run time (spec §6). doctest is for tests only. Proposing a
  third-party runtime library, however good, is a finding by default.

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
| Tweaks | Is the read one load (it is: §7 codegen test)? Are tweaks updated in a batch on reload rather than chased one by one? Does reload run at a point the host chooses (a frame boundary), never in the middle of evaluation? |
| Evaluator | Lazy evaluation means no temporary argument lists, which is good. Per-call cost is recorded (34 ns). Count the loads on the dispatch path. |
| Objects and handles | Salted handles beat pointers (D4, D19). The slot table is a flat array. Do per-object loose parameters (vectors) belong on a hot path? |
| Typed messages (M6) | "Where there is one there are many": a broadcast to N objects should be one call over a span, grouped by type, with no `Node` built. Does the single-object `handle` force N dispatches and N misses? |
| Call statistics (M6) | Counters next to the function record (one cache line), with no clock read unless asked, and compiled out in shipping. |
| Tasks (M7) | Is a coroutine frame a heap allocation per task? Then where are the allocator and the pool? How many tasks are live per frame, and are they stepped in a batch by the scheduler? Are structural changes (`{new}`, `{delete}`, registration) during a step queued and applied at a named sync point, as DOTS's command buffers are? |
| Binary cache (M8) | Load in place: one read, fixups, ≤ 2 allocations, with the expensive work done at bake time. |
| Tooling (`check`, reference, write-back) | Tool path: ordinary C++ is fine. Judge it on iteration time and clarity. |

## Where the school disagrees (use judgment)

- **Templates and generic code.** Insomniac had no hard rule against templates, only
  strong suspicion: "occasionally a good use", usually "a poor use", and "cripples compile
  times". Moira's binding layer is templates, deliberately: spec §5 deduces signatures
  so that binding needs no macro tables. Accept templates where they move work to compile
  time and both the benchmarks and the build times hold. Flag them
  where they only add generality, and consider a generator for anything bulky.
- **"Never virtual" is not the rule.** Measured, the virtual-call cost is small when the
  call is predictable and in cache. The real costs are scattered heap objects and lost
  batching. Review the data layout first, then dispatch.
- **Scripting itself.** No quoted source shows Acton for or against embedded interpreters,
  and the CppCon 2014 talk and its Q&A do not mention scripting. The nearest evidence is
  the 2018 Unity talk. There he did not take scripting away from game programmers. He
  offered a "high performance C# subset" under hard rules (value types only, no garbage
  collection, declared reads and writes), compiled for the hardware, and claimed about
  "two orders of magnitude" over "C# scripts".
  - The inference, not his words: he accepts a higher-level language for game code when
    its rules let the system lay data out and compile it well.
  - For Moira, that means judging the evaluator on whether data stays out of the
    per-entity loop and gets baked, not on whether there is an interpreter at all.
  - A second researcher's unquoted claim that he preferred compiled graphs to
    interpreted bytecode is consistent with this, but is still not citable.
  - Insomniac's engine team listed "Visual scripting" among its development tools
    (slide 5).
  - Peers in the same school shipped interpreters or Lisps: Bitsquid shipped Lua with hot
    reload. Naughty Dog built GOOL and GOAL, custom Lisps, for its PlayStation games.
  - The data-oriented question is not "script or not", but whether the interpreter is
    kept off the per-entity hot loop and whether its data is baked.
- **Scope.** Critics (Shamus Young, HN and gamedev.net practitioners) accept the
  performance argument but reject applying it to every program. That is fair for
  business software and editors. It does not soften the lens for Moira, which sits in the
  engine's core:
  - runtime and load paths face the full lens;
  - tool paths face the cost-of-change lens, within the limits in review step 1.
