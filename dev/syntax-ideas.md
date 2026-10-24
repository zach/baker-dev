# Eight syntax ideas, prototyped and compared

One pass at radical changes to Baker's text form, each sketched in code on its own branch
and judged by measurement. `dev/syntax-survey.md` is how the eight were chosen: seven readers
mapped the language and its tools, eight ideators proposed 34 changes from eight angles,
three judges scored them, and a synthesizer merged the overlapping ones into briefs. Each
branch forks from this one (`claude/fervent-cray-l1kyf0`, main plus the `experiments/`
harness), carries its prototype under `experiments/syntax-<slug>/` and, where the idea
needed one, a core change (behind a build switch, except layer-markers), and has a design
note at `dev/syntax-<slug>-design.md` in ten sections (thesis, syntax, what it keeps and
changes, cost, payoff, objections, migration, what the prototype proved, open questions,
recommendation). Nothing is merged to main. This note ranks them, records what an
independent verifier reproduced, what two reviewers found (the technical director's lens,
`.claude/skills/acton-review/`, and the tool author's), what D101's raw gate says, and what
the owner has to decide.

## At a glance

Numbers are instructions per 16 KB parse of bench_parse's text (base 1,845,499) unless
marked, and core lines from `tools/count_lines.py` (base 15,190 of the 15,500 soft budget,
D220). "Gate" is D101's raw corpus, 464 files, run on each branch's switch-on build
(below). Scores are 1 to 5 from the two reviewers.

| Rank | Idea | Branch `claude/fervent-cray-l1kyf0-syntax-…` | Parse cost | Core lines | Gate | TD | Tools | Recommendation |
|---:|---|---|---|---|---|---:|---:|---|
| 1 | infix commands, and an operator vocabulary | `curly-infix` | 0 off; +0.56% with the reader | ~125 (the vocabulary) | 464/464; 0 braces fire | 3 | 3 | adopt the vocabulary; hold the infix reader |
| 2 | rows keyed by a path, for layers | `path-rows` | +0.47% | ~200 | 464/464; the dotted variant fails | 2 | 4 | adopt in part, after a comparison with a host `set` command |
| 3 | `#append`, `#drop`, `#was` beside `#remove` | `layer-markers` | −0.18% (check +2.19%) | ~240 as built; ~150 narrowed | 464/464 | 3 | 3 | promote's removal now; the verbs narrowed, when a package needs them |
| 4 | `#pack`, `#origin`, `#shape`; `#` before a letter | `hash-declarations` | 0 (no core change) | ~100 (estimated); shape +20 | 464/464 | 3 | 3 | the parts that need no syntax now; hold the directives |
| 5 | an `@` tag is metadata | `at-metadata` | +1.01% with the switch; `find_float` +55% | ~104; ~35 for the spelling alone | **456/464** | 2 | 3 | explore the spelling-only pass |
| 6 | `#columns` headers over keyed tables | `column-tables` | not built in the core | ~120 (estimated) | not run (no core change) | 2 | 2 | decline; an aligning printer instead |
| 7 | indentation as structure | `indent-lines` | +1.67% always; +1.53% opt-in | ~250 | **76 files read differently** (always) | 2 | 2 | decline; take the printer's house style |
| 8 | `(#layer ...)` blocks as sub-layers | `inline-layers` | −0.05% | 221 | 464/464 | 2 | 2 | decline; `(when COND)` on project layers |

All eight branches build, pass their tests in their default configuration (23 of 23
suites with experiments on), round-trip the 68 corpus files with no fault, and are pushed; each verifier
rebuilt the branch, reran the tests and reproduced the note's numbers, with the exceptions
listed under each idea.

## The branches at their final heads

Each branch forks from this one at `3817213` (main at `565bb63` plus the harness); main has
since moved on (the 0.6.0 release and later D-entries), and none of the branches has been
merged anywhere. "Reviewed at" is the head the verifier and the reviewer read; each
branch's last commit adds its design note's section 11, which records that review, the
verifier's corrections and the raw gate (curly-infix's commits between the two add only
its fuzz-harness fix and fuzz record). "Core files" counts files changed under `src/` and
`include/`.

| Branch | Final head | Commits | Reviewed at | Core files |
|---|---|---:|---|---:|
| `claude/fervent-cray-l1kyf0-syntax-curly-infix` | `ffbe386` | 13 | `ed037c5` | 10 |
| `claude/fervent-cray-l1kyf0-syntax-path-rows` | `a0df58c` | 9 | `d89146f` | 6 |
| `claude/fervent-cray-l1kyf0-syntax-layer-markers` | `b1674cb` | 9 | `39f5248` | 16 |
| `claude/fervent-cray-l1kyf0-syntax-hash-declarations` | `3c65782` | 13 | `104fcec` | 2 |
| `claude/fervent-cray-l1kyf0-syntax-at-metadata` | `c9cd552` | 11 | `1c73321` | 11 |
| `claude/fervent-cray-l1kyf0-syntax-column-tables` | `cd49650` | 5 | `2f53a8a` | 0 |
| `claude/fervent-cray-l1kyf0-syntax-indent-lines` | `19c6e88` | 8 | `b1a88ae` | 8 |
| `claude/fervent-cray-l1kyf0-syntax-inline-layers` | `baa3828` | 11 | `85d53e7` | 7 |

## How it was done

- **The harness** (`experiments/`, on this branch): the corpus (68 `.dta` files under
  `tests/`, `examples/` and `stacks/`), a comparison that names where two trees first
  differ, the round trip (Baker's reader, the idea's printer, the idea's reader, compare),
  and a benchmark against `baker::read_string` on the same content. `syntax-template/` is
  the identity experiment (round trip clean, time ratio 0.98).
- **Each prototype** is a reader of its form into Baker's existing tree and a printer back,
  so every tool keeps working on what it reads. Six of the eight also built the change in
  the core (five behind a switch that is off by default, layer-markers always on),
  because the cost that decides them is a per-token cost in Baker's own parser, which a
  translator cannot show.
- **Measurement** is cachegrind instruction counts (`tools/count_instructions.py`) against
  the fork point, because the machine was shared by eight builds at a load of 6 to 16 and
  wall-clock rows moved by up to 65% with no code change. Wall clock is reported where it
  agrees.
- **Verification**: one agent per branch rebuilt it from its worktree, reran every test
  and reproduced every number it could in a few minutes; one reviewer per branch judged it
  under both lenses and rewrote a corpus snippet in the new syntax to check the note's
  examples.

## D101's raw gate on every branch

The gate is the 464 raw Harmonix files of D101 (RB1 PS2 304, GH2 PS2 Deluxe 120, GH2 360
DLC 40), read with `baker_parse_corpus --roundtrip --milo-paths` from a local clone of
`mariteaux/milo-script-library`, the corpus the tool's own header names. As D3 requires,
nothing from it is in any branch; the run was in the session's scratch directory. Parsing
is not enough for an idea that changes how text reads, so `indent-lines` was also compared
tree by tree.

| Build | Parsed, round trip | What else |
|---|---|---|
| this branch (main + harness) | 464/464, 0 failures | 7 files include one of 6 files the dump lacks (as D101) |
| hash-declarations, layer-markers, inline-layers | 464/464, 0 | |
| curly-infix, switch on | 464/464, 0 | the infix rule fired on **0 braces**: the branch's own condition for adopting it is met |
| path-rows, `/` rows on | 464/464, 0 | 0 tags begin with `/`; 1,097 tags hold `.` or `/` (35 at top level) |
| path-rows, dotted variant | 464/464, **1 failure** | reads those 1,097 dotted tags (`motto.btn`, `tutorials.btn`, …) as rows: the sigil-free form is out |
| at-metadata, switch on | **456/464** | one site: `rb1/ps2/config/cheats.dta:81` binds the keyboard's `@` key as `('@' "Game: Win w/ 3 stars…" {fake_win 2})`, included by 8 files; a located error, not silent. A quoted symbol must never be metadata |
| indent-lines, opt-in `#lines` | 464/464, 0 | trees identical to today on all 426 files that read alone |
| indent-lines, always on | 464/464, 0 | but **76 of 426 files read to a different tree, silently** (51 in `rb1/ps2/tutorial`): Harmonix scripts write a bare macro alone on a line at top level (`START_BG_MUSIC`, `WAIT_FOR_VO`), which the layout reads as an array |

## The eight, in rank order

### 1. curly-infix: the vocabulary now, the infix reader later

`claude/fervent-cray-l1kyf0-syntax-curly-infix`, `dev/syntax-curly-infix-design.md`.

Inside braces only, a brace whose odd slots are one and the same operator is rewritten
head-first when it closes, so `{0.3 <= $value <= 0.7}` is the tree `{<= 0.3 $value 0.7}`
and `{$i += 1}` is `{+= $i 1}`; mixed operators are a located error, not a precedence
table. An array flag remembers the spelling so the printer gives it back. With it comes a
vocabulary: variadic `+ - *`, chained `< <= > >=`, and `+= -= *= /=`.

```dta
(pass {and {>= $value 0.3} {<= $value 0.7}})   ; stacks/duel/metrics/duel.dta:31 today
(pass {<= 0.3 $value 0.7})                     ; the vocabulary alone, prefix
(pass {0.3 <= $value <= 0.7})                  ; the infix reader: the same tree
```

The vocabulary is faster than main on every operator it touches (`{+ 1 2}` 633 to 450
instructions; a band 1,755 as `and` of two calls, 775 as one chained call, 54.75 ns against
132.58 ns) and allocates nothing. The reader rule costs +0.56% of every parse even with no
operator, so it sits behind `BAKER_CURLY_INFIX`; written infix, a brace costs about 190
instructions at load. The converter rewrote 76 of 210 top-level commands in 14 files; the
11 samples lose 1.7% of their bytes and no lines. Core 15,190 to 15,477 (+287: the
vocabulary alone +124). libFuzzer ran 2,220,593 executions with the switch on, short of
the 10 M bar, and its one stop was the harness defect on main below, as was the
`[property]` defect it also found. The gate,
which the note made the condition for adopting the reader, fired on 0 of the raw corpus's
braces.

The technical director's review (3 of 5, adopt in part) put the payoff in its place: the
infix tree is the prefix chain's tree, so every saving in evaluation and brackets belongs
to the vocabulary, and over the 76 commands prefix chains use the same 252 brackets and
fewer atoms (318 against 338); the reader buys reading order alone, which nobody measured, for
most of the remaining budget, and a second spelling every text tool must learn (the
stacks' agent docs included). The tools review (3, adopt in part) found the spellings
`check` misses: `{0.3 <= $value 0.7}`, `{$value <= 0.7 0.3}` and `{$value <=}` all pass,
where the prefix typo is an error.

**Recommendation: adopt in part.** The vocabulary now, with `+=` and its family and the
property fix, as its own D-entry; drop `&& || !`. Hold the infix reader until `check`
rejects malformed infix and a trial shows it reads better than prefix chains.

### 2. path-rows: a layer file is the diff it will become

`claude/fervent-cray-l1kyf0-syntax-path-rows`, `dev/syntax-path-rows-design.md`.

A parenthesized array whose first atom is a rooted path is the nested tree that path names:
`(/fighters.kestrel.moves.jab.startup 5)` reads as
`(fighters (kestrel (moves (jab (startup 5)))))`, with the row's file and line on every
wrapper, and rows sharing a prefix fold so each key heads one array per level. It is the
spelling `diff`, `explain`, errors and the console already print, written into the files
people and agents actually write by hand: layers, overlays, agent edits.

```dta
; stacks/tools/test_commands.py:124, the agent layer today: 2 lines, 14 parentheses
(fighters (kestrel (health 1100) (walk_back 1.7)))
(cpu (easy (reaction 26)))
; rows: 3 lines, 6 parentheses, each value on its own line with its reason
(/fighters.kestrel.health 1100)      ; survives one more jab string
(/fighters.kestrel.walk_back 1.7)
(/cpu.easy.reaction 26)
```

Built in the core behind `BAKER_PATH_ROWS`. Every parse of a file without rows pays +0.47%
(1,845,499 to 1,854,169, under the +0.5% bar after four tries; the first cost +3.10%
because `close()` stopped being inlined). Rows themselves cost 2.31 times the instructions
of the same tree nested (137 allocations against 26 per MB), about a microsecond on a
layer-sized file, so the note is plain that rows are for layers and base files stay
nested: over the stacks' 11 data files rows are 11% more lines and 35% more bytes. A Duel
move override goes from 10 parentheses to 2; a key written twice is a located error;
`write_back` replaces one token, `write_back_to` appends a row, `promote` from a rows layer
is byte-identical to promote from the nested one and can finally write a removal. Core
+351 with the switch's scaffolding (over the budget), about +297 adopted whole, about +200
for the recommended part.

The sigil matters: the dotted variant (a bare `fighters.kestrel.health` as a row's key)
reads 1,097 tags of the raw corpus and four of Baker's own `{config.unsaved}` replies as
rows, silently, so the `/` stays.

The tool author scored it 4 of 5 (adopt in part): a hand rewrite of a Duel layer went
from 28 parentheses to 12 and read back to the same tree, and write-back, provenance,
`check` and promote all hold. Four edges come before adoption: the host commands the loop
depends on (`diff --rows`, `promote --patch --rows`, the journal export) are not built;
`#row` is stored in the public `SourceSpan::macro`, whose documented meaning ("spliced in
from this macro, so not written here") it changes; a stray row in a D31 list file
(scenarios, objectives) switches the whole file's duplicate rule, and the error names an
unrelated line; and `write_back_to` appends a new row at the file's end, away from its
siblings. The technical director scored it 2 (explore further): the cost is measured but
the benefit is not (the repository's layer files are a few lines and no misplaced-bracket
bug is on record), the +0.47% is GCC's alone and moves with the inliner, and the note
never weighs the zero-grammar alternative, a host-kit `set PATH VALUE --layer agent` over
the existing `cfg.set` and `write_back_to`, which gives the agent the path spelling with
no parser change.

**Recommendation: adopt in part, after one comparison.** The branch proposes the parser
hook and write-back's rows branches (about 200 core lines), promote's removal as its own
step, the full rows printer kept a tool and the dotted form declined. Before the grammar,
run the stacks' agent loop with a rows `agent.dta` and with a host `set` command over
today's write-back, and take the grammar only if rows win; fix the four tool edges either
way. Promote's removal stands on its own.

### 3. layer-markers: verbs for layers beside `#remove`

`claude/fervent-cray-l1kyf0-syntax-layer-markers`, `dev/syntax-layer-markers-design.md`.

`(key #remove)` is one verb of a merge language the layers never got. `#append` and
`#drop` use D143's slot rule, header bit and printer exception, so two packages that each
add to one list compose in stack order, a patch drops one cancel without restating a list
a person may have retuned since, and `promote` writes a removal by deleting the key's
extent instead of refusing. `#was` pins the value a row was written against, so a stale
overlay is a located warning where it is staged.

```dta
(items #append rope)                                       ; dlc_a
(items #append torch)                                      ; dlc_b: items gains both
(fighters (kestrel (moves (jab (cancels #append palm)))))  ; one cancel, the list unknown
(cpu (easy (reaction 26 #was 30)))                         ; stale if the base moved
```

Built in the core, always on, as the only prototype that is the change itself rather than
a sketch of it: parse −0.18%, `check` +2.19%, `check_text` +1.89%, every other case 0; a
check with every row pinned +15.3%, which misses the brief's +2% bar because a pinned row
is rebuilt instead of shared. The stacks hold 26 lists, of which 16 a keyed table cannot
cover; adding one element is 41% fewer bytes than restating, dropping one 16%.
`supplier()` and `source()` skip a composing row and name the literal run's layer, and
write-back refuses, located, a path a layer above composes (the hazard the tools judge
named at ideation). A
random driver found a real printer bug in its first 20,000 inputs, fixed, then 1,000,000
inputs clean. Core 15,190 to **15,499 of 15,500**: the whole headroom, against an estimate
of 230.

The verifier found the Hearth sample overstates the payoff (those things are keyed, and
`(phone #remove)` already does it) and that the test proving every layer reader goes
through `leaf_values` greps for a function that no longer exists, so it proves nothing.

Both reviewers scored it 3. The technical director (explore further) found an invariant
hole the note misses: the marker flag is set by the parser, `make_from` and `make_copy` but
not by `Array::set`, so an overlay a C++ live-ops tool builds with `make` and `set` merges
with `#append` leaking into the merged tree (verified). The +2.19% on a marker-free check
is a new scan of every node in `make_from`, which setting the flag where a marker is made
would remove; and the need is thinner than 16 lists, 11 of them Wayfarer cell geometry no
package extends. The tool author (adopt in part) found the agent loop loses idempotence:
after `promote --patch` lands, a `(cancels #append palm)` left in `agent.dta` gives
`jab low_kick palm palm`, silently (verified); a verb row cannot carry `#was`, so the
staleness guard never reaches composition; and the Duel sample fails Duel's own shape
check.

**Recommendation: adopt in part, narrower than the branch.** Promote's removal by extent
now. `#append` and `#drop` when a package that composes a list exists, rebuilt to cost
nothing when unused (the flag set where a marker is made, `Array::set` included) and to
warn when an appended operand is already present below, at about 150 lines; `#was` with
at-metadata, where it and its note belong.

### 4. hash-declarations: what the reader is told, in the reader's namespace

`claude/fervent-cray-l1kyf0-syntax-hash-declarations`,
`dev/syntax-hash-declarations-design.md`.

Everything a file says to the reader rather than to the game moves into `#`: `#pack (...)`,
`#origin (...)` and `#shape (...)` are directives taking one form, recorded in the session
and never in the tree, so position sniffing ends (`(origin 0 0 0)` is data anywhere, a
second declaration is a located error) and the `(shapes ...)` container D84 forced goes.
`#` opens a directive only before a letter, so `#`, `#1` and `#00aa00` are symbols again.
A list may say its length, `(list float 4)`. `[NAME ...]` becomes the one macro form.

```dta
(pack boss_tools (level task) (requires combat_core))  ; today: a declaration only as the first form
#pack (boss_tools (level task) (requires combat_core)) ; new: anywhere, once
(hitbox (list float) (desc "x y w h, meters"))        ; today: a 3-float hitbox passes
(hitbox (list float 4) (desc "x y w h, meters"))      ; new: it is an error
```

No core change was made; the prototype is a translator that migrates and checks, and the
shape library gained the arity (547 to 567 of 600 lines). The migrator rewrote 18 of the
68 files (11 packs, 2 origins, 12 containers into 30 `#shape`, 2 `#define` bodies) and
each reads back with the same tree, value digest and session. The packs lose 3% of their
bytes. Taking shapes out of the merged tree is a wash in instructions (a toggle −5 to −7.5%
of a merge, a push +4 to +7%). The adoption is estimated at about +100 core lines. It found
the received-layer gap on main below, which the new form closes. The verifier found the
translator's instruction counts stale at HEAD (a ratio of 1.527, not 1.476), which does not
touch the core estimate.

Both reviewers scored it 3 (adopt in part). The technical director: most of the payoff
does not need the new syntax. The arity is built on today's `(shapes ...)`, D108's
wrong-depth failure is closed by the governs-nothing finding, and the received-layer gap by
a policy rule; the namespace move answers failures the repository's 68 files do not have,
for an estimated 100 lines not yet written. The tool author: moving shapes out of the tree
takes shape changes out of `diff`, `layer_effect` and `explain`, so an agent layer could
loosen a shape invisibly, and `read_structure`'s bracket tree would show
`#shape (fighter ...)` inside `(fighters ...)` as a fighter named `fighter`.

**Recommendation: adopt the parts that need no new syntax.** The arity (with `count` in
`baker.shapes`), the governs-nothing finding, a policy rule for received shapes, and the
`#`-before-a-letter rule now. Hold `#pack`, `#origin` and `#shape` until `diff`,
`layer_effect` and the tolerant read can show a declaration; hold `[NAME ...]`-only;
defer the at-the-table form.

### 5. at-metadata: an `@` tag is never a key

`claude/fervent-cray-l1kyf0-syntax-at-metadata`, `dev/syntax-at-metadata-design.md`.

A tagged child whose tag begins with `@` is metadata: `(hit_pause 6 (@range 0 30) (@desc
"..."))`, `(startup int (@range 1 60) (@kind frames))`, `(move (@at fighters * moves)
(@keyed) ...)`. D88's closed list of seven names and the shape grammar's reserved words go,
so `(ball (kind ball) (pos -6 6.5))` is two fields in any order and a field may be named
`kind`, `desc`, `at` or `keyed`; `(@rnage 0 1)` is an error instead of silence. In the
strong form, `values()` stops at the first `@` child for every reader, so
`get<std::array<float,3>>` of a ranged color reads three numbers.

Built in the core behind `BAKER_AT_METADATA`; the default build runs the fork point's
instructions on every case. Switched on: parse +1.01% (twice the bar, inside D143's
+1.1%), five instructions on every typed read. A Python migrator renamed 379 `.dta` keys,
23 in Markdown and 77 in C++ literals; the migrated repository passes 21 of 21 test
programs after two hand edits. Core +152 as built, about +104 adopted. The payoff in this
corpus is narrow: 4 values become readable whole and 133 fields are freed from position;
the stacks' files grow 333 bytes.

The verifier did not reproduce the note's claim that the typed-read cost hides in noise:
pinned to one core, `find_float` runs +55% (average row) and +65% (last child) with the
switch, D77's warm row, which the brief said must not move. The gate failed 8 files on one
line of real data, `('@' "Game: Win w/ 3 stars…" …)` in Rock Band's `cheats.dta`, a
keyboard cheat bound to the `@` key. Both point the same way as the note.

The tool author scored it 3 (adopt in part, for shapes and `{func}`), the technical
director 2 (explore further), and both note that the recommended variant is the one not
built. The tool author found `split_metadata` now returns fields that follow metadata as
metadata, a layout `write_back_to` itself produces; `(@desc)` on a scenario is ignored and a
misspelt `(@rnage)` in data is silent, the pain the idea set out to fix; the migration
warning misses an old word that is not last; and files grow (+0.3% to +2.5% from renames
alone), the polished samples relying on a column the core cannot print and the first
write-back breaks. The technical director adds the argument the note does not make: one
token marking metadata lets a bake strip cold metadata, about 13.5% of the stacks' bytes.

**Recommendation: explore the spelling, not the boundary.** A second pass that keeps `@`
as the name of metadata where it is read (`split_metadata` returning only `@` children,
the tweak bind, shapes, `check` reporting every bare old word and every unknown `@word`)
and leaves `values()`, the parser and the cache alone; a quoted symbol is never metadata.
Adopt it only if it measures zero on parse, `find_float` and `set`, at about 35 lines.

### 6. column-tables: a keyed table as a sheet

`claude/fervent-cray-l1kyf0-syntax-column-tables`, `dev/syntax-column-tables-design.md`.

`#columns (button startup active ...)` inside a table declares its columns once; each row
is its key and exactly N cells, whatever their form, and reads to the keyed record
`(jab (button light) (startup 4) ...)`. `_` is a hole. The synthesizer had rejected the
idea because a list cell such as `(jab low_kick)` looks like a tagged array; the fixed
count removes that by construction, and the prototype tests it.

```dta
(moves
   #columns   (button  startup active recovery damage hitstun blockstun push cancels        hitbox)
   (jab        light   4       2      7        30     12      9         0.12 (jab low_kick) (0.3 1.3 0.55 0.2))
   (side_kick  kick    7       3      14       60     17      11        0.2  _              (0.35 0.9 0.75 0.25)))
```

The rule works: both headline rows read right, and a short row is located
(`row jab has 7 cells, #columns declares 9 (missing push hitbox)`). But the prototype found
the family of silent misreads is wider than the judges' fatal: a pasted nested row read as
`(k (a a 1) (b b 2))`, and a dropped cell beside trailing children shifted silently; a
guard and a strict rule close both, at the price of trailing children. A translator reads
tables at 9.5 to 15 MB/s, under spec §7's 40 MB/s floor, so the core cost is an estimate.
Measured on the stacks' 11 data files: 510 to 413 lines and 7.4% fewer bytes printed, but
four files grow; Duel's moves halve while their longest line grows from 123 to 142
characters.

Both reviewers scored it 2 and agree with declining. The technical director: the corpus's
keyed tables average 4.6 rows of 3.5 fields; a header repeats for sibling tables of one
schema (which is why Contraption grows 13.5%); and the zero cost on headerless files is
reasoned, not measured. The tool author: a positional cell hides its field from a diff
(`-(turtle 10 0.9 1 0.5 0.8 _ 0.35 8)` names nothing), a value moved into a neighbouring
hole read silently, every write-back that widens a cell drifts the alignment that makes
cells readable, and the agent's hand-off breaks, since promote into a headed file is
refused and Duel's moves are the table an agent tunes.

**Recommendation: decline for now.** An aligning printer for today's keyed tables first;
the strict header (about 120 core lines) only if designers still ask for it after using
the grid.

### 7. indent-lines: indentation as structure

`claude/fervent-cray-l1kyf0-syntax-indent-lines`, `dev/syntax-indent-lines-design.md`.

A line that begins with an atom is an array, and deeper lines are its children, so the
outer parentheses of every line go and the three-space indentation every file keeps by
hand becomes what the parser reads. Brackets stay for groups within a line, commands and
properties, and a line that begins with a bracket reads as today.

```dta
player
   speed 3.5   ; how fast
   lives 3
```

Built in the core three ways (`BAKER_INDENT_LINES` off, always, and opt-in by `#lines`).
Off costs nothing. Always costs +1.67% of every parse and the opt-in +1.53%, above D143's
bar in all four layouts tried. Converting the 68 files removes a third of the parentheses
(4,654 to 3,004) and nearly every closing pile (489 lines to 29) for 2.4% of bytes and two
lines. `promote --patch` on an indented file still gives a one-line diff. The prototype
counted 6 test-suite texts that silently changed meaning and pinned three readers of
value text (tweak defaults, `eval`, console requests) to today's reading. The gate settles
it: always on, 76 of the raw corpus's 426 readable files read to a different tree with no
error, because Harmonix scripts write a bare macro alone on a line.

Both reviewers scored it 2. The technical director ran the raw gate as well, found the
same 76 files and calls it fatal; the per-token test costs every parse whether or not the
file uses the form, and the "free" opt-in the note mentions (a second copy of the parse
loop) is neither built nor costed. The tool author found the common reflow silent: a
wrapped row continued at its key's own indent, which is what an editor's auto-indent
gives, moves to the parent with no error; and every stack would hold two spellings for
good, since scenarios, objectives and every tool's output stay bracketed.

**Recommendation: decline the layout; take the printer's house style.** Indent 3 and width
100 as the printer's defaults, a two-line change with a D-entry, so a block `write_back_to`
inserts matches the file around it.

### 8. inline-layers: a block in a file is a toggleable sub-layer

`claude/fervent-cray-l1kyf0-syntax-inline-layers`, `dev/syntax-inline-layers-design.md`.

`(#layer (handheld (when HANDHELD)) (renderer (shadows 0)))` is lifted out of the tree into
the session, and the Config pushes it directly above its file as the sub-layer
`default:handheld`: enabled by a condition, toggleable, named by `explain`, written back
into by its own extent, reloaded and placed with its file. It is the bracketed, visible
replacement for a token-level `#ifdef`.

Built in the core behind `BAKER_INLINE_LAYERS`, with one rule ("a file and its blocks are
one unit") enforced in four places; merge, snapshots, provenance, `explain`, write-back
and the policies needed nothing else. Parse cost on block-free text −0.05%. Core +221 as
adopted (bar 150), about 319 for the full form. On the raw corpus, 162 of 1,024 conditional
regions (16%) become blocks; the rest are inside commands, nested, hold directives or bare
values. In the repository one file has a conditional and no stack has one.

Both reviewers scored it 2 of 5 and would decline. The technical director refuted the
note's "build time, not run time": a platform condition the bake could settle becomes a
file the bake refuses, and shipping refuses the toggle that is the feature's only payoff.
The tool author found three claims in the note wrong: shipping refuses
`set_layer_enabled`, so the live-ops "flip without a second push" does not work;
`ui_menu.dta`'s real reader (a plain `read_file`) would reject the converted file; and the
Duel A/B replaces host code a project-file stack does not have. `check` never sees a
disabled block, and the printer cannot write one.

**Recommendation: decline.** If conditional platform layers are wanted sooner, a
`(when COND)` flag on the project file's layer entries, beside `optional`, `disabled` and
`dev_only` (D172, D204), estimated at 10 to 15 lines.

## Found on main along the way

Three defects in today's Baker, each found by a prototype and independent of any syntax:

- **The fuzz property fails on every `(key #remove)`.** The edit-mode span check re-reads
  each literal token alone, and `#remove` alone is refused (D143). Found independently by
  four branches. curly-infix's libFuzzer run (ASan and UBSan, the only coverage-guided run
  of the round) stopped on it after 1,331,783 executions, about 40 minutes on this
  machine, and its agent confirmed main's own harness and library refuse `(a #remove)` the
  same way; layer-markers, inline-layers and curly-infix each fixed it on their branch,
  with regression inputs (`remove_marker_span.bin`, `bare_remove_marker.bin`). CI's
  two-minute fuzz job is too short to reach it, and `fuzz/dta.dict` holds neither of
  Baker's own directives, `#require` and `#remove`.
- **A `[property]` argument to a function with one overload is never evaluated.**
  `{% [hp] 3}` in a handler fails. The fix is one comparison in `Args`
  (`include/baker/script.hpp`), on the curly-infix branch.
- **A received layer can widen a pack's shape.** With `data_only`, `scalars_only`,
  `existing_only` and `warnings_refuse` all set, `check_layer_text` passes an overlay that
  declares no pack and restates `(shapes (item (price int (range 0 99999))))` beside
  `(shop (sword (price 5000)))`, where the pack declared `(range 0 1000)`: the shape is
  data, its paths already exist, and its values are scalars, so every rule admits it, and
  D145's narrowing binds only files that declare a pack. Found by hash-declarations
  (`experiments/syntax-hash-declarations/tests.cpp`, "received text: a (shapes ...) loosens
  a pack's shape today", run against main's unchanged core), whose `#shape` form closes it.

Each wants a D-entry and a small fix on main whatever happens to the syntax; the fixes for
the first two exist on the branches named.

## Combinations

- **path-rows and layer-markers are one feature for layers.** A row carries a verb
  (`(/items #append rope)`, `(/menu.debug_menu #remove)`), promote writes a removal from
  either, and the journal and `diff --rows` export rows with their verbs. Taken together
  they are the "a layer file is the diff it will become" idea, and they share promote's
  removal-by-extent, which each built separately.
- **layer-markers' `#was` belongs with at-metadata's spelling.** The pin and its
  `(note ...)` are row metadata; today a `(note ...)` beside a value fails a shape check
  (`'speed' takes one float, given 2 values`). With `@` naming metadata, `(@was 30)` and
  `(@note "...")` ride any row without tripping shapes, which is why layer-markers holds
  `#was`.
- **hash-declarations and at-metadata answer the shape grammar's reserved words twice.**
  One moves `(at ...)` and `(keyed)` into a `#shape` directive; the other spells them
  `(@at ...)` and `(@keyed)`. Adopt one spelling for both, or neither.
- **indent-lines' printer defaults and column-tables' formatter are one tool.** Both
  prototypes conclude that the corpus's hand alignment is real and that a printer laying
  out today's syntax (indent 3, width 100, keyed tables as an aligned grid) gets most of
  the benefit with no grammar.
- **curly-infix's vocabulary and the objectives.** `{<= 0.3 $value 0.7}` is a band in
  today's prefix syntax once comparisons chain; with it the 15 objective bands lose their
  `and` and two-thirds of their brackets before any infix reader exists.

## What the round taught about today's syntax

- **The pains that are real** are the ones three or more readers found and every
  prototype that touched them measured: layer files restate nesting a tool already prints
  flat (a Duel move override is 10 parentheses for one value), value lists cannot be
  composed by layers, metadata is told from values by a closed list of names that collide
  with real fields, and declarations are recognised by position. Each has a branch above
  that relieves it at a measured cost.
- **The pains that are smaller than they look** are the page's: parentheses (a third of
  them could go, for 2.4% of bytes and no lines), column headers (7% of bytes on the
  stacks' data, more on two files, less on four), and `#ifdef` (one file in the repository
  has one).
- **The binding constraints**, in the order they decided things: the core's line budget
  (each adopt-in-part above spends 35 to 250 of the 310 lines left, and together they
  spend about 600); the per-parse bars, D143's +1.1% and the briefs' +0.5% (path-rows and
  curly-infix met the tighter one only after rework, at-metadata missed it at +1.01%,
  indent-lines missed both in all four layouts tried, and the margin moves by a point with
  how GCC lays out a loop);
  silent reinterpretation of real files (the always-on layout and the dotted rows failed
  here, and the in-repo corpus could not show either); and one token per value for
  write-back, which every prototype kept.
- **The in-repo corpus is not enough to judge a lexical change.** Every prototype that
  changed how text reads passed the 68 files; on the 464 raw ones at-metadata failed 8,
  the dotted rows variant reinterpreted 1,097 tags, and the always-on layout changed 76
  trees with no error. D101's gate is run by hand and is not in CI; its four results
  above each settled or changed a recommendation.

## Decisions for the owner

Each is one decision, with the recommended answer first.

1. **Fix the three defects found on main, whatever happens to the syntax?** Recommended:
   yes, each as its own step with a D-entry: the fuzz property's `#remove` case (the fix
   and regression file are on inline-layers), the `[property]` argument (one comparison,
   on curly-infix), and the received-layer shape widening (a policy rule: a received
   layer that declares no pack may not hold `(shapes ...)`, or shapes stop being data, as
   hash-declarations proposes). Alternative: leave them for the syntax work that touches
   each.
2. **The line budget.** The parts recommended above cost about 600 core lines against 310
   left: the curly-infix vocabulary about 125, path-rows about 200, the narrowed layer
   verbs about 150, at-metadata's spelling pass about 35, and hash-declarations' directives
   about 100 if they are ever taken. Recommended: take the vocabulary and promote's
   removal within the budget now, and decide a raise once, when the layer work (rows and
   verbs) has earned it by the comparison in item 3. Alternative: raise the soft budget to
   about 16,000 now (D220's precedent).
3. **Layer syntax: earn it against a host command first.** Recommended: run the stacks'
   agent loop on a few task packs with a rows `agent.dta` and with a host-kit
   `set PATH VALUE --layer agent` over today's write-back (no grammar), and take path-rows
   only if rows win on refused or wrong edits; take `#append` and `#drop` when a package
   that composes an ordered list exists, which would reverse the owner's two declines of an
   append marker (D138 gap 36, D142). Alternative: take rows now on the strength of the
   tool author's 4.
4. **curly-infix: the vocabulary now, the infix reader held.** Recommended: take variadic
   `+ - *`, chained comparisons and `+= -= *= /=` now; hold the reader until `check`
   rejects malformed infix and a trial shows infix beats prefix chains for the people who
   write objectives. Alternative: take the reader now behind the switch (the gate is
   clean).
5. **Declarations in `#`.** Recommended: the parts that need no new syntax now (the
   `(list T N)` arity with `count` in `baker.shapes`, the governs-nothing finding, the
   received-shape policy rule) and the `#`-before-a-letter rule; `#pack`, `#origin` and
   `#shape` only once `diff`, `layer_effect` and the tolerant read can show a declaration.
   Alternative: the directives as one step now, with cache version 7.
6. **One spelling for metadata and for the shape grammar's words.** Recommended: build
   at-metadata's second pass (`@` where metadata is read, no `values()` boundary, a quoted
   symbol never metadata) and decide it together with item 5, since `#shape` and `(@at
   ...)` answer the same reserved words; `#was` follows it. Alternative: keep D88's list
   of seven names.
7. **The page: a printer, not a grammar.** Recommended: indent 3 and width 100 as the
   printer's defaults now (two lines), and an aligning option for keyed tables, outside
   the core beside shape; decline indent-lines' layout and column-tables' header.
   Alternative: build column-tables' strict header behind a switch for designers to try.
8. **Conditional layers.** Recommended: decline in-file blocks; if platform layers are
   wanted, `(when COND)` on the project file's layer entries (10 to 15 lines).
9. **D101's raw gate as a step, not a memory.** Every idea above that changed how text
   reads passed the 68 corpus files, and three failed or changed the 464 raw ones.
   Recommended: `tools/local_matrix.sh` runs `baker_parse_corpus --roundtrip --milo-paths`
   (and the tree comparison) when an environment variable names a local checkout, and
   skips it otherwise; the corpus stays out of the repository and CI (D3). Alternative:
   leave the gate to be run by hand when a lexer change is proposed.
