# Moira under the data-oriented lens: known gaps

**Status, 2026-10-13, after D100: no open gaps.** First audited 2026-10-11 at M6 step 1
(`23f4142`); the audit's reasoning for each closed gap is in its D-entries. Sizes are GCC
13 on x86-64. Each gap names its path class (SKILL.md, review step 1). When one is fixed,
or accepted as a deviation, record it in `docs/decisions.md` and update this file.

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
| 11, the STL in public headers | header-only vocabulary types only (D60); `MOIRA_USE_EASTL` inside (D62) | **done** |
| 12, the reload history under polling | loop statistics keep their own samples (D90 Q1, D92) | **done** |

## Already aligned (keep it that way)

- `Node` is 16 bytes and `Array` is a 24-byte header with its children inline, both pinned
  by `static_assert`.
- Function, variable, object-name, and class lookup go through flat per-Context tables
  indexed by `Symbol::index()` (D44). No hashing on dispatch.
- Lazy evaluation means no temporary argument lists and no allocation per call. Calls,
  methods, messages, task steps, and `get`/`set` are proven allocation-free by
  `alloc_counter` tests; `moira_alloc_tests` fails if a Context or Config reaches
  `operator new`.
- `Tweak<float>` reads compile to one relaxed load, checked by a codegen test.
- No RTTI is needed (D49), and `MOIRA_SHIPPING` builds run in CI. (Exceptions are
  gone entirely: D54, D59.)
- Object references are salted handles into a flat slot table (D4, D19), not pointers.
- Every performance decision carries before/after numbers from the same machine (D47,
  D51, D52, D77, D78).
- Tools share the core's libraries, and cost a game nothing it does not call: no tool adds
  a field to a hot structure, runtime state for tools compiles out under
  `MOIRA_SHIPPING`, and a shipped game links 0 bytes of `check`, JSON, the reference, or
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
   and destroyed, so accepted (D100). `handle_all` takes the caller's span: Moira never
   iterates objects itself.
10. **Tooling in the core (tool).** Accepted as a standing rule, now under "Already
   aligned": a minimal shipping game links 41.5 KB of tooling with default flags and 4.8
   KB with `--gc-sections`, which the cookbook documents (D100).

