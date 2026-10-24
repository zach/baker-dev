# syntax-at-metadata

A tagged child whose tag begins with `@` is metadata: `(hit_pause 6 (@range 0 30) (@desc
"..."))`, `(move (@at fighters * moves) (@keyed) ...)`, `{func f ($x) (@status stub) ...}`.
The lexer, the printer and the tree do not change; what changes is the reading (a value
ends before its metadata, for every reader) and one placement rule (metadata comes after
the value). The core change is behind `-DBAKER_AT_METADATA=ON`; this directory holds the
migration over a tree with the printer's metadata column (`print`), the reader with the
placement rule (`read`), the inverse (`strip_sigils`), the corpus round trip, the
benchmarks (`bench`, `bench-bind`, `bench-panel`) and the tests of the core's behaviour with
the switch on. `tools/migrate_dta.py` is the migration over text, which keeps comments and
layout; the seven `samples/` are its renames, the printer's column and hand polish, each
beside its original. The design note, with every number, is
`dev/syntax-at-metadata-design.md`.
