# Drop-in layers: a design note

*From a reading of the book Game++ (sections 0x16 and 0x53): a data-driven engine whose
operators dropped files into a folder during a live match, and behaviour modules that join a
running game when a file appears. A proposal; nothing is built.*

**Status: superseded** by `dev/layer-directory-note.md` (Oct 22, 2026), which hosts with
mods and plugin packs asked for. Deferred before that (D201).

## The need

A stack's agent writes a scenario, a task pack or an experiment as a new file. Today a new
file reaches the running game only when something names it: a project file entry, an
`#include`, or a `push_layer` call. The agent (or a modder, or a designer with a folder of
experiments) therefore edits two things, the file and the list that names it, and the
second edit is the one that goes wrong: forgotten, misspelled, or made in a file that is
someone else's. The watcher already watches directories (`include/baker/host/watcher.hpp`),
for the files it knows.

## The proposal

A project entry that names a directory rather than a file:

```
(layers
   (default "data/default.dta")
   (experiments (dir "data/experiments" (match "*.dta")))   ; every file, in name order
   (local "data/local.dta" (optional)))
```

- **Order.** The directory's files are layers in name order (`01-slower-ai.dta` before
  `02-...`), between the entries around them. Name order is the only order a person can
  predict from a listing.
- **Appearance and removal.** The watcher reports a new or deleted file in a watched
  directory; `Live::begin_frame` pushes or pops the layer and the reload record names
  it. A file that fails to load is refused as any layer is (D161) and the rest stand.
- **Provenance.** `source()` names the file, as for any layer; `explain` lists the
  directory's layers under the directory's name.
- **`check` and the reference** list the directory and its files, so a tool sees the
  stack a person sees.
- **Shipping.** A directory entry is read once at open and never watched, like every
  layer; a package that wants the same effect ships its files and a project file naming
  them.

## Cost and risks

- The core gains nothing: this is the project file (`project.hpp`), the host kit's `Live`,
  and the watcher.
- A directory listing is a system call per `watch()`, never per `poll()`, so a quiet
  frame still allocates nothing (D189's rule).
- The risk is the one the book names for its data-driven engine: a file nobody meant to
  ship, or a stale experiment, joins the game because it is in the folder. `check` listing
  the directory's files, and a `local`-style convention that experiments live under a
  directory git ignores, are the answers; a directory entry should also refuse to be the
  bottom layer.

## Questions for the owner

1. Build it, as a step of the host kit, or leave the two-edit workflow and document it?
2. If built: name order only, or also `(order mtime)` for "newest wins"?
3. Should a stack's `agent` layer become such a directory, so an agent's experiments are
   files rather than edits to one file?
