# The shape check of a push: the overlay, not the tree

*For the six-part TD review's finding 11 (`dev/td-review-2026-11.md`). The owner chose:
check only the overlay, with cross-record rules indexed by what changed; a held report is
not checked again.*

## Today

`shape::validator()` is one call over the whole stack:
- it builds the shape set from every layer's session;
- it checks every record of the merged tree;
- it reports what it finds.

It runs at three points:
1. **A check of a candidate layer** (`check_layer`, a push with a `LayerPolicy`,
   `LayerChecker`), with the candidate on top. Every finding in the tree is the
   candidate's, so under `warnings_refuse` one old warning in the base refuses every
   overlay.
2. **After every merge** in a development build (D307 keeps it there, and makes it
   `checks_only` in shipping).
3. **Both, for a push whose report still holds** (`LayerOptions::checked`). The check ran
   when the report was made, and after the merge the whole tree is checked again.

The cost is O(tree) at each point. The review's numbers: a 122 KB stack, 157 µs a check,
and 829 µs to build the rule paths (finding 12, a separate fix).

## The shape

- **`ValidatorStack::changed`.** The candidate's own tree, when the stack is a candidate's;
  null otherwise. A validator may judge only what the candidate touches.
- **The shape check, scoped.** For a candidate, `shape::check` walks the merged tree only
  down the paths the candidate's tree names. Each record the candidate adds or changes is
  checked whole, as merged, with its fields from below. Records it does not name are not
  visited.
- **Cross-record checks.** Two kinds reach outside the records the candidate names:
  - **A key field** (`(key_of TABLE)`, D-R2) in a record the candidate names is looked up in
    the merged table, as now.
  - **A record the candidate removes or renames** may be named by keys elsewhere. The key
    uses of the base are indexed once per stack, by table and key. A removal looks up what
    names it, and checks those records.
- **Shape declarations.** A candidate that declares or narrows a `#shape` changes what
  governs records it does not name, so it is checked whole, as now. So is any stack whose
  shapes changed since the last full check.
- **A held report.** A push whose report still holds runs no `any_thread` validator after
  its merge. The check that made the report ran them over the same stack.

## The owner's answers

1. **A candidate's report holds only its own findings:** those in records it adds, changes
   or removes, and in records that name what it removed.
2. **After a push in a development build, the scoped check too.** After a reload, a toggle
   or a restore, the whole tree.
3. **Removals are followed through a key index.**

## Measuring

`shape::validator` over the review's stack: a candidate of one record and of a hundred, at
10k and 100k records, before and after; and a push with a held report.
