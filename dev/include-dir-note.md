# A Directory as the Children of One Array: `#include_dir`

A design note, agreed with the owner on Oct 22, 2026. It answers hosts whose content is one
record per file: hundreds of cards, each its own file, with its own history, edited by
several people. Results and deviations go into `dev/decisions.md` when it is built.

## The need

Baker's keyed tables hold their records as the children of one array, so a game with a file
per record must list the files somewhere, or bake the list at edit time. With the directory
as the array:
- a layer retunes `cards.fireball` by name;
- provenance points at `cards/fireball.dta`;
- a new card is a new file.

This is the data-level counterpart of `dev/layer-directory-note.md`. That note makes each
file a layer; this one makes each file a record inside one layer.

## The design

- **The directive.** `#include_dir "cards"` may appear wherever a child may, inside any
  array:

  ```dta
  (cards
     #include_dir "cards"
     (starter_deck fireball fireball shield))   ; other children beside it
  ```

- **Each file becomes a child named by its file.** `cards/fireball.dta`, holding
  `(cost 3) (damage 6)`, becomes `(fireball (cost 3) (damage 6))`.
  - The key is the file name without `.dta`, so the key and the file cannot disagree, and
    renaming the file renames the record.
  - A name that is not a symbol Baker can read bare is an error, located at the directive.
- **A subfolder is a nested table.** `cards/fire/fireball.dta` becomes
  `(fire (fireball ...))` under `cards`, following the layout on disk. A key is unique
  within its folder.
- **Order.**
  - A folder's `order.dta` lists the names in order, as in `dev/layer-directory-note.md`.
    Names it does not list follow in name order, and a name it lists that the folder lacks
    is a warning, located.
  - Without an `order.dta` the order is name order, the same on every platform and host.
  - `order.dta` is not itself a record.
- **Through the host.** `LoadSession::set_lister` lists a directory through the host, as
  `set_reader` reads a file. Without a lister, it lists the disk. The host kit passes
  `CommandHooks::list` to it, so a packed archive or a virtual file system works.
- **What the rest of Baker sees:**
  - **Provenance:** each record's nodes keep their own file and line, so `source()` and
    `explain` name `cards/fireball.dta:2`.
  - **Packs:** each file is in the pack of the file that holds the directive. A file of
    its own may declare a pack, as any included file may.
  - **The cache:** it records the listing, so a file added, removed or renamed, or a
    changed `order.dta`, makes the cache stale.
  - **The host kit's watcher** watches the directories.
  - **`stack_files()`** lists every file read.
- **Errors.** A file that does not parse is a located error, as for `#include`. A directory
  that does not exist is an error naming the directive. Since `include_dirs` are search
  paths for `#include`, they are searched for `#include_dir` too.

## Cost

Load time only: one listing per folder, then each file read as an include. Nothing at read
or frame time.

## Tests to write

- **Wrapping:** files wrapped by name, with other children beside the directive.
- **Folders and order:** a subfolder as a nested table; `order.dta`'s order, with unlisted
  files after it in name order, and a missing name warned.
- **Layers:** a layer above retunes one record by name, and adds a record.
- **Provenance** at the record's own file and line.
- **Through the host:** read through a host reader and lister, as from an archive.
- **The cache:** stale after a file is added or `order.dta` changes.
- **Errors:** a bad file name, a file that does not parse, and a missing directory.
