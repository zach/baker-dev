# Layers inside a file: a design note

*The `inline-layers` syntax idea, prototyped on the branch
`claude/fervent-cray-l1kyf0-syntax-inline-layers` behind `BAKER_INLINE_LAYERS` (off in the
default build, on with `BAKER_BUILD_EXPERIMENTS`). Recorded as D227 on the branch. Scoped
as the judges asked: the named block with `(when COND)` and `(disabled)` only, read from
text only; groups, windows, `dev_only` and a cache entry are estimated, not built.*

## 1. Thesis

Today a layer is a file. A platform variant is a second file and a `push_layer` the host
makes on that platform (`examples/config/main.cpp:60-61`); an A/B experiment is two files
and two `toggle_layer` calls (`docs/cookbook.md:214-218`); and a conditional inside a file
is `#ifdef`, a token-level region the parser resolves before any tool sees it, so neither
the Layers panel, nor `explain`, nor write-back can name it, fold it, or toggle it. This
idea lets a file declare layers inside itself: `(#layer (handheld (when HANDHELD))
(renderer (shadows 0)))` is a bracketed block that the parser lifts out of the file's tree
into the session, and that a Config pushes directly above the file as the sub-layer
`default:handheld`. It is on when its condition holds for the session's defines, off when
its head says `(disabled)`, and a person can toggle it either way. It is named by `source()`
and `explain`, written back into by its own extent, reloaded, placed and removed with its
file.

It is the right radical move for the pains it names because it uses the one mechanism
Baker already has for "a difference that applies on top": the layer (spec §4, "Personal,
experiment, and scratch layers", and D32's provenance from the layer trees). A conditional
stops being text the parser deletes (spec §1's `#ifdef`) and becomes data with a name, a
line and a toggle, so the variant a platform or an experiment makes is visible to every
tool the spec built for layers: the Layers panel, `layer_effect`, `diff`, and the policies
for received layers (D161). It serves the guide's "one file serves every target" and spec
§4's A/B comparison during polish (try the handheld's values on a PC, or the other arm, with
a click, `explain` naming it) without a second file per variant.

The prototype says it costs more than the judges' bar (221 core lines as adopted, 265 with
the switch's guards, against a bar of 150), that it replaces only the share of today's
conditionals that wrap whole tagged forms (16% of the 1,024 regions in the 464 raw files),
and that the Config half fits the layer machinery through one "a file and its blocks are
one unit" rule, as hoped.

## 2. The syntax

### Grammar

```
BLOCK   := '(' '#layer' HEAD FORM* ')'
HEAD    := NAME | '(' NAME OPTION* ')'
OPTION  := '(' 'when' COND ')' | '(' 'disabled' ')'
COND    := DEFINE | '(' 'not' COND ')' | '(' 'and' COND COND* ')' | '(' 'or' COND COND* ')'
NAME    := a symbol, bare or quoted          DEFINE := a symbol
FORM    := any data node: a tagged array, (key #remove), a value
```

- **Tokens.** None new. `#layer` is an atom starting with `#`, which the lexer already makes
  a `Tok::Directive` (`src/data/parse.cpp`, `lex_atom`). The parser takes it only as the
  first token inside `(` (the frame is an array and holds nothing yet); anywhere else it is
  the located error `#layer is written first inside '('`. Before the prototype it was
  `unknown directive '#layer'`, so no file that read before reads differently.
- **The head** is read unexpanded, as `#ifdef`'s name is (`parse.cpp`, `conditional`): a
  macro named `HANDHELD` does not turn `(when HANDHELD)` into `(when 1)`. An option outside
  the closed set, including `(group ...)`, `(from ...)`, `(until ...)` and `(dev_only)`, is
  the located error naming the set. Options live only in a head, so Ledger's milestone field
  `(when {>= ...})` (`stacks/ledger/data/default.dta:25`) is data, untouched (tested).
- **COND** is evaluated when the block closes, against the session's defines at that
  point, with `LoadSession::is_defined`, exactly as `#ifdef` reads them: the host's defines
  and the `#define`s read above it in the load.
- **Forms** are ordinary data. A directive inside a block other than `#remove` is the
  located error `a layer block holds data: no #include inside (#layer ...)`, so a block
  never includes a file, defines a macro, nests a conditional or another block, or collects
  an `#autorun`.
- **Position:** at a file's top level, or inside arrays that all have a symbol tag. Inside,
  the forms are wrapped in the enclosing tags from the root, so `(renderer (fog 1) (#layer
  hh (shadows 0)))` is the file `(renderer (fog 1))` and the sub-layer `(renderer (shadows
  0))`. Inside a command, a property, a macro body or an untagged array it is a located
  error. Wherever it is written, a block applies above its whole file.
- **Names** are unique in a load (a file and what it includes), since each names a layer;
  a second one is the located Refused `block 'x' is declared twice (first at t.dta:1)`.
- **Who reads blocks.** A session takes blocks only with `LoadSession::layer_blocks`, which
  a Config sets for its layers and its text checks. Off (the default), a block is the
  located error `a layer block is read only as a Config's layer`: a reader that never asks
  for `blocks()` (scenarios, objectives, the console's requests, a script load, a host's
  own `read_file`) would otherwise drop the block's data in silence.

### What it maps to

Nothing new in the tree: Node, Array and their layouts are unchanged. The forms become a
root Array made in the load's arena (D64) with the block's file and line; each wrapper
carries its enclosing array's line. The new thing is a session record, as packs and
origins are (D82, D91): `LoadSession::blocks()` returns `Block {name, file, line, disabled,
when, holds, tree}`, `when` being the COND as written and `holds` its value. The parent
array gets nothing, as `#autorun` leaves nothing in the tree. In edit mode the forms keep
their spans as today, and each block tree (and wrapper) is recorded with the block's extent,
brackets included (D33, D128).

### Today's forms, and the edge cases

| Form | Under blocks |
|---|---|
| Arrays, commands, properties, variables, symbols, strings, ints, floats | Unchanged, inside a block as anywhere. |
| Quoting | A quoted name is a name: `(#layer 'arm b' ...)`. A string is not. |
| Numbers | A head that is a number is refused: `(#layer 3 ...)`. |
| Comments | Anywhere, as today; the converter keeps them. |
| An empty block | `(#layer x)` is an empty layer. |
| `#remove` | Allowed in a block, with D143's rules: the block's layer removes the key. |
| Macros | A block's forms expand as anywhere in the file, with the macros above it; its head never. |
| `#ifdef` | Kept. A block inside an active region is read; inside an inactive one, skipped. No `#ifdef` inside a block. |
| `#include` | Not in a block. An included file's blocks are its including layer's, `LAYER:NAME`. |
| Packs | A block in a pack file is a layer of that file; the one-way rule applies to its macros (D82). |
| Shapes | A block may hold `(shapes ...)`: layers merge shapes like any data. |
| Scenarios | Scenario and objectives files are read without `layer_blocks`: a block there is an error, never lost. |
| The project file | Unchanged; blocks never appear in its `(layers ...)`. |

### The corpus before and after

`examples/config/data/default.dta:2-4`, `handheld.dta:2-3` and the second push at
`examples/config/main.cpp:61` become one file
(`experiments/syntax-inline-layers/samples/config/default.blocks.dta`):

```dta
(renderer
   (shadows 1 (desc "Cast shadows"))
   (clear_color 0.1 0.1 0.2 (desc "Background")))
...
; The handheld platform: only what differs, on when the host defines HANDHELD.
(#layer (handheld (when HANDHELD))
   (renderer (shadows 0)))
```

`tests/corpus/ui_menu.dta:39-47`, nine lines of `#ifdef HAS_VOCALS ... #else ... #endif` and
`#ifndef DEMO_BUILD ... #endif`, become (`samples/ui_menu/ui_menu.blocks.dta:39-43`):

```dta
(vocals_panel (lanes 0))
(#layer (vocals (when HAS_VOCALS)) (vocals_panel (lanes 3) (pitch_arrow TRUE)))

(#layer (store (when (not DEMO_BUILD)))
   (store_button (text "Store") (select {ui goto_screen store_screen})))
```

The `#else` branch stays as the default because the defined branch sets every key it sets
(`lanes`); the converter applies that rule and the check confirms it under all four define
sets.

The language guide's example, `docs/language.md:155-161` on the session branch,
`(renderer #ifdef HANDHELD (shadows 0) #else (shadows 1) #endif)`, converts to the file
`(renderer (shadows 1))` and the block `(#layer (handheld (when HANDHELD)) (renderer
(shadows 0)))` (`experiments/syntax-inline-layers/tests.cpp`, "a region inside a tagged
array").

The cookbook's A/B (`docs/cookbook.md:214-218`: two files, `push_layer` and two
`toggle_layer` calls) is shown on a real file, Duel's
(`stacks/duel/data/default.dta`, `samples/duel-ab/default.blocks.dta:49-51`):

```dta
; A/B on the weight of a hit: one arm on, the other a click away in the Layers panel.
(#layer hit_pause_heavy (duel (hit_pause 9)))
(#layer (hit_pause_light (disabled)) (duel (hit_pause 4)))
```

The live-ops overlay's "Ships Friday 17:00 UTC; roll back Monday"
(`examples/live-ops/overlays/2026-10-17-double-xp.dta:1`) stays a comment: windows are out
of scope.

## 3. What it keeps and what it changes

- **Lexer:** unchanged. **Parser:** a FrameInfo flag (`block`), the head, the condition, the
  re-wrap, the "holds data" rule and the session record: 129 lines of `parse.cpp` and 12 of
  `parse.hpp` as adopted. A file with no block takes one extra test per directive token, one
  per macro candidate (`in_head()`, only when the session has defines), and one per close
  (`f.block`); the block's close is out of line and cold, so `close()` still inlines into
  the parser's loop (section 4).
- **The printer:** unchanged. A block never lives in a tree, so `to_dta` has nothing to print;
  the experiment's `to-dta` writes blocks back as `#ifdef` regions, and a `write_stack`
  tool that prints a Config's blocks is deferred.
- **Write-back** (D34, D132, D183, D226): the mechanism is unchanged. A block is a layer
  whose map is its file's and whose root is recorded with the block's extent, so
  `write_back`/`write_back_to` into a block replaces its token or inserts inside its extent
  by the existing deepest-ancestor rule. Two lines change: a layer "has no source file" is
  now told by its `file`, not its `path`, and a new top-level form goes at the end of the
  file only for a file layer, not a block. Promote out of a block into its own file is the
  self-write the planner already treats as a virtual re-read (tested). **Promote into a
  block is refused** (`promote into a layer block is not built`): its virtual re-read would
  have to re-read the file and pick the block out, about eight lines more; `write_back_to`
  each path works.
- **Edit-mode source maps** (D33, D128): unchanged in kind; the parser records each block
  tree and wrapper with the block's extent. The fuzz property checks every recorded span lies
  in the text, for block trees too.
- **The tolerant structure read** (D220, `read_structure`): unchanged. A block is a bracket
  whose first token is the directive `#layer`; an editor folds or labels it from that.
- **Provenance and explain** (D32): unchanged. A block is a layer with its own tree, so
  `source()` answers `default:handheld` and the block's line, `explain` lists it (`Disabled`
  when off), and `layer_effect`, `diff`, `layer_root`, `set_layer_enabled` and
  `toggle_layer` take its name (tested against `examples/config` run both ways).
- **Layers, placement and `#remove`:** one rule, "a file and its blocks are one unit,
  blocks directly above their file", enforced in four places: `reread` backs up from a
  block to its file and remakes the file's blocks from what it now says (and keeps a host's
  toggle unless the head changed); `placement` (D144) snaps "below a block" to below its
  file and "above a file" to above its last block; `remove_layer` and `pop_layer` take the
  run of blocks with the file (removing a block alone is refused: disable it); `push_read`
  builds the unit and refuses a block name already in the stack. Snapshot and restore copy
  the stack and needed nothing. Merging needed nothing: a block is one more enabled layer.
  `#remove` in a block works as in any layer.
- **`check` and policies** (D161, D165, D167): a received text's blocks are each a
  candidate of its own, `FILE:NAME`, under the push's policy, every block whether enabled
  or not, since a person may enable it later without a check. `check_layer_text` and
  `LayerChecker::check_text` now share one reader that reports the blocks (the checker's
  path missed them in the first cut); a report that still holds is the push's verdict,
  blocks included. The demand report and the reference JSON are unchanged; check-json's
  layer names may now read `file:block` (a 1.4 minor, not made).
- **Shapes:** unchanged; a shape validator sees the merged tree with the enabled blocks.
- **The console's requests:** unchanged. They are read without `layer_blocks`, so a block
  in a request is an error. Layer names in replies (`source`) may read `default:handheld`.
- **The project file:** unchanged.
- **The binary cache:** unchanged format, `kVersion` stays 6. `bytes_of` refuses a load
  whose session read blocks, so neither a Config's cache nor `baker_bake` can write a cache
  that drops them; a Config notes the file through its warning handler and reads it from
  text. A `blocks` meta entry waits for the next format bump (about 28 lines, section 4).
- **The docs:** `docs/language.md`'s "Platforms and builds" teaches blocks first and keeps
  `#ifdef` for what a block cannot say; the directive table lists `(#layer ...)`.
- **The corpus:** unchanged. Of its 68 files only `ui_menu.dta` has conditionals; no stack
  has one.

## 4. Cost, measured

**Core lines** (`tools/count_lines.py`, against the session branch's 15,190 of 15,500):

| | with the switch's guards | as adopted (no switch) |
|---|---:|---:|
| `parse.cpp` | +147 | +129 |
| `parse.hpp` | +18 | +12 |
| `config.cpp` | +66 | +54 |
| `config_check.cpp` | +23 | +19 |
| `config_impl.hpp` | +4 | +2 |
| `config_write.cpp` | +2 | +2 |
| `cache.cpp` | +5 | +3 |
| **total** | **+265 (15,455)** | **+221 (15,411)** |

The bar was 150. The Config half (80 adopted) came in under the brief's 110; the parser
half (141) came in well over its 95. Of the parser's lines, the in-array re-wrap is about
16 (the ancestor check and the wrapper loop), the head's grammar and its located errors
about 35, the condition evaluator 14, `layer_blocks` 6, and keeping the hot path as it was
about 12 (the cold attribute's macro, which adoption would share with `baker::script`'s
`BAKER_COLD`, and a cold path that calls none of the hot path's helpers). A top-level-only
block would be about 205.

**The full form**, estimated, not built: groups of exclusive arms (`(group NAME)`, and
`set_layer_enabled` turning the others off) about 16; windows (`(from ...)`/`(until ...)`,
an ISO-8601 subset, evaluated against the host's clock per frame) about 50 in the core and
10 of the host kit's 39; `(dev_only)` about 4; a `blocks` cache entry (written in the meta,
replayed, `kVersion` 7) about 28. The full form is about 319 core lines as adopted: more
than the whole remaining headroom (310), as the cost judge said.

**Parse speed** on block-free text, against the session branch, same machine:

`tools/bench_compare.py --bench parse layers --runs 5` (Release, five alternating runs a
side; medians with min–max):

| benchmark | row | `8f15684` | blocks on | change |
|---|---|---:|---:|---:|
| parse | parse: 4.2 MB in | 26.6 ms (25.3–32.1) | 27.8 ms (24.3–34.4) | +4.4% |
| layers | read_string | 68.3 us (62.2–69.9) | 69.9 us (63.3–92.8) | +2.3% |
| layers | push_layer (tree), no policy | 88.0 us (79.8–90.0) | 81.9 us (79.7–140.4) | −6.9% |
| layers | push_layer_text, no policy | 156.1 us (143.4–160.8) | 157.4 us (142.9–164.3) | +0.8% |
| layers | push_layer_text, policy | 468.1 us (466.6–519.5) | 470.2 us (462.7–498.3) | +0.4% |
| layers | check_layer_text | 379.8 us (373.7–411.8) | 372.8 us (370.3–409.0) | −1.8% |
| layers | LayerChecker::check_text | 405.8 us (375.4–411.7) | 352.7 us (350.5–381.8) | −13.1% |
| layers | push_layer_text, policy, report held | 158.3 us (145.3–160.6) | 147.3 us (144.3–161.0) | −6.9% |

No row's ranges are apart, on this shared machine (eight prototypes building beside it):
the moves, both ways, are noise. `8f15684` is the session branch, whose code is the fork
point's (`3817213`); bench_layers has no reload row, so reload is measured by
`bench-layers` below.

**Instructions** (`tools/count_instructions.py`, cachegrind, Release; the new side built
with `BAKER_INLINE_LAYERS=ON`):

| case | `8f15684` | blocks on | change |
|---|---:|---:|---:|
| parse (a 16 KB document) | 1,845,499 | 1,844,556 | −0.05% |
| lookup | 203 | 203 | 0.00% |

The first cut cost +3.45% (1,909,140): `close()` had grown past GCC's inlining into the
parser's loop, and `truncate()`, `record()` and `vector::resize()` gained call sites in the
block's close and went out of line (cachegrind per function: the loop 116.9 M instructions
became 70.0 M plus `close()` 46.3 M, `truncate()` 20.8 M and `record()` 5.0 M over 800
documents). Now `close()` tests only `f.block`, the block's close is `noinline` and cold and
calls none of the hot path's helpers (it erases and inserts, which nothing else calls), and
`in_head()` reads the frames, so no close resets it. A lesson for any hook in the parser:
measure instructions, because the cost of a hook is in what it does to inlining, not in its
own test.

**The experiment's reader** is Baker's: `baker_syntax_inline-layers bench` compares Baker's
parser on the original against the same parser on the converted text (best of five, runs
interleaved).

- The generated 4 MB text (block-free, so the same bytes both sides): 167.0 against 163.9,
  169.8 against 175.0, and 175.4 against 162.2 MB/s, ratios 1.019, 0.970 and 1.081. Equal
  bytes read by one parser move by up to 8% between runs here: that is this machine's
  noise floor for MB/s, and why the instruction count is the number that decides.
- `tests/corpus/ui_menu.dta`, its two `#ifdef` regions (1,275 bytes) against the converter's
  two blocks (1,316 bytes): 116.5 against 115.3 MB/s, ratios 1.043, 1.039, 1.048, 1.032. A
  block builds the tree of a branch `#ifdef` would skip when its define is not set; at
  11 µs a read, the 4% is a few hundred nanoseconds.
- The stacks' files have no conditionals, so their conversions are their originals; the
  harness's `bench <file>` reads with a plain session, which their packs' levels refuse,
  so they are not in this table.

**A file with k blocks is k+1 merges.** `bench-layers k`: a Config pushing the 4 MB generated
text as one layer, against the same forms with the second half in k
blocks:

| k | one layer | file and k blocks | ratio (three runs) |
|---:|---:|---:|---|
| 1 | 35.7 ms | 35.7 ms | 1.001, 1.021, 1.063 |
| 8 | 36.0 ms | 41.3 ms | 1.146, 1.170, 1.120 |
| 64 | 35.3 ms | 70.0 ms | 1.980, 2.038, 1.896 |

The text has 16,558 top-level forms, so each merge rebuilds a wide root: a block costs about
0.6 ms here, the price of one more layer over a 4 MB stack (D75), paid at push and at
every full merge (a reload, a toggle). A platform block of a few keys over a real file is
far smaller; a file with dozens of blocks over a wide root is where it shows.

**Lines and bytes of the samples** (`experiments/syntax-inline-layers/samples/`):

| Sample | Before | After |
|---|---|---|
| examples/config | 2 files, 10 lines, 344 bytes, and a host `push_layer` on a handheld | 1 file, 11 lines, 399 bytes |
| tests/corpus/ui_menu.dta | 49 lines, 1,275 bytes; the conditionals 9 lines | 45 lines, 1,298 bytes; the blocks 3 lines |
| stacks/duel + A/B pair | 3 files, 52 lines, 3,346 bytes, two pushes and two toggles | 1 file, 52 lines, 3,325 bytes |

Lines go down where an `#else` collapses into a default (ui_menu, −4); bytes go up slightly
where the condition moves out of the host into the file (config, +55 bytes: the head
`(#layer (handheld (when HANDHELD))` is the price of a condition the data now states).

**Allocation and layout.** Node and Array are unchanged. `LoadSession::State` gains an
empty `Vec<Block>` (no allocation until a block is read; one session per load, never on a
hot path, so D62's MSVC note does not bite per frame). `FrameInfo` and `LayerState` each
gain a `bool` in existing padding; the parser two `uint32_t`. A block's tree is made in its
load's arena. A file with k blocks keeps k more `LayerState`s and makes k more merges per
full merge, which `bench-layers` measures.

## 5. Payoff, measured where possible

- **A designer** sees a platform's or an experiment's difference as a named layer in the
  panel and toggles it: the handheld's values on a PC, or the other A/B arm, with no file
  and no code (tested: `set_layer_enabled("default:handheld", true)` brings `shadows 0`;
  Duel's arms swap with two clicks). `explain` names the block and its line. A conditional is
  no longer text the parser deletes: in ui_menu the nine lines of `#ifdef` are three lines
  of blocks, and the `#else` duplicate (`(vocals_panel (lanes 0))` restated in a branch)
  becomes the one default.
- **An engineer** drops a file and a `push_layer` per platform variant (examples/config:
  one file and one call fewer) and two files and their pushes per A/B (Duel: three files
  to one). The host still defines `HANDHELD`; it no longer chooses files.
- **A tool** gets conditionals as data: `LoadSession::blocks()` lists each with its
  condition as written (`when`) and its value, so a checker can say which blocks a define
  set turns on, and the converter and its check are about 600 lines of experiment code, not
  a second parser. Received overlays may carry blocks under the same policy, each checked.
- **An agent** can propose a variant as a block in the file it already edits, toggled for
  comparison by name, and see it in `explain`, instead of creating files and asking for code.
- **Errors a person can no longer make:** pushing the handheld file on the wrong platform
  (the condition is in the data, beside what it guards); an `#ifdef` branch silently
  shadowed by an earlier key in the same array (a block always applies above the file: the
  check found this pattern, see below); a misspelled block option (a located error, where a
  misspelled `#ifdfe` is too, but `#ifdef HANDHLED` is silently false either way).
- **Diffs:** turning a platform value on or off is a one-line head edit
  (`(disabled)`), not a file add or a code change.

**What it does not replace.** `survey` over the 464 raw DTA files of D101's gate (RB1 PS2
304, GH2 PS2 Deluxe 120, GH2 360 DLC 40; counted outside the repository, nothing kept):
112 files have conditionals, 1,024 regions over 28 defines.

| Region | Count |
|---|---:|
| encloses whole tagged forms, at the top level or in tagged arrays: **converted** | 162 (96 top level) |
| inside a `{command}` or `[property]` (script) | 317 |
| nested in another region | 356 |
| holds a directive (an `#include`, a `#define`, an inner region) | 73 |
| holds bare values (a list's elements) | 76 |
| inside an untagged array | 38 |
| spans a bracket | 2 |

So 16% of the raw corpus's regions become blocks, and only 2 of 1,024 are the form blocks
can never express in principle (a bracket opened in one branch and closed in another).
The rest are script, nesting, directives and values, which a data layer is not for. Of the
36 files with a converted region, the check merged 29 (7 include files the dump lacks or
lays out elsewhere) under every define set: 10 equal, 15 equal up to key order (a block's
new keys follow the file's), and 4 differ, each only when two or more defines are set
together, as two platforms at once, which no build does. There two regions both read in
the original leave a repeated key whose first occurrence wins, where as blocks the later
one wins.

## 6. Objections

- **"Cost per byte" (Acton).** The block-free parse pays one predictable branch per
  directive, close and macro candidate; section 4 measures noise-level change. A file with
  blocks pays a merge per block at every full merge, which is what a layer costs and what
  the file-per-variant design already paid. Conceded: a block-free *game* now carries
  221 lines of a feature it may never use, unless the switch stays; a shipped build that
  wants none could keep `BAKER_INLINE_LAYERS` off, which Baker's "every costly feature off
  by a switch" rule allows.
- **"A second parser."** There is none: the lexer is unchanged and the parser gains one
  frame flag; the converter is an experiment tool. Conceded: there are now two conditional
  forms, `#ifdef` (positional, any tokens) and blocks (layered, data only), with different
  precedence, and the guide must teach when each applies. The check's four differing raw
  files show the difference is real, if only under define sets a build never uses.
- **"Build time, not run time."** A condition is resolved when the file is read, as
  `#ifdef` is; what moves to run time is only the toggle, which is the point and is dev-only
  in practice (shipping refuses `set_layer_enabled`). The data to decide what ships is the
  same.
- **"Solving a problem you don't have."** Conceded in large part. No stack writes a
  platform variant or an A/B today; Baker's corpus has one file with conditionals; the
  examples' one platform file is a teaching example. The raw corpus shows the problem
  exists at scale in a shipped game (112 of 464 files), but mostly in forms blocks do not
  take.
- **The DTA heritage.** The corpus reads unchanged (the round trip passes on all 68 files,
  and `#layer` was an error before). Modders and the raw corpus keep `#ifdef`; nothing is
  retired. A modder's text overlay may hold blocks under the received-layer policy, each
  checked. The cost is a form the original never had, in a language whose value is partly
  that it is the original's.
- **Silent loss.** A block is data that only a Config reads; any other reader would drop
  it. Answered with `layer_blocks`: off, a block is an error.

## 7. Migration

Nothing has to migrate: `#ifdef` stays, and a file without blocks reads as before. Files
move one at a time, by hand or with the converter: `baker_syntax_inline-layers to-new f.dta`
turns each convertible region into a block (one per condition, after the file's forms,
comments kept) and names the regions it leaves; `compare f.dta f.blocks.dta` merges both
under every define set. Both forms live side by side for good unless the gate decision
below retires `#ifdef`.

- **The one-way rule and packs:** a block is read in its pack's file, so its macros obey
  D82 as the file's do; nothing new.
- **Layers:** a file's blocks are its layer's, named `FILE:NAME`, so a stack's layer list
  grows by one per block; a host that matched layer names exactly is unaffected, one that
  splits on `:` must not.
- **Live-ops overlays:** a received text may hold blocks, each checked under the overlay's
  policy as a layer of its own; one disabled block per alternative is the A/B a live team
  can flip without a second push. Windows (`Ships Friday ... roll back Monday`) stay
  outside the data until the owner decides on clocks.
- **Caches:** a file with blocks is read from text until a format bump adds a `blocks` meta
  entry; a bake of such a file is refused with that reason.

## 8. What the prototype proved, and did not

**Built:** the parser (`src/data/parse.cpp`, `include/baker/parse.hpp`), the Config's
sub-layers (`config.cpp`, `config_impl.hpp`, `config_write.cpp`), checks
(`config_check.cpp`), the cache refusal (`cache.cpp`), the fuzz property and dictionary
(`fuzz/`), the guide (`docs/language.md`), and `experiments/syntax-inline-layers` (the
converter, `to-dta`, `check`, `compare`, `survey`, `bench`, `bench-layers`, three samples).

**Passed:**
- `baker_data_tests` (258 cases, 93,623 assertions), 19 of them on blocks: the head and
  its options; conditions; the re-wrap; `#remove` kept and every other directive refused;
  located errors; inactive regions; a reader without `layer_blocks` refusing; handheld as a
  block equal to handheld as a file (get, the merged root, `source`, `explain`,
  `layer_effect`, `diff`, `layer_root`); ui_menu's conditionals as blocks per define set; a
  disabled block toggled, snapshot and restore, a block's name taken, removing a block
  refused, `pop_layer` of the unit; `remove_layer` of a file with blocks; placement below a
  block and above a file, with the re-read (D144); reload after a block's value changes,
  after its head changes, after a block is added or removed, and after a broken head; write
  back into a block, into its extent, a new key into it, through a wrapper, and promote out
  of it; a text overlay's two blocks admitted under the live-ops policy and refused when one
  holds a command; `check_layer_text` and the off-thread checker reporting blocks, and a
  holding report as the verdict; promote into a block refused; the cache refusal; an
  included file's blocks; a duplicate name; `#include` in a block.
- The experiment's tests (10 cases): the corpus round trip through the block-aware reader
  (68 files, 0 faults); the converter on each region kind; the check catching a region a
  later form overrides and a shadowed repeated key; blocks back to `#ifdef` under every
  condition form, merging equal; the samples merging as their originals.
- `ctest` (23 tests) with the switch on; the default build (switch off) builds and passes
  its 21 tests; clang-tidy finds nothing in the changed files; `publish.py check` passes.
- The parse fuzz property, extended to blocks (each block the same in a plain and an
  edit-mode read, its spans in the text, its tree printed and read back), over the seeds
  (block inputs, the corpus, the samples, the regression files) and the dictionary with
  `#layer` and the head's words. This machine has no libFuzzer runtime, so a
  dictionary-mutation driver ran the property under ASan and UBSan (GCC): 2,152,426 inputs
  on the final parser in 10 minutes, no failure and no sanitizer report. An earlier run
  failed on every input with `(key #remove)`, a fault of the property itself, on main too,
  fixed in `fuzz/parse_harness.cpp` with a regression file. `baker_data_tests` also passes
  whole under ASan and UBSan.

**Not built, or failed:** promote into a block (refused); groups, windows, `dev_only`, the
cache entry; check-json's 1.4 note; a `write_stack` printer; `examples/config` itself still
uses two files, so the default build's example is unchanged (the sample shows the one-file
form). The local matrix was not run whole on this shared machine: the release build with
the switch on and off, clang-tidy and ASan+UBSan over the data tests were.

## 9. Open questions for the owner

1. **Is `FILE:NAME` the sub-layer name (check-json 1.4), and may a host push a text layer
   whose blocks' names collide with file layers?** Recommended: yes to `FILE:NAME`; a
   collision is refused at push (built: "there is already a layer named ...").
2. **Groups and windows: later, with a stack that writes one, or never?** Recommended:
   later, and windows never in data: a window is a host's release policy, which the live-ops
   tooling already owns.
3. **The cache: a `blocks` meta entry at the next `kVersion` bump, or text-only for good?**
   Recommended: text-only until a shipped game ships a file with blocks; then the entry
   (about 28 lines).
4. **Retire `#ifdef` once blocks exist?** Recommended: no. 84% of the raw corpus's regions
   are forms a block cannot take, and the gate reads that corpus.
5. **Blocks inside arrays (the re-wrap), or top level only?** Recommended: top level only if
   the idea is adopted at all: the re-wrap costs about 14 lines and the converter writes
   top-level blocks anyway.
6. **May a block hold `#remove` and layer-markers' verbs; is a received text's block a policy
   subject of its own?** Recommended: `#remove` yes (built); markers' verbs only if that idea
   lands; each block its own subject, every block checked whether enabled or not (built).
7. **If the scoped form lands above 150 lines, raise the budget, or wait?** Recommended:
   wait. 221 lines is 71% of the headroom for a feature no stack uses.

## 10. Recommendation

**Decline for now, and keep the branch as the measured reference.** The prototype proves
the half the judges doubted: sub-layers fit Config's layer machinery through one unit rule
in four places, with provenance, `explain`, write-back, reload, placement, snapshots and
policies unchanged in kind, and it costs nothing measurable on block-free text. It also
proves the half they feared: 221 core lines as adopted, over the 150 bar and 71% of the
remaining headroom, for a feature with no user in the stacks, which replaces a sixth of a
shipped game's conditionals and adds a second conditional form with different precedence.
The next step, if the owner wants conditional platform layers sooner, is the cheaper half
on its own: a `(when COND)` flag on the project file's `(layers ...)` entries, beside the
`optional`, `disabled` and `dev_only` it already takes (D172, D204), which with `disabled`
gives conditional, toggleable platform and experiment layers with no parser change, no
cache question and no second conditional form, at an estimated 10 to 15 lines; in-file
blocks wait for a stack that writes a platform variant or an A/B and feels the file count.

## 11. Verification, the raw gate, and review

*Added after the prototype, from the session's review round; `dev/syntax-ideas.md` on
`claude/fervent-cray-l1kyf0` compares all eight branches.* An agent independent of the
prototype rebuilt this branch, reran its tests and reproduced its numbers; a reviewer
judged it under the data-oriented lens (`.claude/skills/acton-review/`) and the tool
author's; and D101's raw gate (464 raw Harmonix files, `baker_parse_corpus --roundtrip
--milo-paths` over a local clone of mariteaux/milo-script-library, nothing from it in the
repository, D3) was run on the branch's build. Where a finding below disagrees with an
earlier section, this section is the later word.

- **Reviewed at** `85d53e7`, the branch's final head.
- **Verifier.** Builds; 23 of 23 with the switch, 21 of 21 in a fresh default build;
  258 data cases. `count_lines`, the cachegrind counts, the samples and the corpus check
  reproduce exactly; the bench rows within noise. Corrections: section 4 omits the
  prototype's own size (985 code lines, 1,200 raw); reload is never timed (bench-layers
  times the first push only); `bench_compare.py` as committed configures the new side with
  the switch off; the fuzz run's driver and block seeds are not committed, so it cannot be
  rerun from the branch; `bench <file>` exits silently when an include does not resolve.
- **Raw gate.** Switch on: 464/464, 0 failures. The note's own survey of the same corpus
  (1,024 conditional regions, 162 convertible) stands; its arithmetic checks.
- **Technical director, 2 of 5, decline.** Section 6's "build time, not run time" is
  refuted: a platform condition the bake could settle becomes a file the bake refuses,
  and shipping refuses the toggle that is the feature's only payoff. Platform variants,
  A/B arms and live-ops alternatives are three data problems the block fits none of.
- **Tool author, 2 of 5, decline.** Three claims do not hold: shipping refuses
  `set_layer_enabled` (`src/data/config.cpp:731-732`), so the live-ops flip still needs a
  push; `ui_menu.dta`'s real reader, a plain `read_file`, would reject the converted file;
  the Duel A/B replaces host code a project-file stack does not have. `check` never sees
  a disabled block, and the printer cannot write one.
- **Fuzzing.** libFuzzer under ASan and UBSan with `BAKER_INLINE_LAYERS=ON`, built as
  CI's fuzz job is and seeded with the corpus, the fuzz regressions and the samples. The
  first run stopped after 140,554 executions on a real bug in this prototype: a
  `[property]` directly in a top-level block's forms, `(#layer x [c 1])`, became a
  property at the block tree's top level, which prints as a macro definition, so the
  block did not print back as itself (D13's top-level property case, which `push()`
  refuses for a file's own top level). Fixed in `283d74e`: such a block is a located
  error ("block 'x': a [property] cannot appear at a block's top level, where '[' defines
  a macro"), and inside tagged arrays the forms are wrapped and a property stays legal;
  +8 core lines on the cold block path (15,463 with the switch, about +229 adopted).
  Resumed from the grown corpus: 978,786 more executions, 2,146 edges, no failure;
  1,119,340 in all, short of spec §8's 10 million.
- **Recommendation after review.** Decline, as section 10 says; `(when COND)` on the
  project file's layer entries if conditional platform layers are wanted.

### The completion round

*Added in the session's last pass over every branch, which took each to spec §8's fuzz bar
and CI to green on every job; `dev/syntax-ideas.md` on `claude/fervent-cray-l1kyf0` records
the round for all eight.*

- **Fuzzing, 10,000,000 executions on the final parser** (from `283d74e`, the block fix; the
  later commits change the note and the harness), in runs each resumed from the last one's
  corpus: a run stopped to apply a harness fix, or by one of two machine restarts, resumed
  from its last logged count, so no execution is counted twice. None found a failure in the
  parser. Section 11's 1,119,340 are not counted.
- **A harness fix from another branch.** main's harness skipped a tree over 100,000
  expanded nodes but read its print back with a 65,536-node limit; indent-lines' run found
  it, and the fix and `macro_sharing_budget.bin` are here too (`ab1d3d8`).
- **CI.** Every job passes on the final head.
- **A second harness skip, for a defect on main.** main's printer cannot spell a tree whose
  first form is `(origin "...")` held as data, a second origin, since it prints as the
  declaration the reader consumes (D91). The parser reads it right. The harness now skips
  that tree, as indent-lines' does, with the same regression input (`bed1c65`). The
  parser did not change, so the count carried on.
