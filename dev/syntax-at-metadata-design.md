# Metadata by spelling: an `@` tag is never a key (a syntax prototype)

*The at-metadata idea (merged from at-attributes, at-annotations, at-metadata and
margin-bars), prototyped on branch `claude/fervent-cray-l1kyf0-syntax-at-metadata`. The core
change is behind `-DBAKER_AT_METADATA=ON` and off by default; the migration, the round trip,
the benchmarks and the tests are in `experiments/syntax-at-metadata/` and
`tools/migrate_dta.py`. Numbers are from this machine (4 cores, shared with seven other
builds while they were taken), Release.*

## 1. Thesis

Metadata is spelled, not placed. A tagged child whose tag begins with `@` is metadata, and
nothing else is: `(hit_pause 6 (@range 0 30) (@desc "Frames both fighters freeze"))`,
`(move (@at fighters * moves) (@keyed) (startup int (@range 1 60) (@kind frames)))`,
`{func boss.find_cover ($player) (@status stub) {nearest_marker ...}}`. The text needs no new
token (the lexer already reads `@range` as a symbol, and no bare atom in the 68 corpus
files begins with `@`) and the tree no new node: `(@range 0 30)` is an Array whose first
node is a Symbol. What changes is the reading. A value ends at its first metadata child for
every reader, through `Array::values()`, not only for a bound tweak; and metadata comes
after the value, a located syntax error otherwise.

Today a reader tells metadata from data by position and by a word list. A tweak's value
stops before a trailing run of children tagged with one of seven words (D88,
`tweak.cpp:18-43` on main); D218 made the same rule a public function because tools
repeated it from the documentation and differed; the shape grammar reserves eleven words
(`shape.cpp:16-18`). The pains, by name: a value's metadata is invisible to every reader but
a tweak, so `get<std::array<float, 3>>("cube.color")` of `(color 0.9 0.5 0.2 (range 0 1))`
fails (D88, "Not changed"); a record's field named `kind`, `desc`, `step` or `at` reads as
metadata when it happens to be last, and the corpus has 133 children tagged with a metadata
word that are fields (§5); a misspelt `(rnage 0 1)` is a field, silently; a shape cannot
describe itself, because there `desc` is a field's name (`hearth_core.dta:20`); and a
`{func}` whose body begins with `(status 1)` loses that form to the metadata scan
(`builtins.cpp`). Spelling serves the spec's centrepiece directly: metadata "may equivalently
live in the data file" (spec §4), and with this rule it lives there for every reader, a
tool's and an agent's included, which can tell metadata from data by one token without a
word list.

## 2. The syntax

**Tokens.** None new. `@` is an atom character (`kAtomEnd`, `src/data/text.hpp:41-46`), an
atom beginning with `@` is a symbol (`classify_number` never reads one as a number), and the
printer writes it bare (`is_bare_symbol`, `text.cpp:101-108`). A quoted `'@range'` interns
the same symbol, so the sigil belongs to the symbol's text, not to how it is written.

**Forms.**

```
META   := '(' SYMBOL@ node* ')'      ; SYMBOL@: a symbol whose text begins with '@'
ARRAY  := '(' node child* ')'        ; slot 0 is the tag's place and is never metadata
child  := VALUE | FIELD | META
VALUE  := atom | string | $var | {command} | [property] | an array not tagged by a symbol
FIELD  := an array tagged by a symbol without '@'
rule   := in an ARRAY, no VALUE after the first META
```

Fields may come before, between and after metadata, so `(move (@at ...) (@keyed) (button
...))`, `(household (@desc "...") (desc string))` and `(cottage (desc "Two rooms") (size 14
10) (@note "why"))` all parse. A value after metadata is a `Syntax` error located at the
value: `(x (@range 0 1) 5)` gives "metadata comes after the value: move (@range 0 1) after 5"
at line and column of the `5`. The rule is checked for parenthesized arrays only; a
`{command}`'s forms are positional and keep their order.

**Tree.** Nothing new: Array and Symbol. Two bits of bookkeeping, both in words already
loaded:

- `SymbolEntry::size`'s high bit is set at intern time when the text begins with `@`;
  `Symbol::is_meta()` is one load of a word the symbol's `str()` already reads.
- `Array::flags_` (16 bits, two used by `kSortedForLookup` and `kHasRemoval`) keeps the index
  of the first metadata child in its other 14 bits (0 for none; slot 0 is never metadata).
  `finish()` writes it in the pass that already reads every child's tag and height (D143),
  and finding it adds no load: D77's `tag_key` already reads each tagged child's
  `SymbolEntry::size` (for D125's empty-tag test), so the sigil bit is a test and a branch
  on a word in a register (§4 counts what that and the rest cost);
  `set()` moves it when the slot it replaces is or becomes the first metadata child;
  `sort_for_lookup()` re-runs `finish()`. Past child 16,383 the parser refuses metadata
  ("metadata past child 16383", `ErrorCode::Limit`).

**Reading.** `values()` is the children after the tag up to the boundary: one compare and a
subspan. `metadata()` is the children from the boundary; `meta(tag)` scans the tag run from
the boundary (D77's keys). `find`, `try_find` and paths are unchanged: metadata is a child,
so `cube.color.@desc` reaches it (`Path` splits on `.` and `/` only).

**What today's forms become.** The migrator applies three rewrites and decides nothing else:

| today | at-metadata | rule |
|---|---|---|
| `(hit_pause 6 (range 0 30) (desc "..."))` | `(hit_pause 6 (@range 0 30) (@desc "..."))` | D218's trailing run, in an array with a value |
| `(move (at fighters * moves) (keyed) ...)` | `(move (@at fighters * moves) (@keyed) ...)` | a shape's own words |
| `(startup int (range 1 60) (kind frames))` | `(startup int (@range 1 60) (@kind frames))` | a field's words after its type, `optional` included |
| `{func f ($p) (status stub) (doc "...") ...}` | `{func f ($p) (@status stub) (@doc "...") ...}` | the forms between parameters and body |
| `(ball (kind ball) (pos -6 6.5))` | unchanged | no value: every child is a field |
| `(objective jump (desc "...") (measure {...}) ...)` | unchanged | `desc` is the objective's field |

**Edge cases, resolved.**

- *Quoting:* `('@x' 1)` is metadata; there is no way to write a field whose name begins with
  `@`. No corpus array is tagged so (`baker_syntax_at-metadata count`: 0 in 68 files).
- *Numbers and comments:* unchanged. `(@1 2)` is metadata tagged `@1`.
- *Empty arrays:* `()` is untagged; `(@note)` is metadata with no values.
- *`#remove`:* unchanged (D143). `(x #remove)` removes a key; `(@range #remove)` in a layer
  removes that one metadata child, by tag, as any key (tested).
- *Macros:* spliced before the rule is checked, on the tree. The error names the child; its
  column is found by lexing the array's text again on the error path, so a value a macro
  splices in is located at the array's line, without a column, when the text cannot show it.
- *Directives and packs:* unchanged. A pack header `(pack duel_core (level game) ...)` has a
  value and fields and no metadata.
- *Shapes:* the grammar's words carry the sigil: `@at @keyed @optional @one_of @range @step
  @kind @desc @note`; `(list T)` is a type and `*` a wildcard, unchanged. A field may now be
  named `at`, `keyed`, `kind` or `desc`; a shape may carry its own `(@desc ...)` and
  `(@note ...)`; an unknown `@word` on a shape or field is a located error, and an unsigiled
  word where a field's metadata goes is one too ("write (@range ...)"), where today other
  words are ignored (`shape.cpp:156` on main).
- *Scenarios and objectives:* unchanged. Their `(desc ...)` and a scenario's `(at t {...})`
  are fields of records with no value, which is exactly what the sigil leaves alone (§9, Q4).
- *Commands:* `finish()` keeps a boundary for a command's array too, but nothing reads a
  command's arguments through `values()`: they are read by index.

**The judges' findings, and how the prototype meets them.**

- *Placement:* at-metadata's rule (no metadata before an untagged value; fields interleave
  freely), the one the people judge found consistent with `(household ... (@desc) (desc
  string))` and `(move (@at ...) (@keyed) (button ...))`, and the one under which the tools
  judge's `write_back_to` insertion after the last child writes parseable text. All three
  parse (tested), and the insertion reparses (tested).
- *The store:* the cost judge's design, the bit set at intern time and the boundary in the
  header's spare bits, so `values()` is a compare and a subspan. Its cost is measured in §4,
  and it misses the brief's bar; §10 draws the consequence.
- *The bare `@flag` desugar* the cost judge objected to is not built; it is Q1.
- *The tools judge's three asks:* the D28 interplay is a partition that keeps the boundary
  (tested after `sort_for_lookup`); `check` warns on bare trailing metadata, the silent half
  of the migration (tested); and the migrator emits D218's ambiguous records for a person
  instead of guessing (one in the corpus, §7).
- *at-annotations' reach into `{func}`* is built, as the last step; its scenario `@desc` is
  Q4, answered no.
- *margin-bars* is rejected as a parser rule, as the cost judge's verdict asked (no reserved
  bar, no layout rule over a tree that is byte-identical); it survives as the printer's
  column (§3).

**The corpus before and after.** The samples (`experiments/syntax-at-metadata/samples/`) are
the migrator's renames and the printer's column, hand-polished; each sits beside its
original.

`stacks/duel/data/default.dta:7-13`:

```
(hit_pause 6 (range 0 30) (desc "Frames both fighters freeze on contact: the weight of a hit"))
(buffer 5 (range 1 20) (desc "Frames a press waits for its fighter to be able to act"))
(round_seconds 60 (range 5 600))
```
```
(hit_pause 6        (@range 0 30)   (@desc "Frames both fighters freeze on contact: the weight of a hit"))
(buffer 5           (@range 1 20)   (@desc "Frames a press waits for its fighter to be able to act"))
(round_seconds 60   (@range 5 600))
```

`stacks/cube/data/default.dta:7`, which now reads as three floats through
`get<std::array<float, 3>>` (tested on the converted file):

```
(color 0.9 0.5 0.2 (range 0 1) (desc "Cube color"))
(color 0.9 0.5 0.2 (@range 0 1)                  (@desc "Cube color"))
```

`stacks/duel/packs/game/duel_core.dta:17-20`:

```
(move (at fighters * moves) (keyed)
   (button    symbol (one_of light heavy kick special))
   (crouching bool (optional) (desc "With down held"))
   (startup   int (range 1 60) (kind frames) (desc "Frames before the first active frame"))
```
```
(move (@at fighters * moves) (@keyed)
   (button    symbol (@one_of light heavy kick special))
   (crouching bool   (@optional) (@desc "With down held"))
   (startup   int    (@range 1 60) (@kind frames) (@desc "Frames before the first active frame"))
```

`stacks/hearth/packs/game/hearth_core.dta:19-21`, with the one addition the samples make:
the shape's own description, which today would declare a field named `desc` of type
`"A floor plan..."`:

```
(household (@at households) (@keyed) (@desc "A floor plan and who lives on it")
   (desc      string)
   (size      (list float) (@desc "x z, metres: the floor"))
```

`stacks/contraption/data/default.dta:26`, unchanged and no longer ambiguous in either
order: `(ball (kind ball) (pos -6 6.5))`. `tests/corpus/scripts.dta:6-8`:
`{func boss.find_cover ($player) (@status stub) {nearest_marker {boss position} cover}}`.

## 3. What it keeps and what it changes

Each item carries one verdict: *unchanged*, *re-provided* (changed, and built here behind the
switch), *would need work* (not built), or *breaks* (and why that is acceptable).

- **Lexer and parser: lexer unchanged; parser re-provided.** The parser checks the rule
  once per closed parenthesized array, inline, from the boundary `finish()` has just
  written; it calls out only for an array with metadata or more than 16,383 children.
  Locating the error lexes the array's text again from its bracket, on the error path only
  (`child_at`, `parse.cpp`).
- **Printer: unchanged, plus would need work for the column.** `@range` prints bare.
  margin-bars survives as a printer option, never a parser rule: the translator's printer
  puts the first metadata child of consecutive one-line siblings in one column
  (`align_meta`, `syntax.cpp`), and the corpus round trip proves it is whitespace only. In
  the core it would be `WriteOptions::align_meta` in `put_pretty`, about 15 lines; not built.
- **Write-back (D34, D132, D183, D226): unchanged.** Token replacement takes the untagged
  children as the value, now exactly. `write_back` grows a value written before
  `(@range ...)` (tested). `write_back_to`'s insertion after an array's last child still
  writes text that parses, because a field may follow metadata (tested: inserted after a
  trailing `(@note ...)`, the file reparsed). The refusal "the values of ... are split by
  nested arrays, so their number cannot change" (`config_write.cpp:310-316`) needs no code:
  the parser no longer lets metadata sit between values, so only a field there still meets
  it. D183's integer form and D226's number text are untouched.
- **Edit-mode source maps (D33, D128): unchanged.** A metadata child has spans as any child.
- **The tolerant structure read (D220): unchanged.** It reports `(@range` as a bracket
  tagged `@range`; the translator's placement check runs over it, so an editor can show the
  error without a parse.
- **Provenance and explain: unchanged mechanism, re-provided spelling.** A metadata path is
  `box.width.@note` (the harness's tooltip, `panels.cpp:105-106`; Q3 for the JSON paths).
- **Layers and `#remove`: unchanged.** `merge.cpp` puts the incoming values first and merges
  tagged children by tag, so a merged array keeps the rule; `(width 3)` over
  `(width 2 (@range 0 10))` keeps the range below (as `tests/data/test_config.cpp:70-71`
  means it), and `(@range #remove)` removes one metadata child (both tested).
- **`check` and the demand report: re-provided.** `check` warns once per layer on a bare
  `(range ...)`, or another of the seven old words, directly after a value: "(range ...)
  directly after a value reads as a field; if it is metadata, write (@range ...)"
  (`config_check.cpp`). The demand report's stub scan reads `(@status stub)`
  (`reference.cpp`).
- **Shapes and the reference JSON: re-provided; JSON unchanged.** `shape.cpp` reads the
  sigiled words (§2). `baker.shapes`, `baker.tweaks` and `baker.check` bytes are unchanged:
  they print metadata as JSON fields, not as keys. A shape's own `(@desc ...)` is accepted;
  storing and printing it would need work.
- **The console's requests: unchanged.** Requests are commands, and `@` paths parse.
- **The project file: unchanged.** Its layer flags are bare symbols (`project.cpp:184`).
- **The binary cache: unchanged format.** The boundary rides in the array header as
  `kHasRemoval` does (D143), and a cache round trip keeps it (tested). `kVersion` is not
  bumped in the prototype (Q7).
- **The docs and the corpus: break until migrated.** Unmigrated text reads its metadata as
  fields. Measured on this branch with the switch on and nothing migrated, 8 of 23 test
  programs fail, every failure a metadata word read as a field; two of them then crash in
  the test code, which indexes past a `REQUIRE` that cannot abort in a build without
  exceptions (the core does not crash). Acceptable only with `tools/migrate_dta.py` run in
  the same commit and `check`'s warning for a release (§7).

## 4. Cost, measured

**Instructions** (`tools/count`'s fixed workloads under cachegrind, Release, per operation;
runs of N and 2N differenced, as `tools/count_instructions.py` does; the base is the
branch's fork point, 3817213):

| case | base | switch off | switch on | on vs base |
|---|---:|---:|---:|---:|
| `parse`: 16 KB of config text, no metadata | 1,845,498 | 1,845,498 | 1,864,145 | **+1.01%** |
| `parse_meta`: 16 KB, `(@range)` `(@step)` `(@desc)` on every value | 1,951,421 | 1,951,421 | 1,987,976 | +1.87% |
| `lookup`: `find_float`, the last of 20 children (D77's row) | 203 | 203 | 208 | +2.5% |
| `lookup_text`: the same by text, interning each call | 366 | 366 | 372 | +1.6% |
| `set`: `Config::set` on its fast path | 623 | 623 | 634 | +1.8% |
| `frame`: a quiet frame of the host kit's `Live` | 197 | 197 | 197 | 0 |

(`parse_meta` on the base is the old reader reading `(@range ...)` as plain children: the
same text. The base's very first `set` and `frame` runs read 614.5 and 192.8, on a temp
directory that did not exist yet; every later run of either build reads 623 and 197.)

- **The default build is unchanged:** every case counts the same with the switch off.
  That took four fixes to the inherited code (§8).
- **The parse misses the bar.** The brief's bar was +0.5% with the intern-time bit; this is
  +1.01%, inside D143's +1.1% ceiling. Callgrind over 200 parses puts the rest in three
  places: `finish()` +2.12M (12 instructions an array: a test and branch per tagged child on
  the size word `tag_key` already loads, and writing the boundary), the parser +1.06M (6 an
  array: the inline test of the boundary and the child count), the symbol table +0.25M (the
  size mask on each probe). Two earlier versions cost more: the inherited one +3.22%
  (1,904,991: an out-of-line placement call per array and a pointer chase per tagged child),
  and a version that gave each array an own-bit in `flags_` for its parent to read +2.11%
  (1,884,490: the bit cost more per array than the chase it saved).
- **A typed read pays five instructions** for `values()`'s compare (203 → 208), on every
  `find<T>`, `get<T>` and `Values` read; on the wall clock that is inside this machine's noise (below).

**Wall clock** (this machine, shared: a load average of 6 to 10 while these ran; best of
the runs; the ratio is what counts):

| benchmark | base | switch off | switch on |
|---|---:|---:|---:|
| `bench_parse` (4.2 MB), best of 13 rounds / median | 134.1 / 86.7 MB/s | 133.8 / 86.7 MB/s | 123.8 / 88.0 MB/s |
| `find_float`, avg, best / median of 7 | 11.37 / 13.31 ns | 9.25 / 9.85 ns | 12.17 / 13.35 ns |
| `find_float`, last | 14.40 / 17.78 ns | 11.75 / 12.85 ns | 17.46 / 26.13 ns |
| `find<float>`, avg | 9.63 / 12.42 ns | 9.63 / 10.68 ns | 10.04 / 10.44 ns |
| `get<int>(Path)`, 4 segments | 21.56 / 31.67 ns | 21.51 / 31.82 ns | 23.84 / 32.58 ns |
| `find_float`, cold, text-loaded | 342 / 468 ns | 325 / 435 ns | 264 / 418 ns |
| `find_float`, cold, cache-loaded | 265 / 324 ns | 296 / 408 ns | 288 / 370 ns |
| `bench-bind`: 1,000 tweaks, bind stage, best / median of 13 | | 0.217 / 0.245 ms | 0.186 / 0.239 ms |
| `bench-panel`: range, step, kind, desc a tweak, best / median of 13 | | 31.3 / 42.0 ns | 31.5 / 33.6 ns |

The base and the switch-off build run the same instructions on every row, yet their best
`find_float` rows differ by 2 to 3 ns: that is this machine's noise floor, and the switch's
five instructions (about 1 to 2 ns) sit inside it. The parse's best runs read 7% slower with
the switch while the medians are equal; the instruction count (+1.01%) is the number to
trust. The experiment's own `bench`, three rounds a build, interleaved with Baker's reader
on the same tree:

| text | build | `read_string` MB/s | the translator's `read` MB/s | time ratio | the parser alone on the converted text, ratio |
|---|---|---:|---:|---:|---:|
| generated, 4.2 MB | off | 68.7 / 88.2 / 88.2 | 26.9 / 39.0 / 35.7 | 2.42 / 2.14 / 2.34 | 0.88 / 0.99 / 1.49 |
| generated, 4.2 MB | on | 131.4 / 84.6 / 76.3 | 55.5 / 29.8 / 33.7 | 2.24 / 2.69 / 2.14 | 0.97 / 1.04 / 1.21 |
| the 39 stacks files joined, 57 KB | off | 173.7 / 115.6 / 169.7 | 67.7 / 41.1 / 68.4 | 2.05 / 2.25 / 1.99 | 0.99 / 1.03 / 0.98 |
| the 39 stacks files joined, 57 KB | on | 168.1 / 171.3 / 95.9 | 66.7 / 63.9 / 39.9 | 2.02 / 2.14 / 1.92 | 1.02 / 0.97 / 0.97 |

The translator's reader runs at about half Baker's speed because it lexes twice
(`read_structure` for the placement check, then the parser); that is the price of faking
the rule outside the core, not of the rule, which in the core runs inside the one parse. The
last column is the parser alone on the converted text (its comments dropped by the printer,
so it is 5% to 20% shorter): within noise of 1.0 in both builds. (The stacks files are
joined without their `#require`, `#include` and pack header lines, which a lone read cannot
follow; the largest single file is 5.8 KB.)

**Text.** The stacks' 39 files: 57,557 → 57,890 bytes (+333, +0.58%), lines unchanged (the
migrator inserts one character a key); their six packs: 10,645 → 10,892 (+247). The twelve
that change, in bytes: duel's pack +72, ledger's pack +42, stuntline's pack +38, wayfarer's
pack +37, contraption's pack +31, hearth's pack +27, and the data files of hearth +20,
wayfarer +20, cube +16, duel +13, ledger +9 and contraption +8; the other 27 (metrics, sims,
regions, levels) have no metadata after a value. The 68
corpus files: 68,894 → 69,273 (+379, +0.55%). The samples, before and after (the after
files are the migrator's renames, the printer's column, and hand polish; hearth's adds one
shape description):

| sample | lines | bytes before | renames only | polished |
|---|---:|---:|---:|---:|
| `stacks/cube/data/default.dta` | 15 → 15 | 666 | 682 | 751 |
| `stacks/duel/data/default.dta` | 48 → 48 | 3,133 | 3,146 | 3,193 |
| `stacks/duel/packs/game/duel_core.dta` | 47 → 47 | 2,830 | 2,902 | 2,943 |
| `stacks/contraption/data/default.dta` | 57 → 57 | 2,363 | 2,371 | 2,388 |
| `stacks/hearth/packs/game/hearth_core.dta` | 24 → 24 | 1,477 | 1,504 | 1,608 |
| `examples/harness-imgui/data/default.dta` | 20 → 20 | 760 | 775 | 832 |
| `tests/corpus/scripts.dta` | 16 → 16 | 435 | 436 | 436 |

**The prototype's own size** (none of it counted): the experiment, 861 lines of C++
(`syntax.hpp` 14, `syntax.cpp` 259, `main.cpp` 163, `tests.cpp` 425); `tools/migrate_dta.py`,
362 lines of Python.

**The core's line budget** (`tools/count_lines.py`; the budget is 15,500, D220). As built,
behind the switch, the core goes from 15,190 to 15,342 (+152: data +141, script +11), and
`baker::shape` from 547 to 569 (+22 of its 53 lines of headroom). The switch doubles some
code (both spellings, both forms of `finish()`); adopted, with the switch and the old paths
deleted, the estimate per file is:

| file | as built | adopted (estimate) |
|---|---:|---:|
| `include/baker/array.hpp` (`values`, `metadata`, `meta`, the boundary) | +21 | +20 |
| `src/data/array.cpp` (`finish`, `set`) | +33 | +27 |
| `src/data/parse.cpp` (the rule, its located error) | +36 | +35 |
| `src/data/config_check.cpp` (the migration warning, for a release) | +20 | +20 |
| `include/baker/symbol.hpp`, `src/data/symbol.cpp` (the bit) | +15 | +6 |
| `src/data/lookup.cpp` (sorted mode) | +6 | +5 |
| `src/data/tweak.cpp` (the word list and `without_metadata` gone) | +10 | −15 |
| `src/script/builtins.cpp`, `reference.cpp` (`{func}`) | +11 | +6 |
| **core** | **+152** | **about +104** |
| `src/shape/shape.cpp` (outside the core) | +22 | about +18 |

About +104 of the 310 lines left, a third of the headroom, twice the brief's estimate of
+50. The excess is the located placement error (`child_at`, about 15 lines, could locate at
the array's line without a column) and the boundary's upkeep in `set()` and at the 16,383
limit (about 16).

**Allocation and layout:** none changed. `Node` is 16 bytes, the array header 24 (the
boundary uses `flags_`'s 14 spare bits), `SymbolEntry` 24 (the bit is `size`'s high bit; no
symbol is 2 GB). `baker_alloc_tests` passes with the switch on.

## 5. Payoff, measured where possible

- **Values read as values, for every reader.** In the corpus, 165 data arrays carry a value
  and metadata. 161 have one value, which a scalar read already takes today (D88), so the
  change matters to four: the three-number colors of `stacks/cube/data/default.dta:7,15`,
  `examples/harness-imgui/data/default.dta:7` and `examples/config/data/default.dta:4`.
  Each now reads through `get<std::array<float, 3>>` and through a shape's check, where
  today only a bound tweak sees three numbers (tested on the converted cube file and the
  harness sample). Small in this corpus; every tool that reads a value gets it without
  `split_metadata`.
- **A field's name is free.** The corpus has 512 children tagged with one of the ten
  metadata words; 379 are metadata and 133 are fields: 97 `desc` of objectives and scenarios,
  32 `kind` of contraption's pieces, 2 `at` of scenarios, the wayfarer tunable named `step`,
  and the review item's `note`. Today each is a field because of where it sits or what reads
  it; under the rule it is a field by spelling, in any order (tested both orders of
  `(ball (kind ball) (pos ...))`). A shape may have fields named `at`, `keyed`, `kind` and
  `desc` (tested), and describe itself.
- **Errors a person can no longer make, or now sees:**
  - metadata before or between values is a located error (`(x (@range 0 1) 5)` at 2:20,
    tested), where today `(x (range 0 1) 5)` makes `(range 0 1)` part of the value;
  - an unknown or misspelt word on a shape or a field, `(@rnage 0 1)` or `(@keyd)`, is a
    located error, where today it is ignored (`shape.cpp:156`); the old spelling there is an
    error that names the new one (tested);
  - an unknown `@word` among a `{func}`'s leading forms is an error, and `(status 1)` as a
    body's first form is the body (tested);
  - the old spelling after a value draws `check`'s warning (tested). *Not built:* an unknown
    `@word` after a value in data is not reported; tools keep their own words there (spec
    §4's "other metadata is the tools' business"), so it would be a warning at most.
- **Rows shortened: none.** Every sample keeps its line count; the renames add a byte a key:
  cube 666 → 682 bytes (+2.4%), duel's data 3,133 → 3,146 (+0.4%), its pack 2,830 → 2,902
  (+2.5%), contraption 2,363 → 2,371 (+0.3%), hearth's pack 1,477 → 1,504 (+1.8%), the
  harness's data 760 → 775 (+2.0%), `scripts.dta` 435 → 436. The polished samples add the
  column's whitespace and hearth's one addition (table in §4).
- **Metadata deduplicated: not by this change.** The same range is still written in C++
  `TweakMeta`, in a shape field and in a row; Q5 is the step that would use this base to
  retire the copies.
- **Smaller diffs.** Adding a field after a record's last metadata child, sorting a layer
  (D28), or a tool appending through `write_back_to` no longer changes which children are
  metadata (tested: sort and insert keep the boundary).
- **Tools and agents.** D218's rule (seven words, a trailing run, only after a value)
  becomes one test of one token, which the tolerant reader (D220) already exposes as a
  bracket's tag text; an agent writing a scenario or a record cannot make metadata by
  naming a field `desc` and putting it last.
- **Engineers.** `tweak.cpp`'s word list and `without_metadata` go (about 15 lines when
  adopted). Binding 1,000 tweaks whose every value carries `(@range ...)` and `(@desc ...)`
  costs 1.9% fewer instructions with the switch (callgrind, `TweakBase::bind` inclusive:
  25,053,211 → 24,584,509 over the run's binds; `without_metadata`'s 512,578, about 64 a
  tweak a bind, are gone), and 0.217 → 0.186 ms at best on the wall clock (medians 0.245 and
  0.239). A tuning panel's frame, which reads each tweak's range, step, kind and desc by tag,
  is unchanged by construction (the same `try_find` with other keys): 31.3 and 31.5 ns a
  tweak at best (`bench-panel`, standing in for the ImGui harness).

## 6. Objections

- **"What does it cost per byte?"** (the data-oriented review's first question.) The text
  grows by one byte a key: 379 bytes over the corpus's 68,894 (0.55%). The tree does not
  grow: the same nodes, the same 16-byte `Node`, the boundary in header bits that were
  spare. The parse pays +1.01% of its instructions on text without metadata, +1.87% on text with metadata on every value (§4), the price of `finish()` loading each tagged
  child's tag until it finds the boundary. A typed read pays one compare in `values()`
  (five instructions on `find_float`, 203 → 208). *Answer, with a concession:* cheap per byte, not free per array.
- **"A second parser."** There is none in the product: the rule is a reading of the tree the
  one parser builds, and the experiment's `read()` is that parser behind a placement check.
  The migrator is another matter: it is a frozen copy of the lexer's rules in Python, which
  D220 set out to stop tools from writing. *Concession, bounded:* it runs once, its output is
  held to the C++ reader (the samples' `verify`, the migrated repository's 21 test
  programs), and the brief's own design (a C++ migrator over `read_structure`'s spans, as
  the experiment's `to-new` is over the tree) would retire it; not built here.
- **"Build time or run time?"** Build. The boundary is decided once, where the array is made
  (a parse, a merge, a `set`), and a cache keeps it; no read derives it again. Today's rule
  is the other way: `without_metadata` re-derives the value at every bind with up to seven
  symbol compares per trailing child (§4's bind row).
- **"You are solving a problem you don't have."** The stacks run today, and no shipped bug is
  laid at a collision. The evidence that the problem exists is in the record: D88 (a tint of
  three floats warned and kept its default), D218 (tools disagreed with the documented rule
  until Baker exported it), 133 fields named with a metadata word held apart by position or
  by context, a tunable named `step` (`stacks/wayfarer/data/default.dta:16`) that is safe
  only because it is not last, a `(status 1)` body form swallowed, a shape that cannot carry
  its own description. *Concession:* the problem is real and small in this corpus; the
  payoff is a rule a tool or an agent can apply from one token, more than a bug count.
- **"The heritage: it is not DTA any more."** The spec places metadata in data "matching the
  slide syntax: `(width 2 (range 0 10) (step 1) (desc "Box width"))`" (spec §4), and that
  form is how the original talk taught it. `@` changes the look of every tuned file a
  designer or a modder knows. Baker is not bit-compatible with the shipped format (spec §0)
  and the original had no metadata convention for this to break, but the familiarity is real.
  *Concession:* the largest cost of the change, and not measurable; spec §4's example would
  be amended.
- **"A reserved character is a reserved word in disguise."** Every symbol beginning with `@`
  in tag position becomes metadata. The corpus has none (0 of 68 files), but a user's data
  could (`(@home ...)` as a record key). *Answer:* one reserved prefix replaces eleven
  reserved words (seven for tweaks, eleven in shapes, overlapping), and the migration's
  `count` reports any existing `@`-tagged array before the switch.
- **"Unmigrated text misreads silently."** It does: `(range 0 10)` after a value becomes a
  field. *Answer:* the migration lands in the same commit, `check` warns on the old spelling
  once per layer, and a live overlay with it is refused by the example's policy (§7); measured
  on the unmigrated tree, 8 of 23 test programs fail, so nothing slips through the suite.
- **"It spends the line budget."** About +104 core lines when adopted: a third of the 310
  left, and twice the brief's estimate, for a rule of spelling. The boundary, its upkeep
  and the parse-time error are about 80 of them. *Concession;* §10 recommends the spelling
  without them, estimated at about +35.

## 7. Migration

**The tool.** `tools/migrate_dta.py` (outside the budget) inserts `@` and changes nothing
else: it reads tokens and brackets by byte span over a frozen copy of the lexer's rules, so
comments, layout and macros stay as written. It applies the three rewrites of §2 and decides
nothing it cannot: an array with no value whose last child is tagged with a metadata word is
D218's ambiguity, and goes to a review list. `--cpp` reads each run of adjacent C++ string
literals as the compiler joins them, and `--md` reads `dta` and unmarked fenced blocks and
inline code (`cpp` fences as C++); in Markdown, a lone metadata form in prose is listed for
a person. `--report` prints what it would do. (The version the previous prototyper left
looped forever on a backslash in `--cpp` and `--md` and grew to 8.5 GB on `docs/`; it was
rewritten on this branch.)

**Run over this repository:**

| input | renames | files changed | review list |
|---|---:|---:|---:|
| `.dta`, the 68 corpus files | 379 | 23 of 68 | 1 |
| Markdown (`docs/`, `README.md`, the examples' and stacks' notes) | 23 | 4 of 20 | 19 |
| C++ string literals (`tests/`, `examples/`, `bench/`, `stacks/`) | 77 | 9 of 100 | 4 |

By word, over the 68 files: range 124, desc 123, optional 49, kind 27, at 23, keyed 22,
step 6, note 2, one_of 2, status 1. The `.dta` review item is `tests/corpus/game_tuning.dta:10-11`,
a `(phase ...)` with no value whose last child is `(note "0.6 feels unfair ...")`: today
D218 would call it metadata if anything read the phase as a value, and under the rule it is
a field until a person writes `@note`. The Markdown list is for reading, not applying: it
holds `(status stub)` in `docs/language.md`'s table, `(keyed)` in the stacks' READMEs, and
three scenario `(at ...)` forms in `docs/sim-json.md`, which are fields and must stay. The
C++ list holds two copies of `(limits (min 0) (max 100) (step 0.5))` in the benchmarks and a
test's `(p (kind nope))`, all fields.

**What the tool cannot reach:** expected output written outside a DTA literal. Two in this
repository: a CMake regex that expects `(volume 0.45 (range 0 1)` in `baker_example_config`'s
output (`examples/CMakeLists.txt:65`), and a test that searches a message for "no (at ...)"
(`tests/shape/test_shape.cpp:186`). Trees built through the API, such as
`array("width", 2, array("range", 0, 10))` (`tests/data/test_lookup.cpp:139`), are not text;
that test passes under either reading.

**Proof.** A copy of the repository was migrated by the tool (all three modes) and built with
`-DBAKER_AT_METADATA=ON`: 19 of 21 test programs passed, and 21 of 21 once the two expected
texts above were edited by hand (the experiments were off in that build; they read the
unmigrated corpus). The same tree unmigrated fails 8 of 23 (§3).

**One commit, no period of both.** The two readings cannot share a text: after a value,
`(range 0 10)` is metadata today and a field under the rule. So the migration lands in the
same commit as the rule, as `CLAUDE.md` wants for every breaking change, and `check` warns on
the old spelling (once per layer) so a file the commit did not reach is found at its first
check.

- **Packs and the one-way rule:** unaffected; packs are files and migrate with the rest (the
  stacks' six packs gain 247 bytes). A third party's pack written for an older Baker reads
  its metadata as fields until it is run through the tool, and `check` says so.
- **Layers:** personal and experiment layers (`local.dta`, `local/<you>.dta`) live on
  people's machines, outside any commit; their owners run the tool once. Write-back and
  promotion write values, never metadata, so they cannot create the old spelling.
- **Live-ops overlays:** a received overlay is checked before it applies. The live-ops
  example's policy (`examples/live-ops/main.cpp:31-36`: scalars only, every warning a
  refusal) would refuse an overlay with an old-spelled `(range ...)` rather than misread it;
  the corpus's overlays carry plain values only.

## 8. What the prototype proved, and did not

**Built.** The core rule behind `BAKER_AT_METADATA` (the intern-time bit, the boundary and
its upkeep, the parser's located error, `values()`/`metadata()`/`meta()`, the sorted-mode
partition), every reader of metadata under it (tweaks, `split_metadata`, `check`'s warning,
shapes, `{func}`, the demand report, the harness's `.@note`), the migrator with its review
list, the experiment (`print` is the migration over a tree with the metadata column,
`read` is the placement check over `read_structure` and then Baker's parser, `strip_sigils`
is the inverse; the CLI adds `bench-bind`, `bench-panel`, `verify` and `count`), seven
samples, and the fuzz dictionary's `"(@"` and `"@range"`.

**Passed.**

- The corpus round trip, 68 files, 0 failures, with the switch off and on (Baker's reader,
  the migration, the reader with the rule, the sigils stripped, compared).
- The experiment's 16 test cases with the switch on (413 assertions) and off
  (275; the core's cases are skipped there). They cover each case the brief lists:
  the converted cube file's color as three floats and the original's failure; `(ball (kind
  ball) ...)` in both orders; a tweak bound to `(x 5 (@range 0 10))` with its range and to
  `(x 5 (range 0 10))` without, with the warning; `(@at ...)`/`(@keyed)` governing and fields
  named `at` and `kind`; `(x (@range 0 1) 5)` an error at its column; a layer restating a
  value keeping the `@range` below, and `(@range #remove)`; sorted mode keeping the
  boundary; `set()` moving it; `write_back` growing a value before its `(@range ...)`;
  `write_back_to` inserting after a trailing `(@note ...)` and the file reparsing;
  `box.width.@range` and `cube.color.@desc` by path; `{func f () (@status stub) (status 1)}`
  keeping `(status 1)`; a binary cache keeping the boundary; the 16,383 limit (metadata at child 16,383 taken, at 16,384 refused); the
  migration's rules and idempotence; each sample stripped back to its original.
- The repository migrated by the tool and built with the switch on: 21 of 21 test programs,
  `baker_data_tests` included (its corpus prefix-truncation test and the fuzz regressions),
  `baker_alloc_tests` and `baker_tweak_codegen`, after the two hand edits of §7, run
  again with the final core sources.
- The default build: unchanged by instruction count on every `baker_count` case, and its
  full suite passes (23 of 23 test programs).
- A tweak read is still one load with the switch defined (`check_tweak_read.cmake` run by
  hand with `-DBAKER_AT_METADATA=1`: `movd kBoxWidth+180(%rip), %xmm0`).

**Not done, or failed.**

- Parse cost misses the brief's bar: +1.01% against +0.5% (§4).
- Not built: `WriteOptions::align_meta` in the core (the column is the translator's);
  storing and printing a shape's own `(@desc)`; reporting an unknown `@word` in data; a C++
  migrator over `read_structure` in place of the Python one; the scenario and objective
  `(@desc)` (deliberately, Q4).
- Not measured: the ImGui harness's frame (it fetches Dear ImGui and GLFW over the network
  and opens a window; neither is available here), so `bench-panel` stands in for its
  per-frame reads; MSVC, libc++, TSan and the local matrix's other configurations (CI's,
  with the switch on, would need a job); `bench_compare.py`, which compares commits, not a
  switch, so the wall-clock rows were taken by hand on three builds of one tree.
- The previous prototyper's first core version cost +3.22% on the parse (an out-of-line
  check per array and a pointer chase per tagged child) and changed the default build's
  code (`values()` rewritten for both builds, an extra mask in `finish()`, the symbol
  table's size load hoisted above the hash compare, a capturing comparator in the sort).
  All were found by instruction counts on this branch and fixed (§4).

## 9. Open questions for the owner

Each is one decision; the recommended answer is first.

1. **Bare `@optional` as a desugared flag (at-attributes)?** *Recommended: no; `(@optional)`
   stays the one spelling.* A bare `@optional` in value position is a value today, so the
   parser would test the intern-time bit on every bare symbol it reads as a value and turn
   one into an array. It saves two bytes at 49 sites (98 bytes over the corpus) and makes a
   value-position token change meaning, the kind of rule this change exists to remove.
2. **The boundary in `flags_` (an O(1) `values()`), or a parent bit and a prefix scan
   (at-metadata's lighter lines)?** *Recommended: neither on the hot path; if the boundary
   is kept after all, `flags_`.* The measured price of any boundary is in §4: five
   instructions on every typed read (203 → 208 on `find_float`) and +1.01% on the parse. A
   parent bit and a prefix scan would cut the read's cost to one bit test for arrays
   without metadata and save about 20 lines (`set()`'s upkeep and the limit), but walk the
   value run on every read of an array that has metadata. §10 recommends dropping the
   boundary: the spelling alone, read where metadata is read.
3. **Metadata paths change spelling (`box.width.note` becomes `box.width.@note`): bump
   `baker.reply`/`baker.check` a minor version, or keep the old spelling as an alias in
   `Path`?** *Recommended: a minor bump, no alias.* The JSON's shape does not change, only
   the paths in it; an alias is the compatibility layer `CLAUDE.md` rules out, and it would
   make `box.width.note` mean two things in a record that has a field named `note`.
4. **`{func}`'s `(@status)`/`(@doc)` and scenarios' and objectives' `(@desc)`: one rule for
   the whole language, or the script and host readers separately?** *Recommended: `{func}` in
   this change, scenarios and objectives not at all.* `{func}`'s metadata is metadata, and the
   sigil fixes a real swallowing of `(status 1)` as a body's first form. An objective's
   `(desc ...)` is a field of a record with no value; reading it by `try_find("desc")`
   (`scenarios.cpp:50`) is right, and the migrator leaves all 97 alone.
5. **The 'declare once' precedence (the row, then the governing shape field, then C++
   `TweakMeta`), which would retire drifting copies of the same range in code and data: the
   next step, or part of hash-declarations' `#shape`?** *Recommended: the next step, on this
   base,* because it needs exactly what this change gives: a shape field's `(@range ...)`
   and a row's are found by the same `meta()`.
6. **Should typed-rows' `(@type T)` child and `lo..hi` literal follow on this base?**
   *Recommended: no; types stay in declarations (shapes).* `(@type T)` on every row repeats
   what a shape says once, and `lo..hi` is a new token, which this change takes care not to
   need.
7. **`kVersion`: ride the bits without a bump (the prototype), or bump to 7?**
   *Recommended: bump to 7.* Riding is safe for this corpus, which has no `@`-tagged array,
   but a cache written by an older build of a text that has one holds no boundary, and would
   read differently from its text; a bump costs one constant and one cold load per cache.

## 10. Recommendation

**Adopt in part: the spelling, not the boundary.** The measured payoff of `@` comes from
naming metadata by one token: 133 fields freed from position, misspelt and unknown words
caught in shapes, `{func}`'s `(status 1)` kept as a body, one rule for every tool and agent,
and a migration that a 360-line tool does with one record to review and two expected texts
to edit by hand (21 of 21 test programs pass after it). The measured cost comes from the
boundary in `values()` and the parse-time rule: +1.01% parse instructions, twice the bar;
five instructions on every typed read; about 80 of the ~104 core lines, for four color
values in this corpus that a plain `get<T>` cannot read whole today. The next step is a
second pass on this branch that keeps the spelling and drops the boundary: `split_metadata`
and the tweak bind take the trailing run of `@`-tagged children (one token test in place of
seven symbol compares), `check` reports metadata before a value instead of the parser,
`Symbol::is_meta()` reads the text's first byte (no bit, no mask), and `values()`, the
parser and the cache stay as they are. Measure it with the same `baker_count` cases
(expected: no change on parse or lookup) and `count_lines.py` (expected: about +35 core
lines, about +15 once the migration warning goes), then decide between it and the full
change as built here. If neither payoff is worth a migration of 479 sites and a look that
departs from the talk's slide syntax, decline: the corpus works today.

## 11. Verification, the raw gate, and review

*Added after the prototype, from the session's review round; `dev/syntax-ideas.md` on
`claude/fervent-cray-l1kyf0` compares all eight branches.* An agent independent of the
prototype rebuilt this branch, reran its tests and reproduced its numbers; a reviewer
judged it under the data-oriented lens (`.claude/skills/acton-review/`) and the tool
author's; and D101's raw gate (464 raw Harmonix files, `baker_parse_corpus --roundtrip
--milo-paths` over a local clone of mariteaux/milo-script-library, nothing from it in the
repository, D3) was run on the branch's build. Where a finding below disagrees with an
earlier section, this section is the later word.

- **Reviewed at** `1c73321`, the branch's final head.
- **Verifier.** Builds; 23 of 23 by default. Every instruction count on every case and
  build, the core line counts, the samples, the migration counts and the 21-of-21 proof on
  a migrated copy reproduce exactly. Corrections: the `find_float` cost is not noise.
  Pinned to one core and interleaved, base and switch-off agree and the switch runs +55%
  on the average row and +65% on the last child, D77's warm row; `bench_parse` is about 4%
  slower with the switch at best and median. The prototype is 865 code lines, not 861.
  The migration is not on the branch, so building with the switch on fails 8 of 23 tests.
- **Raw gate.** Switch on: **456/464**. All 8 failures are one line of real data,
  `rb1/ps2/config/cheats.dta:81`, `('@' "Game: Win w/ 3 stars, Meta: Win Event w/ 2 Star
  Avg" {fake_win 2})`, a keyboard cheat bound to the `@` key, included by 8 files. The
  error is located, not silent. A quoted symbol must never be metadata.
- **Technical director, 2 of 5, explore further.** No misread exists in the corpus to fix;
  the boundary costs +1% on the parse and the reproduced `find_float` row; the recommended
  variant is not built. The argument the note does not make: one token marking metadata
  lets a bake strip cold metadata, about 13.5% of the stacks' bytes.
- **Tool author, 3 of 5, adopt in part.** `split_metadata` now returns fields that follow
  metadata as metadata, a layout `write_back_to` itself produces; `(@desc)` on a scenario
  is ignored and a misspelt `(@rnage)` in data is silent; the migration warning misses an
  old word that is not last; files grow 0.3% to 2.5% from renames alone.
- **Fuzzing.** libFuzzer under ASan and UBSan with `BAKER_AT_METADATA=ON`, built as CI's
  fuzz job is and seeded with the corpus, the fuzz regressions and the samples, after
  porting main's harness fix for `(key #remove)` (`cd35137`): 1,075,950 executions in 25
  minutes, 2,028 edges, no failure. Short of spec §8's 10 million, which belongs to an
  adoption step.
- **Recommendation after review.** Build the spelling-only pass of section 10 with
  `metadata()` returning only `@` children and every bare old word and unknown `@word`
  reported, measure it to zero on parse, `find_float` and `set`, and decide it together
  with hash-declarations' `#shape`.
