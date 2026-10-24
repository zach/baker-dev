# syntax-curly-infix

A `{...}` whose odd slots hold one operator reads head-first: `{0.3 <= $value <= 0.7}` is
the command `{<= 0.3 $value 0.7}`, `{c ? a : b}` is `{if c a b}`. The rule lives in the
core's reader and printer on this branch (`src/data/parse.cpp`, `src/data/write.cpp`), so
the reader here is `baker::read_string`; this directory holds the converter that rewrites
a DTA file's operator calls as infix in place (`to-new`), the proof of a rewritten file
against its original (`same`, and `prove_rewrite.sh` over git's copies), the corpus checks
(`check`), the parse benchmark (`bench`), and the originals of the rewritten files. The
design note is `dev/syntax-curly-infix-design.md`.

## The in-tree rewrite

On this branch the switch is on by default and the fourteen files the converter changes are
written infix in the tree: the seven stacks' metrics, Ledger's milestones
(`stacks/ledger/data/default.dta`) and its unlocks (`stacks/ledger/data/economy.dta`, and
the sheet it is exported from), the calculator, the scripting example's rules, and three
test fixtures (`tests/corpus/ui_menu.dta`, `tests/corpus/scripts.dta`,
`tests/host/stack/metrics/objectives.dta`). Each is `to-new`'s output; the one hand edit is
the alignment of Hearth's continuation line.

`samples/<name>.original.dta` is git's copy of each file from before the rewrite (`15ce5b6`).
`tests.cpp` proves each rewritten file against it: its original reads with no brace the
rule rewrites (so as with the switch off); the rewrite reads to the converter's tree, its
written-infix marks included; that tree, with each rewrite that rests on the vocabulary
undone (15 bands, 1 turned comparison, 3 assignments, 1 merged nest), is the original's
exactly, and each of those rewrites evaluates as what it replaced (all but Hearth's sum,
which calls the game's functions); and every comment is on its line and the line count kept.
Five files read to the original's tree with nothing undone. `prove_rewrite.sh <baker_syntax_curly-infix> [BASE]` does the same from git's history
directly, for every `.dta` changed since BASE.

| file | lines | lines changed | bytes before | bytes after |
|---|---:|---:|---:|---:|
| examples/calculator/functions.dta | 5 | 3 | 279 | 281 |
| examples/scripting/rules.dta | 11 | 2 | 404 | 405 |
| stacks/contraption/metrics/puzzle.dta | 23 | 5 | 1,097 | 1,082 |
| stacks/cube/metrics/cube.dta | 6 | 1 | 359 | 344 |
| stacks/duel/metrics/duel.dta | 61 | 11 | 2,918 | 2,843 |
| stacks/hearth/metrics/hearth.dta | 36 | 10 | 1,709 | 1,703 |
| stacks/ledger/data/default.dta | 29 | 5 | 1,667 | 1,667 |
| stacks/ledger/data/economy.dta | 22 | 4 | 1,496 | 1,496 |
| stacks/ledger/metrics/ledger.dta | 40 | 8 | 1,947 | 1,872 |
| stacks/stuntline/metrics/track.dta | 31 | 7 | 1,415 | 1,385 |
| stacks/wayfarer/metrics/world.dta | 51 | 12 | 2,223 | 2,208 |
| tests/corpus/scripts.dta | 16 | 2 | 435 | 421 |
| tests/corpus/ui_menu.dta | 49 | 4 | 1,275 | 1,244 |
| tests/host/stack/metrics/objectives.dta | 5 | 2 | 225 | 225 |
| **total** | **385** | **76** | **17,449** | **17,176 (-1.6%)** |

With the switch off these files still parse, but no longer mean what they say: each infix
brace is a command headed by its first operand, so `{0.3 <= $value <= 0.7}` calls `0.3`, an
error when it is evaluated.
