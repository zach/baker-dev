# syntax-inline-layers

Layers inside a file: `(#layer (handheld (when HANDHELD)) (renderer (shadows 0)))` is a
block the parser lifts out of the file's tree, and a Config pushes it as the sub-layer
`default:handheld` directly above its file, enabled by its condition or toggled by hand. The
core change is behind `BAKER_INLINE_LAYERS` (on in the experiments build); this directory is
the converter from today's `#ifdef` regions to blocks and back (`to-new`, `to-dta`), the
check that a converted file merges to the same tree under every define set (`check`), a
survey of conditional regions (`survey`), and the benchmarks (`bench`, `bench-layers`).
`samples/` holds three real files as blocks beside their originals. The design note is
`dev/syntax-inline-layers-design.md`.

| Sample | Before | After |
|---|---|---|
| `config/`: examples/config | `default.dta` + `handheld.dta`, 10 lines, 344 bytes, and a `push_layer` the host makes on a handheld | `default.blocks.dta`, 11 lines, 399 bytes |
| `ui_menu/`: tests/corpus/ui_menu.dta | 49 lines, 1,275 bytes, two `#ifdef` regions (9 lines) | 45 lines, 1,298 bytes, two blocks (3 lines) |
| `duel-ab/`: stacks/duel/data/default.dta and an A/B pair in the cookbook's pattern | 3 files, 52 lines, 3,346 bytes, two `push_layer` calls and two toggles | `default.blocks.dta`, 52 lines, 3,325 bytes, toggled from the Layers panel |

`compare <original> <conversion>` checks a hand-made conversion; the tests check all three
(`the samples merge as their originals did`). `hit-pause-heavy.dta` and `hit-pause-light.dta`
are written for this sample, as the cookbook's "Experiments and A/B comparison" would have a
studio write them; Duel has no A/B of its own.
