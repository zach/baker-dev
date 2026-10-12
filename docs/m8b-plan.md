# M8b plan: shapes, `#require`, and packs

Draft for review. Spec §1 ("Directives": `#require`, `(pack ...)`), §5 ("Load-time
checking": the one-way rule), §6 ("Shapes: an optional third library"), and §10 (the M8b
row) are the requirements; `docs/background/harness-tree-vision.md` §6 is the why. The
open questions at the end change the design. Once they are answered, the decisions go into
`docs/decisions.md`, and this file is deleted when M8b is done.

## Scope

From the M8b row: `moira::shape` (shape declarations, shape-aware `check`, the enumeration
API) and `#require`.

Done when:

1. A shape violation in the corpus-style test data is reported at load, with file and
   line.
2. Extra fields warn without failing.
3. A pack loaded through `#require` from three files defines its macros once.
4. A `game`-level pack referencing a `task`-level function produces a one-way-rule
   warning (the spec says *layer*; D80 names it *level*).

Not in M8b: pack metadata beyond `level` and `requires` (`owners`, `for`, `stage` are M10),
the repetition report and machine-readable check output (M10), and the ImGui harness (M9).

## Design

### 1. `#require` (moira::data, the parser)

- `#require path` resolves like `#include` and splices the file the first time a session
  sees its normalized path; later `#require`s of that path, from any file, are no-ops.
  The set lives in the `LoadSession`, beside its defines, and a copied session copies it.
- A recorded load (D72) records `#require`d files as dependencies, and a cache replays
  the set like the defines, so a cache load leaves the session as the text load did.
- `#include` of a file already `#require`d still splices it: the two directives keep
  their own meanings.

### 2. Packs (moira::data records them; moira::script checks them)

- `(pack name (level L) (requires other ...))` as a file's first form declares the file a
  pack. The parser records it in the session, with the file, and drops the form from the
  tree, as it drops directives.
- The ordered list of pack levels, hardest first, is the session's
  (`session.set_pack_levels({"engine", "game", "task"})`; Q3, D80). A pack whose level is
  not in the list is an error at load.
- **The one-way rule** warns when a pack uses vocabulary defined by a softer pack:
  - **Macros, at parse time.** The session knows which pack defined each macro; expanding
    one defined in a softer pack than the file being read is a located warning, kept in
    the session (`session.warnings()`).
  - **Functions, in `check`.** `{func}` definitions are attributed to the pack of the file
    they are in; a call from a harder pack's file to a softer pack's function is a
    located warning in `CheckReport`.
  - Undeclared files are unconstrained.

### 3. `moira::shape`, a third library (depends only on moira::data)

```
(shape boss_phase
  (enter_at float (range 0 1) (kind percent) (desc "HP fraction that starts this phase"))
  (attacks  (list symbol))
  (cadence  float (range 0.2 5)))
```

- **Declarations.** A `Shapes` set collects `(shape ...)` forms from trees it is given:
  field name, type (`int`, `float`, `bool`, `symbol`, `string`, `(list T)`, a nested shape
  by name, or `any`), and the tweak metadata of spec §4 (`range`, `step`, `kind`, `desc`,
  `note`), plus `(optional)`. A malformed declaration is a located error.
- **Which arrays a shape governs** is Q1.
- **Validation.** `check(tree, shapes, report)`: for each governed array, each field
  present has the right type and is in range; each required field is present; an
  undeclared field is a *warning*, never an error (spec §6). Every finding is located
  (file, line, path).
- **Enumeration.** `shapes.each(...)` visits shapes and fields with their metadata, for
  generated tuning panels (M9's harness), as `TweakRegistry::each` does for tweaks.
- **"At load"** is Q2.
- Budget (spec §6): at most 600 lines, outside the core.

## Review (acton-review)

| Piece | Path | Cost |
|---|---|---|
| `#require` | load | one lookup in the session's set per directive |
| pack attribution | load | one pack index per macro definition; one compare per expansion |
| shape validation | load / tool | a walk of the governed arrays; nothing at gameplay time |
| enumeration | tool | none on any hot path |

- Nothing here touches lookup, evaluation, or dispatch.
- Shapes are data in the tree; a `Shapes` set holds `Ref`s to them, so it is freed with
  them.

## Budget

| Area | Estimated lines |
|---|---:|
| `#require`, and its record in caches | 50 |
| Packs: declaration, levels, attribution, macro warnings | 110 |
| One-way rule for functions in `check` | 50 |
| **Core total** | **~210** |
| `moira::shape`: declarations, validation, enumeration | ≤ 600 (outside the core) |

The core goes from 9,850 to about 10,060 of 15,000 (D69).

## Open questions

**Answered 2026-10-12 (D80):** Q1–Q4, each as recommended.

**Q1. Which arrays a shape governs.**
- **Recommended.** By tag, with an optional path. A shape governs every array whose tag
  is its name, anywhere in the tree (`(boss_phase (enter_at 0.5) ...)`), which is how DTA
  data already names its kinds of things. `(shape boss_phase (at bosses.*.phases) ...)`
  narrows it to arrays under a path.
- **By path only.** Each shape names the paths it governs; nothing is matched by tag.
- **By the host.** C++ binds paths to shapes (`shapes.bind("bosses.*", "boss")`); data
  only declares fields.

**Q2. What "reported at load" means.**
- **Recommended.** Config runs a validator after each merge (`ConfigOptions::validate`, a
  callback moira::data defines and moira::shape provides), so every push, reload, and
  restore reports violations, located, through the Config's warning sink. The data
  library still knows nothing about shapes.
- **The host calls it.** `moira::shape::check(cfg.root(), shapes)` from the host's reload
  callback; nothing in Config.

**Q3. The name for pack layers.** Spec §1 writes `cfg.layers({"engine", "game", "task"})`,
but a Config's *layers* are already its stack of files, and `Tier` already names a
function's native or script implementation.
- **Recommended.** *Pack levels*: `session.set_pack_levels({...})`, `(pack name (level
  game))`. Reads like the harness doc's L0–L3, and collides with nothing.
- **As the spec writes it:** `(layer L)`, beside Config's layers.
- **Strata:** `(stratum game)`. Unambiguous, but unfamiliar.

**Q4. Where the one-way rule's warnings go.**
- **Recommended.** Macro warnings, found while parsing, go to a list on the session
  (`session.warnings()`), and Config forwards them to its warning sink; function warnings
  go in `CheckReport`, as a third list beside errors and demand.
- **All in `check`.** The parser records which macros each file expanded, and `check`
  reports both kinds. One place to look, but it records every expansion.
