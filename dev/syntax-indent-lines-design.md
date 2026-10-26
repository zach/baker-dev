# Indentation is structure: a design note (`indent-lines`)

*A syntax prototype on branch `claude/fervent-cray-l1kyf0-syntax-indent-lines`. The core
change is behind `-DBAKER_INDENT_LINES` (`OFF` by default, `ALWAYS`, or `DIRECTIVE`); the
translator, the migration tool, the gate and the samples are in
`experiments/syntax-indent-lines/`. Nothing here is on main.*

## 1. Thesis

A line that begins with an atom is an array. Its tokens are the array's nodes, and the lines
indented deeper are its children, so the outer parentheses of most lines go and the
indentation every corpus file already keeps by hand becomes the structure the parser checks.
Brackets stay for groups inside a line (tuple rows, `(range 0 10)`), for commands and
properties, and for any node that spans lines; a line that begins with a bracket reads
exactly as today. `(player (speed 4.5 (range 0 10)))` is written

```
player
   speed 4.5 (range 0 10) (desc "Run speed, in m/s")
```

and reads to the same tree, through the same `Array`, `Node` and arena, with nothing new in
the tree or the cache.

The corpus writes its structure twice. All 160 first children of a top-level form in the 68
corpus files sit at 3 spaces; 1,154 of its 2,219 parenthesized arrays open their own line
(section 4); and 489 of its 1,485 lines end in a pile of two or more closers, 106 in three
or more, the bytes a designer counts by eye and a merge conflict lands on. The layout makes
the indentation the one statement and checks it: a dedent that lands between levels, a tab,
and a value list continued on a deeper line are located errors (spec §1, "Robustness
principle"). It serves the spec's headline goal, a designer-editable value at the cost of
one line (spec §0), by making the file read as the outline a designer already writes, and
write-back's minimal diff (spec §1, "Edit-mode parsing and write-back"): appending a child
adds a line and touches none, where today it moves the closers off the old last line. Merged
in is one-form's half that fits: the printer's defaults become the house style, indent 3
and width 100, so an entry `write_back_to` inserts matches the file around it (today it is
printed at indent 2, width 80, `include/baker/write.hpp:27-28` on main).

## 2. The syntax

### Tokens

Unchanged: `kAtomEnd`, `classify_number`, strings and their two escapes, quoted symbols, `#`
directives, `$` variables, both comment forms; `Token` is unchanged (48 bytes). The lexer
stores the byte offset where the current line begins (one store on each newline it skips,
`parse.cpp`, `lex_token`). The parser keeps the previous token's end; a token is *first on
its line* when its line begins at or after that end. A block comment before a token on its
line counts in its indent (`a\n   b\n   /* w */ c 2` makes `c` a child of `b`,
`tests/data/test_lines.cpp`, "each form").

### The layout

It is decided only for a token first on its line while no explicit bracket is open (the
frame is the root or a line; inside `(`, `{` or `[` indentation means nothing, as today), and
only in a text read as lines: every file in an `ALWAYS` build, from a file's `#lines` on in a
`DIRECTIVE` build, and from the start wherever `LoadSession::lines` is set.

1. **A conditional takes no part.** `#ifdef`, `#ifndef`, `#else` and `#endif` neither open
   nor close a line whatever their indent, so they sit at the margin inside a block, as
   `tests/corpus/ui_menu.dta:39-43` writes them between forms. Tokens in a branch not read
   never reach the layout.
2. **Close.** The token's indent (bytes from its line's start) closes every open line whose
   indent is at least it. If any closed, the indent must equal the last one closed: a dedent
   lands on a level, or `indent 5 matches no open line` (located at the token).
3. **Open.** An atom (Int, Float, String, Symbol, quoted symbol, Var) that is not a macro
   name opens a line at its indent: an Array whose first node is the atom, holding the rest
   of its line's nodes (bracket groups included, parsed by today's frames) and every line
   opened deeper. A line at the top level begins at the margin, else the same error.
4. **No line.** A bracket opens none: its group, and the groups after it on the line, are
   nodes of the enclosing line or of the root, as today. A macro name splices its nodes there
   as today. Any other directive (`#include`, `#merge`, `#define`, `#autorun`, `#remove`)
   does what it does today in the enclosing frame; its indent closes lines like a token's,
   so `#include part.dta` at the margin splices into the root and indented into the block.
5. **Errors.** A tab among the indent bytes, where the indent decides something (a line is
   open, or the token opens one), is `a tab in a line's indentation: indent with spaces`;
   inside brackets, and before a bracket at the top level, tabs stay whitespace
   (`tests/corpus/odd_tokens.dta` reads as before). A deeper atom-first line under a line
   that already holds a value beyond its tag is `a value list continues on the same line or
   in brackets; a deeper line is a child`: this refuses the trap of the four sim files that
   continue `(objectives ...` on a second line (`stacks/hearth/sim/households.dta:8-9`,
   `stacks/ledger/sim/economy.dta:13-14`, `stacks/stuntline/sim/tracks.dta:7-8` and `:13-14`,
   `stacks/wayfarer/sim/world.dta:8-9`), which under a plain layout would silently become a
   child array. A close bracket first on a line where none is open is `unexpected ')'` as
   today. The end of the file closes every line. Deep indentation meets `max_depth` as
   brackets do.

### The tree

A line closes as a `Type::Array` made in the load's arena exactly as a `(` frame
(`close_line`, which repeats `close()`'s tail so the `OFF` build's `close()` is the base's),
with the file and the line of its first token; the tag
rule is unchanged (a leading symbol is the tag). `landmark` alone is `(landmark)`;
`waves {spawn grunt 3}` is `(waves {spawn grunt 3})`; a bracket-only line adds no array; a
first line `pack hearth_core (level game)` is the pack declaration, as `(pack ...)` is.
Nothing is new in the tree: no `Type`, no flag, no header byte; the cache stays `kVersion
6` (`src/data/cache.cpp:44`).

### What today's forms become

| today | as lines |
|---|---|
| `(key value)`, `(key v1 v2 (group))` | `key value`, `key v1 v2 (group)` |
| `(key (child 1) (child 2))` over lines | `key` and the children as deeper lines |
| `(key value (child 1))` over lines | `key value` and `(child 1)` as a deeper bracket row: a line holding a value takes no child lines |
| tuple rows `(walls (0 -5 14 0.2) ...)` | `walls` and the rows as deeper bracket rows, or on its line |
| `{command}`, `[property]`, `()` | unchanged |
| `(key #remove)` (D143) | `key #remove` |
| `[NAME ...]`, `#define NAME (...)` | unchanged (a bracket at the margin) |
| a value at the top level | after the node before it on the same line; one that begins the root after `#lines` (a directive that opens nothing): `#lines 5` |

### The corpus before and after

`examples/quickstart/default.dta:1-3`, by the migration tool:

```
; The values the game reads, by path: player.speed is 4.5 here.
player
   speed 4.5 (range 0 10) (desc "Run speed, in m/s")
```

`tests/host/stack/data/default.dta:1-9`, the trailing comment where the author put it:

```
; The test game's data.
shapes
   player
      speed float (range 0 10)
      lives int
player
   speed 3.5   ; how fast
   lives 3
waves {spawn grunt 3}
```

`stacks/duel/packs/game/duel_core.dta:8-16`, a shape's fields as a table:

```
shapes
   fighter (at fighters) (keyed)
      health        int   (range 1 100000)
      width         float (range 0.2 3) (kind meters)
      height        float (range 0.5 4) (kind meters)
```

`stacks/hearth/data/default.dta:45-55`, hand-polished
(`samples/stacks/hearth/data/default.polished.dta`): tuple rows keep their parentheses and
sit under their key as its values.

```
households
   cottage
      desc "Two rooms and a couple"
      size 14 10
      walls
         (0 -5 14 0.2) (0 5 14 0.2) (-7 0 0.2 10) (7 0 0.2 10)
         (1 -3 0.2 4) (1 3.25 0.2 3.5)
      residents
         ada (weights (social 1.4)) (color 0.95 0.55 0.4)
         bo (weights (fun 1.3) (hygiene 0.8)) (color 0.4 0.6 0.95)
```

And what does not change: `stacks/duel/metrics/duel.dta:5-8` names an instance by a value,
so its properties stay bracket rows, and so do every objective, scenario and sim in the
corpus (272 arrays, section 4).

```
objective duel.no_infinites
   (desc "No combo goes on forever")
   (measure {duel.infinites})
   (pass {== $value 0})
```

## 3. What it keeps and what it changes

- **Lexer and parser.** Re-provided by the prototype behind the switch
  (`src/data/parse.cpp`): the line start in the lexer; in the parser `Frame::Line`, a
  `flat_` flag (the layout applies and no bracket is open, set at each open and close), the
  previous token's end, and `layout()`, `close_line()` and `holds_values()`, taken only for
  a first-on-line token while `flat_`. The `OFF` build compiles none of it, and its parse
  executes the base's instructions exactly (section 4).
- **The printer.** Re-provided: `WriteOptions::lines` (`put_line` beside `put_pretty`,
  `src/data/write.cpp`) prints each tagged array as `tag values` with its tagged children as
  deeper lines, untagged children as rows of groups packed to the width, and falls back to
  the bracketed form where a head does not fit or a line cannot say the array. The pretty
  defaults are indent 3, width 100. The compact form (`to_string`, `write(Node)`, reply text,
  `check`'s repeat forms, `results.dta`) is unchanged, so no versioned document's bytes move.
  Where the layout reads its output, a value at the top level is written after the node
  before it and a root that begins with one after `#lines`, so the printer spells every
  parsed tree (the fuzz harness's round trip holds; D13's list gains nothing). Whether
  `write_dta` defaults to lines is section 9's question; it does not today.
- **Write-back.** Token replacement is unchanged (D34, D183, D226): a line's value is one
  token with its span. An inserted entry takes its parent's recorded form
  (`src/data/config_write.cpp`, `plan`): in a bracketed parent, brackets, so it can never
  re-read as the parent's values (the tools judge's hazard); in a line, a deeper line at the
  previous sibling's indent after the sibling's trailing comment; in a line that holds a
  value, a bracket row; at the top level, a line after a line and brackets after brackets.
  D132's batching is untouched. Tested: `test_lines.cpp`, "write-back replaces one token".
  *Would need this work:* `write_back_to` that adds values to a key that has child lines
  (`targets` empty in `plan`) writes them after the tag, which the continued-list rule then
  refuses on re-read; it should refuse first, or write the key's values as brackets.
- **Edit-mode source maps (D33, D128).** Re-provided: a line's extent is its first token's
  begin to its last token's end in this file, recorded through the same `record()`; each
  node keeps its token span. `source()` names the line of a line's first token
  (`player.speed` at line 4 in the test).
- **The tolerant structure read (D220).** Re-provided: a line is a `TextBracket` of the line
  kind (`line_kind`, with `open` 0), closed after its last token, so an editor's outline and
  folding see it; a dedent between levels and a tab are reported as problems and the read
  goes on. `read_structure`'s corpus test is still clean under `ALWAYS`. `tree_estimate`
  (the first block's size, D131) is unchanged: it does not count a line's array, so a lines
  file over 16 KB is estimated short by one array header a line. Counting them cost a test
  on every byte of the 16 KB sample (version 1, section 4).
- **Provenance and explain.** Unchanged: they read the arrays' file and line.
- **Layers and `#remove`.** Unchanged; `key #remove` is a line and D143's rule applies.
- **`check` and the demand report; shapes and the reference JSON.** Unchanged: they read
  trees. Shape files read as lines (the six packs convert, section 4).
- **The console's requests.** Unchanged: `eval_request` and `Context::eval(text)` read with
  `LoadSession::lines` cleared, so a request is `(` first as today
  (`src/host/console.cpp:58-59`) and a line typed for a person, `$score`, is a value, not
  `($score)`. So is a tweak's default text (`DescribedTweaks::add`, `src/data/tweak.cpp`).
- **The project file.** Unchanged: it is read through the same reader. The test stack's
  `baker-project.dta` rewritten as lines drives `diff` and `promote` (section 5).
- **The binary cache.** Unchanged: it stores trees.
- **The docs and the corpus.** Would need this work: a page on the form, the spec §1
  grammar, and the corpus rewritten by the migration tool (section 7). Nothing here is
  written yet.

## 4. Cost, measured

### Instructions (the cost judge's first step)

`tools/count_instructions.py`'s method (cachegrind, Release, GCC 13, runs of N and 2N
differenced) with `tools/count`'s driver, which gains a `parse_file` case; the base is
`9f35682`, the branch before the core change, whose core is the session branch's. Each
build is the same commit with `BAKER_INDENT_LINES` set. Instructions per parse:

| case | base | `OFF` | `ALWAYS` | `DIRECTIVE` |
|---|---:|---:|---:|---:|
| `bench_parse`'s config text, 16,552 bytes, brackets | 1,845,499 | 1,845,499 (±0) | 1,876,393 (+1.67%) | 1,873,809 (+1.53%) |
| the same tree as lines, 15,464 bytes (migration tool) | | | 1,554,752 (-15.8%) | 1,555,266 (-15.7%, after `#lines`) |
| the 11 stacks data files' trees, brackets, 17,498 bytes | 1,897,537 | 1,897,537 (±0) | 1,929,850 (+1.70%) | 1,927,490 (+1.58%) |
| the same trees as lines, 16,440 bytes (printer) | | | 1,871,386 (-1.4%) | 1,871,993 (-1.3%, after `#lines`) |

Percentages are against the base on brackets. **The cost to a file that does not use the
form is +1.67%, above D143's bar of +1.1%** (about +0.5% was expected). The `DIRECTIVE`
build, the cost judge's fallback, pays nearly as much, +1.53%, because a bracketed file
there still pays the lexer's line start and the parser's per-token test; it decides
nothing until `#lines`, but it tests. A text written as lines is cheaper to read than the
same tree in brackets, by 15.8% for the config text (fewer bytes and tokens, the same
arrays) and 1.4% for the stacks' data.

Four versions were measured, all reading the same trees and passing the same tests:

| version | the per-token test | `ALWAYS` | `DIRECTIVE` | `OFF` |
|---|---|---:|---:|---:|
| 1 | a first-on-line bit on the token, then the frame's kind; `tree_estimate` tests each byte for a line start; `close()` shared with lines | +7.16% | +7.13% | +0.41% |
| 2 (committed) | a `flat_` flag, recomputed at each close; each token's end stored | +1.67% | +1.53% | ±0 |
| 3 | each frame saves `flat_`, so a close restores it; the end stored only while `flat_` | +2.69% | +2.62% | ±0 |
| 4 | version 1's bit, with version 2's `tree_estimate` and `close()` | +2.41% | +2.38% | ±0 |

Version 1's +7.2% was `tree_estimate`'s per-byte test (+107k instructions a parse on this
text, which just passes the 16,384-byte sample) and its `OFF` regression the shared
`close()`; both were removed. Between versions 2, 3 and 4 the design's own work is the same
(one store a newline, one test a token), and the result moves by a point with how the
compiler lays out the loop: a line-level profile of version 3 (`-g`) puts about four
instructions a token on the test itself and one reload a token at each of four other lines
of `run()`, where the extra state pushes a value out of a register. The floor of the design
is about three instructions a token, near +0.6% on this text; none of the four layouts
reached it.

### Wall clock

`bench_parse` (4.2 MB, best of 10), three rounds interleaved across the four builds:
base 143.5, 151.5, 121.5 MB/s; `OFF` 138.3, 159.6, 169.1; `ALWAYS` 162.0, 166.6, 131.7;
`DIRECTIVE` 157.1, 158.1, 160.6; 43 allocations in every build. The machine's noise (a
range of 30 MB/s within one build) is far above a 1.7% effect, as D143 found for its own.

The experiment's `bench` (both readers interleaved, best of five rounds; `bench <files>` on
the 11 stacks data files' trees repeated to 2.1 MB):

| build, text | `read_string` on brackets | the experiment's reader | time ratio |
|---|---|---|---|
| `OFF` (the translator), generated, as lines (3,708,778 bytes) | 126.1, 126.6, 120.6 MB/s | 72.5, 76.2, 73.4 MB/s | 1.539, 1.470, 1.452 |
| `OFF`, generated, the same bracketed text (the translator's overhead) | 116.9, 124.3, 122.1 | 81.5, 87.9, 83.4 | 1.434, 1.414, 1.464 |
| `OFF`, the stacks' trees as lines / as brackets | 104.9 / 106.0 | 68.1 / 70.0 | 1.447 / 1.514 |
| `ALWAYS` (the core), generated, as lines | 137.2, 141.7, 148.5 | 124.1, 133.3, 126.9 | 0.977, 0.940, 1.035 |
| `ALWAYS`, generated, brackets with the layout off (`LoadSession::lines` cleared) | 136.5, 146.7, 141.9 | 141.0, 151.0, 157.0 | 0.968, 0.972, 0.904 |
| `ALWAYS`, the stacks' trees as lines / brackets with the layout off | 105.6 / 104.1 | 103.2 / 110.3 | 0.961 / 0.943 |
| `DIRECTIVE`, generated, as lines / brackets with the layout off | 136.2 / 141.4 | 128.2 / 146.3 | 0.939 / 0.966 |
| `DIRECTIVE`, the stacks' trees as lines / brackets | 112.3 / 110.1 | 109.0 / 111.0 | 0.968 / 0.992 |

The translator, a text pass before the parser, reads at 0.65-0.71 of the parser's speed; the
core reads lines at about the parser's speed on brackets (ratios 0.94-1.04), and a ratio
under 1 on the same bracketed text with the layout off is that reader skipping the layout's
work. This machine is slower and noisier than the README's numbers; the instruction counts
above are the measurement.

### Lines and bytes of the real files

The migration tool (`baker_syntax_indent-lines convert`) deletes the parentheses of each
array the layout reads back as that array, keeping every comment and directive, reads the
result back with its includes and compares the tree. Over the 68 corpus files every tree is
the same:

| files | lines | bytes | parentheses | arrays: first on their line / made lines |
|---|---:|---:|---:|---:|
| the 11 stacks data files | 437 → 435 | 21,789 → 21,177 (-2.8%) | 1,962 → 1,378 (-29.8%) | 967: 330 / 292 |
| the 6 packs | 179 → 179 | 10,645 → 10,335 (-2.9%) | 938 → 628 (-33.0%) | 435: 155 / 155 |
| the 14 examples | 98 → 98 | 3,723 → 3,593 (-3.5%) | 242 → 112 (-53.7%) | 121: 65 / 65 |
| all 68 corpus files | 1,486 → 1,484 | 68,894 → 67,215 (-2.4%) | 4,654 → 3,004 (-35.5%) | 2,219: 1,154 / 825 |

Per file (the 31 the brief names; `samples/` holds each beside its original):

| file | lines | bytes | parentheses |
|---|---:|---:|---:|
| stacks/contraption/data/default.dta | 57 → 57 | 2,363 → 2,261 | 274 → 172 |
| stacks/cube/data/default.dta | 15 → 15 | 666 → 646 | 54 → 34 |
| stacks/duel/data/default.dta | 48 → 48 | 3,133 → 3,075 | 278 → 228 |
| stacks/hearth/data/default.dta | 108 → 108 | 5,807 → 5,696 | 476 → 382 |
| stacks/ledger/data/default.dta | 29 → 29 | 1,667 → 1,633 | 102 → 68 |
| stacks/ledger/data/economy.dta | 22 → 20 | 1,496 → 1,464 | 136 → 106 |
| stacks/stuntline/data/default.dta | 40 → 40 | 1,959 → 1,907 | 194 → 142 |
| stacks/wayfarer/data/default.dta | 74 → 74 | 3,210 → 3,089 | 316 → 196 |
| stacks/wayfarer/data/regions/interior.dta | 22 → 22 | 754 → 712 | 66 → 24 |
| stacks/wayfarer/data/regions/north_coast.dta | 9 → 9 | 311 → 295 | 28 → 12 |
| stacks/wayfarer/data/regions/south_coast.dta | 13 → 13 | 423 → 399 | 38 → 14 |
| stacks/*/packs/game/*_core.dta (6) | 179 → 179 | 10,645 → 10,335 | 938 → 628 |
| examples/**/*.dta (14) | 98 → 98 | 3,723 → 3,593 | 242 → 112 |

The hand-polished three (`.polished.dta`), against their originals: hearth 108 → 117 lines,
5,807 → 5,709 bytes, 476 → 368 parentheses; duel 48 → 52, 3,133 → 3,087, 278 → 216;
wayfarer 74 → 83, 3,210 → 3,125, 316 → 178. Polishing trades lines for parentheses: a key
whose values were packed on its parent's row becomes a line of its own.

Of the 1,154 arrays first on their line, 329 keep their parentheses: 272 under a parent
holding a value (`objective NAME`, `scenario NAME`: every metrics and sim file), 24 in a row
wrapped under the node after their parent's tag (hearth's objects), 20 inside a parent that
keeps brackets, 6 sharing their last line with a sibling, 4 untagged or led by a macro, 2
continuing a value list, 1 off the layout's indent (a tab). The brief's estimate of 1,130
that "open and close their line" is near the tool's 1,154 first on their line; the
parentheses that go are 1,650, not 2,260, because the value-holding instances keep theirs.

### Line budget

`tools/count_lines.py`: the core goes from 15,190 to 15,531 counted lines (+341), 31 over
the soft budget of 15,500 (D69, D220): `parse.cpp` +209, `write.cpp` +81,
`config_write.cpp` +29, `parse.hpp` +6, `write.hpp` +6, `tweak.cpp` +4, `context.cpp` +3,
`request.cpp` +3. Of those, about 78 are the switch itself (71 `#if`/`#else`/`#endif` lines
and the `#else` alternatives), and 8 repeat `close()`'s tail in `close_line()` so the `OFF`
build is the base's; the `#lines` directive is 7. Adopted always on, without the switch, the
projected delta is about +250, to about 15,440 of 15,500; adopted as the `#lines` opt-in,
about +260. The brief estimated +225. `baker::shape` and `baker::host` do not move.

The prototype's own code, outside the core: `experiments/syntax-indent-lines/` 1,153 counted
lines (translator and printer 413, migration tool 137, CLI 302, tests 265, header 36), and
`tests/data/test_lines.cpp` 258. It uses the STL, as a tool may.

### Allocation and node layout

None changes. A line is made by the same `arena_.make_array` as a `(` frame, so a parse
allocates as before (`bench_parse`: 43 allocations in every build). `Token` is unchanged
(48 bytes, measured); `FrameInfo` gains two bools and an indent, in the padding after
`removes`; the parser gains a bool and a `uint32_t`, the lexer a `size_t`.

## 5. Payoff, measured where possible

- **Parentheses.** 1,650 of the corpus's 4,654 go (35.5%); 1,024 of the 31 named files'
  3,142 (32.6%): in the stacks' data files 584 of 1,962 (29.8%), in the packs 310 of 938
  (33.0%), in the examples 130 of 242 (53.7%). Lines ending in two or more closers go from 489 to 29,
  in three or more from 106 to 1 (the 68 files, comments aside).
- **Lines and bytes.** Almost nothing: 1,486 → 1,484 lines, -2.4% bytes. The house style
  already puts each child on its own line; the form removes characters, not lines.
- **Errors a person can no longer make.** A missing or extra closer at the end of a block
  (the 489 piles) cannot be typed in a line: there is none. A dedent that lands between
  levels is located where it lands, as a wrong close is today; a continued value list is
  refused instead of silently changing (section 2, rule 5). What a person *can* now do
  silently is section 6's first objection.
- **Diffs.** `promote --patch` on the test stack rewritten as lines (`baker_host_test_game`,
  `ALWAYS`) writes one line, its comment kept, exactly as for brackets:
  `-   speed 3.5   ; how fast` / `+   speed 4.5   ; how fast`; `promote --write` and `eval
  {config.get player.speed}` then read 4.5. Appending a child is a one-line addition: in
  `test_lines.cpp` `write_back_to("player.jump.height")` adds `   jump` / `      height 2`
  after `lives 3   ; per run` and touches no existing line, where the bracketed file's
  `(lives 3))` line must change to move its closer.
- **A tool.** An editor's outline is the indentation (the `line` bracket kind); an agent
  writing a line need not balance closers it cannot see. The migration tool and the printer
  agree with the core's reader on every corpus file (`tests.cpp`, "agree with the core's").
- **A designer.** Shapes, tuning sections and region files read as outlines (the samples);
  the named-instance files (metrics, sims) do not change, so a designer sees both forms in
  one stack.

## 6. Objections

- **"Cost per byte": the parser now pays for every file.** Conceded, measured: +1.67% of
  instructions on text that uses no lines (`ALWAYS`), +1.53% where files opt in
  (`DIRECTIVE`), against D143's bar of +1.1%; four layouts of the test landed between +1.5%
  and +2.7% (section 4). For a game reading the binary cache it is nothing; for a tool
  reading text, it is a tax on every file for a form most of the corpus would use only in
  part. An opt-in that costs bracketed files nothing needs `#lines` as a file's first line
  choosing a second copy of the parse loop (a template on the layout), which this prototype
  did not build or measure.
- **A second spelling is a second parser.** It is one parser here (the layout is a frame of
  the same parser, and `read_structure` mirrors it), but it is a second *spelling*: every
  stack would hold both forms, since named instances, wrapped rows and value lists stay
  bracketed, and the printer's fallback brackets what a line cannot say. A reader of Baker
  files must know both forever. Conceded.
- **"Solving a problem you don't have."** Partly conceded. The pain is real and measured
  (489 closer piles, the inserted-block mismatch), but the files are short (the stacks'
  data files average 40 lines) and an editor balances parentheses for free. No line is
  saved. The measured gain is characters and closers, not reading time, which this
  prototype could not measure.
- **Build time against run time.** The layout is decided at parse time, once per load; it
  adds nothing to lookup, evaluation, the cache or a frame. In a shipped game reading the
  binary cache it costs nothing at all.
- **The silent reinterpretation (the cost judge's fatal).** Measured, and not empty. Under
  `ALWAYS`, the 68 corpus files read to the same trees (the gate below), but 65 distinct
  texts the test suites read change meaning: each a value or a bare value list at a text's
  top level (`read_string("42")`, an included `1 2 3`, a macro body `10`, a loose symbol in a
  merged file). 59 of them fail a check (8 test cases, 61 assertions); 6 pass silently
  (`'#remove'` in `test_m3b.cpp:204`, and five truncated corpus prefixes and fuzz
  regressions ending in a bare atom). Three of the core's own readers read such text
  (a tweak's default, `Context::eval(text)`, a console request) and had to be pinned to
  today's reading with `LoadSession::lines = false`; without that, `eval("42")` returns
  `(42)`. Any host that reads a value's text with `read_string` changes the same way and
  nothing tells it. The class cannot be an error without forbidding the form. Conceded:
  this is the fatal, and it selects the `#lines` opt-in if the form is adopted at all.
- **A dedent onto an outer level is a different, well-formed tree.** `a / b / c 1 / d 2`
  with `d` back at `b`'s indent makes `d` a sibling of `b`, silently; only a dedent between
  levels is caught. Conceded as a limit (section 9).
- **The DTA heritage.** Every DTA reader, the modding community's tools and editors, and
  the milo corpus read brackets. A bracketed file still reads the same in every build (the
  gate), so the heritage is not broken; but a file written as lines is not DTA any more to
  any of them. The `DIRECTIVE` form keeps that explicit: `#lines` is an unknown directive to
  every other reader, which fails loudly rather than misreading.

## 7. Migration

The migration tool (`convert`, `to-lines-text`) rewrites a file in place of its brackets,
keeping every comment, directive, macro and alignment it can, and refuses nothing: an array
it cannot make a line keeps its brackets, and the result is read back with its includes and
compared before it is written. The printer (`to-new`, `WriteOptions::lines`) is for trees a
tool writes from scratch: it keeps no comment. Both forms read side by side indefinitely in
either build, since a bracketed file is read as today. Under `DIRECTIVE` a converted file
needs `#lines` at its top (the tool would add it); under `ALWAYS` nothing.

- **The one-way rule and packs.** Unchanged: a pack's declaration is its first form either
  way, and macros resolve as before. A pack written as lines is read as lines wherever it is
  included; a pack's includer may be in either form.
- **Layers and live-ops overlays.** An overlay is a text a live game receives
  (`push_layer_text`); in `ALWAYS` it reads as lines unless the host clears
  `LoadSession::lines`, so an overlay authored as brackets reads the same, and one authored
  as lines needs the receiving build to have the switch. Under `DIRECTIVE` the overlay says
  so itself.
- **Write-back** follows each file's form (section 3), so a migrated file stays migrated and
  a bracketed one bracketed.

## 8. What the prototype proved, and did not

Built and tested, on three builds (`OFF`, `ALWAYS`, `DIRECTIVE`, Release, GCC 13), with
clang-format and clang-tidy (`.clang-tidy`, the changed core files in `OFF` and `ALWAYS`)
clean:

- **The cost judge's first step.** Measured before anything else was decided, in four
  versions (section 4): the per-token cost is above D143's bar in every version, in the
  `DIRECTIVE` fallback too. This is the prototype's main finding.
- **OFF** (the default): all 23 ctest suites pass, and the parse executes the base's
  instructions exactly (1,845,499 per document in both).
- **DIRECTIVE**: all 23 suites pass, `baker_data_tests` 249 of 249 test cases, the new
  `lines:` cases included. No existing text changes meaning, the printer's new defaults
  included.
- **ALWAYS**: 22 of 23 suites pass (script, host, the host commands script, shape, alloc,
  headers, the experiments). `baker_data_tests` fails 11 of 249 cases, 64 assertions: 8 are
  the reinterpreted texts of section 6, 3 pin the printer's old output (a root value on its
  own line, `test_write.cpp:65` and `:85`, `test_preprocessor.cpp:135`).
- **The gate.** `baker_syntax_indent-lines trees` in the `OFF` and `ALWAYS` builds, diffed:
  0 of the 68 corpus files read differently. The reads of the test suites, logged by a
  temporary counter in `layout()` (not committed), give the 65 texts of section 6. The
  stacks' C++ tests were inspected, not built: their literals are bracketed. **Not run:** the
  brief's gate over the 464 raw milo files (`baker_parse_corpus --milo-paths`). There is no
  checkout on this machine, and fetching the dump would bring the decompilations the brief
  says not to consult; `trees`, or `baker_parse_corpus` built with `-DBAKER_INDENT_LINES=
  ALWAYS` and diffed against an `OFF` build, is the owner's one command to run where the
  checkout is (D101).
- **The corpus round trip.** `baker_syntax_indent-lines check`: 68 files, 0 failures, in
  every build (the translator's printer and reader in `OFF`, the core's in the others). The
  migration tool: 68 files, every tree the same. The polished samples: the same trees as
  their originals (`same`).
- **Tests added.** `tests/data/test_lines.cpp` (10 cases: the brief's snippets, each form, a
  bracketed text and the bare-atom gate, value text and `#lines`, directives with a
  conditional at the margin, `#define`, a macro, `#include` inside and outside a block, a
  pack line, the located errors — dedent between levels, a top-level line off the margin, a
  tab, the continued list, a stray close, `#remove`, the depth limit — provenance and
  extents, the printer, write-back and insertion in each form, `read_structure`, and
  `#lines` inside brackets), and the experiment's 15 cases (the corpus round trip, every
  form and error through whichever reader the build has, the translator against the core,
  the migration tool on the corpus and its rules).
  `fuzz/dta.dict` gains `\n   `, `\n      `, `\n\t` and `#lines`, and the fuzz harness reads a
  token alone as a value, so the corpus-prefix and regression tests pass under `ALWAYS`.
- **Not done.** No libFuzzer run of the layout (the harness and dictionary are ready; this
  machine's load did not leave time). No MSVC, libc++ or TSan build of the switch, and the
  local matrix (`tools/local_matrix.sh`) was not run: the branch changes no default
  behaviour, and the switch's builds were run by hand. No layout of the per-token test
  reached the design's floor (section 4); a second copy of the loop for `#lines` files was
  not tried.
  The `write_back_to` case of section 3 is not handled. The translator does not evaluate
  conditionals, so a line opened in one branch closes at that branch's end (the core keeps
  it open); they agree on the corpus.

## 9. Open questions for the owner

1. **Always on, or `#lines` per file?** Recommended: `#lines` per file, if the form is
   adopted, and only as a `#lines` first line that selects a second copy of the parse loop,
   so a bracketed file pays nothing. Both deciding numbers came out against always on: the
   instruction delta on bracketed text is +1.67%, above the bar, and the gate's silent class
   is not empty (65 texts, 6 of them silent, and three core readers that had to be pinned; a
   host's `read_string` of a value would change silently). The `DIRECTIVE` build as made
   pays +1.53%, so it is not yet the fallback the cost judge meant. Alternative: always on,
   with every value reader taught to clear `LoadSession::lines` and the bar raised.
2. **Does `write_dta`'s default flip to lines, rewriting the stacks and examples in one
   commit?** Recommended: no; lines stays a printer option and the migration tool the way
   files move, since one stack would still hold both forms. Alternative: flip it and rewrite.
3. **Tabs in indentation.** Recommended: refused, as built. Alternative: a fixed width of 8,
   which makes the same file read differently in two editors.
4. **A dedent onto an outer level.** Recommended: accept it as a well-formed tree (the
   error catches dedents between levels). Alternative: refuse a sibling line after a line
   with children unless the indent equals the first child's, which catches more typos and
   refuses some legal outlines.
5. **One-line text: console and `eval(text)`.** Recommended: never laid out, by the API
   (`LoadSession::lines` cleared by the value readers, as built), not by guessing from
   newlines. Alternative: a text without a newline reads as today, which makes `b 3` and
   `b 3\n` mean different things.
6. **The printer's bracket fallback past the width.** Recommended: keep it (one file may
   hold both forms; it already does for named instances). Alternative: break a long line's
   values onto deeper bracket rows, which the continued-list rule allows for groups only.
7. **one-form's full formatter.** Recommended: never, now that the printer's defaults match
   the house style. Alternative: a later tool beside `shape`.
8. **The house-style defaults alone (indent 3, width 100).** Recommended: adopt them on main
   whatever the answer to 1, as a two-line change: an inserted entry then matches its file.
   Alternative: keep 2 and 80.

## 10. Recommendation

**Decline the layout as Baker's reading, and adopt in part: the printer's house-style
defaults.** The prototype answers the two questions it was built for, and both answers are
against it. The per-token price on files that do not use the form is +1.67% of the parse's
instructions (+1.53% for the opt-in as built), above D143's +1.1% with four layouts tried;
and the silent class is not empty: no corpus file changes, but every value read as text
(`read_string("42")`, an included `1 2 3`, a macro body, a console line, a tweak's default)
silently becomes an array unless its reader opts out, which is the cost judge's fatal in
exactly the place a host would not look. What it buys is real but small: a third of the
parentheses (1,650 of 4,654) and nearly all the closer piles (489 lines to 29), no lines,
2.4% of bytes, and every stack would then hold both forms, since the named instances of
every metrics and sim file (272 arrays) keep their brackets. It costs about 250 core lines
and the budget. The indent-3, width-100 defaults are worth taking alone, as a two-line
change with a D-entry, so an entry `write_back_to` inserts matches its file (they broke no
existing test in the `DIRECTIVE` build). If the owner wants the form later, the next step
is the opt-in done so that it is free: `#lines` as a file's first line choosing a second
instantiation of the parse loop, measured against D143's bar, with the migration tool,
the `trees` gate and the samples on this branch as the starting point.

## 11. Verification, the raw gate, and review

*Added after the prototype, from the session's review round; `dev/syntax-ideas.md` on
`claude/fervent-cray-l1kyf0` compares all eight branches.* An agent independent of the
prototype rebuilt this branch, reran its tests and reproduced its numbers; a reviewer
judged it under the data-oriented lens (`.claude/skills/acton-review/`) and the tool
author's; and D101's raw gate (464 raw Harmonix files, `baker_parse_corpus --roundtrip
--milo-paths` over a local clone of mariteaux/milo-script-library, nothing from it in the
repository, D3) was run on the branch's build. Where a finding below disagrees with an
earlier section, this section is the later word.

- **Reviewed at** `b1a88ae`, the branch's final head.
- **Verifier.** Builds; 23 of 23 OFF and DIRECTIVE; ALWAYS fails 11 of 249 data cases, as
  section 8 says. All 12 cachegrind counts, the migration tool's counts, the samples and
  the line budget reproduce exactly. Corrections: `test_lines.cpp` is 262 lines, not 258;
  the closing-pile count depends on how comments are stripped (499/107 with careful
  stripping, against 489/106); the core layout tests sit under `#if BAKER_INDENT_LINES`,
  so the default build runs none of them.
- **Raw gate.** Sections 3, 8 and 10 say it was not run; this is the run. OFF and
  DIRECTIVE: 464/464, 0 failures, and tree hashes identical to today on all 426 files that
  read alone. ALWAYS: 464/464 parse, but **76 of the 426 read to a different tree with no
  error** (51 in `rb1/ps2/tutorial`, 11 in `gh2/ps2_deluxe/ui`, 11 in `rb1/ps2/ui`), because
  Harmonix scripts write a bare macro alone on a line at top level
  (`tutorial_11_tuning.dta:33` `START_BG_MUSIC`, `:35` `WAIT_FOR_VO`), which the layout
  reads as an array. The silent class is 16% of the raw corpus.
- **Technical director, 2 of 5, decline.** Ran the same gate and calls the 76 files fatal;
  the per-token test costs every parse whether or not the file uses the form; the opt-in
  as a second parse loop is neither built nor costed.
- **Tool author, 2 of 5, adopt in part (the printer defaults).** A wrapped row continued
  at its key's own indent, which is what an editor's auto-indent gives, moves to the
  parent with no error; every stack would hold two spellings for good.
- **Fuzzing.** libFuzzer under ASan and UBSan with `BAKER_INDENT_LINES=ALWAYS`, built as
  CI's fuzz job is and seeded with the corpus, the fuzz regressions and the samples, after
  porting main's harness fix for `(key #remove)` (`b94a465`). The first run stopped after
  853,555 executions on a file holding a second `(origin "...")` after its own: held as
  data, it prints as the root's first form, which the reader consumes as a declaration
  (D91), so the print round trip loses it. That reproduces on main in three lines and is
  not this branch's: it is a tree today's text cannot spell, as D13's are, so the harness
  skips its print round trip and the input is `tests/fuzz_regressions/origin_first_form.bin`
  (`da74957`). Resumed from the grown corpus: 1,027,204 more executions, 2,048 edges, no
  failure; 1,880,759 in all, short of spec §8's 10 million.
- **Recommendation after review.** Decline the layout; take indent 3 and width 100 as the
  printer's defaults, as section 10 says.

### The completion round

*Added in the session's last pass over every branch, which took each to spec §8's fuzz bar
and CI to green on every job; `dev/syntax-ideas.md` on `claude/fervent-cray-l1kyf0` records
the round for all eight.*

- **A defect in main's harness, found here.** Resumed from section 11's corpus, the run
  stopped after 563,079 more executions on a text whose macros expand to between 65,536 and
  100,000 nodes. The harness skipped a tree over 100,000 expanded nodes, but read its print
  back with a 65,536-node limit; macro sharing lets a text read under the limit, since a use
  counts once, while its print spells every node. main's harness has the same two numbers:
  `macro_sharing_budget.bin`, 164 bytes of three nested macros, fails it. The budget is now
  the session's own limit (`88ccf90`), here and on the other seven branches.
- **Fuzzing, 10,000,000 executions on the final parser** (the layout always on), in runs
  each resumed from the last one's corpus: a run stopped to apply a harness fix, or by one
  of two machine restarts, resumed from its last logged count, so no execution is counted
  twice. None found a failure in the parser. Section 11's 1,880,759 are not counted.
- **CI.** Every job passes on the final head. The release dry run had failed on six lines
  that cited this note, a decision number or a stack's path in running text; they were
  reworded (`b14d93c`).
