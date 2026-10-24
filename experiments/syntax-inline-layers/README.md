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
