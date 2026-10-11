# Sources for the acton-review skill

Gathered 2026-10-11 in two passes.
1. **Web search from this repository's sandbox.** Its network policy blocks fetching pages,
   so this pass saw search-result excerpts only.
2. **A second research agent with page access, at the project owner's request.** It
   supplied exact quotes with slide numbers. The sandbox still could not fetch
   `neil3d.github.io`, `cellperformance.beyond3d.com`, or `blog.s-schoener.com` to
   recheck them, so quotes from this pass are confirmed by one reader, not two.

The confidence levels below reflect this. Before quoting anything as Acton's exact words
outside this repository, check it against the deck or the video.

Slide numbers refer to the PDF export of the CppCon 2014 deck at
<https://neil3d.github.io/assets/img/ecs/DOD-Cpp.pdf>. Other exports number them differently,
for example the SlideShare mirror at
<https://www.slideshare.net/slideshow/data-oriented-design-and-c/39109965>.

## Acton's own material

- **"Data-Oriented Design and C++", CppCon 2014.**
  - Abstract, high confidence:
    <https://isocpp.org/blog/2015/01/cppcon-2014-data-oriented-design-and-c-mike-acton>.
    It says "the transformation of data is the only purpose of any program", and that
    common C++ approaches work against that goal in console game development, including
    limits inherent in C++ compilers.
  - Slides: <https://www.slideshare.net/cellperformance/data-oriented-design-and-c>.
    - **High confidence** (quoted by pass 2, and consistent with pass 1's excerpts):
      - slide 30: "The purpose of all programs, and all parts of those programs, is to
        transform data from one form to another."
      - slide 34: "If you have different data, you have a different problem."
      - slide 41: "Rule of thumb: Where there is one, there are many. Try looking on the
        time axis."
      - "Latency and throughput are only the same in sequential systems." The slide is
        around 40; exports differ.
      - "Solving problems you probably don't have creates more problems you definitely
        do." (the principles section)
      - slide 120: "Ignoring inconvenient facts is not engineering; It's dogma."
      - slides 27–28: "No STL", "Custom allocators (lots)", "Custom debugging tools".
    - **Medium confidence** (pass 1's excerpts only): the rest of the Insomniac list: no
      exceptions, templates, iostream, multiple inheritance, operator overloading, or
      RTTI. Pass 2 said templates were "limited, not banned" but gave no quote for it or
      for its reasons.
    - **Medium confidence:** a `bool` in a struct has "extremely low information density"
      (pass 2 quoted the phrase, without a slide number).
    - **Pass 1 excerpts:**
      - a cache-line utilisation example (about a 10x speedup);
      - maintainability, debuggability, and the cost of change as goals;
      - "visual scripting" among the engine team's tools.
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
  found.
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

- A quoted statement of Acton's view on embedded scripting languages or interpreters.
- The original "Typical C++ Bullshit" images.
