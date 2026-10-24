# Curly-infix: a brace of one operator reads head-first

*A prototype on `claude/fervent-cray-l1kyf0-syntax-curly-infix`, merged from the proposals
curly-infix and expr-precedence (dev/syntax-survey.md on the session branch, "EXPRESSIONS").
The reader rule and its printer sit behind a compile-time switch, `BAKER_CURLY_INFIX`, which
is on where the experiments build and off in the default build; the vocabulary (variadic and
chained operators, `+=` and its family) is unconditional. Every number below was measured on
this branch on the shared 4-core machine the brief names, against the session branch
(`8f15684`, whose code is the fork point `3817213`).*

## 1. Thesis

Inside `{...}` only, a brace of an odd number of nodes, three or more, whose odd slots hold
one and the same bare operator symbol, and which has a number, variable, property or command
among its operands, is read as the prefix command it spells: `{0.3 <= $value <= 0.7}` is the
tree `{<= 0.3 $value 0.7}`, `{$i += 1}` is `{+= $i 1}`, and `{c ? a : b}` is `{if c a b}`.
An operator at slot 1 that fits neither shape is a located syntax error ("nest braces"). The
operators become words that take any number of arguments (`+ - * /` fold left; `< <= > >=
== !=` test every adjacent pair, evaluating each argument once), and `+= -= *= /=` join them.
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
not operator bytes (src/data/text.hpp:76-92).

**The rule** (src/data/parse.cpp:593-683), applied when a `{` frame, a command or an
`#autorun` brace, closes over nodes k0 .. kn:

1. *Eligible*: n+1 is odd and at least 3; no odd slot was written as a quoted symbol or a
   macro use, and no macro in the brace expanded to more than one node (a frame mark set in
   `symbol()`, parse.cpp:531-540); and k1 is a Symbol whose first byte is an operator byte or
   `?` (the inline pre-test, parse.cpp:637-642). Anything else closes as today.
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
5. *Mixed*: k1 is an OPERATOR or `?` and neither shape holds. ErrorCode::Syntax, "mixed
   operators in {...}: nest braces to say which applies first", at the brace's line and
   column.

**Mapping.** The result is a Command whose slot 0 is the operator Symbol or `if`, and whose
operands are the nodes they already are: Int, Float, Symbol, String, Var, Property, Command,
Array. No Type is added, Node stays 16 bytes, the Array header stays 24 bytes, and the only
new state is the header bit `kWrittenInfix` (include/baker/array.hpp:540-546, 580), which
equality, the digest and evaluation ignore and the cache carries as it carries D143's bit.

**The printer** (src/data/text.cpp:126-150, src/data/write.cpp:38-60). A command marked
written infix whose shape still fits prints `{k0 op k2 op k4}` or `{c ? a : b}`; when it does
not fit the width, it breaks one operand a line with the operator ending each line, as the
pretty printer breaks any array. A command printed prefix that the reader would take infix
(an odd count, an operator or `?` at slot 1, an operand among the even slots) quotes its
odd-slot operators: `{+ '-' 1}`, `{1 '?' 2 ':' 3}`. Every other tree prints byte for byte as
on main, so `parse(write_dta(tree))` stays structurally equal for every tree, C++-built ones
included (constraint 1), and the bytes of check's repeat forms, results.dta and every
existing reply do not move.

**The vocabulary** (src/script/builtins.cpp:125-247, 315-419). `+ *` take two numbers or more
and `/` two or more (`{+ 1}` stays an error, as it is today); `-` takes one or more (one is
negation). Ints stay ints, wrapping, until a float joins; a quotient is always a float.
`< <= > >=` take two numbers or more and `== !=` two values or more, each argument evaluated
once, stopping at the first pair that fails, so `{0.3 <= {f} <= 0.7}` calls `f` once.
`+= -= *= /=` take a place (`$var`, or `[prop]` in a method, as `set` takes) and a number:
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
  rule out of line and cold (parse.cpp:593-683). With the switch off all of it compiles away.
- **Printer.** Re-provided: `infix_shape` decides chain, ternary, quoted prefix or plain
  (text.cpp:126-150), and `put_flat`/`put_pretty` write the operator between operands.
- **Write-back** (D33, D34, D132, D183, D226). Unchanged in code. A command value is replaced
  wholesale, as config_write.cpp does today, and printed by the printer, so a band written
  infix stays infix when promote or `write_file` writes it. Each operand keeps its own span
  (tested, tests/data/test_parse.cpp:268-312), so a one-token edit of a bound replaces one
  token. Two spans are new in kind: a chain's head carries its first operator's span, so a
  tool that replaced it in a three-operand chain would make a mixed-operator error, loud and
  located, never a silent change; a ternary's `if` has an empty span and is refused (parse.cpp
  ternary branch, test_parse.cpp's "a ternary's if is written nowhere").
- **Edit-mode source maps** (D33, D128). Re-provided: the spans are permuted with the nodes
  and the dropped operators' spans removed, so `spans(a).size() == a.size()` holds. The
  libFuzzer harness checks every literal span against its token, and now also that the
  written-infix marks survive print and re-read (fuzz/parse_harness.cpp:51-63, 113-118).
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
- **`check` and the demand report.** Re-provided: the operators' signatures are now one
  overload each with a typed rest, `{+ number number number...}`, and reference.cpp's checker
  holds every literal of a typed rest to its type (reference.cpp:104-110, +5 lines), so
  `{+ 1 "x"}` and `{1 + 2 + "x"}` are still check errors. `{$x = 1}` is demand for `=`.
  Messages change (section 4).
- **Shapes and the reference JSON.** Shapes: unchanged. The reference (baker.reference/1.2
  and docs/script-reference.md) changes content, not schema: the twelve operator entries
  read `{+ number number number...} -> value` where they listed `(int int) (float float)`
  (ten entries: `+ - * / < <= > >= == !=`), and seven entries are new (`&& || ! += -= *= /=`).
- **The console's requests.** Unchanged: `(ID {command})` is read by the parser, so with the
  switch on `(7 {0.3 <= $v <= 0.7})` works; a reply's `text` prints the result, which is a
  command only when a function returns one, and then keeps its spelling.
- **The project file.** Unchanged: it holds no commands.
- **The binary cache.** Unchanged format: the header bit rides as D143's does, kVersion stays
  6 (tests/data/test_cache.cpp:594-603). A cache an infix build wrote, read by a build without
  the switch, prints prefix.
- **Docs and corpus.** docs/script-reference.md is regenerated for the vocabulary;
  docs/language.md would need a section on the rule; tests/script/check_expected.json moves
  one message (section 4). The corpus is not rewritten in the tree on this branch (section 7).

## 4. Cost, measured

PLACEHOLDER-COST

## 5. Payoff, measured where possible

PLACEHOLDER-PAYOFF

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
"infix rule: N braces fired, M held back by the guard" (tools/parse_corpus.cpp:59-84), so the
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
one operator (anywhere for `+` and `*`, leftmost for `-` and `/`; a nest that is not leftmost
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
the tree. With the rule behind a switch that is off in the default build, an infix file in
`stacks/` or `examples/` would break the default build and the stacks' own CI, so they stay
prefix here; the eleven samples beside their originals stand in, and `tests.cpp` proves each
reads to the converter's tree of its original. On adoption the rewrite is one `to-new` per
file and reverses by `git revert`.

## 8. What the prototype proved, and did not

PLACEHOLDER-PROVED

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
9. **Adopt the vocabulary without the rule?** Recommended: yes, now: chained comparisons
   make a band one call in prefix too (`{<= 0.3 $value 0.7}`), the operators lose their
   overload sets, and the single-overload property fix stands on its own (section 8).

## 10. Recommendation

PLACEHOLDER-RECOMMENDATION
