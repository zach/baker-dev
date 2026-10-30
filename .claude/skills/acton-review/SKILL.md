---
name: acton-review
description: Review designs, plans, and diffs of game-engine and engine-core C++ code the way a data-oriented console-engine technical director (Mike Acton's school) would. Covers runtime libraries at the heart of a game (allocators, containers, data and config layers, embedded scripting, messaging, job and task systems, asset loading, the runtime half of tools). Use for a design or milestone plan, a decision record, a hot-path change, or anything that adds allocation, indirection, abstraction, runtime work, build time, code size, static initializers, atomics, thread-locals, floating-point results that must match across platforms, or content from mods and live-ops; also when the user asks "what would Acton say", for a data-oriented or performance review, or to check alignment with console engine practice.
---

# Data-oriented review for engine-core code

Mike Acton's views stand in for those of high-end console engine programmers generally.
He says out loud what many technical directors think (see "Why the line is held"). This
skill turns those views into review questions you can check against code and plans. Where
a project chooses something else, the review makes the choice explicit and costed. The
burden of proof is on the choice, not on the reviewer.

The skill's files:
- `references/sources.md`: the sources, and how sure each attribution is.
- `references/recipes.md`: the command behind each check, with the numbers it gave on a
  real engine-core library, for calibration. Run them; do not reason your way to a number
  a command can give.
- The project's own record. A project that uses this skill keeps a list of its known gaps
  and accepted deviations, and the review reads it first. For Baker, the library this skill
  was developed on, that is `references/baker-gaps.md` and its decision log.

## Why the line is held (read this first)

*A project owner's account of the role, recorded 2026-10-11. It sets the posture for
every review.*

**It is not one man's view.** In the owner's firsthand experience, technical directors
they worked with (John Cash, Adrian Stephens, Jeff Lander) generally thought this way. They
were simply less outspoken about it than Acton. The view is seldom stated in public, but
it was widely held, at least up to the 2010s. Treat Acton's talks as the clearest written
statement of a common professional position, not as an outlier's manifesto. The public
record of criticism comes mostly from outside console engine work (see "Where the school
disagrees"). It should not be read as a sign that the field is divided.

From outside, and to anyone without a technical director's combat experience, this stance
can look narrow-minded, stuck in the past, inflexible, and antagonistic. Do not read it
that way, and do not argue from that reading.

- **The TD speaks for the player.** On a large game, the technical director acts as a
  technical product manager, a role game development otherwise lacks. Their first
  concern is that the game's performance is not sacrificed to technical choices that
  give the player nothing. Players do not feel an engineer's pleasure in elegant code.
  They feel a game that is not jerky and not late. (The owner borrows a line here they
  credit to Brad Bird: "pain is temporary, film is forever".) The investor's interest
  follows, but the player's comes first.
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

**Code at the heart of a game engine must be thoroughly Acton-compatible.** Expect extreme
skepticism from any TD evaluating it, and design for that review.
- A TD who sees that a recommended library depends on, say, Boost will roll their eyes,
  and the recommender will be teased for it.
- The answer has to be visible in the build:
  - zero external runtime dependencies;
  - every costly feature off, or removable by a switch;
  - nothing paid for unless it is used.

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
   solve data-transformation problems, not to write code. Every engine subsystem is a
   transform: text → tree, file → loaded blob, layers → merged config, command → value,
   input → frame. Name the input, the output, and the cost of each one.
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
   API that handles one thing at a time (one value, one message, one object, one node)
   should be asked what happens when it is called 10,000 times a frame, or every frame
   for an hour.
5. **"If you have different data, you have a different problem."** (slide 33) A 20-entry
   config table, a 5 MB database, and a 3-node command are different problems. One code
   path serving all three is a claim that needs evidence.
6. **"Latency and throughput are only the same in sequential systems."** (slides 38–39) Say
   when each result is needed, whether this frame, at load, or offline at build time.
   Related rules of thumb:
   - "The more context you have, the better you can make the solution. Don't throw away
     data you need." (slide 41)
   - "NUMA extends to I/O and pre-built data all the way back through time to original
     source creation." (slide 42) A decision the build can make should not be thrown away
     and remade at runtime.
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
   ones pay. A reload that compares content and redoes only what changed is in this
   spirit: a value that did not change should cost nothing.
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
45–46). The lies are "Software is a platform.", "Code should be designed around a model of
the world." and "Code is more important than data." In the talk: "Reality is not a hack…
Reality is the actual problem", and "if you think the right way is some way other than the
way that actually works in context, then your idea of the right way is wrong." How each lie
shows up in an engine-core library:
- treating the library as a platform for every possible game;
- an object model that mirrors designers' nouns, not the data's access patterns;
- clever code that ignores the shape of the data it walks.

**From the March 2018 Unity talk** ("democratize data-oriented design"; spoken, from the
transcript):
- **"Performance by default."** "Your first instinct when you put something in Unity
  should be a good first level approximation of the right answer", not something "you
  need to rethink or throw away". It should also be "optimizable by default": later work
  improves the same layout instead of starting over. The obvious API, the one-liner every
  example shows, must be the fast path. There should be no convenient-but-slow API with an
  expert-only fast one beside it.
- **Hard rules, few of them.** "These aren't complicated rules, but they're hard rules":
  declare what you read and what you write; streams of small data; value types only.
  Declaring access is what lets the system verify correctness and race freedom. Put such
  declarations in signatures, where the compiler can check them.
- **Nothing opaque.** Component memory is "blocks of memory organized into 64K chunks,
  none of which is opaque". The compiler's output is inspectable too. "This is not about
  making things more opaque for you." Users must be able to see the layout of the
  library's data and the cost of its calls: static asserts, benchmarks, statistics, a
  trace hook.
- **An entity is an index.** "An entity is nothing more than a lookup index or an ID into
  those parallel streams… think about it as a database key." Salted (generational)
  handles follow the same idea.
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

**The deck's own code review** (CppCon 2014, pages 158–184, a review of OGRE's
`OgreNode.cpp`; page 160 links the OGRE author's reply). It is the closest thing in the
record to an Acton review of a real diff, and it is the shape to copy:
- "(1) Can't re-arrange memory (much) / Limited by ABI / Can't limit unused reads / Extra
  padding." A public struct is a layout you cannot change later. This is the case for
  opaque implementations and handles at a library's boundary.
- "(2) Bools and last-minute decision making."
- "(5) Over-generalization". Complex constructors imply "Reads are unmanaged (one at a
  time…)", "Unnecessary reads/writes in destructors", "Unmanaged icache (i.e. virtuals) =>
  unmanaged reads/writes", and "Unnecessarily complex state machines (back to bools) – E.g.
  2^7 states". Its rule of thumb: "Store each state type separately / Store same states
  together".
- "(6) Undefined or under-defined constraints… e.g. Strings, generally. Filenames, in
  particular."
- "(7) Over-solving (computing too much)… you can make simple tools that do… E.g.
  Premultiply matrices", and "Work with the (actual) data you have."
- The fix, in three steps: "Step 1: organize. Separate states so you can reason about them.
  Step 2: triage. What are the relative values of each case, i.e. p(call) * count, e.g.
  in-game vs. in-editor. Step 3: reduce waste." Then "(back of the envelope read cost)
  ~200 cycles x 2 x count" (page 184).

So virtual calls are in the deck, as unmanaged instruction-cache reads, not as a ban.
Triage by `p(call) * count` is review step 1 below, in his words.

**GDC 2014 Code Clinic, "How to Write Code the Compiler Can Actually Optimize"** (Acton;
the deck is on GitHub, see `references/sources.md`):
- **Ghost reads and writes:** "Don't re-read member values or re-call functions when you
  already have the data." "Hoist all loop-invariant reads and branches. Even super-obvious
  ones that should already be in registers." It "applies to any member fields especially".
  The compiler re-reads because a store through any pointer might change a member, and
  "all compilers are limited", not only MSVC. In a hot loop, copy `this->size_`, table base
  pointers and the like into locals first, then read the generated code.
- **The summary rules:** "Arrange memory by probability of access." "Hoist decision making
  to first-opportunity." "Maximize memory read value."
- **Measure information density by compressing it:** dump a field every frame for 10,000
  frames, compress the dump, and compare the bits per frame with the cache lines loaded to
  read it (`references/recipes.md`). A field that compresses to under a bit a frame is a
  state to split out, not a field to read.
- "Our role is not to write 'good' code. Our role is to solve our problems well."

**Shared practice in the field** (not Acton himself; sources in `references/sources.md`):
- "Don't do anything at runtime that you can do offline." (Llopis)
- **Load in place.** A flat, relocatable blob, read in one go, then a pointer-fixup pass.
  Better still, offsets everywhere, so there is no fixup pass at all: "I don't use pointers
  in the resource data. Instead, I just use offsets all the time, both in memory and on
  disk." (Frykholm, "The Blob and I")
- **Triage by calls per frame** (Frykholm, "A Pragmatic Approach to Performance"):
  - "1-10 Performance doesn't matter. Do whatever you want."
  - "100 Make sure it is O(n), data-oriented and cache friendly."
  - "1000 Make sure it is multithreaded."
  - "10000 Think really hard about what you are doing."

  Use a complex solution only for "a factor 2 or so", in a system that matters. "Avoid 'do
  nothing' updates — instead, keep track of active objects."
- **Strings at runtime are for UI and debugging only.** Names are hashed at data compile,
  "Hash collision is considered a compile error", and the reverse-lookup table is "not
  loaded by the runtime" (Frykholm, "Strings Redux").
- **Make the struct you have most of smaller.** "Think about a struct that you have the most
  of in memory and think about how you can make it smaller. That's the whole trick."
  - His tools: indexes instead of pointers; booleans out of band; struct-of-arrays to
    remove padding; sparse data in hash maps; enums and flags folded into the tag
    "according to your actual distributions".
  - And "stop memoizing stuff I can calculate", such as a line and column recomputed from
    a byte offset.
  - Zig's tokens went from 64 bytes to 5, and its intermediate code became "only four
    arrays", each read and written with one system call (Kelley, Handmade Seattle 2021).
- **A dirty flag can cost more than the work.** In Albrecht's measurements, a mispredicted
  `if (m_Dirty)` cost 23–24 cycles against 12 for the work it skipped: "using a dirty flag
  here is actually slower than not using one". His rule: "Don't test for exceptions – sort
  by them" (Albrecht, "Pitfalls of Object Oriented Programming", 2009). Ask whether every
  "skip if unchanged" test on a per-item path is cheaper than the work it skips.
- **Aliasing is the compiler's reason to re-read.** "Class members are virtually as bad as
  global variables"; "'const' doesn't help… const promises almost nothing!" Make local
  copies of members, and "Don't take the address of variables" (Ericson, "Memory
  Optimization", GDC 2003, which Acton's material builds on).
- **Zero is a valid default.** Reserve slot 0 as a dummy item, so that index 0 means "none"
  and a lookup needs no branch (Gray, "Defaulting to Zero").
- **Closed sets dispatch by switch or table.** Muratori measured virtual dispatch to a
  switch at 1.5×, to a table at 10×, and "nearly 15×" once a property is added ("'Clean'
  Code, Horrible Performance", 2023). A library's public hierarchy compiles "the vtable
  dispatch code" into the user's program, so keep swappable types for "the boundary of a
  library or plug-in system".
- **Handles, not pointers.** A system owns its memory, hands out only index handles, uses
  the spare bits for safety checks, and converts to a pointer only when needed, "and don't
  store the pointer anywhere" (Weissflog, "Handles are the better pointers", 2018).

## Review procedure

For a plan, decision, or diff, answer these in order and write the answers down.

### 1. Classify the path
Mark each piece of code as one of:
- **hot**: per frame, per call, or per element at runtime;
- **load**: once per file, level, or reload;
- **tool**: editor, checking, documentation, write-back, or development only;
- **offline**: the build-time bake.

The hard rules below apply to **hot** with full force, and to **load** for allocation and
memory layout. **Tool** code may be ordinary C++ inside. But if it ships in the same
library as the runtime, it still may not:
- add a dependency;
- require exceptions or RTTI;
- put containers, `std::string`, `std::function` or smart pointers into a public header,
  or break whatever header policy the project keeps;
- add work to a hot path, even when the tool is not in use.

Then triage, as the deck does: the value of each case is `p(call) * count`, in-game against
in-editor. Frykholm's calls-per-frame table (above) turns that into how hard to look.

Acton's critics mainly fault him for applying console-hot-path rules to all programs.
That is a point about other programs. Engine-core code faces the lens, and its tool code is
relaxed only inside these limits.

### 2. State the transform
- What is the input data, what is the output, how often does it run, and how much data
  goes through it?
- What are the common real values? For example: most calls have 1–3 arguments, most
  tuned values are floats, and most arrays have fewer than 20 elements. Is the design
  built around those, with the rare cases on a slow path?
- If the plan cannot answer these, that is the first finding.

### 3. Hot-path rules (check every item)
- **Allocation.** The hot path allocates nothing after warm-up, and a test proves it with
  a test-only counting allocator or a replaced global `operator new`, not by inspection.
  A new hot path adds that test.
- **Capacity boundaries.** An inline buffer ("8 on the stack, more from the heap") is a
  fast path with a cliff, and the allocation test cannot see the cliff unless it runs one
  past the capacity. For each fixed capacity in a change:
  - name it;
  - test N + 1 under the counting allocator;
  - say which allocator the spill uses (the owner's, never the process default).

  In the library this skill was calibrated on, an overload call spilled on its ninth
  argument, once per call, and no test passed nine (`recipes.md`).
- **Indirection.** Count the dependent loads (pointer chases) per operation. Each likely
  cache miss on a cold path costs about 100+ cycles. A `shared_ptr`, a `std::function`, a
  virtual call, or a per-element heap object on the hot path needs a measured reason.
- **Layout.**
  - Hot fields go together, and cold fields (docs, names, file and line, debug data) go
    elsewhere.
  - **Keys together, values elsewhere.** Any search (by tag, by symbol, by name) should scan
    a contiguous run of keys, not step through records or pointers to read one field
    each. This is the talk's dictionary example. Check every search mode: a linear scan
    can read a key run while a binary search beside it still chases a pointer per probe.
  - Look for padding and stray `bool`s in structs that are iterated. The deck's phrase for
    a `bool` in a struct is "Extremely low information density" (slides 120–122). It
    measures it as signal against noise: bits of information used per frame, divided by
    cache-line bytes loaded per frame (slides 147–148). Do the same arithmetic for any hot
    struct under review, or measure it by compressing a dump of the field (`recipes.md`,
    Information density).
  - Dump the layout rather than reading the declaration. `-Wpadded` and
    `-fdump-record-layouts` show which cache line each hot field is on (`recipes.md`,
    Layout).
  - Prefer index-addressed flat arrays (a table indexed by an interned name's dense
    index) to maps.
  - New per-element structs on a hot path get a `static_assert(sizeof(...))`.
- **One versus many.** Does the API force one call per item? If the natural caller is
  "every object", "every value", or "every task", there should be a batch form taking a
  span. The single-item form is then a thin wrapper.
- **Branches on the common path.** A feature that is off should cost at most one
  predictable branch. Check that with the benchmark, not by reasoning.
- **No last-minute decisions.** A per-item check of something the caller already knows
  (a state flag, "has an override?", "is this the root?") should be decided once,
  outside the loop, with the cases split.
- **Strings.** None on a hot path. Names are interned or hashed at load or at build time.
  A `std::string` built during a call is a finding, except on the error path.
- **Exceptions and RTTI.** None in runtime code. Every fallible API returns an
  `expected`-style value. A `throw`, a `try`, a `typeid`, or a `dynamic_cast` in a change
  is a finding. (On one library, a `try` alone measured about 10 ns per call.) A rule that
  no compiler flag enforces is a wish: the build passes `-fno-exceptions` and `-fno-rtti`
  (`/EHs-c-` and `/GR-`). A library that needs no RTTI but never builds without it has
  never been compiled the way a game with RTTI off compiles it.
- **Atomics.**
  - Every operation names its memory order. A bare `fetch_add` is `seq_cst`, which is a
    full fence on ARM.
  - Every `acquire` names, in a comment, the `release` it pairs with.
  - x86 makes every load an acquire, so tests on x86 cannot catch a missing one.
    Switch-class and Apple hardware can. TSan cannot check a protocol built on
    `atomic_thread_fence`, so a seqlock needs a stress test on ARM (`recipes.md`,
    Threads).
  - Two threads writing in one cache line serialize. The line is 64 bytes on x86 and 128
    on Apple silicon.
- **Atomics stop the optimizer.** A compiler never hoists an atomic load. So an atomic
  tuned value read inside a per-entity loop is read every iteration, and the loop does not
  vectorize. The fast pattern is to read once per frame into a local. Docs and examples
  should show that pattern, and an API that returns many values should return a
  snapshot.
- **Thread-locals and fibers.** Engines with fiber job systems switch fibers inside a job,
  and can resume one on another thread. A library's `thread_local` "current context" is
  wrong after a switch inside a callback, and compilers cache TLS addresses across calls
  (MSVC's `/GT` exists for this). A new `thread_local` states what happens under a fiber
  switch.
- **Floating point is per compiler.** GCC and Clang fuse `a * b + c` into an FMA when the
  target has one. MSVC does not, by default. AArch64 always has FMA. A result that must
  match across platforms (curve samples, baked values, replays, lockstep, golden tests)
  needs contraction off in the library's own build (`-ffp-contract=off`, `/fp:precise`),
  or a test of its exact bits on every compiler in CI.
- **Static initialization.** A namespace-scope object is `constinit`, or it runs at startup
  in an order nobody chose. A function-local `static` with a non-trivial type costs a guard
  check on every call. `nm | grep _GLOBAL__sub_I` and Clang's `-Wglobal-constructors
  -Wexit-time-destructors` list the exceptions.
- **Code size.** Count the bytes each instantiation adds, not only its speed. The
  instruction cache is shared with the game. A thunk per binding, and a registration path
  inlined at every call site, push the hot loop out of it, so registration and other code
  that runs once belongs out of line. Luau keeps its interpreter's core loop near 16 KB,
  half of L1i. A calibration figure: about 850 bytes of code per bound function at `-O2`,
  on one library (`recipes.md`).
- **Debug builds are a target.** A game is played, and tested, in its debug build.
  - A layer of small inline functions, or an algorithm from `<algorithm>` with a lambda
    per element, is free at `-O2` and costs at `-O0`.
  - For a change to a hot path, give the debug row next to the release row.
  - Calibration (GCC `-O0` against release, on one library): calls 11× slower; a
    `std::partition_point` search 17×, and slower than a linear scan even at 1,000
    elements; `-Og` 2.5×.
  - MSVC's debug build differs again: `__forceinline` is ignored at `/Ob0`, and `/JMC`,
    `/RTC1` and iterator debugging each add their own cost (`recipes.md`).
- **Templates.** Acton's stated costs are compile time and slower code, and his stated
  alternative is generated code. A template that removes a per-call decision (for example,
  deducing a bound function's marshalling at compile time) is a real use. Still ask:
  - what does each template cost in compile time, in code size per instantiation, and in
    debug-build speed;
  - would a small code generator do it better?

  A template that only buys "genericity" for one real use case is a finding.
- **Build time is a benchmark.** A game pays a public header's cost once per including
  file.
  - Calibration (Clang 18, C++20): a header that reaches only `<string_view>`, `<span>`
    and `<optional>` costs about 0.2 s per translation unit, against 0.22 s for an empty
    file. `<string_view>` alone is 18,600 preprocessed lines in C++20 (12,400 in C++17).
  - So a new standard include in a public header is a cost to measure, not a detail.
  - A forward-declaring header is the usual remedy for code that only names the types.
- **Shipping.** Development-only work compiles out under the project's shipping switch,
  and a CI job builds that configuration.
- **Removability.** For any technology a change leans on, ask whether a game could turn it
  off or replace it later, without rewriting callers. Ask before it spreads, because it
  cannot be asked after. The usual answers:
  - no exceptions and no RTTI;
  - an allocator per owner, and an arena per load;
  - a switch for the container library;
  - a switch for atomic reference counts;
  - a shipping switch.

  A new dependency on the global heap, the STL in a public signature, or a mandatory
  callback type is a finding unless it comes with its switch or its funnel.
- **Dependencies.** None at run time. Test frameworks are for tests only. Proposing a
  third-party runtime library, however good, is a finding by default.

### 4. Load-path and offline rules
- Anything computable at build time belongs in the baked data: macro expansion, name
  interning, sorting.
- **A resolved reference needs its invalidation rule.** Resolving a name at bake or load
  time (a function pointer kept in a node, a table, or a cache) is the obvious speedup, and
  it is Lua's classic reload bug: `local f = mod.f` keeps calling the old function after a
  reload. Luau's load-time imports need a de-optimization for the same reason. A proposal
  that caches a resolved name says:
  - how each way of changing the name invalidates it: redefinition, override, removing the
    layer or module that defined it, and hot reload;
  - what the cache saves, against an index lookup that costs about 2 ns.
- **No caches written into the data.** Inline caching and quickening, as GDScript and CPython
  3.11 do them, write into the code they run. Data that is shared, loaded in place
  (possibly read-only), written back by tools, or reloaded is never a cache. A per-call
  cache goes in a side table that its owner can invalidate.
- **Untrusted and long-running content.** Mod files and live-ops data, which are reloaded
  all session, need bounds that a level designer's data does not:
  - **work:** an execution budget, or an interrupt the host can set from another thread.
    Luau checks one host-set pointer at calls and loop back-edges. A step count is
    deterministic, which replays and headless simulation need; a wall-clock check is not;
  - **memory:** a cap that fails with an error, not an abort;
  - **growth:** interned names that never die, and old versions of reloaded code kept
    alive. Erlang keeps at most two versions of a module;
  - **collisions:** an unseeded hash probed by `hash & mask`, which a hostile name set can
    force into long chains.

  Ask what an adversarial file, or a file reloaded 10,000 times, does to each one. Never
  load binary caches from an untrusted source: they can point anywhere.
- **Shipping carries only what shipping reads.** Descriptions, docs, groups, parameter
  names, source paths and type names are tool data. Each one compiled into a shipping
  binary costs size, and can tell a dataminer what the code does. Check with `strings` on
  a shipping build (`recipes.md`).
- **The C library is a platform.** `printf("%g")`, `strtod` and `localeconv` follow
  `LC_NUMERIC`, which a host can set to a comma-decimal locale without asking (Qt does).
  Data text and JSON go through locale-independent code (`std::to_chars`, fast_float),
  never through the bare C functions. Float text must round-trip the same on every
  target, or a baked file differs by platform.
- Is the runtime load a single read plus fixups, with the allocation count fixed? Check
  this on the path a console takes, through the host's reader (packs, asynchronous I/O,
  decompression into a buffer it chooses), not only through `fopen`. The host should be
  able to read straight into the library's arena.
- **Memory ownership.** Who owns each allocation, from which heap, and when is it freed?
  Every owner takes an allocator and passes it to the tables it keeps. A load builds in one
  arena. A new allocation goes through its owner's allocator, never global `operator new`,
  and a test fails otherwise.
- **Memory a host can budget.** A TD's memory report lists bytes by system, against a
  budget, live and at peak. Can a host tell the library's name table from its load arenas,
  script frames and tuned values? Can it get an error, not an abort, when a budget runs
  out?
- **Fatal errors go to the host.** An engine routes every fatal path to its own crash
  reporter, with a message and, where there is one, the script stack. A new `abort()`
  without a host hook is a finding.
- **Iteration speed is a requirement, not a nicety.** Insomniac's engine group built
  designers' tools themselves. Fast iteration (hot reload, live tweaks) is often why a data
  layer exists at all. A data-oriented review must not trade away designer iteration time
  without saying so. Weigh the frame-time cost against the iteration-time cost explicitly.

### 5. Evidence
- Every performance claim names a benchmark row and a before/after number from the same
  machine.
- "Should be fast" is not evidence.
- A regression on a stated target blocks the change until it is fixed or a decision is
  recorded.
- **Best-of-N is a regression gate, not a frame budget.** A best-of-N row is the cost of a
  warmed path, and a player feels the worst frame. For work that runs inside a frame and
  is not constant-time, also give:
  - the first call in a fresh process;
  - the largest single instance: the biggest reload, a mass wake of tasks, a level's
    worth of loading;
  - the 99th percentile and the maximum over at least 100 runs;
  - the page faults it takes;
  - that worst case as a share of the frame (1 ms is 6% of a 16.6 ms frame).

  Work that can exceed its share needs a way to split it across frames, or to move it off
  the game's thread.
- **Read the code the compiler made** for any claim about it (`recipes.md`, Codegen).
- **Say what was not measured.** MSVC, `perf` counters and ARM hardware are often not at
  hand. A finding that depends on one says so, rather than estimating.

### 6. Report
Write findings as a short list, most costly first. Each finding names:
- the path class;
- the cost in cycles, allocations, bytes, or iteration time;
- the evidence or the missing evidence;
- a concrete change, or "record a decision".

Accepted deviations go into the project's decision log: deviating is fine, deviating
silently is not. Do not rewrite code to follow this lens without a finding that names its
cost.

## Applying it to common engine-core subsystems

| Area | The question a data-oriented TD asks |
|---|---|
| Parser, preprocessor | Is this a load-time transform whose output is shaped for the runtime's reads, and could the bake do it instead? |
| Trees and tables of data | Fixed-size elements, contiguous children, one arena and one count per load, keys beside the elements. Does a change keep arrays in their load's arena? |
| Lookup | Is a linear scan right for the real sizes (mostly under 20)? Is there a sorted or hashed mode for the rest, used where the data is big, and does every mode read a contiguous key run? |
| Interned names | One table, lock-free to read, built at load or bake. Does a change intern on a hot path, or let content grow the table without a bound? |
| Tuned values (tweaks, config) | Is a read one load? Is it read once per frame, not inside a per-entity loop? Does a value carry only what shipping reads? Are values updated in a batch on reload, at a point the host chooses (a frame boundary), never mid-evaluation? |
| Embedded scripting | No temporary argument lists and no allocation per call. Count the loads on the dispatch path. Leaf builtins that cannot recurse may skip frame bookkeeping. Is there a work budget for untrusted code? |
| Objects and handles | Salted handles into a flat table, not pointers. Do per-object side tables belong on a hot path? |
| Messages and events | "Where there is one there are many": a broadcast over a span, one lookup per run of a type, nothing built per message. Callbacks give "little control over when", reentrancy bugs and instruction-cache misses (Frykholm), so prefer queues that are polled. |
| Tasks, jobs, coroutines | Frames from a pool with a budget; structural changes queued to a named sync point; nothing that breaks under a fiber switch. |
| Binary caches and asset loading | Load in place: one read into the right memory, offsets or one fixup pass, a fixed allocation count, the expensive work done at bake time. Delta-patch friendly, never trusted from outside. |
| Statistics and profiling | Counters beside the record they count, no clock read unless asked, compiled out in shipping. A native profiler sees all script time as one function, and a sampler at 8 kHz cannot see a 30 ns call. Can the host give each script function its own profiler zone (Tracy, PIX), with a stable identity? |
| Tooling | Tool path: ordinary C++ is fine. Judge it on iteration time and clarity. |
| The shipping build | What does `strings` find that only tools read? What does `nm` find that runs before `main`? What does `size` say a minimal game links? |
| Mods and live-ops content | Can a file stop the frame, exhaust memory, or grow a table forever? |

## Where the school disagrees (use judgment)

- **Templates and generic code.** Insomniac had no hard rule against templates, only
  strong suspicion: "occasionally a good use", usually "a poor use", and "cripples compile
  times".
  - Accept templates where they move work to compile time, and both the benchmarks and
    the build times hold.
  - Flag them where they only add generality.
  - Consider a generator for anything bulky.
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
  - So judge an interpreter on whether data stays out of the per-entity loop and gets
    baked, not on whether there is an interpreter at all.
  - A second researcher's unquoted claim that he preferred compiled graphs to
    interpreted bytecode is consistent with this, but is still not citable.
  - Insomniac's engine team listed "Visual scripting" among its development tools
    (slide 5).
  - Peers in the same school shipped interpreters or Lisps:
    - Bitsquid shipped Lua with hot reload.
    - Naughty Dog built GOOL and GOAL, custom Lisps, for its PlayStation games.
    - On PS3, Naughty Dog built DC, "a custom data scripting system developed in Racket",
      which compiled data offline for the C++ runtime (Liebgold, RacketCon 2013).
  - A tree walker is slow when its nodes are scattered: children that "could be
    *anywhere*", headers on every node, and a virtual visit per node. Crafting Interpreters
    measures about 72 s for jlox's `fib(40)` against half a second in C. Small nodes,
    inline and contiguous in one arena, remove that cause. Per-node heap objects or a
    virtual `eval` per node bring it back.
  - A JIT is not available on consoles or iOS. Luau is fast as an interpreter for that
    reason, and Wren benchmarks against LuaJIT with the JIT off. A proposal for runtime
    code generation names its platforms and its fallback.
  - The data-oriented question is not "script or not", but whether the interpreter is
    kept off the per-entity hot loop and whether its data is baked.
- **The school's own stop rules.** These come from inside the school, so cite them before
  pushing a change further:
  - Gray's default is an array of structures, "with 'holes' and permanent pointers", and
    struct-of-arrays only "for the cases where you need really fast number crunching".
  - Frykholm: 1–10 calls a frame "doesn't matter", and complexity only for about 2×.
  - Albrecht: "OO is not necessarily EVIL", and prefetching can thrash other threads'
    caches.
  - Kelley: integer indexes lose type safety, so give them distinct types.
  - Frykholm rejected generating hash constants in the build as messier, for the build and
    the debugger, than a debug-asserted literal. "Generate it" is not free either.
- **Scope.** Critics (Shamus Young, HN and gamedev.net practitioners) accept the
  performance argument but reject applying it to every program. That is fair for business
  software and editors. It does not soften the lens for code in an engine's core:
  - runtime and load paths face the full lens;
  - tool paths face the cost-of-change lens, within the limits in review step 1.
