# syntax-hash-declarations

The reader's namespace: `#pack (...)`, `#origin (...)` and `#shape (...)` are directives
recorded in the session and never in the tree, `[NAME ...]` is the one macro form, and `#`
opens a directive only before a letter, so `#1` is a symbol. The prototype is a front end
over Baker's reader (`syntax.cpp`: a translator into today's DTA, a printer, and the
migrator `to-new` uses); the design note is `dev/syntax-hash-declarations-design.md`.
`check` round-trips the corpus, `migrate-check` reads the migrated corpus back and compares
trees, packs, origins and macros, `bench` and `count` compare the readers (wall clock and
cachegrind), `bench-stack` and `count-stack` measure a stack's reload, toggle and push with
a pack's shapes in the tree and out of it, and `samples/` holds real files in the new form
(`duel-at-table/` is Duel with its shapes written at their tables).

The core reads the new form itself when built with `-DBAKER_HASH_DECLARATIONS=ON`; the front
end is then the identity. `core-record` (a build without the switch) and `core-check
<record>` (a build with it) compare the core's read of the migrated corpus with today's read
of the original: trees, value digests and sessions (design note §11).
