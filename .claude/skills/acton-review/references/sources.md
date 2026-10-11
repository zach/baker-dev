# Sources for the acton-review skill

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
- **"Proportional to surprise."** Spoken: "The actual global energy that you use to make
  a transformation should be proportional to the amount of surprise in the data." Pass 2
  traced it through Sebastian Schöner's notes,
  <https://blog.s-schoener.com/2019-06-09-data-oriented-design/>, to a 2018 keynote, given
  either as Unite LA 2018 or GDC 2018. Medium-high confidence on the words, uncertain on
  the venue. Unity's DOTS course paraphrases it:
  <https://learn.unity.com/course/dots-best-practices/tutorial/part-2-data-design>.

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
- **"Typical C++ Bullshit" (around 2014).** Its critiques of virtual calls, heap
  allocation, and getter/setter abstraction. The original annotated images were not
  found. The deck's own worked review (around slides 120–150) is about `bool`s,
  last-minute decision making, and information density, not those topics.
- **Insomniac, GDC 2013.** Live editing on in-place binary buffers rather than text
  parsing.
- **Allocators.** No global heap during gameplay; frame, per-system, and fixed-capacity
  allocators; single owners and handles. The "complete prohibition" wording is stronger
  than anything quoted.
- **Templates.** Limited for compile time, code size, and debug-build speed.
- **Criticism.** "Engine developers such as Vittorio Romeo" calling strict data-oriented
  design brittle. No source was given, so it is excluded.
- **Coroutines.** Console engines override coroutine frame allocation or use fibers. This
  is plausible and matches `moira-gaps.md` item 7, but no source was given.

## Peer practice, from general knowledge (not checked this session)

- Naughty Dog built GOOL (Crash Bandicoot) and GOAL (Jak and Daxter), custom Lisps for the
  PlayStation. Pass 2 confirms this. Pass 2 omits that its later games kept a Lisp-family
  data and script language: Jason Gregory's *Game Engine Architecture* describes a
  Racket-based one. That is the closest shipped relative of DTA in the console AAA world,
  and worth sourcing properly before relying on it.

## Not found

- A quoted statement of Acton's view on embedded scripting languages or interpreters
  (not in the CppCon 2014 talk or its Q&A either).
- The original "Typical C++ Bullshit" images.
