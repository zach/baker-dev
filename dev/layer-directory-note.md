# A Directory of Layers: Mods, DLC, Experiments

A design note, agreed with the owner on Oct 22, 2026. It answers hosts with mods, plugin
packs and DLC, and supersedes `dev/drop-in-layers-note.md`, which D201 deferred until a
host asked. Results and deviations go into `dev/decisions.md` when it is built.

## The need

Each file that joins a stack must be named somewhere: the project file, an `#include`, or
`push_layer`. A modder, a DLC, a plugin pack or an agent's experiment therefore edits two
things, the file and the list that names it, and the list is the edit that goes wrong.

Some hosts also mount packs after the Config is open, so files appear in a directory that
was already read.

## The design

- **A layers entry may name a directory:**

  ```dta
  (layers
     (default "data/default.dta")
     (mods (dir "mods"))                       ; each mod a layer, in mods/order.dta's order
     (local "data/local.dta" optional dev_only))
  ```

  The directory's layers stand between the entries around them.
- **One layer each:**
  - every `*.dta` file directly in the directory, named by its file name without
    extension;
  - every subfolder that holds a `layer.dta`, named by the folder. A mod of several files
    is a folder whose `layer.dta` `#include`s the rest, with the folder on its include
    path.
- **The order** is the directory's `order.dta`, a list of names:

  ```dta
  (order base_fixes hard_mode new_units)
  ```

  - A layer it does not list follows the listed ones, in name order.
  - A name it lists that the directory lacks is a warning, located in `order.dta`.
  - Without an `order.dta`, the order is name order.
  - `order.dta` is not itself a layer.
- **All are enabled** when found. A host's mod menu turns one off or on with
  `set_layer_enabled`, as for any layer, and the choice holds across a rescan for as long as
  the layer is there.
- **Every reload lists the directory again,** through the host's reader and directory
  listing (`CommandHooks::list` and the session's reader, as 0013 gave scenarios):
  - a new file or folder becomes a layer;
  - a removed one is removed;
  - a change to `order.dta` reorders the layers.

  A reload with nothing changed does no more work than before, beyond the listing. The host
  kit's watcher watches the directory, so `Live` notices new files without a call.
- **A layer that does not read** is refused as any layer is, and the rest stand. The
  problem is located in its file.
- **Provenance and tools.**
  - `source()` names the layer's file.
  - `explain` and `layer_effect` name the layer, and `stack_files()` lists the directory,
    its `order.dta` and each layer's files.
  - The reference lists the directory entry and its layers.
- **The entry's options** apply to each layer it finds: `optional` (the directory may be
  absent), and `dev_only` (none of them in a shipping build).

## Cost

- A reload lists the directory, one host call. Reading a layer is as for any file layer:
  it is read again only when its text changed (D221).
- Each layer is a layer, so a merge costs per layer as today. A hundred mods are a hundred
  layers.

## Tests to write

- **Discovery:** files and folders become layers in `order.dta`'s order, and unlisted ones
  follow in name order.
- **Changes over reloads:** a file added, removed, or renamed in `order.dta` between
  reloads.
- **Mod menu:** a layer toggled off stays off across a rescan.
- **Failures:** a mod that does not parse, located, with the rest standing.
- **Through the host:** a directory read through a host reader and listing, as from a
  mounted pack.
- **Builds:** `dev_only` and shipping.
- **Live:** the host kit's `Live` notices a new file.
