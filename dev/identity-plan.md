# Plan: W3, identity, references and evolution

*Phase W3 of `dev/schema-layer-spec.md` (D269), after W1 (`dev/views-plan.md`, D277–D283).
Terms are `dev/glossary.md`'s. The design note is §2; §3 is the steps, each committed green.*

**Status: approved 2026-10-31; Q1 answered bare symbol, letter-first, Q2 to Q4 as
recommended; in progress.**

| Decision | Choice |
|---|---|
| What ships | `(@id "...")` and `RecordId`; `(@aka old)` with alias-aware merging and `key_of`; `rename`; `(@renamed_from old)`; `format --baker-mint-ids`; `(asset KIND)` with the project's `(assets ...)`, existence checks and `assets` writing `baker.assets/1` |
| Where | Identity in `baker::data` (`<baker/record_id.hpp>`, `src/data/`); alias resolution in `src/data/merge.cpp` and `src/shape/shape.cpp`; the commands in `baker::host`; the rewrites on `tools::migrate_text`'s edit machinery |
| Depends on | W1 (the shape grammar, the generated readers). W2 builds on it |

## 1. What the data says

- **A record's key is its only name today.** Every `key_of` in the stacks names a key
  (`(weapon (key_of weapons))`, `(cancel_into (list (key_of moves)))`), overlays retune by
  key, and write-back targets by path. Renaming `laser` breaks all three.
- **Keys are few and short.** A stack's tables hold tens of records, not thousands
  (`stacks/*/packs/game/*_core.dta`); a rename touches a handful of files.
- **Lexing.** A bare 32-digit ID that happens to be all digits is an error ("integer
  12345678901234567890123456789012 is out of 64-bit range"), and one spelled digits-`e`-digits
  reads as a float. The spec's bare hex form cannot be used as written.
- **Symbols are immortal and capped.** A name costs about 74 bytes and counts against a
  read's `max_new_symbols` (D253). An ID per record, interned, would add 3.7 MB for 50,000
  records and never leave.
- **The merge matches keyed children by tag** (`merge_into`, `src/data/merge.cpp:317`): a
  linear scan for up to 32 keys, an index above. A miss appends the child. `Array::kHasMarker`
  is a subtree-propagated bit the merge already tests for verbs and `(@was ...)` pins; the
  flag word has no free bit.
- **Nothing in the library is random**, and no 128-bit type exists. `tools::migrate_text`
  rewrites tokens from `read_structure`'s spans; write-back (`config_write.cpp`) rewrites a
  value's span and inserts a `(@was ...)` child, and refuses arrays macros or includes
  built (`not_literal`).

## 2. Design note

### 2.1 The transforms, by path class

| Transform | Class | How often |
|---|---|---|
| reading `(@id "...")` into a `RecordId` | load, tool | per record that carries one, when asked |
| duplicate-ID check | tool (a validator) | per merge, only where added |
| alias lookup in the merge | load | per keyed child the lower array lacks, only in arrays with aliases |
| alias lookup in `key_of` checking | tool | per key miss, in `check` and the validator |
| `rename`, `format --baker-mint-ids`, `@renamed_from` rewrite | tool | on demand |
| asset existence, `assets` | tool | per `check` |

Nothing changes on a lookup, a value read or a reload of a stack with no `@id`, `@aka` or
asset field; the merge's miss path tests one bit it tests today.

### 2.2 The ID is a bare symbol whose first character is a letter

`(@id b3c9a7e5f214d68a1e2c3b4d5e6f70812)`: 32 lowercase hex digits, the first of them `a` to
`f`, so the token is never a number to the lexer (an all-digit ID is "out of 64-bit range"
today, and digits-`e`-digits a float). The owner chose the bare form over a string, knowing
a symbol per record is interned for good (about 74 bytes each, against the read's name
cap, D253): tools mint IDs with that first digit, and the parser of an ID accepts only that
form. `baker::RecordId {uint64_t hi, lo}` in `<baker/record_id.hpp>` (reaching only
`<cstdint>`, `<optional>` and `<string_view>`), with `parse(string_view)` and
`write(char (&)[32])`; `Array::record_id()` reads a record's `(@id ...)`, `RecordId{}` for
none.

### 2.3 Duplicates are a validator's finding, in `baker::data`

`baker::id_validator()` walks the merged tree collecting IDs into a `HashMap` on the
Config's allocator and reports two records with one ID as a located error naming both. A
Config pays nothing unless it adds the validator; the kit's `check` always runs it, and the
example adds it. This is how shapes already report (D84): the load itself never fails on
data, which the spec's "error at load" is read as.

### 2.4 Aliases: the merge finds a renamed record on a miss

`(@aka laser)` on a record is metadata, kept by `--drop-metadata` (D265) beside `@id`.
`marker_flag` raises `kHasMarker` for an `@aka` as it does for `@was`, so the merge's miss
path asks one bit the lower array already carries: when set, a tag the lower array lacks
is looked for among its children's `(@aka ...)` values, and a hit merges into that child.
An array without aliases pays nothing, and an array with them pays a scan of its children's
metadata per missed key, which happens once per overlay push or reload. A key that is both
one record's key and another's alias is a located error at both (`id_validator`, as both
are identity). `shape::check`'s `key_of` resolution tries the key, then the aliases of the
table's records, and the kit's `check` lists an alias no layer uses as a finding.

### 2.5 `@renamed_from` is checked, read by views, and rewritten; never rewritten at load

`(@renamed_from old)` on a shape field: `shape::check` warns at a record that has `old` and
not the new name, naming both, and errors at one that has both; the generated reader
accepts `old` for the member (a second `case`); `format` rewrites `old` to the new name.
`get<T>` reads the tree as written: renaming at load would rewrite trees on every merge for
a migration that `format` does once.

### 2.6 Rewrites are token edits over the source map

`rename`, `--baker-mint-ids` and the `@renamed_from` rewrite reuse what write-back and the
migrators have: `read_structure`'s token spans and `tools::migrate_text`'s edit list, with
edits that own their text. `rename` edits the record's tag token in the declaring layer,
every `key_of` value token naming it in every layer of the stack (overlays and mod
directories included), and inserts `(@aka old)` after the record's last child, as the
`(@was ...)` insertion does; a reference inside an array `not_literal` refuses (a macro, an
`#include_dir` file) is listed as an error and nothing is written. Files are written as
`format --baker-write` writes them.

### 2.7 Minting is the tool's, not the library's

The library has no random source and gains none: `baker::host` mints with
`std::random_device` (tool path) in `format --baker-mint-ids TABLE`, `rename` (a renamed
record without an ID gets one) and, later, a promoted change set.

### 2.8 Assets: the lister answers for a file by listing its directory

`(asset KIND)` is a field type; the project's `(assets (mesh ".glb" ".fbx") ...)` names the
kinds and their extensions. `shape::check` reports an extension the kind lacks; the kit's
`check` and `assets` report a file the lister cannot find by listing the file's directory
once per directory per run (the lister lists directories; no new hook). The `assets`
command writes `baker.assets/1`: every edge of the merged stack, sorted by file, line, path.

### 2.9 What this plan changes in the spec

- `@id`'s first digit is a letter, and only that form parses (§2.2).
- Duplicate IDs are a validator's located errors, not a load failure (§2.3).
- `@renamed_from` is not applied at load; views and `format` carry it (§2.5).
- The lister is not extended; existence is a directory listing (§2.8).

## 3. Steps

Each step runs `tools/local_matrix.sh` and clang-tidy before its commit, and records its
result in its D-entry (D284 onward).

1. **`@id`.** `RecordId`, the string form, `Array::record_id()`, `@id` and `@aka` as known
   metadata words, `without_metadata` keeping them, `id_validator`, `tools::find_by_id`.
   Tests: parse and write round trips, a malformed ID located, duplicates named at both
   records, `--drop-metadata` keeping identity, the alloc test over the validator.
2. **`@aka`.** The marker bit, the merge's miss path, `key_of` through aliases, the key-and-alias
   error, `check`'s unused-alias finding. Tests: an overlay retuning by the old key merges
   into the renamed record; a 32-plus-key array (the indexed path); the no-cost rows.
3. **`@renamed_from` and minting.** The shape word, the check warning and error, the
   generated reader's second case (and the golden headers), `format`'s rewrite,
   `format --baker-mint-ids TABLE` with duplicate re-minting.
4. **`rename`.** The command, its dry run and `--baker-write`, the refusals, `(@aka)` insertion,
   a renamed record minted an ID. Tests through the host test game on a stack copy.
5. **Assets.** `(asset KIND)`, the project's `(assets ...)`, `shape::check`'s extension and
   existence checks, the `assets` command, `docs/assets-json.md`, `baker.shapes/1.7`.
6. **The example, the docs and the record.** `examples/content-tables` grows a rename and an
   asset field; `docs/language.md` (identity, aliases, assets), `docs/cookbook.md`,
   `docs/tools.md`; the no-cost rows (`layers`, `cache`, `parse` counts) and the gaps rows;
   the changelog lines in the D-entry.

## 4. Open questions for the owner

Asked with AskUserQuestion before step 1; their answers go in D284.

- **Q1.** `@id` as a string (§2.2) rather than the spec's bare hex symbol.
- **Q2.** Duplicates reported by a validator the kit always runs (§2.3), rather than failing a load.
- **Q3.** `@renamed_from` never applied at load (§2.5): check, views and `format` carry it.
- **Q4.** `rename` writing files directly, as `format --baker-write` does, with no new writer hook.
