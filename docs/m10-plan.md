# M10 plan: harness instrumentation

Spec §10 (the M10 row) is the requirement; spec §4 ("Loop telemetry and
the edit journal"), §1 ("Directives": pack metadata and `origin`), and §5 ("Load-time
checking": the repetition report, owner findings, and the machine-readable output) say
what each piece is; `docs/background/reading-a-studio.md` §2, §7, and §9 are the why. The
open questions at the end were answered by the project owner on 2026-10-12 (D90), and the
design below follows the answers. This file is deleted when M10 is done.

## Scope

From the M10 row: loop statistics (aggregating M3b's reload records), the edit journal,
pack harness metadata (`owners`, `for`, `stage`), `origin` provenance, the repetition
report, owner findings, and machine-readable check output.

Done when:

1. `loop_stats()` reports the rolling median and worst edit-to-see time per file and per
   layer, and counts a failed reload.
2. `unsaved()` lists a tweaked-but-not-written-back value.
3. `source()` names a value's `origin` tool.
4. A subtree repeated in three files appears in the repetition report with all three
   locations.
5. A one-owner pack is reported.
6. Check JSON output is schema-versioned and stable across runs.
7. All instrumentation adds no allocations on the hot path and compiles out under
   `MOIRA_SHIPPING`.
8. The core stays within the 15,000-line budget (if not, the repetition report moves into
   a separate tool, recorded in `docs/decisions.md`).

Not in M10: survey prompts, user identity, network reporting (reading-a-studio §9,
"considered and not adopted"); snapshots serialized to DTA (D42 left it open; nothing has
asked for it since).

## What exists

- **Reload telemetry (M3b, D42):** every change makes a 120-byte `ReloadRecord` (six
  timed stages, `origin_ns`, the outcome, the first changed file) in a ring allocated once
  (default 256), visited by `each_reload`. A reload that finds nothing records
  `Unchanged`. Nothing aggregates the records, and nothing marks when a change became
  visible.
- **`unsaved()` (M3b):** the paths in scratch. Criterion 2 is met already; the journal
  is what M10 adds around it.
- **Packs (M8b, D82):** `(pack name (level L) (requires ...))` is recorded as a `Pack`
  (name, level, rank, file, requires). Other children are accepted and dropped.
- **`check` (M4, M8b):** errors, the demand report, and the one-way rule's warnings, in
  `CheckReport`, written as text (`write(report, sink)`). `reference(ctx)` knows nothing
  of packs.
- **What the harness taught (M9):** a host that polls for changed files four times a
  second fills the 256-record ring with `Unchanged` records in about a minute. Statistics
  computed from the ring would see only the last minute of edits.

## Design

### 1. Loop statistics (moira::data, Config)

- **`cfg.mark_visible()`**, called by the host once a frame when it presents, stamps
  `visible_ns` on every record completed since the last call that changed something. With
  nothing pending it is one predictable branch and reads no clock.
- **Edit-to-see**, per record that changed something: `visible_ns - origin_ns` when the
  host gave both; else `end_ns - origin_ns` (the substrate's share); else nothing, and the
  record still counts as a reload.
- **`cfg.loop_stats()`** returns, per file and per layer: reloads, failures
  (`ParseError`, `Refused`), the median and worst edit-to-see over the last N edits, and
  the median and worst reload time. The samples live in a fixed table allocated once
  (Q1): up to 32 files and 16 layers, each with its last 32 edit-to-see and reload times
  (about 10 KB per Config); the least recently changed file gives up its row to a 33rd.
  Only records that changed something feed it, so a polling host cannot push edits out.
  A record is attributed
  to its first changed file and that file's layer (`ReloadRecord` gains `layer`): most
  reloads are one saved file.
- **`ReloadRecord`** gains `visible_ns` and `layer` (120 to 136 bytes).
- Compiled out under `MOIRA_SHIPPING`, as the records are: `mark_visible()` does nothing,
  and `loop_stats()` is empty.

### 2. The edit journal (moira::data, Config)

- Every live change through the Config (`set`, `Tweak::set`, `set_values`, and therefore
  the console and panels) appends an entry: the path, the old and new values, the
  provenance of the old value, a time from the Config's time source, and its fate.
- **Values** are held inline (Q2): up to four nodes each for the old and new values, with
  the true count; a longer value keeps its first four.
- **Fate:** `Unsaved` until a `write_back`, `write_back_to`, or `promote` writes the
  path (`Written`, with the file and line), a `clear_scratch` drops it (`Reverted`), or a
  later edit of the same path supersedes it (`Superseded`).
- **`cfg.journal(fn)`** visits entries oldest first; `unsaved()` stays as it is.
- **Storage** is a ring allocated once, like the reload history
  (`set_journal_history(n)`, default 256; 0 turns it off): about 160 bytes an entry, 40 KB
  for 256.
- `set`'s fast path stays allocation-free: appending writes one ring slot.
- Compiled out under `MOIRA_SHIPPING`, where `set` is refused anyway.

### 3. Pack harness metadata (moira::data, the parser)

- `Pack` gains `owners`, `audience` (the `for` form; `for` is a keyword), and
  `stage` (`genesis`, `custom`, `product`, or `commodity`; another symbol is a located
  error at load). Absent forms are empty. Caches (D72) record and replay them with the
  rest of the declaration.
- `reference(ctx, out, &session)` gains a **Packs** section: each pack's name, level,
  file, requires, owners, audience, and stage. The generated reference is then the
  harness register of reading-a-studio §7.

### 4. `origin` provenance (moira::data)

- `(origin "economy.xlsx" (tool sheets-export))` at the top level of a file records,
  for that file, where it came from. The parser drops it from the tree, as it drops a
  pack declaration, and keeps it in the session (`session.origin_of(file)`); caches
  replay it.
- `Source` gains `origin` and `tool` (24 to 40 bytes), filled by `source()` from the file
  that supplied the value. Values from a file without one have them empty.
- The reference lists, per tool, the files it generated: which external tools feed the
  config.

### 5. The repetition report (moira::data; `check` reports it)

- `moira::find_repeats(trees, options, out)` (Q3: in moira::data, so it needs no
  Context and data-only projects have it) hashes every array and command subtree of the
  trees it is given,
  structurally: node types and values, recursively, after macro expansion, ignoring file
  and line. Equal hashes are confirmed with `structurally_equal`.
- **Reported:** every subtree of at least `min_nodes` nodes (default 6, counted
  recursively) that appears at least `min_repeats` times (default 3), with every location
  (file, line).
- **Only the largest.** A repeated subtree inside a reported one, at the same places, is
  not reported again.
- **Not counted:** a macro's expansions. They share the macro's arrays (spec §1), so
  repeated identity is a word already made, not one to make. Identical pointers count
  once.
- The options are `RepeatOptions{.min_nodes, .min_repeats}`. `check` takes them
  (`CheckOptions`), calls `find_repeats` over the trees it checks, and puts the result in
  `CheckReport::repeats`, so one report holds every finding.

### 6. Owner findings (moira::script, `check`)

- With the session (`check(tree, ctx, report, &session)`), every pack that declares
  `(owners ...)` with fewer than two is an informational finding in
  `CheckReport::findings`, located at the pack's file. A pack without `(owners ...)` is
  not reported (Q4), so a project that does not declare owners pays nothing.

### 7. Machine-readable output (moira::script)

- `write_json(report, sink, &session)` writes one JSON object: `"schema": "moira.check/1"`,
  then `errors`, `warnings`, `demand`, `repeats`, `findings`, and `packs`.
- **Stable across runs:** every list sorted by file, line, then name; no pointers,
  hashes, times, or absolute paths beyond what the caller passed; strings escaped by one
  small writer, numbers printed by the library's own formatter. A test checks it twice
  and byte for byte against a checked-in expected file.
- **Documented** in `docs/check-json.md`: every field, and the rule that a field is added
  only with a new schema minor version, and removed or renamed only with a new major.
- `<moira/std_script.hpp>` adds `to_json(report)` returning a string.

### Shipping (Q5)

Under `MOIRA_SHIPPING`, the runtime instrumentation compiles out (§1, §2):
`mark_visible()` does nothing, and `loop_stats()` and the journal are empty. The tools
(§5–§7) stay in every build, as `check` and the reference do now: they cost nothing
unless called, and a build step or CI can run them against shipping defines. What is data
(§3, §4) is read in every build, as packs are now, and `source()` reports it.

## Review (acton-review)

| Piece | Path | Cost |
|---|---|---|
| `mark_visible()` | **hot** (once a frame) | one predictable branch when nothing is pending; a clock read and a few ring writes when something is |
| loop statistics, written | load (once per reload that changed something) | a few stores into a fixed table (Q1); no allocation |
| `loop_stats()`, read | tool | computes medians on demand; may allocate its result |
| journal append | tool, with M3b's allocation-free contract on `set` | one ring slot, no allocation (Q2) |
| pack metadata, `origin` | load | one form per file, kept in the session |
| `Source` +16 bytes | tool | `source()` is not on a gameplay path |
| repetition, owners, JSON | tool | a walk and a hash table per `check`; nothing at gameplay time |

- **Nothing touches lookup, evaluation, dispatch, or tweak reads.** The codegen test
  (one load per `Tweak<float>` read) and every allocation test stay as they are; M10 adds
  allocation tests for `mark_visible`, a fast-path `set` with the journal on, and a
  reload that feeds the loop statistics.
- **"Where there is one, there are many":** `mark_visible` is called every frame for the
  life of a session; it must cost nothing when nothing changed, and its benchmark row
  (`bench_cache`'s Config rows, or a new one) shows it.
- **The ring-flooding finding** (M9) is the reason Q1 recommends samples kept apart from
  the history: a statistic that a polling host silently truncates to a minute is wrong
  data, not slow data.
- **Gap 10 (tooling in the core)** grows: the repetition report (moira::data) and JSON
  (moira::script) are tool code. They add no field to a hot structure and cost nothing
  unless called; shipping builds keep them (Q5), as they keep `check` now.

## Budget

| Area | Estimated lines |
|---|---:|
| Loop statistics: `mark_visible`, the sample table, `loop_stats` | 160 |
| Edit journal: the ring, fates, `journal()` | 150 |
| Pack metadata, and its record in caches | 50 |
| `origin`: parser, session, caches, `Source` | 80 |
| Reference: packs and origins | 50 |
| Repetition report (moira::data) | 200 |
| Owner findings | 30 |
| JSON writer | 180 |
| **Core total** | **~900** |

The core goes from 10,116 to about 11,000 of 15,000 (D69): the fallback in criterion 8 is
not needed.

## Order

1. Pack metadata and `origin` (data, the parser, caches, `Source`).
2. Loop statistics and `mark_visible`.
3. The edit journal.
4. The repetition report (moira::data), and `check`'s owner findings.
5. JSON output and its schema document; the reference's packs and origins.
6. The harness's Loop panel shows `loop_stats()`, and a Journal list joins it; results
   (D-entry), and this file deleted.

## Open questions

**Answered 2026-10-12 (D90):** Q1, Q2, Q4, and Q5 as recommended; Q3 in moira::data.

**Q1. Where loop statistics keep their samples.**
- **Recommended.** A fixed table, allocated once: up to 32 files and 16 layers, each with
  its last 32 edit-to-see and reload times (about 10 KB per Config); the least recently
  changed file gives up its row when a 33rd arrives. Fed when a record that changed
  something completes, so polling cannot push edits out.
- **From the history ring, on demand.** No new storage; `loop_stats()` walks the 256
  records. Simple, but a host polling four times a second keeps about a minute.
- **The ring, without `Unchanged` records.** Stop appending unchanged reloads to the
  history (counting them instead). Keeps one store, but changes M3b's telemetry contract.

**Q2. How a journal entry holds its values.**
- **Recommended.** Inline: up to four nodes each for the old and new values (a scalar, a
  vector, a color: the common cases), with the true count; a longer value keeps its first
  four. About 160 bytes an entry, 40 KB for 256, allocated once; appending never
  allocates.
- **A reference to each value array.** Exact for any size, but the fast path changes
  scratch in place, so each `set` would copy its array: an allocation per edit.
- **Paths and fates only.** The smallest; the journal then answers "what was changed and
  kept" but not "from what to what".

**Q3. Where the repetition report lives.**
- **Recommended.** In `check` (`moira::script`), as the spec says: one walk serves errors,
  demand, the one-way rule, and repetition, and one report holds them.
- **In moira::data.** `moira::find_repeats(trees, options)`: needs no Context, so
  data-only projects get it; `check` calls it.
- **A separate tool** (`tools/moira_repeats`), outside the core: the spec's fallback,
  used now although the budget does not require it.

**Q4. Which packs an owner finding covers.**
- **Recommended.** Packs that declare `(owners ...)` with fewer than two names, as the
  spec says. A project that does not declare owners pays nothing.
- **Every pack.** A pack with no `(owners ...)` counts as none, so it is reported too:
  catches the unowned pack, but every pack of a project that does not use owners is a
  finding.

**Q5. What shipping builds compile out.**
- **Recommended.** The runtime instrumentation (`mark_visible`, the statistics, the
  journal), as the spec says. `check`, the repetition report, JSON, and the reference stay
  in every build, as `check` and the reference do now: they cost nothing unless called,
  and a build step or CI may run them against shipping defines.
- **The tools too.** `check`, the reports, JSON, and the reference compile to stubs
  under `MOIRA_SHIPPING`: a smaller shipped library, but a shipping-define CI run cannot
  check its data.
