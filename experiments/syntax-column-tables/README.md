# syntax-column-tables

A keyed table declares its columns once, `#columns (button startup ...)`, and writes each
record as a row of cells, exactly one per column, with `_` for a field the row lacks; the
reader expands each row into the nested record Baker reads today. The reader is a rewrite
over `read_structure`'s tokens into nested text read by `baker::read_string`, the printer
heads a table where its heuristic says so, and `HeadedFiles` lets a `Config` write back into
headed files. `check` round-trips the corpus, `stats` counts the corpus's tables, and
`bench` / `bench-table` time the reader against Baker's. The design note, with the numbers,
is `dev/syntax-column-tables-design.md`; `samples/` holds four stacks files in the form
beside their originals.
