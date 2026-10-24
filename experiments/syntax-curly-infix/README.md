# syntax-curly-infix

A `{...}` whose odd slots hold one operator reads head-first: `{0.3 <= $value <= 0.7}` is
the command `{<= 0.3 $value 0.7}`, `{c ? a : b}` is `{if c a b}`. The rule lives in the
core's reader and printer on this branch (`src/data/parse.cpp`, `src/data/write.cpp`), so
the reader here is `baker::read_string`; this directory holds the converter that rewrites
a DTA file's operator calls as infix in place (`to-new`), the corpus checks (`check`), the
parse benchmark (`bench`), and the samples. The design note is
`dev/syntax-curly-infix-design.md`.
