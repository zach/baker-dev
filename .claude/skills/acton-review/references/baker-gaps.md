# Baker under the data-oriented lens: known gaps

**Status, 2026-10-15, after D133: gaps 1–26, 29 and 31 are closed; 27, 28, 30 and 32
below are open.** D132 closed the write-back batch (22–25), D133 gap 29. D127 closed gap 31 (the owner's choice in `docs/allocation-review.md`), and
D128–D131 the review's other findings: edit-mode source maps, sessions, script callables
and handlers on their owner's allocator, and the first arena block and text copies. D124 closed the hot-path batch (13–18), D125 gap 26, D126 the load-path batch
(19–21). A fresh review of the core (2026-10-14) fixed what it found that was a bug or a
measured cost (D116–D122) and recorded the rest here, for later. D117's deferral costs one
predictable branch per host `eval`, none per call. Before that, **2026-10-13, after D100:
no open gaps.** First audited 2026-10-11 at M6 step 1 (`23f4142`); the audit's reasoning
for each closed gap is in its D-entries. Sizes are GCC 13 on x86-64. Each gap names its
path class (SKILL.md, review step 1). When one is fixed, or accepted as a deviation, record
it in `docs/decisions.md` and update this file.

| Gap | Decision | When |
|---|---|---|
| 1, allocator hook | per-Context allocators (D62), an arena per load (D64, D65) | **done** |
| 2b, tag lookup | the tag run beside each array's nodes (D76, D77) | **done** |
| 2, 3, hot/cold layout, dispatch loads | functions: hot fields first in the record (D78); methods measured and accepted (D100) | **done** |
| 4, one item at a time | `handle_all` broadcast, one lookup per run of a type (D53, D66) | **done** |
| 5, per-array refcounts | one count per load (D64, D65) | **done** |
| 6, objects' vtable and parameters | parameters: the Context's allocator (D98); the vtable measured and accepted (D100) | **done** |
| 7, coroutine frames | per-Scheduler pool with a frame budget (D58, D68) | **done** |
| 8, exceptions | throwing forms removed entirely (D54) | **done** (D59) |
| 9, `Error` size | `Error` becomes a handle (D55) | **done** (D60) |
| 10, tooling in the core | accepted as a standing rule; link flags documented (D100) | **done** |
| 11, the STL in public headers | header-only vocabulary types only (D60); `BAKER_USE_EASTL` inside (D62) | **done** |
| 12, the reload history under polling | loop statistics keep their own samples (D90 Q1, D92) | **done** |

## Open gaps (the 2026-10-14 review)

Found by the review's four readers and not yet acted on. **Evidence**: *reproduced* means a
driver or test showed it; *read* means the code says so, but nothing measured it. Measure
before fixing (SKILL.md, step 5), and do not fix one without a cost that justifies its code.

| Gap | Path | Finding | Evidence, or status |
|---|---|---|---|
| 13, a C++-to-script call allocates | hot | `Context::call(object, method, args)` builds a `Vec` and then `Array::make_from`: two allocations per call, also for every `ObjectBase::handle` that falls through to a script handler. One `Array::make(n + 2)` would do. | **closed**, D124: a cached command per size, 0 allocations |
| 14, messages skip the handler bit | hot | `send()` walks the type and its bases for a script class on every method call once any `{class}` exists; the one-load `handled_names` test is only in `detail::scripted`. Moving it into `handler_for` saves the walk. Add a `bench_call` row with an unrelated class loaded. | **closed**, D124: the bit before the walk |
| 15, `Args::array` copies | hot | A `const Array&` parameter copies the node and pushes it on `kept` (two refcount atomics) even when the argument is a literal array the command already holds. | **closed**, D124: literal arrays returned as held |
| 16, debug-build argument indexing | hot (debug) | `ArgIndex::of(I)` is not forced to compile time, so MSVC's debug build, a stated target (D62), runs a loop per argument per call. | **closed**, D124: compile-time indexes |
| 17, `Slot` padding | hot | `Context::Impl::Slot` is 40 bytes; reordered it is 32, two to a cache line. | **closed**, D124: 32 bytes, asserted |
| 18, layer records ignore the Config's allocator | load | D61 says a Config's tables take its allocator; `LayerState`'s strings and `deps`, `read()`'s deps, `copy_scratch`, `reread`'s `fresh` and `promote`'s `virt` use the default one. `baker_alloc_tests` passes because nothing reaches `operator new`. | **closed**, D124: the Config's own records; the rest is D61's (gap 31) |
| 19, the lexer scans strings a byte at a time | load | `lex_string` runs an escape-aware loop where the closing quote is always the first `"`; the newline count is a second loop, written twice; a `;` comment is skipped a byte at a time. `memchr`-style scans and `std::count` would do. | **closed**, D126: find and count, 26% on text with long strings |
| 20, the shape checker searches and allocates | load | `walk()` finds a tag's shape by a linear scan of every shape, for each tagged array; `instance()` allocates a `seen` vector per governed array and searches it per field. | **closed**, D126: by index, stacks; 3.8× and 12 allocations |
| 21, freshness re-serializes macros | load (development) | `cache.cpp`'s `fresh()` prints every session define's body to text to hash it, on every check of every layer. | **closed**, D126: hashed from nodes, 2.9× |
| 22, write-back merges twice | tool | `write_into` merges the whole stack to see whether a layer above hides the value, then `commit` merges again; a `supplier` check answers the first. `source_in` is also computed twice. | **closed**, D132: a `supplier` check, no second merge; `source_in` once. `write_back` −10% instructions, −15% time |
| 23, session copies | tool | `promote` copies the session below for every leaf, and `refresh_read` copies a whole `LoadSession` on every commit even when the top layer's session has not changed. | **closed**: `refresh_read` by D129; `promote`'s copy by D132, one per promote, not per leaf |
| 24, a write's time is in no stage | tool | `write_into` and `promote` start the parse clock after the writer runs, so the write's time falls in no stage, against `finish()`'s promise that the stages tile the change. The "reread, time, mark ParseError" block is written three times; one helper would fix both. | **closed**, D132: a `write_ns` stage (the owner's choice); one `reread_stages` helper for the three sites |
| 25, quadratic leftovers | tool | `collect_leaves` searches a vector of seen tags per child; `restore` re-marks journal entries O(J·S). | **closed**, D132: `collect_leaves` uses the tag run, or a set past 32 children; `restore` looks up each scratch entry's edit |
| 26, the `''` tag | correctness | `Array::tag()` treats the empty symbol as no tag (D115), but lookup's `tag_of` and merge's `tag_of` treat `('' 1)` as tagged. Decide one meaning, then keep one function. | **closed**, D125: '' is no tag, everywhere |
| 27, latent desyncs | correctness | `merge_into`'s Overwrite branch rebuilds `dst` but not its spans (no caller combines them yet); an included file's nodes count twice toward `max_nodes`; `SourceMap::record_text` keeps the old text when a map is reused. | read |
| 28, duplicated code | code | `Path::parse` and `Path::matches` tokenize alike; `LoadSession`'s four special members copy the same five fields; origin and pack registration is written in both the Parser and `SessionAccess`; `format_double` and its fallback normalize the decimal point twice; the arity message is built five times in baker::script; `cache.cpp`'s `nodes_at` re-derives `Array::run_bytes`. | read |
| 29, methods' status | tool | A method's `Status` and replacement are stored, but the reference prints only functions' status, and checks use only `Stub`. Print it, or drop the replacement. | **closed**, D133: printed, through one `status_text` for functions and methods |
| 30, build time | build | doctest's implementation is compiled twice (`doctest_main.cpp`, `tests/alloc/main.cpp`, about 6.5 s each at `-O3`), `alloc_counter.cpp` seven times, and `bench_call.cpp` takes 35 s (128 bound types). An OBJECT library for each, and a runtime method count in the bench, would cut them. | reproduced (timed) |
| 31, a Config's merged trees | load | Each merge builds the Config's tree as solo arrays from the default allocator (D61, D64): about 50 blocks in `baker_alloc_tests`' Config scenario, of 215 default allocations. A Config arena for its merged root would put them on the Config's own heap and free them in one go. That changes D64's ownership, so it needs the owner's decision. | **closed**, D127: merge shares identical arrays and reuses its scratch; 9,067 allocations a merge to 8, trees stay solo |
| 32, promote and duplicate tags | correctness | A layer with two children of one tag reads, through the Config, as the later one (merge folds the duplicate in), but `promote` writes the first (`collect_leaves` keeps each tag's first child, as `find()` would). Promoting `(k 1) (k 9)` writes 1 where the Config read 9. Found writing D132's test; which meaning promote should take is the owner's call. | reproduced (test) |

## Already aligned (keep it that way)

- `Node` is 16 bytes and `Array` is a 24-byte header with its children inline, both pinned
  by `static_assert`.
- Function, variable, object-name, and class lookup go through flat per-Context tables
  indexed by `Symbol::index()` (D44). No hashing on dispatch.
- Lazy evaluation means no temporary argument lists and no allocation per call. Calls,
  methods, messages, task steps, and `get`/`set` are proven allocation-free by
  `alloc_counter` tests; `baker_alloc_tests` fails if a Context or Config reaches
  `operator new`.
- `Tweak<float>` reads compile to one relaxed load, checked by a codegen test.
- No RTTI is needed (D49), and `BAKER_SHIPPING` builds run in CI. (Exceptions are
  gone entirely: D54, D59.)
- Object references are salted handles into a flat slot table (D4, D19), not pointers.
- Every performance decision carries before/after numbers from the same machine (D47,
  D51, D52, D77, D78).
- Tools share the core's libraries, and cost a game nothing it does not call: no tool adds
  a field to a hot structure, runtime state for tools compiles out under
  `BAKER_SHIPPING`, and a shipped game links 0 bytes of `check`, JSON, the reference, or
  `find_repeats`, and 4.8 KB of tooling with `--gc-sections` (D100). Flag a hot struct
  that gains a field only tools use, or a tool the runtime starts to reference.

## Closed gaps

One line each; the D-entries hold the measurements.

1. **Allocator hook (load, hot).** Every table takes its owner's allocator, and a load
   builds in one arena (D61, D62, D64, D65).
2. **Hot and cold fields (hot), functions.** `Function`'s call fields come first, about
   one cache line; docs and overload lists follow (D78). Methods: gap 2m below.
3. **Dispatch loads (hot), functions.** A single-overload call reads the thunk and
   callable from the record, without the overload list (D78): `{add 1 2}` about 31 ns,
   no allocation.
2b. **Tag lookup (hot, load).** Each array keeps its children's tags in a run of 4-byte
   keys beside its nodes (D77): `find_float` on 20 children about 9 ns warm, 138–187 ns
   cold, from 224–293.
4. **One item at a time (hot).** `handle_all` sends a typed message to a span of
   objects, one lookup per run of a type (D66): 6.7 ns an object.
5. **Per-array refcounts (load, hot).** One count per load (D64, D65).
7. **Coroutine frames (hot).** Frames come from a per-Scheduler pool with a frame
   budget; a tick with 10,000 tasks asleep costs 5–7 ns (D58, D68).
8. **Exceptions.** None anywhere; every fallible call returns `Expected` (D54, D59).
9. **`Error` size.** `Error` is an 8-byte handle; `Expected<Node>` is 24 bytes (D55,
   D60).
11. **The STL in public headers.** Public headers reach only D55's allowed standard
   headers, checked by `tools/check_headers.py`; `std_interop.hpp` and `std_script.hpp`
   are opt-in (D60, D62).
12. **The reload history under polling (load, tool).** A host polling four times a second
   filled M3b's 256-record ring with unchanged checks in a minute; loop statistics keep
   their own 14 KB table, fed only by reloads that changed a file (D90 Q1, D92).
2m. **Method entries (hot).** A type's 120-byte `MethodEntry`s are binary-searched. With 32
   methods the stride costs a cold call 1.6 D1 misses; names in their own array would save
   those and 9 instructions, invisible in timings, so accepted (D100). Reopen if a game's
   profile names `TypeRecord::method`.
6. **Objects carry a vtable and a side table (hot).** Loose parameters come from the
   Context's allocator and leave with the object (D98). The vtable costs a cold message
   about 26 instructions and 5 ns; removing it changes how every game object is declared
   and destroyed, so accepted (D100). `handle_all` takes the caller's span: Baker never
   iterates objects itself.
10. **Tooling in the core (tool).** Accepted as a standing rule, now under "Already
   aligned": a minimal shipping game links 41.5 KB of tooling with default flags and 4.8
   KB with `--gc-sections`, which the cookbook documents (D100).

