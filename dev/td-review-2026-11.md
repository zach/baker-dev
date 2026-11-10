# TD review of Baker in six parts (November 2026)

A gamedev-td review of main at 0985b2c. Baker was divided into six parts, each reviewed
on its own with measured numbers: the data core; config and layers; script; shapes; baked
data (cache, views, tables, the stack image, record IDs); tools and the host kit.
Benchmarks are release builds on the review machine; instruction counts are cachegrind.

Each finding is marked:

- **fix**: the cost is clear and the remedy is local;
- **decide**: the remedy changes behaviour, the API, or what ships, so the owner chooses;
- **note**: real, but small or rare; recorded for the gaps table.

## Ranked across all parts

| # | Part | Finding | Evidence | Mark |
|---|------|---------|----------|------|
| 1 | Data core | A macro bomb is refused only after it expands | 419 bytes of text: 1.65 s and 1.35 GB before the refusal | fix: an expansion cap, documented beside `max_new_symbols` |
| 2 | Tools | Live always adds four whole-tree validators, in shipping too, and runs them on every merge, unchanged ones included | 8.5 ms a merge at 50k records; `id_validator` is 62%, building path `Str`s and tag vectors per array | fix the cost; decide whether shipping keeps them |
| 3 | Tools | `format` is quadratic in `Formatter::align` | 19 s on a 2.7 MB file | fix |
| 4 | Baked | The stack image loads slower than the text it replaces | 29–35 ms against 16–18 ms; the byte-wise FNV `image_hash` is 13 ms; data is copied several times | fix: `hash_bytes`, one arena, read in place |
| 5 | Script | Every task frame takes 4 KB | 10k tasks: 41.5 MB, against 4 MB at 256 B (64×; D67 understates it) | decide: size classes |
| 6 | Baked | Any change rebuilds every table when a parent array is not shared | 1.71 ms against 0.012 ms (140×); 21 ms at -O0. The merge returns new arrays for overridden records | fix: rebuild per parent |
| 7 | Data core | Crafted names collide in the unseeded symbol table | 38× slower lookups | fix: a seeded probe index |
| 8 | Config | A `get<T>` above a pending live edit merges per read | 46 µs on 122 KB, about 2 ms on 2 MB, per read | decide |
| 9 | Config | Reload runs entirely on the game thread | 2.3 ms for a 122 KB edit, 29 ms for 4.2 MB; a quiet poll costs 2 mallocs per file through `fopen` | decide |
| 10 | Shapes | An undeclared field builds a message per field per record per push | about 560 ns and 9 allocations per record per push (8× a clean push) | fix: report once per (shape, tag) |
| 11 | Shapes | The validator re-checks the whole tree each push, and again on a held push | O(tree) per push | decide: skip `any_thread` validators on a held report, or check O(overlay) |
| 12 | Shapes | `each_ruled` rebuilds the path string per array | 829 µs, against 157 µs for the check itself | fix |
| 13 | Config | `scratch_root` is quadratic in pending edits | 401 edits: 2 ms | fix |
| 14 | Data core | Sorted mode is slower than linear when cold, at every size | 446 against 137 ns at 20 children; 636 against 484 at 1,000 | fix: keep hash bits in the tag run |
| 15 | Shapes | Keyed shapes test `governs` per array | 157 → 417 µs with 16 keyed shapes | fix: bucket by depth |
| 16 | Config | Every change rebinds every tweak, one at a time | 62 ns a tweak, 54× the merge | fix |
| 17 | Shapes | Wide records: `Field` is 200 B, `std::find` over seen is O(F²), `walk` revisits fields | quadratic in fields | fix |
| 18 | Tools | The console has no per-frame budget, and its buffers are unbounded | 1,000 queued requests: 1.74 ms in one frame | decide |
| 19 | Tools | A shipping Live links tool code and binds `config.*` | about 420 KB | decide |
| 20 | Shapes | Tool code ships with the validator | about 20 KB | decide: move it, or `-ffunction-sections` |
| 21 | Baked | The image's table section is 36% relocations, with dead ID columns and duplicate strings | size | fix the duplicates; decide the dead ID columns |
| 22 | Baked | `build_table` reads every record twice | about 23% of the build | fix |
| 23 | Script | `ctx.call` and `handle` allocate per call with 6 or more arguments | `kCallSizes = 8` against `kMaxMessageFields = 8` | fix, with an N+1 test |
| 24 | Script | `Context::call` fills through the general `Array::set` | 458 instructions a call | fix |
| 25 | Script | A `float` result goes through `float_to_decimal_double` | 25–45 ns a result; `bench_call`'s "native method" row measures this, not dispatch | decide |
| 26 | Script | Thread tasks rebind every capture each tick | 93 ns with 8 captures | fix |
| 27 | Config | The layer check interns each leaf's dotted path as a permanent symbol, refused ones included | 15% of the check, and symbols that never go away | fix |
| 28 | Tools | The Watcher uses `std::filesystem` and the global heap, and `watch()` is quadratic | 67 allocations | fix |
| 29 | Data core | A curve read from the tree costs 5× a baked one | 95 against 20 ns | decide |
| 30 | Baked | Generated structs keep field order and its padding | `Scalars`: 40 B, could be 32 | decide |
| 31 | Script | Capture scans globals linearly | linear | fix |
| 32 | Shapes | A whole shape check with a `(key_of TABLE)` field is quadratic: `Checker::table_for` looks for the table outward from each record, scanning the wide table array each time | 6 s at 100k records, against 13 ms without the field; found while building D316 | fix: remember a miss per (array, segment) |

## Notes

- **Data core:**
  - `Path()` zeroes 336 B (11 ns);
  - there is no N+1 test for the `ValueList` spill at 9;
  - `sort_for_lookup` scratch uses the default allocator.
- **Config:**
  - `set()` grows with tweak and scratch counts: 8.3 µs at 2,000 tweaks;
  - the registry spinlock busy-waits with no backoff;
  - wide arrays in the merged root have no index: 733 ns.
- **Script:**
  - `type_named` is a linear scan;
  - static guards;
  - `eval_request`'s `keyed()` is quadratic.
- **Shapes:**
  - allocations come from the default allocator;
  - `names()` has a static guard;
  - `check_rules` uses a linear `find_if`;
  - the `thread_local` in rules has no fiber note;
  - `shape.hpp` says schema 1.7 while 1.8 is written;
  - `find_repeats` costs 1.8 ms and 20k allocations by default.
- **Baked:**
  - `Table::operator[]` uses an `imul` stride;
  - `read_table`'s damage checks are partial;
  - debug readers are 21× slower;
  - `find_by_id` builds a `Str` per array;
  - the release flags lack `-fno-rtti`.
- **Tools:**
  - stamp polling is a 100 ms spike, two syscalls per file;
  - the `t_output` `thread_local` in `SimRun::step` has no fiber note.

## The owner's decisions

- **2, Live's validators:** dev only; in dev they run only when a merge changed something.
- **5, task frames:** size classes.
- **8, reads above a pending edit:** merge once and cache it until the next edit.
- **9, reload:** read and parse off the game thread; the game thread only swaps the result,
  when the host chooses. Polling uses the stamp check, not `fopen`.
- **11, shape pushes:** check only the overlay, with cross-record rules indexed by what
  changed; a held report is not re-checked.
- **18, console:** a per-frame budget set by the host, and a byte cap per connection.
- **19 and 20, shipping size:** split tool code out of shipping; bind `config.*` only
  outside it.
- **21, image ID columns:** a table keeps one only if its view asks for `find_by_id`;
  strings are de-duplicated.
- **25, float results:** widen exactly; convert to the shortest decimal only when printing.
- **29, curves read from the tree:** the first sample bakes the curve and caches it beside
  the node until it changes.
- **30, generated structs:** reorder fields by alignment.

## Status, 2026-11-10

Every finding is closed or decided; the D-entries hold the measurements, and the gaps table
(`.claude/skills/gamedev-td/references/baker-gaps.md`, "The 2026-11 review") lists what each
left open.

| Finding | D-entry | Status |
|---|---|---|
| 1 | D306 | closed |
| 2 | D307 | closed: checks only in shipping (not dev-only, which would have let a shipping Live accept a bad overlay); the run-on-change skip was tried and dropped |
| 3 | D308 | closed |
| 4 | D309, D325 | closed; in-place loading not built, by the owner's choice |
| 5 | D310 | closed |
| 6 | D311 | closed for an edit elsewhere; a record's own edit still rebuilds the table |
| 7 | D312 | closed |
| 8 | D313 | closed |
| 9 | D314 | closed |
| 10 | D315 | closed |
| 11 | D316 | closed |
| 12, 15, 17 | D318 | closed |
| 13, 16 | D317 | closed |
| 14, 22 | D320 | closed |
| 18, 19, 20 | D322 | closed |
| 21, 30 | D324 | closed |
| 23, 24, 26, 31 | D319 | closed |
| 25 | D323 | kept: the owner chose the present conversion |
| 27, 28 | D321 | closed |
| 29 | D323 | closed |
| 32 | D318 | closed (found while building D316) |

Of the notes, one no longer holds: "the release flags lack `-fno-rtti`". `CMakeLists.txt`
compiles with `-fno-rtti` (and `/GR-` on MSVC) unless `BAKER_RTTI` is set. `shape.hpp`'s
schema comment is corrected with this record. The others stand as recorded.
