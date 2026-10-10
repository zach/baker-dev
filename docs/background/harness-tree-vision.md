# The Harness Tree

A vision for how large-scale game development builds the environments in which a game is
actually discovered — and what that implies for how Moira is implemented, integrated, and
used.

Companion to `moira-spec.md`, `iteration-backgrounder.md`, `halo-tags-vs-dataarray.md`,
and `seexpr-vs-moira.md`. Sources are listed at the end.

---

## 0. The thesis

Large-scale game development converged on a model in which much of the engineering is the
construction of **harnesses** for human developers: environments, built by one role for
another, in which a specific kind of creative work can be done quickly and safely.
Animators work inside rigs. Technical artists build procedural tools for environment
artists. Designers work inside encounter systems, ability systems, and spreadsheets that
someone else assembled and they then specialized. And the game itself — the boss fight that
works, the economy that feels fair — is found by iterating on data inside those harnesses,
often by playing the running game and adjusting it live.

The harnesses form a tree. At the leaves is the data of one encounter. Above it, the
harness a designer specialized to make that encounter tunable. Above that, the game-wide
designer harness. Above that, the engine's data-definition harness. And at the root, a
general substrate for representing, evaluating, and reloading data — which is what Moira
is for.

This document tests that thesis against the public record, sharpens it, and turns it into
a process and a set of requirements.

### What "harness" means here

A harness, in this sense, has four properties:

1. **It speaks the domain's vocabulary.** A rig exposes controls, not joints. An encounter
   tool exposes tasks and squads, not state machines.
2. **It bounds what can be expressed.** Constraints are the point. A good harness makes the
   wrong things hard to say.
3. **It closes a loop to the running game.** A harness without fast feedback is just a form.
4. **It can be specialized by its user.** The person inside it can reshape it further,
   without going back to the person who built it.

The word is borrowed from the test harness, and it has lately acquired a third meaning: the
environment that wraps an AI agent and gives it tools, context, and a feedback loop. The
three senses share a structure — a bounded, instrumented environment that makes a kind of
work tractable — and the parallel is useful, not just cute. §7 returns to it.

---

## 1. The evidence, by role

The convergence is real and it is visible in every discipline.

### Animators: the rig

A rig is the canonical harness. A rigger builds a control layer over a skeleton so an
animator manipulates intent — reach, plant, look — rather than joint rotations. The craft
has industrialized around this: modular rigging frameworks assemble rigs from reusable
blocks and regenerate them on demand, specifically so rigs can be iterated without
redoing work by hand.

The interesting recent development is a step toward property 4. Unity's GDC 2020
Technical Artist Summit talk on *freeform* animation rigging described a problem with
conventional rigs: once a control rig has been animated, changing its structure destroys
the animation. Their approach lets animators restructure the control rig themselves,
non-destructively, without a rigging specialist — explicitly framed as giving animators a
faster iteration loop and more customization. That is a harness becoming malleable by its
user.

### Technical artists: procedural tools

Houdini Digital Assets are the clearest example of harness-building as a job. SideFX's own
material describes the pattern: technical artists build procedural tools in Houdini and
share them with game artists throughout the studio, who use them inside Unreal or Unity
while Houdini cooks the result underneath — keeping assets procedural deep into
production. Ubisoft's job descriptions for the role describe it as understanding level
designers' and level artists' needs in order to provide custom-tailored tools, reconciling
gameplay, technical budgets, and art direction in one rule set.

That is, almost word for word, the harness thesis: one role builds a bounded, specialized,
parameterized environment for another.

### Designers: encounters

Damián Isla's GDC 2008 talk on Halo 3's AI Objectives System is the best documented case
of a designer harness being redesigned around its users. Halo 2 gave mission designers an
imperative tool: build a state machine that moves groups of AI from area to area. Halo 3
inverted it. Designers declare tasks in the environment — what needs doing, with
priorities and conditions — and the system distributes squads across them, so a battle can
be dismantled in any order while the designer's intent is preserved. Two details from the
slides matter for this document: the system was designed **from the UI outward**, as a
one-stop shop for encounter construction; and it became considerably more complicated as
encounters approached shippable state.

The move from imperative to declarative is the harness tree growing a layer. Designers had
been building the same patterns by hand in the FSM tool. Bungie crystallized those patterns
into a vocabulary.

### Designers: abilities

Overwatch's **Statescript** is a visual scripting language for the high-level state
machines that drive hero weapons and abilities. Dan Reed's GDC 2017 abstract describes its
purpose in harness terms: an ever-growing library of code-backed building blocks that let
designers take new heroes from prototype to shippable with little or no new code, with
prediction and replication handled automatically so the scripter doesn't face networking
problems directly.

Unreal's **Gameplay Ability System**, originally built for Paragon and used throughout
Fortnite, is the same idea shipped as an engine feature. It must be set up in C++, but
abilities and effects are then authored by designers, with gameplay effects designed to be
data-only. Its hierarchical gameplay tags exist, per Epic's own technical brief, to allow
designer-driven gameplay without code support. GAS is a textbook layer-2 harness: an
engine-level framework that each game specializes into its own vocabulary of attributes,
effects, and tags, which designers then use to build abilities.

### Designers: economies and balance

The spreadsheet is the most widely used designer harness in the industry, and it was never
designed to be one. Hutch Games described the familiar trajectory: they first built bespoke
forms for every entity designers wanted to tweak, found it cumbersome to build and slow to
use, and ended up integrating directly with the tool designers already preferred — Google
Sheets, exported to JSON. GDC 2026 ran a whole microtalk session on spreadsheets for
systems, economy, and narrative designers, and there is a small industry of
sheets-to-game-data products.

The lesson is uncomfortable for tool builders: when the official harness is worse than a
general-purpose malleable tool, designers route around it. The spreadsheet wins because it
satisfies property 4 better than almost anything a studio builds.

### Programmers: immediate-mode tools

Dear ImGui began at Media Molecule on *Tearaway* and is now used across the industry. Its
stated purpose is to enable fast iteration and to let programmers create content-creation,
visualization, and debug tools — both ad hoc short-lived ones and long-lived elaborate ones.
That last phrase is the important one. Programmers' harnesses are built constantly and
thrown away constantly, and ImGui succeeded by making the cost of a disposable harness
nearly zero.

### Gameplay scripting: data and behavior together

Naughty Dog's state scripts (Uncharted, The Last of Us) and Harmonix's DataArray are the
two clearest cases of a single s-expression substrate carrying both data and behavior, on
top of which game-specific vocabularies were built. Both are covered in the earlier
documents; they matter here because they show the substrate layer existing as a distinct
thing, under the game-specific layers.

---

## 2. The theory: this has a name, and it predates games

### Nardi's three tiers

Bonnie Nardi's *A Small Matter of Programming* (MIT Press, 1993) studied end-user
programming in two successful settings, spreadsheets and CAD, and found a consistent social
structure. There are **end users**, who are domain experts with little programming
background. There are **local developers** — domain experts with more computing
sophistication, who write macros and customizations and support their colleagues. And there
are **professional programmers**. When the local-developer role was formalized and
recognized in the organizations she studied, those people were called **gardeners**.

Map it onto a game studio:

| Nardi | Game studio |
|---|---|
| End users | Designers, artists, animators |
| Local developers / gardeners | Technical designers, technical artists, riggers |
| Professional programmers | Engine, tools, and gameplay engineers |

The fit is exact, and it explains why the "technical" roles exist. Studios formalized the
gardener role and gave it titles. Nardi's central technical thesis is also the right one for
the harness tree: end-user programming succeeds when it is built around **task-specific
operations** — give spreadsheet users a `SUM`, not a loop — and her book is, perhaps
surprisingly, a defense of text-based formal languages for end users.

### Malleable software

Ink & Switch's 2025 essay *Malleable Software: Restoring User Agency in a World of
Locked-Down Apps* (Litt, Horowitz, van Hardenberg, Matthews) argues for tools that users can
reshape with minimal friction, so modification becomes routine rather than exceptional. It
names three patterns: a **gentle slope** from user to creator, as in spreadsheets and
HyperCard; **tools, not apps** — composable instruments rather than sealed products; and
**communal creation**. It also makes the point that AI-assisted programming is not
sufficient on its own: generated tools need a malleable environment to compose into.

The games industry's harness tree is arguably the largest real-world instance of a
malleable-software ecosystem, built under deadline pressure by people who mostly never used
the term. That's worth stating plainly, because it means Moira can borrow from both bodies
of work: the industry's war stories for what fails in practice, and the research for why.

---

## 3. The tree, refined

Your proposed layering holds up. Three refinements make it more useful.

### The layers

```
L0  Substrate         Moira: representation, evaluation, reload, layers,
                      provenance, introspection.
                      Built by: library authors.

L1  Engine harness    The engine's vocabulary bound to the substrate:
                      entities, assets, transforms, generic inspectors,
                      the command channel.
                      Built by: engine and tools engineers.

L2  Game harness      This game's vocabulary: abilities, weapons,
                      encounters, economies, factions.
                      Built by: gameplay engineers with technical designers.
                      Examples: GAS as specialized by a game; Statescript's
                      block library; Halo 3's objectives.

L3  Task harness      One task family made tunable: a boss-encounter
                      workbench, an economy sheet, a character rig.
                      Built by: gardeners — technical designers and
                      technical artists — mostly in data, not engine code.

L4  Instance          The data of this fight, this level, this hero:
                      numbers, timings, placements.
                      Built by: everyone, mostly by playing.
```

### Refinement 1: the tree is built downward but discovered upward

Each layer is *authored* using the vocabulary of the layer beneath it. But the shape of
each layer is *discovered* from the layer above. Halo 3's objectives system exists because
designers had been hand-building the same encounter patterns in Halo 2's FSM tool. Statescript's
block library grows because heroes keep needing the same behaviors. Hutch's sheets
integration exists because designers kept fleeing the bespoke forms.

So the process is not "design the harness, then use it." It is: work at the instance level,
notice repetition, and **crystallize** recurring patterns into vocabulary one layer up — at
the lowest layer that can hold them.

### Refinement 2: the loop is cross-cutting, not a layer

Every layer must preserve the fast loop to the running game, or it fails regardless of how
good its vocabulary is. Destiny's editor had vocabulary; it had an eight-hour load. The loop
is a property each layer inherits from the substrate and must not break. That is why
reload, layering, and provenance belong at L0: nothing above can add them back if L0 lacks
them.

### Refinement 3: malleability is per layer, and it determines who can garden

A layer is only as malleable as its least-flexible dependency. If L2 vocabulary can only be
extended in C++, the gardeners can't extend it and every L3 harness becomes an engineering
request. The design question for every layer is: **can the people one level up extend it
without a rebuild?** Nardi's gardeners only exist where the answer is yes.

---

## 4. A worked example: iterating a boss fight

To make the layers concrete — illustrative syntax, not a spec.

**L0 (Moira)** provides arrays, commands, macros, tweaks, layers, reload, `eval`.

**L1 (engine harness)** binds engine verbs and declares shapes for engine objects:

```
{func spawn ($archetype $marker) ...}          ; native, registered by engine
(shape actor_spawn
  (archetype symbol)
  (marker    symbol (kind marker))
  (delay     float  (range 0 30) (step 0.5)))
```

**L2 (game harness)** defines the game's vocabulary for encounters, out of L1 pieces:

```
(shape boss_phase
  (enter_at  float (range 0 1) (kind percent) (desc "HP fraction that starts this phase"))
  (attacks   (list symbol))
  (cadence   float (range 0.2 5) (desc "Seconds between attack decisions")))

[PHASE_ENTER (on_hp_below $threshold {encounter advance_phase})]
```

**L3 (task harness)** is a technical designer's boss workbench: a macro set, a few script
functions, and a tuning panel generated from the shapes — plus commands bound to hotkeys:

```
{func boss.restart_phase ($n) {encounter reset} {encounter jump_to_phase $n}}
{func boss.godmode () {player set_invulnerable TRUE}}
```

**L4 (instance)** is the fight itself:

```
(warden_fight
  (phase (enter_at 1.00) (attacks sweep slam)      (cadence 1.8))
  (phase (enter_at 0.60) (attacks sweep slam roar) (cadence 1.2))
  (phase (enter_at 0.25) (attacks frenzy)          (cadence 0.6)
         (note "0.6 feels unfair at 60fps on handheld — try 0.7")))
```

The designer plays. Phase three is too punishing. They type
`{boss.restart_phase 3}` at the console, drag `cadence` in the generated panel, and feel the
change immediately. When it's right, they press **save** — and the new value is written back
into `warden_fight.dta`, at the line it came from, with the note intact. The
change is a two-line diff in version control. Nothing was rebuilt; no engineer was involved.

That last step — **write-back** — is the hinge of "direct online discovery." Values found by
playing must flow back to the source, losslessly, or discovery stays trapped in the running
session. Butcher's Guerilla wrote tags; Hipple's ScriptableObjects persist runtime edits;
most debug-tweak systems don't, and the tuned value gets transcribed by hand or lost.

---

## 5. The process: how a studio grows its harness tree

### Phase 1 — Substrate and loop first

No harness is worth building until a change can be seen in the running game in seconds,
on target hardware. Get reload, layers, provenance, and the command channel working before
anyone builds anything domain-specific. Every war story in the backgrounder is a failure at
this phase.

### Phase 2 — Instrument the instance level

Give everyone tweaks and a console from day one. Then watch what people do repeatedly:
which values they keep dragging, which command sequences they keep typing, which
spreadsheets they keep exporting. Those repetitions are the specification for the next
layer.

### Phase 3 — Crystallize, at the lowest layer that can hold it

When a pattern recurs — a working rule of thumb is the third time — promote it into
vocabulary. Data-only patterns become macros. Behavioral patterns become script functions.
Only patterns that need performance or engine access become native code. Isla's lesson
applies: design the new vocabulary from the user's task outward, not from the
implementation inward.

### Phase 4 — Hand the pen to the gardeners

L3 harnesses should be built by technical designers and technical artists, in data, without
engine rebuilds. The engineering job at this point is to make L2 extensible enough that
gardeners rarely need to ask. When they do ask, that's a signal about L2's vocabulary, not
just a ticket.

### Phase 5 — Prune

Harnesses outlive their purpose. Halo 3's objectives grew more complicated near ship;
every studio's tools accrete. Treat a harness with no users in a milestone as a candidate
for removal, and keep the substrate small enough that removing things is safe.

### Failure modes worth naming

- **The borrowed harness.** Tooling built for one game's L2 imposed on another's. The
  BioWare-on-Frostbite reporting is the standard example: excellent engine, harness tree
  grown for a different genre.
- **The harness without a loop.** Rich vocabulary, slow feedback. Destiny's editor.
- **The inner platform.** A harness that slowly reinvents a worse general-purpose language
  inside its UI. Guard against this by keeping a real language (the substrate) underneath
  and letting gardeners drop down to it.
- **The single gardener.** Naughty Dog left GOAL partly because, by former staff accounts,
  only one person really understood the compiler. Every layer needs more than one person
  who can extend it.
- **The fled harness.** Users route around the official tool to a spreadsheet. Not a
  failure of the users — a signal that the harness failed property 4.

---

## 6. What this means for Moira

Moira is L0. Its job is to make every layer above it cheap to build, fast to iterate, and
malleable by the people one level up. Measured against that, the spec already covers a lot:
macros and script functions give data-only vocabulary extension; native binding covers
engine verbs; layers map naturally onto harness levels (engine defaults, game, level,
designer-local, live patch); provenance answers "where did this come from"; tweaks and
`eval` give every layer a tuning surface and a command channel.

Four gaps stand out, in priority order.

### 1. Lossless editing and write-back

The worked example's hinge. A tuned value must be written back into its source file at the
location provenance already knows, preserving comments, notes, formatting, and macro
structure. That requires a **lossless parse mode** that retains trivia (comments,
whitespace) alongside the tree — the "load for editing" versus "load for gameplay"
distinction Butcher described in 2004. Gameplay loads stay lean; editing loads keep
everything. Write-back of a scalar is then a token replacement at a known position.

### 2. Shapes: data-defined schemas, optional

The Halo comparison concluded that self-describing data is right and code-defined schemas
are wrong, but it left something on the table: Halo's schemas generated a complete editing
UI. **Shapes** recover that without the cost. A shape is declared *in data*, by whichever
layer owns the vocabulary (L1 for engine objects, L2 for game concepts), and gives fields
types, ranges, kinds, descriptions, and notes — generalizing the tweak metadata that
already exists. Shapes drive two things: generated tuning panels, and `moira::check`
validation at load. They are opt-in; untyped data still works everywhere. Because shapes
are data, gardeners can write them, which means L3 harnesses can get generated UI without
an engineer.

### 3. Harness packs: include-once and namespacing

Each layer's vocabulary — macros, functions, shapes, tweak metadata — needs to be packaged,
loaded once, and kept from colliding with other layers'. That needs two small things: a
`#require` directive (include-once, so several L3 harnesses can depend on the same L2 pack),
and a naming convention with dotted prefixes (`boss.restart_phase`, `econ.drop_rate`) that
the generated reference and tuning panels group by.

### 4. A reference harness

Moira should ship one example of a complete L1-style harness: Dear ImGui panels generated
from the tweak registry and shapes, a console on `eval`, provenance shown on hover, and a
save button that writes back. Not core library code — an example, under `examples/` — but
it is the thing that turns a substrate into something a team can picture adopting. It is
also, given ImGui's place in the industry, the most likely form a real integration would
take.

---

## 7. A note on the third meaning of "harness"

The AI-agent sense of the word is not a coincidence of vocabulary. An agent harness is a
bounded, instrumented environment that gives a worker the domain's tools and a feedback
loop — the same four properties as §0.

Ink & Switch's point about AI coding applies directly: generated tools only compose if the
environment underneath them is malleable. A harness tree built on Moira is text all the way
down, introspectable through the generated reference and shapes, validated by `check`, and
reloadable in seconds. Those are exactly the properties that make it feasible for an agent
to act as a gardener — writing an L3 harness from a designer's description, validating it,
and hot-loading it into the running game for the designer to try. That isn't a feature to
build into Moira. It's a reason to get the substrate properties right, because they are
what make the top of the tree cheap to grow, whoever is growing it.

---

## Sources

- Damián Isla, "Building a Better Battle: The Halo 3 AI Objectives System," GDC 2008
  (slides via WPI course archive; abstract via halo.bungie.org).
- Dan Reed, "Networking Scripted Weapons and Abilities in Overwatch," GDC 2017 (abstract
  and GDC preview).
- Epic Games, Gameplay Ability System technical brief; community GAS documentation;
  CNAM article on GAS as a designer tool.
- SideFX, "Procedural Primer for Artists," GDC 2022; SideFX games industry pages; Ubisoft
  Houdini Technical Artist job description.
- Dave Hunt, "Freeform Animation Rigging: Evolving the Animation Pipeline," GDC Technical
  Artist Summit (abstract); modular rigging frameworks (Crab, Clemson MFA thesis).
- Hutch Games, "For the love of spreadsheets" (tech blog); GDC 2026 Spreadsheets
  Microtalks; Beamable Game Content Designer announcement.
- Dear ImGui README and funding wiki (Omar Cornut).
- Bonnie A. Nardi, *A Small Matter of Programming: Perspectives on End User Computing*,
  MIT Press, 1993; Nardi and Miller, "Gardeners and Gurus: Patterns of Cooperation among
  CAD Users."
- Ink & Switch, *Malleable Software: Restoring User Agency in a World of Locked-Down Apps*,
  2025.
- Jason Gregory, "State-Based Scripting in Uncharted 2," GDC 2009; Eric Malafeew, "Data-
  Driven Programming Made Easy," GDC 2005; Chris Butcher, Game Tech 2004.
- Ryan Hipple, "Game Architecture with Scriptable Objects," Unite Austin 2017.
