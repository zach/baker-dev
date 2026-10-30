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
`.claude/skills/gamedev-td/`, and the tool author's), what D101's raw gate says, and what
the owner has to decide.

## At a glance

Numbers are instructions per 16 KB parse of bench_parse's text (base 1,845,499) unless
marked, and core lines from `tools/count_lines.py` (base 15,190 of the 15,500 soft budget,
D220). "Gate" is D101's raw corpus, 464 files, run on each branch's switch-on build
(below). Scores are 1 to 5 from the two reviewers. Each recommendation leads with its
category, strongest first: **Adopt in part** (named parts now, the rest held), **Trial
first** (decided by a stated comparison), **Explore** (another design pass before a
decision), **Decline**. No idea is recommended for adoption whole.

| Rank | Idea | Branch `claude/fervent-cray-l1kyf0-syntax-…` | Parse cost | Core lines | Gate | TD | Tools | Recommendation |
|---:|---|---|---|---|---|---:|---:|---|
| 1 | infix commands, and an operator vocabulary | `curly-infix` | 0 off; +0.56% with the reader | ~125 (the vocabulary) | 464/464; 0 braces fire | 3 | 3 | **Adopt in part.** The vocabulary now; the infix reader after a reading trial |
| 2 | rows keyed by a path, for layers | `path-rows` | +0.47% | ~200 | 464/464; the dotted variant fails | 2 | 4 | **Trial first.** Rows only if they beat a host `set` command; the dotted form out |
| 3 | `#append`, `#drop`, `#was` beside `#remove` | `layer-markers` | −0.18% (check +2.19%) | ~240 as built; ~150 narrowed | 464/464 | 3 | 3 | **Adopt in part.** Promote's removal now; the verbs narrowed, when a package needs them |
| 4 | `#pack`, `#origin`, `#shape`; `#` before a letter | `hash-declarations` | 0 off; −0.37% with the switch | 128 measured; shape +20 | **270/464** refusing `#define N (body)`; 464/464 keeping it | 3 | 3 | **Adopt in part.** The parts that need no syntax now; the directives held; `#define N (body)` kept |
| 5 | an `@` tag is metadata | `at-metadata` | +1.01% with the switch; `find_float` +55% | ~104; ~35 for the spelling alone | **456/464** | 2 | 3 | **Explore.** A spelling-only pass |
| 6 | `#columns` headers over keyed tables | `column-tables` | not built in the core | ~120 (estimated) | not run (no core change) | 2 | 2 | **Decline.** An aligning printer instead |
| 7 | indentation as structure | `indent-lines` | +1.67% always; +1.53% opt-in | ~250 | **76 files read differently** (always) | 2 | 2 | **Decline.** The printer's house style instead |
| 8 | `(#layer ...)` blocks as sub-layers | `inline-layers` | −0.05% | ~229 | 464/464 | 2 | 2 | **Decline.** `(when COND)` on project layers instead |

All eight branches build, pass their tests in their default configuration (23 of 23
suites with experiments on), round-trip the 68 corpus files with no fault, and are pushed; each verifier
rebuilt the branch, reran the tests and reproduced the note's numbers, with the exceptions
listed under each idea. Every CI job (GCC, Clang, MSVC, libc++, TSan, the sanitizers, the
stacks, clang-tidy and the release dry run) passes on every final head, and each branch's
parser, or column-tables' translator, ran 10 million libFuzzer executions on its final code
(below).

## The branches at their final heads

Each branch forks from this one at `3817213` (main at `565bb63` plus the harness); main has
since moved on (the 0.6.0 release and later D-entries), and none of the branches has been
merged anywhere. "Reviewed at" is the head the verifier and the reviewer read. The commits
after it add each design note's section 11 (that review, the verifier's corrections, the
raw gate and the fuzz runs), the work the completion round below did (hash-declarations'
core change, curly-infix's in-tree rewrite and its malformed-infix error, column-tables'
fuzz target), the fixes the fuzz runs and CI called for, and each note's last subsection,
"The completion round". "Core files" counts files changed under `src/` and `include/`.

| Branch | Final head | Commits | Reviewed at | Core files |
|---|---|---:|---|---:|
| `claude/fervent-cray-l1kyf0-syntax-curly-infix` | `56dfa1f` | 21 | `ed037c5` | 10 |
| `claude/fervent-cray-l1kyf0-syntax-path-rows` | `4c98385` | 17 | `d89146f` | 6 |
| `claude/fervent-cray-l1kyf0-syntax-layer-markers` | `cd8f07f` | 14 | `39f5248` | 16 |
| `claude/fervent-cray-l1kyf0-syntax-hash-declarations` | `a80575d` | 21 | `104fcec` | 11 |
| `claude/fervent-cray-l1kyf0-syntax-at-metadata` | `8ff8995` | 19 | `1c73321` | 11 |
| `claude/fervent-cray-l1kyf0-syntax-column-tables` | `8b04008` | 11 | `2f53a8a` | 0 |
| `claude/fervent-cray-l1kyf0-syntax-indent-lines` | `f7994a1` | 14 | `b1a88ae` | 8 |
| `claude/fervent-cray-l1kyf0-syntax-inline-layers` | `59f09e7` | 16 | `85d53e7` | 7 |

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
| layer-markers, inline-layers, hash-declarations with its switch off | 464/464, 0 | |
| hash-declarations, switch on (the core change) | **270/464** | every one of the 194 failures is `#define NAME (body)`, refused as the design proposed: 127 files write one, 67 include one of 15 that do. With that form kept and the rest of the switch on, 464/464 and 0 failures: the form stays |
| curly-infix, switch on | 464/464, 0 | the infix rule fired on **0 braces**, and on the final code refused 0 files as malformed infix: the branch's own condition for adopting it is met |
| path-rows, `/` rows on | 464/464, 0 | 0 tags begin with `/`; 1,097 tags hold `.` or `/` (35 at top level) |
| path-rows, dotted variant | 464/464, **1 failure** | reads those 1,097 dotted tags (`motto.btn`, `tutorials.btn`, …) as rows: the sigil-free form is out |
| at-metadata, switch on | **456/464** | one site: `rb1/ps2/config/cheats.dta:81` binds the keyboard's `@` key as `('@' "Game: Win w/ 3 stars…" {fake_win 2})`, included by 8 files; a located error, not silent. A quoted symbol must never be metadata |
| indent-lines, opt-in `#lines` | 464/464, 0 | trees identical to today on all 426 files that read alone |
| indent-lines, always on | 464/464, 0 | but **76 of 426 files read to a different tree, silently** (51 in `rb1/ps2/tutorial`): Harmonix scripts write a bare macro alone on a line at top level (`START_BG_MUSIC`, `WAIT_FOR_VO`), which the layout reads as an array |

## The eight, in rank order

### 1. curly-infix: the vocabulary now, the infix reader later

**Description.** Baker's commands are written head-first, as in every Lisp and in DTA: the
function comes first and its arguments after it, so a sum is `{+ $a 1}` and a comparison is
`{>= $value 0.3}`. That is regular, but it reads badly in exactly the places where designers
write script. The stacks' objective files test whether a measured value falls in a band, and
today every such band is two comparisons joined by `and`, naming the value twice:
`{and {>= $value 0.3} {<= $value 0.7}}` (15 of the corpus's 54 `pass` objectives are written
this way). A clamp is two nested `if`s, and adding one to a counter is `{set $i {+ $i 1}}`.
The syntax survey (`dev/syntax-survey.md` on the session branch) listed this as its seventh
pain: script is wordier than the data around it and hostile to the people the harness is
meant to have writing it.

The idea attacks the pain in two independent ways. The first is a larger set of operator
functions, which works in today's head-first spelling: if `<=` accepts more than two
arguments and tests every adjacent pair, the band becomes one call, `{<= 0.3 $value 0.7}`.
The second is a change to the reader: inside braces only, a command whose every other
element is one and the same operator may be written with the operator between its
operands, `{0.3 <= $value <= 0.7}`, and the reader rearranges it into the head-first command
`{<= 0.3 $value 0.7}` as the brace closes. Nothing downstream of the reader changes: the
evaluator, `check`, the binary cache, the content digest and the tools all see the same
head-first tree they see today. There is deliberately no precedence table: a brace that
mixes two operators is an error that tells the writer to nest braces, rather than a rule
every reader of the file must remember. The prototype merged two proposals from the survey,
curly-infix itself and expr-precedence (a full C-like expression grammar), and kept only
the first; the full grammar was rejected because every tool would have to carry its
precedence table, its printer would lose the writer's spelling, and it would add a new kind
of silent misreading.

```dta
(pass {and {>= $value 0.3} {<= $value 0.7}})   ; stacks/duel/metrics/duel.dta:31 today
(pass {<= 0.3 $value 0.7})                     ; the new vocabulary alone, head-first
(pass {0.3 <= $value <= 0.7})                  ; the infix reader: the same tree as above
```

Other examples from the corpus, as the prototype's converter rewrote them: the counter
`{set $menu_enter_count {+ $menu_enter_count 1}}` becomes `{$menu_enter_count += 1}`
(tests/corpus/ui_menu.dta:30), and the calculator's clamp
`{if {< $x $lo} $lo {if {> $x $hi} $hi $x}}` becomes
`{{$x < $lo} ? $lo : {{$x > $hi} ? $hi : $x}}` (examples/calculator/functions.dta:5).

**Elements.** The idea is made of these separable pieces. The first group is the infix
reader; the second is the operator vocabulary, which needs no reader change; the last items
are fixes and tools that came with the work.

- **The infix reader rule.** When a brace (a command, or an `#autorun` brace) closes and it
  holds an odd number of nodes, three or more, whose second, fourth and later slots all hold
  the same bare operator symbol (made only of the bytes `+ - * / % < > = ! & | ^ ~`), the
  reader rewrites it as the head-first command it spells: `{a op b op c}` becomes
  `{op a b c}`. Parentheses, the data arrays, are never touched. The rule sits behind a
  compile-time build switch, `BAKER_CURLY_INFIX`: it was off in the default build for the
  prototype and is on by default on the branch since the completion round.
- **The guard.** The rule fires only if at least one operand is a number, a variable, a
  property or a command. A brace of bare symbols, such as `{a - b}` or
  `{wheat -> flour}`, stays an ordinary command, so no old DTA brace written with plain
  words can change meaning.
- **The exemptions.** A quoted symbol in an operator slot (`{a '-' b}`), a macro in an
  operator slot, or a macro that expands to several nodes anywhere in the brace keeps the
  brace head-first. Quoting is how a writer, or the printer, says "this is a call, not
  infix".
- **The mixed-operator error.** A brace whose second slot is an operator but whose other
  operator slots hold a different one, such as `{a < b <= c}`, is a syntax error at the
  brace's line and column: "mixed operators in {...}: nest braces to say which applies
  first". This is what replaces a precedence table.
- **The ternary.** `{c ? a : b}` reads as `{if c a b}` and `{c ? a}` as `{if c a}`. `?` and
  `:` are reserved for it. The `if` is written nowhere in the text, so it has no source
  span, and write-back refuses to replace it.
- **The written-infix mark.** One bit in the array header, `kWrittenInfix`, beside the bit
  D143 added (D143 is the decision that a layer removes a key with `(key #remove)`), records
  that a command was written infix. Equality, the digest and evaluation ignore it; the
  binary cache carries it with no format change.
- **The infix printer.** A command carrying the mark prints back as it was written, so
  promote, `write_file` and the text of a console reply give a designer their own spelling.
  A long one breaks one operand a line with the operator ending each line.
- **The printer's quoting.** A command built head-first that the reader would otherwise take
  as infix, or refuse as malformed, is printed with its operators quoted, `{+ '-' 1}` or
  `{$obj '<=' 1 2}`, so that printing any tree and reading it back always gives the same
  tree.
- **The operator vocabulary.** Changes to the built-in operator functions, unconditional
  (in every build, with or without the switch):
  - **Variadic `+ - *`.** `+` and `*` take two numbers or more and `-` one or more (one
    argument is negation), folding left to right; integers stay integers until a float
    joins. `{+ 1}` stays an error, as it is today.
  - **Chained `< <= > >=`.** Each takes two numbers or more and tests every adjacent pair,
    evaluating each argument once and stopping at the first pair that fails, so
    `{0.3 <= {f} <= 0.7}` calls `f` once.
  - **Two-argument `/ == !=`.** These keep main's two arguments. A chained `==` and a
    variadic `/` were measured and were dearer per call (below), and nothing in the corpus
    chains them; `{a == b == c}` is reported by `check` as "'==' takes 2 arguments,
    given 3".
  - **One overload each.** Each folding or chaining operator becomes a single function
    whose parameters are a typed rest (`{+ number number number...}`), where main had a set
    of two to four overloads for int and float. This is where the speed comes from, and it
    changes some error messages.
- **The typed-rest literal check.** Five lines in the reference checker hold every literal
  passed to a typed rest to its type, so `{+ 1 "x"}` (and `{1 + 2 + "x"}`) are still `check`
  errors once `+` is variadic.
- **The compound assignments `+= -= *= /=`.** Each takes a place (a `$variable`, or a
  `[property]` inside a method, as `set` takes) and a number: it loads the place, applies
  the operator, stores and returns the result. An unset variable is the error "$x is not
  set". `=` stays undefined, so `{$x = 1}` is never an assignment; `check` reports it as a
  call of a missing function.
- **The logical aliases `&& || !`.** Second names for `and`, `or` and `not`, so a condition
  can read `{a && b}`. They were proposed and built (six lines) but nothing needed them.
- **The property-argument fix.** A bug on main that the vocabulary exposed: a function with
  a single overload never evaluated a `[property]` argument, so `{% [hp] 3}` in a handler
  failed with "argument 1 of '%' is property, not int". Argument passing now evaluates a
  command, a property or a variable alike. It is a fix to today's Baker, independent of
  everything else here.
- **The malformed-infix error.** Added in the completion round, after the tool author's
  review. A brace with an even number of nodes whose head is an operand (a number,
  variable, property or command) and whose second slot is an operator or `?` is not a call
  anyone writes, so it is now a syntax error at the brace that names the fix:
  `{0.3 <= $value 0.7}` gives "infix {...} needs '<=' between every two operands", and
  `{$value <=}` gives "infix {...} ends with '<=': write an operand after it". A malformed
  ternary gives "'?' in {...} takes {c ? a} or {c ? a : b}". It is part of the reader and
  lives behind the same switch.
- **The head check.** Also from the completion round: `check` now reports a command whose
  head no call can have, a number, a string or a property, as evaluation already does.
  Before, `check` silently accepted any command whose head was not a symbol, treating it as
  an object computed at run time. This is independent of the spelling: it catches
  `{0.3 ...}` written head-first too.
- **The converter.** The experiment's tool `baker_syntax_curly-infix to-new` rewrites a
  file's operator calls in place by their source spans, keeping every comment and line
  break, and `same` proves a rewritten file against its original. `to-dta` prints any file
  head-first, but loses comments.
- **The gate counters.** `baker_parse_corpus`, the tool that runs D101's raw-corpus gate
  (D101 is the decision that 464 raw Harmonix DTA files, kept outside the repository, must
  all parse and round-trip), now also prints how many braces the rule rewrote, how many the
  guard held back, and how many files the rule refused as malformed infix.

**Build notes.** The reader rule, the guard, the mark, the printer's two halves and the
malformed-infix error are in the core's parser and printer behind `BAKER_CURLY_INFIX`; the
vocabulary, the compound assignments, the aliases, the typed-rest check, the property fix
and the head check are in the core unconditionally. The converter and the comparisons are an
experiment under `experiments/syntax-curly-infix/`, outside the core's line budget.

- *Cost of the vocabulary, per call.* Counted in instructions per evaluation under
  cachegrind (the unit used because the shared machine's wall-clock rows moved by up to 65%
  on unchanged code), against the fork point: `{+ 1 2}` goes from 633 to 450 (-28.9%),
  `{+ 1.5 2}` from 825 to 481, `{< 1 2}` from 634 to 485. Today's band, `and` of two
  comparisons, goes from 1,755 to 1,613; written as one chained call it costs 775, 56% less
  than the band today. `/` and `==`, kept at two arguments, are unchanged (342 to 343, 501
  to 499); made variadic they had cost 478 (+40%) and 616 (+23%), which is why they were
  kept. In wall-clock time (bench_call) the band as one call took 54.75 ns against 132.58 ns
  as three calls, and every row allocated nothing in 1,000 calls; `baker_alloc_tests`
  passes.
- *Cost of the reader, per parse.* Counted in instructions per 16 KB of the parse
  benchmark's text, which holds no operator call, against main's 1,845,499. With the switch
  off, nothing of the rule is compiled and the parse reads 1,843,323 (-0.12%, from folding
  two flags of the parser's frame into one byte). With the switch on it reads 1,855,835
  (+0.56%), about five instructions for every array that closes, paid even by text that
  never uses infix. For comparison, D143's `#remove` marker was accepted at +1.1%. The
  first version of the rule cost +1.26% before it was moved out of the hot loop. Reading a
  brace actually written infix costs about 190 more instructions than reading the same
  command head-first, at load only (+2.18% on a benchmark text with 12,460 operator
  braces). Since the bake stores the expanded tree, a shipping load reads no text and pays
  none of this, and evaluation pays nothing because the tree is the same.
- *Cost in core lines.* `tools/count_lines.py`: the core goes from 15,190 to 15,477 of its
  15,500 soft budget (+287); the malformed-infix error and the head check bring it to
  15,494. Of the +287, the vocabulary in builtins.cpp is +124 (the brief had projected
  +45), and the property fix and `Raw<Var>::is_set` another 11. Adopted without the switch,
  the ternary or `&& || !`, the note projects about +250.
- *Layout.* Node stays 16 bytes, the array header 24 bytes (one bit used, thirteen spare),
  the parser's frame 48 bytes; nothing new allocates.
- *Samples and the converter.* Over the 68 corpus files the converter rewrote 76 of the 210
  commands written at top level or inside data, in 14 files: 99 operator calls written
  infix, 15 bands made one chain, 3 ternaries, 3 assignments, 2 nests of `+` merged. Over
  those 76 commands brackets fall from 324 to 252 (-22%), atoms from 371 to 338 (-9%). The
  eleven sample files lose 1.7% of their bytes (15,293 to 15,034) and no lines (342 to 342).
- *Tests.* With the switch on, all 23 test suites pass, with new cases for the chain and
  its spans, the ternary, the mixed-operator error, the guard, quoting, macros, the printer,
  the cache bit, the folds and chains, `+=` on variables and on `[hp]`, and `check`; the
  experiment has 13 cases (14 after the rewrite). With the switch off, all 21 suites passed
  before the rewrite. Three existing test expectations changed, all error messages from the
  overload sets becoming one function each: for example `check`'s `{+ 1}` was "no overload
  of '+' takes 1 argument" and is now "'+' takes at least 2 arguments, given 1".
- *The raw gate.* Run on the switch-on build over D101's 464 raw Harmonix files: 464 of 464
  parsed, 0 round-trip failures, and the rule fired on 0 braces, with 0 held back by the
  guard. Rerun on the final code with the refusal counter: the same, and 0 files refused as
  malformed infix. The design note had made a firing brace the condition that would stop
  the reader shipping as it is, since it would mean an old file silently changing meaning.
- *The in-tree rewrite (completion round).* The switch went on by default on the branch, and
  the converter rewrote the tree's own files in place: 14 files (the seven stacks' metrics,
  Ledger's milestones and unlocks with the four `economy.xlsx` cells they are exported from,
  the calculator, the scripting example and three test fixtures), 76 of their 385 lines,
  every comment kept on its line. Each was proved against git's copy of its original: the
  same tree, up to the 20 rewrites that rest on the new vocabulary, each of which undoes
  exactly and 19 of which were evaluated against what they replaced (the twentieth, a sum
  of Hearth's event counts, calls game functions, and Hearth's runs agree). The stacks' 30
  tests pass, and their 130 simulation runs, 385 objective verdicts and `check` reports are
  byte for byte as before. With the switch off, these files still parse but no longer mean
  what they say (17 of 21 suites pass).
- *The malformed-infix error and head check (completion round).* No instruction cost on a
  parse: 1,855,835 before and after with the switch on, 1,843,323 with it off. Per brace,
  an even brace costs 9 instructions more and an infix band 7 fewer; on the benchmark text
  these net to zero. In a stack, a malformed band now fails the load: `stacks_duel check`
  exits with the problem at metrics/duel.dta:31:10.
- *Fuzzing.* libFuzzer with the address and undefined-behaviour sanitizers ran 10,000,000
  executions on the final parser, with the reader on and built as CI's fuzz job, seeded with
  the corpus, the regression inputs and every file the rewrite changed: the first 595,779 in
  a smoke run, the rest in runs each resumed from the last one's corpus (a run stopped to
  apply a harness fix, or by one of two machine restarts, resumed from its last logged
  count, so no execution is counted twice). None found a failure. An earlier 2,220,593
  executions, run before the malformed-infix error, are not counted; their one stop was a
  defect in main's fuzz harness, which read D143's `#remove` marker as a lone token; the
  harness was fixed and the input kept as a regression. A second harness fix, found on the
  indent-lines branch, is here too: main's harness skipped a tree over 100,000 expanded
  nodes but read its print back with a 65,536-node limit, so a tree in between, which macro
  sharing builds from a few hundred bytes, failed a check the parser never broke.
- *CI.* Every job passes on the final head, MSVC, libc++, TSan and the stacks included. The
  release dry run had failed since the prototype's first commit, because eleven shipped
  comments, test names and a CMake comment cited the design note in running text, which
  `tools/release/publish.py check` refuses; they were reworded.

**Review.** An agent independent of the prototype rebuilt the branch, reran its tests and
reproduced its numbers. Every instruction count, line count, sample and converter count
reproduced exactly, and the per-call figures reproduced with the verifier's own driver (the
branch does not commit its driver, so that table cannot be rerun from the branch alone). It
corrected three things: the infix-reading row was measured on the first 999,700 bytes of a
5.3 MB text, which the note did not say; the `bench_compare` wall-clock row came from an
earlier commit and was not reproduced; and wall-clock ratios differ within noise.

The technical director's review, under the data-oriented lens of
`.claude/skills/gamedev-td/`, scored it 3 of 5 and recommended adopting in part. Its
central point is that the infix tree is the prefix chain's tree: `{0.3 <= $value <= 0.7}`
and `{<= 0.3 $value 0.7}` read to the same array. So every saving in evaluation cost and in
brackets belongs to the vocabulary, not the reader. Over the 76 rewritten commands, chains
written head-first use the same 252 brackets as the infix spelling and fewer atoms (318
against 338, since infix repeats the operator). What the reader adds on top is reading
order alone, which nobody measured, at the price of most of the core's remaining line
budget, five instructions on every array close, and a second spelling that every text tool
not built on Baker's parser must learn: the tree-sitter grammar, the language server, the
tolerant structure read's `tag` field, and the stacks' agent documentation.

The tool author's review also scored it 3 of 5, adopt in part. It found that `check` passed
malformed infix: `{0.3 <= $value 0.7}` (a missing operator), `{$value <= 0.7 0.3}` and
`{$value <=}` gave no error, because their even node count put them outside the rule and
`check` took a command with a non-symbol head for an object computed at run time. They
would fail only when first evaluated, where the matching typo in head-first spelling is a
`check` error. The completion round answered this with the malformed-infix error and the head
check above, so all three forms now fail the load at the brace.

**Recommendation: adopt in part.** Element by element:

- *Take now, as one step:* the operator vocabulary (variadic `+ - *`, chained
  `< <= > >=`, `/ == !=` left at two arguments, one overload each), the typed-rest literal
  check that keeps `check` as strict as it is today, the compound assignments
  `+= -= *= /=`, the property-argument fix, and the head check. The vocabulary is faster
  than main on every operator it touches and equal on the rest, allocates nothing, and on
  its own turns every band into one head-first call, `{<= 0.3 $value 0.7}`, in today's
  syntax; the compound assignments shorten every counter; the property fix and the head
  check repair defects in today's Baker whatever is decided about spelling. "One step"
  means a change on main with its own entry in `dev/decisions.md` (a D-entry, the record of
  a decision and its measured results), the tests from the branch, the regenerated
  `docs/script-reference.md`, and the updated expectations for the error messages that
  change. The vocabulary is about 124 core lines, and the fixes a few more.
- *Drop:* the logical aliases `&& || !`. They would read well beside infix
  comparisons, but the converter never needed them, and they add three more names to a
  vocabulary that already has `and`, `or` and `not`. Drop the ternary too, in any adoption:
  `{if c a b}` is already one word, and the ternary costs two reserved symbols, a head with
  no source span, a branch in the rule and the printer, and extra atoms (the calculator's
  three commands went from 31 atoms to 33).
- *Hold:* the infix reader, meaning the rule, the guard, the exemptions, the
  mixed-operator error, the written-infix mark, the infix printer and its quoting, and the
  malformed-infix error. Every measurable condition for it is now met: the raw gate fired
  on no brace, malformed infix is an error at the brace that names the fix, and the tree's
  own files are rewritten and proved with every stack's results unchanged. What is left is
  the reviewers' reading trial: whether the people who write objectives read
  `{0.3 <= $value <= 0.7}` better than the head-first chain `{<= 0.3 $value 0.7}`, which is
  the same tree with fewer atoms. The branch's rewritten metrics files are its material; the
  note does not fix the trial's form, and none was run. If the trial favours infix, the
  adopting change owes its own D-entry, the build switch deleted, the tolerant structure
  read and the text tools taught the rule (including the clause for malformed even braces),
  a section of `docs/language.md`, and a rule for the cost the design note names: a file
  written infix means something else to a build without the rule, so live-ops overlays and
  packs for older builds must stay head-first. It would cost +0.56% of every parse and
  about 190 instructions per infix brace at load. If the trial does not favour it, the
  vocabulary alone already gives the bands as head-first chains, and the branch's rewrite
  of the tree reverses with `git revert`.

**Design note.** The full design is in `dev/syntax-curly-infix-design.md`, on this
idea's branch.

### 2. path-rows: a layer file is the diff it will become

**Description.** In Baker, a layer is a file that changes a few values of the data below it:
an agent's `agent.dta`, a person's `local.dta`, a live-operations overlay that a running game
receives, a patch to a shape (the schema data is checked against). Today a layer has to be
written as a nested tree, the same way as the base data it changes. To change one value four
levels down, you write every level as its own array:
`(fighters (kestrel (moves (jab (startup 5)))))` spends 10 parentheses on one number. The
reader has to count brackets to learn which key changes, and a miscounted bracket does not
always fail to parse: it can put the value one level off, silently. Meanwhile every tool
already prints a changed value as a path. The host kit's `diff` command prints
`player.speed  3.5 -> 4.5`, errors print `'fighters/kestrel/health'`, and the console sets
a value with `{config.set a.b v}`.

This idea lets a file use that path spelling as input. A **row** is a parenthesized array at
a file's top level whose first atom is a path beginning with `/`. It reads as the nested tree
the path names: `(/fighters.kestrel.moves.jab.startup 5)` is read exactly as
`(fighters (kestrel (moves (jab (startup 5)))))`. Rows that share a prefix are joined, so
each key still heads one array per level. Below the parser nothing changes: merging,
lookup, provenance (which file and line a value came from), shapes, `check`, the binary
cache and every JSON document see the same nested tree they see today, and a file without
rows reads exactly as it does now. A layer then reads as the diff it will become: one line
per value, each with its own comment and its own line in version control, and a line of
`diff` output pastes back as `(/player.speed 4.5)`. The idea is meant for the files people
and agents write by hand (layers, overlays, agent edits), not for base data.

```dta
; stacks/tools/test_commands.py:124, the agent layer today: 2 lines, 14 parentheses
(fighters (kestrel (health 1100) (walk_back 1.7)))
(cpu (easy (reaction 26)))
; rows: 3 lines, 6 parentheses, each value on its own line with its reason
(/fighters.kestrel.health 1100)      ; survives one more jab string
(/fighters.kestrel.walk_back 1.7)
(/cpu.easy.reaction 26)
```

**Elements.** The idea consists of these pieces; the later parts refer to them by these
names.

- **The row rule.** The parser's test that makes an array a row: a bare (unquoted) atom of
  two or more bytes that begins with `/` and stands in slot 0 (the head) of a parenthesized
  array at the file's top level. It is one test in `Parser::symbol()`; the lexer is
  untouched. A lone `/` is still the division symbol, a quoted `'/x'` is still a symbol,
  and a `/`-led symbol anywhere else (`(path /usr/share/game)`) is a symbol as today.
- **The sigil `/`.** The character that marks a row's key. The proposals that were merged
  into this idea used `@`, but another idea (at-metadata) reserves `@` for metadata, `/` is
  already the separator of Baker's paths and the character errors print them with, and no
  bare atom in the repository's 68 `.dta` files begins with `/` except the lone symbol.
- **The key grammar.** A row's key follows the grammar of Baker's existing `Path` type
  (decision D27, the path syntax): segments separated by `.` or `/`, a `'quoted'` segment
  for a name holding either character (`/menu.'v1.2'.price`), and at most 16 segments. A
  prototype addition: a bare segment must read alone as the symbol it names, so
  `(/a.1e5 1)` must be written `(/a.'1e5' 1)`.
- **The wrap.** When a row closes, the parser builds one wrapper array per segment before
  the last, each a two-node `(segment child)` array made in the load's memory arena (the
  rule of decision D64, one refcount per load) and carrying the row's file and line. No new
  node type, array flag or header field is added.
- **The fold.** When the file closes, and only if it held a row, arrays sharing a tag are
  joined in the order written, at the first one's place, recursively, so each key heads one
  array per level. It is its own function (`Parser::fold`), not Baker's merge, because the
  merge policies either drop a later array's values and removals or apply
  the removals instead of keeping them.
  Without the fold, two rows under one prefix would leave tools that read a tag's first
  child (`layer_effect`, `diff`) blind to the second value.
- **The one duplicate rule.** In a file that holds rows, a leaf written twice is an error
  at the later writing (`'fighters/kestrel/health' is written twice`), and a removal joined
  with anything is an error too. A file without rows keeps today's rule from decision D31
  (how Config layers merge), under which the first of two repeated keys silently wins.
- **The key errors.** Located errors, at the key's first byte, for every malformed key: an
  all-digit segment (a list element, which a row cannot set), an empty segment, a trailing
  separator, more than 16 segments, an unclosed quote or a space in the key, a segment that
  must be quoted, and a row anywhere but the top level.
- **The head-slot quoting.** The printer's mirror of the row rule: it quotes a `/`-led
  symbol of two or more bytes in a parenthesized array's head at any depth, so a symbol
  that is not a row never reads back as one. It shares one code path with the existing rule
  that quotes the `#remove` marker (decision D143, which lets a layer remove a key with
  `(key #remove)`).
- **The rows printer.** A printer option, `WriteOptions::rows`, that writes each top-level
  array as rows, one per leaf. What a row cannot spell (a key with a space or bracket, an
  array whose values follow its children, a repeated tag) is written nested, and so is a
  whole file whose top level repeats a tag: the D31 list files such as the stacks'
  `(scenario ...)` and `(objective ...)` lists, 14 of the 68 corpus files.
- **The one-row renderer.** The small function (`render_row`) that writes a single row; it
  is what write-back uses, and what the proposed host commands would use.
- **Write-back's rows branches.** Write-back is the feature that saves an edited value back
  into the source text (decision D34). With rows, `write_back` replaces a row's value
  token in place and keeps the line's comment; `write_back_to`, which inserts a new value
  into a layer file, appends a new row at the file's end when the file already holds rows,
  and creates an absent layer file as rows. A nested file still nests.
- **The `#row` span marker.** How write-back knows an array was made by a row: the
  wrapper's source span stores the reserved name `#row` in the public field
  `SourceSpan::macro`, which normally names the macro a node was spliced from. A `#`-led
  name can never be a real macro, and SourceSpan's layout does not change.
- **Promote's removal.** Promote copies a layer's values into the base file (decision D134,
  promote writes what the Config reads). Today it refuses to promote a `(key #remove)`;
  this piece makes it delete the key's array from the base, with its line when the key
  stood alone. It works for nested layers just as well, so it does not depend on rows.
- **The `PathKey` token kind.** One new case, `TextTokenKind::PathKey`, in the public enum
  of the tolerant structure read (decision D220, a read of text for tools), marking a row's
  key.
- **The host commands.** Proposed and not built: `diff --rows` and `promote --patch --rows`,
  which would print a diff or a promote patch as rows, and a rows form of the journal
  export. Each would be a few lines in the host kit over the one-row renderer.
- **The dotted variant.** An alternative built behind its own switch,
  `BAKER_PATH_ROWS_DOTTED`: a row's key with no sigil, a bare `fighters.kestrel.health` in
  the head slot.
- **The first-block estimate change.** A change the brief asked for, to count a row key's
  segments in the parser's estimate of how big a tree to reserve (decision D131). It was
  built, measured and taken out (Build notes).
- **The host `set` command (not part of the idea).** The alternative the technical director
  raised: a host-kit command `set PATH VALUE --layer agent` built over the existing
  `cfg.set` and `write_back_to`, which would let an agent use the path spelling with no
  change to the grammar at all.

**Build notes.** Everything was built in the core (`src/`) behind the build switch
`BAKER_PATH_ROWS`, off by default, with a translator and tests in
`experiments/syntax-path-rows/`; the translator (Baker's parser followed by a tree
rewrite) is a test oracle, not a proposal. The base for all measurements is the fork point,
`3817213`.

- **What every parse pays.** The row rule adds a test to every symbol, so a file without
  rows also pays. Counted with cachegrind (exact instruction counts, which a busy machine
  does not disturb) on the 16 KB `parse` case of `tools/count_instructions.py`, the final
  build costs 1,854,169 instructions against the base's 1,845,499: +8,670, or **+0.47%**,
  just under the brief's bar of +0.5%. It took four versions to get there. The first cost
  +3.10% because the new code in `close()`'s path stopped GCC inlining `close()` into the
  parser's main loop; a later version cost +1.38%. On the way, a second call site of a
  vector erase, in the fold, stopped GCC inlining that erase into `truncate()`, which every
  `close()` calls, at +36,000 instructions per 16 KB; removing that call site brought the
  cost to +0.47%. The cost therefore depends on what the inliner does
  with code that never runs. Allocations are 43 a parse on both sides; the wall-clock
  benchmark was noise at the machine's load.
- **The first-block estimate change** cost +5,200 instructions per 16 KB sample on every
  text over 16 KB, rows or not, and saved no allocation on 1 MB of rows, so it was removed.
- **What rows cost.** Reading the same tree as rows rather than nested takes **2.31 times
  the instructions** (on 1 MB of the stacks' data: 212.48 million against 91.85 million)
  and 137 allocations against 26. The extra goes to the fold's grouping sort (26.4
  million), the key scan (21.9 million), the fold and join (19.8 million), and the k arrays
  a row of k segments must build. On a layer-sized file this is about a microsecond (the
  Duel agent layer: 2.3 µs against 1.1 µs); a base file read as rows takes 1.7 to 3.8 times
  its nested read. A cached load costs nothing extra, since the binary cache stores the
  folded, nested tree.
- **Size.** Over the stacks' 11 real data files, rows (as printed) are 11% more lines, 35%
  more bytes and 18% fewer parentheses than the nested printer's output, because every leaf
  repeats its path. This is why the note says rows are for layers and base files stay
  nested.
- **The samples.** A Duel move override goes from 10 parentheses to 2, the agent layer
  above from 14 to 6 (at the price of a line), a shape patch from 14 to 8, and each
  live-ops overlay's data drops a line. A base file of records (`ledger/data/economy.dta`)
  triples in lines and grows 42% in bytes as rows. A key written twice becomes a located
  error; `write_back` replaces one token; `write_back_to` appends a row; promote from a rows
  layer gives a byte-identical `default.dta` to promote from the nested one, and with
  promote's removal it can finally write a removal.
- **Core lines** (`tools/count_lines.py`, against the core's soft budget of 15,500 lines,
  base 15,190). With the switch's scaffolding the prototype adds **+351** (15,541, over the
  budget), 48 of them preprocessor lines. Adopted whole, without the switch, it projects to
  about **+297** (about 15,487): the row rule, key scan, wrap, fold and errors about 165,
  the rows printer about 63, write-back's rows branches about 26, promote's removal about
  38. The recommended part is about **+200**.
- **Tests.** The 68-file corpus round-trips with 0 failures, both through the translator
  and through the parser. The experiment has 17 test cases without the switch, 26 with it,
  and 26 with the dotted variant as well. All 23 of the repository's suites pass by
  default; with the switch, 21 pass unchanged and the other two differ only because a
  layer file that write-back creates is now written as rows, as designed. CI passes on the
  final head; a release dry run had first failed on four code comments that cited the
  design note or a decision number in running text, which the release check refuses, and
  they were reworded (`3d7d5be`).
- **The gates.** In the repository, no parenthesized tag in the 68 files or in the 1,050
  DTA literals of the sources begins with `/`. D101's raw gate (464 raw files from Harmonix
  games, kept outside the repository) reads 464 of 464 with rows on and 0 failures, and no
  tag there begins with `/`. The dotted variant fails it: it reads 464 of 464 but with one
  round-trip failure, and 1,097 tags in that corpus hold a `.` or `/` (35 at top level,
  such as `motto.btn`) and would silently be read as rows. In the repository it would also
  silently turn four of Baker's own `{config.unsaved}` replies, `(box/width phase)`, into
  `(box (width phase))`. So the `/` sigil stays.
- **Fuzzing.** libFuzzer, under the address and undefined-behaviour sanitizers with the
  switch on, after porting main's fix for the harness's `(key #remove)` defect: a first
  run of 1,025,153 executions found nothing. Resumed, it stopped after 1,091,362 more
  (2,116,515 in all) on a real bug in the branch. A row never declares anything, so
  `(/pack ...)` as a file's first form is data tagged `pack`; but the printer wrote it
  nested, and `(pack ...)` as a first form reads back as a pack declaration (decision D80).
  With rows on, the printer now writes such a first form as a row (`662d119`), which also
  lets it print a tree main cannot print faithfully, a data array tagged `origin` that
  would read back as an origin declaration, whenever the top level repeats no tag. Then 10
  million executions on the final parser, the count spec §8 asks for, found no failure in
  the parser. One run stopped after 2.6 million on that same main limitation in the one
  case rows cannot cover, a top level that repeats a tag; the harness now skips exactly
  that tree (`4401579`). A harness fix found on another branch (a tree checked up to
  100,000 nodes but read back with a 65,536-node limit) was ported too (`75ebb95`).

**Review.** An agent independent of the prototype rebuilt the branch at its final head
(`d89146f`), reran the tests and reproduced the line counts, the +0.47%, the allocation
counts, the 1 MB instruction counts (within 0.06%), every sample and the gate counters
exactly. The +297 adoption figure is an estimate, and wall-clock ratios ran somewhat lower
on the loaded machine. It listed four gaps: the host commands are not built; there is no
D-entry (no record in `dev/decisions.md`); `promote --patch` is not tested at the host
level; and the dotted variant has no test of its own. Two reviewers then scored it from 1
to 5.

- **The tool author: 4 of 5, adopt in part.** This lens asks whether the people and agents
  who use the tools gain. A hand rewrite of a Duel layer went from 28 parentheses to 12 and
  read back to the same tree, and write-back, provenance, `check` and promote all hold. Four
  edges must be fixed before adoption:
  1. the host commands the agent loop depends on (`diff --rows`,
     `promote --patch --rows`, the journal export) are not built;
  2. the `#row` span marker is stored in the public `SourceSpan::macro`, whose documented
     meaning ("spliced in from this macro, so not written here") it changes;
  3. a stray row in a D31 list file (scenarios, objectives) switches the whole file to the
     one duplicate rule, so the fold joins its `(scenario ...)` forms and refuses their
     shared keys, and the error names an unrelated line;
  4. `write_back_to` appends a new row at the file's end, away from the rows that share its
     prefix.
- **The technical director: 2 of 5, explore further.** This is the data-oriented lens of a
  console-engine technical director (`.claude/skills/gamedev-td/`), which asks what a
  change costs and whether the problem is real. The cost is measured but the benefit is
  not: the repository's layer files are a few lines (the four overlays hold 22 lines,
  `agent.dta` one), and no bug from a misplaced bracket is on record. The +0.47% is
  measured on GCC alone and moves with the inliner, so other compilers may differ. Whether
  a file holds a row decides its duplicate rule, an action at a distance. And the note
  never weighs the zero-grammar alternative, the host `set` command, which gives the agent
  the path spelling with no parser change.

**Recommendation: trial first.** The decision rests on one comparison, run before any
grammar is taken. Element by element:

- **Hold, pending a trial: the row rule, the key grammar, the wrap, the fold, the one
  duplicate rule, the key errors, the head-slot quoting, write-back's rows branches, the
  `#row` span marker and the `PathKey` token kind.** These make up the grammar, about 200
  core lines without the switch, which is what the branch proposed adopting. The trial:
  run the stacks' agent loop (an agent tuning a stack game's data through layer files and
  judging it with the headless simulation) twice, once with `agent.dta` written as rows and
  once with a host `set` command over today's write-back, and take the grammar only if
  rows win. The reason is the technical director's: the host `set` command gives an agent
  the path spelling for nothing in the parser, so rows have to show a gain it does not.
- **Fix the four edges either way:** build the host commands; move the `#row` marker out
  of `SourceSpan::macro` or redefine that field's documented meaning; make a stray row in a
  list file a clear, correctly located error rather than a silent switch of rules; and have
  `write_back_to` place a new row beside its siblings.
- **Take now, as its own step: promote's removal.** It closes a gap left by decision D143
  (a layer can write `(key #remove)`, but promote refuses to carry it to the base), and a
  nested layer needs it as much as a rows layer, so it stands on its own: about 38 core
  lines, with its own D-entry, the record in `dev/decisions.md` of the decision and its
  measurements.
- **Keep outside the core: the rows printer.** It stays a tool (the experiment's
  converter), because adopting everything would take about 297 of the 310 lines left under
  the budget, and the host kit has only 39 lines left of its own 2,400. If rows are
  adopted, the host commands use the one-row renderer write-back already has.
- **Drop: the dotted variant.** It silently reinterprets 1,097 tags of the raw corpus and
  Baker's own `{config.unsaved}` reply, the kind of silent change in meaning D101's gate
  exists to refuse; the `/` sigil stays.
- **Already done: the first-block estimate change** was dropped on the branch, since it
  cost instructions on every large text and saved no allocation.

If rows win the trial, the design note's next steps follow: a D-entry with these numbers,
the parser hook on main with the branch's tests, a section in `docs/language.md`, and the
stacks-agent skill writing `agent.dta` as rows.

**Design note.** The full design is in `dev/syntax-path-rows-design.md`, on this
idea's branch.

### 3. layer-markers: verbs for layers beside `#remove`

**Description.** A Baker Config reads a stack of layers: a base data file at the bottom, and
above it the files and texts that change it, such as a downloadable content (DLC) package,
a title update, a live-operations overlay received at run time, or the edits an agent makes
while tuning. When the stack is merged, a key a higher layer writes replaces the same key
below. Today a layer has exactly one way to say anything other than "this key now has this
value": D143's marker, `(key #remove)`, which deletes a key from the merged result. Every
other change is a restatement of the whole value. That is a problem for lists. To add `rope`
to Wayfarer's items, a package must write the entire list,
`(items climbing_gloves glider lamp_oil spyglass rope)`. If a second package adds `torch` the
same way, the higher of the two replaces the lower and one of the two items is lost (the
content-updates research's scenario 2). A restated list also brings back every element as
it stood when the package was written, so anything retuned in the base since then, by a
title update or live, is silently undone.

The idea gives layers three more markers, written like `#remove` inside the key's own
array. `#append` adds its operands after the list below, so two packages that each extend
one list compose in stack order. `#drop` removes one matching element, so a patch can take
out one item without restating a list a person may have retuned since. `#was` pins the base
value a row was written against, so that when the base has moved underneath an overlay,
`check` reports a located warning at that row instead of the overlay silently winning.
Alongside the markers, `promote` (the host command that writes a layer's changes down into
a lower file, such as the base) learns to write a removal by deleting the removed key's text
from the target file, where D143's version refused. The owner had declined an append marker
twice before (D138's gap 36, and D142, which made keyed tables the convention for content
packages); this prototype measured that choice against the stacks' real lists.

```dta
; today: each package restates the list, and the higher one wins
(items climbing_gloves glider lamp_oil spyglass rope)      ; dlc_a
(items climbing_gloves glider lamp_oil spyglass torch)     ; dlc_b: the rope is lost
; with the verbs
(items #append rope)                                       ; dlc_a
(items #append torch)                                      ; dlc_b: items gains both
(fighters (kestrel (moves (jab (cancels #append palm)))))  ; one cancel, the list unknown
(cpu (easy (reaction 26 #was 30)))                         ; stale if the base moved
```

**Elements.** The idea is these separable pieces; the later parts refer to them by these
names.

- **`#append`.** `(key #append v...)`: after the merge, the key holds the list below it
  followed by the operands. Operands may be any node, including tagged arrays such as
  Hearth's `(lamp -2 4 0)`, which here are list elements rather than keys. On a key absent
  below, it creates the key from its operands.
- **`#drop`.** `(key #drop v...)`: for each operand, removes the first element below that is
  structurally equal to it (a tuple must be restated byte for byte, `(phone -2 4.6 180)`).
  It can empty a list, which a restated row cannot.
- **The drop warning.** A `#drop` operand that matches nothing drops nothing, and `check`
  warns, located at the row: "'items' has no z to drop". The common cause is a tuple
  retuned below the patch.
- **Two rows of one key.** A row carries one verb, so a package that both drops and adds
  (Hearth's cottage pack drops the phone and adds a lamp) writes two rows of the same key,
  which the merge applies in order. The layer tools now list a composing row even when it
  repeats a tag. Several verbs in one row was the costed alternative (about 15 lines), not
  built.
- **The `#was` pin.** `(key v... #was w...)`: the values before `#was` are what the row
  writes; the values after it are what the row was written against. `check_layer` compares
  each pin with the stack below (`stale_pin`) and gives a warning, a refusal under the
  `warnings_refuse` policy rule, and a `stale` entry in the layer-effect report; a layer
  pushed without a policy sends the same warning to the handler. `write_back` of a pinned
  row keeps the pin, and `write_back_to(path, layer, pin)` writes one. `#remove` may carry
  a pin too. The brief asked for exactly as many pins as values; the prototype takes one or
  more, because an exact count could never pin a list whose length the row changes.
- **The slot rule.** One function, `marker_form_error` in `src/data/markers.hpp`, that both
  the parser and the printer use to decide where a marker may stand: a verb only as the
  first value, `#was` only after one or more values, no marker in `{...}`, `[...]` or an
  untagged array, and so on. Every other placement is a located error. The lexer is
  unchanged: the markers were already `#name` directive tokens, previously "unknown
  directive" errors, so no text that read before reads differently now.
- **The marker flag.** D143's header bit `kHasRemoval` is renamed `kHasMarker` (the same bit,
  ORed up the tree when an array is finished, and kept by the binary cache). The parser sets
  it when it pushes a marker; arrays built in C++ are scanned for a marker in `make_from`
  and `make_copy`. The merge and the layer tools test the flag first (`verb_of`), so data
  that holds no marker pays one flag test.
- **Marker-free merged trees.** The merge strips every verb and pin, so lookups, tweak
  reads, the shape instance check, the digest and the cache never see a marker. The node
  layout, the `Type`s and the cache format do not change.
- **`leaf_values`.** The function every reader of a layer's own (unmerged) tree now goes
  through in place of `values_of`, so that it stops at `#was` and knows a verb row's
  values from its operands.
- **Provenance through composition.** `supplier()` and `source()`, which name the layer a
  value comes from, skip a composing row and name the layer of the literal list it builds
  on. `explain` gains a "composes" standing with the row's operation, file and line;
  `config.effect`, `config.diff` and `config.explain` gain `op` and `stale` entries, inside
  the value, so the console's reply format is unchanged.
- **The composed-path refusal.** `write_back`, `write_back_to` and `promote` refuse, located
  at the composing row, a path that a layer at or above their target composes: "'...jab/
  cancels' is composed by layer 'agent': (cancels #append palm); write the base or promote
  the layer". Writing the composed list there would apply the verb twice. This is the
  hazard the tools judge named when the idea was proposed.
- **Promote's removal by extent.** `promote` writes a `(key #remove)` into the target file by
  deleting the removed key's text (its extent), taking the whole line, trailing `;` comment
  included, when the line holds nothing else. Under D143 it refused. So
  `(menu (debug_menu #remove))` promoted into the base becomes a `-` line for the base's
  `(debug_menu 1)`.
- **The note preamble.** `promote --patch` prints each row's `(note "...")` as `# path: note`
  above the patch's first `---`, where `git apply` ignores it and review tools show it as
  the description. With it, `diff` prints a verb (`-> #append palm`) and a stale pin. These
  host changes are 18 lines.
- **The converter.** `baker_syntax_layer-markers` in `experiments/`, outside the core:
  `to-new` turns a layer that restates lists into verbs against a given base, `to-dta`
  resolves verbs back into plain rows, and `lists` classifies every list in the stacks.
- **Held from the start.** `#scale`, `#add` and `#prepend` were held by the brief and the
  cost judge and are not built.

**Build notes.** This is the only one of the eight prototypes built in the core itself and
always on, with no build switch: the verbs are read by Baker's own parser and applied by its
merge, so no front end could fake them. The branch,
`claude/fervent-cray-l1kyf0-syntax-layer-markers`, has 12 commits to its final head
`ac26376` (reviewed at `39f5248`) and changes 16 files under `src/` and `include/`.

- **Instructions** (cachegrind, per operation, against the fork point `3817213`): parsing
  16 KB of marker-free config text, 1,845,499 to 1,842,230, −0.18%; `check` of bench_layers'
  400-row overlay over its 2,000-item table as a tree, with no marker, 3,486,660 to
  3,562,993, +2.19%; the same check from text (`check_text`), 4,580,012 to 4,666,454,
  +1.89%; `lookup`, `lookup_text`, `set` and `frame` unchanged. Profiling puts the check's
  extra cost in four places: the scan of C++-built arrays in `make_from` (+400k over 20
  checks), `verb_of` not being inlined (+320k), the `stale_pin` call (+240k) and
  `leaf_values` (+80k), with `Array::finish` 165k cheaper.
- **The pinned check misses the brief's bar.** The brief asked that a check with every row
  pinned stay within 2% of the plain check. It costs 4,109,043 instructions against
  3,562,993, +15.3%, about 1,365 instructions a pinned row. About a third is malloc and
  free; the rest is mostly that a pinned row cannot be shared into the merged tree (the way
  D127, the merge fix, shares a row the merge leaves unchanged), because the merged copy
  must lose its pin, so every merge of a pinned layer rebuilds each pinned row. From text,
  the pinned overlay is also 32% more bytes (13,030 against 9,830).
- **Wall clock** (noisy: the machine was shared by eight builds). A merge that appends 100
  values to a 2,000-value list takes 18.2–18.3 µs, against 47.5–48.1 µs to restate the list
  with 2,100; dropping the last 100 takes 413.5–414.2 µs, about 9 times a restatement,
  because `#drop` scans the list once per operand. The tweak-read assembly gate passes
  unchanged, since merged trees hold no marker.
- **Core lines** (`tools/count_lines.py`, against the 15,500 soft budget): 15,190 to
  **15,499**, +309 (`baker::data` +289, `baker::script` +20), the whole headroom, against
  the brief's estimate of about 230. Three things the estimate missed were needed: the drop
  warning, keeping promote from writing a verb row's tagged operands as keys, and listing
  composing rows that repeat a tag. Of the 309, the `#was` pin is about 70 and the verbs
  without it about 240. The host grows from 2,361 to 2,379 of its 2,400.
- **Payoff in the stacks.** The stacks' data holds 26 lists: 8 keyed tables, which already
  compose by key; 15 value lists (cancels, items, blocks, climbable); 1 keyed list with a
  repeated tag (Hearth's farmhouse has two beds and two bookcases); and 2 single values
  holding a list, which no verb reaches. Over the 16 lists a keyed table cannot cover, the
  text a package writes to add one element is 1,428 bytes restated against 840 with
  `#append` (−41%), and to drop the first, 953 against 805 with `#drop` (−16%); every verb
  reads what the restated list meant, 0 misreads. The samples are a Wayfarer DLC pair, a
  Hearth furniture pack and the Duel agent's layer; the Duel layer grows, from 390 to 586
  bytes, because its reasons move from `;` comments into `(note ...)` rows that
  `promote --patch` carries.
- **Tests.** `ctest` 23 of 23; `baker_data_tests` 253 cases, 14 of them for the verbs, with
  script tests for `config.get`, `source`, `explain`, `effect` and `diff`, and host tests for
  `diff` and `promote --patch`, whose output `git apply --check` accepts. All 68 corpus files
  round-trip with 0 faults and none changes, since none holds a marker. clang-tidy is clean
  on the changed files. The local matrix's shipping and EASTL builds were not run; every CI
  job passes on the final head.
- **The raw gate** (D101's real-corpus gate: 464 raw Harmonix script files read and
  round-tripped by `baker_parse_corpus` from a local clone that is not in the repository):
  464 of 464, 0 failures.
- **Fuzzing.** libFuzzer's runtime was missing at first, so a scratch random driver ran the
  parse property over token sequences built from the verbs. In its first 20,000 inputs it
  found a real printer bug: a quoted `'#append'` heading an array printed bare, as if it
  were a tag. It was fixed (a marker is never a tag) and kept as the regression input
  `quoted_marker_as_tag.bin`; after the fix, 1,000,000 inputs over five seeds passed. The
  review round then ran libFuzzer under ASan and UBSan (the address and undefined-behaviour
  sanitizers) for 1,083,088 executions in 25 minutes, with no failure. The completion round
  ran 10,000,000 executions on the final parser (`d5a365c`'s code; the later commits change
  comments and the harness), in runs each resumed from the last one's corpus (a run stopped
  to apply a harness fix, or by one of two machine restarts, resumed from its last logged
  count, so no execution is counted twice), with no failure; the earlier 1,083,088 are not
  counted.
- **Fixes to the fuzz harness.** The branch fixed a defect in main's own fuzz property, which
  re-read each token alone and so failed on every `(key #remove)`; it now skips directive
  tokens. It also carries the fix indent-lines' run found in main's harness, which skipped a
  tree over 100,000 expanded nodes but read its print back with a 65,536-node limit, with
  its regression input `macro_sharing_budget.bin` (`ac26376`).
- **CI.** Every job passes on the final head. The release dry run (`publish.py check`) had
  failed on eleven shipped comments, a test's docstring and a regression note that cited
  the design note or a decision number in running text; they were reworded (`06b1ec0`).

**Review.** An independent verifier rebuilt the branch, reran its tests and reproduced its
line counts, every instruction count, the byte counts, the samples, the 26-list analysis and
the round trip exactly. It made three corrections. First, the Hearth sample overstates the
payoff: its cottage half compares the verbs against a restated list, but the cottage's
things are a keyed table, and `(households (cottage (things (phone #remove) (lamp -2 4 0))))`
already gives the same merged result today (the farmhouse half, with its repeated tags,
stands). Second, the test meant to prove that every reader of a layer tree goes through
`leaf_values` greps the source for `values_of(`, a function that no longer exists anywhere,
so it can never fail and proves nothing. Third, the experiment's `bench <file>` fails on
every stacks file that holds a `#require`.

Two reviewers each scored the idea 3 of 5. The technical director, reviewing through the
data-oriented lens of `.claude/skills/gamedev-td/`, recommended exploring further, for
three reasons:

- **An invariant hole the note misses.** The marker flag is set by the parser, `make_from`
  and `make_copy`, but not by `Array::set`. So an overlay that a C++ tool, such as a
  live-ops tool, builds with `make` and `set` is merged as if it held no marker, and its
  `#append` leaks into the merged tree as an ordinary symbol, breaking the promise that
  merged trees hold no marker (verified).
- **The check's +2.19% is avoidable.** On a marker-free check it is the new per-node scan in
  `make_from`; setting the flag at the point where a marker is made, instead of scanning
  for one afterwards, would remove it.
- **The need is thinner than 16 lists.** 11 of the 15 value lists are Wayfarer cell geometry
  that no package extends.

The tool author (the reviewer judging it as someone who writes the tools and agents that
edit layers) recommended adopting in part, and found three problems:

- **The agent loop loses idempotence.** An agent writes its edits to a layer, `agent.dta`,
  and a person lands them with `promote --patch`. A restated row could be left in place
  after landing and read the same. A verb row cannot: once the patch has landed,
  `(cancels #append palm)` left in `agent.dta` gives `jab low_kick palm palm`, silently
  (verified). The composed-path refusal covers layers between the target and the top, not
  the source layer itself.
- **A verb row cannot carry `#was`**, because the slot rule allows `#was` only after values,
  never after a verb. So the staleness guard never reaches composition, the case where a
  list below has moved.
- **The Duel sample fails Duel's own shape check**: "'reaction' takes one int, given 2
  values", on the sample's pinned and noted `reaction` row. The design note had found the
  same with notes: a `(note ...)` beside a value rides into the merged tree as a child, and
  the shape counts it, which is why the agent skill was left asking for `;` comments.

**Recommendation: adopt in part.** Narrower than the branch built it. Taken element by element:

- **Promote's removal by extent: take now.** It is useful today without any new verb, since
  `(key #remove)` already exists and promote currently refuses to write it. Taking it means
  a step that lands that change on main with its tests and an entry in `dev/decisions.md`,
  the record of each decision (a D-entry).
- **`#append`, `#drop`, the drop warning and two rows of one key: hold until a package that
  composes a list exists.** The stacks ship no DLC, so scenario 2's lost item is a research
  finding rather than a reported bug, and most of the value lists are geometry no package
  extends. When the need is real, rebuild the verbs rather than merging this branch: set
  the marker flag where a marker is made (in `Array::set` too), which closes the invariant
  hole and removes the scan behind the +2.19% so the verbs cost nothing when unused; and
  warn when an appended operand is already present below, which turns the agent loop's
  silent `palm palm` into a located message. The review estimates that version at about
  150 core lines, against about 240 for the verbs as built. With them go the pieces they
  need: `leaf_values` (with a test that really fails when a reader bypasses it), provenance
  through composition, and the composed-path refusal. The design note's section 10 also took
  the note preamble with the verbs; the review's recommendation does not name it, and it
  reads best as held with them.
- **The `#was` pin: hold for at-metadata (idea 5).** The pin is the right idea, since a
  located "was 30; the stack has 32" is the cheapest guard against a stale overlay. But it
  is row metadata, like the `(note ...)` that travels with it. As a marker in the value run
  it costs about 70 lines, a branch in `leaf_values` and every layer reader, and a rebuild
  of every pinned row on every merge (the +15.3%); and it cannot ride a verb row. Spelled
  as at-metadata's `(@was 30)`, a trailing attribute the merged tree may keep, it would cost
  none of that, could sit on any row, and would no longer trip shapes, nor would `(@note
  ...)`.
- **Before any of the verbs ship:** refuse verbs inside `(shapes ...)` (shapes merge as data,
  and the per-layer shape read of D145, the rule that a softer pack may narrow but not
  loosen a harder pack's shape, does not know the verbs), extend `docs/language.md`'s layer
  section and rewrite the cookbook's keyed-tables paragraph, fix `explain` of an element
  path into a composed list (about 4 lines), and run the full local matrix, MSVC included.
- **Not taken:** several verbs in one row (two rows of a key already work), and `#scale`,
  `#add` and `#prepend`, which wait for a measured need; merge-time arithmetic would reopen
  at load the question D183 settled for write-back, whether `(lives 3)` times 1.5 stays an
  integer.

**Design note.** The full design is in `dev/syntax-layer-markers-design.md`, on this
idea's branch.

### 4. hash-declarations: what the reader is told, in the reader's namespace

**Description.** A Baker file says two kinds of thing. Most of it is data for the game:
arrays such as `(fighters (kestrel (health 1000)))` that are merged with the other layers
into one tree. A few forms, though, are addressed to the reader and the tools rather than
to the game, and today they are written as if they were data, recognised only by where they
stand or what they hold:

- A **pack declaration**, `(pack NAME (level L) ...)`, says that a file belongs to a pack (a
  named, ranked unit of content, D82, the decision that introduced packs and the one-way
  rule for macros). The parser treats it as a declaration only when it is the file's first
  form. A second `(pack ...)`, or one written after an `#include`, silently becomes ordinary
  data.
- An **origin declaration**, `(origin "economy.xlsx" (tool sheets-export))`, says which
  source a generated file came from (D91). It counts only when it is first, or first after
  the pack, and only when its first value is a string; `(origin 0 0 0)` stays data purely
  because of that type test.
- **Shapes**, the field types and ranges that `baker::shape` checks data against, are
  declared inside one top-level container, `(shapes (fighter ...) (move ...))`. D84 (the
  decision that shapes are declared in `(shapes ...)`) forced the container because shapes
  stay in the merged tree and merge by tag like data: spec §6's own form, one
  `(shape NAME ...)` per shape at the top level, would have had every shape tagged `shape`,
  each layer's overwriting the last's.

Because these declarations look like data, mistakes in them say nothing. A pack written in
the wrong place is data; a keyed shape written at the wrong depth, `(at fighters moves)` for
`(at fighters * moves)`, governs nothing and checks nothing, which is the failure D108 (the
decision that added keyed shapes) found in Duel; and a received layer's `(shapes ...)` is
data that the layer-checking policy cannot tell from a retuned value (the received-layer
gap, under Elements). The idea moves all three into the `#` namespace that DTA already
reserves for the reader (`#include`, `#define`, `#ifdef`): `#pack (...)`, `#origin (...)`
and `#shape (...)` become directives, each taking one parenthesized form, recorded in the
load's session (the record of a read that already holds packs and origins) and never placed
in the tree. Position sniffing ends: a declaration is recognised by its spelling wherever it
is written, and a second one is an error at its line. The `(shapes ...)` container goes, and
`(pack ...)`, `(origin ...)` and `(shapes ...)` become plain data everywhere.

In the same move the idea tidies the rest of the namespace and adds one feature to the
shape library. `#` would open a directive only when the next byte is a letter, so `#`, `#1`
and `#00aa00` read as ordinary symbols. `[NAME ...]` would become the one way to write a
macro, retiring DTA's older `#define NAME (body)`. And a list type may state how many
elements it holds, `(list float 4)`, so that a hitbox of three numbers is an error. The
prototype merged four proposals from the syntax survey: declare-directives,
one-macro-one-hash, reader-forms and tables-declare-rows.

```dta
(pack boss_tools (level task) (requires combat_core))  ; today: a declaration only as the first form
#pack (boss_tools (level task) (requires combat_core)) ; new: anywhere in the file, once
(hitbox (list float) (desc "x y w h, meters"))        ; today: a 3-float hitbox passes
(hitbox (list float 4) (desc "x y w h, meters"))      ; new: a 3-float hitbox is an error
```

A pack with its shapes, from Duel (`stacks/duel/packs/game/duel_core.dta`), today and in the
new form: the container line goes, each shape loses one indent, and the hitbox says how many
floats it holds.

```dta
(pack duel_core (level game) (owners "gameplay-fight" "td-combat") (stage custom))
(shapes
   (fighter (at fighters) (keyed)
      (health        int   (range 1 100000)) ...)
   (move (at fighters * moves) (keyed)
      (hitbox    (list float) (desc "x y w h, meters, relative to the fighter"))))
```

```dta
#pack (duel_core (level game) (owners "gameplay-fight" "td-combat") (stage custom))
#shape (fighter (at fighters) (keyed)
   (health        int   (range 1 100000)) ...)
#shape (move (at fighters * moves) (keyed)
   (hitbox    (list float 4) (desc "x y w h, meters, relative to the fighter")))
```

**Elements.** The idea is made of these separable pieces. The first group is the
directives themselves; the second tidies the rest of the `#` namespace; the third needs no
new syntax at all; the last are the machinery and the tools that came with the work.

- **The `#pack` directive.** `#pack (NAME (level L) ...)` declares the file's pack, with
  the same children as today's `(pack ...)`, anywhere at a file's top level and once per
  file. It is recorded in the session as packs are today.
- **The `#origin` directive.** `#origin ("SOURCE" (tool T))` declares a generated file's
  origin, anywhere at the top level and once per file; its string must come first.
- **The top-level `#shape` directive.** `#shape (NAME (at ...) (keyed) (FIELD TYPE ...) ...)`
  declares one shape, with the same children as a shape inside today's container. It is
  recorded in the session as a shape declaration (the form, its file and line), never put
  in the tree, and the shape library combines declarations by name, a field declared again
  taking the newer type, with D145's rule between packs (D145: a softer pack may narrow a
  harder pack's shape but not loosen it). One consequence: a layer can no longer remove a
  shape with `(shapes (move #remove))`.
- **The at-the-table `#shape`.** A `#shape` written inside the array it governs, as
  `(fighters #shape (fighter ...) (kestrel ...))`, takes its `(at ...)` place from the
  enclosing tags and is implicitly `(keyed)`, so the designer writes a fighter's shape
  beside the fighters with no path to restate. It cannot say `*` (every fighter's moves), so
  Duel's move shape would stay top-level.
- **The placement errors.** A second `#pack` or `#origin` in a file, a declaration below a
  file's top level or without its `(`, an `#origin` without a string first, and an unknown
  `#foo` are each a located error (an error that names the file, line and column). Today a
  second pack is silently data.
- **The whole-file pack.** Every macro and flag a file defines belongs to its pack for the
  one-way rule (D82's rule that a pack's file using a macro a softer pack defined is
  warned about), wherever
  `#pack` is written. Today a macro defined above `(pack ...)` belongs to no pack, which a
  declaration free to move would turn into a trap.
- **The three migration warnings.** `check` warns once per layer about a first-form
  `(pack ...)`, a first-form `(origin "..." ...)` and a top-level `(shapes ...)` that the new
  reader reads as data, in the reader's words ("a (pack ...) first form, read as data: write
  #pack"), so an old file is never misread silently.
- **The received-shape policy rule.** `LayerPolicy` (D161, the policy a host applies to a
  layer before it takes effect) gains one classification: a `#shape` in received text (an
  overlay, an agent's proposal, taken through `push_layer_text`, D163) is refused under the
  `data_only` and `scalars_only` rules; `#pack` and `#origin` are admitted, since a
  published overlay declares itself with them (D162). In the prototype this was
  `check_received`; in the core change it is in `run_check`.
- **The cache entry.** The binary cache's meta tree carries each shape declaration, and the
  cache format's `kVersion` goes from 6 to 7. Without it a cache load would lose every
  shape, since shapes are no longer in the tree.
- **The session-reading shape library.** `shape::Set::add` reads declarations from the
  load session, per enabled layer, instead of scanning the tree for a `(shapes ...)`
  container, and the host kit's `check` and `reference` commands both read the session
  (today `check` reads the merged root and `reference` reads per layer, so they can
  disagree).
- **The `#`-before-a-letter rule.** One lexer rule: an atom starting with `#` is a directive
  only when its second byte is an ASCII letter. Any other `#`-led atom, `#` alone included,
  is an ordinary symbol, and the printer prints it bare. Today any `#`-led atom of two or
  more bytes is a directive, so `#1` is "unknown directive '#1'", and a lone `#` reads bare
  but prints quoted (D101's lone-`#` mismatch). D101, the real-corpus gate, counts 59
  converted community files that fail on such a symbol.
- **The one macro form.** `[NAME body]` as the only way to write a macro, with
  `#define NAME (body)`, which DTA writes and which is the grammar's one layout-sensitive
  rule (the `(` must be on the name's line, D11), refused with a located error naming the
  fix. `#define NAME` alone stays a flag.
- **The list arity.** A list type may give a count, `(list T N)` for exactly N elements or
  `(list T N M)` for N to M, checked with a located error ("'hitbox' holds 3 values, not
  4"), with D145's narrowing rule extended to counts. It works on today's `(shapes ...)`
  and needs no other part. 11 of the corpus's 16 list fields have a fixed or bounded count.
  Its companion is **`count` in `baker.shapes`**: the reference JSON that tools read
  (format `baker.shapes/1.2`) carrying the count, so an editor can build a vector field's
  widget; the design called for it, but the verifier found `write_json` does not yet emit
  it.
- **The governs-nothing finding.** A `check` finding, "shape 'x' governs no array", for a
  shape whose `(at ...)` place matches nothing in the stack's data. It catches D108's
  wrong-depth failure in today's syntax, and is silent over all six stacks' real data.
- **The migrator and the migrate-check.** `baker_syntax_hash-declarations to-new FILE`
  rewrites only the forms that change, keeping every comment and blank line; `migrate-check`
  reads every corpus file migrated and compares it with Baker's read of the original (the
  tree, the value digest of D222, and the session's packs, origins and macro definitions).
  The completion round added `core-record` and `core-check`, which do the same comparison
  through the core change rather than the translator.
- **The translator.** The prototype's first form: a front end that rewrites the new
  spelling into today's DTA before Baker reads it, used before the core change existed. It
  is a cost of the prototype, not of the design.
- **Declarations in `diff`, `layer_effect` and the tolerant read (not built).** Open
  question 8 of the design note: listing a layer's declarations, as entries such as
  `#shape NAME`, in `diff`, in `layer_effect` (a layer's effect as reported before it is
  staged) and in `explain`, and teaching the tolerant structure read (`read_structure`,
  D220, the error-tolerant read editors use) to show a declaration as one. Without it,
  moving shapes out of the tree takes shape changes out of every review tool.
- **Proposals considered and set aside in the design.** A tree-shaped conditional,
  `#(ifdef N (then ...) (else ...))`, from reader-forms (rejected: it loses the
  bracket-spanning `#ifdef` real DTA uses); `(map T)` and named tuple components (not now:
  `(desc "x y w h")` names the components); and `#require` by pack name (a later step).

**Build notes.** The prototype first built the translator, the migrator and the arity: the
translator and its tests are an experiment (`experiments/syntax-hash-declarations/`, 2,091
counted lines) outside the core's line budget, and the arity is a real feature in
`baker::shape`, whose line count rose from 547 to 567 of its 600-line budget (+19 in
`shape.cpp`, +1 in the header). The core was left untouched at first, and its addition was
estimated at about +100 lines. The completion round then wrote the core change itself
behind a CMake build switch, `BAKER_HASH_DECLARATIONS`, off by default: the `#`-letter rule,
the three directives (top-level `#shape` only; the core refuses the at-the-table form with a
located error), the placement errors, the whole-file pack, the refusal of
`#define NAME (body)`, the cache entry, the session-reading shape library, the three
migration warnings and the received-shape policy rule.

- *Cost in core lines.* `tools/count_lines.py` against the fork point, `3c460d9`: the core
  goes from 15,190 to 15,388 of its 15,500 soft budget (+198), counting both sides of every
  `#if` and the `#if` lines. With the switch's scaffolding removed, as adopting it would,
  the change is +128 lines, against the estimate of about 100: the parser took +69 against
  +40 (the placement errors and the whole-file pack were not in the estimate), the cache
  +15 against +30, and the warnings went to `reference.cpp` (+23), where `check` lives.
  `baker::shape` goes from 567 to 589 of 600 (+22, or +1 without the switch), the host kit
  from 2,361 to 2,372 of 2,400 (+11, or +3).
- *Cost in instructions, the core change.* Counted under cachegrind with
  `tools/count_instructions.py` (instructions per operation, Release build), because the
  machine was shared and wall-clock rows were noisy. With the switch off every case reads
  exactly as at the fork point (a parse is 1,845,499 instructions either way). With it on,
  a parse is 1,838,626, 0.37% fewer, because `close()` no longer tests every closing array
  for a first-form declaration; lexing is unchanged, since the letter test is reached only
  on `#` atoms. Lookups and frames are unchanged; a `set` is 631 against 623 (+1.28%), all
  of it inside glibc's `memcmp`, whose path depends on its operands' addresses.
- *Cost of taking shapes out of the tree.* Measured on the six stacks before the core
  change, by parsing each pack's `(shapes ...)` into a body nothing expands, which is what
  an adopted `#shape` costs. It is a wash: a reload is 1.4% to 2.4% more instructions, a
  layer toggle 5.0% to 7.5% fewer, and pushing and removing a text layer 4.1% to 7.4% more,
  the last two on operations under 5 microseconds. The merge saves only 295 to 451
  instructions, because it appends an unchanged subtree by reference, and the session's
  extra record costs about as much wherever a session is copied.
- *Cost of the translator.* The prototype's translator is a second lexing pass: reading
  the generated 1 MB text through it took 1.424 times Baker's instructions at first. The
  note gave 1.476 at its final commit; the verifier found 1.527 at the reviewed head
  (149.7 million instructions, 51.9 million of them the translator's, about 52 a byte).
  None of this is the core change's cost.
- *Samples and the migrator.* The migrator rewrote 18 of the 68 corpus files: 11 packs,
  2 origins, 12 `(shapes ...)` containers into 30 `#shape` directives, and 2 `#define`
  bodies. Each reads back with the same tree, value digest and session. The six packs go
  from 179 lines and 10,645 bytes to 173 and 10,322 (3.0% fewer bytes); the whole migration
  is +183 -195 lines, +56 -68 ignoring whitespace. Through the core, `core-check` matches
  today's read on all 68 files (0 tree, 0 digest and 0 session mismatches, 52
  shape-declaration records); 365 entries differ only in their line number, by one, because
  the migrator removes a container's `(shapes` line. A Duel written with its shapes at their
  tables is five lines longer, four of them a comment explaining the form.
- *Tests.* Without the switch, `ctest` passes all 23 tests; the experiment's suite has 21
  cases and 1,110 assertions, clean under the address and undefined-behaviour sanitizers
  (ASan and UBSan). With the switch on, the data suite has 248 cases (9 new, covering every
  form and error, conditionals, the whole-file pack, an include's declarations, the cache
  round trip and the received-text refusal) and the shape suite 12 (one new: a disabled
  layer's `#shape` does not hold). `ctest` then passes 18 of 23; the five failures read
  repository files still in today's spelling, by design (`tests/corpus/macros.dta`'s
  `#define kMaxPlayers (4)`, a host test stack's `(shapes ...)`, the live-ops overlays'
  `(pack ...)` and `(origin ...)`, and two host examples whose new warning reaches stderr).
  `tools/local_matrix.sh` was not run on the shared machine.
- *The received-layer gap.* The work found a gap on main. A received overlay that declares
  no pack can send `(shapes (item (price int (range 0 99999))))` with
  `(shop (sword (price 5000)))`: the policy sees two numbers retuned, the validator reads
  the overlay's range, and the 5000 passes where it alone would be refused against the
  pack's range of 0 to 1000. This holds under `data_only`, `scalars_only`, `existing_only`
  and `warnings_refuse` without an `allow` list of paths. The received-shape policy rule
  closes it; so would a policy rule in today's syntax.
- *The raw gate.* D101's gate (464 raw Harmonix DTA files, kept outside the repository and
  read with `baker_parse_corpus --roundtrip --milo-paths`) passed 464 of 464 with the switch
  off. With it on, rerun by the orchestrator, **270 of 464 parse**, and every one of the 194
  failures is the same message, `#define NAME defines a flag; a macro is written [NAME ...]`:
  127 files write such a body themselves and 67 include one of 15 files that do. No file
  fails on the `#`-letter rule. With `#define NAME (body)` kept and the rest of the switch
  on (a one-line change, measured and reverted), 464 of 464 parse with 0 round-trip
  failures. That settled the note's open question 6: the old macro form stays.
- *Fuzzing.* libFuzzer under ASan and UBSan, built as CI's fuzz job with the switch on and
  seeded with the corpus, the regression inputs and the new-form samples, ran 10,000,000
  executions on the final parser (from `b42f2ff`; later commits change only comments, the
  note and the harness): the first 519,956 in a smoke run, the rest in runs each resumed
  from the last one's corpus (a run stopped to apply a harness fix, or by one of two machine
  restarts, resumed from its last logged count, so no execution is counted twice). None
  found a failure. An earlier run stopped after 109,286 executions on a defect in main's
  fuzz harness, not in Baker: the harness read D143's `(key #remove)` marker as a lone
  token, though it reads only in a key's one-value slot. The harness was fixed and the input
  kept as a regression. A second harness fix, found on the indent-lines branch, is here too
  (`cc5f483`): main's harness skipped a tree over 100,000 expanded nodes but read its print
  back with a 65,536-node limit.
- *CI.* Every job passes on the final head. The release dry run (`publish.py check`) had
  refused four shipped comments that cited the design note in running text; `ef9480d`
  reworded them. CI builds only the default configuration, never the switch.

**Review.** An agent independent of the prototype rebuilt the branch at `104fcec`, reran
its tests and reproduced its numbers: the build, 23 of 23 tests, the 21 experiment and 11
shape cases under the sanitizers, the corpus check, the migrate-check, the line budgets,
every sample and the migration diff all matched exactly. It made two corrections. The
translator's instruction counts were stale: at that head the ratio is 1.527, not 1.476, and
a count that moves is work, not code layout; this touches only the prototype's translator,
not the core estimate. And `write_json` does not emit the new `count`, so the reference JSON
drops the arity.

The technical director's review, under the data-oriented lens of
`.claude/skills/gamedev-td/`, scored it 3 of 5 and recommended adopting in part. Its point
is that most of the payoff does not need the new syntax. The list arity is built on today's
`(shapes ...)`. D108's wrong-depth failure is closed by the governs-nothing finding, which
needs no directive. The received-layer gap can be closed by a policy rule over today's
forms. What remains for the namespace move is failures the repository's 68 files do not
have: no file has a second pack, a declaration after data, or a `#`-led symbol, and at
review time it cost an estimated 100 lines not yet written (the completion round since
wrote them, at 128).

The tool author's review also scored it 3 of 5, adopt in part, for two reasons. First,
taking shapes out of the tree takes shape changes out of `diff`, `layer_effect` and
`explain`, the tools a person or a staging step uses to see what a layer does; today a
layer that loosens a shape shows up there as a changed value, but after the change an
agent's layer could loosen a shape and no report would show it. Second, the tolerant
structure read, which editors use and which sees only brackets, would show
`#shape (fighter ...)` inside `(fighters ...)` as a fighter named `fighter`.

**Recommendation: adopt in part.** The parts that need no new syntax. Element by element:

- *Take now:* the list arity, with `count` added to `baker.shapes` (the verifier found the
  JSON writer does not emit it yet), because it is built, tested, catches real errors in 11
  of 16 list fields and stands alone; the governs-nothing finding, which closes D108's
  wrong-depth failure in today's syntax; a policy rule for received shapes, which closes the
  received-layer gap over today's `(shapes ...)` without waiting for `#shape`; and the
  `#`-before-a-letter rule, which is a couple of lines in the lexer and the printer, fixes
  D101's lone-`#` mismatch, lets the 59 community files with such symbols read, and costs the
  raw gate nothing. Taking them means a change on main with its own entry in
  `dev/decisions.md` (a D-entry, the record of a decision and its measured results), the
  branch's tests, and the docs updated.
- *Hold:* the `#pack`, `#origin` and top-level `#shape` directives, together with what only
  they need (the placement errors, the whole-file pack, the three migration warnings, the
  cache entry and `kVersion` 7, the session-reading shape library, and the received-shape
  policy rule in its `#shape` form), until `diff`, `layer_effect` and the tolerant read can
  show a declaration. That is the tool author's condition: without it, a shape change
  becomes invisible to review. The core change is written and measured for that day: 128
  core lines once the switch is removed, and a parse 0.37% fewer instructions. Adopting it
  then would mean its own design note and D-entry, the measurements retaken on a quiet
  machine (`bench_compare.py` on `bench_parse`, `count_instructions.py`'s parse case,
  `bench_cache`), the switch deleted, and the migration of the repository's files as its own
  commit.
- *Keep, and so drop from the proposal:* the refusal of `#define NAME (body)`. The raw gate
  requires the old form (refusing it fails 194 of the 464 raw files; keeping it, all 464
  read), so `#define NAME (body)` stays as DTA's heritage macro form beside `[NAME ...]`,
  which Baker's own files and docs use; `check` may suggest `[NAME ...]` for Baker's files.
- *Defer:* the at-the-table `#shape`. It reads well, but it puts vocabulary in
  `default.dta`, where agents write and which no pack's review covers, and it cannot say
  `*`; the governs-nothing finding answers the same wrong-depth mistake.
- *Drop:* the tree-shaped conditional, as the design note itself recommends; and leave
  `(map T)`, named tuple components and `#require` by pack name for later. The migrator and
  the translator are tools of the trial, not parts to adopt; the migrator moves to `tools/`
  only when the directives are taken.

**Design note.** The full design is in `dev/syntax-hash-declarations-design.md`, on this
idea's branch.

### 5. at-metadata: an `@` tag is never a key

**Description.** In Baker a value can carry metadata: the range a tuning panel's slider
allows, the step it moves by, a description shown as a tooltip, the kind of control to
draw. Today that metadata is written as ordinary tagged children after the value, as in
`(hit_pause 6 (range 0 30) (desc "Frames both fighters freeze"))`, and a reader tells it
apart from data by where it sits and by a list of reserved words. A bound tweak (a C++
variable tied to a value in the data) treats a trailing run of children tagged with one of
seven words (`range`, `step`, `desc`, `note`, `kind`, `group`, `one_of`) as metadata and
everything before it as the value; that is D88, the decision that a tweak's value stops
before its metadata. D218 later exported the same rule as the public function
`split_metadata`, because tools were copying it from the documentation and getting it
different. The shape grammar (the language for declaring what a record looks like) reserves
eleven words of its own, such as `at`, `keyed`, `optional`, `kind` and `desc`.

That arrangement has five concrete faults, which the idea set out to remove:

- Only a tweak sees metadata as metadata. Any other reader takes it as part of the value, so
  `get<std::array<float, 3>>("cube.color")` of `(color 0.9 0.5 0.2 (range 0 1))` fails: it
  finds five children, not three numbers.
- A record's field named `kind`, `desc`, `step` or `at` is read as metadata when it happens
  to be the last child. The corpus has 133 such fields, kept apart from metadata only by
  their position or by which code reads them.
- A misspelt `(rnage 0 1)` is not an error. It is silently a field named `rnage`.
- A shape cannot describe itself, because inside a shape `desc` is the name of a field.
- A `{func}` script function whose body begins with a form like `(status 1)` loses that
  form, because the function's metadata scan takes it for its `(status ...)` marker.

The idea: metadata is marked by its spelling, not by its position. A tagged child whose tag
begins with `@` is metadata, and nothing else is. No new token is needed (the lexer already
reads `@range` as an ordinary symbol, and no bare atom in the 68 corpus files begins with
`@`), and no new kind of tree node: `(@range 0 30)` is an array whose first child is a
symbol, as before. Before and after:

```
(hit_pause 6 (range 0 30) (desc "Frames both fighters freeze on contact"))
(move (at fighters * moves) (keyed) (startup int (range 1 60) (kind frames)) ...)
(ball (kind ball) (pos -6 6.5))
```
```
(hit_pause 6 (@range 0 30) (@desc "Frames both fighters freeze on contact"))
(move (@at fighters * moves) (@keyed) (startup int (@range 1 60) (@kind frames)) ...)
(ball (kind ball) (pos -6 6.5))
```

The last line is unchanged, and now unambiguous: `(ball (kind ball) (pos -6 6.5))` is two
fields in either order, and a field may be named `kind`, `desc`, `at` or `keyed`. A misspelt
`(@rnage 0 1)` on a shape or a field becomes an error instead of silence. In the strong form
that was built, every reader, not only a tweak, sees a value end at its first `@` child, so
`get<std::array<float, 3>>` of a ranged color reads three numbers.

**Elements.** The idea as prototyped is made of these separable pieces:

- **The `@` spelling.** The rule itself: a tagged child whose tag symbol begins with `@` is
  metadata, and no other child is. In the prototype a quoted symbol counts too: `('@x' 1)`
  interns the same symbol as `(@x 1)`, so it is metadata, and there is no way to write a
  field whose name begins with `@`.
- **The intern-time bit.** When the symbol table stores a symbol whose text begins with `@`,
  it sets the high bit of the symbol's stored size, so `Symbol::is_meta()` is one load of a
  word the symbol's text lookup already reads.
- **The boundary.** Each array records, in 14 previously spare bits of its 16-bit header
  flags, the index of its first metadata child (0 for none). It is written in `finish()`,
  the pass that already visits every child when an array is completed; `set()` moves it
  when a replaced slot changes it; sorted mode (D28, which reorders an array's children for
  faster lookup) recomputes it; the binary cache stores it with the array. Past child 16,383
  the parser refuses metadata with a limit error.
- **The strong `values()`.** `Array::values()`, the call every typed read (`find<T>`,
  `get<T>`, `Values`) goes through, returns the children after the tag up to the boundary:
  one compare and a subspan. Two new calls go with it: `metadata()`, the children from the
  boundary on, and `meta(tag)`, one metadata child by tag. This is what makes a ranged color
  readable as three floats by any reader.
- **The placement rule.** Metadata comes after the value: in a parenthesized array, no plain
  value may follow the first metadata child, though fields may come before, between and
  after metadata. The parser checks it once per closed array, from the boundary.
- **The located placement error.** Breaking the placement rule is a syntax error at the
  offending value's line and column: `(x (@range 0 1) 5)` gives "metadata comes after the
  value: move (@range 0 1) after 5". Finding the column re-lexes the array's text, on the
  error path only.
- **Retiring the word lists.** D88's seven-word list in `tweak.cpp`, and the function
  `without_metadata` that applies it at every tweak bind, go; so do the shape grammar's
  reserved words as bare names.
- **Sigiled shape words.** The shape grammar's keywords take the sigil (`@at @keyed
  @optional @one_of @range @step @kind @desc @note`). A shape may carry its own `(@desc ...)`
  and `(@note ...)`, a field may be named `at`, `keyed`, `kind` or `desc`, and an unknown
  `@word`, or an unsigiled old word where a field's metadata goes, is a located error where
  today unknown words are ignored. Storing and printing a shape's own `(@desc)` was not
  built.
- **`{func}` metadata.** A script function's leading forms are spelled `(@status stub)` and
  `(@doc "...")`, so `(status 1)` as the first form of a body stays in the body, and an
  unknown `@word` among the leading forms is an error. The demand report (which lists stub
  functions) reads `(@status stub)`.
- **The migration warning.** `check` (Baker's data check) warns once per layer when one of
  the seven old words sits bare directly after a value: "(range ...) directly after a value
  reads as a field; if it is metadata, write (@range ...)". It exists to catch a file the
  migration did not reach.
- **The migrator.** `tools/migrate_dta.py`, 362 lines of Python outside the core: it inserts
  `@` and changes nothing else, keeping comments, layout and macros as written. It handles
  `.dta` files, Markdown code blocks and C++ string literals, and sends the cases it cannot
  decide to a review list for a person.
- **Metadata paths.** A path reaches a metadata child by its sigiled tag, `box.width.@note`
  or `cube.color.@desc`, so the paths in the `baker.reply` and `baker.check` JSON change
  spelling (open question 3 recommends a minor version bump of those formats, no alias).
- **The cache version.** The prototype stores the boundary in the binary cache without
  bumping its format version, `kVersion`; open question 7 recommends bumping it to 7, so an
  older cache cannot read differently from its text.
- **The metadata column.** What survives of the merged margin-bars idea: a printer option
  that puts the first metadata child of consecutive one-line siblings in one column. It is
  whitespace only, built in the translator (`align_meta`), not in the core, where it would
  be about 15 lines (`WriteOptions::align_meta`).
- **The bare `@flag` desugar** (not built). A bare `@optional` in value position read as
  `(@optional)`. Open question 1 recommends no: it saves 98 bytes over the corpus and makes
  a token in value position change meaning.
- **The spelling-only pass** (the recommended variant, not built). Keep the `@` spelling
  where metadata is read, and leave `values()`, the parser and the cache as they are: no
  boundary, no strong `values()`, no placement rule or located error, no intern-time bit
  (`is_meta()` reads the text's first byte). `split_metadata` and the tweak bind take the
  trailing run of `@` children with one token test instead of seven symbol compares, and
  `check`, not the parser, reports metadata before a value. Estimated at about +35 core
  lines, about +15 once the migration warning is removed.
- **Baking out cold metadata** (the technical director's argument, not built). Because one
  token marks metadata, a bake (the build step that turns text into the shipped binary
  cache) could strip metadata the running game never reads; in the stacks that is about
  13.5% of the bytes.

**Build notes.** Everything up to the metadata column is built in the core behind the build
switch `-DBAKER_AT_METADATA=ON`, off by default; the metadata column, the round trip, the
benchmarks and the tests are in the translator under `experiments/syntax-at-metadata/`, and
the migrator is `tools/migrate_dta.py`. The prototype itself is 865 lines of C++ (the note
said 861; the verifier's count) plus the migrator, none of it counted in the core.

- **Default build.** With the switch off, every case of `baker_count` (the fixed workloads
  counted in CPU instructions under cachegrind) runs exactly the same number of instructions
  as the branch's fork point, and the full suite passes, 23 of 23 test programs. That took
  four fixes to the code inherited from the earlier prototyper, whose first version had
  changed the default build's code.
- **Parse cost, switch on.** Instructions per parse of 16 KB of config text with no
  metadata: 1,845,498 at the fork point, 1,864,145 with the switch, **+1.01%**. The brief's
  bar was +0.5%, so this is twice the bar; it is inside the +1.1% that D143 (the
  `(key #remove)` marker that lets a layer delete a key) was allowed to cost the parse. On
  text with `(@range)`, `(@step)` and `(@desc)` on every value the parse costs +1.87%.
  Callgrind places the extra cost in `finish()` (about 12 instructions an array, finding and
  writing the boundary), the parser's inline check of the placement rule (about 6 an array)
  and the symbol table's mask of the size bit. Two earlier versions cost +3.22% and +2.11%.
- **Read cost, switch on.** `find_float` on the last of 20 children (D77's lookup row; D77
  is the tag run, the per-array list of child tags that makes lookup fast) goes from 203 to
  208 instructions, **five instructions on every typed read**, for `values()`'s compare.
  `Config::set` on its fast path goes from 623 to 634 (+1.8%); a quiet frame of the host kit
  is unchanged at 197. The note judged the five instructions to be inside this machine's
  wall-clock noise; the verifier did not confirm that (see Review).
- **Tweak binding gets cheaper.** Binding 1,000 tweaks that each carry `(@range ...)` and
  `(@desc ...)` costs 1.9% fewer instructions with the switch (25,053,211 to 24,584,509 in
  `TweakBase::bind`), because `without_metadata`'s seven-word scan is gone; 0.217 to 0.186 ms
  at best on the wall clock. A tuning panel's per-tweak reads of range, step, kind and desc
  are unchanged, 31.3 and 31.5 ns.
- **Core lines.** By `tools/count_lines.py` (the core's soft budget is 15,500 lines, with 310
  left at the fork point), the core goes from 15,190 to 15,342 as built, **+152**, because
  the switch keeps both spellings; adopted with the old paths deleted, the estimate is
  **about +104**, a third of the remaining headroom and twice the brief's estimate of +50.
  About 80 of those lines are the boundary, its upkeep and the parse-time error. The shape
  library, counted apart, grows +22 (about +18 adopted).
- **Memory.** None changed: a node is still 16 bytes, an array header 24, a symbol entry 24,
  and `baker_alloc_tests` (which fails if a load reaches `operator new`) passes with the
  switch on.
- **Migration.** Run over this repository, the migrator renamed 379 keys in 23 of the 68
  `.dta` corpus files (one record sent for review), 23 in Markdown (19 forms listed for a
  person, which are fields and must stay) and 77 in C++ string literals (4 listed). A copy of
  the repository migrated this way and built with the switch on passes 19 of 21 test
  programs, and 21 of 21 after two hand edits of expected output the tool cannot reach (a
  CMake regex and a test message). Unmigrated, the switch-on build fails 8 of 23 test
  programs, every failure an old metadata word read as a field.
- **Text size and payoff.** The migration adds one byte per key: the stacks' 39 files grow
  333 bytes (+0.58%), the 68 corpus files 379 bytes (+0.55%), no line count changes. In this
  corpus only 4 values become readable whole (the three-number colors of the cube stack,
  the ImGui harness example and the config example); the other 161 values with metadata
  have one number, which a scalar read already takes today. 133 fields are freed from
  position. Seven samples, the migrator's renames plus the metadata column and hand polish,
  sit beside their originals.
- **Tests.** The corpus round trip (68 files: read, migrate, read with the rule, strip the
  sigils, compare) has no failures with the switch off or on. The experiment's 16 test cases
  pass (413 assertions with the switch on), covering the color read, both field orders,
  the located error, layers and `(@range #remove)`, sorted mode, `set()`, write-back, paths,
  `{func}`, the cache and the 16,383 limit.
- **CI, in the completion round.** Every job passes on the final head. Five Clang jobs
  (C++20, C++23, libc++, ASan+UBSan, TSan) had failed to build since the first core commit:
  two helpers, `is_meta_child` and `unmigrated_metadata`, are used only when the switch is
  on, so with it off Clang reports each as an unneeded internal declaration, an error under
  CI's warnings-as-errors. Both are now marked `[[maybe_unused]]` (`5151c5f`, `e690d2c`), and
  a Clang Debug build configured as CI's builds whole with 21 of 21 tests passing. The
  release dry run had failed on two lines citing this note; they were reworded.
- **Fuzzing.** First, before the completion round: libFuzzer under ASan and UBSan with the
  switch on, seeded with the corpus, the fuzz regressions and the samples, 1,075,950
  executions in 25 minutes, 2,028 edges covered, no failure; short of the 10 million that
  spec §8 asks of an adopted change. In the completion round, 10,000,000 executions on the
  final parser, in runs each resumed from the last one's corpus (a run stopped to apply a
  harness fix, or by one of two machine restarts, resumed from its last logged count, so no
  execution is counted twice), found no failure; the earlier 1,075,950 are not counted in
  that total.
- **A harness fix from another branch.** main's fuzz harness skipped a tree over 100,000
  expanded nodes but read its printed form back with a 65,536-node limit; the indent-lines
  branch's run found it, and that fix and its regression input `macro_sharing_budget.bin`
  were ported here too (`42ad1af`), as was main's earlier harness fix for `(key #remove)`.

**Review.** An agent independent of the prototype rebuilt the branch at its final head
(`1c73321`) and reran its tests and measurements. Every instruction count, the core line
counts, the samples, the migration counts and the 21-of-21 proof reproduced exactly. It
corrected three things. First, the typed-read cost is not noise: pinned to one CPU core and
with the builds interleaved, the fork point and the switch-off build agree, and the switch
makes `find_float` **+55% slower on the average row and +65% on the last child**, the warm
lookup row of D77, which the brief said must not move. Second, `bench_parse` is about 4%
slower with the switch, at best and at median. Third, the migration is not committed on the
branch, so building it with the switch on fails 8 of 23 tests.

D101's raw gate was also run on the switch-on build. The gate parses 464 raw Harmonix game
files, round-trips them and checks their paths, from a local clone of the public
mariteaux/milo-script-library kept outside the repository (D3: no Harmonix files in the
repository or CI). It passed **456 of 464**. All 8 failures come from one line of real data,
`rb1/ps2/config/cheats.dta:81` in Rock Band, `('@' "Game: Win w/ 3 stars, Meta: Win Event
w/ 2 Star Avg" {fake_win 2})`: a keyboard cheat bound to the `@` key, written as a quoted
symbol, included by 8 files. The `@` spelling read it as metadata and the placement rule
rejected the array. The error is located, not silent, but the lesson is that a quoted symbol
must never be metadata. Both corrections point the same way as the note's own conclusion
that the boundary is the costly part.

Two reviewers scored the idea from 1 to 5. The tool author (someone who writes editors and
tools over Baker's data) gave **3, adopt in part**, for shapes and `{func}`, and found these
problems:

- `split_metadata` now returns fields that follow metadata as if they were metadata. Under
  the placement rule a field may come after metadata, and `write_back_to`, which appends a
  new field after an array's last child, produces exactly that layout itself.
- `(@desc)` on a scenario is ignored (scenarios and objectives deliberately keep their plain
  `(desc ...)` field), and a misspelt `(@rnage)` in data, outside a shape, is still silent:
  the very pain the idea set out to fix.
- The migration warning in `check` only looks at the old word directly after a value, so it
  misses an old word that is not the last of the trailing children.
- Files grow, 0.3% to 2.5% per sample from the renames alone. The polished samples rely on
  the metadata column, which the core's printer cannot produce, so the first write-back
  breaks their alignment.

The technical director (the data-oriented console-engine lens of
`.claude/skills/gamedev-td/`) gave **2, explore further**: no misread exists in the corpus
to fix; the boundary costs about 1% on the parse and the reproduced `find_float` row; and
the variant the note recommends, the spelling-only pass, is not the one built. Both
reviewers made that last point. The technical director also added an argument the note does
not make, baking out cold metadata: one token marking metadata lets a bake strip it, about
13.5% of the stacks' bytes.

**Recommendation: explore.** The spelling, not the boundary. Exploring here means one more
design pass on this branch, measured, before anything is decided; nothing is adopted now.
Element by element:

- **Hold the `@` spelling, built as the spelling-only pass.** Keep `@` as the name of
  metadata where metadata is actually read: `split_metadata` returning only `@` children
  (which also fixes the tool author's first finding), the tweak bind, the sigiled shape
  words, `{func}` metadata, and the migration warning extended so `check` reports every bare
  old word and every unknown `@word`, which answers the misspelling and missed-word findings.
  The condition for adopting it: it measures zero added instructions on the parse,
  `find_float` and `set`, at about 35 core lines; and it is decided together with
  hash-declarations' `#shape` directive (idea 4), as the review round
  recommends. Adopting would mean a D-entry (an entry in `dev/decisions.md` recording the
  decision), the migrator run over the repository in the same commit, and the fuzzing that
  spec §8 asks for.
- **Change the quoted-symbol rule.** A quoted symbol is never metadata, so the Rock Band
  cheat line `('@' ...)` would read as it does today; the raw gate has not been rerun with
  that rule.
- **Drop the boundary, the strong `values()`, the placement rule and its located error, and
  the intern-time bit.** They are about 80 of the roughly 104 adopted lines, the +1.01%
  parse cost and the five instructions (+55% on the wall clock) on every typed read, and
  they buy readable whole values for four colors in this corpus.
- **Hold the cache version and metadata paths** to the spelling-only pass: with no boundary
  stored, the cache format needs no change; paths still change spelling to `.@note`, with
  the minor version bump of open question 3.
- **Hold the metadata column** as a printer option for a later step; it is whitespace only,
  but samples that depend on it lose their alignment at the first write-back until the
  core's printer can produce it.
- **Drop the bare `@flag` desugar**, as open question 1 recommends.
- **Note baking out cold metadata** as a further payoff the spelling enables, to be weighed
  when the pass is decided.

If neither the spelling-only pass nor the full change is worth a migration of 479 sites and
a look that departs from the slide syntax Baker's metadata was first taught in, decline: the
corpus works today.

**Design note.** The full design is in `dev/syntax-at-metadata-design.md`, on this
idea's branch.

### 6. column-tables: a keyed table as a sheet

**Description.** A keyed table is Baker's convention for any list of records that packages
extend (decision D142, which made keyed tables the content convention): an array whose
children are records, each tagged by its key, each field written as a tagged array of its
own. Today every row of such a table writes every field name again. Duel's five moves
restate `button startup active recovery damage hitstun blockstun push` five times
(`stacks/duel/data/default.dta`), and the Ledger exporter, which reads a spreadsheet,
writes `(base_price 2) (demand 300) ...` on every line of a sheet that was a grid to begin
with, up to 135 characters wide (`stacks/ledger/data/economy.dta`). The repeated names are
text a person must read past, they can be misspelled (a misspelled field is today only a
shape warning, not an error), and they make lines too long to review.

This idea lets a table declare its fields once, in a header, and write each record as one
row of a sheet: `#columns (button startup active ...)` inside the table names the columns,
and each row after it is its key followed by exactly one cell per column, whatever form the
cell takes. The reader turns each row back into the nested record every reader sees today,
so under that header `(jab light 4 ...)` is read as the tree
`(jab (button light) (startup 4) ...)`. A cell written `_` is a hole: that row has no such
field. The header is a directive, consumed by the reader and never stored in the tree, so
merging, layers, provenance, shapes, `check`, the binary cache and the JSON documents see
exactly what they see now. Outside a headed table nothing changes.

The idea had been rejected once, when the proposals were combined, because of a fatal
flaw a judge found in its first grammar: a list cell such as `(jab low_kick)` (the moves a
jab can cancel into) looks exactly like a tagged array, and the first grammar's rule,
"cells end at the first tagged array", read it as a field and so read the headline tables
wrong without any error. The judges named a fix: a row has a fixed number of cells, so a
parenthesized node in a cell's position is always a cell. This prototype exists to test
that fix.

```dta
; today: every field named on every row
(moves
   (jab       (button light) (startup 4) (active 2) (recovery 7) (damage 30)
              (hitstun 12) (blockstun 9) (push 0.12) (cancels jab low_kick)
              (hitbox 0.3 1.3 0.55 0.2))
   ...)
; with a header: one line a move, the names written once
(moves
   #columns   (button  startup active recovery damage hitstun blockstun push cancels        hitbox)
   (jab        light   4       2      7        30     12      9         0.12 (jab low_kick) (0.3 1.3 0.55 0.2))
   (side_kick  kick    7       3      14       60     17      11        0.2  _              (0.35 0.9 0.75 0.25)))
```

**Elements.** The idea consists of these pieces; the later parts refer to them by these
names.

- **The header.** The directive `#columns (NAME ...)`, written directly inside a
  parenthesized array (which makes that array a *table*), with its list opening on the
  directive's own line. Each name is a bare or quoted symbol and is never macro-expanded.
  Nothing new is lexed: an unknown directive is an error today (decision D11, directive
  details), so no existing file can hold one. Misplaced or malformed headers are located
  errors (at a file's top level, inside `{}` or `[]`, with no list, a name that is not a
  symbol, `_` as a name, a name given twice, an empty list). A second header in the same
  table replaces the first for the rows after it.
- **The row.** Every child of a table after its header is a row: a parenthesized array whose
  first node is a symbol, its key, which stays the record's tag. Anything else where a row
  belongs (a number, a command, a row keyed by a number) is a located error. Directives
  between rows (`#ifdef`, `#define`, `#include`) pass through, and so does `(key #remove)`,
  a layer's removal of a key (decision D143, the `#remove` marker).
- **The cell forms.** With N columns, the N nodes after the key are the cells, as written.
  An atom (number, symbol, string, variable) is the column's one value; a parenthesized list
  is the column's values, whatever its first element, so `(jab low_kick)` becomes
  `(cancels jab low_kick)`; `()` is the column present with no values; a `{command}` or a
  property is one value. A macro is one cell however many values it expands to.
- **The hole, `_`.** A cell written `_` adds nothing: the row has no such field. The symbol
  `_` as a value is written quoted, `'_'`. `_` is reserved only in a cell position, and no
  file of the repository's 68 `.dta` files holds a bare `_`.
- **The fixed-count rule and the short-row error.** A row must have at least N cells, and a
  row with fewer is an error that names the row and the missing columns:
  `row jab has 7 cells, #columns declares 9 (missing push hitbox)`. This is the fix the
  judges named.
- **The guard.** A list cell whose first element is the bare name of one of the header's
  columns is an error. Without it, a record pasted in today's nested form under a header,
  `(k (a 1) (b 2))`, reads silently as `(k (a a 1) (b b 2))`, since each tagged field is,
  by the fixed-count rule, a list cell. A list that genuinely starts with a column's name
  quotes it. No list cell in the corpus needs that, so the guard costs existing files
  nothing.
- **The brief's rule (trailing children).** One of two rules built for what may follow the
  N cells (the switch is `ReadOptions::trailing_children`), and the prototype's default:
  nodes after the cells are the row's ordinary nested children, such as
  `(crouching 1) (guard low)`, so optional fields can stay off the grid. A child named like
  a column is an error. Its flaw, found by the prototype: in a row that has children, a
  dropped cell pulls the first child into the last column, with no error.
- **The strict rule.** The other rule: a row is its key and exactly N cells, and anything
  more is an error naming the first extra node. Every dropped or extra cell is then a count
  error; the only positional mistake it cannot see is two cells of one type swapped
  (`4 2` for `2 4`), which a shape's ranges catch only when they differ. Its price is that
  every optional field becomes a column that every other row must fill with a hole.
- **The translator.** The prototype itself, in `experiments/syntax-column-tables/`: a text
  rewrite that uses `read_structure` (Baker's tolerant tokenizer for tools, decision D220)
  to find each header, row and cell, inserts `(NAME ` and `)` around each cell, blanks the
  header and the holes, and hands the nested text to Baker's ordinary reader. It lexes all
  text twice. Text without `#columns` goes to the reader untouched.
- **Write-back into headed files.** Baker's write-back (decision D34) edits a value in its
  source file. The prototype's `HeadedFiles` re-provides it for headed files: a cell is one
  token, so retuning it replaces that token. For `write_back_to` inserting a value that is
  not there, it fills the hole if the row lacks the column, writes a headed row with holes,
  `(uppercut _ 5 _ _)`, if the table lacks the row, and for a field that is not a column
  either writes it after the cells (brief's rule) or refuses, naming the header to change
  (strict rule).
- **Promote refused.** `promote` writes many entries into one file in one text (how an
  agent's tuning is handed back into the base data); the translator refuses it for a headed
  file. Only a core implementation, which records each cell's own span, could support it.
- **The heading printer.** The experiment's own printer, which writes a tree as headed text.
  It heads a candidate table only when every field is flat (holds no array), every row
  agrees on one column order, cells fill at least 60% of the grid, and every row fits 96
  columns. Baker's own printer is unchanged and never writes a header.
- **The core implementation (projected, not built).** The parser would gain `#columns` in
  its directive handling and a column run and cell count in the frame it keeps per array,
  and each cell would open an implicit `(NAME` array as its token arrives, so no text is
  rewritten and nothing is lexed twice.
- **Layer overrides stay nested.** A layer that changes one cell writes today's nested
  form, `(cpu (easy (reaction 26)))`, not a headed row with holes, which would repeat the
  header and make the writer count to the cell.
- **Shapes stay as they are.** A shape's field puts its type as a positional value before
  tagged children (`(startup int (range 1 60) ...)`), which a row's cells cannot express, so
  shapes are not written as tables.
- **The aligning printer (the alternative, not built).** An option of Baker's printer that
  pads a keyed table's fields into aligned columns in today's syntax: most of the grid a
  designer reads, with no grammar change and no positional cells. The objections in the
  design note name it the cheaper first step.

**Build notes.** Nothing was built in the core: the branch adds only the translator,
`experiments/syntax-column-tables/`, a front end over Baker's public headers (`syntax.cpp`
1,207 lines, `syntax.hpp` 158, `main.cpp` 354, `tests.cpp` 701), with both rules after the
cells, the guard, every error located by file, line and column, `HeadedFiles`, the heading
printer, a command-line tool (`to-new`, `to-dta [--strict]`, `desugar`, `check`, `stats`,
`measure`, `bench` and others) and five samples. Its 34 test cases pass, and pass again
under AddressSanitizer and UndefinedBehaviorSanitizer (ASan and UBSan).

- **The rule works on the headline cases.** Both rows from the rejection read to the right
  tree: Duel's jab row (with `(jab low_kick)` read as `(cancels jab low_kick)`) and the
  economy's mill row (with `(wheat wheat -> flour)` read as `(recipe ...)` and the command as
  `(unlock {...})`). A short row is the located error shown above. All 68 corpus files
  round-trip (Baker's reader, the heading printer, the translator's reader, the same tree)
  under both rules with no failure.
- **Two more silent misreads found.** Besides the judges' fatal, the prototype found that a
  pasted nested row read as `(k (a a 1) (b b 2))`, and that a dropped cell beside trailing
  children shifted the later values silently. The guard closes the first and the strict
  rule the second, at the price of trailing children.
- **Speed (translator).** Measured on 4 MB texts, best of 15 interleaved reads, on a shared
  machine: the translator reads table-heavy text at 9.5 to 15 MB/s, below spec §7's floor of
  40 MB/s for the reader. In instructions (cachegrind, exact) it costs 2.5 times Baker's
  reader on the stacks' data and 3.34 times on generated tables; in time, 3.5 to 7.2 times
  in the note's runs. Without headers it costs +0.12% instructions (one scan for `#columns`).
  It cannot be how the syntax ships.
- **Speed (core, estimated).** A core read of headed text was estimated from the lexer's
  counts, as Baker's read of the nested text less the lexing a header removes: 0.67 of the
  nested read's instructions on generated tables (49.5% fewer tokens) and 0.91 on the stacks'
  data (15.3% fewer tokens). It is an estimate, not a measurement, and leaves out the cell
  loop's own cost.
- **Core lines.** About 110 to 130 lines against the core's soft budget of 15,500 lines
  (decisions D69 and D220; 15,190 used, 310 free), more than the proposal's ~90: the
  directive and its errors (~30), the frame's column run and cell count (~5), the cells,
  holes, count errors, guard and strict rule (~50), per-cell source-map spans (~10), the
  binary cache's tree-size estimate counting cells (~5), and write-back's two headed
  insertion cases (~15). No node layout or allocation changes.
- **Size.** The stacks' 11 data files printed by one printer, nested and then headed: 510
  to 413 lines and 17,687 to 16,377 bytes (7.4% fewer). Duel's file drops from 127 to 38
  lines and the economy from 34 to 16, but six files gain a line or more and four grow in
  bytes (Contraption, Ledger's default, two of Wayfarer's), since padding cells and writing
  holes costs more than the names saved in small, sparse tables. In the five hand-polished
  samples, code lines go from 232 to 236 and code bytes fall 4.4%; Contraption grows 13.5%
  in bytes. The economy's longest line falls from 135 characters to 105; Duel's rises from
  123 to 142, because a ten-column header aligned over its cells is wider than any record
  was. Of the corpus's 42 keyed-table candidates, the heading printer heads 20.
- **Robustness test.** Every prefix of the five samples and 2,000 random three-byte
  mutations of each, under both rules (about 50,000 reads), read or fail with a located
  error under ASan and UBSan. Its first run found a crash in the prototype's own file reader
  on a directory path, which was fixed.
- **Raw gate.** Not run. The raw gate is the test of decision D101: Baker's reader over 464
  raw Harmonix `.dta` files from a community archive kept outside the repository, which
  must round-trip. The branch changes no core code, and the translator only reads headed
  text, which those files do not hold.
- **Fuzzing (completion round).** The translator got a libFuzzer (coverage-guided fuzzing)
  target of its own, `experiments/syntax-column-tables/fuzz.cpp`, holding the core fuzz
  harness's property for this form under both row rules: every read is either a located
  error or a tree that prints and reads back to itself. It ran under ASan and UBSan, with
  `#columns` and `_` added to the fuzzing dictionary. It found three bugs, each fixed with a
  test: the printer could begin a file with a symbol whose first bytes are a byte order
  mark, which a reader skips (it now adds a leading space, as the core printer does); a
  quoted column name could hold a line break, which the rewrite copies into every row, so
  every later line moved and an error was located past the end of the text (the reader now
  refuses such a name and the printer heads no such table); and a table of keys alone, too
  wide for nested text, was headed with no columns, which the reader refuses (a layout with
  no column is now rejected). Then 10,000,000 executions on the final translator, in two
  runs (the second resumed after a machine restart), found nothing. The round also carries a
  fix to main's fuzz harness, found by another idea's (indent-lines') run: it skipped a tree
  over 100,000 expanded nodes but read the print back with a 65,536-node limit. Every CI job
  passes on the final head.

**Review.** An agent independent of the prototype rebuilt the branch at its final head,
reran its tests (23 of 23 tests passed, and the 34 experiment cases also under ASan and
UBSan) and reproduced its numbers: every instruction count within 0.16% and every corpus
count exactly. It corrected four things. The lexer row of the instruction table measured
the printer's 3,973,706 bytes, not the 4,194,408 its label says. On its own runs the time
ratios read about 3.3 to 5.7 times, not 3.5 to 7.2 (the translator still under the 40 MB/s
floor). The note recommends the strict rule, but the prototype defaults to the brief's
rule, and the Duel sample needs it: `to-dta --strict` refuses it with "row low_kick has 12
cells, #columns declares 10". And nothing warns on a second `#columns` in a table, and the
Ledger exporter was not changed to write headers.

Two reviewers then scored the idea, each out of 5. The technical director, reviewing with
the data-oriented lens of `.claude/skills/gamedev-td/` (Mike Acton's school of console
engine practice), scored it 2 and agreed with declining. The corpus's keyed tables are
small, averaging 4.6 rows of 3.5 fields, so there is little repetition for a header to
remove. A header must be repeated for each sibling table of one schema, which is why
Contraption's file grows 13.5%. And the claim that a file without headers costs nothing
in a core implementation is reasoned (one frame flag tested per array), not measured.

The tool author, judging the idea as someone who writes tools over Baker's text, also
scored it 2 and agreed with declining. A positional cell hides its field from a diff:
`-(turtle 10 0.9 1 0.5 0.8 _ 0.35 8)` names no field that changed. A value moved by one
place into a neighbouring hole reads silently as the wrong field. Every write-back that
widens a cell (say 9 to 10) breaks the alignment that makes the cells readable. And the
agent's hand-off breaks, since promote into a headed file is refused and Duel's moves are
exactly the table an agent tunes.

**Recommendation: decline.** For now. The fixed-count rule does what the judges asked, but
the payoff on Baker's real files is narrow and the positional cells carry risks a person or
an agent must manage. Element by element:

- **The aligning printer: take next, as its own step.** It gives designers most of the grid
  with no grammar change and no positional risk. It was not built here; taking it would
  mean a decision recorded in `dev/decisions.md` (a D-entry) and a change to Baker's printer.
- **The header, the row, the cell forms, the hole `_`, the fixed-count rule with its
  short-row error, the guard and the strict rule: hold together.** Build them only if
  designers still ask for the header after using the aligned grid. That step would be the
  core implementation described above, about 120 lines in the parser behind a CMake build
  switch, held to `tools/count_instructions.py` against the parse benchmark
  (`bench_parse`) to confirm that files without headers cost nothing and headed files cost
  less than their nested form. The strict rule is the one to build, because it makes every
  dropped or extra cell an error; the guard comes with it, because it closes the
  nested-row misread at no cost to the corpus; `_` is the hole, reserved only in a cell.
- **Write-back into headed files: hold with the header,** with its rules as the note
  recommends: fill a hole, write a headed row with holes for a missing row, and refuse a
  field that is not a column, naming the header to change. Promote would then work, since
  the core records each cell's span.
- **The brief's rule (trailing children): drop.** It keeps optional fields off the grid but
  lets a dropped cell shift values silently, which is the family of misread the idea set out
  to remove.
- **The translator: drop as a shipping path.** It reads headed text at 9.5 to 15 MB/s,
  under the reader's 40 MB/s floor, and refuses promote. It stays on the branch as the
  record of the design.
- **The heading printer: drop as part of Baker.** Baker's own printer, promote, results and
  replies stay nested; only a converter would write headers.
- **Layer overrides stay nested, and shapes stay as they are: keep as decided.** A layer
  changes a cell in the nested form (or a path row, if path-rows is taken), and the shape
  grammar is a separate matter.

**Design note.** The full design is in `dev/syntax-column-tables-design.md`, on this
idea's branch.

### 7. indent-lines: indentation as structure

**Description.** Every Baker data file today states its structure twice. The parentheses
say which array holds which, and the indentation says the same thing again for a person.
The corpus keeps that indentation carefully by hand: all 160 first children of a top-level
form in the repository's 68 `.dta` files sit at three spaces. But the parser ignores it and
reads only the brackets, so a block's end is a pile of closing parentheses that a designer
counts by eye: 489 of the corpus's 1,485 lines end in two or more closers, 106 in three or
more, and those piles are also where merge conflicts land. Appending a child to a block
today also means editing the old last line, because its closers have to move.

This idea makes the indentation the one statement of structure, and checks it. In a text
read this way, a line that begins with an atom (a number, string, symbol or `$variable`) is
an array: its tokens are the array's nodes, and the lines indented deeper beneath it are
its children. The outer parentheses of most lines therefore go. Brackets stay wherever
they mean something within a line: groups such as tuple rows and `(range 0 10)`,
`{commands}`, `[properties]`, and any node that spans lines. A line that begins with a
bracket reads exactly as it does today, so a bracketed file keeps working. The result reads
to the same tree as today's text, through the same `Array` and `Node` types and the same
memory arena; nothing new is added to the tree or to the binary cache.

```dta
; today
(player
   (speed 3.5)   ; how fast
   (lives 3))

; as lines: the same tree
player
   speed 3.5   ; how fast
   lives 3
```

A shape's fields become a table, for example (`stacks/duel/packs/game/duel_core.dta`):

```dta
shapes
   fighter (at fighters) (keyed)
      health        int   (range 1 100000)
      width         float (range 0.2 3) (kind meters)
```

Some forms do not change. A line that already holds a value beyond its tag cannot take
child lines, so a named instance such as `objective duel.no_infinites` keeps its
properties as bracketed rows beneath it; every objective, scenario and simulation file in
the corpus stays bracketed (272 arrays).

**Elements.** The idea consists of these pieces; the later parts refer to them by these
names.

- **The layout rule.** The parser's rule that turns indentation into arrays. It is decided
  only for a token that is first on its line, and only while no explicit bracket is open
  (inside `(`, `{` or `[` indentation means nothing, as today). The token's indent (its
  byte count from the line's start) closes every open line indented at least as deep; an
  atom then opens a new line at its indent, holding the rest of its line's nodes and every
  line opened deeper. A bracket opens no line: its group is a node of the enclosing line.
  The end of the file closes every line.
- **The per-token test.** The run-time price of the layout rule: the lexer stores where
  each line begins (one store per newline), and the parser tests each token for "first on
  its line, outside brackets" before deciding anything. This test runs on every token of
  every text the layout could apply to, whether or not the text uses the form.
- **Directives and conditionals in the layout.** `#ifdef`, `#ifndef`, `#else` and `#endif`
  take no part in the layout, so they may sit at the margin inside a block, as corpus files
  already write them. Other directives (`#include`, `#merge`, `#define`, `#autorun`,
  `#remove`) do what they do today; their indent closes lines like a token's, so
  `#include part.dta` at the margin splices into the root and indented splices into the
  block. A macro name splices its nodes where it stands and opens no line.
- **The located layout errors.** Mistakes the layout reports with their position instead of
  reading silently: a dedent that lands between two levels (`indent 5 matches no open
  line`); a top-level line that does not start at the margin; a tab in the indentation
  (`indent with spaces`); and a value list continued on a deeper line (`a value list
  continues on the same line or in brackets; a deeper line is a child`). The last one
  exists for four simulation files in the stacks that continue `(objectives ...` on a
  second line, which a plain layout would silently turn into a child array.
- **The three build modes.** The core change sits behind a CMake switch,
  `BAKER_INDENT_LINES`: `OFF` (the default, which compiles none of it), `ALWAYS` (every
  file is read as lines), and `DIRECTIVE`, an opt-in where a file reads as lines from a
  `#lines` directive on.
- **The `#lines` opt-in.** The directive itself, and the `LoadSession::lines` setting a
  host can set to read a text as lines from its start. Every other DTA reader sees
  `#lines` as an unknown directive and fails loudly rather than misreading the file.
- **The value readers pinned to today's reading.** Three core readers take a short text
  that is a value, not a file: a tweak's default text, `Context::eval(text)`, and the
  console's requests. Under the layout, `eval("42")` would return the array `(42)`, so the
  prototype makes these readers clear `LoadSession::lines` and read as today.
- **The lines printer.** A new printer option, `WriteOptions::lines`, that writes a tree in
  the new form: each tagged array as `tag values` with tagged children as deeper lines, and
  a bracketed fallback where a line cannot say the array. The compact printer forms (reply
  text, `check` output, `results.dta`) do not change.
- **The printer's house-style defaults.** A separate, small change: the pretty printer's
  default indent goes from 2 to 3 and its width from 80 to 100 columns, matching how the
  corpus is written by hand. Today an entry that `write_back_to` (the call that writes an
  edited value back into its source file) inserts is printed at indent 2, width 80, and so
  does not match the file around it. This piece was merged in from another proposal
  (one-form) and does not depend on the layout.
- **Write-back in the file's form.** Replacing a value still replaces one token, as today.
  An entry `write_back_to` inserts takes its parent's form: brackets in a bracketed parent,
  a deeper line in a line, a bracketed row under a line that holds a value.
- **Tool support.** Edit-mode source maps record a line's extent; the tolerant structure
  read that editors use for outlines and folding (decision D220, `read_structure`) reports a
  line as a bracket of a new `line` kind; provenance (which file and line a value came
  from) and `explain` are unchanged.
- **The translator.** Outside the core, in `experiments/syntax-indent-lines/`: a text pass
  that rewrites lines into brackets before the ordinary parser, used in the `OFF` build so
  the experiment can be run without the core change.
- **The migration tool.** `baker_syntax_indent-lines convert`: rewrites a bracketed file
  into lines, deleting each array's parentheses only where the layout reads the array
  back, keeping every comment and directive, and re-reading the result to compare trees
  before writing.
- **The second parse loop (proposed, not built).** The prototype's own suggestion for an
  opt-in that costs bracketed files nothing: `#lines` as a file's first line would select a
  second copy of the parse loop (a template instantiated with the layout on), so the
  ordinary loop carries no per-token test. Neither built nor measured.

**Build notes.** The core change is in `src/data/parse.cpp` (the layout rule, a line frame,
the per-token test), `src/data/write.cpp` (the lines printer and the house-style defaults)
and `src/data/config_write.cpp` (write-back in the file's form), all behind
`BAKER_INDENT_LINES`. The translator, the migration tool, the gate tool and the samples are
in `experiments/syntax-indent-lines/`. Nothing is on main.

- **Instructions per parse.** Counted with cachegrind (Release build, GCC 13), against the
  branch before the core change, on `bench_parse`'s 16,552-byte bracketed config text:
  1,845,499 instructions on the base, exactly the same with `OFF`, 1,876,393 with `ALWAYS`
  (+1.67%) and 1,873,809 with `DIRECTIVE` (+1.53%). On the 11 stacks data files the same
  pattern holds: +1.70% and +1.58%. That is the cost to a file that does not use the form,
  and it is above the bar the note takes from decision D143 (the `(key #remove)` marker,
  whose parse cost was measured at +1.1%); about +0.5% had been expected.
  The `DIRECTIVE` build pays nearly as much as `ALWAYS` because a bracketed file there still
  pays the per-token test: it decides nothing until `#lines`, but it tests. A text written
  as lines is cheaper to read than the same tree in brackets, by 15.8% for the config text
  and 1.4% for the stacks' data, because it has fewer bytes and tokens.
- **Four layouts of the per-token test.** Four versions were measured, all reading the same
  trees and passing the same tests. The first cost +7.16% (a per-byte test in the parser's
  size estimate, and a shared close routine that also slowed `OFF` by 0.41%); both causes
  were removed. The other three cost between +1.67% and +2.69% under `ALWAYS`, moving with
  how the compiler laid out the loop. The design's floor is about three instructions a
  token, near +0.6%, and none of the four reached it. Version 2 is the one committed.
- **Wall clock.** `bench_parse` (4.2 MB, best of 10, three rounds) on this machine varies
  by 30 MB/s within one build, far above a 1.7% effect, so the instruction counts are the
  measurement. Every build makes 43 allocations per parse. The translator reads at 0.65 to
  0.71 of the parser's speed; the core reads lines at about the parser's speed on brackets.
- **Core lines.** `tools/count_lines.py` puts the core at 15,531 counted lines, up 341 and
  31 over the soft budget of 15,500 (decisions D69 and D220, the core's line budget). About
  78 of those lines are the build switch itself. Adopted always on without the switch, the
  projected cost is about +250 lines, to about 15,440; as the `#lines` opt-in, about +260.
  The experiment's own code is 1,153 counted lines outside the core, plus 262 in
  `tests/data/test_lines.cpp`.
- **The corpus.** Converting the 68 files with the migration tool gives the same tree for
  every file. It removes 1,650 of 4,654 parentheses (35.5%), and lines ending in two or more
  closers drop from 489 to 29 (in three or more, from 106 to 1). It saves almost no lines
  (1,486 to 1,484) and 2.4% of bytes: the house style already puts each child on its own
  line, so the form removes characters, not lines. Three hand-polished samples
  (hearth, duel, wayfarer) trade a few added lines for fewer parentheses.
- **Diffs.** `promote --patch` on the test stack rewritten as lines still writes a one-line
  diff with the line's comment kept, exactly as for brackets. Appending a child is a pure
  addition that touches no existing line, where the bracketed file must change the old last
  line to move its closers.
- **Tests.** `test_lines.cpp` (10 cases covering each form, the directives, the located
  layout errors, provenance, the printer, write-back and the structure read) and the
  experiment's 15 cases. `OFF` and `DIRECTIVE` pass all 23 ctest suites, and in
  `DIRECTIVE` no existing text changes meaning, the new printer defaults included. `ALWAYS`
  fails 11 of 249 data test cases: 8 are texts the layout reinterprets, 3 pin the printer's
  old output. The verifier noted that the core layout tests sit under
  `#if BAKER_INDENT_LINES`, so the default build runs none of them.
- **The silent reinterpretation in the test suites.** Under `ALWAYS`, 65 distinct texts the
  test suites read change meaning: each is a value or bare value list at a text's top level
  (`read_string("42")`, an included `1 2 3`, a macro body `10`). 59 of them fail a check;
  6 pass silently. This is why the value readers had to be pinned to today's reading. Any
  host that reads a value's text with `read_string` would change the same way, with nothing
  to tell it.
- **The raw gate.** The real-corpus gate of decision D101 reads 464 raw Harmonix DTA files
  from a local clone outside the repository (decision D3 keeps them out of it), of which
  426 read on their own. `OFF` and `DIRECTIVE`: all 464 parse and all 426 give the same
  tree as today. `ALWAYS`: all 464 parse, but 76 of the 426 (16%) read to a different tree
  with no error, because Harmonix scripts write a bare macro alone on a line at the top
  level (`START_BG_MUSIC`, `WAIT_FOR_VO`), which the layout reads as an array.
- **Fuzzing.** libFuzzer, under the address and undefined-behaviour sanitizers, ran
  10,000,000 executions on the final parser with the layout always on, in runs each resumed
  from the last one's corpus (a run stopped to apply a harness fix, or by one of two machine
  restarts, resumed from its last logged count, so no execution is counted twice); none
  found a failure in the parser. An earlier 1,880,759 executions are not counted. The runs
  stopped twice on defects that are main's, not this branch's. First, a file holding a
  second `(origin ...)` form: held as data, it prints as the root's first form, which the
  reader takes as the pack's origin declaration (decision D91), so the print round trip
  loses it; it is a tree today's text cannot spell, of the kind decision D13 lists, so the
  harness now skips its round trip and the input is kept as a regression test. Second, a
  defect in main's fuzz harness: it skipped a tree over 100,000 expanded nodes but read the
  printed text back with a 65,536-node limit, and since a macro used several times counts
  once when read but is spelled in full when printed, a text could read under the limit and
  fail on re-reading. A 164-byte input of three nested macros fails it on main. The harness
  now uses one limit, on this branch and the other seven.
- **CI.** Every job passes on the final head. The release dry run had failed on six lines
  that cited this note, a decision number or a stack's path in running text; they were
  reworded.

**Review.** An agent independent of the prototype rebuilt the branch at its final head,
reran its tests and reproduced all 12 instruction counts, the migration tool's counts, the
samples and the line budget exactly. Its corrections were small: `test_lines.cpp` is 262
lines, not 258; the closing-pile count depends on how comments are stripped (499 and 107
with careful stripping, against 489 and 106); and the default build runs none of the core
layout tests. It also ran the raw gate, which the prototype had not, and found the 76
silently changed files. Both reviewers then scored the idea 2 out of 5.

- **The technical director (the data-oriented lens of `.claude/skills/gamedev-td/`), 2,
  decline.** The reviewer ran the raw gate as well, found the same 76 files and called them fatal: a
  syntax change that silently changes 16% of a real corpus cannot be on by default. The
  reviewer objected that the per-token test costs every parse whether or not the file uses the form,
  so bracketed files pay for a feature they do not use. And the opt-in the note calls free,
  the second parse loop, is neither built nor costed, so there is no measured version of
  the idea that meets the bar.
- **The tool author, 2, adopt in part (the printer defaults).** The reviewer found the most common
  reflow mistake silent: when a wrapped row is continued at its key's own indent, which is
  what an editor's auto-indent produces, the row moves to the parent with no error. (Only a
  dedent that lands between levels is caught; one that lands on an outer level is a
  different, well-formed tree.) And every stack would hold two spellings for good, since
  scenarios, objectives and every tool's printed output stay bracketed, so anyone reading
  Baker files must know both forms.

**Recommendation: decline.** The layout is declined; the printer's house style is taken instead.

- **Decline: the layout rule, the per-token test, the three build modes and the `#lines`
  opt-in.** The idea was built to answer two questions, and both answers are against it.
  Its price on files that do not use the form is +1.67% of the parse's instructions
  (+1.53% for the opt-in as built), above D143's +1.1% bar with four layouts tried. And its
  silent class is not empty: 76 of 426 raw Harmonix files, and every value read as text,
  silently become different trees. What it buys is real but small: a third of the
  parentheses and nearly every closing pile, but no lines and 2.4% of bytes, with every
  stack then holding both spellings. It would also cost about 250 core lines and put the
  core near its budget.
- **Decline with it: the located layout errors, the pinned value readers, the lines
  printer, write-back in the file's form, the tool support, the translator and the
  migration tool.** Each exists only to serve the layout. They stay on the branch as the
  starting point if the form is wanted later.
- **Take now: the printer's house-style defaults.** Indent 3 and width 100 as the pretty
  printer's defaults, a two-line change recorded as its own D-entry (an entry in
  `dev/decisions.md` recording a decision and its results), so that a block
  `write_back_to` inserts matches the file around it. It broke no existing test in the
  `DIRECTIVE` build and does not depend on the layout.
- **Hold: the second parse loop.** If the owner wants the form later, the next step is the
  opt-in done so that it is free: `#lines` as a file's first line selecting a second copy
  of the parse loop, so bracketed files carry no per-token test, measured against D143's
  bar, with the migration tool, the `trees` gate and the samples on this branch as the
  starting point. Even then, the tool author's silent reflow and the two spellings remain.

**Design note.** The full design is in `dev/syntax-indent-lines-design.md`, on this
idea's branch.

### 8. inline-layers: a block in a file is a toggleable sub-layer

**Description.** In today's Baker a layer is a whole file. A Config, the object a game uses
to read its tuning data, holds a stack of layers (the base data file at the bottom, then
files that change it), and when it merges the stack a key written by a higher layer
replaces the same key below. Three common needs therefore cost files and host code. A
platform variant, such as lower settings on a handheld, is a second file plus a
`push_layer` call the game's C++ makes only on that platform (`examples/config/main.cpp`).
An A/B experiment is two files plus two `toggle_layer` calls (the cookbook's recipe). And a
difference written inside one file has to be an `#ifdef ... #else ... #endif` region,
which the parser resolves at the token level and deletes before any tool sees the result:
the Layers panel cannot show it, `explain` (the query that says which layer and line a
value came from) cannot name it, write-back cannot edit it, and nobody can toggle it while
the game runs.

The idea lets a file declare layers inside itself. A block written
`(#layer NAME FORMS...)` is taken out of the file's tree by the parser and recorded in the
load session; a Config then pushes it directly above its file as a separate layer, named
`FILE:NAME` (so `default:handheld` for a block `handheld` in the layer `default`). The
block's head may carry `(when COND)`, which turns it on only when the condition holds for
the session's defines (the same named flags `#ifdef` tests), or `(disabled)`, which starts
it off. A person can toggle it either way. Because it is an ordinary layer, every tool
built for layers works on it: `source()` and `explain` name it and its line, `layer_effect`
and `diff` show what it changes, write-back writes into it, and it is reloaded, placed and
removed together with its file. The aim is a bracketed, visible, named replacement for a
token-level `#ifdef` wherever the conditional wraps whole data forms.

```dta
; today: two files, and a push the host makes on a handheld
;   default.dta
(renderer (shadows 1 (desc "Cast shadows")) (clear_color 0.1 0.1 0.2 (desc "Background")))
;   handheld.dta, pushed by examples/config/main.cpp only on that platform
(renderer (shadows 0))

; with blocks: one file; on when the host defines HANDHELD, toggleable in the Layers panel
(renderer (shadows 1 (desc "Cast shadows")) (clear_color 0.1 0.1 0.2 (desc "Background")))
(#layer (handheld (when HANDHELD))
   (renderer (shadows 0)))

; an A/B experiment: one arm on, the other a click away
(#layer hit_pause_heavy (duel (hit_pause 9)))
(#layer (hit_pause_light (disabled)) (duel (hit_pause 4)))
```

The prototype was scoped as the judges (the three reviewers who scored the original
proposals) asked: only the named block with `(when COND)` and `(disabled)`, read from text.
The larger form the proposal sketched was estimated and not built.

**Elements.** The idea is these separable pieces; the later parts refer to them by these
names.

- **The block.** `(#layer HEAD FORM...)`, with `#layer` written as the first thing inside
  `(`. The lexer needs no new token: `#layer` was already lexed as a directive and was
  previously the error "unknown directive", so no text that read before reads differently.
  Anywhere else `#layer` is the located error "#layer is written first inside '('". An
  empty block, `(#layer x)`, is an empty layer.
- **The head.** A name, bare or quoted (a string or a number is refused), or a list
  `(NAME OPTION...)`. It is read unexpanded, as `#ifdef`'s name is, so a macro named
  `HANDHELD` does not rewrite `(when HANDHELD)`. Options exist only in a head, so a data
  field that happens to be called `when`, such as Ledger's milestone field, is untouched.
- **`(when COND)`.** The condition: a define name, or `(not C)`, `(and C C...)`,
  `(or C C...)`. It is evaluated once, when the block closes, against the session's
  defines at that point (the host's defines and the `#define`s read above it), exactly as
  `#ifdef` reads them. Its text and value are kept, so a tool can list which blocks a
  define set turns on.
- **`(disabled)`.** Starts the block off; a person or host can enable it later.
- **The closed option set.** Any other option, including the unbuilt `(group ...)`,
  `(from ...)`, `(until ...)` and `(dev_only)`, is a located error that names the allowed
  set.
- **"A block holds data".** A block's forms are ordinary data, including D143's
  `(key #remove)` marker (the existing way for a layer to delete a key). Any other directive
  inside a block is the located error "a layer block holds data: no #include inside
  (#layer ...)", so a block never includes a file, defines a macro, nests a conditional or
  another block, or collects an `#autorun`. A block's forms expand macros defined above it.
- **Blocks inside arrays (the re-wrap).** A block may stand at a file's top level or inside
  arrays that all have a symbol tag. Inside, its forms are wrapped in the enclosing tags,
  so `(renderer (fog 1) (#layer hh (shadows 0)))` is the file `(renderer (fog 1))` plus the
  sub-layer `(renderer (shadows 0))`. Inside a command, a property, a macro body or an
  untagged array a block is a located error. Wherever it is written, a block applies above
  its whole file.
- **Unique names.** A block names a layer, so a second block of the same name in one load
  (a file and what it includes) is the located refusal "block 'x' is declared twice
  (first at t.dta:1)". A block in an included file belongs to the including layer.
- **The `layer_blocks` opt-in.** A load session reads blocks only when
  `LoadSession::layer_blocks` is set, which a Config does for its layers and its text
  checks. Otherwise a block is the located error "a layer block is read only as a Config's
  layer". Without this, any other reader (scenarios, objectives, the console's requests, a
  script load, a host's own `read_file`) would drop the block's data in silence.
- **The session record.** Nothing new goes into the tree: node and array layouts are
  unchanged. Each block's forms become a separate root array built in the load's arena,
  and `LoadSession::blocks()` returns, per block, its name, file, line, the disabled flag,
  the condition as written and its value, and the tree. In edit mode each block tree is
  recorded with the block's extent (its text span, brackets included), which write-back
  uses.
- **The unit rule.** The Config side is one rule, "a file and its blocks are one unit, the
  blocks directly above their file", enforced in four places: re-reading a block re-reads
  its file and rebuilds the file's blocks (keeping a host's toggle unless the head
  changed); placement (D144, placing a layer below or above another) snaps "below a block"
  to below its file and "above a file" to above its last block; `remove_layer` and
  `pop_layer` take the blocks with the file (removing a block alone is refused: disable it
  instead); and pushing builds the unit and refuses a block name already in the stack.
  Merging, snapshots and restore needed no change: a block is one more layer.
- **Provenance and `explain` through blocks.** Unchanged code: `source()` answers
  `default:handheld` and the block's line, `explain` lists the block (as Disabled when
  off), and `layer_effect`, `diff`, `layer_root`, `set_layer_enabled` and `toggle_layer`
  take its name.
- **Write-back into a block.** The existing mechanism, with two lines changed: whether a
  layer has a source file is now told by its file rather than its path, and a new
  top-level key goes at the end of the file only for a file layer, not a block. Promoting
  (writing a layer's changes down into a lower file) out of a block works; promoting into
  a block is refused, "promote into a layer block is not built" (about eight more lines).
- **Blocks in received layers.** Under D161's layer policies (the rules that check a text
  layer received at run time, such as a live-operations overlay, before it takes effect),
  each block in a received text is checked as a candidate of its own, `FILE:NAME`, whether
  it is enabled or not, since a person may enable it later without another check.
  `check_layer_text` and the off-thread `LayerChecker::check_text` now share one reader that
  reports blocks.
- **The cache refusal.** The binary cache format is unchanged (`kVersion` stays 6). Writing
  a cache from a load that read blocks is refused, so neither a Config's cache nor the
  `baker_bake` tool writes one that silently drops them; a Config notes this through its
  warning handler and reads the file from text.
- **The guide.** `docs/language.md`'s "Platforms and builds" teaches blocks first and keeps
  `#ifdef` for what a block cannot say.
- **The converter.** `baker_syntax_inline-layers` in `experiments/`, outside the core:
  `to-new` turns each convertible `#ifdef` region into a block and names the regions it
  leaves, `to-dta` writes blocks back as `#ifdef` regions, `compare` and `check` merge both
  forms under every define set, `survey` classifies a corpus's conditionals, and `bench`
  and `bench-layers` time them. About 985 code lines.
- **The full form, not built.** Groups of mutually exclusive arms, `(group NAME)` (about 16
  core lines); validity windows, `(from ...)`/`(until ...)` dates checked against the
  host's clock (about 50 core lines plus 10 in the host kit); `(dev_only)` (about 4); a
  `blocks` entry in the binary cache at the next format version (about 28); and a
  `write_stack` printer that can print a Config's blocks.
- **The project-file `(when COND)` flag (the alternative).** Not part of the prototype:
  a `(when COND)` flag on the layer entries of the project file (`baker-project.dta`,
  D172, which lists a game's layers for the game and its tools), beside the `optional`,
  `disabled` and `dev_only` flags it already takes (D204 added `dev_only`, for layers a
  shipping build never loads). Estimated at 10 to 15 lines.

**Build notes.** Built in the core behind the build switch `BAKER_INLINE_LAYERS`, off in
the default build and on with `BAKER_BUILD_EXPERIMENTS`, on the branch
`claude/fervent-cray-l1kyf0-syntax-inline-layers`: 14 commits to its final head `ab1d3d8`,
reviewed at `85d53e7`, changing 7 files under `src/` and `include/` (the parser, the
Config, its checks, write-back and the cache), plus the fuzz property and dictionary, the
guide and the converter.

- **Parse cost** (instructions counted by cachegrind with `tools/count_instructions.py`,
  Release build, against the session branch `8f15684`, whose code is the fork point's): a
  16 KB block-free document parses in 1,845,499 instructions before and 1,844,556 with the
  switch on, −0.05%; a lookup is 203 both ways. The first cut cost +3.45%, because the
  close of an array had grown past the compiler's inlining into the parser's loop; moving
  the block's close into a separate cold function that calls none of the hot path's
  helpers brought it back to noise. Wall-clock benchmarks (`tools/bench_compare.py`, five
  alternating runs a side) moved both ways within overlapping ranges on a machine shared
  with the eight prototypes' builds, so they show noise.
- **Merge cost of blocks.** Each block is one more layer, so a file with k blocks costs k+1
  merges. Pushing a 4 MB generated text as one layer took about 36 ms; with half its forms
  in 8 blocks 41.3 ms (about 1.15 times), in 64 blocks 70.0 ms (about 2 times), roughly
  0.6 ms a block over that text's 16,558 top-level forms, paid at push and at every full
  merge (a reload, a toggle). Reading `ui_menu.dta` with blocks was about 4% slower than
  with `#ifdef` (a few hundred nanoseconds at 11 microseconds a read), because a block
  builds the tree of a branch that `#ifdef` would skip.
- **Core lines** (`tools/count_lines.py`, against the session branch's 15,190 of the
  15,500 soft budget, so 310 lines of headroom): +221 as it would be adopted, without the
  switch's guards, against a bar of 150 set for this prototype; +265 with the guards. The
  fuzz fix below added 8, so about +229 adopted (15,463 with the switch). The Config half
  is 80 adopted lines, under its 110 estimate; the parser half is 141, well over its 95, of
  which the re-wrap is about 16 (14 by the open-questions estimate) and a top-level-only
  block would total about 205. The full form would be about 319, more than the whole
  headroom.
- **Samples.** `examples/config`'s two files and the host's second `push_layer` become one
  file of 11 lines (399 bytes against 344: the head states a condition the host used to
  hold). `tests/corpus/ui_menu.dta`'s nine lines of `#ifdef HAS_VOCALS ... #else ...
  #endif` and `#ifndef DEMO_BUILD ... #endif` become three lines of blocks, 49 lines to 45,
  because the `#else` branch collapses into the file's default. Duel's file plus an A/B
  pair, three files with two pushes and two toggles, become one file of 52 lines.
- **How much it covers.** In the repository's corpus only one file (`ui_menu.dta`) of 68
  has a conditional, and no stack has one. On the raw corpus of D101's real-corpus gate
  (464 Harmonix script files from Rock Band and Guitar Hero 2, read from a local clone
  that is not in the repository), 112 files hold 1,024 conditional regions, and 162 of
  them (16%) can become blocks. The rest are inside script commands or properties (317),
  nested in another region (356), hold a directive (73), hold bare list values (76), are
  inside an untagged array (38), or span a bracket (2). Of the 36 files with a converted
  region, 29 could be merged both ways under every define set: 10 equal, 15 equal up to
  key order, and 4 differ only when two or more platform defines are set at once, which no
  build does.
- **Tests.** `baker_data_tests` 258 cases, 19 of them on blocks (the head and options,
  conditions, the re-wrap, the refused directives, the `layer_blocks` opt-in, handheld as a
  block equal to handheld as a file, toggles, snapshots, the unit rule, reload, write-back,
  received blocks under a policy, the cache refusal); the experiment's 10 cases, including
  the corpus round trip, 68 files with 0 faults; `ctest` 23 of 23 with the switch on and
  21 of 21 in the default build; clang-tidy clean on the changed files. The local matrix
  was not run whole on the shared machine.
- **The raw gate.** With the switch on, 464 of 464 files read and round-trip, 0 failures.
- **Fuzzing.** The prototype round had no libFuzzer runtime, so a dictionary-mutation driver
  ran the parse property (each block the same in a plain and an edit-mode read, its spans
  inside the text, its tree printed and read back) under ASan and UBSan (the address and
  undefined-behaviour sanitizers): 2,152,426 inputs, no failure. The review round then ran
  libFuzzer, built as CI's fuzz job is, with the switch on. It stopped after 140,554
  executions on a real bug in the prototype: a `[property]` written directly among a
  top-level block's forms, `(#layer x [c 1])`, became a property at the top level of the
  block's tree, which prints as a macro definition, so the block did not print back as
  itself. It is now a located error, "a [property] cannot appear at a block's top level,
  where '[' defines a macro" (inside tagged arrays a property stays legal), at +8 core lines
  (`283d74e`). Resumed from the grown corpus, 978,786 more executions found nothing
  (1,119,340 in all). The completion round then ran 10,000,000 executions on the final
  parser (`283d74e`'s code; the later commits change the note and the harness), in runs each
  resumed from the last one's corpus (a run stopped to apply a harness fix, or by one of two
  machine restarts, resumed from its last logged count, so no execution is counted twice),
  with no failure; the earlier 1,119,340 are not counted.
- **Fixes to the fuzz harness.** The branch fixed a defect in main's own fuzz property,
  which failed on every input holding `(key #remove)`, with a regression file. It also
  carries the fix indent-lines' run found in main's harness, which skipped a tree over
  100,000 expanded nodes but read its print back with a 65,536-node limit, with its
  regression input `macro_sharing_budget.bin` (`ab1d3d8`).
- **CI.** Every job passes on the final head.

**Review.** An independent verifier rebuilt the branch and reproduced the test counts, the
line counts, the cachegrind counts, the samples and the corpus check exactly, and the
wall-clock rows within noise. Its corrections: the note leaves out the prototype's own
size (985 code lines, 1,200 raw); reload is never timed (`bench-layers` times the first
push only); `bench_compare.py` as committed builds the new side with the switch off, so it
does not measure the feature; the earlier fuzz driver and its block seeds are not
committed, so that run cannot be repeated from the branch; and the experiment's
`bench <file>` exits silently when an include does not resolve.

Two reviewers each scored the idea 2 of 5 and would decline.

- **The technical director** (the data-oriented console-engine lens of
  `.claude/skills/gamedev-td/`) refuted the note's defence that blocks are "build time,
  not run time". A platform condition is exactly what a bake (the offline step that turns
  data into the binary cache a shipped game loads) could settle once; with blocks it
  becomes a file the bake refuses to cache. And a shipping build refuses
  `set_layer_enabled`, so the run-time toggle, the feature's only payoff over `#ifdef`, is
  not there in the build that ships. Platform variants, A/B arms and live-ops alternatives
  are three different data problems, and the block fits none of them well.
- **The tool author** found three of the note's claims wrong. First, shipping refuses
  `set_layer_enabled` (`src/data/config.cpp:731-732`), so the live-ops promise, one
  disabled block per alternative that a live team can flip "without a second push", does
  not work: the flip still needs a push. Second, `ui_menu.dta`'s real reader is a plain
  `read_file`, which does not set `layer_blocks`, so it would reject the converted file.
  Third, the Duel A/B sample replaces host code (two pushes and two toggles) that a stack
  opened from a project file does not have. Two gaps for tools: `check` never sees a
  disabled block, and the printer cannot write a block at all.

**Recommendation: decline.** Keep the branch as the measured reference, and take nothing
into the core now. The prototype showed that **the unit rule** works: sub-layers fit the
Config's layer machinery in four places, with **provenance and `explain`**, **write-back
into a block**, reload, placement, snapshots and **blocks in received layers** unchanged in
kind, at no measurable parse cost on block-free text. But the scoped form (**the block**,
**the head**, **`(when COND)`**, **`(disabled)`**, **blocks inside arrays**, **the
`layer_blocks` opt-in**, **the session record** and **the cache refusal**) costs about 229
core lines as adopted, over the 150 bar and about three quarters of the remaining headroom,
for a feature no stack uses, which replaces only a sixth of a shipped game's conditionals,
and which adds a second conditional form beside `#ifdef` with different precedence (in a
block the later write wins; in `#ifdef` a repeated key's first occurrence does). The
reviewers' points remove most of the payoff: shipping refuses the toggle, the bake refuses
the file, and the live-ops flip still needs a push. **The full form** is dropped for now,
since it alone would exceed the headroom; groups wait for a stack that writes exclusive
arms, and windows belong to the host's release tooling rather than the data. `#ifdef` is
not retired either way: 84% of the raw corpus's regions are forms a block cannot take.

If conditional platform layers are wanted sooner, the step to take instead is **the
project-file `(when COND)` flag**: add `(when COND)` to the layer entries of
`baker-project.dta`, beside `optional`, `disabled` and `dev_only`, so that `open_project`
pushes a layer only when its condition holds for the session's defines. With `disabled`,
that gives conditional, toggleable platform and experiment layers with no parser change, no
cache question and no second conditional form, at an estimated 10 to 15 lines. That step
would be its own D-entry (an entry in `dev/decisions.md` recording the decision and its
measurements). In-file blocks wait for a stack that
writes a platform variant or an A/B and feels the file count.

**Design note.** The full design is in `dev/syntax-inline-layers-design.md`, on this
idea's branch.

## What "done" means for these branches

The goal of the round was a sketch in code and a design for each idea, not an adoption.
On that bar every branch is complete: each has a reader of its form into Baker's tree (or
the change itself in the core), a printer back, tests that pass, the 68-file round trip
with no fault, samples of real files in the new form, measured costs, a ten-section design
note with an eleventh recording the review and the completion round, a run of D101's raw
gate, CI green on every job, and spec §8's fuzz bar. A first pass left two pieces its
briefs named unbuilt, hash-declarations' core change and curly-infix's in-tree rewrite, and
stopped every fuzz run short of the bar; the completion round built both (with the
malformed-infix error curly-infix's review asked for), gave column-tables' translator a fuzz
target of its own, and took every branch to the bar. Each brief also listed work that only
an adoption needs; the prototypes left it deliberately, and it is listed here so that
nothing reads as finished that is not.

**Fuzzing.** Every branch ran libFuzzer under ASan and UBSan, built as CI's fuzz job is,
with its switch on, seeded with the corpus, the fuzz regressions and the branch's samples,
to 10,000,000 executions on its final code: a run that found a bug was fixed and the count
began again on the fixed code. Executions made before a branch's last parser change are not
counted.

| Branch | Build fuzzed | Executions on the final code | What the runs found, and the fix |
|---|---|---:|---|
| curly-infix | reader on | 10,000,000 | main's `(key #remove)` harness defect (below), fixed on the branch |
| path-rows | rows on | 10,000,000 | a bug in the branch: `(/pack ...)` as a first form printed as a declaration, now printed as a row (`662d119`); and main's unprintable `(origin "...")` first form where the top level repeats a tag, which the harness skips as D13's |
| layer-markers | markers (always on) | 10,000,000, and 1,000,000 from a random driver | under libFuzzer, nothing; the random driver found a printer bug in the branch, fixed |
| hash-declarations | switch on | 10,000,000 | main's `(key #remove)` harness defect, fixed on the branch |
| at-metadata | switch on | 10,000,000 | nothing |
| column-tables | the translator (`experiments/syntax-column-tables/fuzz.cpp`) | 10,000,000 | three bugs in the translator, each fixed with a test: a print starting with a byte order mark, a column name across lines, a header with no columns |
| indent-lines | always on | 10,000,000 | main's unprintable `(origin "...")` first form, which the harness skips as D13's, and main's harness budget (below), fixed on every branch |
| inline-layers | switch on | 10,000,000 | a bug in the branch: a `[property]` at a top-level block's top level printed as a macro definition; a located error (`283d74e`) |

**CI.** Every job passes on every final head. Five branches had red jobs the first pass
did not look at: the release dry run (`publish.py check`) refused shipped comments that
cited a design note in running text, and on at-metadata two functions used only with its
switch on stopped all five Clang builds. Each was reworded or fixed on its branch.

**Left for adoption, by branch.** None of these is needed to judge the idea, and where a
verdict rests on one the recommendation above says "explore" or "compare" first.

- **curly-infix.** The reading trial the review asks for. Not built: the tolerant read's
  `tag` for infix braces, tree-sitter and LSP support, a `docs/language.md` section, a
  comment-keeping reverse converter; `==`, `!=` and `/` stay two-argument on a measured
  cost. A build with the reader off no longer reads the branch's rewritten files.
- **path-rows.** Not built: `diff --rows`, `promote --patch --rows`, the journal export,
  a D-entry; the comparison with a host `set` command the review asks for comes first.
- **layer-markers.** Not run: the local matrix's shipping and EASTL builds (CI's matrix
  passes). Not built: the refusal of verbs inside `(shapes ...)`, `leaf_values` for D145's
  per-layer shape read, `explain` of element paths into a composed list, the docs; the
  review's flag-where-made rework and the already-present warning.
- **hash-declarations.** `diff`, `layer_effect` and the tolerant read showing a
  declaration, which the review makes the condition for the directives; the at-the-table
  `#shape` in the core; `count` in `baker.shapes`; a `tools/migrate_dta.py`; a
  `bench_compare.py` run on a quiet machine.
- **at-metadata.** The spelling-only pass section 10 recommends; `align_meta` in the core
  printer; a shape's own `(@desc)`; reporting an unknown `@word` in data.
- **column-tables.** A core implementation; a text-preserving converter; promote into a
  headed file; the Ledger exporter.
- **indent-lines.** The zero-cost opt-in as a second parse loop; `write_back_to` adding
  values to a key with child lines.
- **inline-layers.** Groups, validity windows, `dev_only`, the cache entry and a
  `write_stack` printer, which the brief scoped out; promote into a block.

## Found on main along the way

Five defects in today's Baker, each found by a prototype or its fuzz run and independent of any syntax:

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
- **A tree whose first form is `(origin "...")` held as data cannot be printed.** A file
  holding a second `(origin "...")` after its own reads the second as data; printed, it is
  the root's first form, which the reader consumes as a declaration (D91), so the print
  round trip loses it. Three lines reproduce it on main:
  `(origin "a" (tool t))`, `(origin "b" (tool u))`, `(x 1)`. libFuzzer found it on the
  indent-lines branch after 853,555 executions; it belongs in D13's list of trees the text
  cannot spell, and hash-declarations' `#origin` is the syntax that removes it. With
  rows on, path-rows prints it as a row whenever the top level repeats no tag.
- **The fuzz harness's size budget is above the reader's node limit.** The harness skips a
  tree whose expanded size passes 100,000 nodes, but reads the print back with a limit of
  65,536. A use of a macro counts once when the text is read, while the print spells every
  node, so a tree between the two fails a property the parser never broke: 164 bytes of
  three nested macros (`macro_sharing_budget.bin`) fail main's harness. libFuzzer found it on
  indent-lines after 1.68 million executions; the fix, a budget equal to the session's own
  limit, and the input are on all eight branches.

Each wants a D-entry and a small fix on main whatever happens to the syntax; the fixes for
the first two and the fifth exist on the branches named, and the fourth's harness skip and
regression input are on indent-lines (`tests/fuzz_regressions/origin_first_form.bin`).

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
  trees with no error. D101's gate is run by hand and is not in CI; its five results
  above each settled or changed a recommendation, the last one keeping `#define NAME
  (body)`.
- **Short fuzz runs miss what long ones find.** The first runs stopped at 1 to 2 million
  executions; taking every branch to 10 million found three bugs in prototypes (path-rows'
  first-form row after 2.1 million; column-tables' line-break column name after 4.4 million
  and its empty header 1.3 million later) and a defect in main's own harness after 1.7
  million. CI's two-minute job makes about 200,000.

## Decisions for the owner

Each is one decision, with the recommended answer first.

1. **Fix the five defects found on main, whatever happens to the syntax?** Recommended:
   yes, each as its own step with a D-entry: the fuzz property's `#remove` case (the fix
   and regression files are on inline-layers and curly-infix), the `[property]` argument
   (one comparison, on curly-infix), the received-layer shape widening (a policy rule: a
   received layer that declares no pack may not hold `(shapes ...)`, or shapes stop being
   data, as hash-declarations proposes), the unprintable `(origin "...")` first form
   (D13's list and the harness skip now, `#origin` with item 5), and the harness budget (one
   line and `macro_sharing_budget.bin`, on every branch). Alternative: leave them for the
   syntax work that touches each.
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
4. **curly-infix: the vocabulary now, the infix reader after a trial.** Recommended: take
   variadic `+ - *`, chained comparisons, `+= -= *= /=` and the head check now; malformed
   infix is now an error, so what holds the reader is only a trial of whether infix beats
   prefix chains for the people who write objectives, on the branch's rewritten metrics.
   Alternative: take the reader now behind the switch (the gate is clean, and the branch
   shows the tree's files rewritten).
5. **Declarations in `#`.** Recommended: the parts that need no new syntax now (the
   `(list T N)` arity with `count` in `baker.shapes`, the governs-nothing finding, the
   received-shape policy rule) and the `#`-before-a-letter rule; `#pack`, `#origin` and
   `#shape` only once `diff`, `layer_effect` and the tolerant read can show a declaration;
   `#define NAME (body)` kept either way, since refusing it fails 194 raw files.
   Alternative: the directives as one step now, with cache version 7, as the branch's own
   section 10 now proposes: the core change is written, 128 lines and a parse 0.37%
   cheaper.
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
