# Pushing Up

How a harness tree handles the moment when work at one layer needs something only a harder
layer can provide — organized around Scott Shumaker's distinction between hard and soft
architecture.

Companion to `harness-tree-vision.md`, which defines the layers L0–L4 used here.
**Orientation:** "up" means toward the substrate — toward harder layers. Pushing up means
escalating a need from a softer layer to a harder one.

---

## 1. Shumaker's principle

Scott Shumaker's lecture *Techniques and Strategies for Data-Driven Design in Game
Development*, written while he was at Outrage Games (he later became lead programmer at
Naughty Dog), makes one architectural distinction its first technique:

- **Hard architecture** is low-level code that would be roughly uniform across titles:
  renderer, sound, low-level input, resource loading, utilities, math.
- **Soft architecture** is code specific to the game being made: powerup spawning,
  inventory, combat, scoring.

And one rule governs the boundary: **soft architecture may know about hard architecture,
but never the reverse.** Only soft systems retrieve and manipulate game data; they may pass
data down to hard systems, but hard systems never reach up.

Two other parts of the deck matter as much for this document as the headline distinction.

**The diagnosis of when soft breaks.** Shumaker's data-driven approach — systems,
components, prototypes, all tunable from a data source — works when many objects reduce to
a few variables and behaviors. It breaks down, he says, when the number of unique
prototypes grows and each needs very different behavior: scripted events and per-creature
AI. His example contrasts a grunt and a rhino boss: a long list of constants each, and
almost nothing in common but health. That is precisely the moment a soft layer starts
needing things it doesn't have.

**The escape hatch.** His last technique is generic debug global variables — a few spare
floats and ints that code under development reads in place of the real constant, so values
can be tuned live in the debugger before anyone has exposed them properly through the data
source. It's crude, and it's an admission that the exposure process is slower than tuning
needs to be. Every harness tree needs an equivalent.

He's also skeptical in a way worth keeping. He argues that the case for scripting languages
as easier for non-programmers is beside the point, because complex behavior can only be
written by someone who could also understand C or C++. Read in harness terms: once soft-layer
work becomes genuinely complex, it already belongs to someone working near a harder layer.
That's a push-up signal.

---

## 2. From a binary to a gradient

Shumaker drew one line. A harness tree has four boundaries, and each one is a hard/soft
boundary relative to its neighbors. So it's useful to treat **hardness** as a graded
property of a layer, made up of five measurable things:

| Property | Hard | Soft |
|---|---|---|
| **Change cost** | Rebuild, redeploy, retest | Edit and reload in seconds |
| **Who can change it** | Engineers | Gardeners, designers, eventually anyone |
| **Blast radius** | Many dependents across games or features | One encounter, one level |
| **Generality** | Would be the same in another title | Exists because of this game |
| **Reach** | Memory, threads, platform, physics, rendering | Only what harder layers expose |

The layers order cleanly by this measure: L0 (Baker) hardest, then the engine harness, the
game harness, the task harness, and the instance data softest. Shumaker's rule generalizes
directly: **each layer may depend on harder layers, never on softer ones.**

The fifth property, reach, is the one that forces push-ups. A softer layer can only *do*
what harder layers have given it words for. Everything else is, as you put it, simply
impossible until something is wrapped and exposed.

---

## 3. Why a softer layer needs to push up

Six distinct reasons, which call for different responses:

1. **Capability.** The thing is impossible at this layer: a physics query, a thread, a
   platform API, a render feature. Something harder must be wrapped and exposed.
2. **Access.** The capability exists, but the data doesn't reach this layer: another
   system's state, an internal the harder layer never surfaced.
3. **Performance.** It works, but too slowly. Unreal's current guidance is the clearest
   industry statement of this one: profile first, then move only the hot path into a C++
   base class exposed back to Blueprint. Epic removed automatic Blueprint nativization in
   UE5, and the community consensus since is to start in Blueprint and harden selectively.
4. **Repetition.** The same soft construction keeps appearing. Halo 3's objectives system
   is the example: designers kept building the same encounter patterns in Halo 2's FSM tool
   until Bungie crystallized them into a declarative vocabulary.
5. **Invariants.** The soft layer can't guarantee something that must hold: network
   prediction and replication, save-game correctness, determinism. Overwatch's Statescript
   exists partly so scripters never face synchronization directly — the harder layer
   absorbs it.
6. **Complexity.** The soft code has quietly become a general-purpose program — Shumaker's
   rhino boss. Its author is now doing an engineer's work in a tool built for tuning.

The first two are **gaps**: something is missing below. The last four are **pressures**:
something above has outgrown its layer. The distinction matters because they resolve in
different directions.

---

## 4. Three motions, not one

A healthy tree has material moving in three ways across every boundary.

### Exposing a primitive (resolves gaps)

Add a small, general capability to the harder layer and give the softer layer a word for
it — `UFUNCTION(BlueprintCallable)` is the archetype. The composed behavior that needed it
stays soft.

### Hardening a pattern (resolves pressures)

Move a soft construction into the harder layer and leave a vocabulary entry behind. The
pattern stops being something every user rebuilds and becomes something they name.

### Softening a constant (the reverse motion)

Move something out of a harder layer into data so softer layers can change it. This is
Shumaker's entire Pac-Man case study — speed constants pulled out of code into a data
source — and it's what `Tweak<T>` makes trivial. Trees that only push up become
calcified; the reverse motion keeps the soft layers powerful.

### The rule that governs all three

**Push mechanisms up; keep policy soft.** When a gap appears, push up the smallest
primitive that closes it, and keep the decision about *how to use it* in the softer layer.
A boss fight that needs line-of-sight gets a `world.line_of_sight` primitive at L1 — not a
`boss.choose_cover_position` at L1. The first is general and will be reused; the second
hardens one encounter's design into the engine, where the designer can no longer tune it.

This is the classic mechanism/policy separation, applied per boundary. It is also the
best defense against the borrowed-harness failure: a harder layer full of one game's
policy is exactly what made Frostbite painful for games it wasn't built for.

---

## 5. Words that look like primitives

Guy Steele's 1998 talk *Growing a Language* made an argument that maps precisely onto
pushing up: a language should be designed to be grown by its users, and the words users
define should be indistinguishable in use from the words the language came with. If they
look different, every promotion from user-defined to built-in forces callers to change.

For a harness tree this is the property that makes pushing up cheap:

- A designer calls `{boss.find_cover $player}`.
- Today that's a script function at L3, written by a gardener, approximating cover from
  existing positions.
- Tomorrow, profiling shows it's hot and the approximation is wrong, so an engineer
  implements `boss.find_cover` natively at L2 with the same signature.
- The call site doesn't change. The data doesn't change. The designer never notices.

Baker already gets most of the way there: native functions and script functions share call
syntax. What's missing is a defined rule for what happens when both exist with the same
name — that's the first spec change in §8.

The same property enables the most useful pattern for gaps: **stub soft, implement hard,
swap without changing callers.** When a capability is missing, the gardener writes a script
stub with the intended name and signature that returns a plausible placeholder. Work at the
softer layers continues against the stub; the call sites define the requirement precisely;
and when the hard implementation lands, it replaces the stub in place. This is Shumaker's
debug-global escape hatch, generalized from values to verbs.

---

## 6. Each boundary, managed

| Boundary | Typical push-up | Who asks → who answers | Mechanism | Target turnaround |
|---|---|---|---|---|
| **L4 → L3** | "I keep setting up the same phase transition" | Designer → gardener | Macro or script function in the task pack | Minutes to hours; no rebuild |
| **L3 → L2** | "The workbench needs to know which players are in cover" | Gardener → gameplay engineer or technical designer | Script function in the game pack, or a new native game verb | Hours to days; maybe a rebuild |
| **L2 → L1** | "The game needs line-of-sight queries" | Gameplay engineer → engine/tools engineer | Native binding of an engine capability | Days; rebuild; review |
| **L1 → L0** | "The engine harness needs write-back" | Tools engineer → Baker | Change to the substrate | Rare; versioned; size-budget gate |

Patterns that apply at every boundary:

**Make demand visible.** Every unresolved word is a push-up request in waiting. If the
check pass reports command heads and object methods that don't resolve — with every call
site — then the backlog of missing primitives is generated from the data, not reconstructed
from tickets. A stub is itself a demand signal: a script function marked as a stub is a
push-up request that already has a signature and callers.

**Make cost visible.** Hardening for performance should follow measurement, as Unreal's
guidance insists. Per-function call counts and cumulative time in development builds give a
standing list of hardening candidates — soft functions that are both hot and stable.

**Enforce the one-way rule.** Shumaker's rule is the one most often broken in practice:
the engine grows a special case for one game, a game pack reaches into one encounter's
data. If each harness pack declares its layer and what it requires, a check can warn when a
harder pack references a softer pack's vocabulary. That warning is how calcification gets
caught early.

**Gate the hard layers on generality.** Shumaker defined hard architecture as code that
would be uniform across titles. Use that literally as a review question for every push-up
into L1 or L0: *would a second game want this?* If not, it belongs at L2.

**Don't push up speculatively.** Shumaker's closing caveat — don't write code that won't
be used immediately — is the right default. A push-up should have callers on the day it
lands. Stubs and the demand report make that easy to verify.

**Leave a forwarding address.** When a pattern hardens, its old soft name should keep
working as a deprecated alias, flagged in the generated reference, until the call sites are
gone. Hardening should never break the layer above it.

---

## 7. A worked example

The Warden fight from `harness-tree-vision.md` §4. In phase three, the boss should
retreat to cover the player can't see, then charge.

1. **Gap appears at L4.** The designer asks for "retreat to cover." There's no such word.
2. **Stub at L3.** The gardener writes `boss.find_cover` as a script function: pick the
   marker farthest from the player. It's wrong in plenty of cases, but it has the right name
   and signature, and the designer starts tuning the phase immediately.
3. **Demand is visible.** The stub is marked as one, and the check report lists it with its
   two call sites.
4. **Gap resolved at L1, as a mechanism.** The real need is line-of-sight, which only the
   engine can answer. An engine engineer exposes `world.line_of_sight` — general, reusable,
   no boss logic in it. It passes the second-game test easily.
5. **Policy stays soft.** The gardener rewrites the stub to score markers using
   `world.line_of_sight`. Cover selection — how far, how hidden, how close to the charge
   path — remains tunable data at L3.
6. **Pressure appears later.** Two more encounters copy the cover-scoring function, and
   development-build statistics show it's hot. It's now both repeated and expensive.
7. **Hardened at L2, invisibly.** A gameplay engineer implements `encounter.find_cover`
   natively in the game harness, with the scoring weights still read from data. The three
   encounters' script copies are replaced by calls to the same name; the old L3 name stays as
   a deprecated alias for a milestone.

Every step was the smallest push that unblocked the layer above. No call site changed when
an implementation moved. The boss's behavior stayed in data the whole time.

---

## 8. What this changes in Baker

Applied to `baker-spec.md`:

1. **Name-compatible hardening.** Script and native functions share one namespace. A
   native registration of a name already defined in script supersedes it, with a logged
   notice and a signature comparison (mismatch warns). A script definition of a name that is
   already native is an error unless written explicitly as an override. Registrations carry
   a tier — native, script, or macro — and an optional status — `stub`, `provisional`, or
   `deprecated (use X)` — all shown in the generated reference.

2. **A demand report.** `baker::check` additionally reports every unresolved command head
   and object method, and every call to a function marked `stub`, with all call sites,
   aggregated across everything it was given. This is the push-up backlog, generated from
   the data.

3. **Call statistics.** In non-shipping builds, each function record keeps a call count and
   cumulative time, updated without allocation. `ctx.stats()` returns them, and a helper
   lists hardening candidates: script functions above a configurable threshold. Compiled
   out under `BAKER_SHIPPING`.

4. **Pack layers and the one-way rule.** A harness pack may begin with a declaration —
   `(pack boss (layer task) (requires game engine))` — naming its layer from an ordered,
   user-defined list. `check` warns whenever a pack references vocabulary defined by a
   softer pack. Undeclared files are unconstrained, so small projects pay nothing.

None of these is large. Together they make every push-up in the tree visible before it is
requested, measurable before it is done, and invisible to callers once it lands.

---

## Sources

- Scott Shumaker, *Techniques and Strategies for Data-Driven Design in Game Development*,
  Outrage Games (lecture slides, University of Michigan–Dearborn CIS 487 course archive).
- Guy L. Steele Jr., "Growing a Language," OOPSLA 1998 keynote; published in
  *Higher-Order and Symbolic Computation*, 1999.
- Unreal Engine Blueprint/C++ guidance and the removal of Blueprint nativization in UE5,
  via Epic documentation and community summaries.
- Damián Isla, "Building a Better Battle: The Halo 3 AI Objectives System," GDC 2008.
- Dan Reed, "Networking Scripted Weapons and Abilities in Overwatch," GDC 2017 (abstract).
