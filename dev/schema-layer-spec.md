# The schema layer: shapes become the contract

A buildable spec in four phases, settled with the owner on 2026-10-30 and 2026-10-31 in a
grilling session over Baker 0.7.0. Terms are the glossary's (`dev/glossary.md`); the
hard-to-reverse choices are D269 to D272. Background: the owner's research docs *Game
Asset Systems: Identity, Caching, and Data Models*, *Two Data Systems, 2004–2005* and
*Onager Toolset: Key Insights and Lessons for Baker*.

## Problem statement

A game team uses Baker for tuning and behaviour, and now wants it to hold content
definitions too: bosses and their phases, fighters and their moves, weapons, items. A
designer or an agent authors those records; an engine programmer reads them from C++.
Today that team meets four walls.

1. **Three sources of truth.** A table's layout is written once as a shape, again as the
   C++ struct the game reads, and again as that struct's `Convert<T>`. Nothing ties
   `#shape (boss_phase ...)` to `struct BossPhase`, so when a designer adds a field or a
   programmer renames one, the two drift and nobody hears about it.
2. **Every read walks the tree.** A lookup by path costs about 9 ns (D77), which is fine
   for a few hundred tunables and wrong for 50,000 records a frame might scan. There is no
   packed, typed table to index into.
3. **Renaming a record breaks the world.** Renaming `laser` breaks every `key_of` that
   names it, every overlay that retunes it, including live-ops overlays already shipped,
   and every write-back target. A record has no identity but its key.
4. **Nothing records an edit in a form tools can share.** The journal keeps the last 256
   live value edits, capped at four nodes each, with no structural edits and no undo. An
   agent proposing a new boss has no unit of work the game can check, show, apply and
   undo.

Two smaller gaps sit beside them: no field can say "this names a mesh", so Baker cannot
report the dependency edges an asset pipeline needs; and a shape field has no migration
when it is renamed.

## Solution

The shape becomes the single declaration from which C++ views, baked tables and
dependency edges are generated, while the data on disk stays self-describing and
authoritative: a file is always readable without the code, and a missing field still
reads as its default. Baker does not become a schema-first system in which code owns the
layout.

- **W1, views.** A shape marked `(@view)` generates a C++ struct, its `Convert<T>`, and a
  digest of its layout. The game asserts the digest; `check` reports drift.
- **W3, identity.** A record may carry a random 128-bit `(@id ...)` inside it. A `rename`
  command rewrites references across the stack and leaves `(@aka old)` behind, so shipped
  overlays still resolve. `(asset KIND)` fields name files outside Baker and are reported as
  dependency edges. A shape field may say `(@renamed_from old)`.
- **W2, baked tables.** A viewed table bakes into a packed array of its views, read through
  `Table<T>` by key or by ID, rebuilt on reload like a tree, and shipped as a load-in-place
  section of a stack-wide cache.
- **W4, change records.** Every edit, value or structural, becomes a change record that
  holds whole values; records group into transactions with undo and redo; an agent's
  change set is a layer that is checked, pushed and promoted, and written out as
  `baker.changes/1`.

Each phase ships on its own, in the order W1, W3, W2, W4: baked tables want stable IDs,
and change records want both.

## User stories

The engine programmer:

1. I mark a shape `(@view)` and get a C++ struct I never write by hand.
2. I add one CMake call and the views regenerate whenever the shapes change; nothing
   generated is committed.
3. I read a record into its view with one call and get a located error, naming file, line
   and path, if a field has the wrong type.
4. I read a whole table through `Table<T>`, by key or by ID, without walking the tree.
5. I hold an index into a table across a reload, and it stays valid while the table's keys
   are unchanged.
6. I am told, in development, when the running shapes no longer match my compiled views,
   and the farm's check fails on it.
7. I choose the C++ type of a field when the default does not suit: `(@cxx int64)`.
8. A project that uses none of this pays nothing at load, check or reload.
9. A reader holding a table during a reload keeps the old image until it lets go.
10. My shipped build loads its baked tables in place, with one fix-up pass.
11. A live-ops overlay that retunes a boss is visible through `Table<T>` after its push.

The designer:

12. I add a field to a record before the programmer adds it to the shape, and the game
    keeps running with a warning.
13. I rename a weapon and every boss, unit and overlay that named it follows.
14. An overlay we shipped last season still finds the weapon under its old name.
15. I name a mesh in a record and `check` tells me if the file does not exist.
16. I undo my last action as one step, however many values it touched.
17. A record I copy-paste is reported as a duplicate ID, and one command gives the copy a
    fresh one.
18. A shape field renamed by a programmer is accepted under its old name until `format`
    rewrites my files.

The tools programmer:

19. I read every asset dependency from the reference JSON as a flat list of edges, and
    index it forward or in reverse.
20. I read the change log as `baker.changes/1` JSON, like check and reference output.
21. I address a record by its ID, which survives renames, in my editor's selection and undo
    history.
22. I run `rename` from CI or an editor through the game's binary, as `format` and
    `promote` run today.
23. I see a duplicate ID reported as a located error naming both records.
24. I mint IDs for an existing table's records with one command.

The agent:

25. I propose a new boss as layer text and get the check's verdict before anything lands.
26. My accepted change set is written to the files by promote, and its change records say
    what it did.
27. If the stack changed after my check, my change set is checked again rather than
    refused for the change alone.
28. I read structural edits, list verbs and value edits in one log.
29. I can name a record by ID in a change set that also renames it.

## Implementation decisions

### Across phases

- **Spelling.** Every new form on a record or shape is metadata, spelled with `@` (0.7.0's
  rule, D248): `@id`, `@aka`, `@view`, `@cxx`, `@renamed_from`. A record may still have
  fields named `id` or `aka`. `(asset KIND)` is a field type, like `(key_of TABLE)`.
- **Breaking changes are allowed.** Data forms and APIs may change; where a phase changes a
  data form, the formatter or a `tools/` migrator rewrites files, as 0.7.0's did.
- **Opt-in costs nothing.** A stack with no `@view`, no `@id` and no asset field does no
  extra work at load, check or reload, and the benchmark rows prove it (Testing).
- **One shape set per build.** A `#shape` under `#ifdef`, or a `(when COND)` layer in the
  contract stack, is a generation error naming it. Platform differences live in values,
  never in layouts.

### W1: views

**Declaring.** A shape generates a view if it carries `(@view)`, or if the project file
lists it:

```dta
#shape (boss_phase (@at bosses * phases) (@view)
   (enter_at float (@range 0 1))
   (attacks  (list symbol))
   (cadence  float (@range 0.2 5) (@optional)))
```

```dta
; baker-project.dta
(views
   (layers core content)          ; the contract stack, bottom to top
   (shapes boss_phase move)       ; views besides those marked (@view)
   (namespace game::data))
```

**The contract stack.** Views come from the shapes declared in the named layers only. A
field a higher layer, overlay or mod adds is readable from the tree and absent from the
struct. A shape that only higher layers declare generates nothing.

**Type mapping.**

| Shape type | View member |
|---|---|
| `int` | `int32_t`, narrowed with a located error on overflow |
| `float` | `float` |
| `bool` | `bool` |
| `symbol` | `baker::Symbol` |
| `string` | `std::string_view`, borrowed |
| `curve` | `baker::Curve` |
| another shape | that shape's view, nested |
| `(list T)` | `std::span<const V>` of T's member type, borrowed |
| `(list T N)` | `std::array<V, N>` |
| `(key_of TABLE)` | `baker::Symbol` of the key (W2 may add a typed reference) |
| `(asset KIND)` | `std::string_view` of the path, borrowed (W3) |
| `any` | none: a generation error naming the field |

`(@cxx int64)`, `(@cxx double)` and similar override a member's type where the shape type
allows it. An `@optional` field reads as its default when absent; views hold no
`std::optional`. A field's default is its shape default, else zero, an empty Symbol or an
empty view.

**Lifetime.** A view is a plain value struct. Its strings and lists borrow from the tree or
table they were read from, and stay valid while the reader holds that tree's or table's
`Ref`; a reload never invalidates a held `Ref`.

**Reading.** The generated `Convert<T>` returns `Expected<T>`. A value of the wrong type is
a located error for that record. Range and `@one_of` violations are not read errors; the
shape validator reports them as it does today, and it stays optional.

**The digest.** Each view carries `kShapeDigest`, a 64-bit hash of its fields' names,
types and order (and its nested views'), never of `@range`, `@step`, `@desc`, `@note`,
`@kind`, `@one_of` or rules: tuning never forces a rebuild. The running shape set computes
the same digest. A mismatch is a warning in development builds and in `check`, and an
error under `check --baker-strict-views`, which a farm sets.

**Build integration.** `baker_bake --views OUT_DIR` reads the project's contract stack and
writes one header per viewed shape plus an umbrella header. A CMake function drives it:

```cmake
baker_generate_views(my_game PROJECT ${CMAKE_SOURCE_DIR}/baker-project.dta)
```

It adds the generated directory to the target's include path, regenerates when any file
of the contract stack changes, and writes only to the build tree. Generated headers are
never committed.

**Out of W1:** C++26 reflection. A later drift checker may compare hand-written structs
against shapes, but shapes stay the source.

### W3: identity, references and evolution

**Record IDs.** A record may carry `(@id 0f3c9a7e5b214d68a1e2c3b4d5e6f708)`: 128 random bits
as 32 lowercase hex digits, written inside the record so it travels with it. IDs are
optional. Tools mint one when they create a record (an editor, `rename`, a promoted change
set), and `format --baker-mint-ids TABLE` mints IDs for a table's records that lack one.
The ID is metadata, so readers of values read past it.

- **Duplicates.** Two records in the merged stack with one ID are a located error naming
  both. `format --baker-mint-ids` gives the newer copy, by layer order and then by line, a
  fresh ID.
- **References.** `key_of` keeps naming keys. The ID anchors renames, change records and
  tools; it is never written in a reference by hand.
- **Shipping.** `--drop-metadata` (D265) keeps `@id` and `@aka`: they are identity, not
  tool text.

**Rename.** A host-kit command beside `format` and `promote`:

```
rename --baker-table weapons laser plasma_lance [--baker-write]
```

It renames the record in the layer that declares it, rewrites every `key_of` that names it
in every layer, overlay and mod directory of the stack, and writes `(@aka laser)` on the
record. Without `--baker-write` it lists what it would change. A reference it cannot
rewrite, inside a macro or an `#include`d file that write-back already refuses, is listed
as an error and nothing is written. A record keeps its aliases until someone removes them;
`check` lists an alias no layer of the stack uses, as information.

**Resolution.** `key_of` resolves a key, then an alias. A key that is both one record's key
and another's alias is an error at both. A layer that retunes `(weapons (laser ...))` where
`laser` is an alias merges into the renamed record.

**Asset references.** A field type `(asset KIND)`. The project file declares the kinds:

```dta
(assets
   (mesh    ".glb" ".fbx")
   (texture ".png" ".tga"))
```

The value is a path string, relative to the project's directory. `check` reports a path
whose extension its kind does not list, and a file the session's lister cannot find (the
lister lists directories today, D243; W3 extends it to answer for a file). The reference
JSON gains an `assets` array of edges:

```json
{"from": {"file": "data/bosses.dta", "line": 12, "path": "bosses.dragon.mesh",
          "id": "0f3c9a7e5b214d68a1e2c3b4d5e6f708"},
 "field": "mesh", "kind": "mesh", "target": "art/dragon.glb", "exists": true}
```

A pipeline indexes it forward (what a record needs) or in reverse (what a file change
touches). Baker never imports, stores or cooks an asset.

**Field renames.** `(@renamed_from old)` on a shape field: a record that has `old` and not
the new name reads it under the new name with a warning naming both, and `format` rewrites
the record. A record with both is an error.

### W2: baked tables

**What bakes.** Every table a viewed `(@keyed)` shape governs in the contract stack.

**Layout.** An array of the view struct, one element per record, in the merged tree's
order (array of structs; struct of arrays is deferred). Strings and lists live in a pool
the table owns.

**Reading.**

```cpp
baker::Ref<const baker::Table<BossPhase>> t = cfg.table<BossPhase>();
std::optional<uint32_t> i = t->index_of("dragon"_sym);  // by key or alias
std::optional<uint32_t> j = t->index_of(id);            // by record ID
const BossPhase& p = (*t)[*i];
```

An index stays the same across a reload while the table's set of keys is unchanged; adding
or removing a record renumbers it.

**Literals only.** In W2's first version, a view field must hold a literal after the
preprocessor (macros, defines and `#ifdef` resolved). A `{command}` in a view field is a
bake error naming the field; the field may still be read lazily from the tree if the shape
leaves it out of the view. Folding commands bound as pure is deferred, since it reopens
D8.

**Reload.** In development, `Config::reload()` and every other merge rebuild each baked
table whose records changed, and swap it in with the tree. Tables are refcounted like trees:
a reader holding the old `Ref` keeps it.

**Live-ops.** A shipped game loads its baked tables beside the tree cache. An overlay
pushes as today, under its `LayerPolicy`, and every baked table whose records it touches is
rebuilt from the merged tree at that push. Nothing is patched in place.

**The baked image.** `baker_bake --stack PROJECT -o game.datb` writes one cache for the
contract stack in the existing `.datb` container: same format version, endianness and
pointer-size rules, and a freshness record covering every layer and file it read. The
tables are a new section of it, loaded in place with the tree's one fix-up pass. Each table
section records its view digest; loading a section whose digest differs from the compiled
view's refuses that table with a located error, and the game reads it from the tree.
Development builds build tables in memory at load and never need the file.

**Derived-data caching** stays out of scope, as A5 settled: no shared cache, and no change
to how per-layer caches are named.

### W4: change records

**Content.** A change record holds:

- the record ID, when the record has one;
- the path;
- the whole old and new values, as `Ref`s to trees (no four-node cap), or none for absent;
- the layer written, and the source the old value came from;
- the time, from the Config's time source.

**Kinds.** Set a value, add a record, remove a record, rename a record (with its alias),
reorder records, and the list verbs `#append` and `#drop`.

**Transactions.** Records group into transactions: one console command, one tool action, one
promoted change set. Undo and redo work per transaction. The 256-entry journal becomes a
capped view over the log, as `config.journal` shows it today.

**Agent change sets.** An agent writes its change set as layer text. Baker checks it with
`check_layer_text` under a `LayerPolicy`, pushes it as a layer, and on acceptance writes it
to files with `promote`, which produces the transaction. If the stack changed between the
check and the push, the push checks again (as a `CheckStamp` push does today) and refuses
only if that check fails. A change set may name a record by `@id` and rename it in the
same transaction.

**Wire format.** `baker.changes/1`, documented beside `baker.check` and
`baker.reference`: one object per transaction, with its records in order.

### Targets

- `Table<T>` and the baked section live in `baker::data`, with no script dependency.
- View generation lives in `baker_bake`, driven by `baker_generate_views`.
- `rename` and `format --baker-mint-ids` live in the host kit (`baker::host`).
- Asset kinds and the `views` key are new project-file keys.

## Testing decisions

The seams, highest first:

- **Golden headers (W1).** For a corpus of shapes, generated headers are compared with
  checked-in golden files; a change to codegen updates them in the same commit.
- **Compiled assembly (W2).** A `Table<T>` read by index and by key is compiled and its
  instructions checked, as `tests/codegen/` does for a `Tweak` read today.
- **Corpus round trips (W3).** `format` and `rename` over the corpus and the stacks leave
  every file reading the same values, references resolved.
- **Behaviour tests** per phase:
  - W1: drift detection, a wrong-typed value as a located error, defaults, overflow.
  - W3: duplicate IDs, aliases resolving in overlays, `@renamed_from`, asset edges.
  - W2: reload keeping held `Ref`s, stable indices, live-ops rebuilds, a digest-mismatched
    section falling back to the tree.
  - W4: undo and redo per transaction, a stale change set re-checked, the JSON round trip.
- **No-cost proof.** `bench/` rows for load, check and reload on a stack with none of the
  new forms, before and after each phase, recorded in its D-entry.
- **Allocation gate.** `baker_alloc_tests` keeps passing: tables and logs take the
  Config's allocator.

## Phase plan

**W1, views.** Ships `@view`, the project file's `views`, `baker_bake --views`,
`baker_generate_views`, generated `Convert<T>` and `kShapeDigest`, and drift reporting.
Depends on nothing new. Done when an example's shapes generate headers that compile in
every local-matrix configuration, the golden tests pass, a deliberate drift warns in
development and fails `check --baker-strict-views`, and the no-cost rows are unchanged.

**W3, identity.** Ships `@id`, duplicate detection, `rename` with `@aka`, alias resolution,
`(asset KIND)` with the project's `assets` and the reference JSON's edges, and
`@renamed_from`. Depends on W1 for the shape syntax it extends. Done when renaming a record
in the stacks' data rewrites every reference, a shipped-style overlay naming the old key
still merges, the corpus round trips, and the reference JSON's new version is documented.

**W2, baked tables.** Ships `Table<T>`, in-memory tables for development, the stack
`.datb` section for shipping, reload and live-ops rebuilds. Depends on W1 (views) and W3
(IDs). Done when the example loads a baked image in a shipping build, a reload in
development swaps a table under a held `Ref`, the assembly test passes, and benchmarks
record table reads against `find_float`.

**W4, change records.** Ships the change log, transactions with undo and redo, agent change
sets through check, push and promote, and `baker.changes/1`. Depends on W3 (IDs, rename).
Done when the example's agent change set is checked, pushed, promoted and undone, and the
JSON format is documented.

`examples/content-tables` grows with each phase: a shaped table and its view (W1), a
rename with an alias and an asset field (W3), a baked load (W2), and an agent change set
(W4).

## Out of scope

- The data stays the authority: a generated struct never defines the data or its format on
  disk, and no data file needs matching code to be read.
- Not an asset database: Baker describes assets and reports edges, and never imports,
  stores or cooks them.
- No paging or partial loading: whole-stack, in-memory operation stays the model.
- No shared or cloud cache tier, and no content-addressed renaming of caches.
- No real-time multi-user collaboration; change records make it possible later.
- No new language: generated C++ and project-file keys are the only new surfaces besides
  data forms.
- Shapes that vary with defines.

## Deferred

These were decided as later work, not left open:

- Folding commands bound as pure at bake time (reopens D8).
- Struct-of-arrays layout for baked tables.
- A typed reference type for `key_of` view members.
- A C++26-reflection drift checker for hand-written structs.
- Derived-data cache keys, to be looked at again once W2 exists.

## Open questions

None.
