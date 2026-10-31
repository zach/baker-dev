# Glossary

The terms of the schema layer (`dev/schema-layer-spec.md`), and the existing terms it
builds on. One meaning each; the Avoid line names the synonyms we do not use. Layer, level,
shape, record and table keep their meanings from `docs/language.md`.

**Layer.** One file or directory in a Config's stack, merged by tag over the layers below
it. An overlay is a kind of layer, not a separate thing.
Avoid: level, overlay (for layers in general).

**Level.** A pack's rank among the levels the host names, hardest first. A harder pack
may not depend on a softer one.
Avoid: layer, tier.

**Shape.** A declaration, in data, of what an array it governs should hold: its fields,
their types and their metadata. Data stays the authority; a shape describes it.
Avoid: schema (for one shape), type, tag definition.

**Record.** A keyed child of a table, tagged by its own name, such as `dragon` in
`(bosses (dragon ...))`.
Avoid: entry, row, instance, object.

**Table.** An array whose keyed children a shape governs.
Avoid: collection, list.

**Shape set.** Every shape the merged stack declares, taken together, with one digest.
Avoid: schema.

**View.** A C++ struct generated from a shape marked `(@view)`. It reads data and never
defines it: the data on disk stays self-describing and readable without it.
Avoid: model, binding, DTO.

**Baked table.** A packed, load-in-place image of a table's records, read through
`Table<T>`.
Avoid: cache (the tree cache already has that name).

**Record ID.** A random 128-bit identity written inside a record as `(@id ...)`, which
survives renames of its key. References still name keys; the ID anchors renames, change
records and tools.
Avoid: GUID, UUID.

**Alias.** An old key a record still answers to after a rename, written `(@aka old)` and
kept until someone removes it.
Avoid: redirector.

**Asset reference.** A field of type `(asset KIND)` naming a file outside Baker that the
data depends on, such as a mesh or a texture. Baker reports it; it never imports or cooks
it.
Avoid: path, link.

**Change record.** One edit: a path with its whole old and new values, or one structural
edit to a table's records. Change records group into transactions, which undo steps over.
Avoid: journal entry, diff, patch.

**Contract stack.** The layers the project file names for views: only their shapes generate
C++, so an overlay or mod never changes a compiled struct.
Avoid: base layers, core stack.

**Change set.** An agent's proposed edits, written as layer text, checked before it is
pushed and promoted.
Avoid: patch, proposal, diff.
