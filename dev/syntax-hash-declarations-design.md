# The reader's namespace: `#pack`, `#origin`, `#shape`, one macro form: a design note

*Syntax prototype `hash-declarations` (`experiments/syntax-hash-declarations/`), merged from
the proposals declare-directives, one-macro-one-hash, reader-forms and tables-declare-rows.
Nothing here is adopted. The core is unchanged on this branch; `baker::shape` gains one
feature, a list's arity, which the prototype needed and which stands alone (§4).*

## 1. Thesis

Today a file tells the reader three things through forms that look like data. A first form
`(pack ...)` declares a pack (D82); `(origin "..." ...)` declares an origin when it is
first, or first after the pack, and its first value is a string (D91); and `(shapes ...)`
declares shapes, but stays in the tree and merges like data (D84). The parser recognises
the first two by position and type (`src/data/parse.cpp:598-607`), the shape library the
third by a top-level tag (`src/shape/shape.cpp:188-194`). This change moves all three into
the `#` namespace DTA already reserves for the reader: `#pack (...)`, `#origin (...)` and
`#shape (...)` are directives that take one parenthesized form, are recorded in the session
as packs and origins already are, and are never in the tree. A `#shape` may also be written
inside the table it governs, with its place and `(keyed)` implied. In the same move the
heritage half of the namespace is set right: `[NAME ...]` is the one macro form, `#define
NAME` only a flag, and `#` opens a directive only before an ASCII letter, so `#`, `#1`,
`#00aa00` and `#$%&` are symbols the reader and the printer agree on. A list type gains an
arity: `(list float 4)`.

The pains it relieves, by name. **Position sniffing:** a second `(pack ...)`, or one after an
`#include`, silently becomes data; `(origin 0 0 0)` stays data only because of a type test;
and a received layer's `(shapes ...)` is data that `LayerPolicy` cannot tell from a value,
so an overlay that declares no pack can widen a pack's range under every rule but `allow`
(measured in §5). **D84's container,** which exists only because shapes merge by tag; as
declarations, spec §6's own per-shape top-level form comes back. **D108's silent wrong
depth:** a keyed shape at `(at fighters moves)` checks nothing and says nothing. **The
grammar's one layout-sensitive rule,** `#define NAME (body)` only when the `(` is on the
name's line (`parse.cpp:881`, D11), a second macro form beside spec §1's `[NAME ...]`. **The
lone `#`** the reader takes bare and the writer quotes (`src/data/write.cpp:21-29`, D101).
It serves spec §1 as written (`(origin ...)` "as a top-level form", `[NAME ...]` as the
macro form), spec §6 (shapes declared in data, which a validator reads as declarations
rather than as merged data), and D161's policy, which can then classify a declaration.

## 2. The syntax

### Tokens and forms

One lexer rule changes. An atom whose first byte is `#` is a directive when its second byte
is an ASCII letter (`[A-Za-z]`), and the directive's name is the rest of the atom. Any other
`#`-led atom, `#` alone included, is a Symbol. Today any `#`-led atom of two or more bytes
is a directive (`parse.cpp:243`), so `#1` is "unknown directive '#1'".

```
directive := '#' NAME args             NAME := [A-Za-z][A-Za-z0-9_]* (the atom's other bytes)
  #include P | #include_opt P | #merge P | #require P
  #ifdef N | #ifndef N | #else | #endif | #define N | #undef N | #autorun {...}
  #remove                               only as a key's one value, (key #remove) (D143)
  #pack   ( NAME pack-child* )          new
  #origin ( STRING origin-child* )      new
  #shape  ( NAME shape-child* )         new
symbol    := '#' | '#' non-letter-byte atom-byte*      new: an ordinary Symbol
macro     := '[' NAME node* ']'         at a file's top level only (D11); deeper, a Property

pack-child   := (level L) | (version V) | (requires (NAME "V") | NAME ...) | (owners ...)
              | (for ...) | (stage genesis|custom|product|commodity)   D82, D91, D147, as today
origin-child := (tool NAME)                                            D91, as today
shape-child  := (at SEG ...) | (keyed) | (FIELD TYPE meta...)          D84, D108, D203, as today
TYPE         := int | float | bool | symbol | string | any | SHAPENAME
              | (list T) | (list T N) | (list T N M)                   N M new: a count
```

`directive()` keeps its whitelist (`parse.cpp:851-909`) and gains three branches. An unknown
letter-led name stays `ErrorCode::Directive`, so `#foo` and `#foo-bar` are errors, as today.

**What each reads to.** No new node kind; Array, Command, Property, Var, Symbol, String, Int
and Float are as today. The declaration's form is parsed as an ordinary Array, in the load's
arena (strings, symbols, macros and conditionals inside it all work), and pushed nowhere:

| Form | Where | Recorded as |
|------|-------|-------------|
| `#pack (NAME ...)` | a file's top level, anywhere, once | a `Pack` (`session.packs()`), as today |
| `#origin ("SRC" ...)` | a file's top level, anywhere, once | an `Origin` (`session.origins()`), as today |
| `#shape (NAME ...)` | the top level, or inside a `( )` array | a `ShapeDecl` (`session.shapes()`): the form as a `Ref<Array>` (which keeps the load's arena alive, as `shape::Set::keep` does, D64), file, line, `at`, `keyed` |

A `#shape` written inside an array takes as its `at` the tags of the enclosing arrays from
the root, and is `(keyed)`: it governs that array's children, whatever their tags. Written
at the top level, `(at ...)` and `(keyed)` mean what they mean today, and a shape without
`(at ...)` governs arrays tagged with its name.

### What today's forms become

| Today | New |
|-------|-----|
| `(pack x (level game) ...)` as the first form | `#pack (x (level game) ...)`. `(pack ...)` anywhere is data; `check` warns once per layer about a first-form one |
| `(origin "economy.xlsx" (tool t))` first, or after the pack | `#origin ("economy.xlsx" (tool t))`. `(origin ...)` is data; a first-form one with a string is warned about |
| `(shapes (a ...) (b ...))` at the top level | `#shape (a ...)` and `#shape (b ...)`. `(shapes ...)` is data; at the top level it is warned about |
| `#define N (body)`, `(` on the name's line | `[N body]`; the old spelling is a located error naming the fix |
| `#define N` | unchanged: a flag |
| `'#'`, `'#1'`, `'#00aa00'` | `#`, `#1`, `#00aa00`, printed bare; `'#remove'` and `'#if'` stay quoted |
| `(list float)` | unchanged; `(list float 4)` exactly four, `(list float 1 2)` one or two |

### Edge cases, resolved

- **Quoting.** A quoted `'#pack'` is the symbol `#pack`, never a directive. `is_bare_symbol`
  (`src/data/text.cpp:105`) prints a `#`-led name bare when its second byte is not a letter,
  and quotes it otherwise; `#remove` stays bare only in a key's one-value slot (D143). So
  `(# {toggle_hud})` round-trips as written, closing D101's lone-`#` mismatch.
- **Numbers.** `#1` is not a number: the `#` test comes before number classification, as now.
- **Comments and layout.** Between a directive and its form, comments and newlines are
  skipped as anywhere: `#shape ; the fighter\n (fighter ...)` is one declaration. Nothing
  else in the grammar depends on lines once `#define NAME (` goes.
- **Empty and malformed forms.** `#pack ()` is "(pack ...) needs a name", as today; `#origin
  (economy)` is "#origin takes a string first"; `#shape ((f int))` at a table is "#shape
  (NAME ...) needs a name" (at the top level the shape library reports a malformed
  declaration, as it does today); `#pack x` is "#pack takes one form: #pack (NAME (level L)
  ...)". Each is located.
- **Placement.** A second `#pack` or `#origin` in one file is a located Directive error at
  the second (today the second is silently data). A `#pack` or `#origin` anywhere but the top
  level, and a `#shape` in a macro body, a command, a property or an array with an untagged
  ancestor, is an error. One `#shape` per table; a second is an error.
  `(at ...)` inside a table's `#shape` is refused: it would restate the place.
- **A pack is its whole file's.** Every macro and flag the file defines is the pack's for
  the one-way rule, wherever `#pack` is written. Today a macro defined before `(pack ...)`
  belongs to no pack (D82), which a position-free declaration would make a trap: the
  adopted `declare_pack` attributes the file's earlier definitions (about 3 lines).
- **Conditionals.** A declaration counts only in the branch read: `#ifdef A #pack (a ...)
  #else #pack (b ...) #endif` declares one pack, since `directive()` is reached only in an
  active branch (`skipped_directive`, `parse.cpp:830-834`).
- **Macros.** Inside a declaration, macros expand as in any array: `#pack (x (level LEVEL))`.
  `#define kMax (4)` is "#define kMax defines a flag; a macro is written [kMax ...]" at the
  `(`. `#define kMax` followed by `(4)` on the next line stays a flag and data, as today.
- **`#remove`** is unchanged: a directive, accepted only as a key's one value.
- **Includes.** A file included at the top level declares its own pack and origin, as today.
  A table `#shape` in a file included inside an array takes its place from the including
  file: the sub-parser receives the enclosing tags (a `Vec<Symbol>` beside
  `include_depth_`).
- **Packs, layers, overlays.** A file layer, a text layer (`push_layer_text`, D163) and a
  checked layer read their declarations as today; a tree pushed by `push_layer(name, tree)`
  carries none, as D162 documents for packs.
- **Shapes at a table** govern that one table's children. They cannot say `*`: Duel's move
  shape, `(at fighters * moves)`, governs every fighter's moves and stays top-level.
- **Scenarios, metrics and project files** (`stacks/*/sim`, `stacks/*/metrics`,
  `baker-project.dta`) use none of the changed forms.

### The corpus before and after

**A pack and its shapes** (`stacks/duel/packs/game/duel_core.dta:1,8-29`; after:
`experiments/syntax-hash-declarations/samples/duel_core.dta:1,8-28`). The container line
goes, every shape loses one indent, and the hitbox says how many floats it holds:

```dta
(pack duel_core (level game) (owners "gameplay-fight" "td-combat") (stage custom))
...
(shapes
   (fighter (at fighters) (keyed)
      (health        int   (range 1 100000))
      ...
   (move (at fighters * moves) (keyed)
      ...
      (hitbox    (list float) (desc "x y w h, meters, relative to the fighter")))
```

```dta
#pack (duel_core (level game) (owners "gameplay-fight" "td-combat") (stage custom))
...
#shape (fighter (at fighters) (keyed)
   (health        int   (range 1 100000))
   ...
#shape (move (at fighters * moves) (keyed)
   ...
   (hitbox    (list float 4) (desc "x y w h, meters, relative to the fighter")))
```

**A shape at its table** (`stacks/duel/data/default.dta:16-17`; after:
`samples/duel-at-table/data/default.dta:21-29`, whose pack keeps only the move shape):

```dta
(fighters
   #shape (fighter
      (health        int   (range 1 100000))
      (width         float (range 0.2 3) (kind meters))
      ...
      (moves         any))
   (kestrel
      (health 1000) (width 0.6) (height 1.8) (crouch_height 1.1)
```

**A generated file's origin** (`stacks/ledger/data/economy.dta:4`):
`(origin "economy.xlsx" (tool sheets-export))` becomes
`#origin ("economy.xlsx" (tool sheets-export))`.

**A published overlay** (`examples/live-ops/overlays/2026-10-17-double-xp.dta:2-3`):

```dta
#pack (double_xp_1017 (level live) (version 1) (requires (game_data "1.0")))
#origin ("liveops console, ticket 4412" (tool baker-publish))
```

**Macros** (`tests/corpus/macros.dta:9-11`): `#define kMaxPlayers (4)` and `#define
kDefaultVolume (0.8)` become `[kMaxPlayers 4]` and `[kDefaultVolume 0.8]`; `#define
HAS_VOCALS` stays.

**Symbols** (the experiment's tests; `tests/corpus/odd_tokens.dta` is not changed, since
Baker's reader refuses `#1` until the lexer rule lands): `(hashes # #1 #00aa00 #$%&
'#remove' '#if' (key #remove))` reads and prints back exactly as written.

## 3. What it keeps and what it changes

- **The lexer and parser.** Would need this work, about +40 lines of `parse.cpp`: the
  `#`+letter test in `lex_atom` (`parse.cpp:243`); three branches in `directive()` that open
  a `Frame::Declare` of a kind; in `close()`, the form handed to `declare_pack`,
  `declare_origin` (its string now at index 0) or a new `declare_shape` (the place read from
  `frames_`, `(at ...)` refused inside an array), pushing nothing; once-per-file errors; the
  first-form tests (`parse.cpp:598-607`) and `Frame::DefineBody` (`parse.cpp:449, 589-591,
  881-886`) deleted; the same-line error. `parse.hpp` gains `ShapeDecl` and
  `LoadSession::shapes()` (about +10). The prototype re-provides all of it as a translator
  (§8).
- **The printer.** Unchanged but for `is_bare_symbol`'s `#` rule (about +2 lines). A printed
  tree never held a pack or origin; it now holds no shapes either, as `to_dta` of a tree
  holds nothing the session records. The prototype's printer writes a top-level `(shapes
  ...)` as `#shape` lines, which is the migration.
- **Write-back** (D34, D132, D183, D226). Unchanged: declarations are never children, so
  token replacement and inserted children (`write_back_to` appends inside the deepest
  ancestor or at the file's end) see the same arrays and spans. Values inside a shape can no
  longer be written through a path (`cfg.set("shapes/move/startup/range", ...)` today), since
  shapes are not in the tree; nothing in the repository does that. The prototype does not
  re-provide write-back: its translator changes byte offsets (§8).
- **Edit-mode source maps** (D33, D128). Unchanged: a declaration has no span in the map,
  as a pack today has none. `Field::file` and `Field::line` carry a declaration's place,
  in the pack or at the table.
- **The tolerant structure read** (D220). Unchanged in code: `read_structure` uses the
  parser's own `Lexer`, so it gets the `#`+letter rule with it. An editor sees `#shape` as a
  Directive token followed by an ordinary bracket, never a new token kind.
- **Provenance and explain.** Values' provenance is unchanged. A shape is no longer a value,
  so `explain("shapes/...")`, `layer_effect` and `diff` no longer show a layer's shape
  changes. That is a loss for reviewing a layer (open question 8).
- **Layers and `#remove`.** Merging data is unchanged. Shapes no longer merge by tag (the
  reason for D84's container): `shape::Set` combines declarations by name, a field declared
  again taking the newer type and metadata, exactly as it combines the trees' containers
  today, with D145's narrowing rule between packs. A layer can no longer remove a shape with
  `(shapes (move #remove))`; removing a harder pack's shape is loosening, which D145 refuses
  in spirit.
- **`check` and the demand report.** The demand report is unchanged. `check` gains three
  warnings, once per layer, for a top-level `(shapes ...)`, a first-form `(pack ...)` and a
  first-form `(origin "..." ...)` read as data (`config_check.cpp`, about +15). The host
  kit's `check` command reads shapes from the merged root without packs
  (`src/host/commands.cpp:152-157`) while `reference` reads them per layer with the read
  session (`commands.cpp:179-187`), so the two can disagree on a softer pack's loosening;
  both would read `cfg.read_session()` (about -10 lines of 2,400). The ImGui harness's
  shapes panel (`examples/harness-imgui/panels.cpp:299-306`) changes the same way.
- **Shapes and the reference JSON.** `shape::Set::add(const LoadSession&)` replaces
  `add(const Array&, ...)` and its top-level container scan; `shape::validator` reads
  `ValidatorStack::session`. `write_json` gains `count` for an arity (`baker.shapes/1.2`).
  Built here: the arity, its check and its loosening rule (§4).
- **The console's requests.** Unchanged: a request evaluates a command in the game's Context
  (`include/baker/host/console.hpp`), and one that reads `shapes/...` through the Config's
  tools would find nothing, as nothing in the repository asks; tools get shapes from
  `reference`'s JSON.
- **The project file.** Unchanged: `(project ...)` uses no changed form.
- **`LayerPolicy`** (D161, D163). Would gain one classification: a `#shape` in received text
  is refused under `data_only` or `scalars_only` and admitted otherwise; `#pack` and
  `#origin` are admitted, as a published overlay declares itself with them (D162).
  Prototyped as `check_received` (about +8 lines of `config_check.cpp` adopted).
- **The binary cache.** Would need this work: the meta tree (`src/data/cache.cpp:616-689`)
  gains a `shapes` entry per declaration (file, line, at, keyed, and the form as a
  sub-tree), replayed into the session (`cache.cpp:743`), and `kVersion` goes from 6 to 7
  (`cache.cpp:44`), about +30 lines. Without it a cache load would lose every shape, since
  they are no longer in the tree. The prototype keeps shapes in the tree, so its caches work
  unchanged (tested).
- **The docs and the corpus.** Would need rewriting: 18 of the 68 corpus files, 9 code
  blocks in `docs/language.md` (6) and `docs/cookbook.md` (3), the prose rules beside them
  (`docs/language.md:122-123` and the first-form paragraphs; `README.md:135`;
  `docs/cookbook.md:490`), and about 101 lines of test text in 11 test files (§7).

## 4. Cost, measured

The machine is shared with seven other prototypes: the load average was 7 to 10 during
every wall-clock run, so those numbers are noisy, and the instruction counts (cachegrind,
`count` and `count-stack` in the CLI, `(I(2n) - I(n)) / n` as `tools/count_instructions.py`
does) are the ones to compare. Release build.

**Parse, the prototype's reader against Baker's on the same content.** The adopted change
puts no work on this path but one test of a `#` atom's second byte, reached only on `#`
atoms; the core's parser is untouched on this branch (no diff under `src/data/`), so
`count_instructions.py`'s parse case would read the same 1,845,499 per document by
construction. What is measured is the prototype's translator, which is a second lexing pass
and a copy:

| Text | Baker MB/s | Prototype MB/s | Translate only MB/s | Time ratio |
|------|-----------:|---------------:|--------------------:|-----------:|
| Generated, 4,194,408 bytes (3,973,706 printed), run 1 | 136.7 | 90.9 | 309.5 | 1.424 |
| run 2 | 135.5 | 91.8 | 315.9 | 1.398 |
| run 3 | 117.8 | 91.8 | 313.5 | 1.216 |
| runs 4-6, the final build, load near 10 | 86.2, 121.8, 96.6 | 59.8, 60.1, 57.6 | 216.4, 271.5, 219.5 | 1.365, 1.918, 1.589 |
| `stacks/hearth/data/default.dta` (and its `#require`), three runs | 131.0, 132.3, 129.3 | 89.0, 88.3, 88.0 | 352.1, 350.2, 353.5 | 1.472, 1.498, 1.468 |
| `stacks/wayfarer/data/default.dta` (and its four requires) | 79.5, 79.5, 79.5 | 53.5, 53.7, 53.0 | 336.8, 338.4, 334.1 | 1.487, 1.480, 1.499 |
| `stacks/duel/data/default.dta` (and its pack) | 97.9, 96.6, 98.1 | 65.2, 65.2, 65.0 | 383.8, 382.7, 384.5 | 1.501, 1.482, 1.509 |
| `stacks/duel/packs/game/duel_core.dta` | 111.7, 100.9, 189.7 | 71.8, 63.5, 122.6 | 237.7, 199.5, 381.5 | 1.503, 1.535, 1.494 |

A stack file's MB/s counts its own bytes only; both sides read its included files from
memory, the prototype's migrated. Instructions per read of the generated 1 MB text: Baker
98,042,908 (1,048,668 bytes); the prototype 139,584,075 (995,451 bytes, the same tree), of
which the translator is 41,782,770; ratio **1.424**. At the final commit the prototype
reads 144,665,860 and the translator 46,864,533 (ratio **1.476**): later edits to the
translator moved its count by 5.1 M instructions without touching its work per byte, which
is code layout, so read the translator's cost as 42 to 47 instructions a byte. It is the
prototype's cost, not the design's.

**Merge, reload, toggle and push: what moving shapes out of the tree saves.** The cost
judge expected a saving from no longer merging the `(shapes ...)` subtree (Duel's is 40 of
its pack's 47 lines). `bench-stack` builds a Config for each stack (its `default.dta`,
which `#require`s the pack, and a text layer above it; edit mode on, no tweaks) twice: with
the pack's `(shapes ...)` in the tree, as today, and with it parsed into a macro body
nothing expands, which is what an adopted `#shape` costs (the arrays parsed into the load's
arena, one more entry in the session, which is copied with it, nothing in the tree).
Instructions per operation:

| Stack | Reload, tree → records | Toggle a layer | Push and remove a text layer |
|-------|-----------------------|----------------|------------------------------|
| contraption | 497,550 → 507,293 (+1.96%) | 4,013 → 3,711 (-7.51%) | 46,948 → 50,400 (+7.35%) |
| duel | 622,979 → 632,392 (+1.51%) | 4,349 → 4,023 (-7.50%) | 47,922 → 50,780 (+5.96%) |
| hearth | 802,928 → 815,270 (+1.54%) | 4,347 → 4,021 (-7.50%) | 47,920 → 50,779 (+5.97%) |
| ledger | 534,856 → 544,895 (+1.88%) | 4,852 → 4,509 (-7.07%) | 50,631 → 52,898 (+4.48%) |
| stuntline | 419,945 → 430,063 (+2.41%) | 4,340 → 4,044 (-6.80%) | 47,913 → 51,020 (+6.49%) |
| wayfarer | 787,069 → 798,293 (+1.43%) | 9,009 → 8,558 (-5.01%) | 61,791 → 64,340 (+4.13%) |

Wall clock agrees (best of 300, two runs each): Duel's reload 56.6 and 57.0 µs with the
subtree, 57.3 and 57.9 without; its toggle 0.5 against 0.4 µs; its push 2.7 and 2.8
against 2.9 and 3.0. The merge saving is real and small: 295 to 451 instructions a merge,
because the merge appends an unchanged subtree by reference, never copying it. The
session's extra record costs about as much again wherever a session is copied
(`push_layer_text` reads with a copy of the session below, D163) or recorded (a reload).
**The net is a wash, within ±2.4% of a reload and ±7.5% of operations that take under
5 µs.** The re-read cost D144 names is the reload row: a reload reads `default.dta` and its
pack again and merges the stack. `bench_cache`'s read was not measured: the prototype does
not move declarations into the cache's meta tree. Adopted, the same arrays move from the
tree to the meta tree, so a cache read copies the same bytes and replays one more list.

**Lines and bytes of the real files** (`samples/`, each beside its original as
`*.before.dta`; "migrated" is the migrator's output, "sample" the same with arities written
by hand):

| File | Before | Migrated | Sample |
|------|-------:|---------:|-------:|
| `stacks/contraption/packs/game/contraption_core.dta` | 28 / 1,468 | 27 / 1,419 | 27 / 1,429 |
| `stacks/duel/packs/game/duel_core.dta` | 47 / 2,830 | 46 / 2,733 | 46 / 2,735 |
| `stacks/hearth/packs/game/hearth_core.dta` | 24 / 1,477 | 23 / 1,436 | 23 / 1,444 |
| `stacks/ledger/packs/game/ledger_core.dta` | 28 / 1,837 | 27 / 1,791 | 27 / 1,791 |
| `stacks/stuntline/packs/game/stuntline_core.dta` | 24 / 1,342 | 23 / 1,298 | 23 / 1,298 |
| `stacks/wayfarer/packs/game/wayfarer_core.dta` | 28 / 1,691 | 27 / 1,645 | 27 / 1,649 |
| **The six packs** | **179 / 10,645** | **173 / 10,322 (-3.0%)** | **173 / 10,346** |
| `examples/live-ops/data/default.dta` | 13 / 453 | 12 / 440 | 12 / 440 |
| `examples/live-ops/overlays/*.dta` (four) | 22 / 798 | 22 / 803 | 22 / 803 |
| `stacks/ledger/data/economy.dta` | 22 / 1,496 | 22 / 1,497 | 22 / 1,497 |
| `tests/corpus/macros.dta` | 14 / 315 | 14 / 299 | 14 / 299 |
| Duel at its tables: `default.dta` + `duel_core.dta` | 95 / 5,963 | | 100 / 6,254 |

Each cell is lines / bytes. Each pack loses its `(shapes` line (6 lines in all, not
the 24 the brief estimated, since a dedent shortens lines rather than removing them) and
three bytes a shape line. `#pack (` and `#origin (` are one byte longer than `(pack ` and
`(origin `, not two. `[kMaxPlayers 4]` is eight bytes shorter than `#define kMaxPlayers
(4)`. The at-the-table Duel is five lines longer: four are the comment saying how the form
works, and the shapes sit one indent deeper inside their tables, which outweighs the 57
bytes of `(at fighters) (keyed)`, `(at cpu) (keyed)` and `(at bots) (keyed)` it removes.

**Line budgets** (`tools/count_lines.py`, before → after on this branch): the core 15,190 →
15,190 of 15,500; `baker::shape` 547 → 567 of 600 (the arity: +19 in `shape.cpp`, for
`read_field`, `loosens` and the count check, and +1 in the header); `baker::host` 2,361 →
2,361 of 2,400. The prototype is 2,091 counted lines: `syntax.cpp` 945, `tests.cpp` 751,
`main.cpp` 358, `syntax.hpp` 37.
**Projected if adopted** (estimates, not built): the core about +100 against the 310 left
(`parse.cpp` +40, `parse.hpp` +10, `text.cpp` +2, `cache.cpp` +30, `config_check.cpp` +23 for
the warnings and the policy rule), leaving about 210; `baker::shape` about +5 (the session
reader, `count` in JSON), about 572 of 600 with no raise; the host kit about -10. Without
the at-the-table form, `parse.cpp` is about 15 lines smaller (no place walk, no include
prefix).

**Allocation and layout.** No node or array layout changes. A declaration's arrays are
parsed into the load's arena as today's `(shapes ...)` is; the session holds a `Ref` to each
form (16 bytes and a refcount per declaration in a `ShapeDecl` of about 48 bytes), copied
with the session. Duel's four shapes are four records.

## 5. Payoff, measured where possible

**Errors a person could no longer make.** Each was possible on main, and the experiment's
tests show it closed:

- **A loosened shape in received text.** On main, an overlay that declares no pack, under
  `data_only`, `scalars_only`, `existing_only` and `warnings_refuse` without `allow`, may
  send `(shapes (item (price int (range 0 99999))))` with `(shop (sword (price 5000)))`:
  the policy sees two existing numbers retuned, the validator reads the overlay's range,
  and the 5000 passes where `(shop (sword (price 5000)))` alone is refused against the
  pack's 0 to 1000 (test "received text"). An overlay that declares itself a pack is held
  by D145; the live-ops example sets `allow` to `event`, which refuses the path. Adopted,
  `(shapes ...)` is data that declares nothing, and a received `#shape` is a declaration the
  policy refuses (prototyped as `check_received`).
- **A shape at the wrong depth.** `(move (at fighters moves) (keyed) ...)` for `(at fighters *
  moves)` checks nothing: a startup of 900 and an undeclared field pass with no error and no
  warning (test "a shape at the wrong depth"). Written at its table, the place is where it
  stands. Written at the top level, the check finding prototyped beside it (a shape that
  governs no array of the stack) names it, and is silent over all six stacks' real data.
- **A forgotten `(keyed)`**: implied at a table.
- **A tuple of the wrong length.** `(hitbox 0.3 1.3 0.55)` passes `(list float)` today; with
  `(list float 4)` it is "'hitbox' holds 3 values, not 4", located (test "Duel's shapes at
  their tables"). 11 of the corpus's 16 list fields have a fixed or bounded count (Duel's
  hitbox; Contraption's size, bounds and two positions; Hearth's two sizes, color and
  hours; Wayfarer's grid and position); the other five are open lists. Each stack's data
  passes its arities with no error (test "each stack's migrated pack").
- **A second pack or origin** is a located error, not silent data; **a declaration after
  data or after an `#include`** is a declaration, not silent data.
- **`#define kMax (4)` against `#define kMax` + newline + `(4)`,** which mean different things
  today, is one form and an error that names the fix.
- **`#1`, `#00aa00` and `#$%&`** read: D101 counts 59 converted community files that fail on
  such a symbol. (The brief's "137 of 295" adds D101's 78 files whose bare symbol reads as a
  20-digit integer, which this rule does not touch.)

**What each reader gains.** A designer writes a fighter's shape beside the fighters, with no
path to restate (Duel at its tables, 3 shapes, 57 bytes of place gone). An engineer reads a
pack whose first line says it is a pack in the reader's voice, and shapes without a
container; `check` and `reference` agree. A tool (an editor, the reference panel) gets
declarations from the session, located, and a `count` for a vector field's editor; nothing
in the tree pretends to be data. An agent writing a layer cannot redeclare a shape by
accident in received text, and an agent reading a stack finds every declaration by `#`.

**Diffs.** Adding an arity is one token. Writing a field's shape beside its table puts a new
field and its declaration in one file's diff instead of two. The migration itself is
+183 -195 lines over 18 files, +56 -68 ignoring whitespace (the rest is the dedent).

## 6. Objections

**"A second parser."** Two readers of one syntax drift unless a test holds them together, as
D220 found of editors' lexers. The prototype is one: a translator at 42 to 47 instructions a
byte, a ratio of 1.42 to 1.48. Adopted there is none: `directive()` gains branches reached
only on `#` atoms. *Answer: true of the prototype only.*

**"Cost per byte."** The lexer's `#` test changes from a size test to a byte test on the
`#` path only. *Answer: nil on the parse path, by construction; not measured, since the core
is not changed here.*

**"Build time against run time."** Shapes are read by tools and validators, never by the
game's lookups, and they leave the runtime tree, as Acton's "push work back in time" asks.
But the saving measured is 300 to 450 instructions a merge, because the merge shares the
subtree, and the session's records cost about as much on a push. *Concession: the runtime
argument does not carry the change; correctness and clarity must.*

**"Solving a problem you don't have."** In the repository's 68 files there is no second
pack, no declaration after data, and no `#`-led symbol but a quoted `'#notdirective'`, which
stays quoted; there are two `#define` bodies. What did happen: D108's wrong depth (Duel's
shapes checked nothing, and `docs/language.md`'s example was wrong), D145's research
scenario 9, and the received-layer gap found here. *Partial concession: the namespace and
the policy rule answer real failures; the at-the-table form and the migration warnings
answer likely ones.*

**"The tree is the truth."** Today every declaration but packs and origins is in the tree,
so `layer_effect`, `diff`, `explain` and `promote` show a layer that retunes a shape. After,
they do not, and a live overlay's staging report would not show a `#shape` unless
`layer_effect` lists declarations. *Concession, with a remedy (open question 8).*

**"The corpus and the modding community."** DTA's `#define NAME (body)` is the heritage
form (D11); spec §8 requires the real corpus to parse, 100%, and D101's gate is 464 of 464
raw files. Whether any raw file uses a `#define` body could not be measured here: the
corpus lives outside the repository (D3) and is not on this machine. Community tools that
read DTA would refuse `#pack`, `#origin` and `#shape` as unknown directives, where today
they read `(pack ...)` as data. *Concession: the raw gate must be re-run before
`#define NAME (body)` goes (open question 6); Baker's files would no longer be DTA that older
tools read.*

**"Positional rules are already documented and tested."** They are (D82, D91,
`test_preprocessor.cpp:533-559`), and they have held. *Answer: they hold for the files that
are right; they say nothing when a file is wrong.*

## 7. Migration

**The tool.** `baker_syntax_hash-declarations to-new FILE` is the migrator (adopted, it moves
to `tools/`): span-preserving, it rewrites only the forms that change and keeps every
comment, blank line and the layout of the rest. Prefix-and-split: a first-form `(pack ...)`
becomes `#pack (...)`, a first `(origin "..." ...)` becomes `#origin (...)`, a top-level
`(shapes ...)` becomes one `#shape` per child, dedented one level, and a top-level `#define
N (body)` becomes `[N body]`. It rewrites only what Baker reads as a declaration today
(directive arguments are not data; an `#include` before a `(pack ...)` leaves it data, as
Baker reads it), and reports per file. Moving a shape to its table is a person's choice,
never the tool's. `migrate-check` reads every corpus file migrated and compares with
Baker's read of the original: **68 files, 0 tree, 0 value-digest (D222), 0 session
(packs, origins, macro definitions) mismatches**; 18 files rewritten: 11 packs, 2 origins,
12 containers into 30 `#shape`s, 2 `#define` bodies.

**The rest of the repository.** 9 doc code blocks (each rewritten by the tool) and
their prose; about 101 lines of test text in 11 files (`test_preprocessor.cpp` 38,
`test_shape.cpp` 26, `test_check.cpp` 14, `test_cache.cpp` 6, `test_received_layers.cpp`
4, `test_config.cpp` 3, `test_layer_check.cpp` 3, and 7 more in four files), some of which
are data cases that stay data (`(origin 0 0 0)`, a later `(pack ...)`), and some of which
change meaning (`(origin "a")(origin "b")` becomes two `#origin`s, an error).

**One syntax, not two.** The repository's rule is no compatibility layers. An old file read
by the new reader is not misread silently: its `(pack ...)` is data, and `check` warns once
per layer, in the reader's words ("a (pack ...) first form, read as data: write #pack").
Under `warnings_refuse`, a received overlay still in the old form is refused rather than
applied without its pack. So the switch is one commit in Baker, and one ordering for
published overlays: the publisher writes the new form before games update, since the old
form's pack and origin would not be read.

**The one-way rule, packs and layers** are unchanged in meaning: a pack's rank comes from
`#pack` as from `(pack ...)`. One attribution changes: a macro defined above the
declaration becomes the pack's (§2); no corpus file defines one there. **Live-ops overlays**
keep D162's convention with the new spelling.

## 8. What the prototype proved, and did not

**Built.** A front end over Baker's reader (`syntax.cpp`): a translator from the new form to
today's DTA (a declaration becomes the first form, moved there on one line when written
after data or a definition, so later lines keep their numbers; a top-level `#shape` run
becomes a `(shapes ...)` container; a table's `#shape` is hoisted to a container with its
place and `(keyed)` and moved back to its own lines after the read; a `#`-led symbol is
quoted; a first form that is data gets a node before it, dropped after the read), a printer,
the migrator, and `check_received`. One real feature in `baker::shape`: `(list T N)` and
`(list T N M)`, with `Field::min_count` and `max_count`, the located Range error, and D145's
rule for counts.

**Passed.** `check`: 68 corpus files, 0 failures (no skip list). `migrate-check`: 68, 0
mismatches of tree, value digest or session. `baker_syntax_hash-declarations_tests`: 21
cases, 1,110 assertions, also clean under ASan and UBSan, among them: every new form and
every reported error, located (a second `#pack` at its column, `#define kMax (4)` naming
`[kMax ...]`, `(at ...)` at a table, a second table shape, an unknown `#foo`); the four fuzz
regressions; every prefix of a mixed sample; 3,000 seeded random token texts, each that
reads printing and reading back to the same tree; every sample; each stack's migrated pack
over its real data; Duel at its tables (four shapes, keyed, located at the lines written);
D145 for `#shape` and for counts; a macro defined above `#pack` attributed to the pack for
the one-way rule; a cache round trip of a migrated pack; the received-text gap and its
refusal; the wrong-depth finding. `baker_shape_tests`: 11 cases, one new, also under ASan and
UBSan; clang-tidy finds nothing in `shape.cpp`. The full `ctest` passes (23 tests).
`tools/local_matrix.sh`'s other configurations were not run on this shared machine.

**Not proved, or not re-provided.**
- The core change itself: no parser, session, cache or policy code was written. Its line
  costs are estimates.
- Write-back and edit-mode spans through the prototype: the translator changes byte offsets
  (a quoted `#1`, `(shapes (` for `#shape (`), so `write_back` through it would be wrong.
  Error lines are exact, but for an error inside a declaration moved to the front, which
  reads as one line; columns on a changed line are not.
- A legacy `(shapes ...)` still declares shapes in the prototype, since the translator fakes
  `#shape` with that container and the shape library still scans for it.
- Prototype limits, each a located error: a `#`-led symbol containing `'` (DTA text has no
  spelling for it quoted, D13); a declaration that would move out of an `#ifdef`; an
  included file whose first form is a `(pack ...)` or `(origin "...")` read as data; `#pack`
  in both branches of one conditional is refused as a second `#pack`. Not an error but a
  limit: a table `#shape` in a file included inside an array takes its place from that
  file's root, not the including file's.
- `tools/migrate_dta.py` was not written; the migrator is the experiment's C++.
- The raw gate (`baker_parse_corpus --milo-paths --roundtrip` over the 464 raw files), the
  count of raw files using `#define NAME (body)`, `bench_compare.py` on `bench_parse`, and
  `bench_cache`'s read were not run: the corpus is not on this machine, and the core is
  unchanged here.
- The kit's `check` and `reference` agreeing over the stacks was not tested, only read from
  the code.
- `tests/corpus/odd_tokens.dta` does not gain `(hashes ...)`: Baker's reader would refuse it.

## 9. Open questions for the owner

1. **The at-the-table `#shape`: adopt it, or keep shapes top-level with a check finding?**
   Recommended: keep `#shape` top-level with `(at ...)`, and add the finding "shape 'x'
   governs no array" (prototyped; silent over the six stacks). The table form works and
   reads well, but it puts vocabulary in `default.dta`, where the agent writes and which no
   pack's review covers, and it cannot say `*`. Alternative: admit both.
2. **`(map T)`, and `(list T N)` as a tuple with named components?** Recommended: neither
   now. `(list T N)` covers 11 of 16 list fields; `(desc "x y w h")` names the components;
   `(map T)` would nest Duel's move shape in the fighter's, which `(at fighters * moves)`
   already says.
3. **`kVersion` 7 now, or shapes from text only until the next format bump?** Recommended: 7
   with the change. Once shapes leave the tree, a cache without them loses every shape on
   load.
4. **`#require` by pack name, once `#pack` is a directive?** Recommended: a later step. The
   session learns a pack's name only by reading its file, so a name needs an index (the
   project file could hold it).
5. **reader-forms' tree-shaped conditional (`#(ifdef N (then ...) (else ...))`)?**
   Recommended: never. It loses the bracket-spanning `#ifdef` real DTA uses, and
   `read_structure` already gives tools the conditional tokens.
6. **Drop `#define NAME (body)`?** Recommended: re-run the raw gate with the rule removed
   first, and drop it only if 464 of 464 still parse; it is two lines of the repository's
   corpus and the grammar's one layout-sensitive rule.
7. **Raise the shape budget for arity, or move `read_field` into the data library?**
   Recommended: neither. The arity is in, at 567 of 600, and the session reader and `count`
   leave about 28 lines spare.
8. **Should `layer_effect` and `diff` list a layer's declarations?** Recommended: yes, as
   entries `#shape NAME` and `#pack NAME`, so staging a received layer or reviewing one
   shows what it declares, as it shows a shape change today.

## 10. Recommendation

**Adopt in part.** Take the `#`+letter rule now: it is two lines in the lexer and printer,
closes D101's lone-`#` mismatch and reads 59 community files, and nothing in the repository
depends on the old error. Take the arity, which is built and tested and stands alone. Take
`#pack`, `#origin` and a top-level `#shape` together, as one step with `kVersion` 7, the
policy classification, the three `check` warnings and the governs-nothing finding: they end
position sniffing, restore spec §6's per-shape form, and close the received-layer gap this
prototype found, for about 100 core lines, a third of what is left, and no measurable
runtime gain or loss. Hold `[NAME ...]`-only until the raw gate is re-run, and defer the
at-the-table form. The next step is a design note in `dev/` for that one step, with the
D-entry's measurements re-taken on a quiet machine (`bench_compare.py` on `bench_parse`,
`count_instructions.py`'s parse case, `bench_cache`) and the migration as its own commit.
