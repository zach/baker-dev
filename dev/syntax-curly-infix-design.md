# Curly-infix: a brace of one operator reads head-first

*A prototype on `claude/fervent-cray-l1kyf0-syntax-curly-infix`, merged from the proposals
curly-infix and expr-precedence (dev/syntax-survey.md on the session branch, "EXPRESSIONS").
The reader rule and its printer sit behind a compile-time switch, `BAKER_CURLY_INFIX`, which
was on where the experiments build and off in the default build until the in-tree rewrite
(section 11), and is on by default since; the vocabulary (variadic and chained operators, `+=`
and its family) is unconditional. Every number in sections 1 to 10 was measured on this
branch on the shared 4-core machine the brief names, against the session branch (`8f15684`,
whose code is the fork point `3817213`); section 11's against `15ce5b6`, the commit before the
rewrite.*

## 1. Thesis

Inside `{...}` only, a brace of an odd number of nodes, three or more, whose odd slots hold
one and the same bare operator symbol, and which has a number, variable, property or command
among its operands, is read as the prefix command it spells: `{0.3 <= $value <= 0.7}` is the
tree `{<= 0.3 $value 0.7}`, `{$i += 1}` is `{+= $i 1}`, and `{c ? a : b}` is `{if c a b}`.
An operator at slot 1 that fits neither shape is a located syntax error ("nest braces"). The
operators become words that take more than two arguments where a chain needs it (`+ - *` fold
left; `< <= > >=` test every adjacent pair, evaluating each argument once), and `+= -= *= /=`
join them.
Nothing else is new: no token, no node kind, no evaluator form. One bit of the Array header,
beside D143's `has_removal`, records that a command was written infix, so the printer, and so
`write_file`, promote and a reply's text, give a designer back the spelling they wrote.

This relieves the survey's seventh pain by name: "script is wordier than the data and hostile
to the people the harness vision wants writing it: every objective band is `{and {>= $value
lo} {<= $value hi}}` (15 of 54 passes), clamp is two nested ifs, ... assignment is `{set $i
{+ $i 1}}`". It serves the spec's designer-editability goal (spec:202-210) and the harness
vision's gardeners writing objectives in data, and it does so by the one route the survey's
ninth constraint leaves open: a rewrite at read time into the head-first array the evaluator
already reads, so laziness, `check`, the cache, D222's digest, the console's `(ID {command})`
grammar and write-back's spans meet the trees they meet today. The radical part is that
Baker's commands have been head-first since DTA; this lets the page show a spreadsheet's
spelling while the tree stays a Lisp. The full C-like grammar (expr-precedence) is the
rejected superset: a precedence table every tool would have to carry, a printer lossy by
construction, and a new class of silent misreading in a language that forbids special forms.

## 2. The syntax

**Tokens.** Unchanged. Operators are the symbols they are today and need spaces around them,
as every atom does: `{$x-1}` is still one symbol and `{$x -1}` is `$x` and the Int -1.

**OPERATOR** := an unquoted Symbol whose text is non-empty, every byte one of
`+ - * / % < > = ! & | ^ ~`, and which does not begin `/*` (that opens a comment and can
never be read bare). So `+ - * / % < <= > >= == != && || ! += -= *= /=` qualify; `?` and
`:` are reserved for the ternary and are not chain operators; `.`, `@`, `#`, `$` and `'` are
not operator bytes (src/data/text.hpp:73-92).

**The rule** (src/data/parse.cpp:595-699), applied when a `{` frame, a command or an
`#autorun` brace, closes over nodes k0 .. kn:

1. *Eligible*: n+1 is odd and at least 3; no odd slot was written as a quoted symbol or a
   macro use, and no macro in the brace expanded to more than one node (a frame mark set in
   `symbol()`, parse.cpp:531-545); and k1 is a Symbol whose first byte is an operator byte or
   `?` (the inline pre-test, parse.cpp:639-644). Anything else closes as today.
2. *Guard*: at least one even slot holds an Int, Float, Var, Property or Command. A brace of
   bare symbols, `{wheat -> flour}` or `{a - b}`, is a command as today, so no raw DTA brace
   can change meaning without a number, variable or command in it.
3. *Chain*: k1 is an OPERATOR and every odd slot is the same Symbol. The tree is the Command
   `[k1, k0, k2, k4, ...]`. The nodes, and in edit mode their spans, are permuted in the
   parser's scratch vector; the operators after the first are dropped with their spans; the
   head keeps the first operator's span; the brace's extent is unchanged; the Array is marked
   written infix.
4. *Ternary*: k1 is `?` and either n+1 is 3 (`[if k0 k2]`) or n+1 is 5 with k3 the symbol
   `:` (`[if k0 k2 k4]`). The `if` is written nowhere, so its span is empty and write-back
   refuses to replace it. Marked written infix.
5. *Malformed*: k1 is an OPERATOR or `?` and neither shape holds; or (section 11) n+1 is
   even, k0 is a number, variable, property or command, and k1 is an OPERATOR or `?`, a
   brace no head-first call writes (quoted, `{$obj '<=' 1 2}` is a call). ErrorCode::Syntax at
   the brace's line and column: "mixed operators in {...}: nest braces to say which applies
   first" when another operator holds an odd slot, "infix {...} needs '<=' between every two
   operands" when an operand does, "infix {...} ends with '<=': write an operand after it"
   for an even count, and "'?' in {...} takes {c ? a} or {c ? a : b}" for a ternary of any
   other shape.

**Mapping.** The result is a Command whose slot 0 is the operator Symbol or `if`, and whose
operands are the nodes they already are: Int, Float, Symbol, String, Var, Property, Command,
Array. No Type is added, Node stays 16 bytes, the Array header stays 24 bytes, and the only
new state is the header bit `kWrittenInfix` (include/baker/array.hpp:540-546, 580), which
equality, the digest and evaluation ignore and the cache carries as it carries D143's bit.

**The printer** (src/data/text.cpp:126-150, src/data/write.cpp:38-60, 112-165). A command
marked written infix whose shape still fits prints `{k0 op k2 op k4}` or `{c ? a : b}`; when it does
not fit the width, it breaks one operand a line with the operator ending each line, as the
pretty printer breaks any array. A command printed prefix that the reader would take infix
(an odd count, an operator or `?` at slot 1, an operand among the even slots), or refuse as
malformed (an even count, an operator or `?` at slot 1, an operand at slot 0), quotes its
odd-slot operators: `{+ '-' 1}`, `{1 '?' 2 ':' 3}`, `{$obj '<=' 1 2}`. Every other tree
prints byte for byte as on main, so `parse(write_dta(tree))` stays structurally equal for
every tree, C++-built ones included (constraint 1), and the bytes of check's repeat forms,
results.dta and every existing reply do not move.

**The vocabulary** (src/script/builtins.cpp:123-263, 333-415). `+ *` take two numbers or more
(`{+ 1}` stays an error, as it is today) and `-` one or more (one is negation); ints stay
ints, wrapping, until a float joins. `< <= > >=` take two numbers or more, each argument
evaluated once, stopping at the first pair that fails, so `{0.3 <= {f} <= 0.7}` calls `f`
once. `/`, `==` and `!=` keep main's two arguments: they were single overloads already, a
rest parameter cost them 136 and 115 instructions a call (section 4), and the corpus has no
chained quotient or equality, so `{a == b == c}` reads as a chain and fails at evaluation and
in `check` with "'==' takes 2 arguments, given 3". The brief chained `==` and `!=` too; this
is the one place the prototype narrowed it, on a measurement. `+= -= *= /=` take a place (`$var`, or `[prop]` in a method, as `set` takes) and a number:
load, apply, store, return; an unset variable is "$x is not set", as reading it is. `&& || !`
are `and`, `or`, `not` under a second name. `=` stays undefined, so `{$x = 1}` is a call of
`=` that `check` reports as demand, never an assignment.

**Edge cases.**
- *Quoting.* A quoted symbol at an odd slot exempts the brace: `{a '-' b}` and `{+ '-' 1}`
  never fire. At an even slot a quoted symbol is an operand like any other.
- *Numbers.* `{- 5}` is two nodes and stays a negation; `{x - -5}` is a chain over the Int
  -5. A malformed number is still a symbol (D10), so `{1e - 2}` is a brace of `1e`, `-`, 2:
  the guard holds (2) and it reads `{- 1e 2}`, an error at the first evaluation, as `{- 1e
  2}` is today.
- *Comments* between nodes are skipped as today; the converter refuses to rewrite a command
  whose own gaps hold one, so no comment is lost (it found none).
- *Empty and short braces*: `{}`, `{f}`, `{f a}` and every even count close as today.
- *`#remove`* is refused in a command today and still is.
- *Macros.* A one-node macro at an even slot is an operand: `{$i < kMaxPlayers}` reads
  (tests/corpus/ui_menu.dta:33 through macros.dta:9). A macro at an odd slot, or one that
  expands to several nodes anywhere in the brace, exempts it: its expansion is not text a
  person can see.
- *Directives.* `#ifdef` inside a brace skips tokens as today; the slot parity is counted on
  what was read.
- *Packs.* The rule is lexical and the same in every pack; the one-way rule (D80) is untouched.
- *Shapes and scenarios.* A command in data is a value to a shape and to a scenario; either
  spelling is the same value.

**The corpus, before and after** (`to-new`'s output; every line is a real file's):

| file:line | today | curly-infix |
|---|---|---|
| stacks/duel/metrics/duel.dta:31 | `(pass {and {>= $value 0.3} {<= $value 0.7}})` | `(pass {0.3 <= $value <= 0.7})` |
| stacks/duel/metrics/duel.dta:22 | `(pass {or {< $value 0} {>= $value 10}})` | `(pass {or {$value < 0} {$value >= 10}})` |
| stacks/stuntline/metrics/track.dta:11 | `(pass {and {> $value 0} {<= $value {* 1.5 {track.par}}}})` | `(pass {and {0 < $value} {$value <= {1.5 * {track.par}}}})` |
| stacks/hearth/metrics/hearth.dta:30-31 | `(measure {+ {+ {hearth.events stuck} {hearth.events oscillating}}`<br>`{+ {hearth.events starving} {hearth.events script_errors}}})` | `(measure {{hearth.events stuck} + {hearth.events oscillating} +`<br>`{hearth.events starving} + {hearth.events script_errors}})` |
| examples/calculator/functions.dta:5 | `{func clamp ($x $lo $hi) {if {< $x $lo} $lo {if {> $x $hi} $hi $x}}}` | `{func clamp ($x $lo $hi) {{$x < $lo} ? $lo : {{$x > $hi} ? $hi : $x}}}` |
| examples/scripting/rules.dta:7 | `(damage {if {> $difficulty 2} 12 9})` | `(damage {{$difficulty > 2} ? 12 : 9})` |
| tests/corpus/ui_menu.dta:30 | `{set $menu_enter_count {+ $menu_enter_count 1}}` | `{$menu_enter_count += 1}` |
| tests/corpus/ui_menu.dta:33-35 | `{while {< $i kMaxPlayers} ... {set $i {+ $i 1}}}` | `{while {$i < kMaxPlayers} ... {$i += 1}}` |
| stacks/ledger/data/default.dta:25 | `(when {>= {econ.count mill} 1})` | `(when {{econ.count mill} >= 1})` |

Two shapes the corpus has stay as they are. The multi-line `{if {== $platform pc} {game quit}
{ui show_message ...}}` (ui_menu.dta:26-28) is a statement, so only its condition turns
(`{$platform == pc}`). Ledger's `(recipe wheat wheat -> flour)` (stacks/ledger/data/
economy.dta:18) is in parentheses, where the rule never applies; written in braces it would
be a brace of bare symbols, which the guard keeps a command.

## 3. What it keeps and what it changes

- **Lexer.** Unchanged: no token, no atom-end byte, no lexer state.
- **Parser.** Re-provided in the parser itself: a mark byte in the frame (folding D143's
  `removes` bool and the new `no_infix` into one store, parse.cpp:458-463), the frame kinds
  reordered so a brace is one comparison (parse.cpp:450), a pre-test of slot 1 inline and the
  rule out of line and cold (parse.cpp:595-683). With the switch off all of it compiles away.
- **Printer.** Re-provided: `infix_shape` decides chain, ternary, quoted prefix or plain
  (text.cpp:126-150), and `put_flat`/`put_pretty` write the operator between operands.
- **Write-back** (D33, D34, D132, D183, D226). Unchanged in code. A command value is replaced
  wholesale, as config_write.cpp does today, and printed by the printer, so a band written
  infix stays infix when promote or `write_file` writes it. Each operand keeps its own span
  (tested, tests/data/test_parse.cpp:275-340), so a one-token edit of a bound replaces one
  token. Two spans are new in kind: a chain's head carries its first operator's span, so a
  tool that replaced it in a three-operand chain would make a mixed-operator error, loud and
  located, never a silent change; a ternary's `if` has an empty span and is refused (parse.cpp
  ternary branch, test_parse.cpp:342-357).
- **Edit-mode source maps** (D33, D128). Re-provided: the spans are permuted with the nodes
  and the dropped operators' spans removed, so `spans(a).size() == a.size()` holds. The
  libFuzzer harness checks every literal span against its token, and now also that the
  written-infix marks survive print and re-read (fuzz/parse_harness.cpp:51-62, 105-109).
- **Tolerant structure read** (D220). Unchanged in code, and here is work an adopter owes:
  `TextBracket::tag` is the brace's first token when it is a symbol, so for `{kMax > $x}` it
  names an operand, not the function (it is -1 for the corpus's braces, whose slot 0 is a
  number, variable or bracket). The same ten-line test on token kinds would give the bracket
  an `infix` operator index. No caller in this repository reads `tag`; the LSP and
  tree-sitter grammar outside it do.
- **Provenance and explain.** Unchanged: `source()` names the array's file and line, which
  the rewrite keeps.
- **Layers and `#remove`.** Unchanged: a layer replaces a command value whole, in either
  spelling.
- **`check` and the demand report.** Re-provided: the folding and ordering operators are now
  one overload each with a typed rest, `{+ number number number...}`, and reference.cpp's
  checker holds every literal of a typed rest to its type (reference.cpp:104-110, +5 lines), so
  `{+ 1 "x"}` and `{1 + 2 + "x"}` are still check errors. `{$x = 1}` is demand for `=`.
  Messages change (section 6). Since section 11 a command whose head no call can have (a
  number, string or property) is a check error, as it is at evaluation.
- **Shapes and the reference JSON.** Shapes: unchanged. The reference (baker.reference/1.2
  and docs/script-reference.md) changes content, not schema: seven entries (`+ - * < <= >
  >=`) read `{+ number number number...} -> value` or `{< number number number...} -> bool`
  where they listed `(int int) (float float)`, `+ - *` now say `-> value` where they said
  `-> int` and `-> float` per overload, and seven entries are new (`&& || ! += -= *= /=`).
- **The console's requests.** Unchanged: `(ID {command})` is read by the parser, so with the
  switch on `(7 {0.3 <= $v <= 0.7})` works; a reply's `text` prints the result, which is a
  command only when a function returns one, and then keeps its spelling.
- **The project file.** Unchanged: it holds no commands.
- **The binary cache.** Unchanged format: the header bit rides as D143's does, kVersion stays
  6 (tests/data/test_cache.cpp:594-604). A cache an infix build wrote, read by a build without
  the switch, prints prefix.
- **Docs and corpus.** docs/script-reference.md is regenerated for the vocabulary;
  docs/language.md would need a section on the rule; tests/script/check_expected.json moves
  one message (section 6). The corpus is rewritten in the tree (sections 7 and 11).

## 4. Cost, measured

Instruction counts are the unit (D169, D201): this machine was shared with seven other
prototypes and wall-clock rows moved by up to 65% on unchanged code (below).

**Parse.** `tools/count_instructions.py --case parse lookup set` against the session branch,
per 16 KB of the parse benchmark's config text (which holds no operator call), cachegrind:

| build | parse | lookup | set |
|---|---:|---:|---:|
| main (`8f15684`) | 1,845,499 | 203 | 623 |
| this branch, switch off (the default build) | 1,843,323 (-0.12%) | 203 | 631 (+1.28%) |
| this branch, switch on | 1,855,835 (+0.56%) | 203 | 623 |

- *Switch off*: nothing of the rule is compiled. The 2,176 fewer instructions (-0.12%) are
  code generation around what remains: the frame's two bools folded into one mark byte and
  the frame kinds reordered (not isolated further).
  The `set` row's 8 instructions are all inside glibc's `memcmp`, called as often on both
  sides, with `Path::matches`' own instructions identical: AVX2 `memcmp` takes a longer branch
  for one string's address, which moved with the binary's read-only data. The switch-on
  binary, with more code, reads 623. Placement, not work.
- *Switch on*: +10,336 instructions per 16 KB, about five per array close: the frame-kind test
  on each of the text's ~1,768 closes, the size, parity and slot-1 test on its commands, and
  one test of the result (per-line cachegrind with debug info). The rule itself never runs on
  this text. The predecessor's first version cost +1.26% (1,868,807: the rule inline in the
  hot loop, static-local `Symbol`s tested on every odd command, the parity computed for every
  token); moving the parity behind the quoted-token branch and the rule behind an inline
  slot-1 test gave +0.67%; out of line and cold, +0.73%; arguments by value, +0.77%; the
  frame kinds reordered so a brace is one comparison, +0.56%. D143's `#remove` was accepted at
  +1.1%.
- *Reading infix*: the same tree printed prefix and printed infix (`bench --operators`: the
  benchmark text with a band and `{+ {* $hits 10} $bonus}` added to every record, 999,700
  bytes either way, 12,460 operator braces) reads in 107,087,723 and 109,426,460 instructions:
  +2.18%, about 190 instructions for each brace the rule rewrites, at load only.

**Parse, wall-clock.** `tools/bench_compare.py --bench parse call --runs 5` (built while the
rule was unconditional): bench_parse 4.2 MB in 44.6 ms (30.7-47.8) on main against 31.6 ms
(31.3-33.2), that is 94 and 133 MB/s by the medians; the ranges overlap and no row is marked.
The brief's 209-213 MB/s is another machine. The experiment's own comparison
(`baker_syntax_curly-infix bench`, five interleaved rounds each):

| text | baker::read_string | the same reader on the infix text | time ratio |
|---|---:|---:|---:|
| the parse benchmark's 4 MB (no operators; the printed text lacks its comment lines) | 143.7-162.1 MB/s | 146.1-155.0 MB/s | 0.932-0.991 |
| `--operators`, prefix print against infix print, 5.3 MB | 101.7-129.1 MB/s | 95.6-132.6 MB/s | 0.868-1.064 |
| stacks/duel/metrics/duel.dta (2.9 KB, 11 infix braces) | 279-308 MB/s | 304-316 MB/s | 0.895-0.955 |

The ratios straddle 1.0 by more than the 2.18% the counts measure, so they show only that the
cost is below this machine's noise. The stacks' largest file (stacks/hearth/data/default.dta,
5.8 KB) includes others and does not read through `read_string` alone.

**Calls.** The vocabulary is unconditional, so its cost falls on every build. Instructions per
evaluation, a driver evaluating one command N and 2N times under cachegrind, built against the
fork point and against this branch:

| command | main | this branch | change |
|---|---:|---:|---:|
| `{+ 1 2}` | 633 | 450 | -28.9% |
| `{+ 1.5 2}` | 825 | 481 | -41.7% |
| `{+ $x 1}` | 650 | 577 | -11.2% |
| `{- 5}` | 503 | 379 | -24.7% |
| `{- 5 2}` | 633 | 502 | -20.7% |
| `{* 2 3}` | 633 | 450 | -28.9% |
| `{/ 1 2}` | 342 | 343 | +0.3% |
| `{< 1 2}` | 634 | 485 | -23.5% |
| `{<= $v 0.7}` | 687 | 617 | -10.2% |
| `{== $x 4}` | 501 | 499 | -0.4% |
| `{and {>= $v 0.3} {<= $v 0.7}}` (a band today) | 1,755 | 1,613 | -8.1% |
| `{<= 0.3 $v 0.7}` (the band as one call) | an error | 775 | -56% against the band today |
| `{+ 1 2 3}` | an error | 569 | |
| `{< 0 $v 1 2}` | an error | 913 | |
| `{not 0}`, `{if 1 2 3}` (controls) | 259, 340 | 258, 339 | -0.4%, -0.3% |

One overload with a typed rest beats main's two-overload sets (an overload set evaluates every
argument into a buffer and tries each overload; one overload reads its arguments in place). It
lost where the operator was already one overload: a variadic `/` cost 478 (+40%) and a chained
`==` 616 (+23%), so those keep main's two arguments. An unboxed `Number` (a double and an int
instead of a `Node`) was tried and was slower (`{+ 1 2}` 515). bench_call, wall-clock: the band
in one call 54.75 ns against 132.58 ns as three calls, `{+ 1 2}` 34.86 ns, and 0 allocations a
1,000 calls on every row; `baker_alloc_tests` passes.

**Lines.** `tools/count_lines.py`: the core goes from 15,190 to 15,477 of the 15,500 soft budget
(+287 of the 310 left; 23 remain), and to 15,494 with section 11's fix (6 remain). Data
+152: parse.cpp +56, text.cpp +36, text.hpp +24
(the switch and BAKER_COLD, moved there from script/internal.hpp's -5), write.cpp +31,
array.hpp +5. Script +135: builtins.cpp +124, script.hpp +8 and object.cpp +3 (`Raw<Var>::is_set`
and the property fix), reference.cpp +5, internal.hpp -5. The brief projected about +150; the
vocabulary alone is +124 where it projected +45 (the typed rest, the fold, the chain, four
assignments and three aliases). Adopted without the switch, without the ternary and without
`&& || !`, the projection is about +250 (the switch is about 8 lines, the ternary about 20 across
the rule, the shape test and the printer, the aliases 6). The experiment is 946 code lines
(main.cpp 245, syntax.cpp 474, syntax.hpp 54, tests.cpp 173) outside the budget; the tests,
fuzz harness, bench and parse_corpus changes are +508.

**Layout.** Node: unchanged (16 bytes). Array: one header bit, `kWrittenInfix` (4), beside
D143's two; thirteen bits spare. The parser's frame: unchanged size (48 bytes), its two bools
now one byte. No new allocation anywhere: the rule permutes the parser's scratch in place.

**The corpus's own lines and bytes** (the eleven samples, `to-new`'s output with one hand
edit, beside their originals):

| sample | lines before | lines after | bytes before | bytes after |
|---|---:|---:|---:|---:|
| stacks/contraption/metrics/puzzle.dta | 23 | 23 | 1,097 | 1,082 |
| stacks/cube/metrics/cube.dta | 6 | 6 | 359 | 344 |
| stacks/duel/metrics/duel.dta | 61 | 61 | 2,918 | 2,843 |
| stacks/hearth/metrics/hearth.dta | 36 | 36 | 1,709 | 1,703 |
| stacks/ledger/metrics/ledger.dta | 40 | 40 | 1,947 | 1,872 |
| stacks/ledger/data/default.dta (milestones) | 29 | 29 | 1,667 | 1,667 |
| stacks/stuntline/metrics/track.dta | 31 | 31 | 1,415 | 1,385 |
| stacks/wayfarer/metrics/world.dta | 51 | 51 | 2,223 | 2,208 |
| examples/calculator/functions.dta | 5 | 5 | 279 | 281 |
| examples/scripting/rules.dta | 11 | 11 | 404 | 405 |
| tests/corpus/ui_menu.dta | 49 | 49 | 1,275 | 1,244 |
| **total** | **342** | **342** | **15,293** | **15,034 (-1.7%)** |

## 5. Payoff, measured where possible

Over the 68 corpus files (`baker_syntax_curly-infix check`), the converter rewrites 76 of the
210 commands written at a file's top level or inside data (not inside another command), in 14
files: 99 operator calls written infix, 15 bands made one chain, 1 comparison turned to read
low to high beside its pair, 3 ternaries, 3 assignments, 2 nests of `+` merged (1 of them
reassociated), and none left prefix for a comment. Counted with a tokenizer that skips comments
and treats a string as one atom (experiments/syntax-curly-infix/syntax.cpp `text_size`):

| | brackets | atoms | characters (compact) |
|---|---:|---:|---:|
| the 76 changed commands, before | 324 | 371 | 2,047 |
| after | 252 (-22%) | 338 (-9%) | 1,776 (-13%) |
| all 210, before | 602 | 633 | 4,827 |
| after | 530 (-12%) | 600 (-5%) | 4,556 (-6%) |

The brief's projection (213 commands, brackets 370 to 334, atoms 768 to 734, characters 5,857
to 5,576) counted with another tokenizer; the direction and the size of the change agree.

- **A designer** reads a band the way the objective's `desc` says it: `(desc "Easy: a
  newcomer mashing wins about half the time")` over `(pass {0.3 <= $value <= 0.7})`
  (duel.dta:28-31). Each of the 15 bands goes from 6 brackets and 7 atoms to 2 and 5, and names
  `$value` once. Two mistakes become impossible or loud. A band whose halves name different
  variables, `{and {>= $value 0.3} {<= $valeu 0.7}}`, passes `check` and fails only when its
  second half is first evaluated; a chain names the value once, so it cannot be written. And a
  comparison's direction is on the page, `{$value >= 0.85}`, where `{>= 0.85 $value}` and
  `{>= $value 0.85}` differ by argument order alone. Mixing `<` and `<=` in one brace is a
  located error.
- **An engineer** gets a faster vocabulary in every build (section 4), `{$i += 1}` for
  `{set $i {+ $i 1}}`, and one operator word per operator where the overload sets listed two
  or four.
- **A tool** keeps reading the tree it read: the cache, the digest, `check`, shapes, explain
  and the console see the same arrays. Only text tools that do not use Baker's parser must
  learn the rule (section 3).
- **An agent** writes objectives the way it reads their specification, and its diffs stay one
  line: `to-new`'s rewrite of a whole file touches only the changed commands' lines (one line
  per command in every sample but Hearth's two-line sum), and a later edit of a bound is one
  token in either spelling.
- **What it does not buy**: lines (342 to 342 over the samples) and much size (-1.7% bytes).
  A call in the first slot reads worse: Ledger's milestones become `(when {{econ.count mill}
  >= 1})`, the same bytes with a doubled brace; the calculator gains two atoms (31 to 33) for
  its ternaries.

## 6. Objections

**Cost per byte (the data-oriented review).** "Every array close now pays for a rule almost
no brace meets." True, and measured: about five instructions a close, +0.56% of a parse with
the switch on, nothing with it off (section 4). The concession is that the cost is paid by
data that never uses the rule, which is what SKILL.md's "nothing paid for unless it is used"
forbids; the answer is the precedent and the bake. D143 accepted `#remove` at +1.1% on the
same terms (a feature most files never use, paid in the parse) once a +4.0% version was
redone, and this rule went from +1.26% to +0.56% by the same kind of redo (section 4). The
bake (M8) stores the expanded tree, so a shipping load reads no text and pays nothing.
Evaluation pays nothing either: the tree is the one it was.

**A second parser.** "D220 records that each tool wrote its own lexer." The rule is not a
second parser: it is one function in the one parser, and the lexer, the tolerant read and
the token kinds are untouched. But it is a second spelling, and every tool that reads text
without Baker's parser must learn it: the tree-sitter grammar, the LSP, and
`read_structure`'s `tag` (section 3). What they must learn is one rule with no table (odd
slots, one operator, the guard), where expr-precedence would have made each of them carry a
precedence table. Conceded in part: five places learn it, not none.

**Build time against run time.** The rewrite happens once per read, at load; the cache keeps
the result; nothing happens per frame. The vocabulary's change is per call and measured
(section 4): a chained band is one call where the `and` band was three.

**Solving a problem you don't have.** The measured payoff is modest. Lines do not move,
bytes fall 1.7% over the eleven samples, and the brackets and atoms saved are 22% and 9% of
the commands that change (section 5). What the rule buys is reading, which no benchmark
measures: 76 of 210 top-level commands are spelled as the designer's spreadsheet spells them.
The survey's pain is real and specific (15 bands, clamp, the loop counter), and the stacks'
objective files are written by the people the harness vision names. Whether that is worth a
second spelling is the owner's call, not a number's.

**One spelling** (constraint 1). With the rule, one tree has two spellings, and the mark
decides which the printer writes. The printer stays faithful and canonical per tree (a parsed
tree prints as written, a built one prefix), equality and the digest ignore the mark, and the
round trip holds for every tree. The cost is diffs: an agent that rewrites a prefix band as
infix in a pull request changes a line that means the same thing. The converter makes that a
one-time, whole-file change; a project picks its spelling.

**DTA heritage: the corpus.** A raw Harmonix brace changes meaning only if it has an
operator symbol at slot 1 and a number, variable or command among its operands. The survey
counted none in this repository's 68 files, and this branch confirms it: no brace in today's
corpus reads as infix (`check`, "braces today's corpus reads as infix: 0"). The D101 gate
(464 raw files) is the real test and was not run here: those files are not in the repository
(D3), and the brief forbids consulting Harmonix material. `baker_parse_corpus` now prints
"infix rule: N braces fired, M held back by the guard" (tools/parse_corpus.cpp:59-84, 217), so the
owner's run gives the number. Over this repository it reads 0 and 0. If the gate finds a
firing brace, the brace changes meaning silently, which is the one thing D101 refused, and
the rule must not ship as it is.

**DTA heritage: the modding community.** A file written infix is Baker's, not DTA's: another
DTA reader takes `{0.3 <= $value <= 0.7}` as a call of `0.3`. Rock Band's config files, the
seam the spec names, hold no infix and read as they did. Baker does not promise DTA
compatibility (README), and a project that wants files other DTA tools read writes prefix,
which the printer gives back.

**Error messages move.** Replacing the overload sets changes texts a user or a test may hold.
The reader's `{+ 1 two}` was "no overload of '+' takes (int symbol); it has (int int) (float
float)" and is "argument 2 of '+' is symbol, not number"; check's `{+ 1 two}` was "'+' is
given (int symbol); it takes (int int) (float float)" and is "... it takes (number number
number...)"; check's `{+ 1}` was "no overload of '+' takes 1 argument" and is "'+' takes at
least 2 arguments, given 1" (tests/script/check_expected.json). The brief expected an arity
error such as `{<= 1}` to keep its text; it does not, because on main that error came from
the overload set ("no overload of '<=' takes (int); it has (int int) (float float)") and now
comes from the one overload's arity ("'<=' takes at least 2 arguments, given 1"). Three
existing test expectations were updated for these (tests/script/test_eval.cpp:73,
tests/script/test_check.cpp:108-109, check_expected.json), and nothing else in the existing
suite moved.

## 7. Migration

**Today's files.** `baker_syntax_curly-infix to-new <file>` rewrites a file's operator calls
in place by their spans: every other byte, comment and blank line is kept, and each changed
command keeps its line breaks between operands (experiments/syntax-curly-infix/syntax.cpp).
It rewrites chains, bands (only when the banded value is a variable or property and the
bounds are not commands, since the chain evaluates the value once and the `and` twice),
turned comparisons beside a bound (only when neither side is a command, since turning
reorders their evaluation), one-line `if`s with an else, `{set $x {op $x n}}`, and nests of
one operator (anywhere for `+` and `*`, leftmost for `-`; a nest that is not leftmost
reassociates, which is exact for integers and can move the last bit of a float sum: the
corpus's one case, Hearth's four event counts, is integers). `check` proves every corpus
file's rewrite reads back to the expected tree, marks included, and evaluates each band,
turned comparison, assignment and merged nest against the command it replaced. `to-dta`
prints any file prefix, but through the tree, so comments go; a comment-keeping reverse is
the same converter run the other way and was not built.

**Side by side.** Both spellings are valid forever, and the mark keeps each file's choice, so
there is no flag day: a project converts a file when a person next edits it, or never.

**The one-way rule, packs and layers.** Untouched: the rule is lexical and the tree is the
same, so a pack's calls, its level and the one-way check (D80) see what they saw. A layer's
command value replaces whole in either spelling.

**Live-ops overlays** are the hard case. An overlay is text read by the game's own Baker, so
an overlay written infix reaches a build without the rule as a call of `0.3` (an evaluation
error, "a command starts with a function or an object, not a float") or, for a brace of
symbols, nothing at all. Until every shipped build reads infix, overlays must be written
prefix; the printer writes prefix for any tree (`print_prefix` in the experiment), and the
live-ops tooling would have to say which spelling a target reads. The same holds for packs
sent to older builds. This is the strongest reason to adopt the rule early or not at all.

**The in-tree corpus.** The brief asked for the 16 bands and the calculator to be rewritten in
the tree. At first the switch was off in the default build, so the eleven samples beside their
originals stood in. Section 11 did it for real: the switch is on by default on this branch,
and `to-new` rewrote in place every file it changes, fourteen: the seven stacks' metrics,
Ledger's milestones and its unlocks (with the four cells of `economy.xlsx` they are exported
from), the calculator, the scripting example's rules and three test fixtures. Each is proved
against git's copy of its original (section 8), and the stacks' results do not move. With the
switch off those files still parse but no longer mean what they say: each infix brace is a
command of its first operand, an error when evaluated. That is the cost of adopting the
spelling; nothing builds around it. The rewrite reverses by `git revert`.

## 8. What the prototype proved, and did not

**Built and passing.**
- The rule in the core's parser and printer behind `BAKER_CURLY_INFIX`, the vocabulary
  unconditionally, the written-infix bit, the typed-rest check, and a fix the vocabulary
  exposed: on main a `[property]` argument of a function with one overload is never evaluated
  (`{% [hp] 3}` in a handler fails "argument 1 of '%' is property, not int"); `Args` now
  evaluates a command, a property or a variable alike, with one comparison
  (include/baker/script.hpp:1025-1033, 1040, 1052, 1075; tests/script/test_object.cpp, "a
  [property] argument is evaluated for a function of one overload"). It stands on its own, whatever is decided here.
- With the switch on (the experiments build, `cmake -DBAKER_BUILD_EXPERIMENTS=ON`): all 23
  ctest suites pass, among them the new cases in test_parse (a chain to its prefix tree with
  its spans in order, the ternary and its empty `if` span, the mixed error's text, line and
  column, the guard, quoting, macros, an `#autorun` brace), test_write (prefix quoting of
  `{+ '-' 1}`, `{1 '?' 2 ':' 3}`; `{- - -}` and `{f + 1 2}` printed as today; a flagged tree
  prints infix and re-reads flagged; `/*` never printed between operands; the wrapped form),
  test_cache (the bit rides), test_eval (folds, chains, a counting native evaluated once,
  `+=` on variables, `=` undefined, the ternary's laziness, the clamp), test_object (`+=` and
  `-=` through `[hp]` in a handler), test_check (literal types through a variadic `+`, `=` as
  demand, both spellings giving one report), and the experiment's 13 cases.
- With the switch off (the default options until section 11): all 21 suites passed, including
  a case that reads `{0.3 <= $value <= 0.7} {+ - 1} {$c ? 1 : 2}` and prints it back byte for
  byte as main does. Since the in-tree rewrite, see below.
- `baker_syntax_curly-infix check`: the corpus round trip with every command that can be
  infix printed infix, 68 files, 0 failures; the converter over all 68, each file's text read
  back to the expected tree with the same marks, 0 faults; no brace in today's corpus reads as
  infix; 19 of the 20 rewrites that rest on the vocabulary evaluated against what they
  replaced over values around each bound and agree (the twentieth, Hearth's sum, calls host
  functions).
- The eleven samples read to the converter's tree of their originals, marks included; they
  are the tree's own files now (below).
- clang-format and clang-tidy clean on every changed file.
- libFuzzer (ASan and UBSan, switch on, CI's seeds plus the eleven infix samples, the
  dictionary with `?`, `:`, `<=`, `+=`, `&&` added; the harness now also checks that the
  written-infix marks survive print and re-read), two runs, 2,220,593 executions in all:
  - The first ran 1,331,783 executions (530 a second) and stopped on an input the harness
    refused: `(key #remove)`. The harness reads every literal token alone, and D143's marker
    reads only in a key's one-value slot. Main's harness refuses `(a #remove)` the same way
    (checked against the fork point), so the gap is the harness's, not the rule's; main's
    two-minute CI run and its dictionary, which lacks `#remove`, rarely build that input.
    The harness now skips the marker's token (fuzz/parse_harness.cpp), and
    tests/fuzz_regressions/bare_remove_marker.bin holds the input.
  - The second resumed from the first's corpus for 1,200 seconds: 888,810 executions, 1,582
    corpus entries, 2,125 covered edges (from 1,398 at the seeds), no failure, no sanitizer
    report, no artifact.
  - The brief asked for 10M executions; 2.2M were run. At this machine's rate the rest is
    about three hours of one core.

**The in-tree rewrite** (section 11, `b530d85`).
- *Files.* `to-new` in place: 14 files, 76 of their 385 lines changed, 17,449 bytes to
  17,176 (-1.6%), every line count kept. Lines changed: the stacks' metrics, puzzle.dta 5,
  cube.dta 1, duel.dta 11, hearth.dta 10, ledger.dta 8, track.dta 7, world.dta 12; Ledger's
  default.dta 5 (the milestones) and economy.dta 4 (the unlocks, with the four cells of
  `economy.xlsx` it is exported from, so `export_economy.py --check` holds); the
  calculator's functions.dta 3; the scripting example's rules.dta 2; tests/corpus/ui_menu.dta
  4, tests/corpus/scripts.dta 2, tests/host/stack/metrics/objectives.dta 2. The one hand
  edit is Hearth's continuation line, aligned as its sample was. The converter now changes
  no corpus file, and the corpus reads 85 braces as infix.
- *The same tree.* `baker_syntax_curly-infix same <file> <original>` proves a file against
  its original's text; `prove_rewrite.sh` runs it for every `.dta` changed since `15ce5b6`,
  from git's copy, and the experiment's tests do the same from samples/, which now holds
  those fourteen copies. For each: the original reads with no brace the rule rewrites, so
  as it read with the switch off; the rewritten file reads to the converter's tree of the
  original, written-infix marks included; and each comment keeps its text and line. Five
  files (the calculator, the rules, Ledger's two data files, the host fixture) read to the
  original's tree exactly. The other nine differ from it only by the 20 rewrites that rest
  on the vocabulary, 15 bands made chains, 1 comparison turned, 3 assignments and 1 merged
  nest: each undone, the tree is the original's exactly, and 19 evaluate as what they
  replaced over values around each bound; the twentieth, Hearth's sum of event counts, calls
  the game's functions, and Hearth's runs below give the same values. So the tree is the
  same for every respelling, and the same up to those 20 equivalences.
- *The stacks*, built and tested as CI's `stacks` job (gcc-13, Debug, Ninja, `ctest
  --test-dir build-stacks --output-on-failure`): 30 of 30 tests pass before (`15ce5b6`, the
  switch off by default) and after (the rewrite with the switch on, and again after section
  11's fix). Each stack's `sim --seeds 1-5 --json --results DIR` and `check`, before and
  after: 130 runs, 385 objective verdicts, all passed, 7 of 7 medians passed, check passed in
  all seven; sim.json, results.dta and check.json are the same byte for byte, values
  included, and each stack's `compare` reports nothing broke.
- *Baker's builds.* The default build (CI's gcc-13 Debug job, warnings as errors, switch on):
  21 of 21 suites. The experiments build (Release): 23 of 23, the experiment's 14 cases
  among them. With the switch off, 17 of 21 pass: the four that read rewritten files fail
  (baker_example_calculator, baker_example_scripting, baker_host_commands, and
  baker_script_tests's check over tests/corpus/scripts.dta, whose `{[height] += $n}` section
  11's check reports as a property head; the suite's other 131 cases pass). The stacks with
  the switch off were not run; their objectives would fail at evaluation the same way.

**Not done, or not proved.**
- The D101 gate over the 464 raw Harmonix files: run after the prototype, 0 fired (section
  11). Section 11's refusal of malformed even braces came later; `baker_parse_corpus` now
  counts the files the rule refuses, and the gate has not been rerun with it.
- The local matrix (`tools/local_matrix.sh`: debug, C++23, shipping, sanitizers, Clang,
  data-only, plain refcounts, EASTL, the ImGui harness) and CI's MSVC, libc++ and TSan jobs:
  not run, in the prototype or in section 11's work (the machine was shared with five
  libFuzzer processes). Release GCC with the switch on and off, Debug GCC 13 as CI's default
  job, the stacks as CI's job, and Clang with ASan and UBSan for the fuzzer were built.
- `read_structure`'s `tag` for infix braces, the tree-sitter grammar, the LSP, and a section
  of docs/language.md: not written. stacks/agent.md and the stacks-agent skill now say how a
  brace reads infix and what is an error.
- Wall-clock evidence: this machine could not resolve differences under about 10%; every
  claim of cost rests on instruction counts.
- A comment-keeping reverse converter (infix to prefix by spans): not built.

## 9. Open questions for the owner

1. **Spend a header bit on the written spelling?** Recommended: yes. Without it, promote,
   `write_file` and an inserted child canonicalize a designer's band to prefix on the first
   write, and the spelling becomes something a file has until a tool touches it. The header
   has thirteen bits spare after this one, D143 set the precedent, and the bit costs no
   format change. The alternative, canonical prefix, makes infix a read-only convenience.
2. **Keep the guard, or let the gate decide?** Recommended: keep it. It is three lines and
   it is what makes the rule safe for raw DTA without the gate; the gate then confirms. With
   it, `{a - b}` is a command whatever the gate finds.
3. **Two-tier precedence (comparison chains inside `and`/`or` chains)?** Recommended: no,
   nesting only. The corpus has one mixed case (stuntline's `{0 < $value}` beside
   `{$value <= ...}`) and it reads well nested; two tiers bring back a precedence table for
   every tool.
4. **Should `+=` store through `[prop]`?** Recommended: yes, as built (`set` already does);
   `$this.k` when dollar-places lands, by the same `Raw<Var>` parameter.
5. **Keep `&& || !`?** Recommended: drop them. The converter never needed them (it keeps
   `and`/`or`, which read fine around infix comparisons), and they are three words for
   three words a 25-word vocabulary already has. Six lines.
6. **The typed-rest literal check?** Recommended: yes. Five lines in reference.cpp keep
   `{+ 1 "x"}` a check error once `+` is variadic.
7. **Keep the ternary?** Recommended: drop it. `{if c a b}` is already one word; the
   ternary adds two reserved symbols, an empty-span head, a branch in the rule and the
   printer, and atoms (the calculator's three commands went from 31 atoms to 33). Its one
   strong case is the nested clamp.
8. **Do `+` and `*` take two arguments or more, or one?** Recommended: two, as built, so
   `{+ 1}` stays the error `check` reports in tests/script/check_expected.json.
9. **Chain `==` and `!=`, and fold `/`, as the brief proposed?** Recommended: no, as built.
   Each was one overload on main and a rest parameter made it 23% to 40% dearer a call; no
   corpus site chains them, and `{a == b == c}` is a located error in `check`, not a silent
   misreading.
10. **Adopt the vocabulary without the rule?** Recommended: yes, now: chained comparisons
   make a band one call in prefix too (`{<= 0.3 $value 0.7}`), the operators lose their
   overload sets, and the single-overload property fix stands on its own (section 8).

## 10. Recommendation

Adopt in part. Take the vocabulary now: it is faster than main on every operator it touches
and equal on the rest (`{+ 1 2}` 633 to 450 instructions, a band in one prefix call 775 where
`and` of two comparisons is 1,755), it makes `{<= 0.3 $value 0.7}` a band in today's syntax,
it brings `+=` and its family, and it carries the property-argument fix main needs anyway; drop
`&& || !`. Take with it section 11's check of a head no call can have (a number, string or
property), which fails at every evaluation and is independent of the spelling.

The reader rule's measurable conditions are met on this branch: D101's raw gate read 0 fired
braces, malformed infix is an error at the brace that names the fix, and the tree's own files
are rewritten, proved against their originals, with every stack's results byte for byte as
they were (section 11). What remains is the review's reading trial: whether the people who
write objectives read `{0.3 <= $value <= 0.7}` better than the prefix chain `{<= 0.3 $value
0.7}`, which is the same tree with fewer atoms; this branch's metrics files are its material.
If it does, adopt the rule without the ternary (the calculator's and the rules' three
ternaries go back to `if`): it costs +0.56% of a parse (about five instructions an array
close, below D143's accepted +1.1%) and about 190 instructions for each brace written infix,
and it changes nothing a tool reads from the tree. The adopting change owes a D-entry, the
switch deleted, `read_structure` and the text tools taught the rule with section 11's clause
for even braces, docs/language.md's section, the gate rerun with its refusal counter, and the
cost section 7 names: a file written infix means something else to a build without the rule,
so overlays and packs for older builds stay prefix. If it does not, the vocabulary alone gives
the bands as prefix chains, and the rewrite reverses by `git revert`.

## 11. Verification, the raw gate, and review

*Added after the prototype, from the session's review round; `dev/syntax-ideas.md` on
`claude/fervent-cray-l1kyf0` compares all eight branches.* An agent independent of the
prototype rebuilt this branch, reran its tests and reproduced its numbers; a reviewer
judged it under the data-oriented lens (`.claude/skills/acton-review/`) and the tool
author's; and D101's raw gate (464 raw Harmonix files, `baker_parse_corpus --roundtrip
--milo-paths` over a local clone of mariteaux/milo-script-library, nothing from it in the
repository, D3) was run on the branch's build. Where a finding below disagrees with an
earlier section, this section is the later word.

- **Reviewed at** `ed037c5`. The later commits add only the fuzz harness's `#remove` fix,
  its regression input and the fuzz record in section 8.
- **Verifier.** Builds; 23 of 23 suites with the switch on, 21 of 21 off; 13 experiment
  cases. Every cachegrind count, line count, sample and converter count reproduces
  exactly; the per-call deltas reproduce with an independent driver (the branch does not
  commit its own, so that table cannot be rerun from the branch alone). Corrections: the
  `--operators` instruction row was measured on the first 999,700 bytes of the 5.3 MB
  print, which the note does not say; the `bench_compare` row came from an earlier commit
  and was not reproduced; wall-clock ratios differ within noise.
- **Raw gate.** Switch on: 464/464 parsed, 0 round-trip failures; the infix rule fired on
  **0 braces**, 0 held back by the guard. Section 10's condition for the reader is met.
- **Technical director, 3 of 5, adopt in part.** The infix tree is the prefix chain's
  tree, so every saving belongs to the vocabulary: over the 76 rewritten commands, prefix
  chains use the same 252 brackets and fewer atoms (318 against 338). The reader buys
  reading order alone, unmeasured, for most of the remaining budget, and a second spelling
  every text tool must learn, the stacks' agent docs included.
- **Tool author, 3 of 5, adopt in part.** `check` passes malformed infix:
  `{0.3 <= $value 0.7}`, `{$value <= 0.7 0.3}` and `{$value <=}` give no error, where the
  prefix typo is one.
- **Fuzzing.** Section 8 records it: 2,220,593 libFuzzer executions with the switch on,
  whose one stop was main's harness defect for `(key #remove)`, fixed on this branch.
- **Recommendation after review.** The vocabulary now, as its own step with the property
  fix; the infix reader held until `check` rejects malformed infix and a trial shows it
  reads better than prefix chains.

### The in-tree rewrite and malformed infix

*After the review, on this branch: `b530d85` (the switch on by default, the tree's files
rewritten), `4115c52` (malformed infix), and the commit of this subsection. Measured against
`15ce5b6`, the commit before them.*

- **The rewrite.** Section 8 gives the files, the proof and the results: 14 files, 76 of
  385 lines; every respelled command reads to its original's tree, and the 20 rewrites
  that rest on the vocabulary are undone exactly and agree by evaluation; the stacks' 30
  tests and 130 runs give the same results byte for byte before and after.
- **Malformed infix, as it was.** `{0.3 <= $value 0.7}`, `{$value <= 0.7 0.3}` and
  `{$value <=}` have an even number of nodes, and the rule took only odd braces (rule 1), so
  each closed as written: a command whose head is `0.3` or `$value`. Evaluation fails ("a
  command starts with a function or an object, not a float"; `$value` holds a float, not an
  object), but `check` returned at once from a command whose head is not a symbol, taking it
  for an object computed at run time, so nothing reported them before a run. `{$value <= 0.7
  0.3 1}`, odd, was caught, but as "mixed operators", which it is not.
- **The fix.** Rule 5 now also takes an even brace whose head is a number, variable,
  property or command and whose slot 1 is an operator or `?`: no head-first call is written
  so, and a quoted operator keeps one (`{$obj '<=' 1 2}`). Each is ErrorCode::Syntax at the
  brace's line and column, with a message that names the fix: the first two "infix {...}
  needs '<=' between every two operands", the third "infix {...} ends with '<=': write an
  operand after it". Siblings: the same for `{0.3 <= $value <=}`, `{[hp] -= 5 1}`,
  `{{econ.count mill} >=}` and `#autorun {$i +=}`; "'?' in {...} takes {c ? a} or {c ? a :
  b}" for `{$c ? 1 2}` and `{$c ?}`; and `{$value <= 0.7 0.3 1}` now says the operator is
  missing. A call's head before an operator (`{f + 1 2}`, `{f <=}`), prefix calls (`{<= 0.3
  $v 0.7}`, `{- 5 2 1}`) and object calls with a method word read as before. The printer
  quotes slot 1 of such a command built in C++, `{$obj '<=' 1 2}`, so every tree still
  round-trips. `check` now reports a command whose head no call can have (a number, string or
  property), as evaluation does, and with the switch on and an operator in the brace adds
  "infix needs an operator between every two operands": that covers `{0.3 $value <= 0.7}`,
  whose slot 1 is not an operator. In a stack the malformed band fails the load: `stacks_duel
  check` exits 2 with the problem at metrics/duel.dta:31:10, the brace, and `sim` exits 2.
  `baker_parse_corpus` counts the files the rule refuses, for the gate. Tests: test_parse
  (each form, its message, line and column, and what stays a command), test_write (the
  quoted print), test_check (the head check, with the switch on and off).
- **Its cost.** `tools/count_instructions.py --case parse lookup set`, cachegrind, per 16 KB
  of the parse benchmark's text: with the switch on, both sides, against `b530d85`
  (15ce5b6's code), parse 1,855,835 and 1,855,835 (0.00%), lookup 203 and 203; with it off,
  against `15ce5b6`, 1,843,323 and 1,843,323, the change compiled away. `set` reads 623
  against 631 with the switch on, all of it in glibc's `memcmp` with Baker's functions the
  same instruction for instruction: the placement section 4 describes. Per brace, on texts of
  1,000 records of one shape: +9 instructions for an even brace of two nodes or more (its
  head's type is tested), +5 for a prefix band `{<= 0.3 $v 0.7}`, 0 for an odd call, -7 for
  an infix band, -2 for a parenthesized array; on the benchmark's text these net to zero. The
  messages are built only on the error path. Lines: the core 15,477 to 15,494 of 15,500,
  data +8 and script +9.
- **What text tools learn.** One clause more: an even brace whose head is an operand and
  whose second element an operator is an error, not a call.
- **Fuzzing.** CI's fuzz build with the switch on (`build-cifuzz`: Clang 18, RelWithDebInfo,
  ASan and UBSan), seeded with tests/corpus, tests/fuzz_regressions, the fourteen originals
  and the fourteen rewritten files (41 inputs), CI's dictionary and flags, 600 seconds niced
  beside five other fuzzers: 595,779 executions (991 a second), coverage from 1,453 edges at
  the seeds to 2,104, 1,625 corpus entries, no failure, no sanitizer report, no artifact. The
  harness's round trip covers the new quoting: a parsed `{$a '+' 1 2}` must print and re-read
  as itself.
- **The review's condition.** "The infix reader held until `check` rejects malformed infix
  and a trial shows it reads better than prefix chains." The first half is met: the three
  forms and their siblings fail the load at the brace, which `check` and `sim` report, and a
  head no call can have is a check error. The second is not: no reading trial was run, and
  the rewritten metrics are its material. Section 10 now says so.

### The completion round

*Added in the session's last pass over every branch, which took each to spec §8's fuzz bar
and CI to green on every job; `dev/syntax-ideas.md` on `claude/fervent-cray-l1kyf0` records
the round for all eight.*

- **CI.** Every job passes on the final head, MSVC, libc++, TSan and the stacks included. The
  release dry run had failed on every commit since the prototype's first: eleven shipped
  comments, test names and a CMake comment cited this note in running text, which
  `tools/release/publish.py check` refuses. They were reworded (`58cbf46`).
- **The raw gate on the final code**, with the counter section 11 added: 464/464, 0
  round-trip failures, 0 braces fired, 0 held back, 0 files refused as malformed infix.
- **Fuzzing, 10,000,000 executions on the final parser.** With the reader on, built as CI's
  fuzz job, and seeded with the corpus, the regressions and every file the rewrite changed,
  in runs each resumed from the last one's corpus: a run stopped to apply a harness fix, or
  by one of two machine restarts, resumed from its last logged count, so no execution is
  counted twice. None found a failure in the parser. The 2,220,593 executions of section 8
  ran before the malformed-infix change and are not counted.
- **A harness fix from another branch.** main's harness skipped a tree over 100,000
  expanded nodes but read its print back with a 65,536-node limit, so a tree between the two,
  which macro sharing makes from a few hundred bytes, failed a property the parser never
  broke. indent-lines' run found it; the fix and `macro_sharing_budget.bin` are here too
  (`251bec7`).
- **A second harness skip, for a defect on main.** main's printer cannot spell a tree whose
  first form is `(origin "...")` held as data, a second origin, since it prints as the
  declaration the reader consumes (D91). The parser reads it right. libFuzzer stopped on it
  here after 5.2 million executions; the harness now skips that tree, as indent-lines'
  does, with the same regression input (`617004a`). The parser did not change, so the count
  carried on.
