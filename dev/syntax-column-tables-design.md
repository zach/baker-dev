# Column tables: a design note

*Syntax experiment `column-tables`, on the branch
`claude/fervent-cray-l1kyf0-syntax-column-tables`. The prototype is
`experiments/syntax-column-tables/` (a front end over Baker's public headers; the core is
untouched). It merges two proposals, page-shape's `column-tables` and machines'
`columns-tables`, which the synthesis rejected on the people judge's fatal; this brief exists
to test the fix the judges named. Every number below was measured on this branch, on a
shared 4-core machine that other builds were loading (wall-clock rows are noisy; instruction
counts are not).*

## 1. Thesis

A keyed table, the content convention for anything packages extend (D142), writes every
field name on every row: Duel's five moves restate `button startup active recovery damage
hitstun blockstun push` five times (`stacks/duel/data/default.dta:20-33`), and the Ledger
exporter writes `(base_price 2) (demand 300) ...` on every line of a sheet it read as a
grid, 135 characters wide (`stacks/ledger/data/economy.dta:18-21`). Column tables let a
parenthesized array declare its fields once, `#columns (button startup ...)`, and write each
record as a row: its key, then **exactly one cell per column, whatever the cell's form**. An
atom is the column's one value, a parenthesized list is the column's values whatever its
first element, a `{command}` is the column's one value, and `_` is a hole: the row has no
such field. The reader expands each row into the nested record every reader sees today, so
`(jab light 4 ...)` under the header is the tree `(jab (button light) (startup 4) ...)`. The
header is a directive: it is consumed by the reader and never in the tree, so merge,
layers, provenance, shapes, `check`, the cache and the JSON documents see exactly what they
see now.

It is the radical move worth testing because it relieves the pain the corpus shows most
often (spec §0's designer-editable values, read and tuned as the grid a designer thinks
in): repeated names that a person must read past and can misspell (a misspelled field is
only a shape warning, `docs/language.md:290-292`), lines too long to review, and an
exporter that restates keys. The synthesis rejected the first grammar because a list cell
such as `(jab low_kick)` is lexically a tagged array, so "cells end at the first tagged
array" read the headline tables wrong, silently. This prototype tests the fixed-count rule
the judges stated. **It removes that fatal: both headline examples read to the right tree.
It also finds two more silent misreads of the same family, which a guard and a stricter
count close, and the stricter count gives up the brief's trailing children.** Section 2
gives the grammar the prototype settled on and section 9 asks the owner to choose.

## 2. The syntax

### Tokens and forms

Nothing new is lexed: `#columns` is a directive (an unknown directive is an error today,
D11, so no existing file holds one) and `_` is the bare symbol it is today.

- **Header.** `#columns (NAME ...)`, written directly inside a `(` array (the *table*), its
  list opening on the directive's line (the layout rule `#define NAME (body)` already has).
  Each NAME is a bare or quoted symbol, never macro-expanded. Errors, each located by file,
  line and column: a header at a file's top level ("#columns needs an enclosing
  parenthesized array"), inside `{}` or `[]`, without a list on its line, an unclosed list,
  an entry that is not a symbol, `_` as a name, a name twice, an empty list. A second header
  in the same table replaces the first for the rows after it.
- **Row.** Every child of the table after the header is a row: a `(` array whose first node
  is a symbol, its **key**, which stays the array's tag. A scalar, a command or a property
  where a row goes is an error ("under #columns every child of moves is a row, (key
  cells...), not 5"), and so is a row whose first node is not a symbol. Directives between
  rows (`#ifdef`, `#define`, `#include`) pass through with their arguments; `(key #remove)`,
  a layer's removal (D143), passes through as written.
- **Cells.** With N columns, the N nodes after the key are the cells, *as written*: a macro
  is one cell however many values it expands to, so `BOX` defined `[BOX 0.3 1.3]` under
  `hitbox` is `(hitbox 0.3 1.3)`. Cell i becomes the child `(NAME_i ...)`:

  | Cell as written | Child | Note |
  |---|---|---|
  | `4`, `0.12`, `light`, `"text"`, `'odd name'`, `$var` | `(startup 4)` | one atom |
  | `(0.3 1.3 0.55 0.2)` | `(hitbox 0.3 1.3 0.55 0.2)` | a list: its elements are the values |
  | `(jab low_kick)`, `(-> wheat)` | `(cancels jab low_kick)`, `(recipe -> wheat)` | whatever its first element |
  | `()` | `(optional)` | the column with no values |
  | `((x 1) (y 2))` | `(col (x 1) (y 2))` | a nested record is a list of records |
  | `{>= {econ.total_earned} 1500}` | `(unlock {>= ...})` | a command or a property is one value |
  | `_` | nothing | a hole: the row has no such field |
  | `'_'` | `(col _)` | the symbol `_` as a value is quoted |

- **Fewer than N cells** is an error that names the row and what is missing:
  `row jab has 7 cells, #columns declares 9 (missing push hitbox)  (t.dta:3:4)`.
- **The guard.** A list cell whose first element is the *bare* name of one of the header's
  columns is an error: `row k: the cell for a, (a 1), begins with the column name a: a cell
  is its values alone, and a list that begins with a column's name quotes it ('a' ...)`.
  Without it a record written in today's nested form under a header, `(k (a 1) (b 2))`,
  reads as `(k (a a 1) (b b 2))` with no error: each tagged field is a list cell by the
  fixed-count rule. The printer quotes such a first element; in the corpus no list cell
  needs it (section 5).
- **After the cells**, two rules were built (`ReadOptions::trailing_children`):
  - *The brief's rule* (the prototype's default): the nodes after the N cells are the row's
    ordinary children, as written, `(crouching 1) (guard low)`. A `_` there is an error,
    and so is a child tagged with a column's name ("row k writes a as a cell and as a child;
    write it once, as the cell"): an error, not a later-wins merge, because later-wins would
    make a cell dead text that still looks like data.
  - *The strict rule:* a row is its key and exactly N cells. More is an error naming the
    first extra node: `row low_kick has 6 cells, #columns declares 5 (the first extra is
    (guard low)): a row is its key and one cell per column`.

  The strict rule exists because the brief's rule leaves a silent shift (section 2,
  "Answers"): with children after the cells, a dropped cell pulls the first child into the
  last column. The printer never writes children after the cells, so everything it prints
  reads under both rules.

Outside a headed table nothing changes, and `_` outside a cell is an ordinary symbol.

### What each tree node comes from

The tree is unchanged: no node type, no flag, no array kind. A headed row closes as the
`Type::Array` tagged by its key, holding one `Type::Array` per non-hole cell, tagged by the
column's `Symbol`, then its children. Values keep their own types (Int, Float, Symbol,
String, Command, Property, Var); a list cell's elements are spliced as values. Each
synthesized child carries the file and the line of its cell (the row's line when the row is
on one line); the prototype checks file and line on the row and on a cell, and an included
file's rows carry that file's name.

### Edge cases resolved

- **Quoting.** A quoted column name is the symbol (`#columns ('odd name' b)`); a macro's
  name in a header is written quoted when the rewrite inserts it, so it is never expanded
  (`[startup 99]` defined above the table does not touch the column `startup`). A value that
  is the symbol `_` is `'_'`; a list cell that begins with a column's name quotes it.
- **Numbers.** A cell is a token, so `-3`, `0x1F` and `1.5e3` are cells as they are values.
- **Comments.** Between cells and after a row, as anywhere; the exporter's `; Goods, row 2`
  stays at the end of its row (tested).
- **Empty arrays.** `()` is the column with no values (`(optional)`); `_` is no column.
- **`#remove`.** `(key #remove)` passes through as a row; a `#remove` in a cell is an error
  ("row k: #remove cannot be a cell (the cell for a)"), since D143 allows it only as a
  key's only value.
- **Macros.** One written token, one cell (above). A macro cannot be a row's key (an error).
- **Directives.** Between rows they pass through; in a cell position a directive is an
  error ("#ifdef cannot be a cell (the cell for a)"). An `#include` inside a headed table
  splices that file's forms as nested children: a header does not reach into another file.
- **Packs.** The `(pack ...)` and `(origin ...)` declarations are a file's first forms, not
  tables; nothing about levels or the one-way rule changes. A pack file may hold headed
  tables like any file.
- **Shapes.** Not tables: see "Answers" below. **Scenarios** and task packs are ordinary
  data; a scenario's table may be headed like any other.
- **Nesting.** A cell may hold a headed table of its own (tested), though no file needs it.

### The corpus, before and after

Duel's frame data, `stacks/duel/data/default.dta:20-33` (14 lines, 1,009 bytes), and as
the sample writes it under the brief's rule (7 lines, 733 bytes; the two optional fields
follow the cells as children):

```dta
      (moves
         (jab       (button light) (startup 4) (active 2) (recovery 7) (damage 30)
                    (hitstun 12) (blockstun 9) (push 0.12) (cancels jab low_kick)
                    (hitbox 0.3 1.3 0.55 0.2))
         (low_kick  (button kick) (crouching 1) (startup 6) (active 3) (recovery 12)
                    (damage 45) (hitstun 16) (blockstun 10) (push 0.15) (guard low)
                    (cancels palm) (hitbox 0.3 0.1 0.6 0.25))
         ...

      (moves
         #columns  (button  startup active recovery damage hitstun blockstun push cancels        hitbox)
         (jab       light   4       2      7        30     12      9         0.12 (jab low_kick) (0.3 1.3 0.55 0.2))
         (low_kick  kick    6       3      12       45     16      10        0.15 (palm)         (0.3 0.1 0.6 0.25) (crouching 1) (guard low))
         (side_kick kick    7       3      14       60     17      11        0.2  _              (0.35 0.9 0.75 0.25))
         (heavy     heavy   9       3      18       90     20      14        0.25 (palm)         (0.35 1.1 0.7 0.3))
         (palm      special 12      4      24       120    30      12        0.6  _              (0.2 0.8 0.8 0.8)))))
```

Under the strict rule `crouching` and `guard` are columns, as the printer writes them; at
the printer's 96 columns the header wraps and the names no longer stand over their cells:

```dta
(moves
   #columns (button crouching startup active recovery damage hitstun blockstun push guard
             cancels hitbox)
   (jab       light   _ 4  2 7  30  12 9  0.12 _   (jab low_kick) (0.3 1.3 0.55 0.2))
   (low_kick  kick    1 6  3 12 45  16 10 0.15 low palm           (0.3 0.1 0.6 0.25))
   (side_kick kick    _ 7  3 14 60  17 11 0.2  _   _              (0.35 0.9 0.75 0.25))
   ...
```

Duel's CPU levels and bots, `stacks/duel/data/default.dta:38-48`, already one aligned row a
line; the header removes the names and adds a line per table (only the masher has `mash`;
the other bots hold a hole):

```dta
(bots
   (masher (reaction 40) (block 0)   (punish 0) (aggression 0.1) (combo 0)   (mash 0.05) (approach 0.7)  (decide_every 12))
   (turtle (reaction 10) (block 0.9) (punish 1)   (aggression 0.5) (combo 0.8) (approach 0.35) (decide_every 8))
   ...
(bots
   #columns (reaction block punish aggression combo mash approach decide_every)
   (masher   40       0     0      0.1        0     0.05 0.7      12)
   (turtle   10       0.9   1      0.5        0.8   _    0.35     8)
   ...
```

The generated economy, `stacks/ledger/data/economy.dta:15-22` (`recipe` a list cell whose
first element may be `->`, `unlock` a command cell, the exporter's provenance comment kept):

```dta
(buildings
   (farm (cost 400) (upkeep 6) (rate 20) (recipe -> wheat))   ; Buildings, row 2
   (mill (cost 1500) (upkeep 15) (rate 10) (recipe wheat wheat -> flour) (unlock {>= {econ.total_earned} 1500}))   ; Buildings, row 4
   ...
(buildings
   #columns (cost upkeep rate recipe                 unlock)
   (farm     400  6      20   (-> wheat)             _)                                ; Buildings, row 2
   (mill     1500 15     10   (wheat wheat -> flour) {>= {econ.total_earned} 1500})    ; Buildings, row 4
   ...
```

Stuntline's kit, `stacks/stuntline/data/default.dta:13-23`, with holes where a kind of
piece has no such measure (24 of 70 cells are holes):

```dta
(pieces
   #columns    (kind     length rise radius angle in   out)
   (straight_20 straight 20     _    _      _     flat flat)
   (kicker_15   ramp     15     3    _      _     flat lip)
   (left_45     turn     _      _    40     45    flat flat)
   ...
```

A shape's fields, `stacks/duel/packs/game/duel_core.dta:17-29`, stay as they are (see
"Answers"):

```dta
   (move (at fighters * moves) (keyed)
      (button    symbol (one_of light heavy kick special))
      (crouching bool (optional) (desc "With down held"))
      (startup   int (range 1 60) (kind frames) (desc "Frames before the first active frame"))
```

### Answers to the brief's questions

- **Is "exactly N cells, whatever their form" a rule a designer can hold, and does a dropped
  cell never shift silently?** The rule is short and the short-row error is plain (above).
  But *as briefed* the answer is no: a dropped cell in a row that carries children shifts
  silently. Drop `(palm)` from low_kick's row and the hitbox list becomes `cancels` and
  `(crouching 1)` becomes `hitbox`, with no error (the test "the brief's rule: a dropped
  cell in a row with children shifts silently" reads exactly that tree). Only a shape sees
  it, at `check`, and only where the columns' types differ. The count catches a dropped
  cell only in rows without children. Under the strict rule every dropped or extra cell is
  a count error; what no count can see is two cells of one type swapped, `4 2` for `2 4`,
  which a shape's ranges catch when they differ. So the rule a person can hold is the
  strict one: *count the cells, count the columns*.
- **Should a list cell with a symbol first ever be a tagged child?** No: by construction a
  parenthesized node in cell position is a cell, and the corpus needs it (Duel's `cancels`,
  every `recipe`: 10 of the corpus's 30 list cells, section 5). But "whatever its form"
  needs one exception, the guard: a list whose first element is a column's bare name is an
  error, because that is what a nested field written under a header looks like, and it would
  otherwise read wrong silently.
- **Is `_` the right hole?** Yes. No file of the 68 holds a bare `_` (searched), and the
  raw-DTA gate (D101) cannot meet it: `_` is a hole only in a cell, and a cell exists only
  under `#columns`, which every raw DTA file lacks (an unknown directive there is an error).
  The community corpus D101 describes is outside the repository and this machine, so its
  bare `_` symbols could not be counted; they would matter only to a converted file that
  gained a header. `_` reads as an empty cell to a spreadsheet user, sorts nowhere special,
  and is one character; `-` is worse (it starts numbers and `->`).
- **Should shapes be tables of fields?** No, the shape grammar is a separate matter. A
  shape's field is `(startup int (range 1 60) (kind frames) (desc ...))`: its type is a
  positional *value* before the tagged children, and a row's cells always become tagged
  children, so a table cannot write a shape without a new "value cell" or a new shape
  grammar (`(type int)`). The fields are also sparse: `desc` on some, `one_of` on few. The
  printer's heuristic rejected the shape tables of all six packs (a field holds arrays).
- **How does a layer override one cell?** In the layer's own file, as today: the nested
  `(cpu (easy (reaction 26)))`, or a path row on the path-rows branch. A headed row with
  holes, `#columns (reaction block ...)` then `(easy 26 _ _ _ _ _ _)`, does work (holes add
  nothing, so merge by tag keeps the rest), but it repeats the header and asks the writer
  to count to the cell, and a cell one place off sets the wrong field with no error. The
  nested form it is.
- **What does write-back do with a cell, and what does `write_back_to` insert in a headed
  table?** A cell is one token (or a list's bracketed tokens), so `write_back` replaces it
  and the diff is that token (tested through a Config). For a missing key the prototype
  writes, and the note recommends: a **headed row with holes** for a row the table lacks,
  `(uppercut _ 5 _ _)`, because a nested row is not legal under the fixed-count rule (it is
  a short row, or with N fields the guard's error); the **hole filled** for a column the row
  lacks; and for a field that is not a column, a child after the cells under the brief's
  rule, a refusal under the strict rule ("row jab has no column for guard: add it to the
  table's #columns"). The proposal's claim that a nested row "is legal under the header"
  does not survive the fixed-count rule.
- **Is a header a directive or data?** A directive, consumed and never in the tree. In the
  tree it would be a node every reader, merge, layer, cache and JSON document must skip or
  carry, and a layer above could "merge" a header. A tool that wants it reads it where it
  is written: `read_structure` (D220) gives a Directive token and a bracket, which is all the
  prototype's own reader uses.
- **A child named like a column?** An error, not a later-wins merge (above).

## 3. What it keeps and what it changes

The prototype is a text rewrite: `read_structure`'s tokens locate each header, row and
cell, and the rewrite inserts `(NAME ` and `)` around each cell, blanks the header and
the holes to spaces (keeping every newline, so every line stays where it was), and hands
the nested text to `baker::read_string`. Text without `#columns` goes to `read_string`
untouched.

| Part | With the prototype | With a core implementation |
|---|---|---|
| Lexer | unchanged | unchanged; `fuzz/dta.dict` gains `#columns` and `_` |
| Parser | unchanged; the rewrite runs before it | `directive()` gains `columns`; FrameInfo gains a column run and a cell count; each cell built as its token is read (section 4) |
| Printer (`write_dta`) | unchanged; the experiment's own printer heads tables | unchanged: promote, `results.dta`, reply text and patches stay nested, since the tree never held a header |
| Write-back (D34, D132, D183, D226) | re-provided by `HeadedFiles`: a cell's token, a list cell's values (their number changed inside the cell's brackets), a hole filled, a headed row with holes, a field after the cells; D183's integer form and D226's number text come from Baker's own plan, unchanged. `promote`, which writes many entries in one text, is refused for a headed file | each cell recorded with its own span; `write_back_to`'s insertion learns the two headed cases (fill a hole, write a headed row); promote works as today |
| Source maps (D33, D128) | spans point into the rewritten text: lines are right, columns inside a rewritten row are not; `HeadedFiles` maps write-back's change back | each synthesized child's extent is its cell, its tag's span non-literal (empty file), as `#merge`'s rebuilt arrays are recorded (D135). D128's reservation (an array every 14 bytes) under-counts a headed file: one doubling |
| Tolerant read (D220) | unchanged: the reader is built on it | unchanged; an editor that labels a value by its tag must map a cell's index to the header's name |
| Errors | the rewrite's own errors located; a parser error inside a rewritten row has its column mapped back to the text as written (tested) | located by the parser directly |
| Provenance, `explain` | each child carries its cell's file and line | the same |
| Layers, merge, `#remove` | unchanged: the tree is the nested record; `(key #remove)` passes as a row | unchanged |
| `check`, the demand report | unchanged (they see the tree) | unchanged; a check that the header names the shape's fields is a later tool finding |
| Shapes, reference JSON | unchanged; `(keyed)` shapes govern the rows | unchanged |
| The console's requests | unchanged: replies print the tree, nested | unchanged |
| The project file | unchanged | unchanged |
| Binary cache | unchanged: it stores the tree; `kVersion` stays | unchanged; `tree_estimate` should count cells when a header is in the sampled 16 KB, or a table-heavy load takes a second arena block |
| Docs and corpus | none | `docs/language.md` (Data: tables, the directive table), the cookbook's keyed tables (`docs/cookbook.md:466-481`), the agent docs; `tests/corpus/tables.dta`; the stacks' tables and the Ledger exporter's `table()` (`stacks/ledger/tools/export_economy.py:71-91`) if the owner converts them |

What breaks, and why it is acceptable in a prototype: a tool that reads a SourceMap's
spans directly (an editor's go-to) gets columns in the rewritten text; promote into a
headed file is refused; and the reader lexes twice. A core implementation has none of
these.

## 4. Cost, measured

**Parse speed.** `compare_parsers` (Baker's reader against the experiment's, interleaved,
best of 15 each) and cachegrind instructions per read (`count`, the difference between one
and two reads, as `tools/count_instructions.py` does). The machine was shared with seven
other builds, so the three wall-clock runs disagree with each other by up to 50%; the
instruction counts are exact.

| Text (4 MB) | Run | Baker MB/s | Experiment MB/s | Time ratio |
|---|---|---|---|---|
| The parse benchmark's text, no headers | 1 | 73.5 | 103.4 | 0.673 |
| | 2 | 76.7 | 70.8 | 1.028 |
| | 3 | 106.6 | 101.7 | 0.993 |
| Generated keyed tables (10 rows of 6 or 7 fields a table) | 1 | 115.3 | 12.1 | 7.198 |
| | 2 | 80.4 | 9.5 | 6.408 |
| | 3 | 113.2 | 15.0 | 5.710 |
| The stacks' 11 data files, own forms, repeated | 1 | 82.2 | 20.9 | 3.638 |
| | 2 | 119.2 | 27.0 | 4.089 |
| | 3 | 112.7 | 29.4 | 3.545 |

Each row times Baker on the text as generated and the experiment on the same content as its
printer writes it, so the no-header row compares 4,194,408 bytes with 3,973,706 (the
printer's layout is tighter). Run 1's 0.673 there is the machine, not the reader: the
instruction counts of the same reads agree within 0.2%.

| Instructions per read | Nested text, Baker | Headed text, experiment | The lexer alone (`read_structure`) | The rewrite alone |
|---|---|---|---|---|
| No headers, the same 4,194,408 bytes | 388,173,959 | 388,640,093 (+0.12%) | 298,592,456 | 841,693 |
| Generated tables | 487,363,606 (4,194,690 B) | 1,628,050,940 (3,163,587 B; 3.34x) | nested 379,193,290, headed 216,711,867 | 1,127,408,734 |
| The stacks' data | 391,646,415 (4,209,506 B) | 977,505,743 (3,897,726 B; 2.50x) | nested 299,091,301, headed 263,140,748 | 582,705,203 |

- **Without headers the cost is the scan:** the reader looks for `#columns` once (a
  `find`) and then is `read_string`; +0.12% instructions, all of it the session it sets up
  and the scan. A core implementation would not scan; it would test one frame flag per
  array, which the proposal's go/no-go puts at noise against `bench_parse`'s 1,845,499
  instructions a document (D220).
- **With headers the translator costs 2.5x (the stacks' data) to 3.3x (generated tables)
  Baker's reader in instructions, and 3.5x to 7.2x in time**; it reads table-heavy text at
  9.5 to 15 MB/s, under spec §7's floor of 40 MB/s. It lexes everything twice (once to
  find the cells, once in `read_string`) and its rewrite allocates a string per inserted
  bracket. It cannot be how the syntax ships.
- **What a core implementation would cost** follows from the lexer counts: headed text has
  fewer bytes and tokens to lex and the same arrays to make. Generated tables: 24.6% fewer
  bytes and 49.5% fewer tokens (1,221,263 to 616,303); the stacks' data: 7.4% fewer bytes
  and 15.3% fewer tokens (971,992 to 823,718). Estimating a core read of the headed text as
  Baker's read of the nested text less the lexing it no longer does (`read_structure` is the
  parser's own lexer in tolerant mode plus a bracket walk, D220, so the difference is an
  approximation): generated tables 487,363,606 − (379,193,290 − 216,711,867) = 324,882,183
  instructions, **0.67** of the nested read; the stacks' data 391,646,415 − (299,091,301 −
  263,140,748) = 355,695,862, **0.91**. The cell
  loop's own cost (one two-node array per atom cell, which the nested text also makes, and
  one counter per row) is not in that estimate; it would be measured with
  `count_instructions.py` on a core build.

**Lines and bytes.** The five samples (`experiments/syntax-column-tables/samples/`), the
originals beside their hand-polished headed forms. The headed forms keep every comment and
add a few that explain the table, so the second table counts code only (comments and blank
lines out):

| File | Lines | Bytes | Longest line | Code lines | Code bytes |
|---|---|---|---|---|---|
| `stacks/duel/data/default.dta` | 48 → 46 | 3,133 → 2,987 (−4.7%) | 123 → 142 | 36 → 31 | 2,468 → 2,054 (−16.8%) |
| `stacks/ledger/data/economy.dta` | 22 → 24 | 1,496 → 1,406 (−6.0%) | 135 → 105 | 17 → 19 | 1,070 → 915 (−14.5%) |
| `stacks/stuntline/data/default.dta` | 40 → 45 | 1,959 → 2,133 (+8.9%) | 100 → 100 | 31 → 33 | 1,576 → 1,518 (−3.7%) |
| `stacks/hearth/data/default.dta` | 108 → 111 | 5,807 → 5,915 (+1.9%) | 99 → 99 | 96 → 97 | 5,259 → 5,205 (−1.0%) |
| `stacks/contraption/data/default.dta` | 57 → 64 | 2,363 → 2,681 (+13.5%) | 93 → 93 | 52 → 56 | 2,183 → 2,312 (+5.9%) |
| Total | 275 → 290 | 14,758 → 15,122 (+2.5%) | | 232 → 236 | 12,556 → 12,004 (−4.4%) |

The same eleven stacks data files printed by one printer, nested and then headed
(`measure`: the syntax's effect alone, no comments either side): 510 → 413 lines, 17,687 →
16,377 bytes (−7.4%). Duel 127 → 38 lines, 3,133 → 1,932 bytes; economy 34 → 16 lines,
1,143 → 868 bytes. But six files gain a line or more (a header over a table that was
already one row a line), and four grow in bytes, contraption (2,127 → 2,243), Ledger's
default (1,005 → 1,015), and Wayfarer's interior (637 → 689) and north coast (281 → 333),
because padding cells into columns and writing holes costs more than the names it saves in
small, sparse tables.

The longest line of `economy.dta` goes from 135 characters to 105 with its provenance
comments (114 → 84 without). Duel's longest line goes *up*, 123 → 142: a ten-column header
aligned over its cells is wider than any record was.

**The prototype's own size.** `syntax.cpp` 1,207 lines (shared helpers, about 95; the
rewrite and its offset maps, about 515; the write-back adapter, about 250; the printer,
about 295), `syntax.hpp` 158, `main.cpp` 354, `tests.cpp` 701. It uses the STL (a tool;
the container rule is for `src/`).

**Projected core cost** against the 15,500-line soft budget (D69, D220; 15,190 used today,
310 free): about 110 to 130 lines, over the proposal's ~90. `directive()` gains `columns`
with its errors (~30); FrameInfo gains a column run (start and count into a parser-local
list held in the parser's scratch, so MSVC's empty-`Vec` allocation, D62, is not paid) and
a cell count (~5); a cell opens an implicit `(NAME` frame as its token arrives, so a list
cell is built once with its tag and an atom cell closes at once (~50: holes, the count
errors, the guard, the strict rule); source-map spans per cell (~10); `tree_estimate`
counting cells (~5); the write-back insertion's two headed cases (~15, in
`config_write.cpp`). No node layout or allocation change: the arrays are the nested form's
arrays, made in the load's arena.

## 5. Payoff, measured where possible

`stats` over the 68 corpus files, each file's own forms (what a file `#include`s or
`#require`s is counted in its own file, once). A *candidate* is a tagged array of two or
more keyed records whose children are all tagged arrays; *flat* means every field's values
hold no array, so each field can be a cell:

| | All 68 files | The stacks' data files |
|---|---|---|
| Keyed-table candidates | 42 | 29 |
| Of them flat: tables, rows, fields | 24, 110, 383 | 21, 102, 359 |
| Headed by the printer | 20 | 17 |
| Their rows, cells, holes | 78, 313, 63 | 70, 289, 59 |
| List cells (keep their brackets) | 30 | 27 |
| Header names written | 86 | 76 |
| Parentheses removed | 566 | 524 |
| Field names removed | 313 | 289 |

The printer heads a candidate when every field is flat, every row agrees on one column
order, the cells fill at least 60% of the grid, and every row fits 96 columns. Of the 29
candidates in the stacks' data it heads 17 (17 of the 21 flat ones): it refused 8 for a
field holding arrays (Hearth's objects and their `advertise` pairs, Contraption's levels,
Wayfarer's nested records), 3 for coverage (Contraption's kinds, Ledger's strategies, one of
Wayfarer's) and 1 for width (Stuntline's tracks, whose descriptions are long strings); none
for order. The shape tables of all six packs were refused.

This corrects the page-shape proposal's numbers (21 tables, 120 rows, 414 cells, 828
parentheses, 35 holes over the 68 files). Counting each file's own forms once, the corpus
has 24 flat keyed tables of 110 rows and 383 fields, and the printer heads 20 of them with
313 cells. Parentheses removed are not twice the cells, since a list cell keeps its pair
(30 of 313 are lists): 566, not 828. And a 60%-filled grid writes 63 holes, not 35. 10 of
the 30 list cells begin with a symbol (`(jab low_kick)`, `(-> wheat)`: the reason the
fixed-count rule exists); none begins with a column's name, so the guard costs the corpus
nothing.

- **A designer** reads Duel's moves as the frame-data sheet it is: every startup in one
  column, 14 lines become 7, and a new move is one line. Stuntline's kit reads as a kit,
  with its holes showing which kinds have which measures. In a table of five rows the
  header is on screen; in a table of fifty it is not, and a cell is then read by counting.
- **An engineer** loses nothing at run time: the game reads `fighters.kestrel.moves.jab.
  startup` and the shape governs the same record.
- **A tool** (the exporter) writes the row it read: economy's rows drop from 114 code
  characters to 84 and the header is the sheet's header row. An editor can align a table
  from `read_structure`'s tokens.
- **An agent** retuning a cell edits one token on a line of about 80 characters instead of
  one of 110 to 135. It must count to the cell, and a one-off cell is silent when both
  columns hold numbers; it should write through write-back or a layer instead, as it does.
- **Errors prevented:** a misspelled field cannot occur in a row (the header is the one
  place it is written; today `(startpu 4)` is only a shape warning); a short row is an error
  at load where today a forgotten field is just absent; under the strict rule an extra or
  missing cell is always an error.
- **Errors introduced:** two cells swapped (silent unless a shape's ranges differ), and,
  under the brief's rule, a dropped cell beside a child.
- **Diffs.** Retuning a cell is a one-line diff either way. Adding an optional field to one
  record is one line today; under the strict rule it is the header and every row (a new
  column, a hole in each), six lines for Duel's moves, and every row a merge conflict for
  another branch tuning the table.

## 6. Objections

- **Cost per byte (Acton: "if you don't understand the data you don't understand the
  problem", slide 30).** On Baker's real tables the bytes fall 7.4% and the tokens 15%: the
  stacks' tables are small and already one row a line. On dense generated sheets the
  tokens halve. *Answer:* a core implementation reads a headed file for less than its
  nested form, and a file without a header for the same; the translator does neither and
  must not ship.
- **A second parser.** The prototype is one (it lexes the text, then Baker lexes it again).
  *Answer:* in the core it is one grammar with one directive, read in the frame the parser
  already keeps; but every tool outside the repository that reads DTA (the tree-sitter
  grammar, an editor's outline, a modder's script) must learn that a row's cells are named
  by a header it may not see.
- **Build time, not run time ("solve the problem you have", slide 37).** The one generated
  file, economy, is the sheet's export: the sheet is the source, and the text is read by the
  game and reviewed in diffs. The header shortens the review; it is not needed to read the
  data. *Concession:* for generated text the exporter could equally wrap its lines.
- **"Solving a problem you don't have."** Of eleven stacks data files printed both ways, two
  gain much (Duel, economy), two a little (Hearth, Stuntline), three do not change, and four
  grow. Duel's cpu and bots tables are already aligned grids in today's syntax.
  *Concession:* most of the grid a designer reads is available today from a formatter that
  aligns a keyed table's fields into columns, with no grammar change and no positional risk;
  that formatter is the cheaper first step.
- **"If you have different data, you have a different problem" (slide 33).** A headed table
  is the same data: the tree, the merge and every reader see the nested record. *Answer:*
  this is the proposal's strength, and it holds; nothing below the parser changes.
- **DTA heritage: positional data is what keyed records ended.** Rock Band's `songs.dta`
  scaled to thousands of downloadable songs because each record named its fields (the
  cookbook's keyed tables); a patch, a merge and a person agreed on the names. A row of
  twelve numbers is readable while its header is on screen. *Answer:* the rows stay keyed,
  so packages still add and remove rows by key; only the cells are positional, and the
  strict count makes a misplaced cell an error in all cases but a swap. *Concession:* the
  cells are positional, and that is the trade.
- **The corpus and the modding community.** No raw DTA file can hold `#columns`, and a
  converted file with a header is an opt-in; nothing a modder has today changes meaning.
  But a modder's tool that reads DTA text and sees `#columns` fails on an unknown
  directive until it is taught the form.
- **The fatal, again.** The people judge's fatal is removed: both headline examples read
  right, and a short row is a located error. This prototype found two more silent misreads
  of the same family (a nested row under a header; a dropped cell beside a child). The guard
  closes the first at no cost to the corpus; the strict rule closes the second at the cost of
  the brief's trailing children and diff locality. Whether a person can hold "count the
  cells, count the columns" across a twelve-column table is not something a test can show.

## 7. Migration

- **Both forms stay valid forever.** A file without a header is today's file; a headed
  table and nested rows may sit in different files of one stack, and a layer above a
  headed file writes nested rows. Nothing has to move.
- **Converting a file.** The prototype's printer prints a *tree*, so `to-new` loses
  comments and prints a file's `#require`d forms inline; the samples were hand-polished
  from it. A converter for real files would rewrite tokens in place (the reverse of this
  rewrite: drop each cell's name and brackets, insert a header), which is not built.
- **The exporter** (`stacks/ledger/tools/export_economy.py`) writes a header and cells, its
  provenance comment unchanged; about ten lines.
- **The one-way rule and packs:** unchanged (a header is a file's own business and declares
  no macro). **Layers and live-ops overlays:** a received layer (`push_layer_text`) may be
  headed; `LayerPolicy` sees the tree. A layer overrides a cell in the nested form (section
  2). **Promote** into a headed file needs the core's per-cell spans.

## 8. What the prototype proved, and did not

Built (`experiments/syntax-column-tables/`): the reader (a rewrite over `read_structure`'s
tokens into nested text, read by `read_string` in a session whose reader rewrites included
files too), both rules after the cells, the guard, every error located by file, line and
column; `HeadedFiles`, a Config reader and writer that re-provide write-back into headed
files; the printer with its heuristic; the CLI (`to-new`, `to-dta [--strict]`, `desugar`,
`check`, `stats`, `measure`, `bench`, `bench-table`, `bench-stacks`, `count`); five samples.

Passed (`ctest -R syntax_column-tables`, 34 cases, and the same tests under
AddressSanitizer and UndefinedBehaviorSanitizer):

- **The corpus round trip:** all 68 files, Baker's reader → the printer → the experiment's
  reader → the same tree, with no file skipped and no failure (`check`: "68 corpus files, 0
  failures, 0 the experiment's"), under the brief's rule and under the strict rule.
- **The two headline examples** from the rejection, verbatim: `(jab light 4 2 7 30 12 9
  0.12 (jab low_kick) (0.3 1.3 0.55 0.2) (crouching 1))` reads as `(cancels jab low_kick)`,
  `(hitbox 0.3 1.3 0.55 0.2)` and the child `(crouching 1)`; `(mill 1500 15 10 (wheat wheat
  -> flour) {>= {econ.total_earned} 1500})` as `(recipe wheat wheat -> flour)` and `(unlock
  {...})`.
- **A case for every rule:** atom, string, property, variable and quoted cells; a list cell
  with a symbol first; an empty list cell; a command cell; a hole; children after the cells;
  a second header; children before the header; a removal row; directives between rows; a
  macro cell; quoted names and keys; a table nested in a cell; and every error: a short row
  (with the missing columns named), `_` with no column, `_` as a key, a non-row child (a
  scalar, a command, a row keyed by a number, an array or a string, an empty row), each of
  the header's eight errors, a directive cell, a child named like a column, the guard, and
  the strict rule's extra cell.
- **Errors located as written:** a parser error inside a rewritten row reports its column
  in the text as written (`'{' from line 3 is closed by ')'` at 3:25, not 3:33).
- **Write-back through a Config:** a cell's one token; a list cell's values, their number
  changed; a hole filled; a missing row written as a headed row with holes; a field that is
  not a column written after the cells (and after a trailing hole); the strict rule's
  refusal leaving the file untouched; promote refused, and one entry at a time accepted.
- **Robustness:** every prefix of the five samples and 2,000 random three-byte mutations of
  each, under both rules (about 50,000 reads), read or fail with a located error; none
  crashes. (The first run of this test found a crash in the prototype's own file reader on a
  directory path, fixed.)
- **The samples** read to their originals' trees (Duel's with its fields in another order,
  since two fields follow the cells).

Did not: implement anything in the core, so the core's instruction cost of a header is an
estimate (section 4); a text-preserving converter; promote into a headed file; source-map
columns for an editor; a tree-sitter grammar; a check of a header against the shape; the
D101 corpus's bare `_` count, which is not on this machine; any person reading a table.

## 9. Open questions for the owner

1. **Adopt column tables into the language?** Recommended: *not now*; take the aligning
   formatter first (an option of the printer that pads a keyed table's fields into columns,
   no grammar change) and see whether designers still want the header. The alternative is
   to adopt the strict grammar of this note.
2. **If adopted, which rule after the cells?** Recommended: *the strict rule*: a row is its
   key and exactly one cell per column, so every dropped or extra cell is an error. The
   alternative, the brief's children after the cells, keeps optional fields off the grid and
   adds a silent shift.
3. **If adopted, the guard?** Recommended: *yes*, a list cell may not begin with a column's
   bare name; it costs the corpus nothing and closes the nested-row misread.
4. **The hole's spelling?** Recommended: *`_`*, reserved only in a cell, with `'_'` for the
   symbol.
5. **Shapes as tables?** Recommended: *no*; the shape grammar is a separate matter.
6. **A layer's override of one cell?** Recommended: *the nested form* (or a path row), never
   a headed row with holes.
7. **`write_back_to` into a headed table?** Recommended: *fill the hole; write a headed row
   with holes for a missing row; refuse a field that is not a column*, naming the header
   to change.
8. **Should Baker's own printer ever write a header?** Recommended: *no*; promote, results
   and replies stay nested, and only a converter writes headers.

## 10. Recommendation

**Decline for now, and keep the strict design on record.** The fixed-count rule does what
the judges asked of it: the headline frame-data and economy rows read right and a short row
is a located error. But the prototype shows the family of silent misreads is wider than the
fatal: a pasted nested row and a dropped cell beside a child both read wrong without the
guard and the strict rule, and the strict rule gives up the brief's trailing children and
makes an optional field a column every row must hole. Measured against Baker's real files
the payoff is narrow: two of eleven stacks data files gain much (Duel's moves, 14 lines to
7; economy's rows, 135 characters to 105), the stacks' printed data loses 7.4% of its bytes,
the five samples' code lines go from 232 to 236, and small sparse tables grow. Against that
stand ~120 core lines, a second spelling of every record for every tool outside Baker to
learn, and positional cells a person or an agent must count. The next step is the cheap one
the objections point to: an aligning option in the printer for keyed tables, today's syntax
laid out as a grid; if designers still ask for the header after using it, build the strict
grammar of section 2 in the parser behind a CMake switch and hold it to
`count_instructions.py` against `bench_parse`.
