# syntax-indent-lines

A line that begins with an atom is an array: its tokens are the array's nodes, and the lines
indented deeper are its children; a line that begins with a bracket reads as today. The
design note is `dev/syntax-indent-lines-design.md`. In the default build `read` is a
translator (a layout pass that writes the brackets the indentation implies, then Baker's
reader); built with `-DBAKER_INDENT_LINES=ALWAYS` or `DIRECTIVE`, `read` and `print` are the
core's own reader and printer. `convert` is the migration tool (comments and directives
kept, every tree read back and compared), `trees` the gate (two builds' outputs diffed), and
`samples/` holds the stacks' data files, packs and examples beside their rewrites, three of
them hand-polished (`.polished.dta`).
