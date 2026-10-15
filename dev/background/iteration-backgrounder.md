# Iteration Time: A Backgrounder for Baker

Postmortems, technical talks, and war stories on reducing iteration time for designers,
artists, and programmers — from the 2004–2005 systems we've studied through to Unreal,
Unity, and Frostbite today — and the use cases where Baker is most relevant.

Companion to `baker-spec.md`, `halo-tags-vs-dataarray.md`, and `seexpr-vs-baker.md`.
Sources are listed at the end. Where a primary source was unavailable (several Kotaku and
Bloomberg pieces), the claim is sourced through secondary coverage and marked as such.

---

## 1. The treadmill

Niklas Frykholm (Bitsquid, later Autodesk Stingray) gave the cleanest statement of the
problem at GDC 2012. Every change a content creator makes travels a loop: make the change,
export, compile data, reboot the game, load the level, find the entity, check the result.
In a typical studio of that era the loop took minutes; sometimes hours. His goal was to
get it under one second — on target hardware, at target frame rate, "no cheating."

He framed the payoff in two terms that recur in every talk since:

- **Productivity.** Time spent waiting is time not spent working. This is the obvious one,
  and the least important.
- **Quality.** When a change is cheap to see, people make more of them. More tweaking,
  more assets tested on the real device, more ideas tried and discarded. The best content
  comes from the hundredth iteration, and long loops mean you never get there.

Chris Butcher's 2004 numbers are the strongest evidence for the second point: about 75% of
Halo 2's content was authored by people looking at it running on an Xbox, on a TV next to
their desk, with a 3–5 second turnaround. The cultural consequence he named — *the task is
not done until the content is in the engine and working* — is a property of the loop, not
of the people. Make the loop long and that culture becomes impossible to sustain.

Eric Malafeew's 2005 version is shorter: data you can change without recompiling, often
without even rerunning. Same idea, framed from a programmer's chair.

---

## 2. When the loop breaks

The industry's best-documented failures are, to a striking degree, tooling and iteration
failures. A non-exhaustive list:

**Destiny (Bungie, 2014).** The successor studio to the team whose Halo 2 loop was three
seconds. Kotaku's 2015 investigation, as summarized in secondary coverage, reported that
loading a map into Destiny's editor took about eight hours, opening it another twenty
minutes, and recompiling it after a minor tweak another fifteen. Even small map changes
could take hours. The same company, a decade after Butcher's talk, on a new engine built
for a larger game. Bungie subsequently invested heavily in its asset pipeline — Brandon
Moro's GDC 2018 talk describes a dependency-graph system serving hundreds of content
creators across multiple studios, and specifically the ways they had to "bend" the rules
of a pure dependency graph to recover fast local iteration.

**Anthem, Mass Effect: Andromeda, Dragon Age: Inquisition (BioWare on Frostbite).**
Jason Schreier's reporting on all three games described Frostbite as a recurring source of
pain. Developers on Anthem described the engine as "full of razor blades," poorly
documented, and hacked together, with tasks that should be quick bogged down by sluggish
tools. Anthem was in development for about seven years, but most of the shipped game came
together in the final six to nine months — a former developer later put it at fifteen
months of crunch. Frostbite was built for Battlefield; BioWare was making RPGs and an
online looter-shooter on tooling designed for something else.

**Halo 4 and Halo Infinite (343 Industries).** Studio head Bonnie Ross attributed Halo 4's
crunch partly to the Halo engine being "a very technical engine," hard for artists and
designers to get content into. The response was the Slipspace engine for Halo Infinite,
with expanded tools and pipelines to prevent a repeat. By 2022–2023, reporting from
Bloomberg and Windows Central described Slipspace as buggy, hard to use, built largely on
1990s and early-2000s code, with developer tools characterized as nearly non-existent —
and some multiplayer modes held up partly by engine issues. 343 announced a move to Unreal
Engine 5.

The pattern across all of these is the same, and it is the one Butcher's talk warned
about: **the cost of a change is the real constraint on a game's quality, and that cost
lives in the tools.** Note too the irony in the Halo lineage specifically. The engine
described in 2004 as delivering a three-second loop became, two decades and two studios
later, a codebase whose tooling was a reason to abandon it.

---

## 3. What worked: the prior art

### Bitsquid: fast compile and hot reload, not live editing (GDC 2012)

Frykholm's argument is worth knowing in detail because it is the most rigorous public
treatment of the problem, and because his conclusions line up closely with Baker's.

He considered and rejected *live editing in the running game* as the primary strategy: the
game is not always the best editor, versioning a living binary image is hard, merging
collaborative work is hard, cross-platform editing is hard, and data formats good for
editing are not the ones good for runtime. Instead: keep editing and runtime formats
separate, make the compile step *very* fast, and replace reboot with reload.

The mechanics:

- Game data is a collection of individually compilable resources identified by type and
  name. Granularity matters — a 100 MB resource can never reload quickly, so don't make
  one.
- The running game listens on a TCP port for JSON commands: toggle the performance HUD,
  show debug lines, open a Lua REPL, reload a resource.
- On reload, the game loads the new resource, **notifies systems by type with pointers to
  both old and new**, and each system decides: delete instances (sounds), stop and restart
  them (particles), or keep and update them (textures).
- The data compiler runs as a persistent server with an in-memory dependency database and
  a directory watcher, so a single-file change touches disk only for the file itself.
- Results: zero-change compile in 20 ms and minimum-change compile in 25 ms on a 7,200-file
  project; 366 ms at 100,000 files.

He also names the property that makes or breaks all of this: the incremental path must be
so reliable that nobody ever feels the need to do a full rebuild "to be safe." In his
words, trust is hard to gain and easy to lose.

Two things to take from this. First, the per-type reload decision — delete, restart, or
update — is the same contract Butcher described in 2004 (migrate or punt) and the same one
now in Baker's spec. Three teams converged on it independently, which is a strong sign
it's the right shape. Second, Frykholm's design puts a *command channel* into the running
game, and the same channel serves reload, debug visualization, and a REPL. Malafeew's
remote level editing did the same thing over serialized script calls. That pattern is a
first-class Baker use case (§6).

### Naughty Dog: s-expressions all the way down

Naughty Dog is the closest relative to DataArray anywhere in the industry, and the least
widely known.

Andy Gavin's GOAL — a Scheme dialect compiled to PS2 machine code — carried the Jak and
Daxter series almost entirely. After Sony's acquisition, Naughty Dog moved the runtime to
C++ for the PS3 generation, reportedly because nobody else at Sony could use a GOAL
codebase. But it did not give up Lisp. It built **DC**, a data and scripting system on
Racket (then MzScheme), and used it across the Uncharted series and The Last of Us.

From Dan Liebgold's RacketCon 2013 talk abstract and Jason Gregory's GDC 2009 slides: DC
hosted many domain-specific languages, a gameplay scripting system, and the definitions
for animation, effects, and sound. Gameplay scripts were state machines written as
s-expressions; per a former Naughty Dog engineer's description, those s-expressions ran
through Scheme macros at build time, expressions compiled to bytecode, and the output was
large C++ structures interpreted by the game at runtime. Liebgold's talk specifically
covered the culture clash of getting C++ programmers, technical designers, and
non-technical artists productive in a world of s-expressions and syntax transformations.

The comparison with DataArray is instructive. Both chose s-expressions for data and
behavior in the same tree. Naughty Dog put the power at **build time** — a full Scheme
macro system, compiling down to runtime structures — and kept the runtime lean. Harmonix
put the power at **runtime** — lazy evaluation, live handlers, script-side classes — and
kept the build trivial. Baker follows Harmonix's choice, which buys hot reload of behavior
for free and costs a heavier runtime. That is the right call for iteration, and Naughty
Dog's choice is the right call for maximum runtime performance; the trade is worth
stating in the README so nobody relitigates it.

### Remedy Northlight: live edit, done carefully (GDC 2024)

Remedy's Sebastian Eriksson described Northlight's current world-editing stack: OpenUSD
for authoring, a separate optimized runtime representation, and a live-edit pipeline that
applies entity modifications to a running game process on the fly with near-instant
iteration. This is the contemporary form of Frykholm's conclusion: keep editing and runtime
representations separate, and make the transform between them fast enough to feel live.

---

## 4. The contemporary engines

### Unreal Engine

Three Unreal systems are directly relevant, and one of them is almost exactly Baker's
`Tweak<T>`.

**Console variables.** `TAutoConsoleVariable<T>` is declared as a static in a `.cpp`
file with a name, default value, help text, and flags, and self-registers with the console
manager. Values are queryable at the console with auto-complete and enumerable with their
help text. The `ECVF_Cheat` flag disables a variable in shipping builds. Values can be set
from a layered hierarchy of sources — constructor default, scalability settings, project
ini, a per-machine `ConsoleVariables.ini`, device profiles, and the console itself — with a
defined priority, and each variable records which layer last set it.

This is strong validation for the `Tweak<T>` design: Epic arrived at static
self-registration, mandatory help text, and dev-only flags independently. It also shows
three things Baker should add: **provenance** (which file or layer set the current value),
**shipping-build behavior** (tweaks locked or compiled out), and **ordered layers** rather
than a single merge. Epic's own docs note that CVar state is main-thread-only and is not
network-synchronized or serialized, and that the ini startup file is applied in undefined
order — limits Baker's design already avoids.

**Live Coding.** Unreal integrated Live++ (by Stefan Reinalter's Molecular Matters) as its
default C++ iteration path, replacing the older Hot Reload system, which was prone to
corrupting assets and couldn't handle reflected-type changes. Live Coding patches running
binaries while the editor or game is running. It's a major improvement — and it changes
the argument for Baker (§5).

**Blueprints and config.** Visual scripting for designers, plus `UPROPERTY(config)` for
ini-backed class defaults, together cover most of what DataArray's data and scripting
layers did inside a studio, with an editor UI on top. Inside Unreal, Baker's value
proposition is thin and the docs should say so.

### Unity

Unity's iteration story has been dominated for years by **domain reload**: on every script
change, and by default on entering Play mode, Unity tore down and rebuilt the Mono
scripting domain. Forum threads document projects where this took tens of seconds, then
minutes; one user reported a 17-minute cycle, another that upgrading to 2022 LTS doubled
their iteration time. Unity added options to skip domain and scene reload on entering Play
mode, and current documentation now recommends leaving domain reload off — both for
iteration speed and because Unity's move to the CoreCLR runtime eliminates the
domain-reload concept entirely. The cost of skipping it is that static state survives
between play sessions, and code has to be written to tolerate that.

The relevant design pattern from the Unity world is Ryan Hipple's **ScriptableObject
architecture** (Unite Austin 2017). ScriptableObjects are serialized data assets; Hipple
used them not just for static data but as shared variables (`FloatVariable`) and event
channels, so designers could wire behavior and tune values in the inspector without code
changes, and adjust them at runtime. His three pillars were modularity, editability, and
debuggability, with an emphasis that the more of the game you can change at runtime, the
more balance you can find. Community implementations of the pattern note it also serves
live-game economy tuning without a client update.

`FloatVariable` is `Tweak<float>`; the event-channel asset is Baker's message dispatch
with a different binding. Hipple reached DataArray's shape from inside Unity, twelve years
later.

### Frostbite

Frostbite is the cautionary entry. It's an extraordinarily capable engine for the games it
was built for, and the BioWare reporting above is about the cost of using engine tooling
outside its home genre. EA's own current Frostbite job postings describe the problem from
the inside: core systems for data management and asset processing serving thousands of
users editing millions of assets, with a build cache and on-demand loading service — the
Bungie problem, at EA scale, as a standing team.

---

## 5. What's changed since 2005

Three shifts matter for how Baker should be positioned.

**C++ iteration got good.** In 2005, moving logic into script was one of the few ways to
change behavior without a rebuild-and-restart cycle. Live++ and its descendants now patch
running C++ in seconds in Unreal, in custom engines, and from IDEs. That weakens one of
DataArray's original arguments — "script for iteration" — and strengthens its others:
script for things that change *without a programmer*, for things that change *after ship*,
and for things that must change *on the target device*.

**Iteration moved past ship.** Live operations are now normal, and the same problem
appears post-launch at a different timescale. Blizzard's World of Warcraft team described
server-side hotfixes that can land while players are online, but not for art, sound, or
text without a client patch. Overwatch's team explained that hero ability data is compiled
into assets on both client and server, so even a numeric change normally needs both
updated, and their hotfix system for doing otherwise is complex enough that it has failed
in production. Riot's League team described redeploys that had to replace *all* server
data at once — one of which deleted every player's game executable — replaced by
**micropatches**: targeted overrides that a new game session applies on top of existing
server data, like sticky notes, initially limited to numeric values.

A micropatch is a `#merge` of a small overlay onto a base config. Baker already has the
semantics; what it lacks is the operational wrapping — named layers, provenance, rollback.

**Engines consolidated.** Studios that once built bespoke engines are moving to Unreal,
partly for tooling (343's move is the highest-profile example). That shrinks the market for
a Baker-shaped library inside large studios and grows it in the places Unreal isn't: custom
and small-team C++ engines, dedicated game servers, tools, and middleware.

---

## 6. Use cases for Baker, ranked by relevance

Each entry names the loop being shortened, who's in it, and the prior art.

### 1. Tweaks: one-line tunables with hot reload

**Loop:** programmer or designer changes a number → sees it in the running game.
**Prior art:** Unreal CVars, Valve-style convars, Hipple's `FloatVariable`, DataArray's
config + param editor.
**Why Baker:** this is the single most universally useful piece, and the piece that every
engine reinvents. In a custom C++ engine, `Tweak<T>` is the CVar system you'd otherwise
write yourself, plus hot reload, plus data-side metadata. The highest-leverage thing in the
library and the thing the README should lead with.

### 2. Gameplay config on target hardware

**Loop:** edit a data file on a PC → the console, phone, or handheld picks it up live.
**Prior art:** Butcher's Xbox path-mapping file; Frykholm's PC-hosted file server for
consoles; Remedy's live-edit pipeline.
**Why Baker:** reload is already in the design; what's needed is a transport-agnostic hook
so a host can feed a changed file in from a socket rather than a local filesystem. The
library should not own the network, but it should make wiring one trivial.

### 3. A development command channel

**Loop:** a programmer, designer, or QA tester types a command at a running build — local
or remote — and something happens.
**Prior art:** Frykholm's TCP JSON channel with Lua REPL; Malafeew's remote level editing
via serialized script calls; every engine's developer console.
**Why Baker:** s-expressions are the ideal wire format for this. A command is already a
serialized, parseable, evaluable value, so there is no separate protocol. `{bob grow 10}`
typed at a console, sent over a socket, or recorded in a test script is the same thing.
Needs a public `eval(string)` entry point and an example remote console.

### 4. Live-ops overlays

**Loop:** a live-ops designer pushes a numeric balance change to running servers without a
client patch or a full redeploy.
**Prior art:** Riot micropatches, WoW hotfixes, Overwatch hotfixes, remote-config services.
**Why Baker:** an overlay `.dta` merged onto base config *is* a micropatch. Add named,
ordered, revertible layers and provenance, and Baker covers the server-side half of this
directly. The operational lessons from Riot and Overwatch — environment boundaries,
version history, rollback, tests — belong in the cookbook, because config systems become
dangerous precisely when they make live changes easy.

### 5. Content metadata and extensibility

**Loop:** a content team (or the community) adds new content without an engineer.
**Prior art:** Rock Band's `songs.dta` and everything downstream of it — DLC, the Rock
Band Network, two decades of community content.
**Why Baker:** this is the use case with the longest documented tail, and the one where
self-describing, forgiving, text-first data matters most, because the people writing it
are furthest from the code. The robustness principle (no data can crash the host) is what
makes it safe.

### 6. Data-attached behavior for prototyping

**Loop:** a programmer or technical designer attaches small behaviors to data — on collide,
play this sound and add score — and iterates without touching C++.
**Prior art:** DataArray handlers; Naughty Dog's state scripts; Hipple's event channels.
**Why Baker:** still valuable, but the weakest of the six relative to 2005 because of Live
Coding. Baker's argument here is not "faster than C++" but "editable by someone who isn't
writing C++, and reloadable on a device where Live++ doesn't reach."

### 7. Test and repro scripts

**Loop:** QA or a programmer reproduces a bug or runs an automated scenario.
**Prior art:** Butcher's save-game + build + minidump repro pipeline; automated testing
talks such as Rare's on Sea of Thieves.
**Why Baker:** the command channel (use case 3) recorded to a file is a repro script.
Cheap to support once 3 exists.

### Not use cases

- **Art and bulk asset pipelines.** That is Butcher's and Frykholm's problem domain, and
  binary, schema-rich formats win there.
- **Per-pixel or per-vertex computation.** That's SeExpr's domain.
- **Replacing Blueprints, CVars, or DataAssets inside Unreal.** Unreal already covers the
  ground. A Baker plugin for Unreal would be a curiosity, not a product.

---

## 7. What this changes in the spec

Applied to `baker-spec.md`:

1. **Provenance.** Every tweak and every config value can report the file, line, and layer
   that produced its current value: `kBoxWidth.source()`. "Why is this number what it is?"
   is the most common question asked of any config system, and Unreal records the answer
   for every CVar. We already track file and line on arrays, so this is nearly free.

2. **Ordered, named, revertible layers.** `Config` holds an ordered stack of named layers
   (`default`, `platform`, `app`, `local`, `live-patch-2026-10-11`, …). Merge applies them
   in order; `push_layer`, `pop_layer`, and `remove_layer(name)` re-merge and fire the
   normal reload callbacks. This turns `#merge` into Riot-style micropatches with rollback,
   and gives Unreal's set-by hierarchy a cleaner model.

3. **Shipping behavior.** A `BAKER_SHIPPING` build switch makes reload a no-op, freezes
   tweak values at their loaded state, and lets individual tweaks be marked `dev_only` so
   they ignore data overrides entirely in shipping builds — the `ECVF_Cheat` equivalent.

4. **A public evaluation entry point and a remote-console example.** `ctx.eval(text)`
   parses and evaluates a command string and returns the result or a located error. The
   library still owns no sockets and no threads; `examples/remote-console` shows how to
   wire `eval` to a TCP listener in under a hundred lines, which is the pattern Frykholm and
   Malafeew both described.

---

## Sources

- Niklas Frykholm, "Cutting the Pipe: Achieving Sub-Second Iteration Times," GDC 2012
  (slides mirrored at gwern.net).
- Chris Butcher, Halo 2 talk and speaker notes, Game Tech 2004 (supplied).
- Eric Malafeew, "Data-Driven Programming Made Easy," GDC 2005 (slides and audio).
- Kotaku, Destiny development investigation (2015), via Engadget and GamePro summaries.
- Brandon Moro, "Bungie's Asset Pipeline: Destiny 2 and Beyond," GDC 2018 (session
  abstract).
- Kotaku, "How BioWare's Anthem Went Wrong" (2019), via KitGuru, Wccftech, and Film
  Stories coverage.
- Bloomberg and Windows Central reporting on Slipspace and 343 Industries (2022–2023), via
  VGC, MP1st, Gameblog, and OnMSFT coverage; Bonnie Ross on Halo 4 crunch, via TweakTown.
- Dan Liebgold, "Racket on the PlayStation 3? It's Not What You Think!", RacketCon 2013
  (abstract).
- Jason Gregory, "State-Based Scripting in Uncharted 2: Among Thieves," GDC 2009.
- Sebastian Eriksson, "Real-Time World Editing Technology in Northlight," GDC 2024
  (session abstract).
- Epic Games, Unreal Engine documentation: Console Variables in C++; EConsoleVariableFlags;
  Live Coding.
- JetBrains, "Hot-Reloading C++ with Live++ and JetBrains Rider" (2024).
- Unity documentation, "Configuring how Unity enters Play mode" (Unity 6); Unity
  Discussions thread on domain reload performance.
- Ryan Hipple, "Game Architecture with Scriptable Objects," Unite Austin 2017; Unity's
  "Three ways to architect your game with ScriptableObjects."
- Riot Games, "Micropatches Save Anivia" (2017); Blizzard, Ghostcrawler on WoW hotfixes;
  Overwatch developer post on hotfix infrastructure.
- EA Frostbite production-systems job descriptions (2025–2026).
