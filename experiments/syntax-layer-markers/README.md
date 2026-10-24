# syntax-layer-markers

Layer verbs beside `#remove`: `(key #append v...)`, `(key #drop v...)` and the `#was` pin,
`(key v... #was w...)`. Unlike the other experiments this one is a core change, since the
verbs are read by Baker's own parser and applied by its merge. This directory holds the
conversion and the measurements. `to-new` rewrites a layer that restates the base's lists
into the verbs against the base, and `to-dta` resolves the verbs back into plain rows.
`lists` measures the stacks' lists, `bench-pins` reads an overlay plain and pinned, and
`parse-n` reads a file N times for cachegrind. The design note is
`dev/syntax-layer-markers-design.md`.
