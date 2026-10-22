# syntax-hash-declarations

The reader's namespace: `#pack (...)`, `#origin (...)` and `#shape (...)` are directives
recorded in the session and never in the tree, `[NAME ...]` is the one macro form, and `#`
opens a directive only before a letter, so `#1` is a symbol. The prototype is a front end
over Baker's reader (`syntax.cpp`: a translator into today's DTA, a printer, and the
migrator `to-new` uses); the design note is `dev/syntax-hash-declarations-design.md`.
`check` round-trips the corpus, `migrate-check` reads the migrated corpus back and compares
trees, packs, origins and macros, and `samples/` holds real files in the new form.
