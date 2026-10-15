# Patches, DLC, and new content: Baker in downloaded updates

*2026-10-15. The owner asked how Baker would deliver patches, extensions, and new content
in packaged, downloaded updates. This note answers with the scenarios a shipped game
meets, what Baker does in each today (measured in development and `BAKER_SHIPPING`
builds, not read from the code), how other games solve the same problems, and the gaps.
Decisions go in D-entries.*

**Decided (D138):** the owner's choices on every finding are recorded in D138, with the
order of work. In short:
- shipping checks a cache's recorded macros and falls back to the text;
- the cache format becomes delta-friendly (done: D148, where an inserted section is a
  352-byte patch);
- layers gain a delete marker and `insert_layer`;
- keyed tables become the documented convention;
- harder packs' shapes hold;
- script leaves with its layer;
- the four smaller gaps are all taken.

## The answer, first

Baker's layer model is the right shape for downloaded content, and most of what a game
needs already works:

- **Title updates** push a layer over the base that replaces values and adds keys.
- **DLC** pushes a layer of its own. When it is not installed, an optional layer leaves
  the game running, and reads of its content fail with a located error or take a
  fallback.
- **Live-ops overlays** use `live_layers`, and removing the overlay rolls them back.
- **Provenance** (`source()`) and `layer_effect`/`diff` show which package supplies a
  value.
- **Malformed text** never crashes the host.

Six things stand between that and shipping updates with confidence. The first two are
the most expensive.

1. **A shipping build trusts a stale cache, silently.** A title update that changes a macro
   gets a new base cache, but a DLC's cache, baked against the old base, keeps the old
   values. Measured: after the patch, the base's sword read 12 and the DLC's axe read 10,
   with no warning. The cache already records the macros it was baked with; shipping does
   not look.
2. **The binary cache is hostile to delta patching.** It is 5.8 times the text and 15 times
   it compressed. Any structural edit rewrites offsets throughout: appending one section
   changed all ten 1 MB chunks of a 9.6 MB cache, with a 129 KB xdelta patch where the
   text needed 260 B. Steam warns against exactly this layout.
3. **A layer cannot remove anything.** A patch cannot delete a debug menu flag, an exploit
   entry, or a list element. Every comparable system (RimWorld, KSP's Module Manager)
   has a delete operation.
4. **Value lists do not compose across packages.** Two DLCs that each add a song to one
   `(setlist ...)` leave only the last. Keyed children compose, which is how Rock Band's
   `songs.dta` scaled to thousands of downloadable songs.
5. **The stack's order is the push order.** A title update that fixes a DLC's data loses
   if the DLC is installed, and so pushed, after it.
6. **A package can loosen the base game's shapes.** A mod that declares a wider range
   silences the base's range check.

The smaller findings, ranked, and the decisions they need are at the end.

## The scenarios

Each is a driver against the development and shipping libraries, with files served from
memory (`set_reader`) or disk. Results were identical in both builds unless noted.

| # | Scenario | Today | Evidence |
|---|---|---|---|
| 1 | A title update changes a value and adds a key | works | `speed` 5 → 6; `levels.l2` added beside `l1` |
| 2 | Two DLCs extend a shared list | values: last wins; keyed: composes | `(setlist song_d)`, not `a b c d`; `songs` holds a, b, c, d |
| 3 | A title update fixes a DLC's value | depends on push order | TU pushed before the DLC: 140 (unfixed); after it: 141 |
| 4 | A title update changes a macro the base uses | only layers above see it | base `sword` stays 10; DLC `axe`, read after the TU, gets 12 |
| 5 | A patch removes a key, a list element, an entry | only list replacement works | `debug_menu 1` and `exploit_boss` survive; `items` shrinks |
| 6 | DLC not installed; a save names its content | works | optional layer pushes empty; `get` errors, `get(path, 0)` returns 0 |
| 7 | A DLC requires a pack that is not installed | loads, unchecked | `(requires season_pass_1)` recorded, never checked |
| 8 | A DLC defines script, then is uninstalled | the script stays | after `remove_layer`, `{dlc_bonus 4}` still returns 8 |
| 9 | A mod loosens the base game's shape | silent | control warns "outside 1.0 to 10.0"; the mod that widens the range does not |
| 10 | DLC bought mid-session, after `freeze()` | needs `live_layers` | refused without it; pushes with it |
| 11 | Ship caches only, then a title update changes a base macro | stale, silent | shipping: sword 12, axe 10; development rebuilds the DLC's cache: 12 |
| 12 | DLC installed at a path other than the one it was baked at | cache missed | `inst/userdlc/axe.dta` and even `./inst/dlc/axe.dta` miss; with no text, the layer fails |

Notes on the evidence:

- **Scenario 4** is D-entry behavior (each layer reads with the macros of those below), but
  it means a title update cannot fix a value by changing the macro that produced it.
- **Scenario 11** is the expensive one. The DLC's cache records `(before DMG <hash>)`, the
  macros its load began with (D72). Development compares it and rebuilds; shipping trusts
  the cache by design (D70 Q2). Checking the recorded hashes against the session's needs
  no file I/O, only hashing the session's defines.
- **Scenario 12:** a cache is named by the hash of the layer's path string (D74), so the
  bake machine and the player's machine must agree byte for byte. The read normalizes
  `./`; the cache name does not.

## Download size: text against the binary cache

A 1.66 MB text of 12,000 sections (the merge benchmark's shape), and its 9.6 MB cache.
Each variant changes the middle section, or appends one. Patch size is `xdelta3 -9`. The
chunk column is the 1 MB chunks that differ at the same offset, which approximates what
SteamPipe downloads.

| Change | Text patch | Text chunks | `.datb` patch | `.datb` chunks |
|---|---:|---:|---:|---:|
| one float value | 62 B | 1/2 | 251 B | 5/10 |
| one longer string | 161 B | 2/2 | 2,870 B | 6/10 |
| a key the file never had | 107 B | 2/2 | 86,092 B | 10/10 |
| insert a section | 138 B | 2/2 | 135,223 B | 10/10 |
| append a section at the end | 260 B | 1/2 | 128,817 B | 10/10 |
| remove a section | 61 B | 2/2 | 115,100 B | 10/10 |

| Whole file | Raw | gzip -9 | xz -9 |
|---|---:|---:|---:|
| text | 1,661,352 | 196,769 | 30,856 |
| `.datb` | 9,566,296 | 1,047,690 | 452,656 |

- **Why the cache churns.** It is a memory image: every node holds offsets into the array
  region, the string blob, and the symbol table. One new array or symbol moves what
  follows it and rewrites every offset that points past it. That is the layout Steam's
  guidance calls the worst case: offsets spread through the file, absolute, so that "even
  an 8-byte number change can create a new 1 MB chunk".
- **What the cache buys.** On this file it loads in 4.4 ms against 14.5 ms to parse the
  text (release, pinned, best of 15): about 10 ms per 1.7 MB.
- **What follows.** For data of this size, shipping the text in updates, and baking a
  cache on the device at first launch, costs a few milliseconds once and saves most of the
  download. A base game can still ship its cache, if updates never rewrite it. That is
  Steam's other rule: add new pack files for updates rather than modifying existing ones.

## How other games do it

- **Override layers, last wins.**
  - Bethesda's plugins resolve conflicts record by record: the plugin that loads later
    wins, and masters load before their dependents.
  - Unreal mounts a patch `.pak` at a higher priority than the base, so its packages are
    found first. Patches chain against a numbered base build.
  - Baker's layers are the same model, finer-grained: by key path, not by record or
    package.
- **Explicit edit and delete operations.**
  - RimWorld moved from whole-Def overrides, which conflicted by load order, to XML patch
    operations addressed by XPath: add, replace, remove, conditional.
  - KSP's Module Manager has `@` edit, `!` delete, `%` edit-or-create and `&`
    create-if-absent, with `:NEEDS` conditions and `:BEFORE`/`:FOR`/`:AFTER` passes.
  - Both exist because overriding cannot remove.
- **Ordered rounds instead of load order.** Factorio runs every mod's `data.lua`, then every
  `data-updates.lua`, then every `data-final-fixes.lua`, so that a mod can adjust another's
  data without depending on load order. Mods are sorted by dependency, then by name.
- **Keyed content tables.** Rock Band's DLC is a `songs.dta` per package with one keyed
  entry per song. A song missing from it does not appear. The format let downloadable
  content and the Rock Band Network scale (spec, provenance). This is scenario 2's keyed
  half.
- **Patch-friendly packages.** SteamPipe splits files into about 1 MB chunks and downloads
  only the chunks that changed. Its guidance:
  - keep changes localized and do not shuffle asset order;
  - keep one table of contents, not offsets spread through the file;
  - leave filenames and timestamps out of packs;
  - add new packs for updates rather than rewriting old ones.
- **Missing DLC and saves.** Xbox certification asks that a title handle a save made with
  DLC that is no longer installed. The player is told what is missing, and the save works
  again once the DLC returns. Games either block the load and name the missing pack, or
  warn and continue.
- **Server-side hotfixes** avoid client patches for tuning (EVE; Riot's micropatches,
  already in `iteration-backgrounder.md`). Baker's live-ops overlay is that path.
- **Signing** is the platform's: console packages are certified and signed by the platform
  holder, and stores verify downloads. A game engine verifies its own content only when it
  loads packages from outside the store, as a PC mod loader does.

## Findings, ranked

| # | Finding | Kind | Evidence |
|---|---|---|---|
| 1 | Shipping trusts a DLC cache baked against an older base | correctness | scenario 11 |
| 2 | `.datb` churns on every structural edit; 5.8× text, 15× compressed | download size | delta table |
| 3 | No removal: a layer cannot delete a key, child, or list element | expressiveness | scenario 5 |
| 4 | Value lists do not compose across packages | composition | scenario 2 |
| 5 | Layer order is push order; no insert at a position | ordering | scenario 3 |
| 6 | A package can loosen a harder pack's shapes | safety | scenario 9 |
| 7 | Script a removed layer defined stays defined | lifecycle | scenario 8 |
| 8 | Cache names hash the raw path string | robustness | scenario 12 |
| 9 | Pack `requires` is recorded, not checked | dependencies | scenario 7 |
| 10 | No content version: a DLC cannot say which base it needs | compatibility | not found (D-entries, spec) |
| 11 | A cache from an untrusted source is unsafe to load (D71) | trust | stated limit |

Notes on the findings:

- **Finding 2 also affects engine updates.** A cache format change (`kVersion`) refuses
  every old cache. A DLC that shipped only its cache stops loading after an engine update
  until it is re-baked. Shipping the text beside it, or instead of it, removes the
  failure mode.
- **Finding 4** is a convention as much as a gap: keyed children already compose.
- **Finding 6** fits D80's one-way rule: softer may extend harder, never loosen it.
- **Finding 11** matters only to a PC mod loader: such a loader must accept text, never
  caches, from mods. Console and store packages are signed by their platforms.

Scenarios 1, 6 and 10 need nothing. Scenario 4 needs a sentence in the cookbook: change
values, not the macros below you.

## What each finding needs (options, for the owner)

1. **Stale caches in shipping.** Compare the cache's recorded `before` hashes with the
   session's, in shipping too. This costs no I/O. On a mismatch, fall back to the text, or
   refuse with a located error if there is no text.
2. **Download size.** In order of cost:
   - (a) Recommend text for updates and DLC, with caches baked on the device at first
     launch into a writable cache directory. This works today except that shipping writes
     no caches.
   - (b) Keep the base cache immutable and deliver every update as new layers.
   - (c) Make the format delta-friendly: source-order layout, relative offsets, one table
     at the end. This is a format change.
3. **Removal.** A delete marker in a layer, such as a reserved value or directive meaning
   "remove this key" (RimWorld's remove, Module Manager's `!`). Or keep layers additive
   and document enable flags as the convention.
4. **Composition.** Document keyed tables as the convention for content packages. An
   append marker for value lists is the alternative.
5. **Order.** `insert_layer` before or after a named layer, or a priority per layer. Or the
   host rebuilds the stack in a canonical order (base, DLC by id, title updates, live ops)
   whenever content is installed.
6. **Shapes.** Read shapes only from packs at or above the content's level, so a softer
   pack can add fields and shapes but not loosen a harder one's.
7. **Script lifecycle.** Record which layer defined each function and handler, and remove
   them with the layer. Or document that a host rebuilds its Context when content is
   removed.
8. **Paths.** Name caches by the normalized path, or by a path relative to a content root,
   and say "no cache for X" when falling back.
9. **`requires`.** Warn when a required pack is absent, as the one-way rule warns.
10. **Versions.** A version in the pack declaration and in what it requires, checked like
    `requires`.

## Reproducing

The drivers are small programs against the release and shipping libraries
(`build-matrix/release`, `build-matrix/ship`):

- the scenario driver serves files from a map through `set_reader`;
- the cache pipeline bakes with a development `Config` and `cache_dir`, then loads with a
  shipping one after deleting the text;
- `gen.py` writes the 12,000-section text and its variants;
- `bake1` writes each cache with `write_cache`.

They live in the session's scratchpad, not the repository. Turn them into tests when the
findings are decided.

## Sources

- [Steamworks: uploading and SteamPipe chunking, pack file guidance](https://partner.steamgames.com/doc/sdk/uploading)
- [Unreal Engine: how to create a patch](https://dev.epicgames.com/documentation/unreal-engine/how-to-create-a-patch-in-unreal-engine)
- [Unreal Engine 4.27: ChunkDownloader `MountChunk`](https://docs.unrealengine.com/4.27/en-US/API/Plugins/ChunkDownloader/FChunkDownloader/MountChunk/index.html)
- [Unreal forums: what is the right way to patch a project?](https://forums.unrealengine.com/t/what-is-the-right-way-to-patch-a-project/364564)
- [UESP: Intro to mod conflicts (Oblivion)](https://en.m.uesp.net/wiki/Oblivion_Mod:Intro_to_Mod_Conflicts)
- [STEP: merging plugins](https://stepmodifications.org/wiki/Guide:Merging_Plugins)
- [RimWorld Wiki: PatchOperations](https://rimworldwiki.com/wiki/Modding_Tutorials/PatchOperations)
- [Module Manager syntax (KSP)](https://github.com/sarbian/ModuleManager/wiki/Module%20Manager%20Syntax)
- [Factorio: data lifecycle](https://lua-api.factorio.com/latest/auxiliary/data-lifecycle.html)
- [Rock Band Customs wiki: adding customs (songs.dta layout)](https://gitlab.com/rock-band-customs/playstation-customs/playstation-3/-/wikis/Basic-Guide-to-Adding-Customs/PS3/Rock-Band-3-Customs)
- [mariteaux: Data Array (DTA and DTB)](https://www.nofi.mariteaux.somnolescent.net/modding/guitar-hero/tutorials/an-introduction-to-data-array/)
- [Xbox requirement XR-037: DLC and saved games](https://devdocs.xbox.com/publishing/certification/xr/xr-037)
- [Microsoft game publishing glossary (content updates)](https://learn.microsoft.com/it-it/gaming/game-publishing/glossary)
- [EVE Online: how do we extend or patch EVE?](https://www.eveonline.com/news/view/how-do-we-extend-or-patch-eve)
- [Irony Mod Manager wiki (Paradox FIOS/LIOS)](https://github.com/bcssov/IronyModManager/wiki)
