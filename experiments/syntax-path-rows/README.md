# syntax-path-rows

Rows keyed by a path: `(/fighters.kestrel.moves.jab.startup 5)` at a file's top level is the
tree `(fighters (kestrel (moves (jab (startup 5)))))`, and rows sharing a prefix fold into
one array per key. A layer file reads as the diff it will become. The design note is
`dev/syntax-path-rows-design.md`; `samples/` holds real layer files beside their originals.
`gate` counts what the `/` sigil, and a sigil-free dotted tag, would reinterpret in the
corpus; `stats` gives a file's leaves, depths, lines and bytes nested and as rows.
