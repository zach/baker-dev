# SeExpr and Baker

A comparison of Walt Disney Animation Studios' **SeExpr** expression language with Baker
(our reimplementation of Harmonix's DataArray), and what it implies for the spec.

Sources: the WDAS technology page for SeExpr, the project site and user documentation at
`wdas.github.io/SeExpr`, the SeExpr2 API FAQ, the lifetime-qualifier design note, the
programmer tutorial, and Krita's adapted reference manual.

Companion to `halo-tags-vs-dataarray.md`. As there, we are not implementing SeExpr; this
document sharpens Baker by contrast and ends with specific changes to the spec.

---

## 1. Three systems from the same moment

| | **Halo tags** | **DataArray / Baker** | **SeExpr** |
|---|---|---|---|
| First described | Game Tech 2004 | GDC 2005 | Began on *Chicken Little* (2005); open-sourced 2011 |
| Studio | Bungie | Harmonix | Walt Disney Animation Studios |
| What it is | Asset serialization architecture | Config + behavior language | Pure arithmetic expression language |
| Primary editor | Artists and designers, via generated GUI | Programmers, via text | Look-dev artists, FX artists, TDs, via text + widgets |
| Syntax | None (binary, GUI-edited) | S-expressions | Infix, C-like, `$variables` |
| Typing | Static, from C structs | Dynamic, checked at access | Static (since v2), checked at prep |
| Side effects | n/a | Everywhere — that's the point | None; all functions are pure |
| Hot path | Level load | Lookup and dispatch | One expression × millions of points |
| Escaped its studio? | No | No | Yes — RenderMan, Clarisse, Maya XGen, Krita |

The third column changes the shape of the comparison. Halo and DataArray are both
*engine-shaped*: their value is entangled with the game that hosts them. SeExpr is
*function-shaped*: a pure mapping from host-supplied inputs to an output, with nothing to
say about objects, events, time, or ownership. That difference explains almost everything
below, including why SeExpr is the only one of the three that became an industry standard.

---

## 2. The execution model is the root difference

A SeExpr program describes computation at **one point** of a large domain, and the host
runs it over every point — every pixel of a texture, every vertex of a hair, every particle,
every frame. The design notes are explicit that this "foreach point" model is the mental
model for the whole language.

That single fact drives SeExpr's entire architecture:

- **Pay once at prep, recoup over millions of evaluations.** Version 2 added static types
  and an LLVM backend specifically because each expression is recomputed many times; the
  FAQ notes that code generation overhead makes a one-shot evaluation *slower* under LLVM,
  and that the win only appears at volume. Disney cites over 2,000 expressions in their
  shared library alone.
- **Errors at prep, not at evaluation.** Moving type errors to prep time means an artist
  sees the problem once instead of once per point.
- **Lifetime qualifiers.** Values are classed as *constant* (fixed for all evaluations),
  *uniform* (fixed across a range — one hair's color is constant along that hair), or
  *varying* (changes per point). The compiler splits each program into three sections and
  hoists work outward, so a per-hair computation runs once per hair rather than once per
  point on it. Some functions can demand constant or uniform arguments, which lets them
  ban expensive operations like string formatting from the per-point loop.

Baker's hot path is the opposite shape. A config value is read many times but *computed*
once per load. A command executes when an event fires, not across a domain. So Baker
spends its budget on making lookup and dispatch cheap — a 16-byte node, inline arrays,
pointer-comparison symbols, an allocation-free call path — and has no use for a compile
step, because there is nothing to amortize it over.

Both systems are optimizing correctly for their workload. The lesson is to be precise
about which workload you have: if anyone ever proposes JIT-compiling Baker scripts, the
first question is how many times each script runs per load, and the answer will almost
always be "not enough."

---

## 3. Purity versus effects

SeExpr's design note states it in passing: currently, all functions have no side effects.
That one sentence is load-bearing. It is what makes constant folding sound, uniform
hoisting legal, and per-thread evaluation (via SeExpr2's *variable blocks*, one per
thread) trivially safe. Purity is why SeExpr can run in a renderer's inner loop on many
cores.

Baker is effectful by design. Commands play sounds, add score, send messages, spawn tasks,
mutate variables. That is the whole reason DataArray grew scripting — the data alone was
"too fragile or limited," in Malafeew's words. And its lazy-evaluation trick, where `if` is
just a function, *depends on* evaluation being observable: the reason not to evaluate the
untaken branch is that evaluating it would do something.

These are not competing answers to one question. SeExpr answers *"what value goes here?"*
Baker answers *"what happens when this occurs?"* A system can be very good at both only by
keeping them separate — which is roughly what Disney did by keeping SeExpr pure and letting
each host application own its own effects.

---

## 4. Syntax and audience

SeExpr is infix arithmetic with C-like operators, vector literals, a ternary, `if`/`else`,
compound assignment, and a pipeline operator (`->`) that threads a value through a chain of
functions. Scalars promote to vectors component-wise, so every scalar function works on
colors for free. This is syntax for people who think in math and shader code, and that is
exactly who uses it: look-development and effects artists, character TDs.

DataArray is s-expressions, which are trivially parsed, trivially generated, and naturally
hold *data and code in the same tree* — the property that lets a config file carry
`{play "bonk.wav"}` inside an object definition. That is a programmer's trade: an artist
would find `{+ $a 1}` hostile, and Malafeew said directly that scripts were mostly written
by programmers.

Notably, SeExpr is the most artist-facing of the three systems despite being the only one
that is a *language* in the full sense. Halo put artists in front of a generated GUI and
kept them away from syntax entirely. SeExpr put artists in front of syntax — but syntax
they already knew, from math class and from RenderMan.

Baker should not chase SeExpr's audience. Its syntax is right for its users. But the
contrast is a useful check on any future proposal to "make Baker friendlier": the friendly
version of this idea already exists, and it is a different tool for a different job.

---

## 5. UI metadata: the third independent confirmation

SeExpr's editor reads widget ranges from **comments** on assignment lines. Writing
something like `$freq = 4; # 1, 20` tells the editor to present a slider bounded 1–20; a
three-component vector bounded 0–1 is presented as a color picker. Host applications like
Maya's XGen add sliders automatically for local variables declared this way.

Put that beside the other two:

- **Halo** generated its editing UI from *code* definitions. Butcher named the cost
  explicitly: designers could not annotate fields with notes like "good values are
  0.2–0.5."
- **DataArray** put range, step, and description *in the data*, as structured children of
  the value: `(width 2 (range 0 10) (step 1) (desc "Box width"))`.
- **SeExpr** put ranges *in the data*, as a comment convention next to the value.

Two of three teams, independently and within a year or so, decided tuning metadata belongs
beside the value in the file the person tuning it is editing. The one team that put it in
code reported the limitation unprompted. That is about as strong a design signal as
historical comparison can produce, and Baker's choice — data-side metadata, overriding
code-side — is now triply supported.

The two data-side approaches differ instructively. SeExpr's comment convention costs the
language nothing — the parser ignores comments, so metadata has zero semantic weight — but
it is an invisible, unvalidated micro-format living inside free text. DataArray's
structured children are real data: parseable, validatable, queryable by tooling, and
round-trippable by a pretty-printer. Baker has the better structure. SeExpr has a nicer
idea we lack: **inferring presentation from shape**. A three-float value ranged 0–1 *is*
probably a color, and a tool can say so without anyone declaring it.

---

## 6. Embedding and extension

SeExpr's embedding model is subclass-and-override: derive from `Expression`, override
`resolveVar` and `resolveFunc` to hand the expression host-owned variable and function
objects, set the variables, call `evaluate`, repeat. Custom functions are classes with
separate prep (type-check) and evaluate phases, and can be loaded from shared-library
plugins at runtime.

That is more ceremony than Baker's binder, where `ctx.func("+", [](int a, int b){...})`
deduces everything from the signature. But it buys two things worth noting:

- **Per-expression variable resolution.** Each SeExpr expression is its own context. The
  same expression text can mean different things in different hosts, and a single host can
  run thousands of independent expressions without them sharing state. Baker's variables
  are global to a `Context`, with dynamic scoping — deliberately so, and correct for a game
  — but it means Baker expressions are not hermetic in the way SeExpr's are.
- **A prep phase for functions.** Every SeExpr function gets to inspect its argument types
  before any evaluation happens. That is where SeExpr catches a wrong-arity or wrong-type
  call up front.

Baker cannot adopt per-function prep without giving up laziness — prep implies knowing
argument types, and lazy arguments don't have types until accessed. But the binder already
knows every registered function's signature, which means a large class of errors (wrong
arity, a literal string where an int is required) *can* be caught at load time without
evaluating anything. See §8.

---

## 7. Why SeExpr escaped and DataArray didn't

DataArray shipped in five years of Harmonix games and two decades of downstream Rock Band
content, and was never released. SeExpr was released under Apache 2.0 in 2011 and adopted
by Pixar's RenderMan, Isotropix Clarisse, Autodesk Maya's XGen, and Krita (via the KSeExpr
fork).

The difference is not quality. It is shape. SeExpr is a pure function from host-supplied
inputs to an output, and it requires nothing of its host except variables and an evaluate
loop. Any application that computes values over a domain can adopt it in an afternoon.
DataArray's value lived in its *object and messaging system* — `DataObject::Handle`, the
handler tables, script-side classes, the task scheduler — which are engine architecture, not
a library. You cannot adopt them without adopting a way of building your program.

For Baker this is a direct, practical lesson. Baker has both shapes inside it:

- A **data layer** — nodes, arrays, parser, lookups, merge, config, tweaks, binary cache.
  This is SeExpr-shaped: portable, host-agnostic, adoptable by anyone who has a config file.
- A **script layer** — evaluation, functions, objects, messages, tasks. This is
  engine-shaped and only valuable to someone willing to build around it.

If those two layers are cleanly separable, the data layer can be useful to a far larger
audience than the full system. If they are entangled, Baker's audience is limited to people
who want all of it. Section 8 makes the separation a hard requirement.

---

## 8. What this changes in the spec

Applied to `baker-spec.md`:

1. **Two libraries, strictly layered.** `baker::data` (nodes, symbols, arrays, parser,
   writer, lookup, merge, config, tweaks, binary cache) builds and is fully usable with no
   dependency on `baker::script` (evaluation, functions, objects, messages, tasks). A
   program that only wants hot-reloadable config pays for nothing else. In the data layer,
   `Command` and `Var` nodes still parse and round-trip but are inert: typed access to one
   is a clean error saying the script layer isn't linked. This is SeExpr's portability
   lesson, and it is what makes Baker adoptable by people who would never want an object
   system.

2. **A load-time check pass.** `baker::check(array, ctx)` walks a loaded tree *without
   evaluating anything* and validates every command whose head resolves to a registered
   function: arity against the binder's known signature, and literal argument types where
   the argument is a literal. Report all errors, located, in one pass. This is SeExpr's
   prep-time-errors lesson adapted to a lazy language — we can't type-check evaluated
   arguments, but we can catch the errors that are knowable statically, which in practice
   is most of them. Optional; wired into config load behind a flag; run in CI against the
   corpus.

3. **Presentation hints, inferred where possible.** Add an optional `kind` metadata entry
   (`color`, `angle`, `percent`, `path`, …) alongside `range`/`step`/`desc`/`note`, and have
   the registry infer `color` for 3- or 4-element float arrays ranged 0–1 when `kind` is
   absent. Tools use it; the library attaches no behavior to it.

**Considered and deliberately not adopted:**

- *Static typing and a compile step.* SeExpr's justification is millions of evaluations per
  expression. Baker's scripts don't run that way, and laziness forbids it anyway.
- *Lifetime qualifiers / epoch-cached pure commands.* The interesting translation is that a
  command whose inputs are all tweaks is "constant until reload," and could be memoized on
  the reload epoch if its functions were marked pure. It's a real idea, but it adds a purity
  annotation to the binder and a cache to evaluation for a workload Baker doesn't have.
  Record it in `dev/decisions.md` as a known extension point rather than building it.
- *Native vector types.* SeExpr's scalar-to-vector promotion is elegant for shading math.
  Baker values like `(clear_color 1 1 0)` are just arrays, and `Convert<Vec3>` is the right
  way to read them. A vector node type would cost node-size headroom for no lookup benefit.
- *Comment-embedded metadata.* Our structured form is better. Keep it.
