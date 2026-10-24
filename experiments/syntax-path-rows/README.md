# syntax-path-rows

Rows keyed by a path: `(/fighters.kestrel.moves.jab.startup 5)` at a file's top level is the
tree `(fighters (kestrel (moves (jab (startup 5)))))`, and rows sharing a prefix fold into
one array per key, so a layer file reads as the diff it will become. Built with
`-DBAKER_PATH_ROWS=ON`, Baker's parser reads rows itself (`src/data/parse.cpp`), its printer
writes them (`WriteOptions::rows`) and write-back appends them. Without it, this
experiment's reader is a translator (Baker's parser, then a tree rewrite) with the same
rules, and the same tests pass against both. The design note is
`dev/syntax-path-rows-design.md`. `samples/` holds real layer files beside their originals,
with their counts in `samples/counts.txt`. `gate` counts what the `/` sigil, and a
sigil-free dotted tag, would reinterpret. `stats` gives a file's leaves, depths, lines and
bytes, nested and as rows. `count` runs a fixed number of reads for cachegrind.
