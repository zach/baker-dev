# Rows keyed by a path: a design note (`path-rows`)

*A syntax prototype, on the branch `claude/fervent-cray-l1kyf0-syntax-path-rows`; nothing of
it is on main. It merges four proposals: path-rows (`#paths` files), path-rows (`@key` with a
fold), patch-rows (`@key`, no fold) and path-rows (machines: a dotted tag, the console and
promote). The code is in `src/` behind `BAKER_PATH_ROWS` (off by default) and in
`experiments/syntax-path-rows/`.*

**Status: a prototype for the owner's decision.** The recommendation (§10) is to adopt it in
part.

## 1. Thesis

A layer file names what it changes by the path every tool already prints. A parenthesized
array at a file's top level whose first atom is a bare `/`-led path is a **row**:
`(/fighters.kestrel.moves.jab.startup 5)` is the tree
`(fighters (kestrel (moves (jab (startup 5)))))`. The parser builds the nesting when the
row closes. When the file closes, it folds the rows that share a prefix, so each key heads
one array per level. Below the parser nothing changes. Merge, lookup, provenance, shapes,
`check`, the binary cache (format 6) and every JSON document see the nested tree they see
today. A file without rows reads exactly as it does now.

The pain is that a layer is a diff written as a tree. An agent's `agent.dta`, a person's
`local.dta`, a live-ops overlay and a shape patch each change a handful of leaves, and each
leaf costs two parentheses for every level above it. The Duel move retune
`(fighters (kestrel (moves (jab (startup 5)))))` spends 10 parentheses on one value. A
reader counts brackets to learn which key changes. A wrong count does not always fail to
parse: it can land the value one level off, silently. Meanwhile every tool prints the leaf
as a path:
- the host kit's `diff` command, over `Config::diff`, prints `player.speed  3.5 -> 4.5`
  (`tests/host/test_commands.py:94`);
- errors print `'fighters/kestrel/health'` (D27);
- the console sets `{config.set a.b v}`.

Rows make that spelling the input as well. A line of `diff` pastes back as
`(/player.speed 4.5)`, and a layer reads as the diff it will become: one line per value,
each with its own comment and its own line in version control. This serves spec §4's layers
("move a winning experiment into shared data as a reviewable diff") and write-back's minimal
diff (spec §1). It also serves the stacks' agents (`dev/background/game-stacks.md` §3),
whose whole loop is writing and reading layer files. The sigil is `/`, not the proposals'
`@`, for three reasons:
- the at-metadata brief reserves `@` for metadata tags;
- `/` is already Path's separator and the character errors print paths with (D27);
- no bare atom in the repository's 68 `.dta` files begins with `/` except the lone symbol
  `/`, and `/*` already opens a comment (§2, the gate). The sigil takes nothing already
  written.

## 2. The syntax

### Grammar

```
ROW     := '(' KEY BODY ')'               at a file's top level only
KEY     := '/' SEGMENT { SEP SEGMENT }    one atom, at most 16 segments
SEP     := '.' | '/'
SEGMENT := NAME | QUOTED
NAME    := a bare symbol holding no '.' or '/', not all digits
QUOTED  := "'" bytes holding no "'" and none that ends an atom "'"
BODY    := what follows a tag today: values, metadata arrays (D88), tagged children,
           or #remove as the one value (D143)
```

- **The key's grammar is Path's** (`include/baker/path.hpp`, D27): segments separated by `.`
  or `/`, a `'quoted'` segment for a name holding either (`/menu.'v1.2'.price`), and at
  most `Path::kMaxSegments` = 16 segments.
- **A lone `/` is still the symbol `/`** (`odd_tokens.dta`, script division).
- **Recognition** is one test in `Parser::symbol()`: a bare atom of two or more bytes that
  begins with `/` and is slot 0 of a parenthesized array. The lexer is untouched:
  `kAtomEnd` and the comment rule stay as they are.

### What it builds

- **The row's own array.** The row closes as any array does (`close()`), its tag the key's
  last segment.
- **One wrapper per earlier segment.** Each is a two-node array, `(segment child)`, made in
  the load's arena (D64), with the row's file and line.
- **No new node type, array flag or header field.** Node, Array, `merge.cpp`, the cache
  and `config.cpp` are untouched.
- **The fold.** When the root frame closes, and only if the file held a row, arrays sharing
  a tag are joined in the order written, at the first one's place. The joined children are
  folded in turn, so each key heads one array per level. The join is `Parser::fold`, not
  `merge_into`, for a reason found in the prototype: FillMissing (#merge's policy) drops a
  later array's values and its `(key #remove)` children, and Overwrite applies removals
  instead of keeping them. Neither builds the tree the file spells.

### Errors, located at the key's first byte

| Text | Error |
|---|---|
| `(/fighters.0.health 5)` | a row names keys, and '0' is an element; an element is set with config.set or by writing the whole list |
| `(/a..b 1)`, `(/a.'' 1)` | the key has an empty segment |
| `(/a.b/ 1)` | the key ends with a separator |
| 17 segments | the key has more than 16 segments |
| `(/a.'b 1)`, `(/a.'b c' 1)` | the key has an unclosed quote; a key holds no space or bracket: write that key nested |
| `(/a.1e5 1)`, `(/a.$x 1)`, `(/a.#b 1)` | the key's segment 1e5 must be quoted: '1e5' |
| `(a (/b 1))`, `{f (/a 1)}`, `[M (/a 1)]` | a row is a top-level form; write the key nested, or quote '/b' for a symbol |

- **A bare segment must read alone as the symbol it names**, the rule the printer quotes by
  (`is_bare_symbol`). It is the prototype's addition to the brief. Without it,
  `(/a.1e5 1)` would name a symbol `1e5` whose own text reads back as a float, and edit
  mode's per-segment spans would not re-read as their nodes (the fuzz harness's
  invariant).
- **One duplicate rule.** A file with rows has one rule for a repeated key, at the root and
  in every array the fold joins:
  - a leaf written twice is an error at the later writing, `'fighters/kestrel/health' is
    written twice` (path as errors print it, D27);
  - a removal joined with anything is `'menu/debug_menu' is removed and also written`.

  A file without rows keeps D31's first-wins. A later row never silently wins. The rule
  covers two rows for one path, a row and a nested literal for one leaf, and two literals
  repeating a tag in a file that also holds rows.
- **The column of a duplicate** is that of the row whose line the later writing has, from a
  small table of the rows read. When a literal shares its line with a row, the column names
  the row: an approximation, and the line is always right.

### Today's forms in a file with rows

- **Quoting.** A `'quoted'` symbol is never a key: `('/x' 1)` is the symbol `/x` heading an
  array, as today. A `/`-led bare symbol anywhere but a parenthesized array's slot 0 is a
  symbol as today (`(path /usr/share/game)`, `{/a.b 1}`).
- **The printer's mirror rule.** It quotes a `/`-led symbol of two or more bytes in a
  parenthesized array's slot 0, at any depth: the mirror of D143's `#remove` rule, which
  keeps the round trip.
- **Numbers.** An all-digit segment is an element and refused, as above. A key is never a
  number token: no number begins with `/`.
- **Comments.** `/*` opens a comment, so a key cannot begin with `*`. The printer quotes
  such a first segment (`/'*x'`). A `;` comment after a row is the row's own line comment.
- **Empty arrays.** `(/a.b)` is `(a (b))`. Two empty rows for one path fold without error:
  neither holds a value.
- **`#remove`.** `(/menu.debug_menu #remove)` is `(menu (debug_menu #remove))`. The marker
  keeps D143's one-value rule, and `Array::finish` sets `kHasRemoval` on every wrapper.
- **Macros.**
  - A key is never macro-expanded: the row test comes before the macro lookup.
  - The body expands as any body does, and the one-way rule (spec §5) sees those uses.
  - A row inside a macro definition is an error (rows are top-level).
  - `#define X (/a 1)` is a body holding the symbol `/a`.
- **Directives.**
  - `#ifdef` skips rows like anything else.
  - An `#include`d file's rows are top-level in that file and fold there. Its nodes are
    then spliced where the include stands, as everything an included file holds is: under
    `(a #include rows.dta)` they nest inside `a`.
  - The including file folds the spliced arrays only if it holds rows itself.
- **Packs and origins.** `(pack ...)` and `(origin ...)` as first forms keep their meaning.
  `(/pack ...)` is a row, never a declaration.
- **Shapes.** A shape patch is a layer: `(/shapes.boss_phase.enter_at float (range 0 2))`.
  The shape is untouched; it sees `(shapes (boss_phase (enter_at ...)))`.
- **Scenarios and objectives.** These files are D31 lists of top-level `(scenario ...)` or
  `(objective ...)` forms (`stacks/duel/sim/cpu.dta`, `stacks/duel/metrics/duel.dta`). One
  row in such a file would make the fold join every scenario and refuse their shared keys.
  Rows are therefore for files of keys, not lists of records, and the printer writes a list
  file nested.

### Edit mode (D33, D128)

- **Value tokens** keep their own spans, so write-back replaces one token.
- **The key token** is split into per-segment sub-spans (a quoted segment's quotes
  included). Each synthesized tag therefore re-reads as its symbol under
  `fuzz/parse_harness.cpp`'s `spans_hold`.
- **Every wrapper's extent** is the row's bracket extent.
- **A wrapper's child span** carries `SourceSpan::macro` = the reserved symbol `#row`. A
  `#`-led name cannot be a macro, since the lexer reads it as a directive. Write-back thus
  knows a row-made array without a change to SourceSpan's layout.
- **A joined array** is recorded with the first member's extent and the members' spans
  concatenated.

### The printer

- **`WriteOptions::rows`** (behind the switch) prints each top-level array as rows, one per
  leaf, in the tree's order (`collect_paths`' order, `config_write.cpp:42-53`):
  - a leaf's metadata (the D88 keys) and bare tags such as `(optional)` stay in its row;
  - untagged arrays and commands are values;
  - a row is one line, however long.
- **What a row cannot spell is written nested:**
  - a key that is not a bare or quotable segment (a space, a bracket);
  - an array whose values follow its children;
  - an array that repeats a tag. A whole file whose top level repeats a tag is nested
    throughout, since the fold would join it. These are D31 lists: 14 of the 68 corpus files,
    the stacks' `(scenario ...)` and `(objective ...)` lists and `tests/corpus/ui_menu.dta`.
- **Unchanged forms.** `to_dta` without the option prints nested as today. `baker.reply`'s
  `text` stays the compact nested form, so console protocol 1.1 is untouched.

### The corpus, before and after

- **The Duel agent layer** (`stacks/tools/test_commands.py:122`):
  ```
  (fighters (kestrel (health 1100) (walk_back 1.7)))      (/fighters.kestrel.health 1100)
  (cpu (easy (reaction 26)))                               (/fighters.kestrel.walk_back 1.7)
                                                           (/cpu.easy.reaction 26)
  ```
  This is 2 lines with 14 parentheses before, and 3 rows with 6 after.
- **A Duel move retune:** `(fighters (kestrel (moves (jab (startup 5)))))` has 10
  parentheses in 46 characters. `(/fighters.kestrel.moves.jab.startup 5)` has 2 in 39.
- **The double-XP overlay** (`examples/live-ops/overlays/2026-10-17-double-xp.dta:5-7`)
  keeps its `(pack ...)` and `(origin ...)` forms. Its data goes from 3 lines to 2:
  `(/event.xp_multiplier 2)` and `(/event.banner "Double XP all weekend!")`.
- **A shape patch** (`tests/shape/bosses_patch.dta:2-4`):
  `(/shapes.boss_phase.enter_at float (range 0 2))` and
  `(/shapes.boss.taunts string (optional))`. Each field's key goes from 3 bracket pairs
  to 1; its metadata keeps its own.
- **A removal** (`docs/language.md:99`): `(/menu.debug_menu #remove)`. Promote now deletes
  it from the base as a `-` line (§3).
- **`diff`'s line** `player.speed  3.5 -> 4.5` (`tests/host/test_commands.py:94`) pastes
  back as `(/player.speed 4.5)`.

## 3. What it keeps and what it changes

| Part | Verdict |
|---|---|
| Lexer | **Unchanged.** `kAtomEnd`, comments and number classification are untouched; a key is one symbol token. |
| First-block estimate (D131) | **Left as it is.** The brief's change, counting a row key's segments in `tree_estimate`, was built and measured. It cost +5,200 instructions per 16 KB sample on every text over 16 KB, with or without rows. It saved no allocation on 1 MB of rows (137 a read either way), so it was taken out. |
| Parser | **Changed, behind the switch:** the slot-0 test in `symbol()`, the key scan (`row_key`), the wrap at `close()` (`wrap_row`) and the fold at the root (`fold`, `join`). With rows off, the code compiles out. |
| Printer | **Changed:** `WriteOptions::rows` and `put_rows`. The head-slot quoting of `/`-led symbols is a `Slot` argument, which replaces the old `bool marker`, so `#remove`'s rule and the key rule share one path. The default printing is unchanged. |
| Write-back (D34, D132, D183, D226) | **Re-provided.** `write_back` replaces a row's value token in place and keeps the line's comment, and D183's integer form holds (tested). Plan's walk accepts a `#row` span as written in the file. A row with no value gains one after its key's last segment. |
| Inserted children (D34) | **Changed for files of rows.** `write_back_to` into a layer whose file holds a top-level `(/`, or whose absent file it creates, appends one row at the file's end at column 0, `(/a.b.c v)`, instead of nesting. A nested file still nests. A key with a space falls back to nesting. |
| Promote (D134) | **Unchanged for values:** it still writes nested into `default.dta`. Promote from a rows `agent.dta` gives `default.dta` byte for byte as from the nested one (tested). |
| Promote of a removal (D143) | **New.** A `#remove` leaf whose key the target holds literally deletes the key's array extent, with its line when the key stood alone. Indentation is kept when closing brackets follow it, and a key the target lacks is nothing to do. This lifts the refusal at `config_write.cpp:457-462`, which no test covered. |
| Edit-mode source maps (D33, D128) | **Re-provided.** Per-segment sub-spans and the `#row` marker; SourceSpan's layout and the map's tables are unchanged. |
| Tolerant structure read (D220) | **Additive.** `TextTokenKind::PathKey` marks a row's key (a top-level `(`'s bare `/`-led tag). A nested one stays `Symbol`, since read_structure reports and never refuses. The enum is public, so a tool that switches on it sees one more case. `docs/tools.md` would document it. |
| Provenance and explain | **Unchanged.** Every array a row makes carries the row's line, so `source()` names the row (tested: `source("fighters.kestrel.walk_back")` is `agent.dta:3`), and `explain` lists the row's line. |
| Layers and `#remove` | **Unchanged.** Merge sees nested trees. A removal row removes the key below (tested). |
| `layer_effect`, `diff` | **Unchanged, and right because of the fold.** They read each tag's first child (D134), so two rows under one prefix without the fold would leave the second leaf unseen: the people judge's fatal. With it, `layer_effect` lists all three leaves of the Duel agent layer (tested). |
| `check`, layer policy, the demand report | **Unchanged.** `data_only` refuses a command in a row at the row's line (tested with `check_layer_text`). |
| Shapes and the reference JSON | **Unchanged.** Shapes see nested trees. No JSON schema changes but read_structure's token kind, which is not in a JSON document today. |
| The console's requests | **Unchanged.** `(ID {command})` and `{config.set a.b v}`. A row typed at the console is not a request: rows are data, as the tools judge asked. |
| The project file | **Unchanged.** It is read by the same parser; nothing there is a layer of leaves. |
| Binary cache (format 6) | **Unchanged.** It stores the folded, nested tree with its lines. A cached load of a rows file costs what a nested file's does, so the desugar and the fold are paid once, when the cache is written. |
| Fuzzing | **Re-provided.** `fuzz/dta.dict` gains `"(/"` and `"/a.b"`. `check_parse` holds over row inputs (tested). A gap found on the way, not caused by rows: `spans_hold` reads each literal span alone, and `#remove` alone is an error, so the harness rejects `(m (d #remove))` in today's build. |
| Host commands (`diff --rows`, `promote --patch --rows`, the journal export) | **Would need this work.** Each is a call to the rows printer or to `render_row`, a few lines in `src/host/commands.cpp` against the host kit's 2,400 budget. They are not built here. |
| Docs and the corpus | **Would need this work.** A section in `docs/language.md` after "Removing a key"; `docs/tools.md` for `PathKey`. The corpus stays valid: every file reads as today (the gate). |

## 4. Cost, measured

All on this machine (4 cores shared with seven other prototype builds, load average 9 to 12),
Release, GCC 13. The base is `3817213` (the session branch's code; its later commit `8f15684`
adds a document only).

**A file without rows: what every parse pays** (`tools/count_instructions.py`, the `parse`
case, 16 KB of config text; cachegrind, so the counts are exact on a busy machine):

| build | instructions per 16 KB | change |
|---|---:|---:|
| base | 1,845,499 | |
| first version: the row code inline, `tree_estimate` testing every atom byte | 1,902,657 | +3.10% |
| row code out of line (`BAKER_ROWS_COLD`), `tree_estimate` testing each `(` | 1,853,178 | +0.42% |
| `tree_estimate` left alone, the fold in per-depth buffers | 1,870,949 | +1.38% |
| the fold calls no second `vector<Node>` erase | 1,854,153 | +0.47% |
| final, with clang-tidy's fixes (the open row's segments initialized) | 1,854,169 | **+0.47%** |

- **The final +8,670 per 16 KB** is `Parser::run` +7,705 (the slot-0 test in `symbol()`
  and `close()`'s test of the row flag), `Parser::open` +884 (the flag's store), and the
  rest in building the Parser. Nothing else moves. `lookup` (203) and `set` (623) are
  unchanged.
- **Against the bar.** +0.47% is under the brief's +0.5%, narrowly, and under D143's +1.1%.
- **The two regressions on the way are worth keeping in view.**
  - Code in `close()`'s path stopped GCC inlining `close()` into `run()`.
  - A second call site of `std::vector<Node>::_M_erase_at_end`, in the cold fold, made GCC
    stop inlining it into `truncate()`, which every `close()` calls: +36k instructions per
    16 KB.

  The hook's cost depends on what the inliner does with code that never runs, so it needs
  the instruction count on every compiler the matrix builds.
- **Wall clock** (`tools/bench_compare.py --bench parse --runs 5`, alternating) is noise at
  this load: base 46.5 ms median (33.0–60.4), rows on 56.8 ms (32.4–80.6), ranges
  overlapping. Five direct alternating runs read 65.0–123.2 MB/s base and 63.0–129.3 MB/s
  rows on. **43 allocations** a parse on both sides.

**A file of rows: the same tree, nested and as rows** (the parser with rows on reads both;
`baker_syntax_path-rows bench` and `count`; best of 5 per run, runs interleaved):

| text | nested | rows | time ratio (three runs) |
|---|---|---|---|
| the stacks' data, 1 MB (every `stacks/*/data` file, tags made unique) | 1,050,986 B, 84.8 / 84.8 / 96.7 MB/s | 1,452,264 B, 39.4 / 38.8 / 35.6 MB/s | 2.97, 3.02, 3.75 |
| bench_parse's generated config text, 4 MB | 4,194,408 B, 83.9 / 107.5 / 86.6 MB/s | 4,834,294 B, 27.4 / 50.7 / 33.3 MB/s | 3.52, 2.44, 3.00 |
| `stacks/hearth/data/default.dta` alone | 5,766 B, 153.6 MB/s | 5,569 B, 72.3 MB/s | 2.05, 2.05, 2.02 |
| `stacks/wayfarer/data/default.dta` alone | 3,065 B, 152.5 MB/s | 2,761 B, 82.9 MB/s | 1.66, 1.66, 1.67 |
| `stacks/duel/data/default.dta` alone | 3,094 B, 184.6 MB/s | 4,185 B, 65.5 MB/s | 3.81, 3.82, 3.79 |
| the Duel agent layer (3 leaves) | 78 B, 1.1 µs | 90 B, 2.3 µs | 2.09, 2.09 |
| `tests/shape/bosses_patch.dta` | 172 B, 1.1 µs | 88 B, 1.9 µs | 1.74, 1.67 |

- **Instructions.** On the 1 MB of stacks data, a read is 91.85M instructions nested (87 a
  byte) and 212.48M as rows (146 a byte): **2.31 times the instructions** for the same tree.
- **Where the extra 120M go** (cachegrind, by function):
  - the fold's grouping sort, 26.4M;
  - the key scan, 21.9M (`row_key` 14.5M, `is_bare_symbol` 7.4M);
  - `fold` and `join`, 19.8M;
  - the rest is the syntax's own cost: k arrays a row (`make_array`, `finish`, `adopt`)
    and an interned symbol a segment.

  A merge-style tag index in place of the sort (D75's, reused) would remove most of the
  sort. Nothing removes the k arrays.
- **Allocations a read of the 1 MB:** 26 nested, **137 as rows**. The fold's first version
  made 129,806, a hash map and fresh vectors a join. It now keeps per-depth buffers, as
  MergeScratch does (D127).
- **A layer file costs about 1 µs more to read as rows.** No one will notice. A base file
  read as rows costs 1.7 to 3.8 times its nested read.
- **The translator** (the experiment's reader without the switch: Baker's parser, then a
  tree rewrite) reads the 1 MB at 55.6 and 58.4 times Baker's time. Its fold is pairwise.
  It is a test oracle, not a proposal.

**Lines and bytes of the stacks' real files** (`baker_syntax_path-rows stats`, each file
alone, its includes read as empty). "Written" is the file as it stands, comments included.
The two printed columns are the printers' output, without comments, so they compare like
with like.

| file | data leaves | mean depth | written: lines, bytes, parens | printed nested | rows |
|---|---:|---:|---|---|---|
| contraption/data/default.dta | 68 | 3.84 | 57, 2,363, 274 | 51, 2,127, 272 | 88, 3,582, 232 |
| cube/data/default.dta | 7 | 2.00 | 15, 666, 54 | 9, 435, 52 | 7, 440, 46 |
| duel/data/default.dta | 107 | 3.86 | 48, 3,133, 278 | 127, 3,133, 274 | 107, 4,185, 240 |
| hearth/data/default.dta | 130 | 3.55 | 108, 5,807, 476 | 132, 4,378, 444 | 123, 5,569, 350 |
| ledger/data/default.dta | 22 | 2.77 | 29, 1,667, 102 | 17, 1,005, 96 | 27, 1,359, 82 |
| ledger/data/economy.dta | 52 | 3.00 | 22, 1,496, 136 | 34, 1,143, 132 | 52, 1,661, 104 |
| stuntline/data/default.dta | 66 | 2.79 | 40, 1,959, 194 | 48, 1,599, 194 | 78, 2,371, 180 |
| wayfarer/data/default.dta | 58 | 2.78 | 74, 3,210, 316 | 56, 2,597, 310 | 50, 2,761, 262 |
| wayfarer/data/regions/interior.dta | 18 | 4.11 | 22, 754, 66 | 18, 637, 66 | 18, 944, 36 |
| wayfarer/data/regions/north_coast.dta | 8 | 4.25 | 9, 311, 28 | 8, 281, 28 | 8, 473, 16 |
| wayfarer/data/regions/south_coast.dta | 10 | 4.10 | 13, 423, 38 | 10, 352, 38 | 10, 525, 20 |
| **total** | **546** | **3.41** | **437, 21,789, 1,962** | **510, 17,687, 1,906** | **568, 23,870, 1,568** |

- **For base data, rows are bigger.** Against the nested printer's output, rows are 11% more
  lines, 35% more bytes and 18% fewer parentheses: every leaf repeats its path.
- **One overridden leaf at the stacks' mean depth** costs 6.8 parentheses nested and 2 as a
  row.

**The core's lines** (`tools/count_lines.py`):

| file | base | with the switch | change | of which preprocessor lines |
|---|---:|---:|---:|---:|
| `src/data/parse.cpp` | 1,300 | 1,493 | +193 | 23 |
| `src/data/write.cpp` | 158 | 229 | +71 | 8 |
| `src/data/config_write.cpp` | 592 | 667 | +75 | 11 |
| `src/data/merge.hpp`, `include/baker/write.hpp`, `include/baker/parse.hpp` | 192 | 204 | +12 | 6 |
| **core total** | **15,190** | **15,541** | **+351** | **48** |

- **The prototype is over the budget.** With the switch, the core counts 15,541, past the
  soft budget of 15,500 (D220).
- **Adopted whole, without the switch, it projects to about +297.** That drops the
  preprocessor lines and the refusal the `#else` keeps, and gives about 15,487 of 15,500:
  nearly twice the brief's estimate of +160. By part:
  - the parser hook, about 165 lines: the key scan 35, the wrap 12, the fold and join with
    their buffers 60, errors and members;
  - the rows printer, about 63;
  - write-back's rows branches, about 26;
  - promote's removal, about 38.
- **The prototype's own lines:** 1,246 in `experiments/syntax-path-rows/` (`main.cpp` 333,
  `syntax.cpp` 380, `syntax.hpp` 21, `tests.cpp` 258, `tests_config.cpp` 254), STL
  throughout.

**Layout and allocation.**
- No change to Node, Array (24 bytes), SourceSpan, the source map's tables or the cache.
- FrameInfo gains a `bool`, which fits its padding.
- The Parser gains its open row's key (16 segments, 256 bytes), the list of rows read and
  the fold's per-depth buffers. A file without rows allocates none of them.
- A row's wrappers are two-node arrays in the load's arena (D64).
- The switch changes a public struct (`WriteOptions::rows`) and enum
  (`TextTokenKind::PathKey`). CMake passes `BAKER_PATH_ROWS` publicly, as it passes
  `BAKER_SHIPPING`, so a consumer sees the library's choice.

## 5. Payoff, measured where possible

**The samples** (`experiments/syntax-path-rows/samples/`: each original beside its rows, by
the printer and then by hand with the originals' comments, `(pack ...)` and `(origin ...)`;
counts from `samples/counts.txt`; "code" lines are neither blank nor only a comment):

| sample | nested: lines, code, bytes, parens | rows: lines, code, bytes, parens |
|---|---|---|
| Duel agent layer (`stacks/tools/test_commands.py:122`) | 2, 2, 78, 14 | 3, 3, 90, 6 |
| Duel move retune | 1, 1, 47, 10 | 1, 1, 40, 2 |
| `tests/host/stack/data/agent.dta` | 1, 1, 21, 4 | 1, 1, 20, 2 |
| `2026-10-17-double-xp.dta` | 7, 5, 267, 20 | 6, 4, 267, 18 |
| `2026-10-18-drops.dta` | 5, 3, 158, 14 | 4, 2, 154, 12 |
| `2026-10-19-season-two.dta` | 5, 3, 178, 14 | 4, 2, 174, 12 |
| `2026-10-20-lucky-hour.dta` | 5, 3, 195, 14 | 4, 2, 191, 12 |
| `tests/shape/bosses_patch.dta` | 4, 3, 172, 14 | 3, 2, 169, 8 |
| `examples/config/data/handheld.dta` | 3, 2, 87, 4 | 2, 1, 83, 2 |
| `stacks/cube/data/default.dta` (a base file) | 15, 10, 666, 54 | 12, 7, 668, 48 |
| `stacks/ledger/data/economy.dta` (a base file of records) | 22, 17, 1,496, 136 | 58, 53, 2,132, 108 |

- **Layer files: one line per value, and fewer brackets.** Each overlay's data drops a line.
  The shape patch drops from 14 parentheses to 8, the retune from 10 to 2, and the agent
  layer from 14 to 6 at the price of a line. Bytes barely move, since a row spells the path
  that brackets imply.
- **Base files: no case for rows.** The economy export, a table of records, triples in
  lines and gains 42% in bytes, and its per-record comments (`; Goods, row 2`) can sit on
  only one of a record's four rows. The cube's flat tweaks are the best case of a base file,
  and even there rows are a wash in bytes.

**Errors a person can no longer make.**
- **A value one level off.** In a nested layer, a miscounted bracket can put a value at
  another level and still parse: `(fighters (kestrel (moves (jab (startup 5)) (walk_back
  1.7))))` reads, with `walk_back` under `moves`, one bracket from where it was meant, under
  `kestrel`. A row names its level.
- **A key written twice.** It is a located error in a file with rows, where a nested layer
  keeps the first silently (D31).
- **A layer that under-reports itself.** Two blocks for one prefix in a layer are folded, so
  `layer_effect`, `diff` and `promote` see every leaf (the people judge's fatal, tested).

**Smaller diffs.**
- **Adding a value** to a file of rows is one added line, at its end: the test's agent file
  gains `(/cpu.easy.patience 3)` and nothing else changes. In a nested file it rewrites the
  line of the array it joins: `(box (height 3))` becomes `(box (height 3) (width 4))`, one
  line removed and one added (`tests/data/test_config.cpp:722`).
- **A removal promoted** is one removed line in the base, where today it is refused.

**One spelling for tools and agents.**
- `diff` prints `player.speed  3.5 -> 4.5`, and the agent writes `(/player.speed 4.5)`.
- An error names `'fighters/kestrel/health'`, and `(/fighters/kestrel/health 1100)` reads,
  since both separators do (D27).
- `promote --patch` from a rows `agent.dta` is byte for byte the patch from the nested one,
  so a person reviews the same change either way (tested).

## 6. Objections

**Cost per byte** (the review's "if you don't understand the cost of solving the problem").
- **Files without rows.** Every symbol now pays one test before it is interned: a length,
  the quote flag, and one byte compare. §4 has the instruction count against the bar
  (+0.5%, with D143's +1.1% the ceiling).
- **Files of rows.** A row of k segments makes k arrays where the nested form shares its
  ancestors, and the fold rebuilds every joined array once. So rows are dearer per value
  than the nested text they replace (§4's 1 MB comparison).
- **The answer is where rows are written.** Layer files are a few lines, and the cache
  stores the folded tree. A shipped load of a rows file reads the cache and pays nothing for
  the syntax. Rows over a base file are a mistake the note does not recommend (§5's
  economy sample).

**A second parser.**
- **There is one parser**, and the hook is a few functions in it. The experiment's
  translator (Baker's parser, then a tree rewrite) exists only to show the semantics without
  touching the core. It is not proposed, and its tests pass against both implementations.
- **The real second thing is a second duplicate rule.** A file with rows refuses a repeated
  leaf where D31 keeps the first. Adding one row to a file whose top level repeats a tag
  turns its repeats into errors. That is action at a distance, but loud and located, never
  a silent reinterpretation. The concession: the rule must be learned, and `check` should
  name it.

**Build time against run time.**
- The desugar and the fold run at load, in the parser's existing pass, and the result is
  what the cache stores.
- Shipping reads caches, so shipped data pays nothing. Received live-ops text
  (`push_layer_text`) is parsed each time it arrives, but it is a few lines.
- Nothing about rows survives into the tree or reaches a frame.

**"Solving a problem you don't have."**
- **Small in bytes.** The repository's layer files are small: the four overlays hold 22
  lines, and `agent.dta` is one line. Rows save a few parentheses and a line or two each
  (§5). Measured in bytes, the problem is small.
- **Large in the loop.** The problem rows solve is the agent loop's and the live-ops
  console's: a layer written, diffed, explained and promoted many times a session, by
  tools that already speak in paths. That loop is the stacks' reason to exist.
- **The honest cost is the line budget.** The whole change takes most of what is left
  under 15,500 (§4). §10 proposes the part worth that.

**The corpus and the modding community.**
- **Nothing in the corpus changes meaning.** Rows add one rule, a bare `/`-led head at the
  top level, and the gate found no such head in the repository's 68 files or its 1,050 DTA
  literals.
- **The raw-DTA gate (D101's 464 files) is the owner's to run** (§8).
- **Rows are a Baker extension.** A community tool that reads DTA text will not read them,
  as it does not read `#remove`. Files meant for such tools stay nested, and the printer
  converts both ways.

## 7. Migration

- **Nothing has to migrate.** Every file today reads as it does now. With the switch on,
  21 of the repository's 23 test suites pass unchanged, and the other two differ only where
  a created layer file is now rows (§8). Both forms coexist for good: a file holds rows or
  not, and a stack mixes the two freely.
- **Converting a layer file** is the printer's rows mode (`baker_syntax_path-rows to-new`,
  or `to_dta(root, {.rows = true})`) and back (`to-dta`).
  - The printer loses comments, `(pack ...)` and `(origin ...)`, as `to_dta` does today, so
    the samples were finished by hand.
  - A converter that keeps comments would work from edit mode's spans. It is not built.
- **New files from tools are rows.** `write_back_to` creates an absent layer file as rows,
  and appends rows to a rows file. A nested file stays nested, unless a person converts it.
- **The one-way rule (spec §5)** is unaffected: a key is never a macro, and a body's macro
  uses are checked as before.
- **Packs.** A pack's declaration stays nested and first, and its files may hold rows below
  it.
- **Layers and `#remove`.** A removal row is D143's removal. Promote can now carry it to
  the target (§3).
- **Live-ops overlays.** Received text reads rows the same way. `check_layer_text` and
  `LayerPolicy` locate a refusal at the row's line (tested), and a live-ops console that
  writes overlays would write one row per value.

## 8. What the prototype proved, and did not

**Built:**
- **In `src/`, behind `BAKER_PATH_ROWS`:**
  - the parser's key scan, wrap and fold, with located errors;
  - the rows printer and the head-slot quoting;
  - write-back's `#row` spans, its append-a-row branch and created files of rows;
  - promote's removal;
  - read_structure's `PathKey`.
- **Behind `BAKER_PATH_ROWS_DOTTED`:** the sigil-free dotted tag (F).
- **In `tools/`:** `baker_parse_corpus --gate`, and `--new-cmake` for the two comparison
  scripts.
- **In `experiments/syntax-path-rows/`:** a translator with the same rules, the CLI
  (`to-new`, `to-dta`, `check`, `bench`, `stats`, `gate`, `count`), 26 test cases and the
  samples.

**Passed:**
- **The corpus round trip:** 68 of 68 files, 0 failures, with the switch off (translator)
  and on (parser). 14 files, the D31 lists, print nested throughout.
- **The experiment's tests:**
  - 17 cases without the switch;
  - 26 with it, each case shared by both builds passing against both implementations;
  - 26 with the dotted switch as well.

  They cover: the brief's five snippets, 1,000 generated leaf sets printed both ways and
  read back, folds, duplicates, removal, quoted segments, every key error, the lone `/`, the
  samples, the Config cases of §3, the fuzz harness's invariants over row inputs,
  read_structure, and the core printer.
- **Without the switch,** all of the repository's suites pass. A clean clone of `807915e`,
  configured with the defaults and `-DBAKER_BUILD_EXPERIMENTS=ON`, builds and passes 23 of
  23. The commit after it changes only code behind the switch.
- **With the switch,** 21 of 23 suites pass unchanged. The other two check the text of a
  layer file write-back creates, which is now rows, as designed:
  - `tests/data/test_m3b.cpp:562` expects `(box (width 4))` and reads `(/box.width 4)`;
  - `examples/config` prints `(/audio.channels 8)`.
- **The gate over the repository:**
  - 0 `/`-led parenthesized tags in the 68 files and the 1,050 DTA literals of the tests',
    examples' and stacks' sources;
  - 0 dotted tags in the files, and 5 in the literals: 4 are `{config.unsaved}`'s reply
    `(box/width phase)`, 1 an error message.
  - The dotted build reads that reply as `(box (width phase))`.
- **Instructions:** +0.47% on a file without rows, within the bar.

**Not done, or failed:**
- **D101's raw-DTA gate** (464 files, `baker_parse_corpus --milo-paths --roundtrip --gate`)
  was not run. The corpus is not on this machine, and this work was not to consult Harmonix
  material. The counters are built for the owner's run.
- **The local matrix** was not run: debug, C++23, shipping, ASan+UBSan, Clang, data-only,
  plain refcounts, EASTL, the ImGui harness, and clang-tidy. Two Release configurations
  were built, switch off and on, plus the dotted one. MSVC, libc++ and TSan are CI's.
- **The host commands** `diff --rows`, `promote --patch --rows` and the journal export were
  not built, nor a converter that keeps comments.
- **The wall-clock parse benchmark** could not separate the builds at this machine's load.
  The instruction count stands in for it.
- **A gap found in the fuzz harness,** not caused by rows: `spans_hold` fails on any
  `(key #remove)`. Row inputs without `#remove` pass it.

## 9. Open questions for the owner

1. **The sigil.** Recommended: **`/`**, with `@` left to metadata.
   - The gate counts 0 `/`-led paren tags in the repository's 68 files and 1,050 DTA
     literals.
   - The sigil-free dotted tag (F, built behind `BAKER_PATH_ROWS_DOTTED`) counts 0 in the
     files but 5 in the literals, and 4 of those are Baker's own `{config.unsaved}` reply
     `(box/width phase)` (`tests/script/test_config_tools.cpp:96`). Read back as data, F
     would silently turn that list into `(box (width phase))`: the D101-refused kind of
     reinterpretation the cost judge named.
   - Alternative: `@` for rows and another sigil for metadata.
2. **Rows at depth.** Recommended: **top level only.** `/` reads as the root, and D31 lists
   and nested forms keep their meaning. Rows relative to an enclosing array would make one
   form mean two paths.
3. **A row retuning a key a nested sibling in the same file also sets.** Recommended: **an
   error**, as built. Later-wins is the rule all three judges refused.
4. **Index segments.** Recommended: **refused** while Overwrite replaces a value list whole
   (D31). Revisit when layer markers' `#append`/`#drop` exist, since a row is their natural
   spelling.
5. **The rows printer as default text.** Recommended: **`diff --rows` and
   `promote --patch --rows` as options first**, and the stacks-agent skill switches
   `agent.dta` to rows. The integration repository's skill is that repository's decision.
   `layer_effect`'s and the journal's default text stays nested until the agents' use
   shows rows read better.
6. **`write_back_to` into a nested file.** Recommended: **it still nests**, as built. A file
   converts only when a person converts it.
7. **A name with a space.** Recommended: **nested only.** No tag in the 68 corpus files
   holds a space, bracket or quote. A `/"..."` form waits for a real key that needs it.
8. **The budget** (new). Recommended: **adopt the parser hook and write-back's rows branches
   in the core, about 200 lines, and keep the full rows printer a tool** (§10).
   - The whole change, without the switch, takes about 297 of the 310 lines left.
   - The host kit has 39 (2,361 of 2,400), so the printer does not fit there either.
   - `diff --rows` needs only the one-row renderer write-back already has.
9. **Promote's removal** (new). Recommended: **adopt it as its own step.** It closes a gap
   of D143's that rows only made visible: a nested layer's `(key #remove)` needs it as much.
10. **The fuzz harness's `#remove` gap** (new). Recommended: **fix `spans_hold`** to skip
    the removal marker, which is not a token alone. A one-line change, independent of
    rows.

## 10. Recommendation

**Adopt in part.** Rows are a good syntax for layer files and a poor one for base data, and
the measures say both plainly:
- **For layers:** a layer's override goes from 2×depth brackets to 2, a value added is a
  line added, and a key written twice is an error. Promote's patch is the same, and a
  layer's read costs about a microsecond more.
- **For base data:** rows are 35% bigger and read at 1.7 to 3.8 times the time, so base
  files stay nested.
- **What every parse pays** for the hook is +0.47% instructions, within the bar but at the
  mercy of the inliner.
- **The line budget is the real price.** Adopted whole, the change takes about 297 of the
  310 lines left.

So adopt the parser hook and write-back's two rows branches, about 200 lines without the
switch: the key scan, the wrap, the fold, the head-slot quoting, the `#row` spans, appending
a row, and the created file. Keep the full rows printer a tool (the experiment's
converter), and give `diff --rows` and `promote --patch --rows` the one-row renderer
write-back already has. Take promote's removal as its own step, since it closes D143's gap
for nested layers too, and fix the fuzz harness's `#remove` gap. Decline F.

The next step is the owner's answers to §9, then:
1. the owner's run of the 464-file gate with `--gate`;
2. a D-entry with this note's numbers;
3. the hook on main with this branch's tests;
4. a section in `docs/language.md`;
5. the stacks-agent skill writing `agent.dta` as rows.

## 11. Verification, the raw gate, and review

*Added after the prototype, from the session's review round; `dev/syntax-ideas.md` on
`claude/fervent-cray-l1kyf0` compares all eight branches.* An agent independent of the
prototype rebuilt this branch, reran its tests and reproduced its numbers; a reviewer
judged it under the data-oriented lens (`.claude/skills/acton-review/`) and the tool
author's; and D101's raw gate (464 raw Harmonix files, `baker_parse_corpus --roundtrip
--milo-paths` over a local clone of mariteaux/milo-script-library, nothing from it in the
repository, D3) was run on the branch's build. Where a finding below disagrees with an
earlier section, this section is the later word.

- **Reviewed at** `d89146f`, the branch's final head.
- **Verifier.** Builds; 23 of 23 by default, 21 of 23 with the switch (the two designed
  failures). Line counts, the +0.47% parse count, allocations (43; 26 against 137 per MB),
  the 1 MB counts (within 0.06%), every sample and the gate counters reproduce exactly. The
  +297 adoption figure is an estimate; wall-clock ratios ran somewhat lower on a loaded
  machine. Gaps: the host commands (`diff --rows`, `promote --patch --rows`, the journal
  export) are not built; no D-entry; `promote --patch` is not tested at the host level;
  the dotted variant has no test of its own.
- **Raw gate.** Rows on: 464/464, 0 failures, 0 tags begin with `/`. Dotted variant:
  464/464 with **1 round-trip failure**, and 1,097 tags holding `.` or `/` (35 at top
  level, `motto.btn`, `tutorials.btn`, …) would read as rows: the sigil-free form is out.
- **Technical director, 2 of 5, explore further.** The cost is measured and the benefit
  is not: the repository's layer files are a few lines and no misplaced-bracket bug is on
  record. The +0.47% is GCC's alone and moves with the inliner. The note never weighs the
  zero-grammar alternative: a host-kit `set PATH VALUE --layer agent` over `cfg.set` and
  `write_back_to`. Whether a file has a row decides its duplicate rule, an action at a
  distance.
- **Tool author, 4 of 5, adopt in part.** A hand rewrite of a Duel layer went from 28
  parentheses to 12 and read back to the same tree. Four edges first: the host commands
  above; `#row` overloads the public `SourceSpan::macro`, whose documented meaning it
  changes; a stray row in a D31 list file switches the file's duplicate rule and the error
  names an unrelated line; `write_back_to` appends new rows at the file's end.
- **Fuzzing.** libFuzzer under ASan and UBSan with `BAKER_PATH_ROWS=ON`, built as CI's
  fuzz job is and seeded with the corpus, the fuzz regressions and the samples, after
  porting main's harness fix for `(key #remove)` (`d09f51d`): 1,025,153 executions in 25
  minutes, 2,366 edges, no failure. Short of spec §8's 10 million, which belongs to an
  adoption step.
- **Recommendation after review.** Compare rows with a host `set` command in the stacks'
  agent loop before taking the grammar; fix the four edges either way; promote's removal
  stands on its own.
