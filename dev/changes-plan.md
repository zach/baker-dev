# Plan: W4, change records

*Phase W4 of `dev/schema-layer-spec.md` (D269), the last, after W1 (D277–D283), W3 (D284,
D286–D290) and W2 (D292–D297). Terms are `dev/glossary.md`'s. The design note is §2; §3
is the steps, each committed green.*

**Status: approved 2026-11-06; Q1 to Q3 as recommended; in progress.**

| Decision | Choice |
|---|---|
| What ships | A change log of transactions, each a list of change records (set, add, remove, rename, reorder, append, drop) with whole old and new values, the record's ID, the layer written and the old value's source; `begin_transaction`/`end_transaction` so a console command, a tool action or a promotion is one step; undo and redo per transaction; `baker.changes/1` (`docs/changes-json.md`), the console's `config.changes`, `config.undo`, `config.redo`, and the kit's `promote --baker-json` carrying its transaction; a layer that names a record by its ID |
| Where | The log in `baker::data` (`<baker/changes.hpp>`, `src/data/changes.cpp`), fed by the Config's live edits and commits; the JSON in `baker::tools`; the console and kit commands in `baker::host` and `baker::tools`; ID addressing in the merge |
| Depends on | W3 (IDs, aliases, `rename`), W2 (tables rebuild on every commit, so a transaction's effect reaches them) |

## 1. What the code says

- **The journal is the edit log today** (D93): every live edit through `set`, a tweak or
  `set_values`, as a 216-byte ring entry with up to four nodes of each value, the old
  value's `Source`, and a fate (`Unsaved`, `Superseded`, `Written`, `Reverted`). It costs a
  `set` on the fast path about 30 ns, allocates nothing (a tested contract: 100 edits, no
  allocation), and records nothing about layers, promotions or structure.
- **Every other change is a commit** (`Config::Impl::commit`): a layer pushed, removed or
  toggled, a reload, a restore, a write-back, a promotion. The merge shares every subtree
  it did not change (D75), so two merged roots differ only where something changed, and a
  walk that skips shared pointers finds the changed paths cheaply.
- **`snapshot()` and `restore()`** capture the layer stack (trees shared), the files'
  content and a copy of scratch, and return to it without reading a file; the journal
  marks what a restore drops as `Reverted`.
- **`promote`** values each path of a layer as the stack reads it, plans every write in
  memory, and writes all or nothing: it knows each path's old and new value, and the file
  and line it wrote. `write_back` writes one path. Neither names a transaction.
- **An agent's change set** is layer text: `check_layer_text` under a `LayerPolicy`, then
  `push_layer_text`, then `promote`; a push with a `CheckStamp` checks again when the stack
  moved since the check (D205). A layer names a record by key, or by an alias (D286);
  nothing names one by ID.
- **The console** runs one command per line (`config.set`, `config.journal`,
  `config.snapshot`, `config.restore`); the harness's panels call `set` per control.
- **A merge miss** already tries the lower array's aliases (D286, D290): the place a tag
  that is an ID would be looked up.

## 2. Design note

### 2.1 The transforms, by path class

| Transform | Class | How often |
|---|---|---|
| a `set` with the log off (the journal as today) | hot | per live edit |
| a `set` with the log on: the old and new values as trees | tool | per live edit, where a tool asked for the log |
| a commit's records: the walk of two roots where they differ | load, tool | per commit, only where the log is on |
| undo, redo | tool | on demand |
| the merge's ID lookup | load | per keyed child the lower array lacks whose tag is an ID |

A Config that never turns the log on pays one test per `set` and one per commit; the
journal and the fast path stay what D93 measured.

### 2.2 The log is a second structure, opt-in; the journal stays the fast path's (§4 Q1)

`cfg.set_change_history(n)` keeps the last `n` transactions (default 0: off; the console,
the harness and the kit turn it on). The spec made the journal "a capped view over the
log"; here the journal keeps its no-allocation contract, and the log, which keeps whole
values as trees, allocates for each record. A `set` with the log on leaves the fast path
for that call. A tool that wants the log pays for it; a game that does not pays nothing.

### 2.3 A record, and where each kind comes from

```cpp
struct ChangeRecord {
    ChangeKind kind;        // Set, Add, Remove, Rename, Reorder, Append, Drop
    Symbol path;            // dotted, from the root
    RecordId id;            // the record's (@id ...), or empty
    Ref<Array> before, after;  // the whole value, as a tree; null: absent
    Symbol layer;           // where the new value was written: scratch, or the layer
    Source source;          // where the old value came from
    uint64_t time_ns;
    Symbol renamed_from;    // Rename: the old key, now an alias
};
struct Transaction {
    uint64_t id;            // the session's sequence
    Symbol name;            // "set", a console command, "promote", the reload's trigger
    uint64_t time_ns;
    List<ChangeRecord> records;
    List<WrittenLine> written;  // the files a promotion or write-back wrote
};
```

- **Live edits** (`set`, a tweak, `clear_scratch`): a `Set` record with the path's values
  before and after, from scratch's own arrays; each its own transaction unless one is open.
- **Commits** (`commit`): the walk of the previous root and the new one, skipping shared
  subtrees. A keyed child present after and not before is `Add` (its whole subtree as
  `after`); absent after, `Remove`; both present with other values, `Set` at the leaf
  paths; a removed record and an added one sharing an ID, or the added one's `(@aka ...)`
  naming the removed key, `Rename`; children in another order, `Reorder` at the parent. A
  list whose row in the layer that changed it carries `#append` or `#drop` is `Append` or
  `Drop` with the operand, else `Set`. The transaction's name is the commit's trigger
  (`layer`, `reload`, `restore`, `write_back`, `promote`), and a promotion's transaction
  carries the files it wrote.
- **One transaction per action.** `begin_transaction(name)` and `end_transaction()` nest;
  the console wraps each command, `promote` and `write_back` wrap their commit, a harness
  wraps a drag. Without one open, each change is its own.

### 2.4 Undo and redo are per transaction, in memory (§4 Q2)

Each transaction keeps a snapshot of the Config before it (trees shared, scratch copied,
D133's cost). `undo()` restores the one before the latest, `redo()` the one after; a new
change after an undo drops the redo chain. A transaction that wrote files (`promote`,
`write_back`) is undone in memory the same way, and the files stay as written: the log
names them, and a reload would bring them back. That is said plainly rather than the
alternative, rewriting the files from the records, which would have `undo` write what
`promote` writes with none of its refusals; a follow-up can add it when a tool wants it.

### 2.5 A layer names a record by its ID (§4 Q3)

A keyed child of a layer whose tag spells a record ID (32 hex digits, letter-first, as
`RecordId::parse` reads it) merges into the lower record that carries that ID, as an alias
does on a miss: `(weapons (a7d2c41e9b06f58a3c1d2e4f60718293 (damage 5)))`. The lookup runs
only on a miss, only when the tag parses as an ID, and only when the lower array holds
records with IDs (the parser counts them as it counts aliases). The record keeps its key.
`check`'s `key_of` and the shape checker see the merged tree, so nothing else changes.
Renaming a record from a change set is deferred: `promote` writes values, and a rename
rewrites keys and references, which the kit's `rename` does; an agent runs it as a command
of its own, before or after its change set.

### 2.6 `baker.changes/1`

`tools::write_changes_json(cfg, sink)`: `{"schema": "baker.changes/1", "transactions":
[...]}`, one object per transaction with `id`, `name`, `time`, `written` and `records`, each
record with `kind`, `path`, `id`, `before` and `after` as Baker text (as `baker.reply`
writes values), `layer`, `source` and `time`. The console's `config.changes` writes it; the
kit's `promote --baker-json` and `--baker-write` write the promotion's transaction.

### 2.7 What this plan changes in the spec

- The journal stays as D93 built it; the log is opt-in beside it (§2.2).
- Undo is in memory; files a transaction wrote stay written (§2.4).
- A change set names a record by ID (§2.5); renaming from a change set is deferred.

## 3. Steps

Each step runs `tools/local_matrix.sh` and clang-tidy before its commit, and records its
result in its D-entry (D298 onward).

1. **The log and live edits.** `<baker/changes.hpp>`, `set_change_history`, transactions,
   `Set` records from `set`, tweaks and `clear_scratch`, `each_transaction`. Tests: values
   beyond four nodes, nesting, the cap, the fast path unchanged with the log off (the
   allocation test), the cost with it on.
2. **Commits.** The walk of two roots: `Add`, `Remove`, `Rename`, `Reorder`, `Set`,
   `Append`, `Drop`; the trigger as the name; `promote`'s files. Tests over each kind.
3. **Undo and redo**, the console's `config.undo`, `config.redo`, `config.changes`, a
   command as one transaction. Tests: undo over a set, a push and a promotion; redo; the
   chain dropped by a new change.
4. **The JSON** and `docs/changes-json.md`; `promote --baker-json`. Tests: the kit's.
5. **A record by ID in a layer**: the merge's lookup, the parser's count, `check`. Tests:
   an overlay by ID, by alias and by key together; the no-cost rows.
6. **The example, the docs and the record**: `examples/content-tables` with an agent
   change set checked, pushed, promoted and undone; `docs/language.md`, `docs/tools.md`,
   `docs/cookbook.md`; the no-cost rows; the gaps rows; the changelog lines.

## 4. Open questions for the owner

Asked with AskUserQuestion before step 1; their answers go in D298.

- **Q1.** The change log opt-in beside the journal (§2.2), rather than the journal becoming
  a view over an always-on log that allocates on every live edit.
- **Q2.** Undo in memory, files a transaction wrote staying written and named (§2.4),
  rather than undo rewriting the files from the records.
- **Q3.** A layer names a record by ID through the merge (§2.5), and renaming from a
  change set is deferred to a follow-up.
