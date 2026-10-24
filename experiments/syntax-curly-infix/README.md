# syntax-curly-infix

A `{...}` whose odd slots hold one operator reads head-first: `{0.3 <= $value <= 0.7}` is
the command `{<= 0.3 $value 0.7}`, `{c ? a : b}` is `{if c a b}`. The rule lives in the
core's reader and printer on this branch (`src/data/parse.cpp`, `src/data/write.cpp`), so
the reader here is `baker::read_string`; this directory holds the converter that rewrites
a DTA file's operator calls as infix in place (`to-new`), the corpus checks (`check`), the
parse benchmark (`bench`), and the samples. The design note is
`dev/syntax-curly-infix-design.md`.

## Samples

`samples/` holds eleven real files in infix beside a copy of each original
(`<name>.original.dta`): the seven stacks' metrics, Ledger's milestones
(`stacks/ledger/data/default.dta`), the calculator, the scripting example's rules and the
corpus's `ui_menu.dta`. Each is `to-new`'s output; the one hand edit is the alignment of
Hearth's continuation line. `tests.cpp` checks that each reads to the converter's tree of
its original, written-infix marks included.

| sample | lines before | lines after | bytes before | bytes after |
|---|---:|---:|---:|---:|
| calculator-functions | 5 | 5 | 279 | 281 |
| contraption-puzzle | 23 | 23 | 1,097 | 1,082 |
| cube | 6 | 6 | 359 | 344 |
| duel | 61 | 61 | 2,918 | 2,843 |
| hearth | 36 | 36 | 1,709 | 1,703 |
| ledger | 40 | 40 | 1,947 | 1,872 |
| ledger-milestones | 29 | 29 | 1,667 | 1,667 |
| scripting-rules | 11 | 11 | 404 | 405 |
| stuntline-track | 31 | 31 | 1,415 | 1,385 |
| ui_menu | 49 | 49 | 1,275 | 1,244 |
| wayfarer-world | 51 | 51 | 2,223 | 2,208 |
| **total** | **342** | **342** | **15,293** | **15,034** |
