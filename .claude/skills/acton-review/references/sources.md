# Sources for the acton-review skill

Gathered 2026-10-11 by web search. The sandbox's network policy blocked fetching the pages
themselves, so each attribution below rests on search-result excerpts, with the confidence
stated. Before quoting any of it as Acton's exact words outside this repository, check it
against the talk video or slides.

## Acton's own material

- **"Data-Oriented Design and C++", CppCon 2014.**
  - Abstract, high confidence:
    <https://isocpp.org/blog/2015/01/cppcon-2014-data-oriented-design-and-c-mike-acton>.
    It says "the transformation of data is the only purpose of any program", and that
    common C++ approaches work against that goal in console game development, including
    limits inherent in C++ compilers.
  - Slides: <https://www.slideshare.net/cellperformance/data-oriented-design-and-c>. Seen
    only through excerpts, medium-high confidence. They include:
    - the principles "where there is one there are many (try looking on the time axis)",
      "if you have different data, you have a different problem", "latency and throughput
      are only the same in sequential systems", and "solving problems you probably don't
      have creates more problems you definitely do";
    - the Insomniac feature list: no exceptions, templates, iostream, multiple inheritance,
      operator overloading, RTTI, or STL, and "custom allocators (lots)";
    - a cache-line utilisation example (about a 10x speedup);
    - maintainability, debuggability, and the cost of change as goals, and "ignoring
      inconvenient facts is dogma";
    - "visual scripting" among the engine team's tools.
- **"Three Big Lies: Typical Design Failures in Game Programming", GDC.**
  <https://gdcvault.com/play/1012960/Three-Big-Lies-Typical-Design>. Medium confidence on
  the content: software is a platform; code should be designed around a model of the
  world; code is more important than data. The skill does not quote it directly.
- **"Everyone Watching This Is Fired", GDC 2019.**
  <https://gdcvault.com/play/1025778/Everyone-Watching-This-Is>. The list of about 50
  expectations exists only as fan reconstructions, so it is low confidence on exact
  wording:
  - Adam Johnson: <https://adamj.eu/tech/2022/06/17/mike-actons-expectations-of-professional-software-engineers/>;
  - Jackson Dunstan: <https://www.jacksondunstan.com/articles/5137>;
  - Lobsters: <https://lobste.rs/s/8cb0fy/everyone_watching_this_is_fired>.

  Items used: state the problem, its value, and a fallback; articulate the latency
  requirements; know the common real values and acceptable ranges; profile.
- **MIT 6.189 lecture, Insomniac (2007).** <https://ocw.mit.edu/courses/6-189-multicore-programming-primer-january-iap-2007/0cbeedf18c91ac6c0b75d21194f1d5fd_lec16games.pdf>.
  The engine team's priorities: hard deadlines, soft realtime (33 ms), usability, and
  performance. Most CPU time is in engine code.
- **Unity DOTS Best Practices, "Data design"** (written while he led DOTS):
  <https://learn.unity.com/course/dots-best-practices/tutorial/part-2-data-design>. It
  says "the energy required to transform some data should be proportional to the amount
  of surprise." Medium confidence that this phrasing is his.

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

## Not found

No source states Acton's view on embedded scripting languages or interpreters (Lua or
otherwise). The skill says so instead of inventing one.
