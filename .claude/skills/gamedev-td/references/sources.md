# Sources for the gamedev-td skill

Gathered 2026-10-11 in two passes.
1. **Web search from this repository's sandbox.** Its network policy blocks fetching pages,
   so this pass saw search-result excerpts only.
2. **A second research agent with page access, at the project owner's request.** It
   supplied exact quotes and slide numbers.
3. **The deck itself, read in the sandbox** (2026-10-11). The PDF that pass 2 cited is
   reachable through GitHub, though not through `neil3d.github.io`:
   <https://raw.githubusercontent.com/neil3d/neil3d.github.io/master/assets/img/ecs/DOD-Cpp.pdf>
   (201 pages). Its text was extracted with `pdftotext`, and slide 24 was rendered and
   inspected. The PDF is not committed: its license is unknown.

Every CppCon 2014 slide quote and slide number in the skill is checked against that PDF.
Pass 2's slide numbers were one too high throughout (it gave 30, 34, 41, and 120 where the
deck has 29, 33, 40, and 119). Other exports, such as the SlideShare mirror at
<https://www.slideshare.net/slideshow/data-oriented-design-and-c/39109965>, may number them
differently again.

## Acton's own material

- **"Data-Oriented Design and C++", CppCon 2014.**
  - Abstract, high confidence:
    <https://isocpp.org/blog/2015/01/cppcon-2014-data-oriented-design-and-c-mike-acton>.
    It says "the transformation of data is the only purpose of any program", and that
    common C++ approaches work against that goal in console game development, including
    limits inherent in C++ compilers.
  - The slides, **verified against the deck** (page numbers):
    - 5: the engine team's development tools include "Visual scripting".
    - 18–27, "How are games like the Mars rovers?":
      - 24 lists "Exceptions • Templates • Iostream • Multiple inheritance • Operator
        overloading • RTTI", with no "no" and no "limited" on the slide;
      - 25–27 add "No STL", "Custom allocators (lots)", and "Custom debugging tools".

      That the six on slide 24 are avoided is the talk's spoken framing. Pass 2's claim
      that templates were "limited, not banned" is not in the deck.
    - 29: "The purpose of all programs, and all parts of those programs, is to transform
      data from one form to another."
    - 30: "If you don't understand the data you don't understand the problem."
    - 33: "If you have different data, you have a different problem."
    - 34: "If you don't understand the cost of solving the problem, you don't understand
      the problem."
    - 35: "If you don't understand the hardware, you can't reason about the cost of
      solving the problem."
    - 36: "Everything is a data problem. Including usability, maintenance, debug-ability,
      etc. Everything."
    - 37: "Solving problems you probably don't have creates more problems you definitely
      do."
    - 38–39: "Latency and throughput are only the same in sequential systems."
    - 40: "Rule of thumb: Where there is one, there are many. Try looking on the time
      axis."
    - 41: "Rule of thumb: The more context you have, the better you can make the
      solution. Don't throw away data you need."
    - 42: "Rule of thumb: NUMA extends to I/O and pre-built data all the way back through
      time to original source creation."
    - 43: "Software does not run in a magic fairy aether powered by the fevered dreams of
      CS PhDs."
    - 45–46: data-oriented design as "a response to the culture of C++ … and The Three Big
      Lies it has engendered".
    - 117: "Using cache line to capacity* = 10x speedup".
    - 118–119: "Code is maintainable", "Code is debugable", "Can REASON about cost of
      change"; "Ignoring inconvenient facts is not engineering; It's dogma."
    - 120–122 and later: "bools in structs… (3) Extremely low information density". The
      deck works the arithmetic at 147–148: cache lines loaded per frame against bits of
      information used.
    - 158–184: a review of OGRE's `OgreNode.cpp`. Page 160 links Matias Goldberg's reply,
      <http://yosoygames.com.ar/wp/2013/11/on-mike-actons-review-of-ogrenode-cpp/>.
      Findings (1) memory that cannot be rearranged (ABI, padding); (2) bools and
      last-minute decisions; (5) over-generalization, including "Unmanaged icache (i.e.
      virtuals)"; (6) under-defined constraints ("Strings, generally. Filenames, in
      particular."); (7) over-solving. Then "Step 1: organize… Step 2: triage… p(call) *
      count… Step 3: reduce waste" (181–183), and "~200 cycles x 2 x count" (184).
      Checked against the deck on 2026-10-30 (pages 158–184 read with `pdftotext`;
      findings 3 and 4 are images).
    - 195–198: "Most problems are easy to see"; solving the 90% well lets the compiler
      solve the 10% better; "Organized data makes maintenance, debugging and concurrency
      much easier"; "Good programming is hard. Bad programming is easy."
- **Talk transcript** (CppCon 2014, an automatic transcript the project owner supplied in
  the session, 2026-10-11; not committed). It is garbled in places, with repeated and
  dropped lines, so quote only clean passages. Highest confidence: the words are his,
  though the transcription may slip. What it settles:
  - **The framing of slide 24.** Exceptions: "we avoid them. We turn them off if we can
    at all help it." Templates: "we don't have a hard rule that says no templates…
    occasionally a good use, but most of the time… a poor use… it slows things down",
    and "cripples compile times". Iostream: standard out yes, C++ iostream no. Multiple
    inheritance: "that's right out." Operator overloading: no strict rule, "the frowny
    face". RTTI, "No STL", and "custom allocators" are garbled, so there is no gloss.
  - **The Q&A on templates.** "A template is just a poor man's text process[or]"; generate
    the code instead. Code duplication is "almost certainly not as big a problem as
    people think it is."
  - **The Q&A on portability.** "General portability is a fool's errand": know the finite
    platform range (minimum, maximum, average, common case).
  - **Priorities:** hard deadlines; soft real time at 16 or 33 ms; usability of tools
    for non-programmers; performance.
  - **The dictionary example** (lie 2): store keys together and look values up by index;
    "solve for the most common case first… and not the most generic."
  - **Cache costs** credited to Jason Gregory: L1 about 3 cycles, L2 about 20, RAM 200+.
  - **Strings:** "Strings are generally bad"; "the best code is code that doesn't need to
    exist at all"; precompute string hashes; push work back in time; generate code for
    statically knowable results.
  - **Last-minute decision making** (the node-translate example): split the states so the
    caller, who knows the case, picks the path. Estimate on the back of an envelope to
    the right order of magnitude, then measure.
  - **"Compilers are not magic"**: remove ghost reads and writes, and hoist invariants.
  - **Language:** he prefers C99. C++ is used because it is culturally dominant, and a
    feature only if it pays for itself. C++11 had no tangible performance effect for them.
  - **The closing design-patterns quote is Christer Ericson's** (the transcript spells it
    "Christa Erickson"), not Acton's.
  - **No mention of scripting or interpreters**, in the talk or the Q&A.
- **"Three Big Lies".** High confidence. "Software is a platform." "Code should be
  designed around a model of the world." "Code is more important than data."
  - The 2008 article: <https://cellperformance.beyond3d.com/articles/2008/03/three-big-lies.html>.
  - The GDC talk "Three Big Lies: Typical Design Failures in Game Programming" has two
    Vault ids in circulation: <https://gdcvault.com/play/1012200/Three-Big-Lies-Typical-Design>
    and <https://gdcvault.com/play/1012960/Three-Big-Lies-Typical-Design>.
- **"Everyone Watching This Is Fired", GDC 2019.**
  <https://gdcvault.com/play/1025778/Everyone-Watching-This-Is>.
  Video: <https://www.youtube.com/watch?v=cV5HArLYajE>. Pass 2 quoted the items the
  skill uses verbatim from the slides: articulating the problem, why it matters and what
  it is worth; a plan B (and "I have already implemented my Plan B..."); the (various)
  latency requirements; the most common real values; and the acceptable ranges. High
  confidence on the wording. The fan write-ups' numbering and item merges are unreliable:
  - Adam Johnson: <https://adamj.eu/tech/2022/06/17/mike-actons-expectations-of-professional-software-engineers/>;
  - Jackson Dunstan: <https://www.jacksondunstan.com/articles/5137>;
  - Lobsters: <https://lobste.rs/s/8cb0fy/everyone_watching_this_is_fired>.

- **MIT 6.189 lecture, Insomniac (2007).** <https://ocw.mit.edu/courses/6-189-multicore-programming-primer-january-iap-2007/0cbeedf18c91ac6c0b75d21194f1d5fd_lec16games.pdf>.
  The engine team's priorities: hard deadlines, soft realtime (33 ms), usability, and
  performance. Most CPU time is in engine code.
- **"Proportional to surprise."** "The actual global energy that you use to make a
  transformation should be proportional to the amount of surprise in the data." High
  confidence: it is in the March 2018 Unity talk transcript below, in the Q&A, with the
  UI frame-to-frame example. That settles the venue as March 2018 (the GDC season), not
  Unite LA 2018 as Schöner's notes were read to say:
  <https://blog.s-schoener.com/2019-06-09-data-oriented-design/>. Unity's DOTS course
  paraphrases it:
  <https://learn.unity.com/course/dots-best-practices/tutorial/part-2-data-design>.
- **March 2018 Unity talk on democratizing data-oriented design** (an automatic
  transcript the project owner supplied in the session, 2026-10-11; not committed; garbled
  in places). High confidence on clean passages. What it gives:
  - performance by default and "optimizable by default": the first instinct should be "a
    good first level approximation of the right answer";
  - "not complicated rules, but… hard rules": declared reads and writes, small data in
    streams, value types, no garbage collection;
  - nothing opaque: component memory in 64K chunks "none of which is opaque", and an
    inspector for the compiler's output;
  - "an entity is nothing more than a lookup index or an ID… a database key";
  - named barriers, minimal sync points, and structural changes queued and flushed at
    barriers for determinism;
  - "you can't get away with not having no cache misses ever";
  - its hash map keeps an array of hashes apart from the array of values (keys together
    again);
  - on simplicity versus familiarity: "removed things other than the thing that actually
    solves the problem";
  - on mobile power: finish early "and then let the CPU rest";
  - the "two orders of magnitude" claim for the high-performance C# subset over "C#
    scripts". This is the nearest he comes, in any source here, to a position on
    scripting languages, and it is not an opinion on interpreters as such.

## The same school, not Acton

- Noel Llopis, "Delicious Data Baking":
  <https://www.gamedeveloper.com/programming/delicious-data-baking>. "Don't do anything at
  runtime that you can do offline."
- "Fast File Loading (Pt. 2)": <https://www.gamedeveloper.com/programming/fast-file-loading-pt-2->.
  Load in place, then relocate pointers.
- Insomniac, "Loading Based on Imperfect Data", GDC 2013:
  <https://gdcvault.com/play/1017878/Loading-Based-on-Imperfect>. Data loading with fast
  tool iteration, from Acton's time as engine director.
- Niklas Frykholm (Bitsquid), on live editing and data-driven engines:
  <https://www.gwern.net/doc/cs/algorithm/2012-frykholm.pdf>. A data-oriented engine with
  Lua and reload instead of reboot.
- Stoyan Nikolov, "OOP Is Dead, Long Live Data-oriented Design", CppCon 2018:
  <https://isocpp.org/blog/2019/08/cppcon-2018-oop-is-dead-long-live-data-oriented-design-stoyan-nikolov>.

## Reception and counterpoints

- Qt mailing list recap: the talk "REALLY made a lot of the audience uncomfortable".
  <https://lists.qt-project.org/pipermail/development/2014-September/018374.html>
- D forum threads on writing data-oriented code by hand without help from the language:
  <https://digitalmars.com/d/archives/digitalmars/D/Mike_Acton_at_CppCon_prominent_game-dev_lecture_to_non-game-dev_s_244652.html>
- Shamus Young, "The Schism": credits the performance argument but faults "domain tunnel
  vision". <https://www.shamusyoung.com/twentysidedtale/?p=48554>
- Hacker News: data-oriented design is orthogonal to OOP, not only ECS.
  <https://brianlovin.com/hn/24506744>
- Games From Within (Llopis): the critique targets OOP on modern memory hierarchies, not
  OOP in general. <https://gamesfromwithin.com/category/data-oriented-design/>
- ACCU Overload 167, "Revisiting Data-Oriented Design" (Teodorescu, 2022):
  <https://accu.org/journals/overload/30/167/teodorescu>
- Virtual call cost, measured: about 18% for short functions and under 1% for long ones.
  The larger cost is heap locality. <https://johnnysswlab.com/?p=1247>

## Reported without quotes (do not cite as Acton's words)

Pass 2's section B gave positions without quotes, attached to sources that do not contain
them as cited (for example, the deck's slide 120 for the "Typical C++ Bullshit" review).
They agree with the rest of the record, but the skill does not attribute them:
- **Scripting.** Interpreted scripting on runtime hot paths is costly. He preferred
  compiled visual graphs to bytecode in the DOTS era.
- **"Typical C++ Bullshit."** Its critiques of virtual calls, heap allocation, and
  getter/setter abstraction. The annotated photos are a 2008 SmugMug album,
  `macton.smugmug.com/Other/2008-07-15-by-Eye-Fi/n-xmKDH` (linked from
  `github.com/dbartolini/data-oriented-design`); they could not be read from the sandbox.
  *Correction, 2026-10-30:* an earlier version of this file said the deck's worked review
  covers only `bool`s and information density. Pages 158–184 also review constructors,
  destructors and virtuals (see the deck entry above).
- **Insomniac, GDC 2013.** Live editing on in-place binary buffers rather than text
  parsing.
- **Allocators.** No global heap during gameplay; frame, per-system, and fixed-capacity
  allocators; single owners and handles. The "complete prohibition" wording is stronger
  than anything quoted.
- **Templates.** Limited for compile time, code size, and debug-build speed.
- **Criticism.** "Engine developers such as Vittorio Romeo" calling strict data-oriented
  design brittle. No source was given, so it is excluded.
- **Coroutines.** Console engines override coroutine frame allocation or use fibers. This
  is plausible and matches `baker-gaps.md` item 7, but no source was given.

## Peer practice, from general knowledge (not checked this session)

- Naughty Dog built GOOL (Crash Bandicoot) and GOAL (Jak and Daxter), custom Lisps for the
  PlayStation. Pass 2 confirms this. Its later games kept a Lisp-family data language, now
  sourced (below, 2026-10-30): DC, in Racket.

## The 2026-10-30 research pass

Three research agents, with web search and GitHub access only (most other hosts are
blocked from the sandbox), plus experiments in the repository (`recipes.md`).
- **VP** means the text was read from the source or a GitHub-hosted copy.
- **SE** means a search excerpt only.

Quotes in SKILL.md come only from VP sources. The CppCon 2014 deck's pages 158–184 were
re-read by this session itself. Third-party PDFs and decks were downloaded to the scratchpad
only, never committed: their licenses are unknown.

**Acton and the school**
- Acton, **"How to Write Code the Compiler Can Actually Optimize"**, GDC 2014 Code Clinic
  (GDC Vault 1020472). The pptx is at
  <https://github.com/Michaelangel007/write_code_compiler_can_optimize> (VP). Slides 66–73
  cover ghost reads and writes and hoisting, 76–80 the summary rules, 84–91 information
  density by compression, and 3 "Our role is not to write 'good' code". The GDC 2015
  follow-up is Vault 1021866 (SE only).
- Tony Albrecht, **"Pitfalls of Object Oriented Programming"**, SCEE, GCAP 2009. A Git LFS
  copy is in `Michaelangel007/game_dev_pdfs`, `c++/` (VP): the dirty flag (pp. 29–31),
  19.6 → 12.9 → 4.8 → 3.3 ms (pp. 62, 98, 101), "Don't test for exceptions – sort by
  them" (p. 111), "OO is not necessarily EVIL" (p. 102).
- Christer Ericson, **"Memory Optimization"**, GDC 2003. In the same repository, `memory/`
  (VP): the 3 R's (p. 12), hot/cold splitting (p. 23), aliasing and `const` (pp. 42–58).
- Ulrich Drepper, **"What Every Programmer Should Know About Memory"**, 2007. In the same
  repository (VP): `pahole` (§6.2), the critical word first, false sharing (§6.4).
- Andrew Kelley, **"A Practical Guide to Applying Data-Oriented Design"**, Handmade Seattle
  2021, <https://vimeo.com/649009599>. VP from an auto-caption transcript, which is garbled,
  so only clean phrases are quoted.
- Niklas Frykholm, **Bitsquid blog**, <https://github.com/niklasfrykholm/blog> (VP): "The
  Blob and I" (2010); "A Pragmatic Approach to Performance" (2011); "Strings Redux"
  (2011); "Lightweight Lua Bindings" (2011); "Sensible Error Handling" (2012); "A Simpler
  Design for Asynchronous APIs" (2012); "Allocation Adventures" (2015).
- Niklas Gray and Tobias Persson, **Our Machinery blog**, archived at
  <https://github.com/ruby0x1/machinery_blog_archive> (VP): "Data Structures Part 1: Bulk
  Data", "Defaulting to Zero", "Physical Design", "Minimalist container library in C",
  "Dig Shallow Graves", "Step by Step Programming", "It's all about the data".
- Casey Muratori, **"'Clean' Code, Horrible Performance"**, 2023. VP from a mirror
  (`KraXen72/tech-support-wiki`) and from his Q&A with Robert Martin
  (`cmuratori/misc`, `cleancodeqa.md`). `computerenhance.com` itself is blocked here.
- Andre Weissflog, **"Handles are the better pointers"**, 2018, `floooh/floooh.github.io`
  (VP).
- Dan Liebgold, **"Racket on the Playstation 3? It's Not What you Think!"**, RacketCon 2013
  keynote. The abstract is in `racket/racket-lang-org`, `rcon/2013/all.rkt` (VP): "DC, a
  custom data scripting system developed in Racket at Naughty Dog". Its slides say syntax
  was "the prime enabler of rapid DSL development, but also a source of much inefficiency
  and confusion" (SE). The video is YouTube `oSmqbnhHp1c`.
- Richard Fabian, *Data-Oriented Design* (2018). The contents ("Existence Based
  Processing", "Don't use booleans") are VP, the text SE.
- Vittorio Romeo, "More Speed & Simplicity: Practical Data-Oriented Design in C++", CppCon
  2025 keynote (SE, abstract only). It presents data-oriented design as a complement to
  object orientation. This is a citable source for the critique that "Reported without
  quotes" excludes.
- **Acton since 2022:** reported as having left Unity (SE, dates unconfirmed). No public
  talk or post from 2024–2026 was found.

**Interpreters**
- Luau (Roblox), <https://github.com/luau-lang/site> and <https://github.com/luau-lang/luau>
  (VP):
  - `performance.md`: fastcalls without a frame, the core loop in about 16 KB, imports,
    the native `vector`, GC pacing;
  - `sandbox.md`: the interrupt, read-only builtins, no bytecode loading;
  - `profile.md`: the 10 kHz sampler;
  - `VM/src/lvmexecute.cpp`: where `VM_INTERRUPT()` sits.
- Wasmtime, `docs/examples-interrupting-wasm.md` (VP): fuel is deterministic; epochs cost
  about 10%.
- Wren, `doc/site/performance.markdown` (VP): computed goto 5–10%, copy-down inheritance,
  benchmarks with LuaJIT's JIT off.
- Crafting Interpreters, `book/chunks-of-bytecode.md` and `book/optimization.md` (VP).
- Godot, `modules/gdscript/gdscript_vm.cpp` (VP), and the typed-instructions progress
  report in `godot-website` (VP).
- PEP 659 (VP). Rohou, Swamy and Seznec, "Branch prediction and the performance of
  interpreters: Don't trust folklore", CGO 2015 (SE).
- Lua 5.4 manual (`lua_Alloc`), `lstate.c` and `lstring.c` (VP). Erlang's
  `code_loading.md` (VP): two versions of a module at most.

**Engine practice and tools**
- Tracy's manual, `wolfpld/tracy`, `manual/tracy.tex` (VP).
- ClangBuildAnalyzer and `vcperf` READMEs (VP); Bloaty and SizeBench READMEs (VP).
- Chromium, `docs/static_initializers.md` (VP).
- Microsoft's C++ documentation, `MicrosoftDocs/cpp-docs` (VP): `fp_contract`'s default
  (off since VS 2022 17.0), `/Ob`, `/JMC`, `[[msvc::intrinsic]]`, `_ITERATOR_DEBUG_LEVEL`,
  `/GT`, `/Zc:tlsGuards`. Also `microsoft/STL`'s `yvals.h` for `detect_mismatch` (VP).
- LLVM PR #166542, on TSan and `atomic_thread_fence` (VP).
- GitHub changelog, arm64 runners for public repositories, 2025-08-07 (VP).
- EASTL's `allocator.h` (VP). Unreal's Low-Level Memory Tracker (SE).
- DirectStorage's `DeveloperGuidance.md` (SE). Android's frame-rate guidance, P99 as the
  stutter indicator (SE).
- Frontier, "Cross Platform Determinism", GDC 2024 (SE). Overwatch, "Gameplay Architecture
  and Netcode", GDC 2017 (SE). Gyrling, "Parallelizing the Naughty Dog Engine Using
  Fibers", GDC 2015 (SE: the abstract only).
- Autodesk Stingray documentation: "Consoles and iOS do not allow just-in-time
  compilation" (SE).

**Not found this pass:** Acton's CellPerformance articles on aliasing and `restrict`; the
text of the GDC 2015 Code Clinic; Ryan Fleury's arena material; Reinalter's allocator
series; Fredriksson's "SIMD at Insomniac Games"; anything public on Frostbite's or
Insomniac's script runtimes.

## Not found

- A quoted statement of Acton's view on embedded scripting languages or interpreters.
  It is not in the CppCon 2014 talk or its Q&A. The March 2018 Unity talk gives only the
  restricted, compiled C# subset (see above).
- The original "Typical C++ Bullshit" images.
